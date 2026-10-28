/* Every possible kernel/interface I/O below is link-wrapped. No real socket. */
#define _POSIX_C_SOURCE 200809L
#include "ms2x_linux.h"
#include "linux_test_entry.h"
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static unsigned checks, sockets, writes, closes;
static uint64_t now;
static int write_result=16, write_error, read_result=-1, read_flags;
static short poll_flags;
static int poll_error, rx_remaining=-1;
static struct can_frame received;
static struct can_frame transmitted;
static struct can_frame receive_queue[5];
static unsigned queue_count, queue_pos;
static unsigned reply_after_write;
static int queue_flags;
static struct can_filter installed[6];
static size_t installed_count;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"Linux test %d: %s\n",__LINE__,#x); exit(1); } } while (0)

unsigned __wrap_if_nametoindex(const char *name);
int __wrap_socket(int domain, int type, int protocol);
int __wrap_setsockopt(int fd, int level, int option, const void *value, socklen_t length);
int __wrap_bind(int fd, const struct sockaddr *address, socklen_t length);
int __wrap_close(int fd);
int __wrap_clock_gettime(clockid_t clock, struct timespec *at);
int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout);
ssize_t __wrap_write(int fd, const void *buffer, size_t count);
ssize_t __wrap_recvmsg(int fd, struct msghdr *message, int flags);

unsigned __wrap_if_nametoindex(const char *name) { CHECK(strcmp(name,"can0")==0 || strcmp(name,"testcan")==0); return 7U; }
int __wrap_socket(int domain, int type, int protocol)
{ CHECK(domain==PF_CAN && (type&SOCK_RAW)!=0 && protocol==CAN_RAW); ++sockets; installed_count=0U; return 42; }
int __wrap_setsockopt(int fd, int level, int option, const void *value, socklen_t length)
{
    CHECK(fd==42 && level==SOL_CAN_RAW && value!=NULL);
    if (option==CAN_RAW_FILTER) {
        CHECK(length==2U*sizeof(struct can_filter) || length==3U*sizeof(struct can_filter) || length==6U*sizeof(struct can_filter));
        installed_count=length/sizeof(struct can_filter); memcpy(installed,value,length);
    }
    else CHECK(option==CAN_RAW_RECV_OWN_MSGS || option==CAN_RAW_ERR_FILTER);
    return 0;
}
int __wrap_bind(int fd, const struct sockaddr *address, socklen_t length)
{
    const struct sockaddr_can *can=(const struct sockaddr_can *)(const void *)address;
    CHECK(fd==42 && length==sizeof(*can) && can->can_family==AF_CAN && can->can_ifindex==7); return 0;
}
int __wrap_close(int fd) { CHECK(fd==42); ++closes; return 0; }
int __wrap_clock_gettime(clockid_t clock, struct timespec *at)
{ CHECK(clock==CLOCK_MONOTONIC); at->tv_sec=(time_t)(now/1000U); at->tv_nsec=(long)(now%1000U)*1000000L; return 0; }
int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    CHECK(count==1U && fds[0].fd==42 && timeout>=0);
    if (poll_error) { errno=poll_error; return -1; }
    fds[0].revents=poll_flags;
    if (!poll_flags) now+=(unsigned)timeout;
    return poll_flags ? 1 : 0;
}
ssize_t __wrap_write(int fd, const void *buffer, size_t count)
{ CHECK(fd==42 && buffer!=NULL && count==16U); memcpy(&transmitted,buffer,sizeof(transmitted)); ++writes; errno=write_error; return write_result; }
ssize_t __wrap_recvmsg(int fd, struct msghdr *message, int flags)
{
    CHECK(fd==42 && flags==MSG_DONTWAIT && message->msg_iovlen==1U && message->msg_iov[0].iov_len==16U);
    if (writes<reply_after_write) { errno=EAGAIN; return -1; }
    while (queue_pos<queue_count) {
        const struct can_frame *frame=&receive_queue[queue_pos++];
        size_t i;
        int matched=0;
        /* Linux 5.4 can_rcv_list_find(): CAN_ERR_FLAG in the MASK puts a
         * registration in RX_ERR, never in the normal-frame filter list.
         * Exercise the actual setsockopt arguments, not only (ID & mask).
         */
        for (i=0U;i<installed_count;++i)
            if ((installed[i].can_mask&CAN_ERR_FLAG)==0U &&
                (frame->can_id&installed[i].can_mask)==(installed[i].can_id&installed[i].can_mask)) matched=1;
        if (matched) {
            memcpy(message->msg_iov[0].iov_base,frame,16U); message->msg_flags=queue_flags; return 16;
        }
    }
    if (read_result<0 || rx_remaining==0) { errno=EAGAIN; return -1; }
    if (rx_remaining>0) --rx_remaining;
    memcpy(message->msg_iov[0].iov_base,&received,16U); message->msg_flags=read_flags; return read_result;
}
static int cli(int argc, const char *const *arguments)
{
    char *copy[10];
    int result, i;
    CHECK(argc<=10);
    for (i=0;i<argc;++i) { copy[i]=strdup(arguments[i]); CHECK(copy[i]!=NULL); }
    result=ms2x_cli_main(argc,copy);
    for (i=0;i<argc;++i) free(copy[i]);
    return result;
}
static void real_identities(void)
{
    static const uint32_t ids[]={0x0031b311U,0x0031f758U,0x0031b33eU,0x0031d37eU,0x00319911U};
    static const uint8_t data[][8]={
        {0x43U,0x42U,0x2bU,0x53U,1U,17U,0U,0x40U},
        {0x66U,0x77U,0x88U,0x99U,1U,47U,0U,0x11U},
        {0x3cU,0x28U,0xfaU,0x16U,4U,3U,0xffU,0xf0U},
        {0U,0U,0x18U,1U,2U,1U,0x46U,0xffU},
        {0x12U,0x34U,0xabU,0xcdU,3U,112U,0U,0x33U}
    };
    unsigned mode;
    read_result=-1; rx_remaining=-1;
    for (unsigned i=0U;i<5U;++i) {
        receive_queue[i].can_id=CAN_EFF_FLAG|ids[i]; receive_queue[i].can_dlc=8U;
        memcpy(receive_queue[i].data,data[i],8U);
    }
    for (mode=0U;mode<3U;++mode) {
        ms2x_session_config config={0,0,mode==2U ? 0 : 1,0U,NULL,1000U,NULL,0,0U};
        ms2x_session_result result;
        ms2x_identity selected;
        unsigned before=writes;
        queue_count=mode==0U ? 4U : 5U; queue_pos=0U; now=0U;
        CHECK(ms2x_linux_open("can0",config.query ? MS2X_LINUX_PING : MS2X_LINUX_PASSIVE)==0);
        CHECK(ms2x_session_run(NULL,&config,&result)==0);
        (void)printf("Probe regression: query=%d devices=%u invalid_identities=%u expected=%u\n",
            config.query,result.device_count,result.invalid_identities,queue_count);
        CHECK(result.device_count==queue_count && result.invalid_identities==0U);
        CHECK(writes==before+(config.query ? 1U : 0U) && queue_pos==queue_count);
        for (unsigned i=0U;i<queue_count;++i) {
            const ms2x_identity *id=&result.devices[i].normal;
            uint32_t uid=((uint32_t)data[i][0]<<24)|((uint32_t)data[i][1]<<16)|((uint32_t)data[i][2]<<8)|data[i][3];
            CHECK(result.devices[i].have_normal && id->uid==uid && id->hash==(uint16_t)ids[i]);
            CHECK(id->hardware==(uint16_t)(((uint16_t)data[i][6]<<8)|data[i][7]));
            CHECK(id->major==data[i][4] && id->minor==data[i][5]);
            if (i<4U) CHECK(ms2x_profile_for(id->hardware)==NULL);
        }
        if (mode==0U) CHECK(ms2x_select_device(&result,0U,0,&selected)==MS2X_SELECT_NONE);
        else CHECK(ms2x_select_device(&result,0U,0,&selected)==MS2X_SELECTED && selected.uid==0x1234abcdU && selected.hardware==0x33U);
        (void)printf("Real probe RX: query=%d devices=%u invalid_identities=%u MS2_candidates=%u\n",
            config.query,result.device_count,result.invalid_identities,mode==0U ? 0U : 1U);
        ms2x_linux_close();
    }
    queue_count=0U; queue_pos=0U;
}
static void resource_policy(void)
{
    static const uint32_t forbidden[]={0x00005a21U,0x00305a21U,0x00315a21U,0x00365a21U,0x00360300U};
    ms2x_frame frame={0x00415a21U,8U,{0},1U};
    uint8_t record[16]; int error;
    unsigned before;
    CHECK(ms2x_linux_open("can0",MS2X_LINUX_RESOURCES)==0);
    for (unsigned i=0U;i<3U;++i) {
        frame.id=i==0U ? 0x00415a21U : 0x00429b22U; frame.dlc=i==1U ? 6U : 8U;
        CHECK(ms2x_can_pack(&frame,record,16U)==0 && ms2x_sim_write(record,16U,&error)==16);
    }
    before=writes;
    for (unsigned i=0U;i<sizeof(forbidden)/sizeof(forbidden[0]);++i) {
        frame.id=forbidden[i]; frame.dlc=8U; frame.data[4]=0x11U;
        CHECK(ms2x_can_pack(&frame,record,16U)==0);
        CHECK(ms2x_sim_write(record,16U,&error)==-1 && error==EPERM && writes==before);
    }
    frame.id=0x00429b22U; frame.dlc=5U;
    CHECK(ms2x_can_pack(&frame,record,16U)==0 && ms2x_sim_write(record,16U,&error)==-1 && error==EPERM);
    frame.id=UINT32_C(0x00300000)|MS2X_HASH; frame.dlc=0U;
    CHECK(ms2x_can_pack(&frame,record,16U)==0 && ms2x_sim_write(record,16U,&error)==16);
    before=writes; frame.dlc=1U;
    CHECK(ms2x_can_pack(&frame,record,16U)==0 && ms2x_sim_write(record,16U,&error)==-1 && error==EPERM && writes==before);
    frame.id=0x00305a21U; frame.dlc=0U;
    CHECK(ms2x_can_pack(&frame,record,16U)==0 && ms2x_sim_write(record,16U,&error)==-1 && error==EPERM && writes==before);
    frame.id=0x00300b0dU;
    CHECK(ms2x_can_pack(&frame,record,16U)==0 && ms2x_sim_write(record,16U,&error)==-1 && error==EPERM && writes==before);
    frame.id=0x00315a21U; frame.dlc=8U;
    { const uint8_t ad[8]=MS2X_RESOURCE_ADVERTISEMENT; memcpy(frame.data,ad,8U); }
    CHECK(ms2x_can_pack(&frame,record,16U)==0 && ms2x_sim_write(record,16U,&error)==16);
    frame.data[0]^=1U;
    CHECK(ms2x_can_pack(&frame,record,16U)==0 && ms2x_sim_write(record,16U,&error)==-1 && error==EPERM);
    ms2x_linux_close();
    for (unsigned conflict=0U;conflict<2U;++conflict) {
        ms2x_session_config config={0,0,0,0U,"/offline/resources",1000U,NULL,1,0x5a21U};
        ms2x_session_result result;
        const uint8_t id[]={0x12U,0x34U,0xabU,0xcdU,3U,112U,0U,0x33U};
        receive_queue[0].can_id=CAN_EFF_FLAG|0x00319b22U; receive_queue[0].can_dlc=8U;
        memcpy(receive_queue[0].data,id,8U);
        receive_queue[1]=receive_queue[0]; receive_queue[1].can_id=CAN_EFF_FLAG|0x0041aaaaU;
        queue_count=conflict ? 2U : 1U; queue_pos=0U; queue_flags=MSG_DONTROUTE;
        now=0U; before=writes;
        CHECK(ms2x_linux_open("can0",MS2X_LINUX_RESOURCES)==0);
        CHECK(ms2x_session_run(NULL,&config,&result)==(conflict ? -1 : 0));
        CHECK(writes==before+(conflict ? 0U : 2U) && !result.start_local && !result.firmware_local);
        CHECK(installed_count==6U);
        for (size_t i=0U;i<installed_count;++i) CHECK((installed[i].can_mask&CAN_ERR_FLAG)==0U);
        if (conflict) CHECK(result.server_conflict);
        else CHECK(result.resource_target.uid==0x1234abcdU && result.window_ended && result.streams==0U);
        ms2x_linux_close();
    }
    queue_count=0U; queue_pos=0U; queue_flags=0;
}
static void boot_probe(void)
{
    static const uint32_t forbidden[]={0x00005a22U,0x00304711U,0x00365a21U,0x00360300U,0x00415a22U,0x00420b0dU};
    ms2x_frame query={0x00365a22U,0U,{0},1U};
    uint8_t record[16]; int error;
    unsigned before;
    CHECK(ms2x_linux_open("can0",MS2X_LINUX_BOOT_QUERY)==0);
    CHECK(ms2x_can_pack(&query,record,16U)==0 && ms2x_sim_write(record,16U,&error)==16);
    before=writes;
    for (unsigned i=0U;i<sizeof(forbidden)/sizeof(forbidden[0]);++i) {
        query.id=forbidden[i];
        CHECK(ms2x_can_pack(&query,record,16U)==0 && ms2x_sim_write(record,16U,&error)==-1 && error==EPERM && writes==before);
    }
    query.id=0x00365a22U;
    for (unsigned length=1U;length<=8U;++length) {
        query.dlc=(uint8_t)length; query.data[4]=0x11U;
        CHECK(ms2x_can_pack(&query,record,16U)==0 && ms2x_sim_write(record,16U,&error)==-1 && error==EPERM && writes==before);
    }
    ms2x_linux_close();
    for (unsigned scenario=0U;scenario<7U;++scenario) {
        ms2x_session_config config={0,1,scenario==6U ? 0 : 1,0x1a2b3c4dU,NULL,1000U,NULL,0,0U};
        ms2x_session_result result;
        ms2x_identity id;
        const uint8_t identity[]={0x1aU,0x2bU,0x3cU,0x4dU,1U,2U,0U,0x33U};
        receive_queue[0].can_id=CAN_EFF_FLAG|0x00370b0dU; receive_queue[0].can_dlc=8U;
        memcpy(receive_queue[0].data,identity,8U);
        if (scenario==2U) { receive_queue[0].can_id=CAN_EFF_FLAG|0x00310b0dU; receive_queue[0].data[4]=3U; receive_queue[0].data[5]=112U; }
        if (scenario==3U) receive_queue[0].data[3]^=1U;
        if (scenario==4U) { receive_queue[0].can_dlc=5U; receive_queue[0].data[4]=0x88U; }
        if (scenario==5U) receive_queue[0].data[4]=0xf1U;
        queue_count=scenario==0U ? 0U : 1U; queue_pos=0U;
        before=writes; reply_after_write=before+(config.query ? 1U : 0U); now=0U;
        CHECK(ms2x_linux_open("can0",config.query ? MS2X_LINUX_BOOT_QUERY : MS2X_LINUX_PASSIVE)==0);
        CHECK(ms2x_session_run(NULL,&config,&result)==0 && result.window_ended && now==1000U);
        CHECK(writes==before+(config.query ? 1U : 0U));
        if (config.query) CHECK(transmitted.can_id==(CAN_EFF_FLAG|0x00365a22U) && transmitted.can_dlc==0U);
        CHECK(!result.firmware_local && !result.firmware_protocol && !result.start_local && !result.resources_offered && !result.advertisement_local && result.streams==0U);
        CHECK(installed_count==2U && installed[0].can_id==0x80310000U && installed[1].can_id==0x80370000U);
        CHECK(installed[0].can_mask==0xdfff0000U && installed[1].can_mask==0xdfff0000U);
        if (scenario==1U || scenario==6U) {
            CHECK(ms2x_select_device(&result,config.target,1,&id)==MS2X_SELECTED);
            CHECK(id.uid==config.target && id.bootloader && id.hardware==0x33U && id.hash==0x0b0dU && id.major==1U && id.minor==2U);
            CHECK(result.boot_seen==1U && result.normal_seen==0U);
        } else {
            CHECK(ms2x_select_device(&result,config.target,1,&id)==MS2X_SELECT_NONE);
            if (scenario==2U) CHECK(result.normal_seen==1U && result.boot_seen==0U);
        }
        ms2x_linux_close();
    }
    {
        const char *active[]={"ms2-updater","probe","--bootloader","--query","--interface","can0","--seconds","1","--target","1A2B3C4D"};
        const uint8_t identity[]={0x1aU,0x2bU,0x3cU,0x4dU,1U,2U,0U,0x33U};
        receive_queue[0].can_id=CAN_EFF_FLAG|0x00370b0dU; receive_queue[0].can_dlc=8U;
        memcpy(receive_queue[0].data,identity,8U);
        for (unsigned reply=0U;reply<2U;++reply) {
            before=writes; reply_after_write=before+1U; now=0U;
            queue_count=reply; queue_pos=0U;
            CHECK(cli(10,active)==0 && writes==before+1U && now==1000U);
            CHECK(transmitted.can_id==(CAN_EFF_FLAG|0x00365a22U) && transmitted.can_dlc==0U);
        }
    }
    queue_count=0U; queue_pos=0U; reply_after_write=0U;
}
int main(int argc, char **argv)
{
    uint8_t record[16];
    int error, local;
    unsigned events, before;
    ms2x_frame f={UINT32_C(0x00304711),0U,{0},1U};
    CHECK(argc==2); CHECK(ms2x_can_pack(&f,record,sizeof(record))==0);
    CHECK(ms2x_linux_open("can0",MS2X_LINUX_PASSIVE)==0);
    before=writes; CHECK(ms2x_sim_write(record,16U,&error)==-1 && error==EPERM && writes==before);
    ms2x_linux_close();
    CHECK(ms2x_linux_open("testcan",MS2X_LINUX_PING)==0);
    CHECK(ms2x_sim_write(record,16U,&error)==16);
    f.id=UINT32_C(0x00364711); CHECK(ms2x_can_pack(&f,record,16U)==0);
    before=writes; CHECK(ms2x_sim_write(record,16U,&error)==-1 && error==EPERM && writes==before);
    ms2x_linux_close();
    CHECK(ms2x_linux_open("can0",MS2X_LINUX_UPDATE)==0);
    for (int result=0;result<=16;++result) {
        write_result=result; CHECK(ms2x_sim_write(record,16U,&error)==result);
    }
    write_result=-1;
    {
        const int errors[]={EAGAIN,ENOBUFS,EIO,EINTR};
        for (size_t i=0U;i<sizeof(errors)/sizeof(errors[0]);++i) {
            write_error=errors[i]; CHECK(ms2x_sim_write(record,16U,&error)==-1 && error==errors[i]);
        }
    }
    write_result=16; write_error=0;
    received.can_id=CAN_EFF_FLAG|0x00319b22U; received.can_dlc=8U;
    read_result=16; read_flags=0;
    CHECK(ms2x_sim_read(record,16U,&local,&error)==16 && local==0);
    read_flags=MSG_DONTROUTE; CHECK(ms2x_sim_read(record,16U,&local,&error)==16 && local==1);
    read_flags=MSG_CONFIRM; CHECK(ms2x_sim_read(record,16U,&local,&error)==16 && local==1);
    read_flags=MSG_TRUNC; CHECK(ms2x_sim_read(record,16U,&local,&error)==-1 && error==EMSGSIZE);
    read_flags=0; read_result=15; CHECK(ms2x_sim_read(record,16U,&local,&error)==15);
    read_result=-1; CHECK(ms2x_sim_read(record,16U,&local,&error)==-1 && error==EAGAIN);
    now=10U; CHECK(ms2x_sim_wait(1000U,0,&events,&error)==0 && now==1000U);
    poll_flags=POLLIN|POLLOUT; CHECK(ms2x_sim_wait(1001U,1,&events,&error)==1 && events==3U);
    poll_flags=POLLERR; CHECK(ms2x_sim_wait(1001U,0,&events,&error)==-1 && error==EIO);
    poll_flags=0; poll_error=EINTR; CHECK(ms2x_sim_wait(1001U,0,&events,&error)==-1 && error==EINTR);
    poll_error=0; ms2x_linux_stop();
    before=writes; CHECK(ms2x_sim_write(record,16U,&error)==-1 && error==ECANCELED && writes==before);
    ms2x_linux_close();
    {
        struct can_frame serve_ping;
        const char *help[]={"ms2-updater","--help"};
        const char *update[]={"ms2-updater","update","--target","1A2B3C4D","--firmware",argv[1]};
        const char *boot[]={"ms2-updater","update","--target","1A2B3C4D","--bootloader","--firmware",argv[1]};
        const char *wrong[]={"ms2-updater","update","--target","00000000"};
        const char *universal[]={"ms2-updater","update","--target","11223344","--hardware","0032"};
        const char *automatic[]={"ms2-updater","update"};
        const char *bad_uid[]={"ms2-updater","update","--target","1122334Z"};
        const char *bad_hardware[]={"ms2-updater","update","--hardware","0035"};
        const char *boot_probe[]={"ms2-updater","probe","--bootloader","--seconds","1"};
        const char *bad_query[]={"ms2-updater","update","--bootloader","--query"};
        const char *serve[]={"ms2-updater","serve-resources","--interface","can0","--resources","/offline/resources","--seconds","180"};
        const char *serve_target[]={"ms2-updater","serve-resources","--resources","/offline/resources","--target","1A2B3C4D","--seconds","30"};
        const char *serve_boot[]={"ms2-updater","serve-resources","--resources","/offline/resources","--bootloader"};
        const char *serve_query[]={"ms2-updater","serve-resources","--resources","/offline/resources","--query"};
        const char *passive[]={"ms2-updater","probe","--interface","testcan","--seconds","1"};
        const char *active[]={"ms2-updater","probe","--seconds","1","--query"};
        before=sockets;
        CHECK(cli(2,help)==0);
        CHECK(cli(6,update)==77); CHECK(cli(7,boot)==77);
        CHECK(cli(4,wrong)==2); CHECK(sockets==before);
        CHECK(cli(6,universal)==77); CHECK(cli(2,automatic)==77);
        CHECK(cli(4,bad_uid)==2); CHECK(cli(4,bad_hardware)==2);
        CHECK(cli(4,bad_query)==2 && sockets==before);
        CHECK(cli(5,serve_boot)==2 && cli(5,serve_query)==2 && sockets==before);
        /* No reply: exactly the probe ping, no advertisement or resources. */
        before=writes; now=0U;
        CHECK(cli(8,serve)==1 && writes==before+1U && now==10000U);
        CHECK(transmitted.can_id==(CAN_EFF_FLAG|0x00304711U) && transmitted.can_dlc==0U);
        serve_ping=transmitted;
        /* Exact first-trial target and window; syscalls remain wrapped. */
        receive_queue[0].can_id=CAN_EFF_FLAG|0x00310b0dU; receive_queue[0].can_dlc=8U;
        { const uint8_t id[]={0x1aU,0x2bU,0x3cU,0x4dU,3U,112U,0U,0x33U}; memcpy(receive_queue[0].data,id,8U); }
        before=writes; reply_after_write=before+1U;
        queue_count=1U; queue_pos=0U; now=0U;
        CHECK(cli(8,serve_target)==0 && writes==before+2U && now==30000U);
        CHECK(transmitted.can_id==(CAN_EFF_FLAG|0x00315a21U) && transmitted.can_dlc==8U);
        { const uint8_t ad[8]=MS2X_RESOURCE_ADVERTISEMENT; CHECK(memcmp(transmitted.data,ad,8U)==0); }
        CHECK(installed_count==6U && installed[2].can_id==(CAN_EFF_FLAG|0x00400b0dU) && installed[2].can_mask==0xdfffffffU);
        /* Another MS2 must not satisfy the explicit target. */
        receive_queue[0].data[3]^=1U; queue_pos=0U; now=0U;
        before=writes; reply_after_write=before+1U;
        CHECK(cli(8,serve_target)==1 && writes==before+1U && now==10000U);
        CHECK(transmitted.can_id==serve_ping.can_id && transmitted.can_dlc==0U);
        queue_count=0U; queue_pos=0U; reply_after_write=0U;
        before=writes; now=0U;
        CHECK(cli(6,passive)==0 && writes==before);
        now=0U; CHECK(cli(5,active)==0 && writes==before+1U);
        CHECK(memcmp(&transmitted,&serve_ping,sizeof(serve_ping))==0);
        /* Observe and print an actual non-fixture UID/type/version, no writes. */
        received.can_id=CAN_EFF_FLAG|0x00319b22U; received.can_dlc=8U;
        { const uint8_t data[]={0x11U,0x22U,0x33U,0x44U,3U,112U,0U,0x32U}; memcpy(received.data,data,8U); }
        rx_remaining=1; read_result=16; now=0U; before=writes;
        CHECK(cli(6,passive)==0 && writes==before);
        received.can_id=CAN_EFF_FLAG|0x00379b22U; received.data[4]=0U; received.data[5]=0U;
        rx_remaining=1; now=0U;
        CHECK(cli(5,boot_probe)==0 && writes==before);
        read_result=-1; rx_remaining=-1;
    }
    boot_probe();
    real_identities();
    resource_policy();
    CHECK(sockets==closes);
    (void)printf("SocketCAN/CLI: %u checks passed; all socket/syscall entries wrapped; passive0 writes, query1, update0 opens\n",checks);
    return 0;
}