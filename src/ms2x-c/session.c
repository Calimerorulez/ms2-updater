/* One sequential firmware/application/resource session, using the existing loop. */
#include "ms2x_session.h"
#include <string.h>

enum { FIRMWARE, APPLICATION, RESOURCES, PROBE, DONE, DISCOVER_RESOURCES };
struct ms2x_session {
    ms2x_engine *engine;
    ms2x_resources *resources;
    const ms2x_session_config *config;
    ms2x_session_result *result;
    ms2x_tx firmware_tx;
    ms2x_resource_tx resource_tx;
    uint64_t now, end, query_at;
    unsigned phase, tx_kind;
    int started, query_pending, advertised;
    ms2x_identity peer;
};

static void remember(ms2x_session_result *r, const ms2x_identity *id)
{
    ms2x_device *d=NULL;
    const ms2x_identity *old;
    unsigned i;
    int had;
    for (i=0U;i<r->device_count;++i) {
        ms2x_device *peer=&r->devices[i];
        const ms2x_identity *p=peer->have_normal ? &peer->normal : &peer->boot;
        if (p->uid==id->uid) d=peer;
        else if ((peer->have_normal && peer->normal.hash==id->hash) ||
                 (peer->have_boot && peer->boot.hash==id->hash)) peer->ambiguous=1;
    }
    if (d==NULL) {
        if (r->device_count==MS2X_MAX_DEVICES) { r->device_overflow=1; return; }
        d=&r->devices[r->device_count++];
    }
    old=d->have_normal ? &d->normal : &d->boot;
    if ((d->have_normal || d->have_boot) && old->hardware!=id->hardware) d->ambiguous=1;
    had=id->bootloader ? d->have_boot : d->have_normal;
    old=id->bootloader ? &d->boot : &d->normal;
    if (had && (old->hash!=id->hash || old->major!=id->major || old->minor!=id->minor)) d->ambiguous=1;
    if (id->bootloader) { d->boot=*id; d->have_boot=1; }
    else { d->normal=*id; d->have_normal=1; }
    for (i=0U;i<r->device_count;++i) {
        ms2x_device *peer=&r->devices[i];
        if (peer!=d && ((peer->have_normal && peer->normal.hash==id->hash) ||
                       (peer->have_boot && peer->boot.hash==id->hash))) d->ambiguous=1;
    }
}
int ms2x_select_device(const ms2x_session_result *r, uint32_t uid, int bootloader, ms2x_identity *id)
{
    unsigned i, count=0U;
    if (r==NULL || id==NULL || r->error!=MS2X_OK || r->device_overflow ||
        r->invalid_identities!=0U) return MS2X_SELECT_AMBIGUOUS;
    for (i=0U;i<r->device_count;++i) {
        const ms2x_device *d=&r->devices[i];
        const ms2x_identity *p=bootloader ? &d->boot : &d->normal;
        if (!(bootloader ? d->have_boot : d->have_normal) || (uid!=0U && p->uid!=uid)) continue;
        if (d->ambiguous) return MS2X_SELECT_AMBIGUOUS;
        if (ms2x_profile_for(p->hardware)==NULL) {
            if (uid!=0U) return MS2X_SELECT_UNSUPPORTED;
            continue;
        }
        if (++count>1U) return MS2X_SELECT_AMBIGUOUS;
        *id=*p;
    }
    return count==1U ? MS2X_SELECTED : MS2X_SELECT_NONE;
}
int ms2x_update_exit_status(int outcome, const ms2x_session_result *r)
{
    if (r==NULL || !r->firmware_local || !r->firmware_protocol ||
        !r->start_local || !r->app_version_ok) return 1;
    return outcome==0 && r->error==MS2X_OK ? 0 : 3;
}
int ms2x_session_fail(ms2x_session *s, ms2x_error error, const char *reason)
{
    if (s->result->error==MS2X_OK) { s->result->error=error; s->result->reason=reason; }
    if (s->resources!=NULL) ms2x_resources_abort(s->resources);
    if (s->engine!=NULL && ms2x_get_state(s->engine)!=MS2X_COMPLETE_UNVERIFIED)
        (void)ms2x_transport_error(s->engine,s->now);
    s->phase=DONE; return -1;
}
static int filters(int bound, uint16_t peer)
{
    ms2x_can_filter f[3];
    if (ms2x_can_filters(f,2U)!=0) return -1;
    f[2].id=MS2X_CAN_EFF|UINT32_C(0x00400000)|(bound ? peer : 0U);
    f[2].mask=f[0].mask | (bound ? UINT32_C(0xffff) : 0U);
    return ms2x_sim_filters(f,3U);
}
static int service_filters(uint16_t peer, int bound)
{
    ms2x_can_filter f[6];
    unsigned i;
    if (ms2x_can_filters(f,2U)!=0) return -1;
    for (i=2U;i<5U;++i) {
        f[i].id=MS2X_CAN_EFF|((UINT32_C(0x003e)+i)<<16);
        f[i].mask=f[0].mask; /* Never include CAN_ERR_FLAG. */
    }
    if (bound) { f[2].id|=peer; f[2].mask|=UINT32_C(0xffff); }
    f[5].id=MS2X_CAN_EFF|UINT32_C(0x00300000); f[5].mask=f[0].mask;
    return ms2x_sim_filters(f,6U);
}
static int service_conflict(ms2x_session *s, const char *reason)
{
    s->result->server_conflict=1;
    return ms2x_session_fail(s,MS2X_PROTOCOL,reason);
}
static int service_identity(ms2x_session *s, const ms2x_frame *f)
{
    ms2x_identity id, chosen;
    int found=ms2x_identify(f,&id);
    if (found<0) return ms2x_session_fail(s,MS2X_PROTOCOL,"incomplete resource session identity");
    if (found!=1) return 0;
    remember(s->result,&id);
    if (id.hash==s->config->server_hash || id.uid==MS2X_RESOURCE_UID)
        return service_conflict(s,"resource server hash or UID collision");
    if (s->phase==RESOURCES) {
        if ((id.uid==s->peer.uid && id.bootloader) ||
            ms2x_select_device(s->result,s->config->target,0,&chosen)!=MS2X_SELECTED ||
            chosen.uid!=s->peer.uid || chosen.hash!=s->peer.hash || chosen.hardware!=s->peer.hardware)
            return ms2x_session_fail(s,MS2X_PROTOCOL,"resource target identity changed or became ambiguous");
    }
    return 0;
}
int ms2x_session_begin(ms2x_session *s, uint64_t now)
{
    int result;
    s->started=1;
    if (now>UINT64_MAX-s->config->observe_ms) return ms2x_session_fail(s,MS2X_CLOCK,"window overflow");
    if (s->config->serve) {
        uint64_t discovery=s->config->observe_ms/2U;
        if (discovery>10000U) discovery=10000U;
        s->phase=DISCOVER_RESOURCES; s->end=now+s->config->observe_ms;
        s->query_at=now+discovery; s->result->server_hash=s->config->server_hash;
        if (service_filters(0U,0)!=0) return ms2x_session_fail(s,MS2X_IO,"resource conflict filters failed");
        s->query_pending=1; /* Same one-shot normal identification ping as probe --query. */
        return 0;
    }
    if (!s->config->update) {
        s->phase=PROBE; s->end=now+s->config->observe_ms;
        s->query_pending=s->config->query; return 0;
    }
    s->phase=FIRMWARE;
    if (filters(0,0U)!=0) return ms2x_session_fail(s,MS2X_IO,"resource filter installation failed");
    result=s->config->bootloader ? ms2x_start_bootloader(s->engine,now) :
        s->config->known!=NULL ? ms2x_start_known(s->engine,s->config->known,now) : ms2x_start(s->engine,now);
    return result==0 ? 0 : ms2x_session_fail(s,ms2x_get_error(s->engine),ms2x_reason(s->engine));
}
int ms2x_session_advance(ms2x_session *s, uint64_t now)
{
    if (s->result->error!=MS2X_OK) return -1;
    if (now<s->now) return ms2x_session_fail(s,MS2X_CLOCK,"monotone clock moved backwards");
    s->now=now;
    if (!s->started || s->phase==DONE) return 0;
    if (s->phase==DISCOVER_RESOURCES && now>=s->query_at) {
        if (s->query_pending)
            return ms2x_session_fail(s,MS2X_TIMEOUT,"resource identification ping not sent");
        if (ms2x_select_device(s->result,s->config->target,0,&s->peer)!=MS2X_SELECTED)
            return ms2x_session_fail(s,MS2X_PROTOCOL,"no unambiguous normal MS2; select --target for multiple devices");
        s->resources=ms2x_resources_new(s->config->resources,s->peer.hash,s->config->server_hash);
        if (s->resources==NULL || service_filters(s->peer.hash,1)!=0)
            return ms2x_session_fail(s,MS2X_IO,"standalone resource setup failed");
        s->result->resource_target=s->peer; s->result->requester_hash=s->peer.hash;
        s->phase=RESOURCES; /* Keep the original total window deadline. */
    }
    if (s->phase==FIRMWARE) {
        if (ms2x_advance(s->engine,now)!=0)
            return ms2x_session_fail(s,ms2x_get_error(s->engine),ms2x_reason(s->engine));
        if (ms2x_completed(s->engine)==ms2x_profile_blocks(ms2x_engine_profile(s->engine))) s->result->firmware_protocol=1;
        if (ms2x_get_state(s->engine)==MS2X_COMPLETE_UNVERIFIED) {
            if (now>UINT64_MAX-UINT64_C(15000)) return ms2x_session_fail(s,MS2X_CLOCK,"app deadline overflow");
            s->result->start_local=1; s->phase=APPLICATION;
            s->end=now+15000U; s->query_at=now+500U;
        }
    }
    if (s->phase==APPLICATION) {
        if (now>=s->end) return ms2x_session_fail(s,MS2X_TIMEOUT,"no target application5.6 after start");
        if (now>=s->query_at) s->query_pending=1;
    }
    if (s->phase==RESOURCES && ms2x_resources_advance(s->resources,now)!=0)
        return ms2x_session_fail(s,ms2x_resources_error(s->resources),"resource response failed");
    if ((s->phase==RESOURCES || s->phase==PROBE) && now>=s->end) {
        if (s->phase==RESOURCES && ms2x_resources_next_time(s->resources)!=UINT64_MAX)
            return ms2x_session_fail(s,MS2X_TIMEOUT,"resource window ended during a response");
        if (s->phase==RESOURCES && s->config->update)
            s->result->reason="firmware5.6 verified; resource window ended; resource installation unconfirmed";
        s->result->window_ended=1; s->phase=DONE;
    }
    return 0;
}
int ms2x_session_receive(ms2x_session *s, const uint8_t record[16], int local, uint64_t now)
{
    ms2x_frame f;
    uint32_t command, uid;
    /* RECV_OWN_MSGS is off. Observe other local writers for conflicts too. */
    int kind=ms2x_can_unpack(record,16U,s->config->serve && local==1 ? 0 : local,&f);
    if (kind==MS2X_CAN_INVALID) return ms2x_session_fail(s,MS2X_IO,"invalid CAN record or error frame");
    if (kind==MS2X_CAN_IGNORE) return 0;
    command=f.id&UINT32_C(0x1fff0000);
    if (s->config->serve && command==UINT32_C(0x00300000) && (uint16_t)f.id==s->config->server_hash)
        return service_conflict(s,"another participant uses the resource server hash");
    if (s->config->serve && (command==UINT32_C(0x00410000) || command==UINT32_C(0x00420000)))
        return service_conflict(s,"another file server response observed");
    if (s->config->serve && (command==UINT32_C(0x00310000) || command==UINT32_C(0x00370000)))
        return service_identity(s,&f);
    if (command==UINT32_C(0x00400000)) {
        if (s->phase==RESOURCES && (!s->config->serve || s->advertised)) {
            int accepted;
            if ((uint16_t)f.id!=s->result->requester_hash) { ++s->result->foreign_requests; return 0; }
            accepted=ms2x_resources_receive(s->resources,&f,now);
            if (accepted<0) return ms2x_session_fail(s,ms2x_resources_error(s->resources),"resource request failed");
            if (accepted==1) ++s->result->resource_requests;
        }
        return 0;
    }
    if (command!=UINT32_C(0x00310000) && command!=UINT32_C(0x00370000)) return 0;
    if (s->phase==PROBE) {
        ms2x_identity id;
        int found=ms2x_identify(&f,&id);
        if (found<0) ++s->result->invalid_identities;
        if (found!=1) return 0;
        remember(s->result,&id);
        if (s->config->target!=0U && id.uid!=s->config->target) { ++s->result->other_seen; return 0; }
        if (id.bootloader) {
            ++s->result->boot_seen; s->result->boot_hash=id.hash;
            s->result->boot_major=id.major; s->result->boot_minor=id.minor;
        } else {
            ++s->result->normal_seen; s->result->normal_hash=id.hash;
            s->result->normal_major=id.major; s->result->normal_minor=id.minor;
        }
        return 0;
    }
    if (f.dlc<4U) return ms2x_session_fail(s,MS2X_PROTOCOL,"short identification/status");
    uid=((uint32_t)f.data[0]<<24U)|((uint32_t)f.data[1]<<16U)|((uint32_t)f.data[2]<<8U)|f.data[3];
    if (uid!=s->config->target) { ++s->result->other_seen; return 0; }
    if (command==UINT32_C(0x00310000) || f.dlc==8U) {
        if (f.dlc!=8U || f.data[6]!=(uint8_t)(ms2x_engine_profile(s->engine)->hardware>>8) ||
            f.data[7]!=(uint8_t)ms2x_engine_profile(s->engine)->hardware)
            return ms2x_session_fail(s,MS2X_PROTOCOL,"target identity has wrong hardware/length");
        if (command==UINT32_C(0x00370000)) {
            ++s->result->boot_seen; s->result->boot_hash=(uint16_t)f.id;
            s->result->boot_major=f.data[4]; s->result->boot_minor=f.data[5];
        }
        else {
            ++s->result->normal_seen;
            s->result->normal_hash=(uint16_t)f.id;
            s->result->normal_major=f.data[4]; s->result->normal_minor=f.data[5];
            if (s->phase==APPLICATION || s->phase==RESOURCES) {
                if (f.data[4]!=5U || f.data[5]!=6U)
                    return ms2x_session_fail(s,MS2X_PROTOCOL,"target application version is not5.6");
                if (s->phase==RESOURCES && (uint16_t)f.id!=s->result->requester_hash)
                    return ms2x_session_fail(s,MS2X_PROTOCOL,"target hash changed during resource session");
                if (s->phase==APPLICATION) {
                    s->result->app_seen=1; s->result->app_version_ok=1;
                    if (now>UINT64_MAX-s->config->observe_ms) return ms2x_session_fail(s,MS2X_CLOCK,"resource window overflow");
                    s->result->requester_hash=(uint16_t)f.id;
                    s->resources=ms2x_resources_new(s->config->resources,(uint16_t)f.id,MS2X_HASH);
                    if (s->resources==NULL || filters(1,(uint16_t)f.id)!=0)
                        return ms2x_session_fail(s,MS2X_IO,"resource setup failed");
                    s->phase=RESOURCES; s->end=now+s->config->observe_ms; s->query_pending=0;
                }
                return 0;
            }
            if (s->phase==FIRMWARE && ms2x_get_state(s->engine)!=MS2X_WAIT_PING) return 0;
        }
    }
    if (s->phase==PROBE) return 0;
    if (s->phase!=FIRMWARE) return ms2x_session_fail(s,MS2X_PROTOCOL,"target bootloader status after application start");
    if (ms2x_receive(s->engine,&f,now)!=0)
        return ms2x_session_fail(s,ms2x_get_error(s->engine),ms2x_reason(s->engine));
    return 0;
}
int ms2x_session_peek(ms2x_session *s, ms2x_frame *frame)
{
    int ready=0;
    s->tx_kind=0U;
    if (s->phase==RESOURCES && s->config->serve && !s->advertised) {
        static const uint8_t data[8]=MS2X_RESOURCE_ADVERTISEMENT;
        memset(frame,0,sizeof(*frame)); frame->id=UINT32_C(0x00310000)|s->config->server_hash;
        frame->extended=1U; frame->dlc=8U; memcpy(frame->data,data,8U);
        s->tx_kind=4U; ready=1;
    } else if (s->phase==FIRMWARE) {
        ready=ms2x_peek(s->engine,&s->firmware_tx);
        if (ready==1) { *frame=s->firmware_tx.frame; s->tx_kind=1U; }
    } else if (s->phase==RESOURCES) {
        ready=ms2x_resources_peek(s->resources,s->now,&s->resource_tx);
        if (ready==1) { *frame=s->resource_tx.frame; s->tx_kind=2U; }
    } else if (s->query_pending) {
        memset(frame,0,sizeof(*frame));
        frame->id=s->phase==PROBE && s->config->bootloader ?
            UINT32_C(0x00360000)|MS2X_BOOT_QUERY_HASH : UINT32_C(0x00300000)|MS2X_HASH;
        frame->extended=1U; s->tx_kind=3U; ready=1;
    }
    return ready;
}
int ms2x_session_processed(ms2x_session *s, uint64_t now)
{
    int result;
    if (s->tx_kind==1U) {
        int last=ms2x_get_state(s->engine)==MS2X_TX_CRC && ms2x_current_block(s->engine)==ms2x_engine_profile(s->engine)->last_block;
        result=ms2x_tx_processed(s->engine,&s->firmware_tx,now);
        if (result==0 && last) s->result->firmware_local=1;
    } else if (s->tx_kind==2U) {
        result=ms2x_resources_tx_processed(s->resources,&s->resource_tx,now);
        if (result==0 && ms2x_resources_next_time(s->resources)==UINT64_MAX) {
            ++s->result->streams; s->result->resources_offered=1;
        }
    } else if (s->tx_kind==3U) {
        s->query_pending=0;
        if (s->phase!=DISCOVER_RESOURCES) s->query_at=UINT64_MAX;
        result=0;
    } else if (s->tx_kind==4U) { s->advertised=1; s->result->advertisement_local=1; result=0; }
    else return ms2x_session_fail(s,MS2X_PROTOCOL,"no pending session TX");
    s->tx_kind=0U;
    if (result!=0) return ms2x_session_fail(s,MS2X_PROTOCOL,"local TX processing failed");
    return ms2x_session_advance(s,now);
}
int ms2x_session_done(const ms2x_session *s) { return s->phase==DONE; }
uint64_t ms2x_session_next_time(const ms2x_session *s)
{
    uint64_t at=s->end;
    if (s->phase==FIRMWARE) return ms2x_next_time(s->engine);
    if (s->phase==DISCOVER_RESOURCES && s->query_at<at) at=s->query_at;
    if (s->phase==RESOURCES) { uint64_t r=ms2x_resources_next_time(s->resources); if (r<at) at=r; }
    if (s->phase==APPLICATION && !s->query_pending && s->query_at<at) at=s->query_at;
    return at;
}
int ms2x_session_run(ms2x_engine *engine, const ms2x_session_config *config, ms2x_session_result *result)
{
    ms2x_session s;
    int outcome;
    if (result==NULL) return -1;
    memset(result,0,sizeof(*result)); result->reason="observation ended; installation not confirmed";
    if (config==NULL || config->observe_ms==0U || config->observe_ms>3600000U ||
        (config->serve && (config->update || config->bootloader || config->query || config->known!=NULL ||
         engine!=NULL || config->resources==NULL || config->observe_ms<2U ||
         config->server_hash==0U || config->server_hash==UINT16_MAX || config->server_hash==MS2X_HASH)) ||
        (config->update && (engine==NULL || config->target==0U || config->target!=ms2x_target_uid(engine) ||
         config->resources==NULL || config->query || ms2x_get_state(engine)!=MS2X_NEW ||
         (config->bootloader && config->known!=NULL))) ||
        (!config->update && (engine!=NULL || config->known!=NULL))) {
        result->error=MS2X_INVALID; result->reason="invalid target/session configuration"; return -1;
    }
    memset(&s,0,sizeof(s)); s.engine=engine; s.config=config; s.result=result;
    outcome=ms2x_can_session_loop(engine,&s);
    ms2x_resources_free(s.resources); return outcome;
}