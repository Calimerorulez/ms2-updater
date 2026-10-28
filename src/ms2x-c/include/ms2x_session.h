#ifndef MS2X_SESSION_H
#define MS2X_SESSION_H
#include "ms2x_can_sim_loop.h"
#include "ms2x_resources.h"

/* Existing Gerd/mmame files-only identity, never a physical CS3 emulation.
 * The sender hash is separately assigned and checked against bus traffic. */
#define MS2X_RESOURCE_UID UINT32_C(0x43532d32)
#define MS2X_RESOURCE_ADVERTISEMENT {0x43U,0x53U,0x2dU,0x32U,0x20U,0x20U,0xffU,0xffU}
/* Diagnostic sender: separately reviewed; not can2lan/probe4711 or resources5A21. */
#define MS2X_BOOT_QUERY_HASH UINT16_C(0x5a22)

typedef struct {
    int update, bootloader, query; /* probe query: one normal or empty boot identity request. */
    uint32_t target;
    const char *resources;
    uint64_t observe_ms; /* Absolute probe/resource window, not a completion ACK. */
    const ms2x_identity *known; /* Selected normal identity; NULL queries again. */
    int serve; /* Standalone, one normal discovery ping followed by resource replies. */
    uint16_t server_hash; /* Assigned/reviewed; distinct from MS2X_HASH; zero unavailable. */
} ms2x_session_config;
#define MS2X_MAX_DEVICES 16U
typedef struct {
    ms2x_identity normal, boot;
    int have_normal, have_boot, ambiguous;
} ms2x_device;
typedef struct {
    ms2x_error error;
    const char *reason;
    unsigned normal_seen, boot_seen, other_seen, foreign_requests, resource_requests, streams;
    int firmware_local, firmware_protocol, start_local, app_seen, app_version_ok;
    int resources_offered, resources_confirmed, window_ended;
    uint16_t requester_hash;
    uint16_t normal_hash, boot_hash;
    uint8_t normal_major, normal_minor, boot_major, boot_minor;
    ms2x_device devices[MS2X_MAX_DEVICES];
    unsigned device_count, invalid_identities;
    int device_overflow;
    int server_conflict;
    int advertisement_local; /* One complete local write, not MS2 confirmation. */
    uint16_t server_hash;
    ms2x_identity resource_target;
} ms2x_session_result;
enum { MS2X_SELECTED=1, MS2X_SELECT_NONE=0, MS2X_SELECT_AMBIGUOUS=-1,
       MS2X_SELECT_UNSUPPORTED=-2 };
/* UID0 selects only one unambiguous MS2. Boot selection needs 0037 identity.
 * No traffic or mutation; explicit target does not bypass identity conflicts. */
int ms2x_select_device(const ms2x_session_result *result, uint32_t uid,
                       int bootloader, ms2x_identity *identity);
/* Engine is NEW and references a validated image for update; NULL for probe/serve.
 * No hardware APIs here; uses the existing five fixed loop I/O symbols.
 * Return0 = observation window ended cleanly, NEVER whole-update verification.
 */
int ms2x_session_run(ms2x_engine *engine, const ms2x_session_config *config,
                     ms2x_session_result *result);
/* Update CLI: 0=verified firmware/clean resource window, 1=firmware failure,
 * 3=verified firmware followed by resource-phase error. No installation claim. */
int ms2x_update_exit_status(int outcome, const ms2x_session_result *result);

/* Private concrete session/loop coupling; no transport vtable or plugins. */
typedef struct ms2x_session ms2x_session;
int ms2x_can_session_loop(ms2x_engine *engine, ms2x_session *session);
int ms2x_session_begin(ms2x_session *s, uint64_t now);
int ms2x_session_advance(ms2x_session *s, uint64_t now);
int ms2x_session_receive(ms2x_session *s, const uint8_t record[16], int local, uint64_t now);
int ms2x_session_peek(ms2x_session *s, ms2x_frame *frame);
int ms2x_session_processed(ms2x_session *s, uint64_t now);
int ms2x_session_fail(ms2x_session *s, ms2x_error error, const char *reason);
int ms2x_session_done(const ms2x_session *s);
uint64_t ms2x_session_next_time(const ms2x_session *s);
#endif