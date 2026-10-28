/* Shared original loop: fixed I/O symbols supplied by RAM harness or Linux.
 * The historical firmware-only entry keeps its original behavior. */
#include "ms2x_session.h"
#include <errno.h>

static int io_fail(ms2x_engine *engine, ms2x_session *s, uint64_t now)
{
    return s!=NULL ? ms2x_session_fail(s,MS2X_IO,"CAN I/O failed or interrupted") : ms2x_transport_error(engine,now);
}

static int sample(ms2x_engine *engine, ms2x_session *s, uint64_t *now)
{
    uint64_t stamp;
    if (ms2x_sim_clock(&stamp)!=0) return io_fail(engine,s,*now);
    *now=stamp;
    return s!=NULL ? ms2x_session_advance(s,stamp) : ms2x_advance(engine,stamp);
}

static int again(int error)
{
    return error==EAGAIN || error==EWOULDBLOCK;
}

static int route(ms2x_engine *engine, ms2x_session *s, const uint8_t *record, size_t length,
                 int local_origin, uint64_t now)
{
    ms2x_frame frame;
    int kind;
    if (s!=NULL) return ms2x_session_receive(s,record,local_origin,now);
    kind=ms2x_can_classify_target(record,length,local_origin,ms2x_target_uid(engine),&frame);
    /* Ignore only a well-formed, correctly typed target *normal ping* outside
     * WAIT_PING. Malformed pings, firmware NACKs and unknown statuses retain
     * the existing memory adapter/engine's fail-closed behavior.
     * The caller has already advanced the absolute deadline for this record.
     */
    if (kind==MS2X_CAN_TARGET && ms2x_get_state(engine)!=MS2X_WAIT_PING &&
        (frame.id&UINT32_C(0x1fff0000))==UINT32_C(0x00310000) &&
        frame.dlc==8U && frame.data[6]==(uint8_t)(ms2x_engine_profile(engine)->hardware>>8) &&
        frame.data[7]==(uint8_t)ms2x_engine_profile(engine)->hardware) return 0;
    return ms2x_can_receive_memory(engine,record,length,local_origin,now);
}

int ms2x_can_session_loop(ms2x_engine *engine, ms2x_session *s)
{
    ms2x_can_filter filters[2];
    uint64_t now=0U;
    int writable=1;
    if (s==NULL && (engine==NULL || ms2x_get_state(engine)!=MS2X_NEW)) return -1;
    if (sample(engine,s,&now)!=0) return -1;
    if (ms2x_can_filters(filters,2U)!=0 || ms2x_sim_filters(filters,2U)!=0)
        return io_fail(engine,s,now);
    if (sample(engine,s,&now)!=0 || (s!=NULL ? ms2x_session_begin(s,now) : ms2x_start(engine,now))!=0) return -1;
    for (;;) {
        ms2x_tx pending;
        ms2x_frame frame;
        unsigned received;
        int present, interrupted=0;
        if (sample(engine,s,&now)!=0) return -1;
        if (s!=NULL ? ms2x_session_done(s) : ms2x_get_state(engine)==MS2X_COMPLETE_UNVERIFIED) return 0;
        /* Drain a bounded number of nonblocking records BEFORE the next TX.
         * A premature ACK is thus not deliberately parked until WAIT_CRC.
         * A full RX batch still leaves one TX opportunity in this iteration.
         */
        for (received=0U;received<MS2X_SIM_RX_BUDGET;++received) {
            uint8_t record[MS2X_CAN_RECORD_SIZE];
            int local_origin=0, error=0, count;
            if (sample(engine,s,&now)!=0) return -1;
            if (s!=NULL && ms2x_session_done(s)) return 0;
            count=ms2x_sim_read(record,sizeof(record),&local_origin,&error);
            if (sample(engine,s,&now)!=0) return -1;
            if (count<0) {
                if (count==-1 && error==EINTR) { interrupted=1; break; }
                if (count==-1 && again(error)) break;
                return io_fail(engine,s,now);
            }
            if (count!=(int)MS2X_CAN_RECORD_SIZE)
                return io_fail(engine,s,now);
            if (s!=NULL && ms2x_session_done(s)) return 0;
            if (route(engine,s,record,sizeof(record),local_origin,now)!=0) return -1;
        }
        if (interrupted) continue; /* Recheck RX/time before offering another TX. */
        if (sample(engine,s,&now)!=0) return -1;
        present=s!=NULL ? ms2x_session_peek(s,&frame) : ms2x_peek(engine,&pending);
        if (s==NULL && present==1) frame=pending.frame;
        if (present<0) return io_fail(engine,s,now);
        if (present==1 && writable) {
            uint8_t record[MS2X_CAN_RECORD_SIZE];
            int error=0, count;
            if (ms2x_can_pack(&frame,record,sizeof(record))!=0)
                return io_fail(engine,s,now);
            count=ms2x_sim_write(record,sizeof(record),&error);
            /* Even a complete write may have used up the existing budget.
             * Sampling before the receipt prevents a late write resetting it.
             */
            if (sample(engine,s,&now)!=0) return -1;
            if (count!=(int)sizeof(record) && (count!=-1 || !again(error)))
                return io_fail(engine,s,now);
            if (s!=NULL && ms2x_session_done(s)) return 0;
            if (count==(int)sizeof(record)) {
                if ((s!=NULL ? ms2x_session_processed(s,now) : ms2x_tx_processed(engine,&pending,now))!=0) return -1;
                if (s!=NULL ? ms2x_session_done(s) : ms2x_get_state(engine)==MS2X_COMPLETE_UNVERIFIED) return 0;
                continue;
            }
            writable=0; /* No acceptance: preserve this exact pending item. */
        }
        if (sample(engine,s,&now)!=0) return -1;
        {
            unsigned events=0U;
            int error=0;
            int waiting=ms2x_sim_wait(s!=NULL ? ms2x_session_next_time(s) : ms2x_next_time(engine),!writable,&events,&error);
            if (sample(engine,s,&now)!=0) return -1;
            if (waiting==-1 && error==EINTR) continue;
            if (waiting<0 || waiting>1 || (events&~(MS2X_SIM_RX|MS2X_SIM_TX))!=0U ||
                (waiting==0 && events!=0U) || (waiting==1 && events==0U))
                return io_fail(engine,s,now);
            if (s!=NULL && ms2x_session_done(s)) return 0;
            if ((events&MS2X_SIM_TX)!=0U) writable=1;
        }
    }
}

int ms2x_can_sim_run(ms2x_engine *engine)
{
    return ms2x_can_session_loop(engine,NULL);
}