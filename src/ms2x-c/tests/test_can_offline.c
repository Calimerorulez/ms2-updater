#include "ms2x_can_offline.h"
#include <linux/can.h> /* Layout check only. No socket/device function is used. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(struct can_frame)==16U, "Linux classical CAN size");
_Static_assert(offsetof(struct can_frame,can_dlc)==4U, "Linux length offset");
_Static_assert(offsetof(struct can_frame,data)==8U, "Linux payload offset");
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"CAN offline FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

static ms2x_frame reply(void)
{
    ms2x_frame f={0};
    f.id=0x0037beefU; f.extended=1U; f.dlc=5U;
    f.data[0]=0x4dU; f.data[1]=0x55U; f.data[2]=0x2cU; f.data[3]=0x58U; f.data[4]=0x88U;
    return f;
}

static void codec_and_filter(void)
{
    ms2x_frame f=reply(), decoded;
    uint8_t record[72], envelope[13];
    struct can_frame linux_frame;
    ms2x_can_filter filters[2];
    unsigned i, hash;
    for (i=4U;i<=8U;++i) {
        f.dlc=(uint8_t)i;
        CHECK(ms2x_can_pack(&f,record,16U)==0);
        memcpy(&linux_frame,record,sizeof(linux_frame));
        CHECK(linux_frame.can_id==(f.id|CAN_EFF_FLAG));
        CHECK(linux_frame.can_dlc==f.dlc);
        CHECK(memcmp(linux_frame.data,f.data,i)==0);
        CHECK(record[5]==0U && record[6]==0U && record[7]==0U);
        CHECK(ms2x_can_classify(record,16U,0,&decoded)==MS2X_CAN_TARGET);
        CHECK(ms2x_frame_equal(&f,&decoded));
        CHECK(ms2x_frame_encode(&decoded,envelope,13U)==0);
        CHECK(envelope[0]==0U && envelope[1]==0x37U && envelope[2]==0xbeU && envelope[3]==0xefU);
    }
    CHECK(ms2x_can_pack(&f,record,13U)!=0);
    CHECK(ms2x_can_pack(NULL,record,16U)!=0);
    CHECK(ms2x_can_pack(&f,NULL,16U)!=0);
    CHECK(ms2x_can_filters(filters,2U)==0);
    CHECK(ms2x_can_filters(filters,1U)!=0);
    for (hash=0;hash<=65535U;++hash) {
        uint32_t id=MS2X_CAN_EFF|0x00370000U|hash;
        CHECK((id&filters[1].mask)==(filters[1].id&filters[1].mask));
        CHECK(((id|MS2X_CAN_RTR)&filters[1].mask)!=(filters[1].id&filters[1].mask));
    }
    f=reply();
    for (i=0;i<4U;++i) {
        f.data[i]^=1U;
        CHECK(ms2x_can_pack(&f,record,16U)==0);
        CHECK(ms2x_can_classify(record,16U,0,&decoded)==MS2X_CAN_IGNORE);
        f.data[i]^=1U;
    }
    CHECK(ms2x_can_pack(&f,record,16U)==0);
    CHECK(ms2x_can_classify(record,16U,1,&decoded)==MS2X_CAN_IGNORE);
    CHECK(ms2x_can_classify(record,16U,2,&decoded)==MS2X_CAN_INVALID);
    for (i=0;i<72U;++i) if (i!=16U)
        CHECK(ms2x_can_classify(record,i,0,&decoded)==MS2X_CAN_INVALID);
    for (i=9U;i<=255U;++i) {
        record[4]=(uint8_t)i;
        CHECK(ms2x_can_classify(record,16U,0,&decoded)==MS2X_CAN_INVALID);
    }
    CHECK(ms2x_can_pack(&f,record,16U)==0);
    { uint32_t id=MS2X_CAN_EFF|MS2X_CAN_ERR|f.id; memcpy(record,&id,sizeof(id)); }
    CHECK(ms2x_can_classify(record,16U,0,&decoded)==MS2X_CAN_INVALID);
    { uint32_t id=MS2X_CAN_EFF|MS2X_CAN_RTR|f.id; memcpy(record,&id,sizeof(id)); }
    CHECK(ms2x_can_classify(record,16U,0,&decoded)==MS2X_CAN_IGNORE);
    { uint32_t id=0x123U; memcpy(record,&id,sizeof(id)); }
    CHECK(ms2x_can_classify(record,16U,0,&decoded)==MS2X_CAN_IGNORE);
    f=reply(); f.dlc=3U;
    CHECK(ms2x_can_pack(&f,record,16U)==0);
    CHECK(ms2x_can_classify(record,16U,0,&decoded)==MS2X_CAN_INVALID);
}

static void receipts_and_deadlines(const ms2x_firmware *fw)
{
    ms2x_engine *e=ms2x_engine_new(fw,5000U);
    ms2x_tx queued;
    ms2x_frame f=reply();
    uint8_t record[16];
    CHECK(e!=NULL && ms2x_start(e,0U)==0);
    CHECK(ms2x_peek(e,&queued)==1);
    CHECK(ms2x_can_pack(&queued.frame,record,16U)==0); /* RAM queue insertion only */
    CHECK(ms2x_get_state(e)==MS2X_TX_PING);
    CHECK(ms2x_peek(e,&(ms2x_tx){0})==1);
    CHECK(ms2x_tx_processed(e,&queued,1U)==0); /* explicit local RAM receipt */
    CHECK(ms2x_get_state(e)==MS2X_WAIT_PING);
    f.data[0]^=1U; /* unrelated target must not reset the deadline */
    CHECK(ms2x_can_pack(&f,record,16U)==0);
    CHECK(ms2x_can_receive_memory(e,record,16U,0,4000U)==0);
    CHECK(ms2x_next_time(e)==5001U);
    CHECK(ms2x_can_receive_memory(e,record,16U,0,5001U)!=0);
    CHECK(ms2x_get_error(e)==MS2X_TIMEOUT);
    ms2x_engine_free(e);
    e=ms2x_engine_new(fw,5000U); CHECK(e!=NULL && ms2x_start(e,0U)==0);
    CHECK(ms2x_can_receive_memory(e,record,15U,0,1U)!=0);
    CHECK(ms2x_get_error(e)==MS2X_IO);
    ms2x_engine_free(e);
    e=ms2x_engine_new(fw,5000U); CHECK(e!=NULL && ms2x_start(e,2U)==0);
    CHECK(ms2x_can_receive_memory(e,record,16U,0,1U)!=0);
    CHECK(ms2x_get_error(e)==MS2X_CLOCK);
    ms2x_engine_free(e);
}

int main(int argc, char **argv)
{
    ms2x_error error;
    ms2x_firmware *fw;
    if (argc!=2) return 2;
    fw=ms2x_firmware_load(argv[1],&error); CHECK(fw!=NULL && error==MS2X_OK);
    codec_and_filter(); receipts_and_deadlines(fw);
    ms2x_firmware_free(fw);
    (void)printf("CAN memory adapter: %u assertions passed; no interface or target opened\n",checks);
    return 0;
}