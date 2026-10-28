/* Fixed MS2 catalog and uncompressed command0x20/0x21 replies only.
 * CS3 event: 2fc24 file slices,1d6a0/32b44 versions,76c64 text output,
 * 30b14/30e78 pacing,3e644/3e694/3e6c0 wire encoding. See report20.
 */
#include "ms2x_resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IDLE, ECHO, START, DATA };
enum { BINARY, DATABASE, MFX_TEXT };
static const struct {
    const char *name, *version, *file;
    unsigned offset, kind;
} catalog[] = {
    {"lang", "langver", "lang.ms2", 0U, BINARY},
    {"lokdb", "ldbver", "flashdb.ms2", 0U, DATABASE},
    {"mfxdefs", "mfxver", "mfxdefs.ms2", 0U, MFX_TEXT},
    {"mfxbin", "mfxbver", "mfxdefs.bin", 0U, BINARY},
    {"ms2", "ms2ver", "050-ms2.bin", 252U, BINARY},
    {"ms2x", "ms2xver", "051-ms2.bin", 0U, BINARY},
    {"ms2y", "ms2yver", "052-ms2.bin", 0U, BINARY},
    {"gb2", "gb2ver", "016-gb2.bin", 6U, BINARY}
};

struct ms2x_resources {
    char directory[512];
    uint8_t buffer[1024], echo[8];
    FILE *file;
    uint32_t size;
    size_t length, padded, position;
    uint64_t now, deadline, ready, sequence;
    uint16_t peer, hash, crc;
    unsigned phase, batch;
    ms2x_error error;
};

static void unselect(ms2x_resources *s)
{
    if (s->file!=NULL) (void)fclose(s->file);
    s->file=NULL;
}

static int fail(ms2x_resources *s, ms2x_error error)
{
    if (s!=NULL) {
        if (s->error==MS2X_OK) s->error=error;
        s->phase=IDLE;
        unselect(s);
    }
    return -1;
}

int ms2x_resources_advance(ms2x_resources *s, uint64_t now)
{
    if (s==NULL || s->error!=MS2X_OK) return -1;
    if (now<s->now) return fail(s,MS2X_CLOCK);
    s->now=now;
    if (s->phase!=IDLE && now>=s->deadline) return fail(s,MS2X_TIMEOUT);
    return 0;
}

ms2x_resources *ms2x_resources_new(const char *directory, uint16_t peer, uint16_t hash)
{
    ms2x_resources *s;
    if (directory==NULL || directory[0]=='\0' || strlen(directory)>=512U) return NULL;
    s=calloc(1U,sizeof(*s));
    if (s!=NULL) {
        memcpy(s->directory,directory,strlen(directory)+1U);
        s->peer=peer; s->hash=hash;
    }
    return s;
}

void ms2x_resources_free(ms2x_resources *s)
{
    if (s!=NULL) { unselect(s); free(s); }
}

void ms2x_resources_abort(ms2x_resources *s) { (void)fail(s,MS2X_IO); }
ms2x_error ms2x_resources_error(const ms2x_resources *s) { return s==NULL ? MS2X_INVALID : s->error; }
uint64_t ms2x_resources_next_time(const ms2x_resources *s)
{
    if (s==NULL || s->error!=MS2X_OK || s->phase==IDLE) return UINT64_MAX;
    return s->ready>s->now && s->ready<s->deadline ? s->ready : s->deadline;
}

static int open_file(ms2x_resources *s, unsigned index)
{
    char path[544];
    long size;
    int n=snprintf(path,sizeof(path),"%s/%s",s->directory,catalog[index].file);
    unselect(s);
    if (n<0 || (size_t)n>=sizeof(path)) return fail(s,MS2X_INVALID);
    s->file=fopen(path,"rb");
    if (s->file==NULL || fseek(s->file,0L,SEEK_END)!=0 ||
        (size=ftell(s->file))<=0L ||
        fseek(s->file,0L,SEEK_SET)!=0) return fail(s,MS2X_IO);
    s->size=(uint32_t)size;
    if ((long)s->size!=size) return fail(s,MS2X_INVALID);
    return 0;
}

static int number(const char *text, size_t length, uint32_t *value)
{
    size_t i;
    uint32_t n=0U;
    if (length==0U) return -1;
    for (i=0U;i<length;++i) {
        unsigned digit=(unsigned)(unsigned char)text[i]-(unsigned)'0';
        if (digit>9U || n>(UINT32_MAX-digit)/10U) return -1;
        n=n*10U+digit;
    }
    *value=n;
    return 0;
}

static int field(ms2x_resources *s, const char *name, uint32_t value)
{
    int n;
    if (value==0U) return 0; /* CS3's writer omits the default zero. */
    n=snprintf((char *)s->buffer+s->length,sizeof(s->buffer)-s->length,
               " .%s=%lu\n",name,(unsigned long)value);
    if (n<0 || (size_t)n>=sizeof(s->buffer)-s->length) return fail(s,MS2X_INVALID);
    s->length+=(size_t)n;
    return 0;
}

static int version(ms2x_resources *s, unsigned index)
{
    uint32_t values[4]={0U,0U,0U,0U};
    size_t n=s->size<1023U ? (size_t)s->size : 1023U;
    unsigned i;
    memset(s->buffer,0,sizeof(s->buffer));
    if (fread(s->buffer,1U,n,s->file)!=n) return fail(s,MS2X_IO);
    if (catalog[index].kind==DATABASE) {
        if (n<8U) return fail(s,MS2X_INVALID);
        for (i=0U;i<4U;++i) values[i]=(uint32_t)s->buffer[2U*i]|((uint32_t)s->buffer[2U*i+1U]<<8U);
    } else if (catalog[index].kind==MFX_TEXT) {
        char *p=(char *)s->buffer;
        const char prefix[]="[mfxms2]\nversion\n";
        if (strncmp(p,prefix,sizeof(prefix)-1U)!=0) return fail(s,MS2X_INVALID);
        p+=sizeof(prefix)-1U;
        while (*p==' ') {
            char *end=strchr(p,'\n');
            unsigned slot;
            if (end==NULL || (strncmp(p," .major=",8U)!=0 && strncmp(p," .minor=",8U)!=0))
                return fail(s,MS2X_INVALID);
            slot=p[3]=='a' ? 0U : 1U;
            if (number(p+8,(size_t)(end-p)-8U,&values[slot])!=0) return fail(s,MS2X_INVALID);
            p=end+1;
        }
    } else {
        unsigned offset=catalog[index].offset;
        if (n<(size_t)offset+2U) return fail(s,MS2X_INVALID);
        values[0]=s->buffer[offset]; values[1]=s->buffer[offset+1U];
    }
    s->length=0U;
    if (catalog[index].kind==DATABASE) {
        static const char *const keys[]={"version","monat","jahr","anzahl"};
        for (i=0U;i<4U;++i) if (field(s,keys[i],values[i])!=0) return -1;
    } else if (field(s,"vhigh",values[0])!=0 || field(s,"vlow",values[1])!=0 ||
               field(s,"bytes",s->size)!=0) return -1;
    unselect(s);
    return 0;
}

static int queue(ms2x_resources *s, const ms2x_frame *query)
{
    if (s->now>UINT64_MAX-MS2X_TIMEOUT_MS || s->sequence==UINT64_MAX)
        return fail(s,MS2X_CLOCK);
    s->padded=(s->length+7U)&~(size_t)7U;
    memset(s->buffer+s->length,0,s->padded-s->length);
    s->crc=ms2x_crc16(s->buffer,s->padded);
    memcpy(s->echo,query->data,8U);
    s->position=0U; s->batch=0U; s->phase=ECHO; ++s->sequence;
    s->ready=s->now; s->deadline=s->now+MS2X_TIMEOUT_MS;
    return 1;
}

int ms2x_resources_receive(ms2x_resources *s, const ms2x_frame *query, uint64_t now)
{
    char name[9];
    size_t length=0U, i;
    uint32_t part;
    if (ms2x_resources_advance(s,now)!=0) return -1;
    if (!ms2x_frame_valid(query) || query->id!=(UINT32_C(0x00400000)|s->peer) ||
        query->dlc!=8U || s->phase!=IDLE) return 0;
    while (length<8U && query->data[length]!=0U) ++length;
    for (i=length;i<8U;++i) if (query->data[i]!=0U) { unselect(s); return 0; }
    memcpy(name,query->data,length); name[length]='\0';
    for (i=0U;i<sizeof(catalog)/sizeof(catalog[0]);++i) {
        int is_version=strcmp(name,catalog[i].version)==0;
        if (!is_version && strcmp(name,catalog[i].name)!=0) continue;
        if (open_file(s,(unsigned)i)!=0) return -1;
        if (!is_version) return 1; /* Name selects, following decimal query reads. */
        if (version(s,(unsigned)i)!=0) return -1;
        return queue(s,query);
    }
    /* The observed ASCII ffffffff is not an extra completion/ACK layer. */
    if (strcmp(name,"ffffffff")==0) return 0;
    if (number(name,length,&part)!=0 || s->file==NULL || part>(s->size-1U)/1024U) {
        unselect(s); return 0;
    }
    {
        uint32_t offset=part*1024U;
        size_t amount=s->size-offset<1024U ? (size_t)(s->size-offset) : 1024U;
        /* offset<size, whose positive ftell() value already fits in long. */
        if (fseek(s->file,(long)offset,SEEK_SET)!=0)
            return fail(s,MS2X_IO);
        memset(s->buffer,0,sizeof(s->buffer));
        if (fread(s->buffer,1U,amount,s->file)!=amount) return fail(s,MS2X_IO);
    }
    s->length=sizeof(s->buffer);
    return queue(s,query);
}

int ms2x_resources_peek(ms2x_resources *s, uint64_t now, ms2x_resource_tx *item)
{
    ms2x_frame *f;
    if (ms2x_resources_advance(s,now)!=0) return -1;
    if (item==NULL) return fail(s,MS2X_INVALID);
    if (s->phase==IDLE || now<s->ready) return 0;
    memset(item,0,sizeof(*item)); item->owner=s; item->sequence=s->sequence;
    f=&item->frame; f->extended=1U;
    f->id=(s->phase==ECHO ? UINT32_C(0x00410000)|s->hash : UINT32_C(0x00420000)|s->peer);
    f->dlc=8U;
    if (s->phase==ECHO) memcpy(f->data,s->echo,8U);
    else if (s->phase==START) {
        unsigned i;
        f->dlc=6U;
        for (i=0U;i<4U;++i) f->data[i]=(uint8_t)(s->length>>(24U-8U*i));
        f->data[4]=(uint8_t)(s->crc>>8U); f->data[5]=(uint8_t)s->crc;
    } else memcpy(f->data,s->buffer+s->position,8U);
    return 1;
}

int ms2x_resources_tx_processed(ms2x_resources *s, const ms2x_resource_tx *item, uint64_t now)
{
    ms2x_resource_tx expected;
    uint64_t delay=0U;
    if (ms2x_resources_peek(s,now,&expected)!=1) return fail(s,MS2X_PROTOCOL);
    if (item==NULL || item->owner!=s || item->sequence!=expected.sequence ||
        !ms2x_frame_equal(&item->frame,&expected.frame)) return fail(s,MS2X_PROTOCOL);
    if (s->sequence==UINT64_MAX) return fail(s,MS2X_PROTOCOL);
    ++s->sequence;
    if (s->phase==ECHO) { s->phase=START; delay=50U; }
    else {
        if (s->phase==DATA) s->position+=8U;
        s->phase=s->position==s->padded ? IDLE : DATA;
        if (++s->batch==5U) { s->batch=0U; delay=5U; }
    }
    s->ready=now+(delay<s->deadline-now ? delay : s->deadline-now);
    return 0;
}