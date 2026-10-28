"""Drive every RX in a complete C/Python RAM transfer through the memory adapter."""
import ctypes as C
import os
import unittest

import test_integration as reference

lib = reference.lib
pack = reference.bind('can_pack', C.c_int, C.POINTER(reference.CFrame), reference.p, reference.z)
receive = reference.bind('can_receive_memory', C.c_int, reference.p, reference.p,
                         reference.z, C.c_int, reference.u)


class CanMemoryTrace(unittest.TestCase):
    def test_full_transfer_with_other_uid_and_local_echo_traffic(self):
        error = C.c_int()
        firmware = lib.ms2x_firmware_load(os.fsencode(reference.FWPATH), C.byref(error))
        self.assertTrue(firmware)
        self.addCleanup(lib.ms2x_firmware_free, firmware)
        session = reference.Session(firmware)
        self.addCleanup(session.free)
        ignored = 0

        def rx(frame):
            nonlocal ignored
            record = C.create_string_buffer(16)
            self.assertEqual(pack(C.byref(reference.as_c(frame)), record, 16), 0)
            deadline = lib.ms2x_next_time(session.e)
            wrong_uid = bytearray(record.raw)
            wrong_uid[8] ^= 1
            self.assertEqual(receive(session.e, C.create_string_buffer(bytes(wrong_uid)),
                                     16, 0, session.now), 0)
            self.assertEqual(receive(session.e, record, 16, 1, session.now), 0)
            self.assertEqual(lib.ms2x_next_time(session.e), deadline)
            ignored += 2
            session.records.append(frame.record('rx', session.now))
            return receive(session.e, record, 16, 0, session.now)

        session.rx = rx
        session.to(reference.COMPLETE)
        expected = reference.Firmware.load(reference.FWPATH)
        self.assertEqual(session.peer.assembled, expected.padded)
        self.assertEqual(lib.ms2x_completed(session.e), 156)
        self.assertEqual(len(session.peer.blocks), 156)
        self.assertEqual(ignored, 628)
        self.assertEqual(lib.ms2x_close(session.e, session.now), 0)