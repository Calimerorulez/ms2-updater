/* Native offline resource tests. Only original files and temporary fixtures. */
#define _POSIX_C_SOURCE 200809L
#include "ms2x_resources.h"
#include "ms2x_can_offline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned assertions, cases;
#define CHECK(x) do { ++assertions; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static const char *root;
static const struct {
    const char *file, *name, *version, *text;
    size_t size;
    uint16_t crc[3]; /* Independent Python crc_hqx, first/middle/last part. */
} files[] = {
    {"lang.ms2","lang","langver"," .vhigh=5\n .vlow=4\n .bytes=61767\n",61767U,{0xda66U,0x554fU,0x2729U}},
    {"flashdb.ms2","lokdb","ldbver"," .version=16\n .monat=3\n .jahr=2021\n .anzahl=1166\n",74688U,{0x217fU,0xbc98U,0xc1ddU}},
    {"mfxdefs.ms2","mfxdefs","mfxver"," .vlow=3\n .bytes=1902\n",1902U,{0x0cb0U,0x9f75U,0x9f75U}},
    {"mfxdefs.bin","mfxbin","mfxbver"," .vlow=3\n .bytes=772\n",772U,{0x126dU,0x126dU,0x126dU}},
    {"050-ms2.bin","ms2","ms2ver"," .vhigh=5\n .vlow=6\n .bytes=93500\n",93500U,{0x42beU,0xfe03U,0x8d1bU}},
    {"051-ms2.bin","ms2x","ms2xver"," .vhigh=5\n .vlow=6\n .bytes=159219\n",159219U,{0x5cddU,0xe12aU,0xda0fU}},
    {"052-ms2.bin","ms2y","ms2yver"," .vhigh=5\n .vlow=6\n .bytes=126092\n",126092U,{0x0e7fU,0xc23bU,0x4dd2U}},
    {"016-gb2.bin","gb2","gb2ver"," .vhigh=1\n .vlow=70\n .bytes=27063\n",27063U,{0xe4bbU,0xd98eU,0xd0dfU}}
};

static ms2x_frame query(const char *name)
{
    ms2x_frame f={0};
    size_t n=strlen(name);
    CHECK(n<=8U);
    f.id=UINT32_C(0x00409b22); f.extended=1U; f.dlc=8U;
    memcpy(f.data,name,n);
    return f;
}

static int request(ms2x_resources *s, const char *name, uint64_t now)
{
    ms2x_frame f=query(name);
    return ms2x_resources_receive(s,&f,now);
}

static void path(char *out, size_t capacity, const char *directory, const char *file)
{
    int n=snprintf(out,capacity,"%s/%s",directory,file);
    CHECK(n>0 && (size_t)n<capacity);
}

static uint8_t *load(const char *filename, size_t size)
{
    char name[1024];
    uint8_t *data=malloc(size);
    FILE *f;
    CHECK(data!=NULL);
    path(name,sizeof(name),root,filename);
    f=fopen(name,"rb"); CHECK(f!=NULL);
    CHECK(fread(data,1U,size,f)==size); CHECK(fgetc(f)==EOF);
    CHECK(fclose(f)==0);
    return data;
}

static unsigned collect(ms2x_resources *s, uint64_t start, const ms2x_frame *echo,
                        const uint8_t *body, size_t size, int golden_crc)
{
    ms2x_resource_tx tx, repeated;
    uint64_t now=start;
    unsigned count=0U;
    size_t padded=(size+7U)&~(size_t)7U, offset=0U;
    uint8_t copy[1024]={0};
    CHECK(size<=sizeof(copy)); memcpy(copy,body,size);
    for (;;) {
        uint8_t memory[16], wire[13];
        ms2x_frame decoded;
        int result=ms2x_resources_peek(s,now,&tx);
        CHECK(result>=0);
        if (result==0) {
            uint64_t next=ms2x_resources_next_time(s);
            if (next==UINT64_MAX) break;
            CHECK(next>now); now=next; continue;
        }
        CHECK(count<130U);
        CHECK(now==start+(count==0U ? 0U : 50U+5U*((uint64_t)(count-1U)/5U)));
        CHECK(ms2x_resources_peek(s,now,&repeated)==1);
        CHECK(tx.sequence==repeated.sequence && tx.owner==repeated.owner);
        CHECK(ms2x_frame_equal(&tx.frame,&repeated.frame));
        CHECK(ms2x_can_pack(&tx.frame,memory,sizeof(memory))==0);
        CHECK(ms2x_frame_encode(&tx.frame,wire,sizeof(wire))==0);
        CHECK(ms2x_frame_decode(wire,sizeof(wire),&decoded)==0);
        CHECK(ms2x_frame_equal(&decoded,&tx.frame));
        if (count==0U) {
            CHECK(tx.frame.id==UINT32_C(0x00414711)); CHECK(tx.frame.dlc==8U);
            CHECK(memcmp(tx.frame.data,echo->data,8U)==0);
        } else {
            CHECK(tx.frame.id==UINT32_C(0x00429b22));
            if (count==1U) {
                uint32_t length=((uint32_t)tx.frame.data[0]<<24U)|((uint32_t)tx.frame.data[1]<<16U)|
                                ((uint32_t)tx.frame.data[2]<<8U)|tx.frame.data[3];
                uint16_t crc=(uint16_t)(((uint16_t)tx.frame.data[4]<<8U)|tx.frame.data[5]);
                CHECK(tx.frame.dlc==6U); CHECK(length==size);
                CHECK(crc==ms2x_crc16(copy,padded));
                if (golden_crc>=0) CHECK(crc==(uint16_t)golden_crc);
            } else {
                CHECK(tx.frame.dlc==8U); CHECK(offset+8U<=padded);
                CHECK(memcmp(tx.frame.data,copy+offset,8U)==0); offset+=8U;
            }
        }
        CHECK(ms2x_resources_tx_processed(s,&tx,now)==0); ++count;
    }
    CHECK(count==2U+padded/8U); CHECK(offset==padded);
    CHECK(ms2x_resources_error(s)==MS2X_OK);
    return count;
}

static void catalog_round(void)
{
    size_t i;
    for (i=0U;i<sizeof(files)/sizeof(files[0]);++i) {
        ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
        uint8_t *original=load(files[i].file,files[i].size);
        size_t parts=(files[i].size+1023U)/1024U, part;
        ms2x_frame f=query(files[i].version);
        ms2x_resource_tx tx;
        uint64_t now=0U;
        CHECK(s!=NULL); CHECK(ms2x_resources_receive(s,&f,now)==1);
        (void)collect(s,now,&f,(const uint8_t *)files[i].text,strlen(files[i].text),-1);
        now+=1000U;
        CHECK(request(s,"0",now)==0); /* A version reply does not select a data file. */
        CHECK(request(s,files[i].name,now)==1);
        CHECK(ms2x_resources_peek(s,now,&tx)==0); /* No push on selection. */
        for (part=0U;part<parts;++part) {
            uint8_t expected[1024]={0};
            char number[9];
            size_t amount=files[i].size-part*1024U;
            int crc=-1;
            if (amount>1024U) amount=1024U;
            memcpy(expected,original+part*1024U,amount);
            CHECK(snprintf(number,sizeof(number),"%lu",(unsigned long)part)>0);
            f=query(number);
            if (part==0U) crc=(int)files[i].crc[0];
            if (part==parts/2U) crc=(int)files[i].crc[1];
            if (part==parts-1U) crc=(int)files[i].crc[2];
            CHECK(ms2x_resources_receive(s,&f,now)==1);
            CHECK(collect(s,now,&f,expected,sizeof(expected),crc)==130U);
            now+=1000U;
        }
        /* Official client may reselect the name for every part. */
        CHECK(request(s,files[i].name,now)==1); f=query("000");
        CHECK(ms2x_resources_receive(s,&f,now)==1);
        {
            uint8_t first[1024]={0};
            size_t amount=files[i].size<1024U ? files[i].size : 1024U;
            memcpy(first,original,amount); (void)collect(s,now,&f,first,sizeof(first),(int)files[i].crc[0]);
        }
        ms2x_resources_free(s); free(original); ++cases;
    }
}

static void invalid_queries(void)
{
    static const char *const bad[]={"", "unknown", "../lang", "-1", "+1", "0x1", "1a", "99999999", "61"};
    ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
    ms2x_resource_tx tx;
    ms2x_frame f;
    size_t i;
    CHECK(s!=NULL);
    for (i=0U;i<sizeof(bad)/sizeof(bad[0]);++i) {
        CHECK(request(s,"lang",0U)==1); CHECK(request(s,bad[i],0U)==0);
        CHECK(ms2x_resources_peek(s,0U,&tx)==0);
        CHECK(request(s,"0",0U)==0); CHECK(ms2x_resources_error(s)==MS2X_OK);
    }
    for (i=0U;i<8U;++i) {
        f=query("langver"); f.dlc=(uint8_t)i;
        CHECK(ms2x_resources_receive(s,&f,0U)==0);
    }
    f=query("langver"); f.extended=0U; CHECK(ms2x_resources_receive(s,&f,0U)==0);
    f=query("langver"); ++f.id; CHECK(ms2x_resources_receive(s,&f,0U)==0);
    f=query("lang"); f.data[7]=1U; CHECK(ms2x_resources_receive(s,&f,0U)==0);
    f=query("langver"); f.id=UINT32_C(0x00419b22); CHECK(ms2x_resources_receive(s,&f,0U)==0);
    CHECK(ms2x_resources_receive(s,NULL,0U)==0);
    CHECK(request(s,"ffffffff",0U)==0); CHECK(ms2x_resources_next_time(s)==UINT64_MAX);
    ms2x_resources_free(s); ++cases;
}

static void interrupts_and_time(void)
{
    static const unsigned cuts[]={0U,1U,2U,64U,129U};
    size_t cut;
    for (cut=0U;cut<sizeof(cuts)/sizeof(cuts[0]);++cut) {
        ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
        ms2x_resource_tx tx;
        unsigned sent;
        uint64_t now=0U;
        CHECK(s!=NULL); CHECK(request(s,"lang",now)==1); CHECK(request(s,"0",now)==1);
        for (sent=0U;sent<cuts[cut];++sent) {
            now=sent==0U ? 0U : 50U+5U*((uint64_t)(sent-1U)/5U);
            CHECK(ms2x_resources_peek(s,now,&tx)==1);
            CHECK(ms2x_resources_tx_processed(s,&tx,now)==0);
        }
        ms2x_resources_abort(s);
        CHECK(ms2x_resources_error(s)==MS2X_IO); CHECK(ms2x_resources_peek(s,now,&tx)==-1);
        CHECK(ms2x_resources_next_time(s)==UINT64_MAX);
        CHECK(request(s,"langver",now)==-1);
        ms2x_resources_free(s); ++cases;
    }
    {
        ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
        ms2x_frame f=query("ms2xver"), other=query("langver");
        CHECK(s!=NULL); CHECK(ms2x_resources_receive(s,&f,0U)==1);
        CHECK(ms2x_resources_receive(s,&other,0U)==0);
        other.id=UINT32_C(0x00401234); CHECK(ms2x_resources_receive(s,&other,0U)==0);
        (void)collect(s,0U,&f,(const uint8_t *)files[5].text,strlen(files[5].text),-1);
        ms2x_resources_free(s); ++cases;
    }
    {
        ms2x_resources *s=ms2x_resources_new(root,0xffffU,0U);
        ms2x_frame f=query("langver");
        ms2x_resource_tx tx;
        unsigned i;
        f.id=UINT32_C(0x0040ffff); CHECK(s!=NULL);
        CHECK(ms2x_resources_receive(s,&f,0U)==1);
        CHECK(ms2x_resources_peek(s,0U,&tx)==1); CHECK(tx.frame.id==UINT32_C(0x00410000));
        CHECK(ms2x_resources_tx_processed(s,&tx,100U)==0);
        CHECK(ms2x_resources_peek(s,149U,&tx)==0); CHECK(ms2x_resources_next_time(s)==150U);
        /* A late batch starts from actual acceptance; no accumulated bursts. */
        for (i=0U;i<5U;++i) {
            CHECK(ms2x_resources_peek(s,2000U,&tx)==1); CHECK(tx.frame.id==UINT32_C(0x0042ffff));
            CHECK(ms2x_resources_tx_processed(s,&tx,2000U)==0);
        }
        CHECK(ms2x_resources_peek(s,2000U,&tx)==0); CHECK(ms2x_resources_next_time(s)==2005U);
        CHECK(ms2x_resources_peek(s,2005U,&tx)==1);
        ms2x_resources_abort(s); ms2x_resources_free(s); ++cases;
    }
    {
        ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
        ms2x_resource_tx first, again;
        CHECK(s!=NULL); CHECK(request(s,"ms2xver",0U)==1);
        CHECK(ms2x_resources_peek(s,0U,&first)==1);
        CHECK(request(s,"langver",1000U)==0); /* Busy must not overwrite/reset deadline. */
        CHECK(request(s,"ffffffff",4999U)==0);
        CHECK(ms2x_resources_peek(s,4999U,&again)==1); /* Simulated repeated EAGAIN, no receipt. */
        CHECK(first.sequence==again.sequence && ms2x_frame_equal(&first.frame,&again.frame));
        CHECK(ms2x_resources_next_time(s)==5000U);
        CHECK(ms2x_resources_tx_processed(s,&first,5000U)==-1);
        CHECK(ms2x_resources_error(s)==MS2X_TIMEOUT);
        ms2x_resources_free(s); ++cases;
    }
    {
        ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
        ms2x_resource_tx tx;
        CHECK(s!=NULL); CHECK(request(s,"langver",0U)==1);
        CHECK(ms2x_resources_peek(s,0U,&tx)==1); CHECK(ms2x_resources_tx_processed(s,&tx,0U)==0);
        CHECK(ms2x_resources_peek(s,49U,&tx)==0); CHECK(ms2x_resources_next_time(s)==50U);
        CHECK(ms2x_resources_peek(s,50U,&tx)==1);
        CHECK(ms2x_resources_advance(s,49U)==-1); CHECK(ms2x_resources_error(s)==MS2X_CLOCK);
        ms2x_resources_free(s); ++cases;
    }
    {
        ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
        CHECK(s!=NULL); CHECK(request(s,"langver",UINT64_MAX-10U)==-1);
        CHECK(ms2x_resources_error(s)==MS2X_CLOCK); ms2x_resources_free(s); ++cases;
    }
    {
        ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
        ms2x_resource_tx old;
        CHECK(s!=NULL); CHECK(request(s,"langver",0U)==1);
        CHECK(ms2x_resources_peek(s,0U,&old)==1); CHECK(ms2x_resources_tx_processed(s,&old,0U)==0);
        CHECK(ms2x_resources_tx_processed(s,&old,50U)==-1);
        CHECK(ms2x_resources_error(s)==MS2X_PROTOCOL); ms2x_resources_free(s); ++cases;
    }
}

static void fixtures(void)
{
    char directory[]="/tmp/ms2-resource-test-XXXXXX", name[1024];
    FILE *f;
    ms2x_resources *s;
    ms2x_frame q;
    size_t i;
    CHECK(mkdtemp(directory)!=NULL); path(name,sizeof(name),directory,"051-ms2.bin");
    f=fopen(name,"wb"); CHECK(f!=NULL);
    for (i=0U;i<157127U;++i) CHECK(fputc(i==0U ? 4 : i==1U ? 28 : 0,f)!=EOF);
    CHECK(fclose(f)==0);
    s=ms2x_resources_new(directory,0x9b22U,0x4711U); CHECK(s!=NULL);
    q=query("ms2xver"); CHECK(ms2x_resources_receive(s,&q,0U)==1);
    CHECK(collect(s,0U,&q,(const uint8_t *)" .vhigh=4\n .vlow=28\n .bytes=157127\n",35U,0xe487)==7U);
    ms2x_resources_free(s); ++cases;
    s=ms2x_resources_new(directory,0x9b22U,0x4711U); CHECK(s!=NULL);
    CHECK(request(s,"ms2x",0U)==1);
    f=fopen(name,"wb"); CHECK(f!=NULL); CHECK(fclose(f)==0);
    CHECK(request(s,"0",0U)==-1); CHECK(ms2x_resources_error(s)==MS2X_IO);
    ms2x_resources_free(s); CHECK(unlink(name)==0); ++cases;
    s=ms2x_resources_new(directory,0x9b22U,0x4711U); CHECK(s!=NULL);
    CHECK(request(s,"langver",0U)==-1); CHECK(ms2x_resources_error(s)==MS2X_IO);
    ms2x_resources_free(s); CHECK(rmdir(directory)==0); ++cases;
}

static void firmware_independence(void)
{
    char name[1024];
    ms2x_error error;
    ms2x_firmware *image;
    ms2x_engine *engine;
    ms2x_tx pending;
    ms2x_resources *s=ms2x_resources_new(root,0x9b22U,0x4711U);
    ms2x_resource_tx resource;
    size_t i;
    path(name,sizeof(name),root,"051-ms2.bin"); image=ms2x_firmware_load(name,&error);
    CHECK(image!=NULL && error==MS2X_OK); engine=ms2x_engine_new(image,5000U);
    CHECK(engine!=NULL && s!=NULL); CHECK(ms2x_start(engine,0U)==0);
    CHECK(ms2x_peek(engine,&pending)==1); CHECK(ms2x_tx_processed(engine,&pending,0U)==0);
    for (i=0U;i<sizeof(files)/sizeof(files[0]);++i) {
        uint8_t record[16];
        ms2x_frame f=query(files[i].version);
        uint64_t now=100U*(uint64_t)i;
        CHECK(ms2x_can_pack(&f,record,sizeof(record))==0);
        CHECK(ms2x_can_receive_memory(engine,record,sizeof(record),0,now)==0);
        CHECK(ms2x_get_state(engine)==MS2X_WAIT_PING);
        CHECK(ms2x_next_time(engine)==5000U); CHECK(ms2x_completed(engine)==0U);
        CHECK(ms2x_resources_receive(s,&f,now)==1);
        (void)collect(s,now,&f,(const uint8_t *)files[i].text,strlen(files[i].text),-1);
    }
    CHECK(ms2x_resources_peek(s,1000U,&resource)==0);
    CHECK(ms2x_advance(engine,5000U)==-1); CHECK(ms2x_get_error(engine)==MS2X_TIMEOUT);
    ms2x_engine_free(engine); ms2x_resources_free(s); ++cases;
    /* A busy resource response must not consume a firmware NACK. */
    engine=ms2x_engine_new(image,5000U); s=ms2x_resources_new(root,0x9b22U,0x4711U);
    CHECK(engine!=NULL && s!=NULL); CHECK(ms2x_start(engine,0U)==0);
    CHECK(ms2x_peek(engine,&pending)==1); CHECK(ms2x_tx_processed(engine,&pending,0U)==0);
    {
        ms2x_frame ping={UINT32_C(0x00319b22),8U,{0x4dU,0x55U,0x2cU,0x58U,3U,112U,0U,0x33U},1U};
        ms2x_frame nack={UINT32_C(0x00379b22),5U,{0x4dU,0x55U,0x2cU,0x58U,0xf2U},1U};
        uint8_t record[16];
        CHECK(ms2x_receive(engine,&ping,0U)==0);
        CHECK(ms2x_peek(engine,&pending)==1); CHECK(ms2x_tx_processed(engine,&pending,0U)==0);
        CHECK(ms2x_advance(engine,500U)==0); CHECK(ms2x_peek(engine,&pending)==1);
        CHECK(ms2x_tx_processed(engine,&pending,500U)==0);
        CHECK(ms2x_advance(engine,1000U)==0); CHECK(ms2x_peek(engine,&pending)==1);
        CHECK(ms2x_tx_processed(engine,&pending,1000U)==0);
        CHECK(ms2x_get_state(engine)==MS2X_WAIT_SELECT);
        CHECK(request(s,"langver",1000U)==1);
        CHECK(ms2x_resources_receive(s,&nack,1001U)==0);
        CHECK(ms2x_can_pack(&nack,record,sizeof(record))==0);
        CHECK(ms2x_can_receive_memory(engine,record,sizeof(record),0,1001U)==-1);
        CHECK(ms2x_get_error(engine)==MS2X_PROTOCOL);
        CHECK(ms2x_resources_peek(s,1001U,&resource)==1);
    }
    ms2x_engine_free(engine); ms2x_firmware_free(image); ms2x_resources_free(s); ++cases;
}

int main(int argc, char **argv)
{
    CHECK(argc==2); root=argv[1];
    CHECK(ms2x_resources_new(NULL,0U,0U)==NULL);
    CHECK(ms2x_resources_new("",0U,0U)==NULL);
    CHECK(ms2x_resources_error(NULL)==MS2X_INVALID);
    CHECK(ms2x_resources_next_time(NULL)==UINT64_MAX);
    ms2x_resources_abort(NULL); ms2x_resources_free(NULL);
    catalog_round(); invalid_queries(); interrupts_and_time(); fixtures(); firmware_independence();
    printf("MS2 resources: %u cases, %u checks passed; eight files and versions, all536 parts; no CAN/device I/O\n",cases,assertions);
    return 0;
}