/* Pure memory adapter for Linux classical CAN records; no interface APIs. */
#ifndef MS2X_CAN_OFFLINE_H
#define MS2X_CAN_OFFLINE_H
#include "ms2x.h"

#define MS2X_CAN_RECORD_SIZE 16U
#define MS2X_CAN_EFF UINT32_C(0x80000000)
#define MS2X_CAN_RTR UINT32_C(0x40000000)
#define MS2X_CAN_ERR UINT32_C(0x20000000)
typedef struct { uint32_t id, mask; } ms2x_can_filter;
enum { MS2X_CAN_INVALID = -1, MS2X_CAN_IGNORE = 0, MS2X_CAN_TARGET = 1 };

/* can_id is native-endian, length at byte4, data at byte8; exactly 16 bytes.
 * This is not the 13-byte big-endian network envelope. No raw struct casts.
 */
int ms2x_can_pack(const ms2x_frame *frame, uint8_t *record, size_t length);
/* Generic decoding of the same memory record;1=extended classical frame.
 * Same error/local/RTR/SFF policy as classify(), without command/UID filtering. */
int ms2x_can_unpack(const uint8_t *record, size_t length, int local_origin, ms2x_frame *frame);
int ms2x_can_classify(const uint8_t *record, size_t length, int local_origin,
                      ms2x_frame *frame);
int ms2x_can_classify_target(const uint8_t *record, size_t length, int local_origin,
                             uint32_t uid, ms2x_frame *frame);
/* Two ID filters; target UID and all protocol-state validation stay in userspace. */
int ms2x_can_filters(ms2x_can_filter *filters, size_t count);
/* now is caller-supplied logical monotone ms. Check deadlines before every
 * record, including ignored traffic. Error frames/short reads/FD are failures.
 * local_origin is 0/1, supplied by a future driver; no socket is opened here.
 */
int ms2x_can_receive_memory(ms2x_engine *engine, const uint8_t *record,
                           size_t length, int local_origin, uint64_t now);
#endif