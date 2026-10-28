"""ELF-byte policy tests. Fixtures are data, never loaded or executed."""
import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location('elf_audit', Path(__file__).resolve().parents[1]/'cross/verify_elf.py')
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


def fixture(extra=None, flags=0x70001007, machine=8, kind=2, program=1,
            relocation=False, gp_type=1, gp_value=0, defined_gp=True):
    names = b'\0_gp\0_gp_disp\0'
    symbols = b'\0'*16
    if defined_gp:
        symbols += struct.pack('<IIIBBH', 1, 0x428000, 0, 0, 0, 0xfff1)
    symbols += struct.pack('<IIIBBH', 5, gp_value, 0, 0x10 | gp_type, 0, 0)
    if extra:
        name, binding = extra
        offset = len(names)
        names += name.encode()+b'\0'
        symbols += struct.pack('<IIIBBH', offset, 0, 0, binding << 4 | 2, 0, 0)
    string_offset = 84
    symbol_offset = string_offset+len(names)
    relocation_offset = symbol_offset+len(symbols)
    shoff = relocation_offset+(8 if relocation else 0)
    sections = [(0,)*10,
                (0, 3, 0, 0, string_offset, len(names), 0, 0, 1, 0),
                (0, 2, 0, 0, symbol_offset, len(symbols), 1, 1, 4, 16)]
    if relocation:
        sections.append((0, 9, 0, 0, relocation_offset, 8, 2, 0, 4, 8))
    ident = b'\x7fELF\x01\x01\x01'+b'\0'*9
    header = ident+struct.pack('<HHIIIIIHHHHHH', kind, machine, 1, 0x400000, 52,
                              shoff, flags, 52, 32, 1, 40, len(sections), 0)
    return (header+struct.pack('<8I', program, 0, 0, 0, 0, 0, 5, 4)+names+symbols+
            (b'\0'*8 if relocation else b'')+
            b''.join(struct.pack('<10I', *s) for s in sections))


class ElfPolicy(unittest.TestCase):
    def test_exact_mips_linker_residue_allowed(self):
        result = audit.static_symbol_audit(fixture())
        self.assertTrue(result['ok'], result)
        self.assertEqual(result['allowed_undefined'], ['_gp_disp'])

    def test_other_real_undefined_symbols_rejected(self):
        for name in ('malloc_missing', '__lshrdi3', '_gp_disp_typo', 'socket'):
            with self.subTest(name=name):
                result = audit.static_symbol_audit(fixture(extra=(name, 1)))
                self.assertFalse(result['ok'])
                self.assertIn(name, result['unexpected_undefined'])

    def test_undefined_weak_function_rejected(self):
        self.assertFalse(audit.static_symbol_audit(fixture(extra=('weak_missing', 2)))['ok'])

    def test_wrong_architecture_abi_or_file_type_rejected(self):
        for options in ({'machine': 40}, {'kind': 1}, {'kind': 3},
                        {'flags': 0x80000000}, {'flags': 0x70002000}):
            with self.subTest(options=options):
                self.assertFalse(audit.static_symbol_audit(fixture(**options))['ok'])

    def test_dynamic_segments_rejected(self):
        for program in (2, 3):
            self.assertFalse(audit.static_symbol_audit(fixture(program=program))['ok'])

    def test_relocation_residue_rejected(self):
        result = audit.static_symbol_audit(fixture(relocation=True))
        self.assertFalse(result['ok'])
        self.assertFalse(result['context']['no_remaining_relocations'])

    def test_gp_disp_must_be_zero_sized_global_object(self):
        for options in ({'gp_type': 2}, {'gp_type': 0}, {'gp_value': 1}, {'defined_gp': False}):
            self.assertFalse(audit.static_symbol_audit(fixture(**options))['ok'])

    def test_truncated_and_wrong_endian_elf_rejected(self):
        data = fixture()
        for bad in (b'', data[:51], data[:-1], data[:5]+b'\x02'+data[6:]):
            self.assertFalse(audit.static_symbol_audit(bad)['ok'])