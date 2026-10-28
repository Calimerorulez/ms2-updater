"""Exercises the actual compiled C API; no devices, sockets or ARM execution.
Python's existing RAM model is a differential peer, never hardware evidence.
"""
import binascii
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import unittest

from ms2updater.codec import Frame, announcement, reply
from ms2updater.firmware import Firmware, Identity, REFERENCE_CRCS
from ms2updater.model import BootloaderModel
from ms2updater.protocol import Policy, ProtocolError
from ms2updater.transport import metadata, read_trace, replay

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
BUILD = Path(os.environ.get('MS2X_BUILD', HERE / 'build'))
FWPATH = Path(os.environ.get('MS2X_FIRMWARE', './051-ms2.bin'))
CLI = Path(os.environ.get('MS2X_CLI', BUILD / 'ms2x-offline'))
U64MAX = (1 << 64) - 1
NEW, TX_PING, WAIT_PING, TX_RESET, RESET_DELAY, TX_QUERY, BOOT_DELAY, TX_SELECT, WAIT_SELECT, TX_DATA, DATA_DELAY, TX_CRC, WAIT_CRC, TX_START, COMPLETE, ABORT = range(16)

class CFrame(C.Structure):
    _fields_ = [('id', C.c_uint32), ('dlc', C.c_uint8), ('data', C.c_uint8 * 8), ('extended', C.c_uint8)]

class Tx(C.Structure):
    _fields_ = [('frame', CFrame), ('sequence', C.c_uint64), ('owner', C.c_void_p)]

WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t)
READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t, C.POINTER(C.c_size_t), C.POINTER(C.c_uint64))
class IO(C.Structure):
    _fields_ = [('context', C.c_void_p), ('write', WRITE), ('read', READ)]

lib = C.CDLL(str(BUILD / 'libms2x.so'))
def bind(name, restype, *args):
    fn = getattr(lib, 'ms2x_' + name)
    fn.restype = restype
    fn.argtypes = args
    return fn

p = C.c_void_p
u = C.c_uint64
z = C.c_size_t
bind('firmware_load', p, C.c_char_p, C.POINTER(C.c_int))
bind('firmware_from_bytes', p, p, z, C.POINTER(C.c_int))
bind('firmware_free', None, p)
bind('firmware_block', C.c_int, p, C.c_int, p, z)
bind('firmware_bytes', C.c_int, p, p, z)
bind('firmware_major', C.c_uint, p)
bind('firmware_minor', C.c_uint, p)
bind('crc16', C.c_uint16, p, z)
bind('sha256', C.c_int, p, z, p)
bind('frame_decode', C.c_int, p, z, C.POINTER(CFrame))
bind('frame_encode', C.c_int, C.POINTER(CFrame), p, z)
bind('engine_new', p, p, u)
bind('engine_free', None, p)
for name in ('start', 'advance', 'close', 'transport_error'):
    bind(name, C.c_int, p, u)
bind('peek', C.c_int, p, C.POINTER(Tx))
bind('tx_processed', C.c_int, p, C.POINTER(Tx), u)
bind('receive', C.c_int, p, C.POINTER(CFrame), u)
bind('receive_wire', C.c_int, p, p, z, u)
for name in ('get_state', 'get_error', 'current_block', 'boot_seen'):
    bind(name, C.c_int, p)
bind('completed', C.c_uint, p)
bind('next_time', u, p)
bind('reason', C.c_char_p, p)
bind('offline_send', C.c_int, p, C.POINTER(IO), u)
bind('offline_receive', C.c_int, p, C.POINTER(IO), u)

def as_c(f):
    out = CFrame()
    wire = C.create_string_buffer(f.to_net())
    assert lib.ms2x_frame_decode(wire, 13, C.byref(out)) == 0
    return out

def as_python(f):
    return Frame(f.id, bytes(f.data[:f.dlc]), bool(f.extended))

class Session:
    def __init__(self, firmware, boot=True):
        self.e = lib.ms2x_engine_new(firmware, 5000)
        assert self.e
        self.now = 0
        self.records = []
        self.peer = BootloaderModel()
        self.responses = []
        self.boot = boot
        self.require(lib.ms2x_start(self.e, 0))
    def require(self, result):
        if result != 0:
            raise AssertionError(lib.ms2x_reason(self.e).decode())
    @property
    def state(self):
        return lib.ms2x_get_state(self.e)
    def peek(self):
        tx = Tx()
        return tx if lib.ms2x_peek(self.e, C.byref(tx)) == 1 else None
    def rx(self, frame):
        self.records.append(frame.record('rx', self.now))
        return lib.ms2x_receive(self.e, C.byref(as_c(frame)), self.now)
    def send(self, failure=False):
        tx = self.peek()
        assert tx is not None
        frame = as_python(tx.frame)
        expected = frame.to_net()
        calls = []
        @WRITE
        def write(_, wire, length):
            calls.append(C.string_at(wire, length))
            return -1 if failure or calls[-1] != expected else 0
        io = IO(None, write, READ())
        result = lib.ms2x_offline_send(self.e, C.byref(io), self.now)
        if failure:
            return result
        self.require(result)
        assert calls == [expected]
        self.records.append(frame.record('tx', self.now))
        if frame.data == Identity().uid_bytes + b'\x11':
            assert frame.can_id == 0x00364711
        else:
            responses = self.peer.handle(frame)
            if not self.boot and self.state == BOOT_DELAY:
                responses = []
            self.responses.extend(responses)
        return result
    def step(self):
        if self.responses:
            self.now += 1
            self.require(self.rx(self.responses.pop(0)))
        elif self.peek() is not None:
            self.send()
        else:
            self.now = lib.ms2x_next_time(self.e)
            assert self.now < U64MAX
            self.require(lib.ms2x_advance(self.e, self.now))
    def to(self, state):
        bound = 0
        while self.state != state:
            assert self.state not in (ABORT, COMPLETE)
            self.step()
            bound += 1
            assert bound < 60000
        return self
    def free(self):
        lib.ms2x_engine_free(self.e)
        self.e = None

class Integration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        error = C.c_int()
        cls.fw = lib.ms2x_firmware_load(os.fsencode(FWPATH), C.byref(error))
        assert cls.fw and error.value == 0
        cls.python_fw = Firmware.load(FWPATH)
        cls.artifacts = BUILD / 'test-artifacts'
        cls.artifacts.mkdir(parents=True, exist_ok=True)
    @classmethod
    def tearDownClass(cls):
        lib.ms2x_firmware_free(cls.fw)
    def session(self, state=None, boot=True):
        s = Session(self.fw, boot)
        self.addCleanup(s.free)
        return s.to(state) if state is not None else s
    def assertAbort(self, s, result, completed=0):
        self.assertNotEqual(result, 0)
        self.assertEqual(s.state, ABORT)
        self.assertEqual(lib.ms2x_completed(s.e), completed)
        self.assertIsNone(s.peek())
    def test_crc_actual_c_vectors(self):
        rng = random.Random(0x33)
        vectors = [b'', b'123456789', b'\0', b'\xff', bytes(range(256)), b'\xff' * 1024]
        vectors.extend(rng.randbytes(n) for n in (7, 8, 55, 56, 127, 128, 1024, 4096))
        for data in vectors:
            with self.subTest(length=len(data)):
                self.assertEqual(lib.ms2x_crc16(C.create_string_buffer(data), len(data)), binascii.crc_hqx(data, 0xffff))
    def test_sha_actual_c_padding_boundaries(self):
        for n in (0, 1, 3, 55, 56, 63, 64, 65, 119, 120, 127, 128, 1000, 159219):
            data = self.python_fw.data if n == 159219 else bytes(i % 256 for i in range(n))
            digest = C.create_string_buffer(32)
            self.assertEqual(lib.ms2x_sha256(C.create_string_buffer(data), n, digest), 0)
            self.assertEqual(digest.raw, hashlib.sha256(data).digest())
    def test_all_156_c_blocks_bytes_and_independent_crc(self):
        whole = C.create_string_buffer(159744)
        self.assertEqual(lib.ms2x_firmware_bytes(self.fw, whole, 159744), 0)
        self.assertEqual(whole.raw, self.python_fw.padded)
        self.assertEqual(whole.raw[159219:], b'\xff' * 525)
        crc_map = {}
        for n in self.python_fw.numbers:
            block = C.create_string_buffer(1024)
            self.assertEqual(lib.ms2x_firmware_block(self.fw, n, block, 1024), 0)
            self.assertEqual(block.raw, self.python_fw.block(n))
            crc = lib.ms2x_crc16(block, 1024)
            self.assertEqual(crc, binascii.crc_hqx(block.raw, 0xffff))
            crc_map[str(n)] = f'{crc:04X}'
            if n in REFERENCE_CRCS:
                self.assertEqual(crc, REFERENCE_CRCS[n])
        (self.artifacts / 'all-block-crcs.json').write_text(json.dumps(crc_map, indent=2) + '\n')
        self.assertEqual(lib.ms2x_firmware_major(self.fw), 5)
        self.assertEqual(lib.ms2x_firmware_minor(self.fw), 6)
    def test_signed_block_indices_and_lengths(self):
        block = C.create_string_buffer(1024)
        for n in (-2147483648, -1, 0, 3, 160, 2147483647):
            self.assertNotEqual(lib.ms2x_firmware_block(self.fw, n, block, 1024), 0)
        for size in (0, 1023, 1025):
            self.assertNotEqual(lib.ms2x_firmware_block(self.fw, 4, block, size), 0)
    def test_corrupt_firmware_header_size_hash(self):
        original = self.python_fw.data
        cases = [original[:-1], original+b'\xff', original[:100]+bytes([original[100]^1])+original[101:]]
        for offset in range(4):
            altered = bytearray(original)
            altered[offset] ^= 1
            cases.append(bytes(altered))
        for data in cases:
            error = C.c_int()
            self.assertFalse(lib.ms2x_firmware_from_bytes(C.create_string_buffer(data), len(data), C.byref(error)))
            self.assertEqual(error.value, 2)
    def test_codec_exact_envelope_all_dlcs_and_hashes(self):
        for dlc in range(9):
            for id_value in (0, 0x00364711, 0x0037beef, 0x1fffffff):
                f = Frame(id_value, bytes(range(dlc)))
                cframe = as_c(f)
                out = C.create_string_buffer(13)
                self.assertEqual(lib.ms2x_frame_encode(C.byref(cframe), out, 13), 0)
                self.assertEqual(out.raw, f.to_net())
    def test_incomplete_envelopes_and_invalid_dlc(self):
        original = announcement(Identity()).to_net()
        for data in [original[:i] for i in range(13)] + [original+b'\0', original[:4]+b'\x09'+original[5:], b'\x80'+original[1:]]:
            s = self.session(WAIT_PING)
            self.assertAbort(s, lib.ms2x_receive_wire(s.e, C.create_string_buffer(data), len(data), s.now))
    def test_wrong_uid_every_byte_ping_and_crc(self):
        for phase in (WAIT_PING, WAIT_CRC):
            for index in range(4):
                s = self.session(phase)
                f = announcement(Identity()) if phase == WAIT_PING else reply(Identity(), 0x88)
                data = bytearray(f.data)
                data[index] ^= 1
                self.assertAbort(s, s.rx(Frame(f.can_id, bytes(data))))
    def test_wrong_hardware_checks_both_bytes_ping_and_boot(self):
        for phase in (WAIT_PING, BOOT_DELAY):
            for hardware in (0x32, 0x34, 0x133, 0xffff):
                s = self.session(phase)
                self.assertAbort(s, s.rx(announcement(Identity(hardware=hardware), boot=phase == BOOT_DELAY)))
    def test_both_500_ms_delays_and_burst_pacing(self):
        s = self.session(RESET_DELAY)
        first = s.now
        self.assertEqual(lib.ms2x_next_time(s.e), first+500)
        self.assertEqual(lib.ms2x_advance(s.e, first+499), 0)
        self.assertEqual(s.state, RESET_DELAY)
        s.to(BOOT_DELAY)
        second = s.now
        self.assertEqual(lib.ms2x_next_time(s.e), second+500)
        s.step()  # boot identity
        self.assertEqual(s.state, BOOT_DELAY)
        self.assertEqual(lib.ms2x_next_time(s.e), second+500)
        s.to(TX_SELECT)
        self.assertEqual(s.now, second+500)
        s.to(TX_DATA)
        start = s.now
        s.send()
        self.assertEqual(s.state, DATA_DELAY)
        self.assertEqual(lib.ms2x_next_time(s.e), start+10)
        s.to(TX_CRC)
        self.assertEqual(s.now, start+260)
        parts = [r for r in s.records if 0x00360300 <= int(r['id'],16) < 0x00360380]
        self.assertEqual(len(parts),128)
        self.assertEqual([r['t_ms'] for r in parts[1:6]], [start+10]*5)
    def test_cs3_timed_boot_without_reply_explicitly_not_readiness_proof(self):
        s = self.session(TX_SELECT, boot=False)
        self.assertEqual(lib.ms2x_boot_seen(s.e), 0)
        self.assertEqual(s.peek().frame.data[5], 159)
        # This checks CS3's timed software behavior, not a ready0033 bootloader.
    def test_boot_reply_does_not_advance_selector_or_extend_deadline(self):
        s = self.session(BOOT_DELAY)
        query_time = s.now
        s.step()
        self.assertEqual(s.state, BOOT_DELAY)
        self.assertIsNone(s.peek())
        self.assertAbort(s, lib.ms2x_advance(s.e, query_time+5500))
        self.assertEqual(lib.ms2x_get_error(s.e), 6)
    def test_crc_ack_before_selector_data_during_data_and_before_crc_receipt(self):
        for phase in (TX_PING, WAIT_PING, TX_RESET, RESET_DELAY, TX_QUERY, BOOT_DELAY, TX_SELECT, WAIT_SELECT, TX_DATA, DATA_DELAY, TX_CRC):
            with self.subTest(state=phase):
                s = self.session(phase)
                self.assertAbort(s, s.rx(reply(Identity(), 0x88)))
    def test_selector_ack_before_selector_tx_receipt(self):
        s = self.session(TX_SELECT)
        self.assertAbort(s, s.rx(reply(Identity(), 0x44, b'\x9f')))
    def test_duplicate_selector_ack_never_sends_other_block(self):
        s = self.session(TX_DATA)
        self.assertAbort(s, s.rx(reply(Identity(), 0x44, b'\x9f')))
    def test_wrong_block_ack(self):
        for number in (0, 3, 4, 158, 160, 255):
            s = self.session(WAIT_SELECT)
            self.assertAbort(s, s.rx(reply(Identity(), 0x44, bytes([number]))))
    def test_duplicate_crc_ack_immediate(self):
        s = self.session(WAIT_CRC)
        ack = reply(Identity(), 0x88)
        s.responses.clear()
        self.assertEqual(s.rx(ack), 0)
        self.assertEqual(s.state, TX_SELECT)
        self.assertAbort(s, s.rx(ack), 1)
    def test_delayed_crc_ack_during_next_selector_or_data_rejected(self):
        for phase in (WAIT_SELECT, TX_DATA, DATA_DELAY, TX_CRC):
            s = self.session(WAIT_CRC)
            s.step()  # normal block159 CRC ACK
            s.to(phase)
            self.assertAbort(s, s.rx(reply(Identity(), 0x88)), 1)
    def test_identical_old_crc_ack_in_later_valid_wait_is_ambiguous(self):
        s = self.session(WAIT_CRC)
        old = s.responses[0]
        s.step()
        s.to(WAIT_CRC)
        self.assertEqual(lib.ms2x_current_block(s.e), 158)
        self.assertEqual(old, s.responses[0])
        s.responses.clear()
        self.assertEqual(s.rx(old), 0)
        self.assertEqual(lib.ms2x_completed(s.e), 2)
        # Expected limitation, NOT a claim that stale replies were solved.
    def test_old_crc_ack_in_last_valid_wait_can_request_start_limitation(self):
        s = self.session(WAIT_CRC)
        old = s.responses[0]
        while lib.ms2x_current_block(s.e) != 4:
            s.step()
            s.to(WAIT_CRC)
        self.assertEqual(lib.ms2x_completed(s.e), 155)
        s.responses.clear()
        self.assertEqual(s.rx(old), 0)
        self.assertEqual(s.state, TX_START)
        self.assertEqual(lib.ms2x_completed(s.e), 156)
        self.assertNotEqual(s.state, COMPLETE)
    def test_nacks_all_supported_codes_any_sender_hash_no_retry(self):
        for code in range(0xf1, 0xf7):
            for extra in (b'', b'\x01'):
                s = self.session(WAIT_CRC)
                self.assertAbort(s, s.rx(reply(Identity(), code, extra, sender_hash=0xbeef)))
    def test_unknown_status_command_extra_length_and_standard_frame(self):
        cases = [reply(Identity(),0x99), reply(Identity(),0x88,b'\0'), Frame(0x00310000, Identity().uid_bytes+b'\x88')]
        for f in cases:
            s = self.session(WAIT_CRC)
            self.assertAbort(s, s.rx(f))
        s = self.session(WAIT_CRC)
        f = as_c(reply(Identity(),0x88)); f.extended = 0
        self.assertAbort(s, lib.ms2x_receive(s.e,C.byref(f),s.now))
    def test_sender_hash_is_not_invented_ack_correlation(self):
        s = self.session(WAIT_CRC)
        self.assertEqual(s.rx(reply(Identity(),0x88,sender_hash=0xbeef)),0)
        self.assertEqual(lib.ms2x_completed(s.e),1)
    def test_timeout_every_protocol_phase(self):
        for phase in range(TX_PING, TX_START+1):
            with self.subTest(state=phase):
                s = self.session(phase)
                completed = lib.ms2x_completed(s.e)
                self.assertAbort(s,lib.ms2x_advance(s.e,s.now+6000),completed)
                self.assertEqual(lib.ms2x_get_error(s.e),6)
    def test_timeout_exact_deadline_and_late_ack_cannot_resume(self):
        s = self.session(WAIT_CRC)
        deadline = lib.ms2x_next_time(s.e)
        s.now = deadline
        self.assertAbort(s,s.rx(reply(Identity(),0x88)))
        self.assertEqual(lib.ms2x_get_error(s.e),6)
        s.now += 1
        self.assertAbort(s,s.rx(reply(Identity(),0x88)))
    def test_irrelevant_traffic_cannot_refresh_timeout(self):
        s = self.session(WAIT_CRC)
        s.now += 1
        self.assertAbort(s,s.rx(Frame(0x00310000,b'12345678')))
        self.assertEqual(lib.ms2x_next_time(s.e),U64MAX)
    def test_unprocessed_burst_has_absolute_deadline(self):
        s = self.session(TX_DATA)
        start = s.now
        for _ in range(3):
            s.now += 1000
            if s.state == DATA_DELAY:
                s.require(lib.ms2x_advance(s.e,s.now))
            s.send()
        self.assertEqual(lib.ms2x_next_time(s.e),start+5000)
        self.assertAbort(s,lib.ms2x_advance(s.e,start+5000))
    def test_clock_reverse_overflow_and_invalid_timeout(self):
        s = self.session()
        self.assertEqual(lib.ms2x_advance(s.e,1),0)
        self.assertAbort(s,lib.ms2x_advance(s.e,0))
        self.assertFalse(lib.ms2x_engine_new(self.fw,0))
        self.assertFalse(lib.ms2x_engine_new(self.fw,U64MAX))
        e = lib.ms2x_engine_new(self.fw,5000)
        try:
            self.assertNotEqual(lib.ms2x_start(e,U64MAX-1),0)
            self.assertEqual(lib.ms2x_get_error(e),7)
        finally:
            lib.ms2x_engine_free(e)
    def test_local_receipt_duplicate_wrong_sequence_owner_and_frame(self):
        for modification in ('duplicate','sequence','owner','frame'):
            s = self.session()
            tx = s.peek()
            if modification == 'duplicate':
                self.assertEqual(lib.ms2x_tx_processed(s.e,C.byref(tx),0),0)
            elif modification == 'sequence': tx.sequence += 1
            elif modification == 'owner': tx.owner = None
            else: tx.frame.id ^= 1
            self.assertAbort(s,lib.ms2x_tx_processed(s.e,C.byref(tx),0))
    def test_writes_fail_in_every_tx_phase_and_stop_without_receipt(self):
        for phase in (TX_PING,TX_RESET,TX_QUERY,TX_SELECT,TX_DATA,TX_CRC,TX_START):
            s = self.session(phase)
            completed = lib.ms2x_completed(s.e)
            self.assertAbort(s,s.send(failure=True),completed)
            self.assertEqual(lib.ms2x_get_error(s.e),4)  # MS2X_IO
    def test_reads_errors_short_frames_eof_and_invalid_result(self):
        for result,length in ((-1,0),(0,0),(1,12),(1,14),(2,13)):
            s = self.session(WAIT_PING)
            @READ
            def read(_, wire, capacity, out_length, out_now):
                self.assertEqual(capacity,13)
                data = announcement(Identity()).to_net()
                C.memmove(wire,data,13)
                out_length[0] = length
                out_now[0] = s.now
                return result
            io = IO(None,WRITE(),read)
            self.assertAbort(s,lib.ms2x_offline_receive(s.e,C.byref(io),s.now))
    def test_interrupted_transfer_every_phase_never_false_success(self):
        for phase in range(TX_PING,TX_START+1):
            s = self.session(phase)
            completed = lib.ms2x_completed(s.e)
            self.assertAbort(s,lib.ms2x_close(s.e,s.now),completed)
    def test_normal_full_c_trace_matches_python_transfer_bytes(self):
        s = self.session(COMPLETE)
        self.assertEqual(lib.ms2x_completed(s.e),156)
        self.assertEqual(s.peer.assembled,self.python_fw.padded)
        self.assertEqual(len(s.peer.blocks),156)
        historical = read_trace(ROOT/'traces/synthetic-success.jsonl')
        prefix = s.records[:-1]
        def signature(records):
            return [(r['direction'],r['id'],r['dlc'],r['data'],r['extended']) for r in records]
        self.assertEqual(signature(prefix),signature(historical[1:]))
        self.assertEqual(len(prefix),20597)
        self.assertEqual(s.records[-1]['data'],'1A2B3C4D11')
        self.assertEqual(s.records[-1]['direction'],'tx')
        replay(self.python_fw,[metadata(Policy())]+prefix)
        with self.assertRaises(ProtocolError):
            replay(self.python_fw,[metadata(Policy())]+s.records)
        self.assertEqual(lib.ms2x_close(s.e,s.now),0)
        events = ''.join(f"{r['t_ms']} {r['direction'].upper()} {Frame.from_record(r).to_net().hex()}\n" for r in s.records)
        (self.artifacts/'normal.events').write_text(events)
        (self.artifacts/'normal.jsonl').write_text('\n'.join(json.dumps(r,sort_keys=True) for r in [metadata(Policy())]+s.records)+'\n')
        stats = {'origin':'synthetic C sender + existing Python RAM model; not observed MS2',
                 'blocks':156,'padded_bytes_equal':159744,'crc_blocks_independently_compared':156,
                 'transfer_prefix_frames_equal_to_python':len(prefix),'c_total_frames':len(s.records),
                 'tx':sum(r['direction']=='tx' for r in s.records),'rx':sum(r['direction']=='rx' for r in s.records),
                 'end_ms':s.now,'python_prefix_replay':'PASS','python_full_replay':'rejects extra directed11 as expected',
                 'hardware_success':False,'go_no_go':'NO-GO'}
        (self.artifacts/'comparison.json').write_text(json.dumps(stats,indent=2)+'\n')
        result = subprocess.run([str(CLI),'replay',str(FWPATH),str(self.artifacts/'normal.events')],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        cli = json.loads(result.stdout)
        self.assertEqual(cli['state'],'COMPLETE_UNVERIFIED')
        self.assertFalse(cli['hardware_success'])
        self.assertEqual(cli['tx'],20284)
        self.assertEqual(cli['rx'],314)
    def test_cli_inspect_invalid_usage_missing_or_corrupt_file(self):
        result = subprocess.run([str(CLI),'inspect',str(FWPATH)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['version'],'5.6')
        corrupt = self.artifacts/'corrupt.bin'
        corrupt.write_bytes(self.python_fw.data[:-1])
        for arguments in ([],['live'],['inspect',str(corrupt)],['inspect',str(self.artifacts/'missing')],['replay',str(FWPATH),str(self.artifacts/'missing')]):
            result = subprocess.run([str(CLI)]+arguments,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
    def test_cli_truncated_invalid_and_interrupted_events(self):
        # No normal trace prerequisite: every case starts from a valid initial ping.
        ping = Frame(0x00304711).to_net().hex()
        ident = announcement(Identity()).to_net().hex()
        for index,content in enumerate(('',f'0 TX {ping}',f'0 TX {ping}\n',f'0 XX {ping}\n',f'-1 TX {ping}\n',f'0 TX {ping} extra\n','x'*170+'\n',f'0 TX {ping}\n5000 RX {ident}\n',f'0 RX {ident}\n')):
            path = self.artifacts/f'bad-{index}.events'
            path.write_text(content)
            result = subprocess.run([str(CLI),'replay',str(FWPATH),str(path)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0,(index,result.stdout))
            self.assertEqual(json.loads(result.stdout)['state'],'ABORTED')
    def test_no_socket_or_device_symbols_in_offline_executable(self):
        forbidden = {'socket','socketpair','connect','bind','listen','accept','accept4',
                     'send','sendto','sendmsg','recv','recvfrom','recvmsg','ioctl',
                     'system','execve','execvp','posix_spawn','dlopen','popen',
                     'tcgetattr','tcsetattr'}
        targets = {CLI, CLI.parent/'test-core', BUILD/'test-core',
                   CLI.parent/'test-can-offline', BUILD/'test-can-offline',
                   BUILD/'libms2x.a', BUILD/'libms2x.so'}
        for target in sorted(targets):
            with self.subTest(binary=str(target)):
                result = subprocess.run(['nm','-u',str(target)],capture_output=True,text=True,check=True)
                symbols = {line.split()[-1].split('@')[0] for line in result.stdout.splitlines() if line.split()}
                self.assertFalse(symbols & forbidden)

if __name__ == '__main__':
    unittest.main(verbosity=2)