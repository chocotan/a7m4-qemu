/* Original A7M4 Lz77Decoder descriptor/vtable verified at libObj+156310.
 * Decode original resource streams into their original locked bitmaps. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "sony-lz77.h"
static uintptr_t base;
extern void a7m4_osd_start(unsigned long);
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static int console=-1;
static void record(const char *tag,const char *text)
{
    char line[400];int n=snprintf(line,sizeof line,"%s %s\n",tag,text);
    if(console<0)console=open("/dev/console",O_WRONLY);
    if(n>0 && n<(int)sizeof line && console>=0)(void)!write(console,line,n);
}
static unsigned long decode(void *object)
{
    if(!object || !base)return 0;
    const unsigned char *p=object;
    void *stream=*(void *const *)(p+8),*image=*(void *const *)(p+16);
    unsigned format=*(const unsigned *)(p+24),width=*(const unsigned short *)(p+28),height=*(const unsigned short *)(p+30);
    unsigned input_size=*(const unsigned *)(p+32),bpp=format==1?4:format==7?2:format==8?1:0;
    if(!stream || !image || !bpp || !width || !height || width>8192 || height>8192 || !input_size || input_size>0x1000000){
        record("A7M4_RESOURCE_LZ_FAIL","unsupported-descriptor");return 0;
    }
    unsigned long row=((width+15UL)&~15UL)*bpp,bytes=row*height;
    uintptr_t table=*(uintptr_t *)stream;
    if(bytes>0x1000000 || !table || *(uintptr_t *)(table+0x58)!=base+0x12f720){
        record("A7M4_RESOURCE_LZ_FAIL","unverified-getter-or-size");return 0;
    }
    const unsigned char *input=*(const unsigned char *const *)((const char *)stream+16);
    unsigned long available=*(const unsigned long *)((const char *)stream+24);
    if(input_size>available || sony_lz77_decode(input,input_size,0,bytes)){
        record("A7M4_RESOURCE_LZ_FAIL","stream-validation");return 0;
    }
    uintptr_t image_table=*(uintptr_t *)image;
    if(!image_table || !*(uintptr_t *)(image_table+0x58))return 0;
    typedef void *(*lock_fn)(void *,unsigned *);
    typedef void (*unlock_fn)(void *);
    typedef void (*size_fn)(void *,unsigned short *,unsigned short *);
    unsigned stride=0;unsigned short output_width=0,output_height=0;
    void *output=((lock_fn)(base+0x12c840))(image,&stride);
    if(!output){record("A7M4_RESOURCE_LZ_FAIL","bitmap-lock");return 0;}
    ((size_fn)*(uintptr_t *)(image_table+0x58))(image,&output_width,&output_height);
    int rc=5;
    if(stride==row && output_width>=width && output_height>=height)rc=sony_lz77_decode(input,input_size,output,bytes);
    unsigned long nonzero=0;
    if(!rc)for(unsigned long i=0;i<bytes;i++)nonzero+=((const unsigned char *)output)[i]!=0;
    ((unlock_fn)(base+0x12c858))(image);
    char text[300];snprintf(text,sizeof text,"format=%u width=%u height=%u input=%u available=%lu output=%lu stride=%u bitmap=%ux%u nonzero=%lu result=%d",
        format,width,height,input_size,available,bytes,stride,output_width,output_height,nonzero,rc);
    record(rc?"A7M4_RESOURCE_LZ_FAIL":"A7M4_RESOURCE_LZ_OK",text);
    return rc==0;
}
void a7m4_resource_lz_arm(void)
{
    pthread_mutex_lock(&mutex);
    if(base){pthread_mutex_unlock(&mutex);a7m4_osd_start(base);return;}
    void *symbol=dlsym(RTLD_DEFAULT,"_ZN6stella12MemoryStreamC1EPKvmb");
    Dl_info info={0};
    if(!symbol || !dladdr(symbol,&info) || !info.dli_fbase){pthread_mutex_unlock(&mutex);return;}
    uintptr_t candidate=(uintptr_t)info.dli_fbase,*slot=(uintptr_t *)(candidate+0x25ed20);
    const unsigned *getter=(const unsigned *)(candidate+0x12f720);
    if((uintptr_t)symbol!=candidate+0x12f934 || *slot!=candidate+0x156310 ||
       getter[0]!=0xf9400800 || getter[1]!=0xd65f03c0 ||
       mprotect((void *)((uintptr_t)slot&~4095UL),4096,PROT_READ|PROT_WRITE)){
        record("A7M4_RESOURCE_LZ_ARM_FAIL","original-slot-or-getter-mismatch");
    }else{
        base=candidate;*slot=(uintptr_t)decode;
        record("A7M4_RESOURCE_LZ_ARMED","libObj+25ed20 original=156310 getter=12f720 lock=12c840 unlock=12c858");
    }
    pthread_mutex_unlock(&mutex);
    if(base)a7m4_osd_start(base);
}
