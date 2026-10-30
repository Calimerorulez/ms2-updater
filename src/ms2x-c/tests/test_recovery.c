/* Dynamic RAM peer for the existing session/loop/engine. No socketcan.c,
 * hardware, file fixtures or second updater implementation. Persistence and
 * reselect semantics below are MODEL ASSUMPTIONS, not MS2 hardware evidence. */
#include "ms2x_session.h"
#include "synthetic_image.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, cases;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"recovery:%d: %s\n",__LINE__,#x); exit(1); } } while (0)
enum fault { NONE, CUT_MIDDLE, NO_BOOT, WRONG_TYPE, WRONG_UID, SHORT_BOOT,
             NO_SELECT_ACK, NO_CRC_ACK, DATA_TIMEOUT, NO_APP, NACK };
static struct {
    const ms2x_profile *profile;
    uint32_t uid;
    uint16_t reported_type;
    int boot, updated, in_place, second_device;
    uint8_t flash[159744], buffer[1024];
    unsigned writes[156]; /* persistent CRC-accepted counts across attempts */
} peer;
static struct {
    uint64_t now, due;
    ms2x_frame replies[4];
    unsigned head, tail, tx, ping, query, reset, selects, data, crcs, starts;
    unsigned accepted, parts;
    int block, first_block;
    enum fault fault;
} bus;
static uint8_t image[159745], expected[159744], interrupted[159744];

static void fresh_peer(uint16_t hardware, int in_place)
{
    memset(&peer,0,sizeof(peer));
    peer.profile=ms2x_profile_for(hardware); CHECK(peer.profile!=NULL);
    peer.uid=UINT32_C(0x11223300)+hardware;
    peer.reported_type=hardware; peer.in_place=in_place;
    memset(peer.flash,0xa5,sizeof(peer.flash));
    memset(image,0,sizeof(image)); synthetic_image(image,peer.profile);
    memset(expected,0xff,sizeof(expected));
    memcpy(expected,image,peer.profile->image_size);
}
static void new_attempt(enum fault fault)
{
    /* Deliberately retain peer.flash, buffer and writes, like a host restart. */
    memset(&bus,0,sizeof(bus)); bus.block=-1; bus.first_block=-1; bus.fault=fault;
}
static ms2x_frame reply(uint8_t code, uint8_t tail, uint8_t dlc)
{
    ms2x_frame f={0};
    f.id=0x00371234U; f.extended=1U; f.dlc=dlc;
    f.data[0]=(uint8_t)(peer.uid>>24); f.data[1]=(uint8_t)(peer.uid>>16);
    f.data[2]=(uint8_t)(peer.uid>>8); f.data[3]=(uint8_t)peer.uid;
    f.data[4]=code; f.data[5]=tail;
    return f;
}
static ms2x_frame identity(int boot)
{
    ms2x_frame f=reply(boot ? 1U : peer.updated ? 5U : 3U,
                       boot ? 2U : peer.updated ? 6U : 112U,8U);
    f.id=boot ? 0x00371234U : 0x00315678U; /* hashes/versions may differ */
    f.data[6]=(uint8_t)(peer.reported_type>>8); f.data[7]=(uint8_t)peer.reported_type;
    return f;
}
static void enqueue(ms2x_frame f)
{
    CHECK(bus.tail-bus.head<4U); bus.replies[bus.tail++%4U]=f; bus.due=bus.now+1U;
}
/* Independent bit-at-a-time CRC, not the engine's byte/XOR implementation. */
static uint16_t peer_crc(void)
{
    uint16_t r=0xffffU;
    for (unsigned i=0U;i<1024U;++i)
        for (unsigned bit=0U;bit<8U;++bit) {
            unsigned feedback=((unsigned)r>>15)^((unsigned)peer.buffer[i]>>(7U-bit));
            r=(uint16_t)((unsigned)r<<1);
            if ((feedback&1U)!=0U) r^=0x1021U;
        }
    return r;
}
int ms2x_sim_clock(uint64_t *now) { *now=bus.now; return 0; }
int ms2x_sim_filters(const ms2x_can_filter *filters, size_t count)
{
    CHECK(count==2U || count==3U);
    CHECK(filters[0].id==0x80310000U && filters[1].id==0x80370000U);
    return 0;
}
int ms2x_sim_wait(uint64_t until, int want_write, unsigned *events, int *error)
{
    CHECK(until>=bus.now && !want_write); *events=0U; *error=0;
    if (bus.head!=bus.tail && bus.due<until) { bus.now=bus.due; *events=MS2X_SIM_RX; return 1; }
    bus.now=until; return 0;
}
int ms2x_sim_read(uint8_t *record, size_t capacity, int *local, int *error)
{
    CHECK(capacity==16U); *local=0; *error=0;
    if (bus.fault==CUT_MIDDLE && bus.accepted==ms2x_profile_blocks(peer.profile)/2U && bus.parts==64U) {
        *error=EIO; return -1;
    }
    if (bus.head==bus.tail || bus.now<bus.due) { *error=EAGAIN; return -1; }
    CHECK(ms2x_can_pack(&bus.replies[bus.head++%4U],record,capacity)==0);
    return 16;
}
int ms2x_sim_write(const uint8_t *record, size_t length, int *error)
{
    ms2x_frame f;
    CHECK(ms2x_can_unpack(record,length,0,&f)==MS2X_CAN_TARGET); *error=0; ++bus.tx;
    if (f.dlc==0U) {
        if (f.id==0x00304711U) { ++bus.ping; if (!peer.boot) enqueue(identity(0)); }
        else {
            CHECK(f.id==0x00364711U || f.id==0x00365a22U); ++bus.query;
            if (peer.boot && bus.fault!=NO_BOOT) {
                ms2x_frame id=identity(1);
                if (bus.fault==WRONG_TYPE) id.data[7]^=1U;
                if (bus.fault==WRONG_UID) id.data[3]^=1U;
                if (bus.fault==SHORT_BOOT) id.dlc=7U;
                enqueue(id);
                if (peer.second_device) { id.data[3]^=0x80U; id.id=0x0037abcdU; enqueue(id); }
            }
        }
        return 16;
    }
    if (f.id>=0x00360300U && f.id<=0x0036037fU) {
        CHECK(peer.boot && bus.block>=4 && bus.parts<128U && f.dlc==8U);
        CHECK(f.id==0x00360300U+bus.parts);
        size_t offset=(size_t)(bus.block-4)*1024U+(size_t)bus.parts*8U;
        memcpy(peer.buffer+(size_t)bus.parts*8U,f.data,8U);
        if (peer.in_place) memcpy(peer.flash+offset,f.data,8U);
        ++bus.parts; ++bus.data;
        if (bus.fault==DATA_TIMEOUT && bus.parts==64U) bus.now+=MS2X_TIMEOUT_MS;
        return 16;
    }
    CHECK(f.dlc>=5U && f.data[0]==(uint8_t)(peer.uid>>24) && f.data[1]==(uint8_t)(peer.uid>>16) &&
          f.data[2]==(uint8_t)(peer.uid>>8) && f.data[3]==(uint8_t)peer.uid);
    if (f.id==MS2X_HASH) {
        CHECK(f.dlc==6U && f.data[4]==0x80U && f.data[5]==0xffU);
        CHECK(!peer.boot); peer.boot=1; ++bus.reset;
    } else {
        CHECK(f.id==0x00364711U && peer.boot);
        if (f.data[4]==0x44U) {
            CHECK(f.dlc==6U && (int)f.data[5]==peer.profile->first_block-(int)bus.selects);
            CHECK(f.data[5]>=4U); bus.block=f.data[5]; bus.parts=0U;
            if (bus.selects++==0U) bus.first_block=bus.block;
            memset(peer.buffer,0xff,sizeof(peer.buffer));
            if (peer.in_place) memset(peer.flash+(size_t)(bus.block-4)*1024U,0xff,1024U);
            if (bus.fault!=NO_SELECT_ACK) enqueue(reply(0x44U,(uint8_t)bus.block,6U));
        } else if (f.data[4]==0x88U) {
            uint16_t crc=peer_crc();
            CHECK(f.dlc==7U && bus.parts==128U);
            CHECK(f.data[5]==(uint8_t)(crc>>8) && f.data[6]==(uint8_t)crc); ++bus.crcs;
            if (bus.fault==NACK) enqueue(reply(0xf2U,0U,5U));
            else {
                size_t index=(size_t)(bus.block-4);
                memcpy(peer.flash+index*1024U,peer.buffer,1024U);
                ++peer.writes[index]; ++bus.accepted;
                if (bus.fault!=NO_CRC_ACK) enqueue(reply(0x88U,0U,5U));
            }
        } else {
            CHECK(f.data[4]==0x11U && f.dlc==5U && bus.accepted==ms2x_profile_blocks(peer.profile));
            CHECK(memcmp(peer.flash,expected,ms2x_profile_padded(peer.profile))==0);
            ++bus.starts; peer.boot=0; peer.updated=1;
            if (bus.fault!=NO_APP) enqueue(identity(0));
            else peer.boot=1; /* Model a failed application boot. */
        }
    }
    return 16;
}
static ms2x_identity discover(int boot)
{
    ms2x_session_config cfg={0,boot,1,0U,NULL,10U,NULL,0,0U};
    ms2x_session_result r; ms2x_identity id;
    CHECK(ms2x_session_run(NULL,&cfg,&r)==0);
    CHECK(ms2x_select_device(&r,0U,boot,&id)==MS2X_SELECTED);
    CHECK(id.uid==peer.uid && id.hardware==peer.profile->hardware && id.bootloader==boot);
    CHECK(ms2x_profile_for(id.hardware)==peer.profile);
    CHECK(bus.data==0U && bus.reset==0U && bus.starts==0U);
    return id;
}
static ms2x_firmware *firmware(void)
{
    ms2x_error error;
    ms2x_firmware *fw=ms2x_firmware_from_bytes_for(image,peer.profile->image_size,peer.profile->hardware,&error);
    CHECK(fw!=NULL && error==MS2X_OK && ms2x_firmware_profile(fw)==peer.profile);
    return fw;
}
static ms2x_engine *engine(const ms2x_firmware *fw)
{
    ms2x_engine *e=ms2x_engine_new_target(fw,MS2X_TIMEOUT_MS,peer.uid,peer.profile->hardware);
    CHECK(e!=NULL); return e;
}
static int transfer(ms2x_engine *e, const ms2x_identity *known, ms2x_session_result *r)
{
    ms2x_session_config cfg={1,known==NULL,0,peer.uid,".",10U,known,0,0U};
    return ms2x_session_run(e,&cfg,r);
}
static void complete(ms2x_engine *e, ms2x_session_result *r, int outcome)
{
    CHECK(outcome==0 && r->error==MS2X_OK && ms2x_update_exit_status(outcome,r)==0);
    CHECK(r->firmware_local && r->firmware_protocol && r->start_local && r->app_version_ok);
    CHECK(!r->resources_offered && !r->resources_confirmed && r->streams==0U);
    CHECK(ms2x_get_state(e)==MS2X_COMPLETE_UNVERIFIED && ms2x_completed(e)==ms2x_profile_blocks(peer.profile));
    CHECK(bus.first_block==peer.profile->first_block && bus.accepted==ms2x_profile_blocks(peer.profile));
    CHECK(bus.data==128U*ms2x_profile_blocks(peer.profile) && bus.starts==1U);
    CHECK(memcmp(peer.flash,expected,ms2x_profile_padded(peer.profile))==0);
}
static void normal(uint16_t hw)
{
    ms2x_session_result r;
    fresh_peer(hw,0); new_attempt(NONE); ms2x_identity id=discover(0);
    ms2x_firmware *fw=firmware(); ms2x_engine *e=engine(fw); new_attempt(NONE);
    int outcome=transfer(e,&id,&r); complete(e,&r,outcome);
    CHECK(bus.reset==1U && bus.ping==0U && bus.query==1U);
    ++cases; ms2x_engine_free(e); ms2x_firmware_free(fw);
}
static void recovery(uint16_t hw, int in_place, int cold_boot)
{
    ms2x_session_result r; ms2x_tx tx;
    fresh_peer(hw,in_place); new_attempt(NONE); ms2x_identity id=discover(0);
    ms2x_firmware *fw=firmware(); ms2x_engine *e=engine(fw); new_attempt(CUT_MIDDLE);
    CHECK(transfer(e,&id,&r)!=0 && r.error==MS2X_IO);
    unsigned half=ms2x_profile_blocks(peer.profile)/2U, sent=bus.tx;
    CHECK(bus.accepted==half && ms2x_completed(e)==half && bus.parts==64U && bus.starts==0U);
    CHECK(bus.data==half*128U+64U && peer.boot && !peer.updated);
    CHECK(ms2x_get_state(e)==MS2X_ABORTED && ms2x_peek(e,&tx)==0);
    CHECK(ms2x_start_bootloader(e,bus.now)!=0 && ms2x_peek(e,&tx)==0);
    CHECK(transfer(e,NULL,&r)!=0 && r.error==MS2X_INVALID && bus.tx==sent);
    memcpy(interrupted,peer.flash,sizeof(interrupted));
    CHECK(memcmp(interrupted,expected,ms2x_profile_padded(peer.profile))!=0);
    ms2x_engine_free(e);
    if (cold_boot) { memset(peer.buffer,0,sizeof(peer.buffer)); peer.boot=1; }
    new_attempt(NONE); id=discover(1); /* No functioning application identity. */
    CHECK(id.bootloader && peer.boot && !peer.updated);
    CHECK(memcmp(interrupted,peer.flash,sizeof(interrupted))==0);
    e=engine(fw); new_attempt(NONE);
    int outcome=transfer(e,NULL,&r); complete(e,&r,outcome);
    CHECK(bus.reset==0U && bus.ping==0U && bus.query==1U);
    unsigned blocks=ms2x_profile_blocks(peer.profile);
    for (unsigned i=0U;i<blocks;++i) CHECK(peer.writes[i]==(i>=blocks-half ? 2U : 1U));
    printf("recovery %04X: %u blocks restarted from %d; %s, %s; bytes verified\n",
           hw,blocks,bus.first_block,in_place ? "partial writes" : "commit at CRC",cold_boot ? "cold boot" : "no peer reset");
    ++cases; ms2x_engine_free(e); ms2x_firmware_free(fw);
}
static void failures(uint16_t hw)
{
    for (enum fault fault=NO_BOOT;fault<=NACK;fault=(enum fault)(fault+1)) {
        fresh_peer(hw,0); peer.boot=1; new_attempt(fault);
        ms2x_firmware *fw=firmware(); ms2x_engine *e=engine(fw); ms2x_session_result r;
        CHECK(transfer(e,NULL,&r)!=0);
        CHECK(r.error==(fault==WRONG_TYPE || fault==SHORT_BOOT || fault==NACK ? MS2X_PROTOCOL : MS2X_TIMEOUT));
        CHECK(ms2x_update_exit_status(-1,&r)==1);
        if (fault<=NO_SELECT_ACK) CHECK(bus.data==0U && bus.starts==0U);
        if (fault!=NO_APP) {
            CHECK(ms2x_get_state(e)==MS2X_ABORTED && bus.starts==0U);
            ms2x_tx tx; CHECK(ms2x_peek(e,&tx)==0);
        } else CHECK(r.firmware_protocol && r.start_local && !r.app_version_ok);
        CHECK(bus.reset==0U); ++cases;
        ms2x_engine_free(e); ms2x_firmware_free(fw);
    }
}
static void invalid_images(uint16_t hw)
{
    fresh_peer(hw,0); new_attempt(NONE); ms2x_error error;
    size_t length=peer.profile->image_size, offset=peer.profile->version_offset;
    CHECK(ms2x_firmware_from_bytes_for(image,length-1U,hw,&error)==NULL && error==MS2X_FIRMWARE);
    CHECK(ms2x_firmware_from_bytes_for(image,length+1U,hw,&error)==NULL);
    image[length-1U]^=1U;
    CHECK(ms2x_firmware_from_bytes_for(image,length,hw,&error)==NULL); image[length-1U]^=1U;
    image[offset]=4U;
    CHECK(ms2x_firmware_from_bytes_for(image,length,hw,&error)==NULL); image[offset]=5U;
    image[offset+3U]^=1U;
    CHECK(ms2x_firmware_from_bytes_for(image,length,hw,&error)==NULL); image[offset+3U]^=1U;
    for (uint16_t other=0x32U;other<=0x35U;++other)
        if (other!=hw) CHECK(ms2x_firmware_from_bytes_for(image,length,other,&error)==NULL);
    ms2x_firmware *fw=firmware();
    CHECK(ms2x_engine_new_target(fw,5000U,peer.uid,(uint16_t)(hw^1U))==NULL);
    CHECK(ms2x_engine_new_target(fw,5000U,0U,hw)==NULL);
    CHECK(bus.tx==0U); ++cases; ms2x_firmware_free(fw);
}
static void selection(void)
{
    fresh_peer(0x33U,0); peer.boot=1;
    ms2x_session_config cfg={0,1,1,0U,NULL,10U,NULL,0,0U};
    ms2x_session_result r; ms2x_identity id;
    peer.second_device=1; new_attempt(NONE);
    CHECK(ms2x_session_run(NULL,&cfg,&r)==0);
    CHECK(ms2x_select_device(&r,0U,1,&id)==MS2X_SELECT_AMBIGUOUS);
    CHECK(ms2x_select_device(&r,peer.uid,1,&id)==MS2X_SELECTED && id.hardware==0x33U);
    CHECK(bus.data==0U && bus.starts==0U && bus.reset==0U); ++cases;
    peer.second_device=0;
    for (unsigned n=0U;n<3U;++n) {
        peer.reported_type=n==0U ? 0U : 0x35U; new_attempt(n==2U ? NO_BOOT : NONE);
        CHECK(ms2x_session_run(NULL,&cfg,&r)==0);
        CHECK(ms2x_select_device(&r,peer.uid,1,&id)==(n==2U ? MS2X_SELECT_NONE : MS2X_SELECT_UNSUPPORTED));
        CHECK(bus.data==0U && bus.starts==0U); ++cases;
    }
}
int main(void)
{
    selection();
    for (uint16_t hw=0x32U;hw<=0x34U;++hw) {
        normal(hw); invalid_images(hw); failures(hw);
        for (int in_place=0;in_place<2;++in_place)
            for (int cold_boot=0;cold_boot<2;++cold_boot) recovery(hw,in_place,cold_boot);
    }
    printf("Offline recovery: %u cases, %u checks passed; synthetic RAM only, no hardware claim\n",cases,checks);
    return 0;
}
