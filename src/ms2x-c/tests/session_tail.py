"""Only reference bytes/timestamps, using the existing offline resource prototype."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'reports/evidence/phase18'))
from resource_stream_offline import file_chunk, stream_frames
from ms2updater.codec import Frame

rows = [(1, 'RX', Frame(0x00319b22, bytes.fromhex('1a2b3c4d05060033'))),
        (2, 'RX', Frame(0x00401234, b'langver\0'))]
request = Frame(0x00409b22, b'langver\0')
rows.append((10, 'RX', request))
for i, frame in enumerate(stream_frames(request, b' .vhigh=5\n .vlow=4\n .bytes=61767\n', 0x4711)):
    rows.append((10 + (0 if i == 0 else 50 + 5*((i-1)//5)), 'TX', frame))
rows.append((100, 'RX', Frame(0x00409b22, b'mfxbin\0\0')))
request = Frame(0x00409b22, b'000\0\0\0\0\0')
rows.append((110, 'RX', request))
data = (Path(sys.argv[1]) / 'mfxdefs.bin').read_bytes()
for i, frame in enumerate(stream_frames(request, file_chunk(data, 0), 0x4711)):
    rows.append((110 + (0 if i == 0 else 50 + 5*((i-1)//5)), 'TX', frame))
Path(sys.argv[2]).write_text(''.join(f'{at} {direction} {frame.to_net().hex()}\n' for at,direction,frame in rows))

# Independent resource-only reference, same existing stream/CRC helper.
# Probe hash4711 remains distinct from resource-server hash5A21.
rows = [(0, 'TX', Frame(0x00304711, b'')),
        (1, 'RX', Frame(0x00319b22, bytes.fromhex('1234abcd03700033'))),
        (5000, 'TX', Frame(0x00315a21, bytes.fromhex('43532d322020ffff'))),
        (5002, 'RX', Frame(0x00401234, b'langver\0'))]
catalog = [('lang.ms2', 'lang', 'langver', 0), ('flashdb.ms2', 'lokdb', 'ldbver', None),
           ('mfxdefs.ms2', 'mfxdefs', 'mfxver', None), ('mfxdefs.bin', 'mfxbin', 'mfxbver', 0),
           ('050-ms2.bin', 'ms2', 'ms2ver', 252), ('051-ms2.bin', 'ms2x', 'ms2xver', 0),
           ('052-ms2.bin', 'ms2y', 'ms2yver', 0), ('016-gb2.bin', 'gb2', 'gb2ver', 6)]
for index, (filename, name, version, offset) in enumerate(catalog):
    data = (Path(sys.argv[1]) / filename).read_bytes()
    if filename == 'flashdb.ms2':
        values = dict(zip(('version', 'monat', 'jahr', 'anzahl'),
                          (int.from_bytes(data[i:i+2], 'little') for i in range(0, 8, 2))))
    elif filename == 'mfxdefs.ms2':
        values = {'vhigh': 0, 'vlow': 3, 'bytes': len(data)}  # Official text fixture.
    else:
        values = {'vhigh': data[offset], 'vlow': data[offset+1], 'bytes': len(data)}
    text = ''.join(f' .{key}={value}\n' for key, value in values.items() if value).encode()
    at = 5010 + 500*index
    for when, query, payload in [(at, version, text), (at+110, '000', file_chunk(data, 0))]:
        if query == '000':
            rows.append((when-10, 'RX', Frame(0x00409b22, name.encode().ljust(8, b'\0'))))
        request = Frame(0x00409b22, query.encode().ljust(8, b'\0'))
        rows.append((when, 'RX', request))
        for i, frame in enumerate(stream_frames(request, payload, 0x5a21)):
            rows.append((when + (0 if i == 0 else 50 + 5*((i-1)//5)), 'TX', frame))
Path(sys.argv[2]).with_name('resource-service.events').write_text(''.join(
    f'{at} {direction} {frame.to_net().hex()}\n' for at, direction, frame in rows))