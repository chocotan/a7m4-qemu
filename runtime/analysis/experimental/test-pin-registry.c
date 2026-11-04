#define _GNU_SOURCE
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "device-models/pin-registry-core.h"
struct message { unsigned bytes,source,released; unsigned char raw[80]; };
static struct message *queued;
static const unsigned keys[1][3]={{0,2,1}};
static struct pin_registry_owner owner={.source=0x9284074a,.count=1,.name="TEST",.keys=keys,.full_key_match=1,.reply_sid=0x9284074a};
static struct message *msg(void *raw){return (struct message *)((char *)raw - offsetof(struct message,raw));}
static unsigned size(void *raw){return msg(raw)->bytes;}
static unsigned source(void *raw){return msg(raw)->source;}
static void set_source(void *raw,unsigned value){msg(raw)->source=value;}
static int release(unsigned destination,void *raw){assert(destination);assert(!msg(raw)->released++);return 0;}
static int allocate(unsigned destination,void **raw,unsigned bytes,unsigned flags){
    assert(destination==0x81cf6111 && flags==1 && bytes==56);
    struct message *m=calloc(1,sizeof *m);assert(m);m->bytes=bytes;*raw=m->raw;return 0;
}
static int send(unsigned destination,void *raw){assert(destination==0x81cf6111);assert(!queued);queued=msg(raw);return 0;}
static void put(unsigned char *p,unsigned value){for(unsigned i=0;i<4;i++)p[i]=value>>(8*i);}
static unsigned word(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
int main(void){
    struct full_imaging_io io={allocate,release,set_source,send,size,source,0};
    struct message q={.bytes=48,.source=0x81cf6111};
    put(q.raw,0x81cf6111);put(q.raw+4,0xa0a0a0a0);put(q.raw+8,0x300001);
    put(q.raw+12,24);put(q.raw+16,48);put(q.raw+24,0x1000);put(q.raw+28,0x20018);
    put(q.raw+36,2);put(q.raw+40,1);
    int rc=99;
    assert(pin_registry_dispatch(&owner,0x9284074a,q.raw,&io,1,&rc) && !rc && q.released==1 && queued);
    assert(word(queued->raw+44)==1 && word(queued->raw+48)==0x9284074a && !word(queued->raw+52));
    assert(word(queued->raw+32)==0 && word(queued->raw+36)==2 && word(queued->raw+40)==1);
    assert(pin_registry_release(&owner,0x81cf6111,queued->raw,release,0,&rc) && !rc && queued->released==1);
    free(queued);queued=0;
    struct full_vef_snapshot state;pin_registry_snapshot(&owner,&state);
    assert(state.queries==1 && state.request_freed==1 && state.native_freed==1 && !state.live_replies);
    assert(state.pins[0].state==1 && state.pins[0].peer[0]==255);
    q.released=0;put(q.raw+40,2);
    assert(pin_registry_dispatch(&owner,0x9284074a,q.raw,&io,1,&rc) && rc==-1021 && !q.released && !queued);
    put(q.raw+40,1);put(q.raw+44,0x9284074a);
    assert(pin_registry_dispatch(&owner,0x9284074a,q.raw,&io,1,&rc) && rc==-1021 && !q.released && !queued);
    put(q.raw+44,0);q.bytes=47;
    assert(pin_registry_dispatch(&owner,0x9284074a,q.raw,&io,1,&rc) && rc==-1021 && !q.released);
    puts("A7M4_PIN_REGISTRY_SELFTEST_PASS");return 0;
}
