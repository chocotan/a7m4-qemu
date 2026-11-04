/* A7M4 AP framebuffer consumer. Original VDT replies follow actual leased
 * pixel capture and durable snapshot readback, never an empty ready ACK. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "headless-osd.h"
static pthread_mutex_t lifecycle=PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread;
static unsigned started,live,stop;
static unsigned long libobj;
static int console=-1;
static struct hosd_native native;
static struct hosd_ops ops;
static void record(const char *tag,const char *text)
{
    char line[600];int n=snprintf(line,sizeof line,"%s %s\n",tag,text);
    if(console<0)console=open("/dev/console",O_WRONLY);
    if(n>0 && n<(int)sizeof line && console>=0)(void)!write(console,line,n);
}
static int frame(struct hosd_frame *identity,unsigned char *pixels)
{
    return hosd_capture_frame(libobj,&native,identity,pixels);
}
static void finish(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&lifecycle);
    __atomic_store_n(&stop,1,__ATOMIC_RELEASE);
    if(live){
        int rc=pthread_join(thread,0);
        if(rc)record("A7M4_OSD_JOIN_FAILED","worker join failed");
        else{live=0;record("A7M4_OSD_JOINED","receiver released before registered process exit cleanup");}
    }
    pthread_mutex_unlock(&lifecycle);
}
extern int __cxa_atexit(void (*)(void *),void *,void *);
void a7m4_osd_start(unsigned long base)
{
    pthread_mutex_lock(&lifecycle);
    if(started || stop){pthread_mutex_unlock(&lifecycle);return;}
    int (*stat_queue)(unsigned,unsigned *)=dlsym(RTLD_NEXT,"osal_stat_msgq");
    if(!stat_queue){pthread_mutex_unlock(&lifecycle);return;}
    const unsigned queues[]={HOSD_CONTROL,HOSD_UPDATE,HOSD_META_UPDATE};
    for(unsigned i=0;i<3;i++){
        unsigned state[4]={0};
        if(stat_queue(queues[i],state)){pthread_mutex_unlock(&lifecycle);return;}
    }
    started=1;libobj=base;
    native=(struct hosd_native){
        dlsym(RTLD_NEXT,"osal_wai_sem_tmo"),dlsym(RTLD_NEXT,"osal_sig_sem"),
        (void *)(base+0xfc498),dlsym(RTLD_DEFAULT,"GRM_bitmapReadLockBits"),
        dlsym(RTLD_DEFAULT,"GRM_bitmapReadUnlockBits"),dlsym(RTLD_DEFAULT,"GRM_bitmapRelease"),
        dlsym(RTLD_DEFAULT,"GRM_bitmapGetPhysicalAddress"),dlsym(RTLD_DEFAULT,"GRM_bitmapGetRowPixelCount"),
        dlsym(RTLD_DEFAULT,"GRM_bitmapGetSize"),dlsym(RTLD_DEFAULT,"a7m4_raster_trylock"),
        dlsym(RTLD_DEFAULT,"a7m4_raster_unlock"),record};
    ops=(struct hosd_ops){dlsym(RTLD_NEXT,"osal_rcv_msg_tmo"),dlsym(RTLD_NEXT,"osal_msg_size"),
        dlsym(RTLD_NEXT,"osal_get_msg_src"),dlsym(RTLD_NEXT,"osal_free_msg"),
        dlsym(RTLD_NEXT,"osal_valloc_msg_wait"),dlsym(RTLD_NEXT,"osal_set_msg_src"),
        dlsym(RTLD_NEXT,"osal_snd_msg"),frame,record,&stop};
    char api_line[300];snprintf(api_line,sizeof api_line,"base=%#lx read_lock=%p physical=%p row=%p size=%p raster=%p,%p",
        base,native.read_lock,native.physical,native.row_pixels,native.size,native.try_raster,native.unlock_raster);
    record("A7M4_OSD_API",api_line);
    if(!native.wait_sem || !native.signal_sem || !native.read_lock || !native.read_unlock ||
       !native.release || !native.physical || !native.row_pixels || !native.size ||
       !native.try_raster || !native.unlock_raster || !ops.receive || !ops.size || !ops.source ||
       !ops.release || !ops.allocate || !ops.set_source || !ops.send ||
       (unsigned long)native.read_lock!=base+0xfc448 || (unsigned long)native.physical!=base+0xfc420 ||
       hosd_claim("/log/qemu/a7m4-osd.claim","/proc/modules",stat_queue,record)){
        record("A7M4_OSD_START_FAILED","original API/queue ownership validation failed");
    }else if(__cxa_atexit(finish,0,0)){
        record("A7M4_OSD_START_FAILED","cannot register process exit cleanup");
    }else{
        int rc=pthread_create(&thread,0,hosd_worker,&ops);
        if(rc)record("A7M4_OSD_START_FAILED","cannot create consumer");
        else{live=1;record("A7M4_OSD_OWNER_STARTED","A7M4 mgr=2d0580 bitmap_vptr=254630 native lease and shared raster lock");}
    }
    pthread_mutex_unlock(&lifecycle);
}
