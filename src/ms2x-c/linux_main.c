#define _POSIX_C_SOURCE 200809L
#include "ms2x_linux.h"
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void interrupt(int signal_number) { (void)signal_number; ms2x_linux_stop(); }
static const int live_resources_enabled=1; /* Resource-only release; no runtime override. */
static const uint16_t resource_server_hash=0x5a21U; /* Local collision review: report 27; runtime checks remain active. */
/* U,88 has no block/nonce. No established ACK freshness/exclusivity proof. */
static const int live_update_enabled=0; /* No runtime override. RAM tests remain available. */
static int usage(void)
{
    (void)puts("ms2-updater probe [--interface can0] [--seconds 10] [--query] [--bootloader] [--target UID]\n"
               "  Default: passive, zero CAN writes. --query: one normal identification ping.\n"
               "  --bootloader: passive boot selection; with --query: one empty 0036 query (hash 5A22).\n"
               "ms2-updater serve-resources --resources DIRECTORY [--interface can0] [--seconds 180] [--target UID]\n"
               "  Resource-only mode; one normal identification ping, then checked advertisement (hash 5A21).\n"
               "  Starting authorizes MS2-requested downloads/storage; no MS2 confirmation is assumed.\n"
               "ms2-updater update [--target UID] [--hardware TYPE] [--bootloader]\n"
               "  [--interface can0] [--firmware PATH] [--resources DIR]\n"
               "  One unambiguous MS2 may be selected automatically; otherwise choose its UID.\n"
               "  Live update disabled: CRC-ACK freshness and bus exclusivity are unproven.");
    return 0;
}
static int hex_value(const char *text, size_t width, uint32_t *value)
{
    if (strlen(text)!=width || strspn(text,"0123456789abcdefABCDEF")!=width) return -1;
    *value=(uint32_t)strtoul(text,NULL,16); return 0;
}
static void show_devices(const ms2x_session_result *result)
{
    unsigned i;
    for (i=0U;i<result->device_count;++i) {
        const ms2x_device *d=&result->devices[i];
        unsigned kind;
        for (kind=0U;kind<2U;++kind) {
            const ms2x_identity *id=kind==0U ? &d->normal : &d->boot;
            const ms2x_profile *profile;
            if (!(kind==0U ? d->have_normal : d->have_boot)) continue;
            profile=ms2x_profile_for(id->hardware);
            (void)printf("%s: UID%08" PRIX32 " type%04X reported-version%u.%u hash%04X image=%s%s\n",
                kind==0U ? "normal" : "bootloader identity", id->uid,(unsigned)id->hardware,
                (unsigned)id->major,(unsigned)id->minor,(unsigned)id->hash,
                profile==NULL ? "unsupported" : profile->filename,d->ambiguous ? " AMBIGUOUS" : "");
        }
    }
    (void)printf("devices=%u invalid_identities=%u overflow=%d bus_conflict=%d; %s\n",result->device_count,
        result->invalid_identities,result->device_overflow,result->bus_conflict,result->reason);
}
int main(int argc, char **argv)
{
    const char *interface="can0", *image=NULL;
    const char *directory=".";
    ms2x_session_config config={0,0,0,0U,NULL,10000U,NULL,0,0U};
    ms2x_session_result result;
    ms2x_identity selected={0};
    ms2x_firmware *firmware=NULL;
    ms2x_engine *engine=NULL;
    uint32_t hardware=0U;
    int i, update, serve, resources_given=0, outcome, selection, exit_status;
    struct sigaction action;
    char filename[1024];
    if (argc==2 && strcmp(argv[1],"--help")==0) return usage();
    if (argc<2 || (strcmp(argv[1],"probe")!=0 && strcmp(argv[1],"update")!=0 && strcmp(argv[1],"serve-resources")!=0)) { (void)usage(); return 2; }
    update=strcmp(argv[1],"update")==0; serve=strcmp(argv[1],"serve-resources")==0;
    if (serve) config.observe_ms=180000U;
    for (i=2;i<argc;++i) {
        const char *arg=argv[i];
        if (strcmp(arg,"--query")==0) config.query=1;
        else if (strcmp(arg,"--bootloader")==0) config.bootloader=1;
        else {
            const char *value;
            if (++i>=argc) { (void)fprintf(stderr,"Missing value for %s\n",arg); return 2; }
            value=argv[i];
            if (strcmp(arg,"--interface")==0) interface=value;
            else if (strcmp(arg,"--firmware")==0) { if (!update) return 2; image=value; }
            else if (strcmp(arg,"--resources")==0) { if (!update && !serve) return 2; directory=value; resources_given=1; }
            else if (strcmp(arg,"--target")==0) {
                if (hex_value(value,8U,&config.target)!=0 || config.target==0U || config.target==UINT32_MAX) {
                    (void)fprintf(stderr,"Target must be a nonzero eight-digit UID, not FFFFFFFF\n"); return 2;
                }
            } else if (strcmp(arg,"--hardware")==0) {
                if (!update || hex_value(value,4U,&hardware)!=0 || ms2x_profile_for((uint16_t)hardware)==NULL) {
                    (void)fprintf(stderr,"Supported hardware: 0032, 0033, 0034\n"); return 2;
                }
            } else if (strcmp(arg,"--seconds")==0) {
                char *end; unsigned long seconds;
                if (update || value[0]<'0' || value[0]>'9') return 2;
                errno=0; seconds=strtoul(value,&end,10);
                if (errno!=0 || *end!='\0' || seconds==0UL || seconds>3600UL) return 2;
                config.observe_ms=(uint64_t)seconds*1000U;
            } else { (void)fprintf(stderr,"Unknown option: %s\n",arg); return 2; }
        }
    }
    if ((update && config.query) ||
        (serve && (config.bootloader || config.query || !resources_given))) return 2;
    if (serve) {
        config.serve=1; config.resources=directory; config.server_hash=resource_server_hash;
        if (!live_resources_enabled || resource_server_hash==0U) {
            (void)fprintf(stderr,"NO-GO: live resources disabled; advertisement/server identity and concurrent servers require review; no CAN interface opened\n");
            return 77;
        }
    }
    /* Gate precedes discovery, image access and ANY interface access. */
    if (update && !live_update_enabled) {
        (void)fprintf(stderr,"NO-GO: CRC-ACK freshness/bus exclusivity unproven; live update disabled; no discovery, firmware or CAN interface opened\n");
        return 77;
    }
    memset(&action,0,sizeof(action)); action.sa_handler=interrupt;
    (void)sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT,&action,NULL)!=0 || sigaction(SIGTERM,&action,NULL)!=0) return 1;
    if (update) config.query=!config.bootloader; /* One query, never periodic. */
    (void)printf("%s %s: %s\n",serve ? "serve-resources" : update ? "update discovery" : "probe",interface,
        serve ? "EXPLICIT resource session: one identification ping, checked advertisement, then requested downloads/storage" :
        config.query ? (config.bootloader ? "ACTIVE identification: one empty bootloader CAN query" :
                       "ACTIVE identification: one normal CAN ping") : "PASSIVE: zero CAN writes");
    if (ms2x_linux_open(interface,serve ? MS2X_LINUX_RESOURCES : update ? MS2X_LINUX_UPDATE :
        config.query ? (config.bootloader ? MS2X_LINUX_BOOT_QUERY : MS2X_LINUX_PING) : MS2X_LINUX_PASSIVE)!=0) {
        (void)fprintf(stderr,"SocketCAN open: %s\n",strerror(errno)); return 1;
    }
    outcome=ms2x_session_run(NULL,&config,&result);
    show_devices(&result);
    if (serve) {
        (void)printf("resource_target=%08" PRIX32 " type%04X requester_hash=%04X server_hash=%04X advertisement_local=%d requests=%u streams=%u server_conflict=%d installed_confirmed=%d\n",
            result.resource_target.uid,(unsigned)result.resource_target.hardware,(unsigned)result.requester_hash,
            (unsigned)result.server_hash,result.advertisement_local,result.resource_requests,result.streams,result.server_conflict,result.resources_confirmed);
        exit_status=outcome!=0 ? 1 : 0; goto done;
    }
    selection=ms2x_select_device(&result,config.target,config.bootloader,&selected);
    if (selection==MS2X_SELECTED)
        (void)printf("selected UID%08" PRIX32 " type%04X image=%s\n",selected.uid,
            (unsigned)selected.hardware,ms2x_profile_for(selected.hardware)->filename);
    else (void)printf("selection=%d: no eligible identity, unsupported type or ambiguity; explicit UID required for multiple devices\n",selection);
    if (!update && config.bootloader && config.target!=0U)
        (void)printf("bootloader_target=%08" PRIX32 " found=%d selection=%d\n",
            config.target,selection==MS2X_SELECTED,selection);
    exit_status=outcome!=0 ? 1 : 0;
    if (!update || outcome!=0) goto done;
    if (selection==MS2X_SELECTED) {
        if (hardware!=0U && hardware!=selected.hardware) { exit_status=2; goto done; }
        hardware=selected.hardware; config.target=selected.uid;
    } else if (!(selection==MS2X_SELECT_NONE && config.bootloader && config.target!=0U && hardware!=0U)) {
        (void)fprintf(stderr,"No unambiguous target: refuse update\n"); exit_status=2; goto done;
    } else (void)fprintf(stderr,"Explicit boot target; complete matching bootloader identity still required before data\n");
    if (image==NULL) {
        int length=snprintf(filename,sizeof(filename),"%s/%s",directory,ms2x_profile_for((uint16_t)hardware)->filename);
        if (length<0 || (size_t)length>=sizeof(filename)) { exit_status=2; goto done; }
        image=filename;
    }
    {
        ms2x_error error;
        firmware=ms2x_firmware_load_for(image,(uint16_t)hardware,&error);
        if (firmware==NULL) { (void)fprintf(stderr,"Invalid selected firmware/image hash (%d)\n",(int)error); exit_status=2; goto done; }
    }
    engine=ms2x_engine_new_target(firmware,MS2X_TIMEOUT_MS,config.target,(uint16_t)hardware);
    if (engine==NULL) { exit_status=1; goto done; }
    config.update=1; config.query=0; config.resources=directory; config.observe_ms=30000U;
    config.known=config.bootloader ? NULL : &selected;
    outcome=ms2x_session_run(engine,&config,&result);
    exit_status=ms2x_update_exit_status(outcome,&result);
    (void)printf("UID%08" PRIX32 " type%04X firmware_local=%d protocol_blocks_ack=%d start_local=%d app5.6=%d resources_offered=%d streams=%u installed_confirmed=%d firmware_success=%d resource_phase_error=%d; %s\n",
        config.target,(unsigned)hardware,result.firmware_local,result.firmware_protocol,result.start_local,
        result.app_version_ok,result.resources_offered,result.streams,result.resources_confirmed,
        exit_status!=1,exit_status==3,result.reason);
done:
    ms2x_linux_close(); ms2x_engine_free(engine); ms2x_firmware_free(firmware); return exit_status;
}
