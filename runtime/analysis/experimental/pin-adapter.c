/* Explicit virtual BB metadata catalog, adapted from a7c2-qemu's shared
 * InfraBB Get-only owner. Get returns real registry state=1 (disconnected).
 * No Connect, frames, ready events or hardware compatibility are claimed. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "device-models/pin-registry-core.h"
#include "device-models/scalar-device.h"
#include "device-models/adf-device.h"
#include "device-models/hdmi-pin-device.h"
#include "device-models/audio-device.h"
#include "dig-level-device.h"
static const unsigned keys[21][3] = {
    {0,1,0},{0,1,1},{0,1,2},{0,1,3},{0,2,0},{0,2,1},{0,2,2},{0,2,3},
    {8,1,0},{8,2,0},{6,2,0},
    {0,1,10},{0,1,11},{0,1,12},{0,1,13},{0,2,10},{0,2,11},{0,2,12},{0,2,13},
    {8,1,10},{8,2,10}
};
static struct pin_registry_owner registry = {
    .source=0x9284074a, .count=21, .name="A7M4_BB", .keys=keys,
    .full_key_match=1, .reply_sid=0x9284074a
};
/* appFw 17b2b48..17b2bd8 and 17b33a4..17b3434 construct these
 * descriptors. This catalog is virtual metadata, not the original CP. */
static const unsigned vdf_keys[9][3]={
    {0,1,0},{0,1,1},{0,2,0},{0,2,1},{0,2,2},{8,1,0},
    {1,3,0},{6,2,0},{4,2,0}
};
static struct pin_registry_owner vdf={
    .source=0x90590818,.count=9,.name="A7M4_VDF",.keys=vdf_keys,
    .full_key_match=1,.reply_sid=0x90590818
};
/* Original AP now queries 6/1/0; the existing DrawInfo catalog also owns 0/1/0. */
static const unsigned draw_keys[2][3]={{0,1,0},{6,1,0}};
static struct pin_registry_owner draw={
    .source=0x928d0783,.count=2,.name="A7M4_DRAWINFO",.keys=draw_keys,.full_key_match=1
};
static const unsigned osd_keys[1][3]={{1,3,0}};
static struct pin_registry_owner osd={
    .source=0x9080003b,.count=1,.name="A7M4_OSD",.keys=osd_keys,.full_key_match=1,.reply_sid=0x9080003b
};
static struct pin_registry_owner *owner(unsigned sid)
{
    if(sid==registry.source)return &registry;
    if(sid==vdf.source)return &vdf;
    if(sid==draw.source)return &draw;
    if(sid==osd.source)return &osd;
    if(sid==FULL_VEF_CP)return full_vef_registry();
    if(sid==FULL_SCALAR_CP)return full_scalar_registry();
    if(sid==FULL_ADF_CP)return full_adf_registry();
    if(sid==FULL_HDMI_PIN_CP)return full_hdmi_pin_registry();
    return 0;
}
int a7m4_pin_handles(unsigned sid){return owner(sid)!=0;}
static pthread_once_t once=PTHREAD_ONCE_INIT;
static struct full_imaging_io io;
static int logfd=-1;
extern int a7m4_pin_link(struct pin_registry_owner *,struct pin_registry_owner *,unsigned,void *,const struct full_imaging_io *);
extern int a7m4_pin_link_release(unsigned,void *,int (*)(unsigned,void *),int *);
static void event(const char *tag,const char *detail)
{
    char text[1100];int n=snprintf(text,sizeof text,"%s %s\n",tag,detail);
    if(logfd>=0 && n>0 && n<(int)sizeof text)(void)!write(logfd,text,n);
}
static void initialize(void)
{
    io.allocate=dlsym(RTLD_NEXT,"osal_valloc_msg_wait");
    io.release=dlsym(RTLD_NEXT,"osal_free_msg");
    io.send=dlsym(RTLD_NEXT,"osal_snd_msg");
    io.set_source=dlsym(RTLD_NEXT,"osal_set_msg_src");
    io.size=dlsym(RTLD_NEXT,"osal_msg_size");
    io.source=dlsym(RTLD_NEXT,"osal_get_msg_src");
    io.record=event;logfd=open("/dev/console",O_WRONLY);
}
int a7m4_pin_dispatch(unsigned destination,void *request)
{
    pthread_once(&once,initialize);
    int rc=-1021;
    if(request && io.size && io.size(request)==64 && ((const unsigned short *)request)[13]==1)
        return a7m4_pin_link(owner(destination),owner(((const unsigned *)request)[15]),destination,request,&io);
    struct pin_registry_owner *target=owner(destination);
    if(target && pin_registry_dispatch(target,destination,request,&io,1,&rc))return rc;
    return -1021;
}
int a7m4_audio_dispatch(unsigned destination,void *request,int *rc)
{
    pthread_once(&once,initialize);
    if(destination==NATIVE_DIG_LEVEL_SID)return native_dig_level_dispatch(destination,request,&io,1,rc);
    if(destination!=FULL_AUDIO_INPUT || !request || !io.size || io.size(request)<4)return 0;
    const unsigned short *q=request;
    /* A7M4 producers 35bb46c,35bc5dc,35bc6d8,35bc7a8,35bba20,35bc818.
     * The functional capability owner has no audio input devices. */
    if(q[0]!=0x200d)return 0;
    switch(q[1]){
    case 0x0325:case 0x0327:case 0x0328:case 0x032a:case 0x032e:case 0x033d:break;
    default:return 0;
    }
    return full_audio_dispatch_capabilities(destination,request,&io,FULL_AUDIO_BOOT,FULL_AUDIO_CAPS_INPUTLESS,rc);
}
int a7m4_pin_release(unsigned destination,void *reply,int (*release)(unsigned,void *),int *rc)
{
    if(full_audio_release_reply(destination,reply,release,event,rc))return 1;
    if(native_dig_level_release(destination,reply,release,event,rc))return 1;
    if(a7m4_pin_link_release(destination,reply,release,rc)){
        char text[180];snprintf(text,sizeof text,"destination=%#x rc=%d native_reply_freed=%u",destination,*rc,!*rc);
        event("PIN_LINK_NATIVE_REPLY_FREE",text);return 1;
    }
    if(pin_registry_release(&registry,destination,reply,release,event,rc))return 1;
    if(pin_registry_release(&vdf,destination,reply,release,event,rc))return 1;
    if(pin_registry_release(&draw,destination,reply,release,event,rc))return 1;
    if(pin_registry_release(&osd,destination,reply,release,event,rc))return 1;
    if(full_scalar_release_reply(destination,reply,release,event,rc))return 1;
    if(full_adf_release_reply(destination,reply,release,event,rc))return 1;
    if(full_hdmi_pin_release_reply(destination,reply,release,event,rc))return 1;
    return full_vef_release_reply(destination,reply,release,event,rc);
}
