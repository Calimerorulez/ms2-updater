"""Synthetic transport stages, entirely in RAM; never a Linux/PIC emulator.

No socket, serial, device, clock or process APIs. Times, queue limits, FIFO
service, CAN outcomes and completion delivery are chosen by each test, not
measured hardware properties. Item tokens belong to the model's private
oracle: LAWICEL and MSG_CONFIRM do not supply these sequence numbers.
"""
from collections import Counter, deque
from dataclasses import dataclass


@dataclass(frozen=True)
class Item:
    owner: int
    sequence: int
    wire: bytes  # Existing 13-byte envelope, only for immutable byte identity.

    @property
    def ascii_size(self):
        return 11 + 2*self.wire[4]  # T + eight ID digits + DLC + payload + CR.


@dataclass(frozen=True)
class Echo:
    owner: int
    wire: bytes  # Socket provenance + bytes; deliberately no local sequence.


@dataclass(frozen=True)
class SyntheticCompletion:
    item: Item
    at: int
    success: bool


class RamLink:
    def __init__(self, capacity=2, controller_capacity=3):
        self.capacity = capacity
        self.controller_capacity = controller_capacity
        self.kernel = deque()
        self.serial = deque()
        self.controller = deque()
        self.driver_item = None
        self.accepted = {}
        self.queued_bytes = {}
        self.drained = set()
        self.transmitted = []
        self.finished = set()
        self.failed = set()
        self.link_acked = set()
        self.events = []
        self.now = 0

    def note(self, kind, item, now):
        if now < self.now:
            raise ValueError('Model time moved backwards')
        self.now = now
        self.events.append((kind, item.sequence, now))

    def offer(self, item, now, reject=None):
        if item in self.accepted:
            raise ValueError('No automatic retransmission of accepted items')
        if reject is not None or len(self.kernel) >= self.capacity:
            self.note(reject or 'QUEUE_FULL', item, now)
            return False
        self.accepted[item] = now
        self.kernel.append(item)
        self.serial.append(item)
        self.queued_bytes[item] = 0
        self.note('KERNEL_ACCEPTED', item, now)
        return True

    def echo(self, item, now):
        if item not in self.accepted:
            raise ValueError('No local echo before acceptance')
        self.note('LOCAL_ECHO', item, now)
        return Echo(item.owner, item.wire)

    def uart_accept(self, item, count, now):
        if self.driver_item is None:
            if not self.kernel or self.kernel[0] != item:
                raise ValueError('Synthetic driver services its queue in order')
            self.kernel.popleft()
            self.driver_item = item
        if self.driver_item != item or not 0 <= count <= item.ascii_size-self.queued_bytes[item]:
            raise ValueError('Invalid partial TTY write')
        self.queued_bytes[item] += count
        self.note('TTY_QUEUED', item, now)
        if self.queued_bytes[item] == item.ascii_size:
            self.driver_item = None

    def uart_drain(self, item, now):
        if self.queued_bytes[item] != item.ascii_size or not self.serial or self.serial[0] != item:
            raise ValueError('A complete ordered serial record is required')
        self.serial.popleft()
        self.drained.add(item)
        self.note('UART_DRAINED', item, now)

    def pic_enqueue(self, item, now):
        if item not in self.drained or item in self.controller or item in self.finished:
            raise ValueError('PIC requires a fresh drained serial record')
        if len(self.controller) >= self.controller_capacity:
            self.note('PIC_BUFFERS_FULL', item, now)
            return False
        self.controller.append(item)
        self.note('PIC_TXREQ', item, now)
        return True

    def can_finish(self, item, now, success=True, lose_completion=False):
        if not self.controller or self.controller[0] != item:
            raise ValueError('FIFO CAN service is a model choice, not a PIC guarantee')
        self.controller.popleft()
        self.finished.add(item)
        if success:
            self.transmitted.append(item)
        else:
            self.failed.add(item)
        self.note('CAN_TX_DONE' if success else 'CAN_TX_FAILED', item, now)
        if lose_completion:
            self.note('COMPLETION_LOST', item, now)
            return None
        return SyntheticCompletion(item, now, success)

    def link_ack(self, item, now):
        if item not in self.transmitted:
            raise ValueError('No CAN link ACK before modeled CAN success')
        self.link_acked.add(item)
        self.note('CAN_LINK_ACK', item, now)

    def summary(self):
        return dict(events=dict(Counter(e[0] for e in self.events)),
                    accepted=len(self.accepted), transmitted=len(self.transmitted),
                    failed=len(self.failed), kernel_pending=len(self.kernel),
                    serial_pending=len(self.serial), controller_pending=len(self.controller))