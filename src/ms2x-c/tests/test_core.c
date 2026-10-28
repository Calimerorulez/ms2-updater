#include "ms2x.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { (void)fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr); exit(1); } } while (0)
static ms2x_firmware *firmware;
static uint8_t expected[159744], received[159744];
static unsigned parts_seen, blocks_seen, starts_seen;
static uint32_t peer_uid=MS2X_UID;
static uint16_t peer_hardware=MS2X_HARDWARE;
static unsigned peer_blocks=MS2X_BLOCKS;

static ms2x_frame response(uint8_t code, uint8_t block)
{
    ms2x_frame f={0};
    f.id=0x00370000U; f.extended=1U; f.dlc=code==0x44U ? 6U : 5U;
    f.data[0]=(uint8_t)(peer_uid>>24); f.data[1]=(uint8_t)(peer_uid>>16);
    f.data[2]=(uint8_t)(peer_uid>>8); f.data[3]=(uint8_t)peer_uid;
    f.data[4]=code; f.data[5]=block; return f;
}
static ms2x_frame identity(int boot)
{
    ms2x_frame f=response(3U,112U);
    f.id=boot ? 0x00370000U : 0x00310000U; f.dlc=8U;
    if (boot) { f.data[4]=0U; f.data[5]=0U; }
    f.data[6]=(uint8_t)(peer_hardware>>8); f.data[7]=(uint8_t)peer_hardware; return f;
}
static ms2x_engine *fresh(void)
{
    ms2x_engine *e=ms2x_engine_new(firmware,5000U);
    CHECK(e!=NULL); CHECK(ms2x_start(e,0U)==0); return e;
}
/* Native RAM peer for sanitizer coverage. Python's existing separate model is
 * the differential peer; this driver independently checks every data byte. */
static void step(ms2x_engine *e, uint64_t *now)
{
    ms2x_tx tx;
    ms2x_state state=ms2x_get_state(e);
    if (ms2x_peek(e,&tx)==1) {
        ms2x_frame reply={0};
        int has_reply=0;
        int number=ms2x_current_block(e);
        if (peer_uid!=MS2X_UID && (state==MS2X_TX_RESET || state==MS2X_TX_SELECT ||
            state==MS2X_TX_CRC || state==MS2X_TX_START_APP)) {
            CHECK(tx.frame.data[0]==(uint8_t)(peer_uid>>24) && tx.frame.data[1]==(uint8_t)(peer_uid>>16) &&
                  tx.frame.data[2]==(uint8_t)(peer_uid>>8) && tx.frame.data[3]==(uint8_t)peer_uid);
        }
        if (state==MS2X_TX_PING) { reply=identity(0); has_reply=1; }
        else if (state==MS2X_TX_QUERY) { reply=identity(1); has_reply=1; }
        else if (state==MS2X_TX_SELECT) { reply=response(0x44U,(uint8_t)number); has_reply=1; parts_seen=0U; }
        else if (state==MS2X_TX_DATA) {
            size_t offset=(size_t)(number-4)*1024U+(size_t)parts_seen*8U;
            CHECK(parts_seen<128U); CHECK(tx.frame.id==0x00360300U+parts_seen);
            CHECK(tx.frame.dlc==8U); CHECK(memcmp(tx.frame.data,expected+offset,8U)==0);
            memcpy(received+offset,tx.frame.data,8U); ++parts_seen;
        } else if (state==MS2X_TX_CRC) {
            uint16_t crc=ms2x_crc16(received+(size_t)(number-4)*1024U,1024U);
            CHECK(parts_seen==128U); CHECK(tx.frame.data[5]==(uint8_t)(crc>>8)); CHECK(tx.frame.data[6]==(uint8_t)crc);
            reply=response(0x88U,0U); has_reply=1; ++blocks_seen;
        } else if (state==MS2X_TX_START_APP) {
            ms2x_frame start=response(0x11U,0U); start.id=0x00364711U;
            CHECK(ms2x_frame_equal(&tx.frame,&start)); CHECK(ms2x_completed(e)==peer_blocks); ++starts_seen;
        }
        CHECK(ms2x_tx_processed(e,&tx,*now)==0);
        if (has_reply) { ++*now; CHECK(ms2x_receive(e,&reply,*now)==0); }
    } else {
        *now=ms2x_next_time(e); CHECK(*now!=UINT64_MAX); CHECK(ms2x_advance(e,*now)==0);
    }
}
static ms2x_engine *reach(ms2x_state target, uint64_t *now)
{
    ms2x_engine *e=fresh();
    unsigned bound=0U;
    *now=0U;
    while (ms2x_get_state(e)!=target) {
        ms2x_state state=ms2x_get_state(e);
        if (state==MS2X_ABORTED || state==MS2X_COMPLETE_UNVERIFIED) {
            (void)fprintf(stderr,"cannot reach %s from %s: %s\n",ms2x_state_name(target),ms2x_state_name(state),ms2x_reason(e));
            CHECK(0);
        }
        CHECK(++bound<30000U);
        if ((target==MS2X_WAIT_PING && state==MS2X_TX_PING) ||
            (target==MS2X_WAIT_SELECT && state==MS2X_TX_SELECT) ||
            (target==MS2X_WAIT_CRC && state==MS2X_TX_CRC)) {
            ms2x_tx tx; CHECK(ms2x_peek(e,&tx)==1); CHECK(ms2x_tx_processed(e,&tx,*now)==0);
        } else step(e,now);
    }
    return e;
}
static void codec_and_crc(void)
{
    ms2x_frame f=response(0x44U,159U), decoded;
    uint8_t wire[13], golden[13]={0,0x37,0,0,6,0x4d,0x55,0x2c,0x58,0x44,159,0,0};
    unsigned n;
    CHECK(ms2x_frame_encode(&f,wire,13U)==0); CHECK(memcmp(wire,golden,13U)==0);
    CHECK(ms2x_frame_decode(wire,13U,&decoded)==0); CHECK(ms2x_frame_equal(&f,&decoded));
    for (n=0; n<=8U; ++n) { f.dlc=(uint8_t)n; CHECK(ms2x_frame_encode(&f,wire,13U)==0); CHECK(ms2x_frame_decode(wire,13U,&decoded)==0); CHECK(ms2x_frame_equal(&f,&decoded)); }
    CHECK(ms2x_frame_encode(NULL,wire,13U)!=0); CHECK(ms2x_frame_encode(&f,NULL,13U)!=0);
    CHECK(ms2x_frame_encode(&f,wire,12U)!=0); CHECK(ms2x_frame_decode(wire,12U,&decoded)!=0);
    CHECK(ms2x_frame_decode(NULL,13U,&decoded)!=0); CHECK(ms2x_frame_decode(wire,13U,NULL)!=0);
    wire[4]=9U; CHECK(ms2x_frame_decode(wire,13U,&decoded)!=0);
    wire[4]=0U; wire[0]=0x80U; CHECK(ms2x_frame_decode(wire,13U,&decoded)!=0);
    f.extended=0U; CHECK(!ms2x_frame_valid(&f)); f.extended=1U;
    f.id=UINT32_MAX; CHECK(!ms2x_frame_valid(&f)); f.id=0x1fffffffU; CHECK(ms2x_frame_valid(&f));
    CHECK(ms2x_crc16(NULL,0U)==0xffffU);
    CHECK(ms2x_crc16((const uint8_t *)"123456789",9U)==0x29b1U);
    CHECK(ms2x_crc16(NULL,1U)==0U);
}
static void firmware_bounds(void)
{
    uint8_t block[1024], digest[32];
    uint8_t *corrupt=malloc(MS2X_IMAGE_SIZE+1U);
    ms2x_error err;
    ms2x_firmware *copy;
    static const int numbers[4]={159,158,5,4};
    static const uint16_t crcs[4]={0x6bfa,0x6476,0x3a6d,0x5cdd};
    unsigned i;
    CHECK(corrupt!=NULL); CHECK(ms2x_firmware_bytes(firmware,expected,sizeof(expected))==0);
    CHECK(ms2x_firmware_major(firmware)==5U && ms2x_firmware_minor(firmware)==6U);
    for (i=0; i<4U; ++i) { CHECK(ms2x_firmware_block(firmware,numbers[i],block,1024U)==0); CHECK(ms2x_crc16(block,1024U)==crcs[i]); }
    for (i=159219U; i<159744U; ++i) CHECK(expected[i]==0xffU);
    CHECK(ms2x_firmware_block(firmware,INT_MIN,block,1024U)!=0); CHECK(ms2x_firmware_block(firmware,-1,block,1024U)!=0);
    CHECK(ms2x_firmware_block(firmware,3,block,1024U)!=0); CHECK(ms2x_firmware_block(firmware,160,block,1024U)!=0);
    CHECK(ms2x_firmware_block(firmware,INT_MAX,block,1024U)!=0); CHECK(ms2x_firmware_block(firmware,4,block,1023U)!=0);
    CHECK(ms2x_firmware_bytes(firmware,expected,sizeof(expected)-1U)!=0);
    memcpy(corrupt,expected,MS2X_IMAGE_SIZE); corrupt[MS2X_IMAGE_SIZE]=0U;
    CHECK(ms2x_firmware_from_bytes(corrupt,MS2X_IMAGE_SIZE+1U,&err)==NULL && err==MS2X_FIRMWARE);
    CHECK(ms2x_firmware_from_bytes(corrupt,MS2X_IMAGE_SIZE-1U,&err)==NULL);
    corrupt[100]^=1U; CHECK(ms2x_firmware_from_bytes(corrupt,MS2X_IMAGE_SIZE,&err)==NULL);
    corrupt[100]^=1U; corrupt[3]=0x32U; CHECK(ms2x_firmware_from_bytes(corrupt,MS2X_IMAGE_SIZE,&err)==NULL);
    corrupt[3]=0x33U; copy=ms2x_firmware_from_bytes(corrupt,MS2X_IMAGE_SIZE,&err); CHECK(copy!=NULL && err==MS2X_OK); ms2x_firmware_free(copy);
    CHECK(ms2x_firmware_load("/nonexistent-ms2x-offline-input",&err)==NULL && err==MS2X_IO);
    CHECK(ms2x_sha256(NULL,0U,digest)==0 && digest[0]==0xe3U && digest[31]==0x55U);
    CHECK(ms2x_sha256(NULL,1U,digest)!=0); CHECK(ms2x_sha256(block,1024U,NULL)!=0);
#if SIZE_MAX > UINT64_MAX / 8U
    CHECK(ms2x_sha256(block,(size_t)(UINT64_MAX/8U+1U),digest)!=0);
#endif
    free(corrupt);
}
static void acknowledgments(void)
{
    ms2x_state phases[]={MS2X_TX_PING,MS2X_WAIT_PING,MS2X_TX_RESET,MS2X_RESET_DELAY,MS2X_TX_QUERY,MS2X_BOOT_DELAY,MS2X_TX_SELECT,MS2X_WAIT_SELECT,MS2X_TX_DATA,MS2X_DATA_DELAY,MS2X_TX_CRC};
    size_t i;
    for (i=0; i<sizeof(phases)/sizeof(phases[0]); ++i) {
        uint64_t now;
        ms2x_frame ack=response(0x88U,0U);
        ms2x_engine *e;
        if (phases[i]==MS2X_WAIT_PING) { ms2x_tx tx; e=fresh(); now=0U; CHECK(ms2x_peek(e,&tx)==1); CHECK(ms2x_tx_processed(e,&tx,now)==0); }
        else if (phases[i]==MS2X_WAIT_SELECT) {
            ms2x_tx tx; e=reach(MS2X_TX_SELECT,&now); CHECK(ms2x_peek(e,&tx)==1); CHECK(ms2x_tx_processed(e,&tx,now)==0);
        } else e=reach(phases[i],&now);
        CHECK(ms2x_receive(e,&ack,now)!=0); CHECK(ms2x_completed(e)==0U); CHECK(ms2x_get_state(e)==MS2X_ABORTED); ms2x_engine_free(e);
    }
    /* Immediate duplicate CRC and selector ACK, and wrong block. */
    for (i=0; i<3U; ++i) {
        uint64_t now; ms2x_engine *e; ms2x_frame ack;
        if (i==0U) { e=reach(MS2X_WAIT_CRC,&now); ack=response(0x88U,0U); CHECK(ms2x_receive(e,&ack,now)==0); CHECK(ms2x_completed(e)==1U); }
        else {
            ms2x_tx tx; e=reach(MS2X_TX_SELECT,&now); CHECK(ms2x_peek(e,&tx)==1); CHECK(ms2x_tx_processed(e,&tx,now)==0);
            ack=response(0x44U,i==1U ? 159U : 158U);
            if (i==1U) CHECK(ms2x_receive(e,&ack,now)==0);
        }
        CHECK(ms2x_receive(e,&ack,now)!=0); CHECK(ms2x_completed(e)==(i==0U ? 1U : 0U)); ms2x_engine_free(e);
    }
    /* The unavoidable old-88 ambiguity: after all of block158's local TX
     * receipts the exact old bytes are accepted. This is a LIMITATION test. */
    {
        uint64_t now; ms2x_engine *e=reach(MS2X_WAIT_CRC,&now); ms2x_frame old=response(0x88U,0U);
        CHECK(ms2x_receive(e,&old,now)==0);
        while (ms2x_get_state(e)!=MS2X_TX_CRC) step(e,&now);
        { ms2x_tx tx; CHECK(ms2x_peek(e,&tx)==1); CHECK(ms2x_tx_processed(e,&tx,now)==0); }
        CHECK(ms2x_current_block(e)==158); CHECK(ms2x_receive(e,&old,now)==0);
        CHECK(ms2x_completed(e)==2U); ms2x_engine_free(e);
    }
}
static void failures_and_timeouts(void)
{
    ms2x_state phases[]={MS2X_TX_PING,MS2X_WAIT_PING,MS2X_TX_RESET,MS2X_RESET_DELAY,MS2X_TX_QUERY,MS2X_BOOT_DELAY,MS2X_TX_SELECT,MS2X_WAIT_SELECT,MS2X_TX_DATA,MS2X_DATA_DELAY,MS2X_TX_CRC,MS2X_WAIT_CRC,MS2X_TX_START_APP};
    size_t i;
    for (i=0; i<sizeof(phases)/sizeof(phases[0]); ++i) {
        uint64_t now; ms2x_engine *e;
        if (phases[i]==MS2X_WAIT_PING || phases[i]==MS2X_WAIT_SELECT) {
            ms2x_tx tx;
            e=reach(phases[i]==MS2X_WAIT_PING ? MS2X_TX_PING : MS2X_TX_SELECT,&now);
            CHECK(ms2x_peek(e,&tx)==1); CHECK(ms2x_tx_processed(e,&tx,now)==0);
        } else e=reach(phases[i],&now);
        CHECK(ms2x_advance(e,now+6000U)!=0); CHECK(ms2x_get_error(e)==MS2X_TIMEOUT);
        CHECK(ms2x_get_state(e)==MS2X_ABORTED); CHECK(ms2x_receive(e,&(ms2x_frame){0},now+6001U)!=0); ms2x_engine_free(e);
    }
    for (i=0; i<6U; ++i) {
        uint64_t now; ms2x_engine *e=reach(MS2X_WAIT_CRC,&now);
        ms2x_frame nack=response((uint8_t)(0xf1U+i),0U); nack.id|=0xbeefU;
        CHECK(ms2x_receive(e,&nack,now)!=0 && ms2x_completed(e)==0U); ms2x_engine_free(e);
    }
    for (i=0; i<8U; ++i) {
        ms2x_engine *e=fresh(); ms2x_tx tx; ms2x_frame f=identity(0);
        CHECK(ms2x_peek(e,&tx)==1); CHECK(ms2x_tx_processed(e,&tx,0U)==0);
        if (i<4U) f.data[i]^=1U;
        else if (i==4U) f.data[6]=1U;
        else if (i==5U) f.data[7]=0x32U;
        else if (i==6U) f.data[7]=0x34U;
        else f.dlc=7U;
        CHECK(ms2x_receive(e,&f,1U)!=0); CHECK(ms2x_completed(e)==0U); ms2x_engine_free(e);
    }
    {
        ms2x_engine *e=fresh(); ms2x_tx tx; CHECK(ms2x_peek(e,&tx)==1);
        CHECK(ms2x_tx_processed(e,&tx,0U)==0); CHECK(ms2x_tx_processed(e,&tx,0U)!=0); ms2x_engine_free(e);
        e=fresh(); CHECK(ms2x_peek(e,&tx)==1); ++tx.sequence;
        CHECK(ms2x_tx_processed(e,&tx,0U)!=0); ms2x_engine_free(e);
        e=fresh(); CHECK(ms2x_advance(e,1U)==0); CHECK(ms2x_advance(e,0U)!=0); CHECK(ms2x_get_error(e)==MS2X_CLOCK); ms2x_engine_free(e);
        e=ms2x_engine_new(firmware,5000U); CHECK(e!=NULL); CHECK(ms2x_start(e,UINT64_MAX-1U)!=0); CHECK(ms2x_get_error(e)==MS2X_CLOCK); ms2x_engine_free(e);
        e=fresh(); CHECK(ms2x_close(e,0U)!=0 && ms2x_get_state(e)==MS2X_ABORTED); ms2x_engine_free(e);
    }
    CHECK(ms2x_engine_new(NULL,5000U)==NULL); CHECK(ms2x_engine_new(firmware,0U)==NULL);
}
typedef struct { int write_result, read_result; size_t length; unsigned writes; uint64_t time; uint8_t bytes[13]; } fake_io;
static int fake_write(void *context, const uint8_t wire[13], size_t length)
{
    fake_io *f=context; CHECK(wire!=NULL && length==13U); ++f->writes; return f->write_result;
}
static int fake_read(void *context, uint8_t wire[13], size_t capacity, size_t *length, uint64_t *now)
{
    fake_io *f=context; CHECK(capacity==13U); memcpy(wire,f->bytes,13U); *length=f->length; *now=f->time; return f->read_result;
}
static void transport_failures(void)
{
    unsigned i;
    for (i=0; i<6U; ++i) {
        fake_io f={0}; ms2x_offline_transport io={&f,fake_write,fake_read}; ms2x_engine *e=fresh();
        f.write_result=-1;
        if (i==0U) CHECK(ms2x_offline_send(e,&io,0U)!=0 && f.writes==1U);
        else if (i==1U) CHECK(ms2x_offline_send(e,&io,5000U)!=0 && f.writes==0U);
        else {
            CHECK(ms2x_peek(e,&(ms2x_tx){0})==1);
            f.read_result=i==2U ? -1 : (i==3U ? 0 : 1); f.length=i==4U ? 12U : 14U;
            CHECK(ms2x_offline_receive(e,&io,0U)!=0);
        }
        CHECK(ms2x_get_state(e)==MS2X_ABORTED); CHECK(ms2x_completed(e)==0U); ms2x_engine_free(e);
    }
}
static void complete_transfer(void)
{
    ms2x_engine *e=fresh(); uint64_t now=0; unsigned bound=0;
    blocks_seen=0U; starts_seen=0U; memset(received,0,sizeof(received));
    while (ms2x_get_state(e)!=MS2X_COMPLETE_UNVERIFIED) { CHECK(++bound<30000U); step(e,&now); }
    CHECK(blocks_seen==156U && starts_seen==1U); CHECK(ms2x_completed(e)==156U);
    CHECK(memcmp(received,expected,sizeof(received))==0); CHECK(ms2x_close(e,now)==0);
    CHECK(ms2x_peek(e,&(ms2x_tx){0})==0); ms2x_engine_free(e);
    /* A final local write failure cannot produce terminal completion. */
    e=reach(MS2X_TX_START_APP,&now);
    { fake_io f={0}; ms2x_offline_transport io={&f,fake_write,fake_read}; f.write_result=-1;
      CHECK(ms2x_offline_send(e,&io,now)!=0); CHECK(ms2x_get_state(e)==MS2X_ABORTED); }
    ms2x_engine_free(e);
}
static void profiles(const char *image)
{
    static const unsigned counts[]={92U,156U,124U};
    char path[1024], directory[1024];
    const char *slash=strrchr(image,'/');
    unsigned before=checks, variant;
    CHECK(slash!=NULL && (size_t)(slash-image)<sizeof(directory));
    memcpy(directory,image,(size_t)(slash-image)); directory[slash-image]='\0';
    CHECK(ms2x_profile_for(0x35U)==NULL);
    for (variant=0U;variant<3U;++variant) {
        const ms2x_profile *p=ms2x_profile_for((uint16_t)(0x32U+variant));
        ms2x_engine *e;
        ms2x_identity known;
        ms2x_frame f;
        ms2x_error error;
        uint64_t now=0U;
        unsigned bound=0U;
        int length;
        FILE *file;
        CHECK(p!=NULL && p->uid==0U && ms2x_profile_blocks(p)==counts[variant]);
        CHECK(p->version_offset==(variant==0U ? 252U : 0U));
        length=snprintf(path,sizeof(path),"%s/%s",directory,p->filename);
        CHECK(length>0 && (size_t)length<sizeof(path));
        firmware=ms2x_firmware_load_for(path,p->hardware,&error); CHECK(firmware!=NULL && error==MS2X_OK);
        CHECK(ms2x_firmware_major(firmware)==5U && ms2x_firmware_minor(firmware)==6U);
        CHECK(ms2x_firmware_load_for(path,0x35U,&error)==NULL);
        if (variant!=1U) CHECK(ms2x_firmware_load(path,&error)==NULL); /* No 0033 geometry fallback. */
        CHECK(ms2x_engine_new_target(firmware,5000U,0U,p->hardware)==NULL);
        CHECK(ms2x_engine_new_target(firmware,5000U,UINT32_MAX,p->hardware)==NULL);
        CHECK(ms2x_engine_new_target(firmware,5000U,0x12345678U,0x35U)==NULL);
        memset(expected,0xff,sizeof(expected)); file=fopen(path,"rb"); CHECK(file!=NULL);
        CHECK(fread(expected,1U,p->image_size,file)==p->image_size && fclose(file)==0);
        CHECK(ms2x_firmware_from_bytes_for(expected,p->image_size-1U,p->hardware,&error)==NULL);
        expected[p->version_offset+3U]^=1U;
        CHECK(ms2x_firmware_from_bytes_for(expected,p->image_size,p->hardware,&error)==NULL);
        expected[p->version_offset+3U]^=1U;
        peer_uid=0x12345678U+variant; peer_hardware=p->hardware; peer_blocks=counts[variant];
        f=identity(0); CHECK(ms2x_identify(&f,&known)==1 && known.uid==peer_uid);
        e=ms2x_engine_new_target(firmware,5000U,peer_uid,peer_hardware); CHECK(e!=NULL);
        if (variant==0U) CHECK(ms2x_start(e,now)==0);
        if (variant==1U) CHECK(ms2x_start_known(e,&known,now)==0 && ms2x_get_state(e)==MS2X_TX_RESET);
        if (variant==2U) CHECK(ms2x_start_bootloader(e,now)==0);
        blocks_seen=0U; starts_seen=0U; memset(received,0,sizeof(received));
        while (ms2x_get_state(e)!=MS2X_COMPLETE_UNVERIFIED) { CHECK(++bound<30000U); step(e,&now); }
        CHECK(blocks_seen==peer_blocks && starts_seen==1U && ms2x_completed(e)==peer_blocks);
        CHECK(memcmp(received,expected,ms2x_profile_padded(p))==0);
        ms2x_engine_free(e);
        e=ms2x_engine_new_target(firmware,5000U,peer_uid,peer_hardware); CHECK(e!=NULL);
        ++known.uid; CHECK(ms2x_start_known(e,&known,0U)!=0 && ms2x_get_error(e)==MS2X_PROTOCOL);
        ms2x_engine_free(e);
        /* A padded firmware NACK is still a NACK, never a boot identity. */
        e=ms2x_engine_new_target(firmware,5000U,peer_uid,peer_hardware); CHECK(e!=NULL);
        CHECK(ms2x_start_bootloader(e,0U)==0);
        { ms2x_tx tx; CHECK(ms2x_peek(e,&tx)==1 && ms2x_tx_processed(e,&tx,0U)==0); }
        f=identity(1); f.data[4]=0xf1U;
        CHECK(ms2x_identify(&f,&known)==0 && ms2x_receive(e,&f,1U)!=0 && !ms2x_boot_seen(e));
        ms2x_engine_free(e); ms2x_firmware_free(firmware);
    }
    (void)printf("Firmware profiles: 0032/0033/0034, 92/156/124 full blocks, dynamic UIDs, normal/cached/boot starts; %u checks passed\n",checks-before);
}
int main(int argc, char **argv)
{
    ms2x_error error;
    if (argc!=2) return 2;
    firmware=ms2x_firmware_load(argv[1],&error); CHECK(firmware!=NULL && error==MS2X_OK);
    codec_and_crc(); firmware_bounds(); acknowledgments(); failures_and_timeouts(); transport_failures(); complete_transfer();
    ms2x_firmware_free(firmware);
    (void)printf("native: 6 groups passed, %u assertions; complete 156-block RAM comparison; no hardware\n",checks);
    profiles(argv[1]);
    return 0;
}