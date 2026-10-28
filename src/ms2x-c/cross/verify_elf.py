#!/usr/bin/env python3
"""Static metadata audit only: never load or execute the target ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

FORBIDDEN = {'socket', 'socketpair', 'connect', 'bind', 'listen', 'accept', 'accept4',
             'send', 'sendto', 'sendmsg', 'recv', 'recvfrom', 'recvmsg', 'ioctl',
             'system', 'execve', 'execvp', 'posix_spawn', 'dlopen', 'popen',
             'tcgetattr', 'tcsetattr'}


def static_symbol_audit(data):
    """Accept only the observed inert MIPS o32 _gp_disp symbol-table residue.

    Inspect ELF bytes, not just an nm name whitelist. Relocations and dynamic
    loading are forbidden here; all other named UND symbols, including weak
    ones, fail. This is a strict policy for this static MIPS32r2 build only.
    """
    result = {'ok': False, 'allowed_undefined': [], 'unexpected_undefined': [],
              'undefined_symbols': [], 'context': {}}
    try:
        if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
            raise ValueError('expected ELF32 little-endian')
        fields = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
        kind, machine, _, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, _ = fields

        def bounded(offset, size):
            if offset < 0 or size < 0 or offset+size > len(data):
                raise ValueError('ELF table outside file')

        if ehsize != 52 or shsize != 40 or not shnum or (phnum and phsize != 32):
            raise ValueError('unsupported ELF table layout')
        bounded(shoff, shsize*shnum)
        bounded(phoff, phsize*phnum)
        programs = [struct.unpack_from('<8I', data, phoff+i*phsize) for i in range(phnum)]
        sections = [struct.unpack_from('<10I', data, shoff+i*shsize) for i in range(shnum)]
        symbols = []
        for section in sections:
            if section[1] != 2:  # SHT_SYMTAB; dynamic symbol tables are forbidden below.
                continue
            offset, size, link, entsize = section[4], section[5], section[6], section[9]
            if entsize != 16 or size % 16 or link >= shnum or sections[link][1] != 3:
                raise ValueError('invalid symbol/string table')
            bounded(offset, size)
            strings = sections[link]
            bounded(strings[4], strings[5])
            names = data[strings[4]:strings[4]+strings[5]]
            for at in range(offset, offset+size, 16):
                name, value, length, info, other, index = struct.unpack_from('<IIIBBH', data, at)
                if name >= len(names):
                    raise ValueError('invalid symbol name')
                end = names.find(b'\0', name)
                if end < 0:
                    raise ValueError('unterminated symbol name')
                text = names[name:end].decode('utf-8', errors='surrogateescape')
                if text:
                    symbols.append({'name': text, 'value': value, 'size': length,
                                    'type': info & 15, 'binding': info >> 4,
                                    'other': other, 'section': index})
        undefined = [symbol for symbol in symbols if symbol['section'] == 0]
        context = {
            'mips32r2_o32_exec': kind == 2 and machine == 8 and entry != 0 and
                                flags & 0xf000 == 0x1000 and flags & 0xf0000000 == 0x70000000,
            'no_dynamic_loader_or_segment': not any(p[0] in (2, 3) for p in programs),
            'no_dynamic_tables': not any(s[1] in (6, 11) for s in sections),
            'no_remaining_relocations': not any(s[1] in (4, 9) and s[5] for s in sections),
            'defined_gp': any(s['name'] == '_gp' and s['section'] != 0 and s['value'] != 0
                              for s in symbols),
        }
        gp = [s for s in undefined if s['name'] == '_gp_disp']
        allow_gp = (all(context.values()) and len(gp) == 1 and gp[0]['type'] == 1 and
                    gp[0]['binding'] == 1 and gp[0]['value'] == gp[0]['size'] == gp[0]['other'] == 0)
        allowed = ['_gp_disp'] if allow_gp else []
        unexpected = sorted({s['name'] for s in undefined} - set(allowed))
        result.update(context=context, undefined_symbols=undefined,
                      allowed_undefined=allowed, unexpected_undefined=unexpected,
                      ok=all(context.values()) and not unexpected)
    except (ValueError, struct.error) as error:
        result['error'] = str(error)
    return result


def capture(command, destination):
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    destination.write_text(result.stdout)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True)
    parser.add_argument('--link', choices=('static', 'dynamic'), required=True)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    build, prefix = args.build.resolve(), args.prefix
    binary = build / 'ms2x-offline'
    stripped = build / 'ms2x-offline.stripped'
    header = capture([prefix+'readelf', '-hW', str(binary)], build/'elf-header.txt')
    attributes = capture([prefix+'readelf', '-AW', str(binary)], build/'elf-attributes.txt')
    segments = capture([prefix+'readelf', '-lW', str(binary)], build/'elf-segments.txt')
    dynamic = capture([prefix+'readelf', '-dW', str(binary)], build/'elf-dynamic.txt')
    symbols_text = capture([prefix+'nm', '-a', str(binary)], build/'elf-symbols.txt')
    undefined = capture([prefix+'nm', '-u', str(binary)], build/'elf-undefined.txt')
    capture([prefix+'readelf', '-rW', str(binary)], build/'elf-relocations.txt')
    sections = capture([prefix+'size', '-A', str(binary)], build/'elf-size.txt')
    description = capture(['file', str(binary), str(stripped)], build/'file.txt')
    capture([prefix+'objcopy', '--dump-section', '.ms2x_abi='+str(build/'abi.bin'),
             str(build/'abi-probe.o'), str(build/'abi-copy.o')], build/'abi-extract.txt')
    layout = struct.unpack('<11I', (build/'abi.bin').read_bytes())
    names = ('pointer', 'size_t', 'frame', 'frame_align', 'tx', 'tx_align',
             'engine', 'engine_align', 'firmware', 'uint64_t', 'tx_sequence_offset')
    symbols = {line.split()[-1].split('@')[0] for line in
               (symbols_text+'\n'+undefined).splitlines() if line.split()}
    data = binary.read_bytes()
    # Byte checks supplement tool output and do not depend on translated labels.
    elf_header_ok = (data[:7] == b'\x7fELF\x01\x01\x01' and
                     struct.unpack_from('<H', data, 18)[0] == 8 and
                     struct.unpack_from('<H', data, 16)[0] in (2, 3))
    checks = {
        'elf32_little_mips_executable': elf_header_ok,
        'offline_cli_and_engine_symbols': {'main', 'ms2x_start', 'ms2x_sha256',
                                          'ms2x_firmware_load'} <= symbols,
        'stripped_same_identity_and_entry': stripped.read_bytes()[:28] == data[:28],
        'o32_mips32r2_soft_float': 'o32' in header and 'mips32r2' in header and
                                 'Soft float' in attributes,
        'no_forbidden_symbols': not symbols.intersection(FORBIDDEN),
        'non_executable_stack': any('GNU_STACK' in line and 'RWE' not in line
                                    for line in segments.splitlines()),
        'abi_measurement': layout[0] == layout[1] == 4 and layout[8] == 159744 + layout[0],
    }
    if args.link == 'static':
        symbol_audit = static_symbol_audit(data)
        nm_undefined = {line.split()[-1] for line in undefined.splitlines() if line.split()}
        elf_undefined = {s['name'] for s in symbol_audit['undefined_symbols']}
        checks['static_symbol_policy'] = symbol_audit['ok'] and nm_undefined == elf_undefined
        checks['static_no_interpreter_or_needed_libraries'] = (
            'INTERP' not in segments
            and '(NEEDED)' not in dynamic
            and checks['static_symbol_policy']
        )
    else:
        checks['dynamic_musl_soft_float_loader'] = 'ld-musl-mipsel-sf.so.1' in segments
        needed = [line for line in dynamic.splitlines() if '(NEEDED)' in line]
        checks['dynamic_only_libc'] = len(needed) == 1 and '[libc.so]' in needed[0]
    result = {
        'ok': all(checks.values()), 'checks': checks, 'link': args.link,
        'target_executed': False, 'abi_bytes': dict(zip(names, layout)),
        'binary': {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()},
        'stripped': {'bytes': stripped.stat().st_size,
                     'sha256': hashlib.sha256(stripped.read_bytes()).hexdigest()},
        'forbidden_symbols': sorted(symbols.intersection(FORBIDDEN)),
        'file': description.strip(), 'section_sizes': sections,
    }
    if args.link == 'static':
        result['static_symbol_audit'] = symbol_audit
    (build/'verification.json').write_text(json.dumps(result, indent=2, sort_keys=True)+'\n')
    print(json.dumps(result, indent=2, sort_keys=True))
    if not result['ok']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()