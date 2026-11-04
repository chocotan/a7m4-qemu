/* SPDX-License-Identifier: MIT */
#include "device-models/audio-device.h"
#include "dmm-service.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct message {unsigned int size,source;unsigned char data[];};
static void *queued;
static const unsigned int caller=0x81cf61c8;
static struct message *meta(void *p){return (struct message *)((unsigned char *)p-sizeof(struct message));}
static int allocate(unsigned int owner,void **p,unsigned int n,unsigned int count){(void)owner;assert(count==1);struct message *m=calloc(1,sizeof *m+n);assert(m);m->size=n;*p=m->data;return 0;}
static int release(unsigned int owner,void *p){(void)owner;free(meta(p));return 0;}
static void set_source(void *p,unsigned int source){meta(p)->source=source;}
static unsigned int source(void *p){return meta(p)->source;}
static unsigned int size(void *p){return meta(p)->size;}
static int send(unsigned int destination,void *p){assert(destination==caller && !queued);queued=p;return 0;}
static void put16(unsigned char *p,unsigned int v){p[0]=v;p[1]=v>>8;}
static void put32(unsigned char *p,unsigned int v){for(unsigned int i=0;i<4;i++)p[i]=v>>(8*i);}
static unsigned int u32(const unsigned char *p){return p[0]|(unsigned int)p[1]<<8|(unsigned int)p[2]<<16|(unsigned int)p[3]<<24;}
static const struct full_imaging_io io={allocate,release,set_source,send,size,source,0};
static void request(unsigned int command,unsigned int n,const unsigned char *body,unsigned int reply_bytes){
    void *q=0;assert(!allocate(FULL_AUDIO_INPUT,&q,n,1));unsigned char *p=q;
    put16(p,0x200d);put16(p+2,command);put16(p+4,n);put16(p+6,0x8001);
    if(n>8)memcpy(p+8,body,n-8);
    set_source(q,caller);int result=999;
    assert(full_audio_dispatch_capabilities(FULL_AUDIO_INPUT,q,&io,FULL_AUDIO_BOOT,FULL_AUDIO_CAPS_INPUTLESS,&result));
    assert(!result && queued && size(queued)==reply_bytes && source(queued)==FULL_AUDIO_INPUT);
    p=queued;assert(((unsigned)p[2]|(unsigned)p[3]<<8)==(command|0x1000) && u32(p+8)==0x580000);
}
static void finish(void){int rc=999;assert(full_audio_release_reply(caller,queued,release,0,&rc) && !rc);queued=0;}
int main(int argc,char **argv){
    assert(argc==2);unsigned char *source_bytes=malloc(SYSTEM_SOUND_BYTES),*memory=calloc(1,SYSTEM_SOUND_CAPACITY);assert(source_bytes && memory);
    FILE *f=fopen(argv[1],"rb");assert(f && fread(source_bytes,1,SYSTEM_SOUND_BYTES,f)==SYSTEM_SOUND_BYTES && fgetc(f)==EOF);fclose(f);
    assert(!system_sound_load(memory,SYSTEM_SOUND_CAPACITY,source_bytes,SYSTEM_SOUND_BYTES));
    char metadata[600];snprintf(metadata,sizeof metadata,"{\"ready\":true,\"physical\":\"0x%llx\",\"virtual_bus\":\"0x%llx\",\"audio_phys64\":\"0x%llx\",\"capacity\":%lu,\"bytes\":%lu,\"fnv64\":\"%016llx\",\"source_sha256\":\"%s\",\"lifetime\":\"vm\",\"audio_output_verified\":false}\n",SYSTEM_SOUND_PHYSICAL,SYSTEM_SOUND_BUS,SYSTEM_SOUND_AUDIO_PHYS64,SYSTEM_SOUND_CAPACITY,SYSTEM_SOUND_BYTES,SYSTEM_SOUND_FNV,SYSTEM_SOUND_SHA256);
    full_audio_test_reset();assert(!full_sound_test_bind(memory,SYSTEM_SOUND_CAPACITY,source_bytes,SYSTEM_SOUND_BYTES,metadata));
    unsigned char body[32]={8};request(0x32e,10,body,12);finish();
    struct full_audio_snapshot s;full_audio_snapshot(&s);assert(s.known && s.wire_volume==8 && s.driver_level==7 && s.driver_consumed==1);
    request(0x33d,8,0,24);unsigned char *r=queued;assert(u32(r+16)==(unsigned int)SYSTEM_SOUND_AUDIO_PHYS64 && u32(r+20)==(SYSTEM_SOUND_AUDIO_PHYS64>>32));finish();
    request(0x325,8,0,28);finish();full_audio_snapshot(&s);assert(s.capabilities.valid && s.capabilities.queries==1);
    memset(body,0,sizeof body);put32(body,(unsigned int)SYSTEM_SOUND_AUDIO_PHYS64);put32(body+4,SYSTEM_SOUND_AUDIO_PHYS64>>32);put32(body+8,32);body[12]=1;body[14]=1;
    request(0x327,40,body,12);finish();full_audio_snapshot(&s);assert(s.table_applied==1 && s.table[0].size==32 && s.table[0].digest==system_sound_digest(source_bytes,32));
    body[0]=3;body[1]=1;request(0x32a,10,body,12);finish();full_audio_snapshot(&s);assert(s.system_volume[0]==3 && !s.path_applied);
    body[0]=7;body[1]=0;request(0x328,10,body,12);finish();full_audio_snapshot(&s);assert(s.system_path==7 && s.path_applied==1 && !s.faults && !s.live_replies && s.native_freed==6);
    struct dmm_service dmm={.physical=DMM_TEST_PHYSICAL,.memory=calloc(1,DMM_TEST_SIZE)};assert(dmm.memory);
    unsigned char q[40]={0x20},reply[40];put32(q+4,0x70817);put16(q+22,1);
    assert(!dmm_service_reply(&dmm,q,40,reply) && !reply[2]);
    assert(!dmm_service_copy_sound(&dmm,u32(reply+12),0,source_bytes,32));full_sound_bind_dmm(&dmm);
    memset(body,0,sizeof body);put32(body,0x1c100000);put32(body+8,32);body[12]=2;body[14]=1;
    request(0x327,40,body,12);finish();full_audio_snapshot(&s);assert(s.table[1].cpu==0x1c100000 && s.table[1].digest==system_sound_digest(source_bytes,32));
    dmm.memory[0x100000]^=1;const unsigned char *slice=0;assert(dmm_service_sound_slice(&dmm,0x1c100000,32,&slice));
    memory[0]^=1;assert(full_sound_ready()!=0);free(dmm.memory);free(source_bytes);free(memory);
    puts("A7M4_AUDIO_MODEL_SELFTEST_PASS");return 0;
}
