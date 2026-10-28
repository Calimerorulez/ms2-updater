/* Fixed I/O seam used by the original RAM loop, now also implemented by Linux.
 * No vtable, default backend or plugin selection. One single-threaded run.
 */
#ifndef MS2X_CAN_SIM_LOOP_H
#define MS2X_CAN_SIM_LOOP_H
#include "ms2x_can_offline.h"

#define MS2X_SIM_RX 1U
#define MS2X_SIM_TX 2U
#define MS2X_SIM_ERROR 4U
#define MS2X_SIM_RX_BUDGET 8U

/* A fresh NEW engine is started here. 0 means COMPLETE_UNVERIFIED only.
 * Implementations below may not re-enter the loop.
 */
int ms2x_can_sim_run(ms2x_engine *engine);
int ms2x_sim_clock(uint64_t *now_ms);
int ms2x_sim_filters(const ms2x_can_filter *filters, size_t count);
/* Absolute monotone deadline, not a fresh inactivity timeout.
 * wait: 0=timeout, 1=events, -1=error (errno value in error).
 * read/write: byte count or -1, error contains an injected errno value.
 * EAGAIN/EWOULDBLOCK means no bytes accepted/consumed. Short writes and all
 * other write errors, including EINTR and ENOBUFS, are uncertain/fatal.
 * local_origin follows the existing memory adapter's 0/1 contract.
 */
int ms2x_sim_wait(uint64_t until_ms, int want_write, unsigned *events, int *error);
int ms2x_sim_write(const uint8_t *record, size_t length, int *error);
int ms2x_sim_read(uint8_t *record, size_t capacity, int *local_origin, int *error);
#endif