/* Offline conversion/filtering only. No sockets, fd, clock, UART or OS calls. */
#include "ms2x_can_offline.h"
#include <string.h>

int ms2x_can_pack(const ms2x_frame *frame, uint8_t *record, size_t length)
{
    uint32_t id;
    if (!ms2x_frame_valid(frame) || record==NULL || length!=MS2X_CAN_RECORD_SIZE) return -1;
    id=frame->id|MS2X_CAN_EFF;
    memset(record,0,length);
    memcpy(record,&id,sizeof(id));
    record[4]=frame->dlc;
    memcpy(record+8U,frame->data,frame->dlc);
    return 0;
}

int ms2x_can_unpack(const uint8_t *record, size_t length, int local_origin, ms2x_frame *frame)
{
    ms2x_frame decoded={0};
    uint32_t id;
    if (record==NULL || frame==NULL || length!=MS2X_CAN_RECORD_SIZE ||
        (local_origin!=0 && local_origin!=1)) return MS2X_CAN_INVALID;
    memcpy(&id,record,sizeof(id));
    if ((id&MS2X_CAN_ERR)!=0U || record[4]>8U) return MS2X_CAN_INVALID;
    if ((id&MS2X_CAN_EFF)==0U || (id&MS2X_CAN_RTR)!=0U || local_origin)
        return MS2X_CAN_IGNORE;
    decoded.id=id&UINT32_C(0x1fffffff);
    decoded.dlc=record[4]; decoded.extended=1U;
    memcpy(decoded.data,record+8U,decoded.dlc);
    if (!ms2x_frame_valid(&decoded)) return MS2X_CAN_INVALID;
    *frame=decoded; return MS2X_CAN_TARGET;
}

int ms2x_can_classify_target(const uint8_t *record, size_t length, int local_origin,
                             uint32_t uid, ms2x_frame *frame)
{
    ms2x_frame decoded;
    uint32_t command;
    int result;
    if (frame==NULL) return MS2X_CAN_INVALID;
    result=ms2x_can_unpack(record,length,local_origin,&decoded);
    if (result!=MS2X_CAN_TARGET) return result;
    command=decoded.id&UINT32_C(0x1fff0000);
    if (command!=UINT32_C(0x00310000) && command!=UINT32_C(0x00370000)) return MS2X_CAN_IGNORE;
    if (decoded.dlc<4U) return MS2X_CAN_INVALID;
    if (decoded.data[0]!=(uint8_t)(uid>>24) || decoded.data[1]!=(uint8_t)(uid>>16) ||
        decoded.data[2]!=(uint8_t)(uid>>8) || decoded.data[3]!=(uint8_t)uid) return MS2X_CAN_IGNORE;
    *frame=decoded;
    return MS2X_CAN_TARGET;
}

int ms2x_can_classify(const uint8_t *record, size_t length, int local_origin, ms2x_frame *frame)
{ return ms2x_can_classify_target(record,length,local_origin,MS2X_UID,frame); }

int ms2x_can_filters(ms2x_can_filter *filters, size_t count)
{
    if (filters==NULL || count!=2U) return -1;
    filters[0].id=MS2X_CAN_EFF|UINT32_C(0x00310000);
    filters[1].id=MS2X_CAN_EFF|UINT32_C(0x00370000);
    /* CAN_ERR_FLAG in a filter MASK registers RX_ERR instead of normal RX.
     * Match EFF, non-RTR and command; error subscription is separate. */
    filters[0].mask=UINT32_C(0xdfff0000); filters[1].mask=UINT32_C(0xdfff0000);
    return 0;
}

int ms2x_can_receive_memory(ms2x_engine *engine, const uint8_t *record,
                           size_t length, int local_origin, uint64_t now)
{
    ms2x_frame frame;
    int result;
    if (ms2x_advance(engine,now)!=0) return -1;
    result=ms2x_can_classify_target(record,length,local_origin,ms2x_target_uid(engine),&frame);
    if (result==MS2X_CAN_INVALID) return ms2x_transport_error(engine,now);
    if (result==MS2X_CAN_IGNORE) return 0;
    return ms2x_receive(engine,&frame,now);
}