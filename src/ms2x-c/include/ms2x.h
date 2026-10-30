/* MS2 protocol API. Transport and live release policy remain separate. */
#ifndef MS2X_H
#define MS2X_H
#include <stddef.h>
#include <stdint.h>

#define MS2X_UID UINT32_C(0x1a2b3c4d) /* Historical offline fixture, not device discovery. */
#define MS2X_HARDWARE UINT16_C(0x0033)
#define MS2X_IMAGE_SIZE ((size_t)159219)
#define MS2X_PADDED_SIZE ((size_t)159744)
#define MS2X_BLOCK_SIZE ((size_t)1024)
#define MS2X_BLOCKS 156U
#define MS2X_FIRST_BLOCK 159
#define MS2X_LAST_BLOCK 4
#define MS2X_HASH UINT16_C(0x4711) /* Gerd constant, not a measured CS3 hash. */
#define MS2X_DELAY_MS UINT64_C(500)
#define MS2X_TIMEOUT_MS UINT64_C(5000)
#define MS2X_SHA256_HEX "cf2d277e38f6f982ffd3b40dd1c372c594cd8b8592fb5234f3b4da6c6ccd2439"
#define MS2X_DEFAULT_FIRMWARE "./051-ms2.bin"

/* Sizes/offsets are profile facts, not fields added to the CAN protocol. */
typedef struct {
    const char *filename;
    const char *sha256;
    uint32_t uid; /* Reserved legacy field; no UID belongs to a firmware profile. */
    uint16_t hardware;
    size_t version_offset;
    size_t block_size;
    unsigned boot_blocks;
    int first_block;
    int last_block;
    size_t image_size;
} ms2x_profile;
extern const ms2x_profile ms2x_ms2x_profile;
const ms2x_profile *ms2x_profile_for(uint16_t hardware);
size_t ms2x_profile_padded(const ms2x_profile *profile);
unsigned ms2x_profile_blocks(const ms2x_profile *profile);

typedef struct {
    uint32_t uid;
    uint16_t hardware, hash;
    uint8_t major, minor;
    int bootloader;
} ms2x_identity;

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t extended;
} ms2x_frame;

typedef enum {
    MS2X_OK = 0, MS2X_INVALID, MS2X_FIRMWARE, MS2X_MEMORY,
    MS2X_IO, MS2X_PROTOCOL, MS2X_TIMEOUT, MS2X_CLOCK, MS2X_TERMINAL
} ms2x_error;

typedef enum {
    MS2X_NEW = 0, MS2X_TX_PING, MS2X_WAIT_PING, MS2X_TX_RESET,
    MS2X_RESET_DELAY, MS2X_TX_QUERY, MS2X_BOOT_DELAY,
    MS2X_TX_SELECT, MS2X_WAIT_SELECT, MS2X_TX_DATA, MS2X_DATA_DELAY,
    MS2X_TX_CRC, MS2X_WAIT_CRC, MS2X_TX_START_APP,
    MS2X_COMPLETE_UNVERIFIED, MS2X_ABORTED, MS2X_WAIT_BOOT
} ms2x_state;

typedef struct ms2x_firmware ms2x_firmware;
typedef struct ms2x_engine ms2x_engine;

typedef struct {
    ms2x_frame frame;
    uint64_t sequence;
    const ms2x_engine *owner;
} ms2x_tx;

/* Pure bytes/CRC helpers. The 13-byte envelope is NOT Linux's can_frame. */
int ms2x_frame_valid(const ms2x_frame *frame);
int ms2x_frame_encode(const ms2x_frame *frame, uint8_t *out, size_t length);
int ms2x_frame_decode(const uint8_t *wire, size_t length, ms2x_frame *out);
int ms2x_frame_equal(const ms2x_frame *a, const ms2x_frame *b);
/* 1=complete 0031/0037 identity, 0=other/status, -1=invalid identity. */
int ms2x_identify(const ms2x_frame *frame, ms2x_identity *identity);
uint16_t ms2x_crc16(const uint8_t *bytes, size_t length);
int ms2x_sha256(const uint8_t *bytes, size_t length, uint8_t out[32]);

/* Firmware bytes are copied and owned here. Original files are read only. */
ms2x_firmware *ms2x_firmware_from_bytes(const uint8_t *bytes, size_t length, ms2x_error *error);
ms2x_firmware *ms2x_firmware_load(const char *path, ms2x_error *error);
ms2x_firmware *ms2x_firmware_from_bytes_for(const uint8_t *bytes, size_t length,
                                          uint16_t hardware, ms2x_error *error);
ms2x_firmware *ms2x_firmware_load_for(const char *path, uint16_t hardware, ms2x_error *error);
const ms2x_profile *ms2x_firmware_profile(const ms2x_firmware *firmware);
void ms2x_firmware_free(ms2x_firmware *firmware);
int ms2x_firmware_block(const ms2x_firmware *firmware, int number, uint8_t *out, size_t length);
int ms2x_firmware_bytes(const ms2x_firmware *firmware, uint8_t *out, size_t length);
unsigned ms2x_firmware_major(const ms2x_firmware *firmware);
unsigned ms2x_firmware_minor(const ms2x_firmware *firmware);

/* Caller retains firmware for the complete engine lifetime. Time is logical ms.
 * CS3 timed start: ping identification, reset,500ms,query,500ms,selector.
 * A boot identification within BOOT_DELAY is validated, but is not mandatory.
 * Receipts are local, single-use and valid only during this engine lifetime.
 * APIs are single-threaded; callbacks must not re-enter the engine.
 * U,88 lacks a block/nonce: this parser cannot prove ACK freshness. It remains
 * usable for offline research; the Linux firmware transport refuses live use.
 */
ms2x_engine *ms2x_engine_new(const ms2x_firmware *firmware, uint64_t timeout_ms);
/* Operational constructor: explicit real UID and a matching image/profile. */
ms2x_engine *ms2x_engine_new_target(const ms2x_firmware *firmware, uint64_t timeout_ms,
                                    uint32_t uid, uint16_t hardware);
uint32_t ms2x_target_uid(const ms2x_engine *engine);
const ms2x_profile *ms2x_engine_profile(const ms2x_engine *engine);
void ms2x_engine_free(ms2x_engine *engine);
int ms2x_start(ms2x_engine *engine, uint64_t now);
/* Reuse a complete, selected normal identity without another ping. */
int ms2x_start_known(ms2x_engine *engine, const ms2x_identity *identity, uint64_t now);
/* Explicit already-running bootloader entry: query, require UID/type identity,
 * then reuse the existing timed select/data/CRC path. No reset or normal ping. */
int ms2x_start_bootloader(ms2x_engine *engine, uint64_t now);
int ms2x_peek(const ms2x_engine *engine, ms2x_tx *out);
int ms2x_tx_processed(ms2x_engine *engine, const ms2x_tx *item, uint64_t now);
int ms2x_receive(ms2x_engine *engine, const ms2x_frame *frame, uint64_t now);
int ms2x_receive_wire(ms2x_engine *engine, const uint8_t *bytes, size_t length, uint64_t now);
int ms2x_advance(ms2x_engine *engine, uint64_t now);
int ms2x_close(ms2x_engine *engine, uint64_t now);
int ms2x_transport_error(ms2x_engine *engine, uint64_t now);
ms2x_state ms2x_get_state(const ms2x_engine *engine);
ms2x_error ms2x_get_error(const ms2x_engine *engine);
const char *ms2x_reason(const ms2x_engine *engine);
const char *ms2x_state_name(ms2x_state state);
unsigned ms2x_completed(const ms2x_engine *engine);
int ms2x_current_block(const ms2x_engine *engine);
int ms2x_boot_seen(const ms2x_engine *engine);
/* Earliest scheduled action or absolute timeout; UINT64_MAX if terminal/new. */
uint64_t ms2x_next_time(const ms2x_engine *engine);

/* Offline RAM/file transport contract. No sockets, fds, interface names or OS
 * device APIs. 0=success for write; read:1=frame,0=EOF,-1=error. Every result
 * propagates. read provides the logical timestamp and complete envelope size.
 */
typedef struct {
    void *context;
    int (*write)(void *context, const uint8_t wire[13], size_t length);
    int (*read)(void *context, uint8_t wire[13], size_t capacity,
                size_t *length, uint64_t *now);
} ms2x_offline_transport;
int ms2x_offline_send(ms2x_engine *engine, const ms2x_offline_transport *io, uint64_t now);
int ms2x_offline_receive(ms2x_engine *engine, const ms2x_offline_transport *io, uint64_t now);
#endif
