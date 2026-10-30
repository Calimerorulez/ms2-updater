#define _POSIX_C_SOURCE 200809L
#include "ms2x_linux.h"
#include <errno.h>
#include <limits.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(struct can_frame)==16U,"classical CAN size");
_Static_assert(offsetof(struct can_frame,data)==8U,"CAN memory layout");
static int can_fd=-1, mode;
static volatile sig_atomic_t stopped;

void ms2x_linux_stop(void) { stopped=1; }
void ms2x_linux_close(void) { if (can_fd>=0) (void)close(can_fd); can_fd=-1; stopped=0; }
int ms2x_linux_open(const char *interface, int write_mode)
{
    struct sockaddr_can address;
    unsigned index;
    int zero=0;
    can_err_mask_t errors=CAN_ERR_MASK;
    if (interface==NULL || write_mode<MS2X_LINUX_PASSIVE || write_mode>MS2X_LINUX_BOOT_QUERY || can_fd>=0) {
        errno=EINVAL; return -1;
    }
    /* Refuse even direct adapter users before ANY interface/socket access.
     * The identical U,88 replies cannot establish block/session freshness. */
    if (write_mode==MS2X_LINUX_UPDATE) { errno=EPERM; return -1; }
    if (stopped) { errno=ECANCELED; return -1; }
    index=if_nametoindex(interface);
    if (index==0U || index>(unsigned)INT_MAX) { errno=ENODEV; return -1; }
    can_fd=socket(PF_CAN,SOCK_RAW|SOCK_NONBLOCK|SOCK_CLOEXEC,CAN_RAW);
    if (can_fd<0) return -1;
    memset(&address,0,sizeof(address)); address.can_family=AF_CAN; address.can_ifindex=(int)index;
    if (setsockopt(can_fd,SOL_CAN_RAW,CAN_RAW_RECV_OWN_MSGS,&zero,sizeof(zero))!=0 ||
        setsockopt(can_fd,SOL_CAN_RAW,CAN_RAW_ERR_FILTER,&errors,sizeof(errors))!=0 ||
        bind(can_fd,(const struct sockaddr *)&address,sizeof(address))!=0) {
        int saved=errno; ms2x_linux_close(); errno=saved; return -1;
    }
    if (stopped) { ms2x_linux_close(); errno=ECANCELED; return -1; }
    mode=write_mode; return 0;
}
int ms2x_sim_clock(uint64_t *now)
{
    struct timespec at;
    if (stopped) { errno=ECANCELED; return -1; }
    if (clock_gettime(CLOCK_MONOTONIC,&at)!=0 || at.tv_sec<0 ||
        (uint64_t)at.tv_sec>UINT64_MAX/1000U-1U) return -1;
    *now=(uint64_t)at.tv_sec*1000U+(uint64_t)at.tv_nsec/1000000U; return 0;
}
int ms2x_sim_filters(const ms2x_can_filter *filters, size_t count)
{
    struct can_filter native[6];
    size_t i;
    if (count<2U || count>6U) { errno=EINVAL; return -1; }
    for (i=0U;i<count;++i) { native[i].can_id=filters[i].id; native[i].can_mask=filters[i].mask; }
    return setsockopt(can_fd,SOL_CAN_RAW,CAN_RAW_FILTER,native,(socklen_t)(count*sizeof(native[0])));
}
int ms2x_sim_wait(uint64_t until, int want_write, unsigned *events, int *error)
{
    struct pollfd fd;
    uint64_t now, remaining;
    int result;
    *events=0U; *error=0;
    if (ms2x_sim_clock(&now)!=0) { *error=errno; return -1; }
    remaining=until>now ? until-now : 0U;
    fd.fd=can_fd; fd.events=(short)(POLLIN|(want_write ? POLLOUT : 0)); fd.revents=0;
    result=poll(&fd,1U,remaining>(uint64_t)INT_MAX ? INT_MAX : (int)remaining);
    if (stopped) { *error=ECANCELED; return -1; }
    if (result<0) { *error=errno; return -1; }
    if ((fd.revents&(POLLERR|POLLHUP|POLLNVAL))!=0) { *error=EIO; return -1; }
    if ((fd.revents&POLLIN)!=0) *events|=MS2X_SIM_RX;
    if ((fd.revents&POLLOUT)!=0) *events|=MS2X_SIM_TX;
    return result;
}
int ms2x_sim_read(uint8_t *record, size_t capacity, int *local, int *error)
{
    struct can_frame frame;
    struct iovec vector={&frame,sizeof(frame)};
    struct msghdr message;
    ssize_t result;
    *local=0; *error=0;
    if (stopped || capacity!=sizeof(frame)) { *error=stopped ? ECANCELED : EINVAL; return -1; }
    memset(&message,0,sizeof(message)); message.msg_iov=&vector; message.msg_iovlen=1U;
    result=recvmsg(can_fd,&message,MSG_DONTWAIT);
    if (result<0) { *error=errno; return -1; }
    if ((message.msg_flags&(MSG_TRUNC|MSG_CTRUNC))!=0) { *error=EMSGSIZE; return -1; }
    if ((message.msg_flags&(MSG_DONTROUTE|MSG_CONFIRM))!=0) *local=1;
    if (result==(ssize_t)sizeof(frame)) memcpy(record,&frame,sizeof(frame));
    return (int)result;
}
int ms2x_sim_write(const uint8_t *record, size_t length, int *error)
{
    struct can_frame frame;
    ms2x_frame canonical;
    static const uint8_t advertisement[8]=MS2X_RESOURCE_ADVERTISEMENT;
    ssize_t result;
    *error=0;
    if (stopped) { *error=ECANCELED; return -1; }
    if (can_fd<0) { *error=EBADF; return -1; }
    if (ms2x_can_unpack(record,length,0,&canonical)!=MS2X_CAN_TARGET) { *error=EINVAL; return -1; }
    if (mode==MS2X_LINUX_PASSIVE || mode==MS2X_LINUX_UPDATE || (mode==MS2X_LINUX_PING &&
        (canonical.id!=(UINT32_C(0x00300000)|MS2X_HASH) || canonical.dlc!=0U)) ||
        (mode==MS2X_LINUX_BOOT_QUERY &&
         (canonical.id!=(UINT32_C(0x00360000)|MS2X_BOOT_QUERY_HASH) || canonical.dlc!=0U))) {
        *error=EPERM; return -1;
    }
    if (mode==MS2X_LINUX_RESOURCES &&
        !((canonical.id==(UINT32_C(0x00300000)|MS2X_HASH) && canonical.dlc==0U) ||
          ((canonical.id&UINT32_C(0x1fff0000))==UINT32_C(0x00410000) && canonical.dlc==8U) ||
          ((canonical.id&UINT32_C(0x1fff0000))==UINT32_C(0x00420000) && (canonical.dlc==6U || canonical.dlc==8U)) ||
          ((canonical.id&UINT32_C(0x1fff0000))==UINT32_C(0x00310000) && canonical.dlc==8U &&
           (uint16_t)canonical.id!=0U && (uint16_t)canonical.id!=UINT16_MAX &&
           memcmp(canonical.data,advertisement,8U)==0))) {
        *error=EPERM; return -1;
    }
    memcpy(&frame,record,sizeof(frame));
    result=write(can_fd,&frame,sizeof(frame));
    if (result<0) { *error=errno; return -1; }
    return (int)result;
}
