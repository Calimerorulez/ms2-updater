/* File replay only. Input: logical_ms TX|RX 26_hex_digits; no stdin/devices. */
#include "ms2x.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_digit(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int checked_write(void *context, const uint8_t wire[13], size_t length)
{
    return length==13U && memcmp(context,wire,13U)==0 ? 0 : -1;
}
static int parse(char *line, uint64_t *now, char direction[3], uint8_t wire[13])
{
    char hex[27], extra, *end;
    unsigned long long value;
    size_t i;
    if (line[0]<'0' || line[0]>'9') return -1;
    errno=0; value=strtoull(line,&end,10);
    if (errno!=0 || value>UINT64_MAX || *end!=' ') return -1;
    *now=(uint64_t)value;
    if (sscanf(end+1,"%2s %26s %c",direction,hex,&extra)!=2 || strlen(hex)!=26U ||
        (strcmp(direction,"TX")!=0 && strcmp(direction,"RX")!=0)) return -1;
    for (i=0; i<13U; ++i) {
        int hi=hex_digit(hex[2U*i]), lo=hex_digit(hex[2U*i+1U]);
        if (hi<0 || lo<0) return -1;
        wire[i]=(uint8_t)(16*hi+lo);
    }
    return 0;
}
static int replay(ms2x_firmware *fw, const char *path)
{
    FILE *file=fopen(path,"rb");
    ms2x_engine *e;
    char line[160], direction[3];
    uint8_t wire[13];
    uint64_t now=0;
    size_t lines=0, txs=0, rxs=0;
    int failed=0;
    if (file==NULL) { (void)fprintf(stderr,"cannot open replay file\n"); return 1; }
    e=ms2x_engine_new(fw,MS2X_TIMEOUT_MS);
    if (e==NULL) { (void)fclose(file); return 1; }
    if (ms2x_start(e,0U)!=0) failed=1;
    while (!failed && fgets(line,(int)sizeof(line),file)!=NULL) {
        ms2x_frame frame;
        ++lines;
        if (lines>100000U || strchr(line,'\n')==NULL || parse(line,&now,direction,wire)!=0) {
            (void)ms2x_transport_error(e,now); failed=1; break;
        }
        if (strcmp(direction,"TX")==0) {
            ms2x_tx pending;
            ms2x_offline_transport io={wire,checked_write,NULL};
            if (ms2x_advance(e,now)!=0 || ms2x_peek(e,&pending)!=1 ||
                ms2x_frame_decode(wire,sizeof(wire),&frame)!=0 || !ms2x_frame_equal(&pending.frame,&frame) ||
                ms2x_offline_send(e,&io,now)!=0) { failed=1; break; }
            ++txs;
        } else {
            if (ms2x_receive_wire(e,wire,sizeof(wire),now)!=0) { failed=1; break; }
            ++rxs;
        }
    }
    if (ferror(file)!=0) { (void)ms2x_transport_error(e,now); failed=1; }
    if (fclose(file)!=0) { (void)ms2x_transport_error(e,now); failed=1; }
    if (failed) (void)ms2x_transport_error(e,now);
    if (ms2x_close(e,now)!=0) failed=1;
    if (printf("{\"offline_only\":true,\"hardware_success\":false,\"hardware_test\":\"NO-GO\","
               "\"state\":\"%s\",\"blocks\":%u,\"boot_reply_seen\":%s,\"tx\":%zu,\"rx\":%zu}\n",
               ms2x_state_name(ms2x_get_state(e)),ms2x_completed(e),ms2x_boot_seen(e)?"true":"false",txs,rxs)<0) failed=1;
    if (failed) (void)fprintf(stderr,"replay rejected at line %zu: %s\n",lines,ms2x_reason(e));
    ms2x_engine_free(e); return failed ? 1 : 0;
}
int main(int argc, char **argv)
{
    ms2x_error error=MS2X_OK;
    ms2x_firmware *fw;
    int result;
    if (!((argc==3 && strcmp(argv[1],"inspect")==0) || (argc==4 && strcmp(argv[1],"replay")==0))) {
        (void)fprintf(stderr,"offline only: %s inspect FIRMWARE | replay FIRMWARE EVENTS\n",argv[0]); return 2;
    }
    fw=ms2x_firmware_load(argv[2],&error);
    if (fw==NULL) { (void)fprintf(stderr,"firmware rejected (%d)\n",(int)error); return 1; }
    if (argc==3) {
        result=printf("{\"profile\":\"ms2x\",\"file\":\"051-ms2.bin\",\"uid\":\"1A2B3C4D\",\"hardware\":\"0033\","
                      "\"version\":\"%u.%u\",\"sha256\":\"%s\",\"blocks\":156,\"offline_only\":true}\n",
                      ms2x_firmware_major(fw),ms2x_firmware_minor(fw),MS2X_SHA256_HEX)<0 ? 1 : 0;
    } else result=replay(fw,argv[3]);
    ms2x_firmware_free(fw);
    /* Propagate delayed buffered stdout errors as well. */
    if (fflush(stdout)!=0) result=1;
    return result;
}