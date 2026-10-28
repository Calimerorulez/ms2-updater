/* Standalone offline MS2 resource responder. No CAN/serial/device I/O. */
#ifndef MS2X_RESOURCES_H
#define MS2X_RESOURCES_H
#include "ms2x.h"

typedef struct ms2x_resources ms2x_resources;
typedef struct {
    ms2x_frame frame;
    uint64_t sequence;
    const ms2x_resources *owner;
} ms2x_resource_tx;

/* Trusted local directory containing the eight original catalog files.
 * One fixed requester hash for this session; queries contain no target UID.
 * Files are opened read-only on demand, never enumerated or installed.
 * Caller owns this single-threaded object; callbacks must not re-enter it.
 */
ms2x_resources *ms2x_resources_new(const char *directory, uint16_t requester_hash,
                                  uint16_t server_hash);
void ms2x_resources_free(ms2x_resources *server);
/* Logical monotone milliseconds. 1=accepted, 0=ignored/rejected, -1=error.
 * A file name selects a file without TX. A decimal query requests its part.
 * A version name queues an immediate query echo and then a short stream.
 * Busy/unknown/malformed/other-peer requests produce no wire response.
 */
int ms2x_resources_receive(ms2x_resources *server, const ms2x_frame *query, uint64_t now);
/* 1=ready item, 0=idle/pacing, -1=error; peek does not consume the item.
 * Report processed exactly once after a complete local transport write.
 * EAGAIN keeps the item/deadline; uncertain/short/failing writes require abort.
 * A processed item is NOT a CAN ACK, client receipt or installation result.
 */
int ms2x_resources_peek(ms2x_resources *server, uint64_t now, ms2x_resource_tx *item);
int ms2x_resources_tx_processed(ms2x_resources *server, const ms2x_resource_tx *item, uint64_t now);
int ms2x_resources_advance(ms2x_resources *server, uint64_t now);
void ms2x_resources_abort(ms2x_resources *server);
ms2x_error ms2x_resources_error(const ms2x_resources *server);
/* No whole-file/update success API. Idle only means no local pending stream.
 * Absolute 5000-ms budget per response, echo then50ms, at most five stream
 * records per batch then5ms. Selection alone does not start a stream timer.
 */
uint64_t ms2x_resources_next_time(const ms2x_resources *server);
#endif