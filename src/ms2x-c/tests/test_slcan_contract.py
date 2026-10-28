"""Inject synthetic transport faults around the unchanged compiled C engine.

No hardware backend is provided. 'Strong' completions below exist solely in
the RAM model; their tokens and delivery are not features of real SLCAN.
"""
import ctypes as C
import json
import os
import unittest

import test_integration as reference
from slcan_ram import Echo, Item, RamLink

lib = reference.lib
pack = reference.bind('can_pack', C.c_int, C.POINTER(reference.CFrame), reference.p, reference.z)
receive = reference.bind('can_receive_memory', C.c_int, reference.p, reference.p,
                         reference.z, C.c_int, reference.u)


class ContractHarness:
    def __init__(self, session, capacity=2):
        self.s = session
        self.link = RamLink(capacity)
        self.items = {}
        self.receipts = 0

    def submit(self, now, weak_receipt=False, reject=None):
        self.s.now = now
        self.s.require(lib.ms2x_advance(self.s.e, now))
        tx = self.s.peek()
        if tx is None:
            raise AssertionError('No pending item')
        item = Item(tx.owner, tx.sequence, reference.as_python(tx.frame).to_net())
        if not self.link.offer(item, now, reject):
            return None
        self.items[item] = tx
        if weak_receipt:
            self.s.require(self.receipt(item, now))
        return item

    def receipt(self, item, now):
        self.s.now = now
        result = lib.ms2x_tx_processed(self.s.e, C.byref(self.items[item]), now)
        if result == 0:
            self.receipts += 1
        return result

    def physical(self, item, now, **fault):
        self.link.uart_accept(item, item.ascii_size, now)
        self.link.uart_drain(item, now+1)
        if not self.link.pic_enqueue(item, now+1):
            raise AssertionError('Unexpected full PIC model')
        return self.link.can_finish(item, now+2, **fault)

    def strong_receipt(self, event, now):
        # Hypothetical oracle policy only. Real SLCAN supplies no such event.
        if event is None or isinstance(event, Echo):
            return None
        if now < event.at:
            raise ValueError('Completion has not happened in model time')
        if not event.success:
            self.s.now = now
            return lib.ms2x_transport_error(self.s.e, now)
        return self.receipt(event.item, now)

    def rx(self, frame, now, local=0):
        self.s.now = now
        memory = C.create_string_buffer(16)
        if pack(C.byref(reference.as_c(frame)), memory, 16) != 0:
            raise AssertionError('Invalid model input')
        return receive(self.s.e, memory, 16, local, now)

    def full_send(self):
        item = self.submit(self.s.now)
        event = self.physical(item, self.s.now+1)
        self.s.require(self.strong_receipt(event, event.at))
        frame = reference.Frame.from_net(item.wire)
        if frame.data != reference.Identity().uid_bytes+b'\x11':
            self.s.responses.extend(self.s.peer.handle(frame))


class SlcanContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        error = C.c_int()
        cls.fw = lib.ms2x_firmware_load(os.fsencode(reference.FWPATH), C.byref(error))
        if not cls.fw or error.value:
            raise AssertionError('Pinned firmware unavailable')
        cls.addClassCleanup(lib.ms2x_firmware_free, cls.fw)
        cls.evidence = {}

    @classmethod
    def tearDownClass(cls):
        target = reference.BUILD/'test-artifacts/slcan-contract.json'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(dict(synthetic_only=True, hardware_update='NO-GO',
                                         scenarios=cls.evidence), indent=2)+'\n')

    def harness(self, state=reference.TX_PING, capacity=2):
        session = reference.Session(self.fw)
        self.addCleanup(session.free)
        session.to(state)
        return ContractHarness(session, capacity)

    def evidence_row(self, h, **facts):
        self.evidence[self._testMethodName] = dict(**h.link.summary(), receipts=h.receipts,
            state=h.s.state, error=lib.ms2x_get_error(h.s.e),
            completed=lib.ms2x_completed(h.s.e), **facts)

    def assert_abort(self, h, error, completed=0):
        self.assertEqual(h.s.state, reference.ABORT)
        self.assertEqual(lib.ms2x_get_error(h.s.e), error)
        self.assertEqual(lib.ms2x_completed(h.s.e), completed)
        self.assertIsNone(h.s.peek())

    def crc_ack(self):
        return reference.reply(reference.Identity(), 0x88)

    def last_data(self):
        h = self.harness(reference.TX_DATA)
        while h.s.peek() is None or h.s.peek().frame.id != 0x0036037f:
            h.s.step()
        self.assertEqual(len(h.s.peer.parts), 127)
        return h

    def test_acceptance_and_echo_alone_do_not_call_engine_receipt(self):
        h = self.harness()
        item = h.submit(0)
        echo = h.link.echo(item, 1)
        self.assertIsNone(h.strong_receipt(echo, 1))
        self.assertEqual(h.s.state, reference.TX_PING)
        self.assertEqual(h.s.peek().sequence, item.sequence)
        self.assertEqual(h.receipts, 0)
        self.assertEqual(h.link.transmitted, [])
        self.evidence_row(h, echo_has_sequence=hasattr(echo, 'sequence'))

    def test_kernel_acceptance_receipt_can_advance_before_any_uart_or_can(self):
        h = self.harness()
        h.submit(0, weak_receipt=True)
        self.assertEqual(h.s.state, reference.WAIT_PING)
        self.assertEqual(lib.ms2x_next_time(h.s.e), 5000)
        self.assertEqual(h.link.transmitted, [])
        self.assertEqual(h.link.drained, set())
        self.evidence_row(h, physical_delivery_proven=False)

    def test_partial_tty_writes_and_drain_are_distinct_from_can_completion(self):
        h = self.harness(reference.TX_CRC)
        base = h.s.now
        item = h.submit(base)
        h.link.echo(item, base)
        h.link.uart_accept(item, 4, base+1)
        self.assertEqual(h.s.state, reference.TX_CRC)
        self.assertEqual(h.link.queued_bytes[item], 4)
        self.assertNotIn(item, h.link.drained)
        h.link.uart_accept(item, item.ascii_size-4, base+100)
        self.assertNotIn(item, h.link.drained)
        h.link.uart_drain(item, base+200)
        self.assertEqual(h.s.state, reference.TX_CRC)
        self.assertEqual(h.link.transmitted, [])
        self.assertTrue(h.link.pic_enqueue(item, base+201))
        event = h.link.can_finish(item, base+300)
        self.assertEqual(h.strong_receipt(event, base+300), 0)
        self.assertEqual(h.s.state, reference.WAIT_CRC)
        self.assertEqual(lib.ms2x_completed(h.s.e), 0)
        self.evidence_row(h, uart_delay_ms=200, can_delay_ms=300)

    def test_uart_delay_can_exhaust_wait_budget_after_weak_acceptance(self):
        h = self.harness()
        item = h.submit(0, weak_receipt=True)
        deadline = lib.ms2x_next_time(h.s.e)
        h.physical(item, deadline)
        response = h.s.peer.handle(reference.Frame.from_net(item.wire))[0]
        self.assertNotEqual(h.rx(response, deadline+3), 0)
        self.assert_abort(h, 6)
        self.evidence_row(h, response_after_local_deadline=True)

    def test_lost_strong_completion_times_out_without_retransmission(self):
        h = self.harness(reference.TX_CRC)
        base = h.s.now
        item = h.submit(base)
        event = h.physical(item, base+1, lose_completion=True)
        self.assertIsNone(h.strong_receipt(event, base+3))
        deadline = lib.ms2x_next_time(h.s.e)
        self.assertNotEqual(lib.ms2x_advance(h.s.e, deadline), 0)
        self.assert_abort(h, 6)
        self.assertEqual(len(h.link.accepted), 1)
        self.assertEqual(len(h.link.transmitted), 1)
        self.evidence_row(h, hardware_may_have_received_despite_abort=True)

    def test_duplicate_completion_is_rejected_even_with_next_item_pending(self):
        h = self.last_data()
        base = h.s.now
        item = h.submit(base)
        event = h.physical(item, base+1)
        self.assertEqual(h.strong_receipt(event, base+3), 0)
        self.assertEqual(h.s.state, reference.TX_CRC)
        self.assertNotEqual(h.s.peek().sequence, item.sequence)
        self.assertNotEqual(h.strong_receipt(event, base+4), 0)
        self.assert_abort(h, 5)
        self.evidence_row(h, duplicate_cannot_confirm_crc=True)

    def test_full_queue_and_transient_rejections_preserve_absolute_deadline(self):
        for rejection in (None, 'EAGAIN', 'ENOBUFS'):
            with self.subTest(rejection=rejection):
                h = self.harness(capacity=1)
                pending = h.s.peek()
                filler = Item(1, 90000, reference.as_python(pending.frame).to_net())
                self.assertTrue(h.link.offer(filler, 0))
                deadline = lib.ms2x_next_time(h.s.e)
                for now in (1, 1000, 4999):
                    self.assertIsNone(h.submit(now, reject=rejection))
                    self.assertEqual(h.s.peek().sequence, pending.sequence)
                    self.assertEqual(lib.ms2x_next_time(h.s.e), deadline)
                    self.assertEqual(h.receipts, 0)
                self.assertNotEqual(lib.ms2x_advance(h.s.e, deadline), 0)
                self.assert_abort(h, 6)
        self.evidence_row(h, queue_full_and_errno_cases=3)

    def test_rejected_item_can_be_accepted_once_when_room_returns(self):
        h = self.harness(capacity=1)
        self.assertIsNone(h.submit(0, reject='EAGAIN'))
        item = h.submit(1)
        self.assertEqual(h.receipt(item, 1), 0)
        self.assertEqual(h.s.state, reference.WAIT_PING)
        with self.assertRaises(ValueError):
            h.link.offer(item, 2)
        self.assertEqual(len(h.link.accepted), 1)
        self.evidence_row(h, successful_acceptances=1)

    def test_pic_buffer_saturation_and_injected_can_failure(self):
        h = self.harness()
        h.link = RamLink(capacity=4)
        items = [Item(1, n, reference.as_python(h.s.peek().frame).to_net()) for n in range(1, 5)]
        for item in items:
            self.assertTrue(h.link.offer(item, 0))
            h.link.uart_accept(item, item.ascii_size, 0)
            h.link.uart_drain(item, 0)
        for item in items[:3]:
            self.assertTrue(h.link.pic_enqueue(item, 1))
        self.assertFalse(h.link.pic_enqueue(items[3], 1))
        failure = h.link.can_finish(items[0], 2, success=False)
        self.assertNotEqual(h.strong_receipt(failure, 2), 0)
        self.assert_abort(h, 4)
        self.assertTrue(h.link.pic_enqueue(items[3], 2))
        self.evidence_row(h, error_is_synthetic_not_a_real_pic_notification=True)

    def test_byte_identical_echoes_cannot_identify_local_sequence(self):
        h = self.harness()
        item = h.submit(0)
        other = Item(item.owner, item.sequence+1, item.wire)
        self.assertTrue(h.link.offer(other, 0))
        echo = h.link.echo(item, 1)
        matches = [i for i in h.link.accepted if i.owner == echo.owner and i.wire == echo.wire]
        self.assertEqual(matches, [item, other])
        self.assertIsNone(h.strong_receipt(echo, 1))
        self.assertEqual(h.receipts, 0)
        self.evidence_row(h, matching_echo_candidates=len(matches))

    def test_forged_owner_sequence_and_frame_cannot_create_receipts(self):
        for field in ('owner', 'sequence', 'frame'):
            with self.subTest(field=field):
                h = self.harness()
                item = h.submit(0)
                tx = h.s.peek()
                if field == 'owner':
                    tx.owner = (tx.owner or 0)+1
                elif field == 'sequence':
                    tx.sequence += 1
                else:
                    tx.frame.id ^= 1  # A ping has DLC 0; padding is not frame identity.
                self.assertNotEqual(lib.ms2x_tx_processed(h.s.e, C.byref(tx), 1), 0)
                self.assert_abort(h, 5)
        self.evidence_row(h, invalid_receipt_fields=3)

    def test_can_link_ack_does_not_supply_bootloader_ack(self):
        h = self.harness(reference.TX_CRC)
        base = h.s.now
        item = h.submit(base)
        event = h.physical(item, base+1)
        h.link.link_ack(item, base+3)
        self.assertEqual(h.strong_receipt(event, base+3), 0)
        self.assertEqual(h.s.state, reference.WAIT_CRC)
        self.assertEqual(lib.ms2x_completed(h.s.e), 0)
        self.assertNotEqual(lib.ms2x_advance(h.s.e, lib.ms2x_next_time(h.s.e)), 0)
        self.assert_abort(h, 6)
        self.evidence_row(h, link_ack_has_target_identity=False)

    def test_missing_bootloader_reply_times_out_in_ping_select_and_crc(self):
        for tx_state, wait_state in ((reference.TX_PING, reference.WAIT_PING),
                                     (reference.TX_SELECT, reference.WAIT_SELECT),
                                     (reference.TX_CRC, reference.WAIT_CRC)):
            with self.subTest(state=tx_state):
                h = self.harness(tx_state)
                base = h.s.now
                item = h.submit(base)
                event = h.physical(item, base+1)
                self.assertEqual(h.strong_receipt(event, base+3), 0)
                self.assertEqual(h.s.state, wait_state)
                replies = h.s.peer.handle(reference.Frame.from_net(item.wire))
                self.assertEqual(len(replies), 1)  # Deliberately lost in this model.
                self.assertNotEqual(lib.ms2x_advance(h.s.e, lib.ms2x_next_time(h.s.e)), 0)
                self.assert_abort(h, 6)
        self.evidence_row(h, missing_reply_phases=3)

    def test_premature_crc_reply_before_local_crc_receipt_aborts(self):
        h = self.harness(reference.TX_CRC)
        h.submit(h.s.now)
        self.assertNotEqual(h.rx(self.crc_ack(), h.s.now+1), 0)
        self.assert_abort(h, 5)
        self.evidence_row(h, accepted_crc_ack=False)

    def test_peer_reply_can_overtake_a_delayed_local_completion_callback(self):
        h = self.harness(reference.TX_CRC)
        base = h.s.now
        item = h.submit(base)
        event = h.physical(item, base+1)
        ack = h.s.peer.handle(reference.Frame.from_net(item.wire))[0]
        self.assertEqual(h.s.state, reference.TX_CRC)
        self.assertNotEqual(h.rx(ack, base+4), 0)
        self.assert_abort(h, 5)
        self.assertNotEqual(h.strong_receipt(event, base+5), 0)
        self.evidence_row(h, modeled_current_ack=True, delayed_callback_cannot_resume=True)

    def test_buffering_a_premature_reply_until_wait_crc_hides_its_age(self):
        h = self.harness(reference.TX_CRC)
        base = h.s.now
        item = h.submit(base)
        old_ack, arrival = self.crc_ack(), base+1
        event = h.physical(item, base+2)
        self.assertEqual(h.strong_receipt(event, base+4), 0)
        self.assertEqual(h.rx(old_ack, base+5), 0)
        self.assertEqual(lib.ms2x_completed(h.s.e), 1)
        self.evidence_row(h, old_ack_arrival_ms=arrival, processed_ms=base+5,
                          ambiguity_reproduced=True)

    def test_delayed_crc_reply_before_at_and_after_deadline(self):
        for offset in (-1, 0, 1):
            with self.subTest(deadline_offset=offset):
                h = self.harness(reference.TX_CRC)
                item = h.submit(h.s.now)
                event = h.physical(item, h.s.now+1)
                self.assertEqual(h.strong_receipt(event, event.at), 0)
                deadline = lib.ms2x_next_time(h.s.e)
                result = h.rx(self.crc_ack(), deadline+offset)
                if offset < 0:
                    self.assertEqual(result, 0)
                    self.assertEqual(lib.ms2x_completed(h.s.e), 1)
                else:
                    self.assertNotEqual(result, 0)
                    self.assert_abort(h, 6)
        self.evidence_row(h, deadline_offsets=[-1, 0, 1])

    def test_old_crc_ack_in_later_valid_wait_remains_indistinguishable(self):
        h = self.harness(reference.WAIT_CRC)
        old = h.s.responses[0]
        self.assertEqual(h.s.rx(h.s.responses.pop(0)), 0)
        h.s.to(reference.WAIT_CRC)
        current = h.s.responses.pop(0)
        self.assertEqual(old.to_net(), current.to_net())
        self.assertEqual(h.rx(old, h.s.now), 0)
        self.assertEqual(lib.ms2x_completed(h.s.e), 2)
        self.assertEqual(lib.ms2x_current_block(h.s.e), 157)
        self.evidence_row(h, ambiguity_reproduced=True)

    def test_interruption_after_last_data_acceptance_before_crc(self):
        h = self.last_data()
        base = h.s.now
        item = h.submit(base, weak_receipt=True)
        self.assertEqual(h.s.state, reference.TX_CRC)
        self.assertEqual(h.s.peek().frame.data[4], 0x88)
        self.assertNotEqual(lib.ms2x_transport_error(h.s.e, base+1), 0)
        self.assert_abort(h, 4)
        event = h.physical(item, base+2)  # Already accepted data can still leave after abort.
        self.assertNotEqual(h.strong_receipt(event, event.at), 0)
        self.assertEqual(len(h.link.transmitted), 1)
        self.assertEqual(h.link.transmitted[0].wire[0:4], bytes.fromhex('0036037F'))
        self.evidence_row(h, crc_accepted=False, abort_does_not_cancel_kernel_queue=True)

    def test_weak_receipts_can_queue_crc_while_last_data_has_not_left_uart(self):
        h = self.last_data()
        base = h.s.now
        data_item = h.submit(base, weak_receipt=True)
        crc_item = h.submit(base, weak_receipt=True)
        self.assertEqual(h.s.state, reference.WAIT_CRC)
        self.assertEqual(h.link.transmitted, [])
        self.assertEqual(h.link.drained, set())
        self.assertEqual(len(h.link.kernel), 2)
        # A chosen FIFO, loss-free model still succeeds; this is no driver guarantee.
        h.physical(data_item, base+1)
        self.assertEqual(h.s.peer.handle(reference.Frame.from_net(data_item.wire)), [])
        h.physical(crc_item, base+4)
        ack = h.s.peer.handle(reference.Frame.from_net(crc_item.wire))[0]
        self.assertEqual(h.rx(ack, base+7), 0)
        self.assertEqual(lib.ms2x_completed(h.s.e), 1)
        self.evidence_row(h, fifo_delivery_was_an_explicit_model_assumption=True)

    def test_strong_last_data_completion_still_does_not_commit_block(self):
        h = self.last_data()
        base = h.s.now
        item = h.submit(base)
        event = h.physical(item, base+1)
        self.assertEqual(h.strong_receipt(event, event.at), 0)
        self.assertEqual(h.s.state, reference.TX_CRC)
        self.assertEqual(lib.ms2x_completed(h.s.e), 0)
        self.assertNotEqual(lib.ms2x_transport_error(h.s.e, event.at+1), 0)
        self.assert_abort(h, 4)
        self.evidence_row(h, last_data_can_complete_without_crc=True)

    def test_complete_delayed_transfer_with_hypothetical_strong_receipts(self):
        h = self.harness()
        h.s.send = h.full_send
        h.s.rx = lambda frame: h.rx(frame, h.s.now)
        h.s.to(reference.COMPLETE)
        self.assertEqual(len(h.link.transmitted), 20284)
        self.assertEqual(h.receipts, 20284)
        policy = (C.c_uint32 * 4)()
        get_filters = reference.bind('can_filters', C.c_int, reference.p, reference.z)
        self.assertEqual(get_filters(policy, 2), 0)
        echo_matches = sum(any((int.from_bytes(item.wire[:4], 'big') | 0x80000000) & policy[i+1]
                               == policy[i] & policy[i+1] for i in (0, 2))
                           for item in h.link.transmitted)
        self.assertEqual(echo_matches, 0)  # Current RX filters cover replies, not our TX IDs.
        self.assertEqual(lib.ms2x_completed(h.s.e), 156)
        self.assertEqual(h.s.peer.assembled, reference.Firmware.load(reference.FWPATH).padded)
        self.assertEqual(lib.ms2x_close(h.s.e, h.s.now), 0)
        self.evidence_row(h, hypothetical_strong_events_only=True,
                          hardware_success=False, image_bytes=159744,
                          own_tx_echo_matches_current_filters=echo_matches)