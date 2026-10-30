/* Native RAM syscall harness; reference events come from the existing
 * C/Python differential tests. No second peer or protocol engine is built.
 */
#include "ms2x_session.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, cases;
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"CAN sim FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
typedef struct { uint64_t at; ms2x_frame frame; int tx; } event;
static event trace[21000];
static ms2x_frame capture[11];
static size_t events, first_select, first_data, first_crc, second_crc;
static ms2x_firmware *firmware;
static uint8_t expected[MS2X_PADDED_SIZE], assembled[MS2X_PADDED_SIZE];
enum fault { NONE, SHORT_WRITE, WRITE_ERROR, AGAIN_ONCE, AGAIN_ALWAYS, DROP_ACK,
    EARLY_ACK, DELAY_ACK, READ_ERROR, READ_LENGTH, BAD_PING, BAD_STATUS,
    CUT_CRC, OLD_ACK, POLL_ERROR, POLL_FLAGS, POLL_EINTR, READ_EINTR,
    CLOCK_BACK, CLOCK_ERROR, FILTER_ERROR, FLOOD, LOCAL_ERROR, ERROR_FRAME, SLOW_WRITE,
    BAD_FW_LENGTH, POLL_EMPTY };
static struct {
    ms2x_engine *engine;
    enum fault fault;
    size_t pos, target, prefix_pos;
    uint64_t now, release;
    int value, fired, last_write, noise, pings, exact, block, integrated, serve, local_conflict;
    unsigned tx, rx, receipts, attempts, waits, parts, blocks, data, background, normal_pings;
    unsigned ping_left, bg_index, reads_since_yield, max_reads, ping_states;
    ms2x_tx submitted;
    ms2x_can_filter filters[2];
} sim;

static void memory_record(const ms2x_frame *f, uint8_t out[16])
{
    uint32_t id=f->id|MS2X_CAN_EFF;
    memset(out,0,16U); memcpy(out,&id,sizeof(id)); out[4]=f->dlc;
    memcpy(out+8U,f->data,f->dlc);
}
static ms2x_frame ping(void)
{
    ms2x_frame f={0};
    f.id=0x0031beefU; f.dlc=8U; f.extended=1U;
    f.data[0]=0x1aU; f.data[1]=0x2bU; f.data[2]=0x3cU; f.data[3]=0x4dU;
    f.data[4]=3U; f.data[5]=112U; f.data[7]=0x33U; return f;
}
static unsigned nibble(char c)
{
    if (c>='0' && c<='9') return (unsigned)(c-'0');
    if (c>='a' && c<='f') return (unsigned)(c-'a')+10U;
    CHECK(c>='A' && c<='F'); return (unsigned)(c-'A')+10U;
}
static void hex(const char *text, uint8_t *out, size_t length)
{
    size_t i;
    CHECK(strlen(text)==2U*length);
    for (i=0U;i<length;++i) out[i]=(uint8_t)(16U*nibble(text[2U*i])+nibble(text[2U*i+1U]));
}
static void load(const char *reference, const char *fixture)
{
    FILE *file=fopen(reference,"r");
    char line[256]; size_t count=0U;
    uint64_t start=0U, end=0U;
    CHECK(file!=NULL);
    while (fgets(line,sizeof(line),file)!=NULL) {
        char direction[3], wire_hex[27]; uint8_t wire[13];
        event *item;
        CHECK(events<21000U); item=&trace[events];
        CHECK(sscanf(line,"%" SCNu64 " %2s %26s",&item->at,direction,wire_hex)==3);
        hex(wire_hex,wire,13U); CHECK(ms2x_frame_decode(wire,13U,&item->frame)==0);
        CHECK(strcmp(direction,"TX")==0 || strcmp(direction,"RX")==0);
        item->tx=strcmp(direction,"TX")==0;
        if (item->tx && item->frame.dlc==6U && item->frame.data[4]==0x44U && first_select==0U) first_select=events;
        if (item->frame.id==0x00360300U && first_data==0U) first_data=events;
        if (item->tx && item->frame.dlc==7U && item->frame.data[4]==0x88U) {
            if (first_crc==0U) first_crc=events;
            else if (second_crc==0U) second_crc=events;
        }
        ++events;
    }
    CHECK(!ferror(file) && fclose(file)==0); CHECK(events==20598U);
    file=fopen(fixture,"r"); CHECK(file!=NULL);
    while (fgets(line,sizeof(line),file)!=NULL) {
        uint64_t seconds; unsigned micros; char interface[16], text[64], *hash, *tail;
        unsigned long id; size_t length;
        CHECK(count<11U);
        CHECK(sscanf(line,"(%" SCNu64 ".%6u) %15s %63s",&seconds,&micros,interface,text)==4);
        CHECK(strcmp(interface,"can0")==0); hash=strchr(text,'#'); CHECK(hash!=NULL && hash-text==8);
        *hash='\0'; id=strtoul(text,&tail,16); CHECK(*tail=='\0' && id<=0x1fffffffUL);
        length=strlen(hash+1); CHECK(length<=16U && length%2U==0U);
        capture[count].id=(uint32_t)id; capture[count].dlc=(uint8_t)(length/2U); capture[count].extended=1U;
        hex(hash+1,capture[count].data,length/2U);
        end=seconds*UINT64_C(1000000)+micros; if (count==0U) start=end;
        ++count;
    }
    CHECK(!ferror(file) && fclose(file)==0); CHECK(count==11U && end-start==4139844U);
}

int ms2x_sim_clock(uint64_t *now)
{
    if (sim.pos==sim.target && !sim.fired && sim.fault==CLOCK_ERROR) { sim.fired=1; return -1; }
    *now=sim.now;
    if (sim.pos==sim.target && !sim.fired && sim.fault==CLOCK_BACK) {
        CHECK(sim.now>0U); --*now; sim.fired=1;
    }
    return 0;
}
int ms2x_sim_filters(const ms2x_can_filter *filters, size_t count)
{
    CHECK((count==2U || (sim.integrated && (count==3U || count==6U))) && filters[0].id==0x80310000U && filters[1].id==0x80370000U);
    CHECK(filters[0].mask==0xdfff0000U && filters[1].mask==0xdfff0000U);
    if (count==3U) CHECK((filters[2].id==0x80400000U && filters[2].mask==0xdfff0000U) ||
                        (filters[2].id==0x80409b22U && filters[2].mask==0xdfffffffU));
    if (count==6U) {
        CHECK(filters[2].id==0x80400000U || filters[2].id==0x80409b22U);
        CHECK(filters[2].mask==(filters[2].id==0x80400000U ? 0xdfff0000U : 0xdfffffffU));
        CHECK(filters[3].id==0x80410000U && filters[4].id==0x80420000U);
        CHECK(filters[3].mask==0xdfff0000U && filters[4].mask==0xdfff0000U);
        CHECK(filters[5].id==0x80300000U && filters[5].mask==0xdfff0000U);
    }
    memcpy(sim.filters,filters,sizeof(sim.filters));
    return sim.fault==FILTER_ERROR ? -1 : 0;
}
int ms2x_sim_wait(uint64_t until, int want_write, unsigned *ready, int *error)
{
    uint64_t next=until;
    ++sim.waits; sim.reads_since_yield=0U; *ready=0U; *error=0;
    CHECK(until>=sim.now);
    if (sim.fault==POLL_ERROR) { *error=EIO; return -1; }
    if (sim.fault==POLL_FLAGS) { *ready=MS2X_SIM_ERROR; return 1; }
    if (sim.fault==POLL_EMPTY) return 1;
    if (sim.fault==POLL_EINTR) {
        sim.now=until-sim.now>1000U ? sim.now+1000U : until; *error=EINTR; return -1;
    }
    if (sim.fault==FLOOD && sim.pos==sim.target) {
        sim.now=until>sim.now ? sim.now+1U : until; *ready=MS2X_SIM_RX; return 1;
    }
    if (want_write) {
        uint64_t room=sim.now+(sim.fault==AGAIN_ALWAYS ? 1000U : 1U);
        if (room<next) next=room;
        *ready=MS2X_SIM_TX;
    }
    if (sim.pos<events && !trace[sim.pos].tx) {
        uint64_t due=sim.fault==DELAY_ACK && sim.pos==sim.target ? sim.release : trace[sim.pos].at;
        if (due<=next) { next=due<sim.now ? sim.now : due; *ready|=MS2X_SIM_RX; }
    }
    sim.now=next;
    return *ready==0U ? 0 : 1;
}
int ms2x_sim_write(const uint8_t *record, size_t length, int *error)
{
    uint8_t golden[16]; const ms2x_frame *f;
    CHECK(length==16U && sim.pos<events && trace[sim.pos].tx);
    if (!sim.integrated || (sim.engine!=NULL && ms2x_peek(sim.engine,&sim.submitted)==1))
        CHECK(ms2x_peek(sim.engine,&sim.submitted)==1);
    sim.reads_since_yield=0U; ++sim.attempts; *error=0;
    f=&trace[sim.pos].frame;
    if (sim.serve) {
        static const uint8_t ad[8]=MS2X_RESOURCE_ADVERTISEMENT;
        CHECK((f->id==0x00304711U && f->dlc==0U) ||
              (f->id==0x00315a21U && f->dlc==8U && memcmp(f->data,ad,8U)==0) || ((f->id&0xffff0000U)==0x00410000U && f->dlc==8U) ||
                         ((f->id&0xffff0000U)==0x00420000U && (f->dlc==6U || f->dlc==8U)));
    }
    memory_record(f,golden); CHECK(memcmp(record,golden,16U)==0);
    if (sim.pos==sim.target) {
        if (sim.fault==SHORT_WRITE) { sim.last_write=sim.value; return sim.value; }
        if (sim.fault==WRITE_ERROR) { sim.last_write=-1; *error=sim.value; return -1; }
        if (sim.fault==AGAIN_ALWAYS || (sim.fault==AGAIN_ONCE && !sim.fired)) {
            sim.fired=1; sim.last_write=-1; *error=EAGAIN; return -1;
        }
    }
    if (sim.exact) CHECK(sim.now==trace[sim.pos].at);
    if ((f->id&0xffffff80U)==0x00360300U) {
        size_t offset=(size_t)(sim.block-4)*1024U+(size_t)sim.parts*8U;
        CHECK(sim.block>=4 && sim.block<=159 && sim.parts<128U);
        CHECK((f->id&127U)==sim.parts && f->dlc==8U);
        CHECK(memcmp(record+8U,expected+offset,8U)==0);
        memcpy(assembled+offset,record+8U,8U); ++sim.parts; ++sim.data;
    } else if (f->dlc==6U && f->data[4]==0x44U) {
        CHECK((unsigned)f->data[5]==159U-sim.blocks); sim.block=f->data[5]; sim.parts=0U;
    } else if (f->dlc==7U && f->data[4]==0x88U) {
        uint16_t crc=ms2x_crc16(assembled+(size_t)(sim.block-4)*1024U,1024U);
        CHECK(sim.parts==128U); CHECK(f->data[5]==(uint8_t)(crc>>8) && f->data[6]==(uint8_t)crc); ++sim.blocks;
        if (sim.fault==DELAY_ACK && sim.pos+1U==sim.target)
            sim.release=sim.now+5000U+(uint64_t)(sim.value+1)-1U;
    }
    ++sim.pos; ++sim.tx; sim.last_write=16;
    if (sim.fault==SLOW_WRITE && sim.pos-1U==sim.target) sim.now+=5000U;
    return 16;
}
/* Link-time wrapper observes actual callbacks without changing production code. */
int __real_ms2x_tx_processed(ms2x_engine *e, const ms2x_tx *item, uint64_t now);
int __wrap_ms2x_tx_processed(ms2x_engine *e, const ms2x_tx *item, uint64_t now);
int __wrap_ms2x_tx_processed(ms2x_engine *e, const ms2x_tx *item, uint64_t now)
{
    CHECK(sim.last_write==16 && item->owner==sim.submitted.owner && item->sequence==sim.submitted.sequence);
    CHECK(ms2x_frame_equal(&item->frame,&sim.submitted.frame));
    ++sim.receipts; sim.last_write=0;
    return __real_ms2x_tx_processed(e,item,now);
}
int ms2x_sim_read(uint8_t *record, size_t capacity, int *local, int *error)
{
    ms2x_frame f;
    CHECK(capacity==16U); *error=0; *local=0; ++sim.reads_since_yield;
    if (sim.reads_since_yield>sim.max_reads) sim.max_reads=sim.reads_since_yield;
    if (sim.pos==sim.target) {
        if (sim.fault==DROP_ACK || sim.fault==POLL_EINTR) { ++sim.pos; }
        else if (sim.fault==READ_ERROR || sim.fault==CUT_CRC) { *error=EIO; return -1; }
        else if (sim.fault==READ_LENGTH) { memset(record,0,16U); return sim.value; }
        else if (sim.fault==READ_EINTR && !sim.fired) { sim.fired=1; *error=EINTR; return -1; }
        else if (sim.fault==FLOOD) { ++sim.now; ++sim.normal_pings; f=ping(); memory_record(&f,record); return 16; }
        else if (sim.fault==EARLY_ACK) { f=trace[first_crc+1U].frame; memory_record(&f,record); return 16; }
        else if (sim.fault==BAD_PING || sim.fault==BAD_STATUS || sim.fault==LOCAL_ERROR || sim.fault==ERROR_FRAME || sim.fault==BAD_FW_LENGTH) {
            f=ping();
            if (sim.fault==BAD_PING) { if (sim.value==7) f.dlc=7U; else f.data[6]=1U; }
            if (sim.fault==BAD_STATUS) { f.id=0x00370000U; f.dlc=5U; f.data[4]=(uint8_t)sim.value; }
            if (sim.fault==BAD_FW_LENGTH) { f.id=0x00370000U; f.dlc=(uint8_t)sim.value; f.data[4]=0x88U; }
            memory_record(&f,record);
            if (sim.fault==LOCAL_ERROR) *local=2;
            if (sim.fault==ERROR_FRAME) { uint32_t id=MS2X_CAN_EFF|MS2X_CAN_ERR|f.id; memcpy(record,&id,sizeof(id)); *local=1; }
            return 16;
        }
    }
    if (sim.prefix_pos!=sim.pos) { sim.prefix_pos=sim.pos; sim.ping_left=sim.pings ? 2U : 0U; sim.bg_index=0U; }
    if (sim.ping_left!=0U) {
        if (ms2x_get_state(sim.engine)==MS2X_WAIT_PING) sim.ping_left=0U;
        else { --sim.ping_left; f=ping(); memory_record(&f,record); ++sim.normal_pings;
            sim.ping_states|=1U<<(unsigned)ms2x_get_state(sim.engine); return 16; }
    }
    if (sim.noise && sim.pos<events && !trace[sim.pos].tx && sim.bg_index<11U) {
        memory_record(&capture[sim.bg_index++],record); ++sim.background; return 16;
    }
    if (sim.pos>=events || trace[sim.pos].tx) { *error=EAGAIN; return -1; }
    if (sim.now<(sim.fault==DELAY_ACK && sim.pos==sim.target ? sim.release : trace[sim.pos].at)) { *error=EAGAIN; return -1; }
    f=trace[sim.pos].frame;
    if (sim.local_conflict && sim.pos==2U) *local=1;
    if (sim.fault==OLD_ACK && sim.pos==sim.target) {
        CHECK(ms2x_frame_equal(&f,&trace[first_crc+1U].frame)); f=trace[first_crc+1U].frame; sim.fired=1;
    }
    memory_record(&f,record); ++sim.pos; ++sim.rx; return 16;
}

static void run(const char *name, enum fault fault, size_t target, int value, int noise, int pings, ms2x_error outcome)
{
    int result; unsigned tx_before, callbacks_before;
    memset(&sim,0,sizeof(sim)); memset(assembled,0,sizeof(assembled));
    sim.fault=fault; sim.target=target; sim.prefix_pos=SIZE_MAX; sim.value=value;
    sim.noise=noise; sim.pings=pings; sim.exact=fault==NONE && !noise && !pings;
    sim.engine=ms2x_engine_new(firmware,5000U); CHECK(sim.engine!=NULL);
    result=ms2x_can_sim_run(sim.engine);
    CHECK(ms2x_get_error(sim.engine)==outcome);
    if (outcome==MS2X_OK) {
        CHECK(result==0 && ms2x_get_state(sim.engine)==MS2X_COMPLETE_UNVERIFIED);
        CHECK(sim.pos==events && sim.tx==20284U && sim.rx==314U && sim.receipts==sim.tx);
        CHECK(sim.data==19968U && sim.blocks==156U && ms2x_completed(sim.engine)==156U);
        CHECK(memcmp(assembled,expected,sizeof(expected))==0);
    } else {
        CHECK(result!=0 && ms2x_get_state(sim.engine)==MS2X_ABORTED);
        CHECK(ms2x_completed(sim.engine)==0U && ms2x_peek(sim.engine,&sim.submitted)==0);
    }
    tx_before=sim.tx; callbacks_before=sim.receipts;
    CHECK(ms2x_can_sim_run(sim.engine)!=0); CHECK(sim.tx==tx_before && sim.receipts==callbacks_before);
    (void)printf("case %u %s: error=%d tx=%u receipts=%u rx=%u background=%u pings=%u max_rx=%u old_ack=%d\n",
        ++cases,name,(int)outcome,sim.tx,sim.receipts,sim.rx,sim.background,sim.normal_pings,sim.max_reads,sim.fault==OLD_ACK && sim.fired);
    ms2x_engine_free(sim.engine);
}
static void integrated(const char *tail, const char *image_path)
{
    event saved[21000];
    size_t base=events, old_select=first_select, old_crc=first_crc;
    unsigned scenario;
    char directory[1024];
    const char *slash=strrchr(image_path,'/');
    CHECK(slash!=NULL && (size_t)(slash-image_path)<sizeof(directory));
    memcpy(directory,image_path,(size_t)(slash-image_path)); directory[slash-image_path]='\0';
    memcpy(saved,trace,base*sizeof(trace[0]));
    for (scenario=0U;scenario<18U;++scenario) {
        ms2x_session_config config={1,0,0,MS2X_UID,directory,1000U,NULL,0,0U};
        ms2x_session_result result;
        ms2x_identity known={0};
        FILE *file;
        char line[256];
        uint64_t end;
        size_t resource_data=0U, app_index;
        int outcome;
        memcpy(trace,saved,base*sizeof(trace[0])); events=base;
        memset(&sim,0,sizeof(sim)); memset(assembled,0,sizeof(assembled));
        sim.integrated=1; sim.prefix_pos=SIZE_MAX; sim.target=SIZE_MAX;
        sim.exact=scenario==0U || scenario==1U || scenario==16U; sim.noise=scenario==6U;
        if (scenario==1U || scenario==2U || scenario==3U || scenario==15U || scenario==16U) {
            trace[0]=saved[3]; trace[0].at=0U; trace[1]=saved[4]; trace[1].at=1U;
            memmove(trace+2,saved+5,(base-5U)*sizeof(trace[0])); events=base-3U;
            for (size_t i=2U;i<events;++i) trace[i].at-=500U;
            config.bootloader=1;
            if (scenario==2U) trace[1].frame.data[7]=0x34U;
            if (scenario==3U) trace[1].frame.data[0]=0x2bU;
        }
        end=trace[events-1U].at; app_index=events;
        file=fopen(tail,"r"); CHECK(file!=NULL);
        while (fgets(line,sizeof(line),file)!=NULL) {
            char direction[3], wire_hex[27]; uint8_t wire[13];
            CHECK(events<21000U);
            CHECK(sscanf(line,"%" SCNu64 " %2s %26s",&trace[events].at,direction,wire_hex)==3);
            trace[events].at+=end; hex(wire_hex,wire,13U);
            CHECK(ms2x_frame_decode(wire,13U,&trace[events].frame)==0);
            trace[events].tx=strcmp(direction,"TX")==0;
            if (trace[events].tx && trace[events].frame.id==0x00429b22U && trace[events].frame.dlc==8U)
                resource_data=events;
            ++events;
        }
        CHECK(fclose(file)==0);
        if (scenario==4U) { sim.fault=READ_ERROR; sim.target=old_crc; }
        if (scenario==5U) { sim.fault=READ_ERROR; sim.target=resource_data; }
        if (scenario==7U) trace[1].frame.data[7]=0x34U;
        if (scenario==8U) {
            /* Same UID cannot silently rebind after application identification. */
            trace[app_index+1U].frame=trace[app_index].frame;
            trace[app_index+1U].frame.id=0x0031aaaaU;
        }
        if (scenario==9U) config.observe_ms=30U;
        if (scenario==16U) { events=app_index+1U; config.observe_ms=30000U; } /* Verified5.6, no requests. */
        if (scenario==17U) trace[app_index].frame.data[5]=5U; /* Start alone is not verified5.6. */
        if (scenario==10U) { sim.fault=AGAIN_ONCE; sim.target=resource_data; }
        if (scenario==11U || scenario==12U) {
            events=2U; trace[0]=saved[1]; trace[0].at=0U; trace[1]=saved[4]; trace[1].at=1U;
            trace[2].at=2U; trace[2].tx=0; memset(&trace[2].frame,0,sizeof(trace[2].frame));
            trace[2].frame.id=0x00409b22U; trace[2].frame.extended=1U; trace[2].frame.dlc=8U;
            memcpy(trace[2].frame.data,"langver",7U); ++events;
            config.update=0; config.bootloader=0; config.resources=NULL;
            if (scenario==12U) { /* explicit active probe */
                memmove(trace+1,trace,events*sizeof(trace[0])); ++events;
                trace[0]=saved[0]; trace[1].at=1U; trace[2].at=2U; trace[3].at=3U; config.query=1;
            }
        }
        if (scenario==13U) { sim.now=UINT64_MAX-2000U; config.observe_ms=1U; }
        if (scenario==14U || scenario==15U) {
            config.target=0x1234abcdU;
            for (size_t i=0U;i<events;++i) {
                ms2x_frame *f=&trace[i].frame;
                uint32_t cmd=f->id&0xffff0000U;
                if ((cmd==0x00310000U || cmd==0x00370000U || f->id==0x00364711U || f->id==MS2X_HASH) &&
                    f->dlc>=4U && f->data[0]==0x1aU && f->data[1]==0x2bU && f->data[2]==0x3cU && f->data[3]==0x4dU) {
                    f->data[0]=0x12U; f->data[1]=0x34U; f->data[2]=0xabU; f->data[3]=0xcdU;
                }
            }
            if (scenario==14U) {
                uint64_t reset_at=trace[2].at;
                CHECK(ms2x_identify(&trace[1].frame,&known)==1);
                config.known=&known;
                memmove(trace,trace+2,(events-2U)*sizeof(trace[0])); events-=2U;
                for (size_t i=0U;i<events;++i) trace[i].at-=reset_at;
                sim.exact=1;
            }
        }
        sim.engine=config.update ? ms2x_engine_new_target(firmware,5000U,config.target,0x33U) : NULL;
        outcome=ms2x_session_run(sim.engine,&config,&result);
        if (scenario==2U || scenario==3U || scenario==4U || scenario==5U || scenario==7U || scenario==8U || scenario==9U || scenario==13U || scenario==17U) {
            CHECK(outcome==-1 && result.error!=MS2X_OK && !result.resources_confirmed);
            CHECK(ms2x_update_exit_status(outcome,&result)==(scenario==5U || scenario==8U || scenario==9U ? 3 : 1));
            if (scenario!=5U && scenario!=8U && scenario!=9U && scenario!=17U) CHECK(!result.start_local);
            if (scenario==17U) CHECK(result.start_local && !result.app_version_ok);
            if (scenario==3U) CHECK(result.error==MS2X_TIMEOUT && sim.tx==1U);
            if (scenario==13U) CHECK(result.error==MS2X_CLOCK && sim.tx==0U);
        } else {
            CHECK(outcome==0 && result.error==MS2X_OK && result.window_ended);
            CHECK(sim.pos==events && !result.resources_confirmed);
            if (config.update) {
                CHECK(result.firmware_local && result.firmware_protocol && result.start_local);
                CHECK(result.app_seen && result.app_version_ok && result.requester_hash==0x9b22U);
                CHECK(ms2x_update_exit_status(outcome,&result)==0);
                if (scenario==16U) {
                    ms2x_session_result repeat;
                    unsigned sent=sim.tx;
                    CHECK(result.streams==0U && !result.resources_offered && result.resource_requests==0U);
                    CHECK(sim.now==end+1U+30000U && sent==20282U);
                    CHECK(ms2x_session_run(sim.engine,&config,&repeat)==-1 && repeat.error==MS2X_INVALID && sim.tx==sent);
                } else CHECK(result.streams==2U && result.resources_offered && result.foreign_requests==1U);
                CHECK(sim.blocks==156U && memcmp(assembled,expected,sizeof(expected))==0);
            } else {
                CHECK(result.normal_seen==1U && result.boot_seen==1U);
                CHECK(sim.tx==(config.query ? 1U : 0U) && !result.start_local);
            }
        }
        CHECK(sim.max_reads<=8U);
        (void)printf("Integrated session %u: result=%d error=%d tx=%u blocks=%u app=%d streams=%u confirmed=%d exit=%d\n",
                     scenario,outcome,(int)result.error,sim.tx,sim.blocks,result.app_seen,result.streams,result.resources_confirmed,
                     config.update ? ms2x_update_exit_status(outcome,&result) : outcome!=0);
        ms2x_engine_free(sim.engine);
    }
    events=base; first_select=old_select; first_crc=old_crc;
    (void)printf("Integrated sessions:18 scenarios passed; original trace and RAM syscall harness reused\n");
}
static ms2x_frame discovered(uint32_t uid, uint16_t hardware, uint16_t hash, int boot)
{
    ms2x_frame f={0};
    f.id=(boot ? 0x00370000U : 0x00310000U)|hash; f.dlc=8U; f.extended=1U;
    f.data[0]=(uint8_t)(uid>>24); f.data[1]=(uint8_t)(uid>>16);
    f.data[2]=(uint8_t)(uid>>8); f.data[3]=(uint8_t)uid;
    f.data[4]=boot ? 0U : 3U; f.data[5]=boot ? 0U : 112U;
    f.data[6]=(uint8_t)(hardware>>8); f.data[7]=(uint8_t)hardware; return f;
}
static void discovery(void)
{
    unsigned scenario, before=checks;
    for (scenario=0U;scenario<19U;++scenario) {
        ms2x_session_config config={0,0,0,0U,NULL,100U,NULL,0,0U};
        ms2x_session_result r;
        ms2x_identity id;
        int selection;
        memset(&sim,0,sizeof(sim)); sim.integrated=1; sim.target=SIZE_MAX; sim.prefix_pos=SIZE_MAX;
        events=1U; trace[0].frame=discovered(0x11223344U,(uint16_t)(0x32U+scenario%3U),0x9b22U,0);
        if (scenario==3U || scenario==11U) {
            events=scenario==3U ? 3U : 17U;
            for (size_t i=0U;i<events;++i)
                trace[i].frame=discovered(0x11223344U+(uint32_t)i,(uint16_t)(0x32U+i%3U),(uint16_t)(0x9000U+i),0);
        } else if (scenario==4U || scenario==5U || scenario==6U || scenario==12U || scenario==13U || scenario==16U || scenario==17U || scenario==18U) {
            trace[1].frame=trace[0].frame; events=2U;
            if (scenario==5U) trace[1].frame.data[7]=0x33U;
            if (scenario==6U) trace[1].frame.id=0x0031aaaaU;
            if (scenario==12U) trace[1].frame=discovered(0x11223344U,0x32U,0xaaaaU,1);
            if (scenario==13U) ++trace[1].frame.data[3];
            /* An unrelated CAN device needs its own synthetic UID. Same UID
             * with a different type is a conflict, already tested above. */
            if (scenario==16U) trace[1].frame=discovered(0x55667788U,0x0011U,0xbbbbU,0);
            if (scenario==17U) ++trace[1].frame.data[5];
            if (scenario==18U) trace[1].frame.dlc=7U;
        }
        if (scenario==7U) trace[0].frame.data[7]=0x70U;
        if (scenario==8U || scenario==9U || scenario==15U) trace[0].frame=discovered(0x11223344U,0x34U,0x9b22U,1);
        if (scenario==9U) { trace[0].frame.dlc=5U; trace[0].frame.data[4]=0x88U; }
        if (scenario==10U) trace[0].frame.dlc=7U;
        if (scenario==14U) memset(trace[0].frame.data,0,4U);
        if (scenario==15U) trace[0].frame.data[4]=0xf1U;
        for (size_t i=0U;i<events;++i) { trace[i].tx=0; trace[i].at=i+1U; }
        CHECK(ms2x_session_run(NULL,&config,&r)==0 && sim.pos==events && sim.tx==0U && sim.max_reads<=8U);
        selection=ms2x_select_device(&r,0U,scenario==8U,&id);
        if (scenario<=2U || scenario==4U || scenario==8U || scenario==12U || scenario==16U) {
            CHECK(selection==MS2X_SELECTED && id.uid==0x11223344U);
            CHECK(ms2x_profile_for(id.hardware)!=NULL);
            if (scenario<=2U) CHECK(id.hardware==0x32U+scenario);
            if (scenario==8U) CHECK(id.bootloader && ms2x_select_device(&r,0U,0,&id)==MS2X_SELECT_NONE);
            if (scenario==12U) CHECK(ms2x_select_device(&r,0U,1,&id)==MS2X_SELECTED && id.bootloader && id.major==0U);
        } else if (scenario==7U || scenario==9U || scenario==15U) {
            CHECK(selection==MS2X_SELECT_NONE);
            if (scenario==7U) CHECK(ms2x_select_device(&r,0x11223344U,0,&id)==MS2X_SELECT_UNSUPPORTED);
            if (scenario==9U || scenario==15U) CHECK(ms2x_select_device(&r,0U,1,&id)==MS2X_SELECT_NONE);
        } else {
            CHECK(selection==MS2X_SELECT_AMBIGUOUS);
            if (scenario==3U) {
                for (unsigned i=0U;i<3U;++i) CHECK(ms2x_select_device(&r,0x11223344U+i,0,&id)==MS2X_SELECTED && id.hardware==0x32U+i);
                CHECK(ms2x_select_device(&r,0x11223399U,0,&id)==MS2X_SELECT_NONE);
            } else if (scenario!=10U && scenario!=14U)
                CHECK(ms2x_select_device(&r,0x11223344U,0,&id)==MS2X_SELECT_AMBIGUOUS);
        }
        if (scenario==4U) CHECK(r.device_count==1U);
        if (scenario==16U) {
            CHECK(r.device_count==2U);
            CHECK(ms2x_select_device(&r,0x55667788U,0,&id)==MS2X_SELECT_UNSUPPORTED);
        }
        if (scenario==11U) CHECK(r.device_overflow && r.device_count==MS2X_MAX_DEVICES);
    }
    (void)printf("Universal detection:19 scenarios, %u checks passed; same bounded RAM harness, zero writes\n",checks-before);
}
static void standalone(const char *tail, const char *image)
{
    static event saved[21000];
    char directory[1024], reference[1024], line[256];
    const char *slash=strrchr(image,'/'), *end=strrchr(tail,'/');
    unsigned before=checks, scenario;
    size_t count=0U, first_tx=0U, service_data=0U;
    FILE *file;
    CHECK(slash!=NULL && end!=NULL && (size_t)(slash-image)<sizeof(directory));
    memcpy(directory,image,(size_t)(slash-image)); directory[slash-image]='\0';
    CHECK(snprintf(reference,sizeof(reference),"%.*s/resource-service.events",(int)(end-tail),tail)>0);
    file=fopen(reference,"r"); CHECK(file!=NULL);
    while (fgets(line,sizeof(line),file)!=NULL) {
        char direction[3], text[27]; uint8_t wire[13];
        CHECK(count<21000U && sscanf(line,"%" SCNu64 " %2s %26s",&saved[count].at,direction,text)==3);
        hex(text,wire,13U); CHECK(ms2x_frame_decode(wire,13U,&saved[count].frame)==0);
        saved[count].tx=strcmp(direction,"TX")==0;
        if (saved[count].tx && saved[count].frame.id==0x00315a21U && first_tx==0U) first_tx=count;
        if (saved[count].frame.id==0x00429b22U && saved[count].frame.dlc==8U && service_data==0U) service_data=count;
        ++count;
    }
    CHECK(fclose(file)==0);
    for (scenario=0U;scenario<20U;++scenario) {
        ms2x_session_config config={0,0,0,0U,directory,10000U,NULL,1,0x5a21U};
        ms2x_session_result r;
        int result;
        memcpy(trace,saved,count*sizeof(trace[0])); events=count;
        memset(&sim,0,sizeof(sim)); sim.integrated=1; sim.serve=1; sim.exact=1; sim.target=SIZE_MAX; sim.prefix_pos=SIZE_MAX;
        if (scenario==1U || scenario==2U || (scenario>=5U && scenario<=8U) || scenario==14U || scenario>=17U) {
            memmove(trace+3,trace+2,(events-2U)*sizeof(trace[0])); ++events;
            trace[2].at=2U; trace[2].tx=0;
            trace[2].frame=discovered(0x55667788U,0x33U,0xbbbbU,0);
            if (scenario==2U) config.target=0x1234abcdU;
            if (scenario==5U) trace[2].frame=capture[9]; /* Real B33E/FFF0 CS-compatible identity. */
            if (scenario==6U) trace[2].frame=discovered(0x11223344U,0x0011U,config.server_hash,0);
            if (scenario==17U) trace[2].frame=discovered(MS2X_RESOURCE_UID,0xffffU,0xbbbbU,0);
            if (scenario==18U) { trace[2].frame.id=0x00300000U|config.server_hash; trace[2].frame.dlc=0U; }
            if (scenario==19U) trace[2].frame=discovered(0x2c28fa16U,0xfff0U,config.server_hash,0);
            if (scenario==7U || scenario==8U || scenario==14U) {
                trace[2].frame.id=scenario==8U ? 0x00429b22U : 0x0041aaaaU;
                memcpy(trace[2].frame.data,"langver",8U);
                if (scenario==14U) sim.local_conflict=1;
            }
        }
        if (scenario==3U) { memmove(trace+1,trace+2,(events-2U)*sizeof(trace[0])); --events; }
        if (scenario==4U) {
            config.server_hash=MS2X_HASH;
            CHECK(ms2x_session_run(NULL,&config,&r)==-1 && r.error==MS2X_INVALID && sim.tx==0U);
            config.server_hash=0U;
        }
        if (scenario==9U) config.resources="/nonexistent-ms2-phase25-resources";
        if (scenario==10U) { config.observe_ms=5020U; trace[2].at=2510U; } /* Echo at5010, stop before header5060. */
        if (scenario==11U) { sim.fault=READ_ERROR; sim.target=service_data; }
        if (scenario==12U) {
            trace[3].frame=discovered(0x1234abcdU,0x33U,0xaaaaU,0);
            trace[3].at=5002U;
        }
        if (scenario==13U) { sim.fault=AGAIN_ONCE; sim.target=first_tx; sim.exact=0; }
        if (scenario==15U) trace[1].frame=discovered(0x1234abcdU,0x33U,0x9b22U,1);
        if (scenario==16U) { events=3U; } /* Valid identity, no unsolicited resources. */
        result=ms2x_session_run(NULL,&config,&r);
        CHECK(!r.firmware_local && !r.firmware_protocol && !r.start_local && !r.resources_confirmed);
        if (scenario==0U || scenario==2U || scenario==5U || scenario==13U || scenario==16U) {
            CHECK(result==0 && r.window_ended && sim.pos==events && sim.now==10000U);
            CHECK(r.resource_target.uid==0x1234abcdU && r.resource_target.major==3U && r.requester_hash==0x9b22U);
            CHECK(r.streams==(scenario==16U ? 0U : 16U));
            CHECK(r.resource_requests==(scenario==16U ? 0U : 24U));
            CHECK(r.foreign_requests==(scenario==16U ? 0U : 1U));
            CHECK(r.advertisement_local);
            if (scenario==16U) CHECK(sim.tx==2U && !r.resources_offered);
        } else {
            CHECK(result==-1 && r.error!=MS2X_OK);
            if (scenario!=9U && scenario!=10U && scenario!=11U && scenario!=12U) CHECK(sim.tx==(scenario==4U ? 0U : 1U));
            if (scenario==3U || scenario==15U) CHECK(!r.advertisement_local && r.streams==0U);
            if (scenario==4U) CHECK(r.error==MS2X_INVALID);
            if (scenario==6U || scenario==7U || scenario==8U || scenario==14U || scenario>=17U) CHECK(r.server_conflict);
            if (scenario==6U || scenario==17U || scenario==19U) CHECK(strcmp(r.reason,"resource server hash or UID collision")==0);
            if (scenario==18U) CHECK(strcmp(r.reason,"another participant uses the resource server hash")==0);
            if (scenario==9U) CHECK(r.error==MS2X_IO && sim.tx==2U && r.streams==0U);
            if (scenario==10U) CHECK(r.error==MS2X_TIMEOUT && sim.now==5020U && sim.tx==3U && r.streams==0U);
        }
        CHECK(sim.max_reads<=8U);
        (void)printf("Resource service %u: result=%d error=%d tx=%u streams=%u conflict=%d firmware=0\n",
            scenario,result,(int)r.error,sim.tx,r.streams,r.server_conflict);
    }
    CHECK(first_tx>0U);
    (void)printf("Standalone resources:20 scenarios, %u checks passed; existing trace harness/prototype; no firmware TX\n",checks-before);
}
int main(int argc, char **argv)
{
    ms2x_error error; int n;
    int firmware_only=argc==6 && strcmp(argv[5],"--firmware-only")==0;
    int sessions_only=argc==6 && strcmp(argv[5],"--sessions-only")==0;
    static const int write_errors[]={EIO,ENOBUFS,EINTR,ETIMEDOUT};
    static const int statuses[]={0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0x99};
    size_t i;
    if (argc==2 && strcmp(argv[1],"--discovery-only")==0) { discovery(); return 0; }
    if (argc!=5 && !(argc==6 && strcmp(argv[5],"--resources-only")==0) && !firmware_only && !sessions_only) return 2;
    if (argc==6 && !firmware_only && !sessions_only) { load(argv[2],argv[3]); discovery(); standalone(argv[4],argv[1]); return 0; }
    firmware=ms2x_firmware_load(argv[1],&error); CHECK(firmware!=NULL && error==MS2X_OK);
    CHECK(ms2x_firmware_bytes(firmware,expected,sizeof(expected))==0); load(argv[2],argv[3]);
    if (sessions_only) { integrated(argv[4],argv[1]); ms2x_firmware_free(firmware); return 0; }
    CHECK(ms2x_can_sim_run(NULL)!=0);
    run("normal",NONE,0U,0,0,0,MS2X_OK); CHECK(sim.now==41873U && sim.max_reads<=8U);
    run("real-fixture-and-target-pings",NONE,0U,0,1,1,MS2X_OK);
    CHECK(sim.background==3454U && sim.normal_pings>40000U);
    CHECK((sim.ping_states&((1U<<MS2X_TX_DATA)|(1U<<MS2X_DATA_DELAY)|(1U<<MS2X_WAIT_CRC)))==
          ((1U<<MS2X_TX_DATA)|(1U<<MS2X_DATA_DELAY)|(1U<<MS2X_WAIT_CRC)));
    for (n=0;n<=17;++n) if (n!=16) run("short-or-oversized-write",SHORT_WRITE,first_crc,n,0,0,MS2X_IO);
    for (i=0U;i<sizeof(write_errors)/sizeof(write_errors[0]);++i)
        run("fatal-write",WRITE_ERROR,first_crc,write_errors[i],0,0,MS2X_IO);
    run("eagain-once",AGAIN_ONCE,first_data,0,0,0,MS2X_OK); CHECK(sim.attempts==sim.tx+1U);
    run("queue-full-deadline",AGAIN_ALWAYS,0U,0,0,1,MS2X_TIMEOUT); CHECK(sim.tx==0U && sim.receipts==0U && sim.now==5000U);
    run("missing-ping",DROP_ACK,1U,0,0,0,MS2X_TIMEOUT);
    run("missing-selector-ack",DROP_ACK,first_select+1U,0,0,0,MS2X_TIMEOUT);
    run("missing-crc-ack",DROP_ACK,first_crc+1U,0,0,0,MS2X_TIMEOUT);
    run("early-crc-ack",EARLY_ACK,first_crc,0,0,0,MS2X_PROTOCOL);
    run("early-ack-during-data",EARLY_ACK,first_data,0,0,0,MS2X_PROTOCOL);
    run("delayed-crc-before-deadline",DELAY_ACK,first_crc+1U,-1,0,0,MS2X_OK);
    run("crc-at-deadline",DELAY_ACK,first_crc+1U,0,0,0,MS2X_TIMEOUT);
    run("crc-after-deadline",DELAY_ACK,first_crc+1U,1,0,0,MS2X_TIMEOUT);
    run("write-finishes-on-deadline",SLOW_WRITE,first_crc,0,0,0,MS2X_TIMEOUT); CHECK(sim.tx==sim.receipts+1U);
    run("bad-ping-length",BAD_PING,first_crc+1U,7,0,0,MS2X_PROTOCOL);
    run("bad-ping-hardware",BAD_PING,first_crc+1U,0,0,0,MS2X_PROTOCOL);
    for (i=0U;i<sizeof(statuses)/sizeof(statuses[0]);++i)
        run("target-negative-or-unknown-status",BAD_STATUS,first_crc+1U,statuses[i],0,0,MS2X_PROTOCOL);
    for (n=4;n<=8;n+=2) run("invalid-firmware-ack-length",BAD_FW_LENGTH,first_crc+1U,n,0,0,MS2X_PROTOCOL);
    run("cut-after-last-data-before-crc",CUT_CRC,first_crc,0,0,0,MS2X_IO); CHECK(sim.parts==128U && sim.blocks==0U);
    run("old-crc-ack-still-ambiguous",OLD_ACK,second_crc+1U,0,0,0,MS2X_OK); CHECK(sim.fired);
    run("read-error",READ_ERROR,first_crc,0,0,0,MS2X_IO);
    for (n=0;n<=64;++n) if (n==0 || n==15 || n==17 || n==64)
        run("invalid-read-length",READ_LENGTH,first_crc,n,0,0,MS2X_IO);
    run("read-eintr",READ_EINTR,first_crc,0,0,0,MS2X_OK);
    run("poll-error",POLL_ERROR,0U,0,0,0,MS2X_IO);
    run("poll-error-flags",POLL_FLAGS,0U,0,0,0,MS2X_IO);
    run("poll-ready-without-events",POLL_EMPTY,0U,0,0,0,MS2X_IO);
    run("poll-eintr-does-not-renew-timeout",POLL_EINTR,1U,0,0,0,MS2X_TIMEOUT); CHECK(sim.now==5000U);
    run("clock-backwards",CLOCK_BACK,first_crc,0,0,0,MS2X_CLOCK);
    run("clock-error",CLOCK_ERROR,first_crc,0,0,0,MS2X_IO);
    run("filter-error",FILTER_ERROR,0U,0,0,0,MS2X_IO); CHECK(sim.tx==0U);
    run("rx-flood-deadline",FLOOD,first_crc+1U,0,0,0,MS2X_TIMEOUT); CHECK(sim.max_reads<=8U);
    run("rx-flood-yields-to-pending-tx",FLOOD,first_data,0,0,0,MS2X_OK);
    CHECK(sim.max_reads<=8U && sim.normal_pings>0U && sim.normal_pings<=MS2X_SIM_RX_BUDGET);
    run("invalid-local-origin",LOCAL_ERROR,first_crc,0,0,0,MS2X_IO);
    run("local-error-frame-not-hidden",ERROR_FRAME,first_crc,0,0,0,MS2X_IO);
    (void)printf("CAN simulated loop: %u cases, %u assertions passed; no sockets, fds or hardware\n",cases,checks);
    integrated(argv[4],argv[1]);
    if (!firmware_only) { discovery(); standalone(argv[4],argv[1]); }
    ms2x_firmware_free(firmware);
    return 0;
}
