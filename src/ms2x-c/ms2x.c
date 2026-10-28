/* Derived offline protocol design: Gerd Bertelsmann gb2-update.c (Beerware,
 * see LICENSE.gerd), checked against CS3 DeviceUpdater; reports 09/10/11.
 * No OS device/transport code is included. Never reports hardware success.
 */
#include "ms2x.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const ms2x_profile ms2x_ms2x_profile = {
    "051-ms2.bin", MS2X_SHA256_HEX, 0U, MS2X_HARDWARE,
    0U, MS2X_BLOCK_SIZE, 4U, MS2X_FIRST_BLOCK, MS2X_LAST_BLOCK, MS2X_IMAGE_SIZE
};
static const ms2x_profile other_profiles[] = {
    {"050-ms2.bin", "af26cfdc9f747379f5a7bd7e62df384b641e21c4007dbdf9a19c69b3e808bb69",
     0U, 0x32U, 252U, 1024U, 4U, 95, 4, 93500U},
    {"052-ms2.bin", "a56765bb56626795f792839272a3621298955c8831cc6ad35e290afa027f33c7",
     0U, 0x34U, 0U, 1024U, 4U, 127, 4, 126092U}
};
const ms2x_profile *ms2x_profile_for(uint16_t hardware)
{
    if (hardware==0x33U) return &ms2x_ms2x_profile;
    if (hardware==0x32U) return &other_profiles[0];
    if (hardware==0x34U) return &other_profiles[1];
    return NULL;
}
unsigned ms2x_profile_blocks(const ms2x_profile *p)
{ return p==NULL ? 0U : (unsigned)(p->first_block-p->last_block+1); }
size_t ms2x_profile_padded(const ms2x_profile *p)
{ return p==NULL ? 0U : (size_t)ms2x_profile_blocks(p)*p->block_size; }
struct ms2x_firmware { uint8_t bytes[159744]; const ms2x_profile *profile; };
struct ms2x_engine {
    const ms2x_firmware *firmware;
    uint32_t uid;
    ms2x_state state;
    ms2x_error error;
    const char *reason;
    uint64_t now, timeout, deadline, wake, data_deadline, sequence;
    ms2x_tx pending;
    int has_pending, current, boot_seen, identified, selected, crc_sent, boot_entry;
    unsigned completed, parts, chunk;
};

int ms2x_frame_valid(const ms2x_frame *f)
{
    return f!=NULL && f->id<=UINT32_C(0x1fffffff) && f->dlc<=8U && f->extended==1U;
}
int ms2x_frame_encode(const ms2x_frame *f, uint8_t *out, size_t length)
{
    if (!ms2x_frame_valid(f) || out==NULL || length!=13U) return -1;
    memset(out,0,length);
    out[0]=(uint8_t)(f->id>>24); out[1]=(uint8_t)(f->id>>16);
    out[2]=(uint8_t)(f->id>>8); out[3]=(uint8_t)f->id; out[4]=f->dlc;
    memcpy(out+5U,f->data,f->dlc);
    return 0;
}
int ms2x_frame_decode(const uint8_t *wire, size_t length, ms2x_frame *out)
{
    ms2x_frame f={0};
    if (wire==NULL || out==NULL || length!=13U) return -1;
    f.id=((uint32_t)wire[0]<<24)|((uint32_t)wire[1]<<16)|((uint32_t)wire[2]<<8)|wire[3];
    f.dlc=wire[4]; f.extended=1U;
    if (!ms2x_frame_valid(&f)) return -1;
    memcpy(f.data,wire+5U,f.dlc); *out=f;
    return 0;
}
int ms2x_frame_equal(const ms2x_frame *a, const ms2x_frame *b)
{
    return ms2x_frame_valid(a) && ms2x_frame_valid(b) && a->id==b->id &&
        a->dlc==b->dlc && memcmp(a->data,b->data,a->dlc)==0;
}
int ms2x_identify(const ms2x_frame *f, ms2x_identity *identity)
{
    uint32_t command, uid;
    if (!ms2x_frame_valid(f) || identity==NULL) return -1;
    command=f->id&UINT32_C(0x1fff0000);
    if (command!=0x00310000U && command!=0x00370000U) return 0;
    if (command==0x00370000U && f->dlc!=8U) return 0; /* ACK/status is not identity. */
    if (f->dlc!=8U) return -1;
    if (command==0x00370000U && f->data[4]>=0xf1U && f->data[4]<=0xf6U) return 0;
    uid=((uint32_t)f->data[0]<<24)|((uint32_t)f->data[1]<<16)|((uint32_t)f->data[2]<<8)|f->data[3];
    if (uid==0U || uid==UINT32_MAX) return -1;
    identity->uid=uid; identity->hardware=(uint16_t)(((uint16_t)f->data[6]<<8)|f->data[7]);
    identity->hash=(uint16_t)f->id; identity->major=f->data[4]; identity->minor=f->data[5];
    identity->bootloader=command==0x00370000U; return 1;
}
uint16_t ms2x_crc16(const uint8_t *bytes, size_t length)
{
    uint32_t crc=UINT32_C(0xffff);
    size_t i;
    unsigned bit;
    /* NULL is allowed only for the empty vector (as in the hash API). */
    if (bytes==NULL && length!=0U) return 0U;
    for (i=0; i<length; ++i) {
        crc^=(uint32_t)bytes[i]<<8;
        for (bit=0; bit<8U; ++bit)
            crc=((crc&UINT32_C(0x8000))!=0U ? (crc<<1)^UINT32_C(0x1021) : crc<<1)&UINT32_C(0xffff);
    }
    return (uint16_t)crc;
}
ms2x_firmware *ms2x_firmware_from_bytes_for(const uint8_t *bytes, size_t length,
                                          uint16_t hardware, ms2x_error *error)
{
    const ms2x_profile *profile=ms2x_profile_for(hardware);
    uint8_t digest[32];
    char hex[65];
    static const char alphabet[]="0123456789abcdef";
    size_t i;
    ms2x_firmware *f;
    if (error!=NULL) *error=MS2X_FIRMWARE;
    if (profile==NULL || bytes==NULL || length!=profile->image_size ||
        bytes[profile->version_offset]!=5U || bytes[profile->version_offset+1U]!=6U ||
        bytes[profile->version_offset+2U]!=(uint8_t)(hardware>>8) ||
        bytes[profile->version_offset+3U]!=(uint8_t)hardware || ms2x_sha256(bytes,length,digest)!=0) return NULL;
    for (i=0; i<32U; ++i) { hex[2U*i]=alphabet[digest[i]>>4]; hex[2U*i+1U]=alphabet[digest[i]&15U]; }
    hex[64]='\0';
    if (strcmp(hex,profile->sha256)!=0) return NULL;
    f=malloc(sizeof(*f));
    if (f==NULL) { if (error!=NULL) *error=MS2X_MEMORY; return NULL; }
    f->profile=profile; memset(f->bytes,0xff,sizeof(f->bytes)); memcpy(f->bytes,bytes,length);
    if (error!=NULL) *error=MS2X_OK;
    return f;
}
ms2x_firmware *ms2x_firmware_load_for(const char *path, uint16_t hardware, ms2x_error *error)
{
    const ms2x_profile *profile=ms2x_profile_for(hardware);
    FILE *file;
    uint8_t *bytes;
    size_t n;
    int failed;
    ms2x_firmware *result=NULL;
    if (error!=NULL) *error=MS2X_IO;
    if (path==NULL || profile==NULL) return NULL;
    file=fopen(path,"rb");
    if (file==NULL) return NULL;
    bytes=malloc(profile->image_size+1U);
    if (bytes==NULL) { (void)fclose(file); if (error!=NULL) *error=MS2X_MEMORY; return NULL; }
    n=fread(bytes,1U,profile->image_size+1U,file); failed=ferror(file);
    if (fclose(file)!=0) failed=1;
    if (!failed) result=ms2x_firmware_from_bytes_for(bytes,n,hardware,error);
    free(bytes); return result;
}
ms2x_firmware *ms2x_firmware_from_bytes(const uint8_t *bytes, size_t length, ms2x_error *error)
{ return ms2x_firmware_from_bytes_for(bytes,length,MS2X_HARDWARE,error); }
ms2x_firmware *ms2x_firmware_load(const char *path, ms2x_error *error)
{ return ms2x_firmware_load_for(path,MS2X_HARDWARE,error); }
const ms2x_profile *ms2x_firmware_profile(const ms2x_firmware *f) { return f==NULL ? NULL : f->profile; }
void ms2x_firmware_free(ms2x_firmware *f) { free(f); }
int ms2x_firmware_block(const ms2x_firmware *f, int number, uint8_t *out, size_t length)
{
    size_t offset;
    if (f==NULL || out==NULL || length!=f->profile->block_size || number<f->profile->last_block || number>f->profile->first_block) return -1;
    offset=(size_t)(number-f->profile->last_block)*f->profile->block_size;
    memcpy(out,f->bytes+offset,length); return 0;
}
int ms2x_firmware_bytes(const ms2x_firmware *f, uint8_t *out, size_t length)
{
    if (f==NULL || out==NULL || length!=ms2x_profile_padded(f->profile)) return -1;
    memcpy(out,f->bytes,length); return 0;
}
unsigned ms2x_firmware_major(const ms2x_firmware *f) { return f==NULL ? 0U : f->bytes[f->profile->version_offset]; }
unsigned ms2x_firmware_minor(const ms2x_firmware *f) { return f==NULL ? 0U : f->bytes[f->profile->version_offset+1U]; }

static int fail(ms2x_engine *e, ms2x_error error, const char *reason)
{
    if (e==NULL) return -1;
    if (e->state!=MS2X_ABORTED) {
        e->state=MS2X_ABORTED; e->error=error; e->reason=reason;
        e->has_pending=0; e->deadline=UINT64_MAX; e->wake=UINT64_MAX;
    }
    return -1;
}
static int check_time(ms2x_engine *e, uint64_t now)
{
    if (e==NULL || e->state==MS2X_ABORTED) return -1;
    if (now<e->now) return fail(e,MS2X_CLOCK,"logical clock moved backwards");
    e->now=now;
    if (e->state!=MS2X_NEW && e->state!=MS2X_COMPLETE_UNVERIFIED && now>=e->deadline)
        return fail(e,MS2X_TIMEOUT,"absolute protocol phase deadline expired");
    return 0;
}
static int deadline(ms2x_engine *e, uint64_t extra)
{
    if (e->now>=UINT64_MAX-extra) return fail(e,MS2X_CLOCK,"deadline overflow");
    e->deadline=e->now+extra; return 0;
}
static ms2x_frame control(const ms2x_engine *e, uint32_t id, uint8_t code, uint8_t tail, uint8_t dlc)
{
    ms2x_frame f={0};
    f.id=id; f.dlc=dlc; f.extended=1U;
    f.data[0]=(uint8_t)(e->uid>>24); f.data[1]=(uint8_t)(e->uid>>16);
    f.data[2]=(uint8_t)(e->uid>>8); f.data[3]=(uint8_t)e->uid;
    f.data[4]=code; f.data[5]=tail; return f;
}
static int queue(ms2x_engine *e, ms2x_state state, ms2x_frame frame, int fresh_deadline)
{
    if (e->has_pending || e->sequence==UINT64_MAX) return fail(e,MS2X_PROTOCOL,"invalid local TX queue");
    if (fresh_deadline && deadline(e,e->timeout)!=0) return -1;
    e->state=state; e->pending.frame=frame; e->pending.sequence=++e->sequence;
    e->pending.owner=e; e->has_pending=1; return 0;
}
static int delay(ms2x_engine *e, ms2x_state state, uint64_t interval)
{
    if (e->now>=UINT64_MAX-interval) return fail(e,MS2X_CLOCK,"scheduled action overflow");
    e->wake=e->now+interval; e->state=state;
    if (state==MS2X_DATA_DELAY) e->deadline=e->data_deadline;
    else {
        if (e->wake>=UINT64_MAX-e->timeout) return fail(e,MS2X_CLOCK,"delay deadline overflow");
        e->deadline=e->wake+e->timeout;
    }
    return 0;
}
static int select_block(ms2x_engine *e)
{
    if (!e->identified || e->current<e->firmware->profile->last_block || e->current>e->firmware->profile->first_block)
        return fail(e,MS2X_PROTOCOL,"invalid block selection");
    e->selected=0; e->crc_sent=0; e->parts=0;
    return queue(e,MS2X_TX_SELECT,control(e,0x00360000U|MS2X_HASH,0x44U,(uint8_t)e->current,6U),1);
}
static int data_part(ms2x_engine *e)
{
    ms2x_frame f={0};
    size_t offset;
    if (!e->selected || e->parts>=128U || e->current<e->firmware->profile->last_block || e->current>e->firmware->profile->first_block)
        return fail(e,MS2X_PROTOCOL,"invalid data index or missing selector ACK");
    offset=(size_t)(e->current-e->firmware->profile->last_block)*e->firmware->profile->block_size+(size_t)e->parts*8U;
    f.id=0x00360300U+e->parts; f.dlc=8U; f.extended=1U;
    memcpy(f.data,e->firmware->bytes+offset,8U);
    e->deadline=e->data_deadline;
    return queue(e,MS2X_TX_DATA,f,0);
}
static int crc_request(ms2x_engine *e)
{
    uint16_t crc;
    ms2x_frame f;
    if (!e->selected || e->parts!=128U) return fail(e,MS2X_PROTOCOL,"incomplete data burst");
    crc=ms2x_crc16(e->firmware->bytes+(size_t)(e->current-e->firmware->profile->last_block)*e->firmware->profile->block_size,1024U);
    f=control(e,0x00360000U|MS2X_HASH,0x88U,(uint8_t)(crc>>8),7U); f.data[6]=(uint8_t)crc;
    return queue(e,MS2X_TX_CRC,f,1);
}
ms2x_engine *ms2x_engine_new_target(const ms2x_firmware *f, uint64_t timeout_ms,
                                    uint32_t uid, uint16_t hardware)
{
    ms2x_engine *e;
    if (f==NULL || uid==0U || uid==UINT32_MAX || f->profile->hardware!=hardware || timeout_ms==0U || timeout_ms>=UINT64_MAX-500U) return NULL;
    e=calloc(1U,sizeof(*e)); if (e==NULL) return NULL;
    e->firmware=f; e->uid=uid; e->timeout=timeout_ms; e->current=f->profile->first_block; e->state=MS2X_NEW;
    e->reason="not started"; e->deadline=UINT64_MAX; e->wake=UINT64_MAX; return e;
}
ms2x_engine *ms2x_engine_new(const ms2x_firmware *f, uint64_t timeout_ms)
{ return ms2x_engine_new_target(f,timeout_ms,MS2X_UID,f==NULL ? 0U : f->profile->hardware); }
uint32_t ms2x_target_uid(const ms2x_engine *e) { return e==NULL ? 0U : e->uid; }
const ms2x_profile *ms2x_engine_profile(const ms2x_engine *e)
{ return e==NULL ? NULL : e->firmware->profile; }
void ms2x_engine_free(ms2x_engine *e) { free(e); }
int ms2x_start(ms2x_engine *e, uint64_t now)
{
    ms2x_frame ping={0};
    if (check_time(e,now)!=0) return -1;
    if (e->state!=MS2X_NEW) return fail(e,MS2X_PROTOCOL,"engine already started");
    ping.id=0x00300000U|MS2X_HASH; ping.extended=1U;
    e->reason="offline transfer in progress"; return queue(e,MS2X_TX_PING,ping,1);
}
int ms2x_peek(const ms2x_engine *e, ms2x_tx *out)
{
    if (e==NULL || out==NULL) return -1;
    if (!e->has_pending) return 0;
    *out=e->pending; return 1;
}
int ms2x_start_bootloader(ms2x_engine *e, uint64_t now)
{
    ms2x_frame query={0};
    if (check_time(e,now)!=0) return -1;
    if (e->state!=MS2X_NEW) return fail(e,MS2X_PROTOCOL,"engine already started");
    query.id=0x00360000U|MS2X_HASH; query.extended=1U;
    e->boot_entry=1; e->reason="waiting for existing bootloader identity";
    return queue(e,MS2X_TX_QUERY,query,1);
}
int ms2x_tx_processed(ms2x_engine *e, const ms2x_tx *item, uint64_t now)
{
    ms2x_state state;
    if (check_time(e,now)!=0) return -1;
    if (item==NULL || !e->has_pending || item->owner!=e || item->sequence!=e->pending.sequence ||
        !ms2x_frame_equal(&item->frame,&e->pending.frame)) return fail(e,MS2X_PROTOCOL,"invalid, duplicate or foreign local TX receipt");
    state=e->state; e->has_pending=0;
    switch (state) {
    case MS2X_TX_PING: e->state=MS2X_WAIT_PING; return deadline(e,e->timeout);
    case MS2X_TX_RESET: return delay(e,MS2X_RESET_DELAY,500U);
    case MS2X_TX_QUERY:
        if (e->boot_entry) { e->state=MS2X_WAIT_BOOT; return deadline(e,e->timeout); }
        return delay(e,MS2X_BOOT_DELAY,500U);
    case MS2X_TX_SELECT: e->state=MS2X_WAIT_SELECT; return deadline(e,e->timeout);
    case MS2X_TX_DATA:
        ++e->parts;
        if (e->chunk==0U) return fail(e,MS2X_PROTOCOL,"invalid burst accounting");
        --e->chunk;
        if (e->parts==128U) return crc_request(e);
        if (e->chunk!=0U) return data_part(e);
        return delay(e,MS2X_DATA_DELAY,10U);
    case MS2X_TX_CRC:
        if (!e->identified || !e->selected || e->parts!=128U) return fail(e,MS2X_PROTOCOL,"CRC receipt before complete burst");
        e->crc_sent=1; e->state=MS2X_WAIT_CRC; return deadline(e,e->timeout);
    case MS2X_TX_START_APP:
        if (e->completed!=ms2x_profile_blocks(e->firmware->profile)) return fail(e,MS2X_PROTOCOL,"premature application start");
        e->state=MS2X_COMPLETE_UNVERIFIED; e->deadline=UINT64_MAX;
        e->reason="all blocks acknowledged; application start sent; hardware unverified"; return 0;
    default: return fail(e,MS2X_PROTOCOL,"TX receipt outside a transmit state");
    }
}
static int target_uid(const ms2x_engine *e, const ms2x_frame *f)
{
    return f->dlc>=4U && f->data[0]==(uint8_t)(e->uid>>24) && f->data[1]==(uint8_t)(e->uid>>16) &&
        f->data[2]==(uint8_t)(e->uid>>8) && f->data[3]==(uint8_t)e->uid;
}
static int target_type(const ms2x_engine *e, const ms2x_frame *f)
{ return f->data[6]==(uint8_t)(e->firmware->profile->hardware>>8) &&
         f->data[7]==(uint8_t)e->firmware->profile->hardware; }
int ms2x_start_known(ms2x_engine *e, const ms2x_identity *identity, uint64_t now)
{
    ms2x_frame f={0};
    if (check_time(e,now)!=0) return -1;
    if (e->state!=MS2X_NEW || identity==NULL || identity->bootloader ||
        identity->uid!=e->uid || identity->hardware!=e->firmware->profile->hardware)
        return fail(e,MS2X_PROTOCOL,"invalid cached target identity");
    f=control(e,UINT32_C(0x00310000)|identity->hash,identity->major,identity->minor,8U);
    f.data[6]=(uint8_t)(identity->hardware>>8); f.data[7]=(uint8_t)identity->hardware;
    e->state=MS2X_WAIT_PING;
    return ms2x_receive(e,&f,now);
}
int ms2x_receive(ms2x_engine *e, const ms2x_frame *f, uint64_t now)
{
    uint32_t command;
    if (check_time(e,now)!=0) return -1;
    if (!ms2x_frame_valid(f)) return fail(e,MS2X_PROTOCOL,"invalid CAN frame");
    command=f->id&UINT32_C(0x1fff0000); /* sender hash is not a transaction ID */
    if (!target_uid(e,f)) return fail(e,MS2X_PROTOCOL,"wrong or missing target UID");
    if (command==0x00370000U && f->dlc>=5U && f->data[4]>=0xf1U && f->data[4]<=0xf6U)
        return fail(e,MS2X_PROTOCOL,"negative status; automatic retries disabled");
    if (e->state==MS2X_WAIT_BOOT) {
        if (command!=0x00370000U || f->dlc!=8U || !target_type(e,f))
            return fail(e,MS2X_PROTOCOL,"invalid bootloader identity or hardware type");
        e->identified=1; e->boot_seen=1;
        return delay(e,MS2X_BOOT_DELAY,500U);
    }
    if (e->state==MS2X_WAIT_PING) {
        if (command!=0x00310000U || f->dlc!=8U || !target_type(e,f))
            return fail(e,MS2X_PROTOCOL,"invalid ping identification or hardware type");
        e->identified=1;
        return queue(e,MS2X_TX_RESET,control(e,MS2X_HASH,0x80U,0xffU,6U),1);
    }
    if (e->state==MS2X_BOOT_DELAY) {
        if (command!=0x00370000U || f->dlc!=8U || !target_type(e,f) || e->boot_seen)
            return fail(e,MS2X_PROTOCOL,"invalid or duplicate boot identification");
        e->boot_seen=1; return 0; /* CS3 timer, not this optional reply, starts blocks */
    }
    if (command!=0x00370000U || f->dlc<5U) return fail(e,MS2X_PROTOCOL,"unexpected protocol command or length");
    if (e->state==MS2X_WAIT_SELECT && f->dlc==6U && f->data[4]==0x44U) {
        if (!e->identified || (int)f->data[5]!=e->current) return fail(e,MS2X_PROTOCOL,"selector ACK for wrong block");
        e->selected=1; e->parts=0; e->chunk=1U;
        if (deadline(e,e->timeout)!=0) return -1;
        e->data_deadline=e->deadline; return data_part(e);
    }
    if (e->state==MS2X_WAIT_CRC && f->dlc==5U && f->data[4]==0x88U) {
        if (!e->identified || !e->selected || !e->crc_sent || e->parts!=128U || e->has_pending)
            return fail(e,MS2X_PROTOCOL,"CRC ACK without all required local receipts");
        /* No block/CRC/nonce exists in this ACK. An old identical 88 in this
         * state cannot be distinguished. See report 11; never claim otherwise. */
        ++e->completed; --e->current;
        if (e->completed==ms2x_profile_blocks(e->firmware->profile))
            return queue(e,MS2X_TX_START_APP,control(e,0x00360000U|MS2X_HASH,0x11U,0U,5U),1);
        return select_block(e);
    }
    return fail(e,MS2X_PROTOCOL,"unexpected, premature or duplicate protocol message");
}
int ms2x_receive_wire(ms2x_engine *e, const uint8_t *bytes, size_t length, uint64_t now)
{
    ms2x_frame f;
    if (check_time(e,now)!=0) return -1;
    if (ms2x_frame_decode(bytes,length,&f)!=0) return fail(e,MS2X_PROTOCOL,"incomplete or malformed offline envelope");
    return ms2x_receive(e,&f,now);
}
int ms2x_advance(ms2x_engine *e, uint64_t now)
{
    ms2x_frame f={0};
    if (check_time(e,now)!=0) return -1;
    if (e->state==MS2X_RESET_DELAY && now>=e->wake) {
        f.id=0x00360000U|MS2X_HASH; f.extended=1U;
        return queue(e,MS2X_TX_QUERY,f,1);
    }
    if (e->state==MS2X_BOOT_DELAY && now>=e->wake) return select_block(e);
    if (e->state==MS2X_DATA_DELAY && now>=e->wake) { e->chunk=5U; return data_part(e); }
    return 0;
}
int ms2x_close(ms2x_engine *e, uint64_t now)
{
    if (e==NULL) return -1;
    if (e->state==MS2X_ABORTED) return -1;
    if (check_time(e,now)!=0) return -1;
    if (e->state==MS2X_COMPLETE_UNVERIFIED) return 0;
    return fail(e,MS2X_IO,"offline transport ended before application-start receipt");
}
int ms2x_transport_error(ms2x_engine *e, uint64_t now)
{
    if (e==NULL) return -1;
    if (now<e->now) return fail(e,MS2X_CLOCK,"transport clock moved backwards");
    e->now=now; return fail(e,MS2X_IO,"offline transport read/write failure");
}
ms2x_state ms2x_get_state(const ms2x_engine *e) { return e==NULL ? MS2X_ABORTED : e->state; }
ms2x_error ms2x_get_error(const ms2x_engine *e) { return e==NULL ? MS2X_INVALID : e->error; }
const char *ms2x_reason(const ms2x_engine *e) { return e==NULL ? "null engine" : e->reason; }
const char *ms2x_state_name(ms2x_state s)
{
    static const char *const names[]={"NEW","TX_PING","WAIT_PING","TX_RESET","RESET_DELAY","TX_QUERY","BOOT_DELAY","TX_SELECT","WAIT_SELECT","TX_DATA","DATA_DELAY","TX_CRC","WAIT_CRC","TX_START_APP","COMPLETE_UNVERIFIED","ABORTED","WAIT_BOOT"};
    return (unsigned)s<sizeof(names)/sizeof(names[0]) ? names[(unsigned)s] : "INVALID";
}
unsigned ms2x_completed(const ms2x_engine *e) { return e==NULL ? 0U : e->completed; }
int ms2x_current_block(const ms2x_engine *e) { return e==NULL ? -1 : e->current; }
int ms2x_boot_seen(const ms2x_engine *e) { return e!=NULL && e->boot_seen; }
uint64_t ms2x_next_time(const ms2x_engine *e)
{
    if (e==NULL || e->state==MS2X_NEW || e->state==MS2X_ABORTED || e->state==MS2X_COMPLETE_UNVERIFIED) return UINT64_MAX;
    if ((e->state==MS2X_RESET_DELAY || e->state==MS2X_BOOT_DELAY || e->state==MS2X_DATA_DELAY) && e->wake<e->deadline) return e->wake;
    return e->deadline;
}
int ms2x_offline_send(ms2x_engine *e, const ms2x_offline_transport *io, uint64_t now)
{
    ms2x_tx tx;
    uint8_t wire[13];
    if (check_time(e,now)!=0) return -1;
    if (io==NULL || io->write==NULL || ms2x_peek(e,&tx)!=1) return fail(e,MS2X_IO,"offline write callback or pending TX missing");
    if (ms2x_frame_encode(&tx.frame,wire,sizeof(wire))!=0 || io->write(io->context,wire,sizeof(wire))!=0)
        return ms2x_transport_error(e,now);
    return ms2x_tx_processed(e,&tx,now);
}
int ms2x_offline_receive(ms2x_engine *e, const ms2x_offline_transport *io, uint64_t now)
{
    uint8_t wire[13];
    size_t length=0;
    uint64_t received_at=now;
    int result;
    if (check_time(e,now)!=0) return -1;
    if (io==NULL || io->read==NULL) return fail(e,MS2X_IO,"offline read callback missing");
    result=io->read(io->context,wire,sizeof(wire),&length,&received_at);
    if (result==0) return ms2x_close(e,received_at);
    if (result!=1) return ms2x_transport_error(e,received_at);
    return ms2x_receive_wire(e,wire,length,received_at);
}