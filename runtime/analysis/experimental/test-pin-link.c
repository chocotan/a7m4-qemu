#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "device-models/pin-registry-core.h"
extern int a7m4_pin_link(struct pin_registry_owner *,struct pin_registry_owner *,unsigned,void *,const struct full_imaging_io *);
extern int a7m4_pin_link_release(unsigned,void *,int (*)(unsigned,void *),int *);
struct msg { unsigned size,source,released;unsigned char raw[80]; };
static struct msg *messages[12],*queued;static unsigned count,fail_send;
static struct msg *m(void *p){return (struct msg *)((char *)p - offsetof(struct msg,raw));}
static unsigned size(void *p){return m(p)->size;}
static unsigned source(void *p){return m(p)->source;}
static void set_source(void *p,unsigned v){m(p)->source=v;}
static int allocate(unsigned d,void **p,unsigned bytes,unsigned flags){
 assert(d==0x81cf6111 && flags==1 && count<12);struct msg *q=calloc(1,sizeof *q);assert(q);
 q->size=bytes;messages[count++]=q;*p=q->raw;return 0;
}
static int release(unsigned d,void *p){assert(d);assert(!m(p)->released++);return 0;}
static int send(unsigned d,void *p){assert(d==0x81cf6111 && !queued);if(fail_send)return -5;queued=m(p);return 0;}
static void put(unsigned char *p,unsigned x){for(unsigned i=0;i<4;i++)p[i]=x>>(8*i);}
int main(void){
 const unsigned s[4]={0,2,1,0x9284074a},p[4]={0,1,1,0x905a0048};
 const unsigned sk[1][3]={{0,2,1}},pk[1][3]={{0,1,1}};
 struct pin_registry_owner a={.source=s[3],.count=1,.keys=sk},b={.source=p[3],.count=1,.keys=pk};
 a.state.initialized=b.state.initialized=1;a.state.native_freed=b.state.native_freed=1;
 memcpy(a.state.pins[0].self,s,16);memcpy(b.state.pins[0].self,p,16);
 a.state.pins[0].state=b.state.pins[0].state=1;
 struct msg q={.size=64,.source=0x81cf6111};
 put(q.raw,q.source);put(q.raw+4,0xa0a0a0a0);put(q.raw+8,0x400001);
 put(q.raw+12,24);put(q.raw+16,64);put(q.raw+24,0x11000);put(q.raw+28,0x20028);
 memcpy(q.raw+32,s,16);memcpy(q.raw+48,p,16);
 struct full_imaging_io io={allocate,release,set_source,send,size,source,0};
 put(q.raw+60,p[3]^1);
 assert(a7m4_pin_link(&a,&b,a.source,q.raw,&io)==-1024 && !count && !q.released);
 put(q.raw+60,p[3]);
 assert(!a7m4_pin_link(&a,&b,a.source,q.raw,&io) && queued && q.released==1 && count==3);
 assert(!a.state.pins[0].state && !b.state.pins[0].state && !a.state.pending);
 assert(!memcmp(a.state.pins[0].peer,p,16) && !memcmp(b.state.pins[0].peer,s,16));
 assert(messages[0]->size==64 && messages[0]->released==1 && messages[1]->size==72 && messages[1]->released==1);
 assert(queued->size==56 && !queued->released && queued->source==a.source && !memcmp(queued->raw+32,s,16));
 assert(queued->raw[26]==1 && queued->raw[27]==0x80 && !queued->raw[48]);
 int rc=99;assert(a7m4_pin_link_release(q.source,queued->raw,release,&rc) && !rc && queued->released==1);
 queued=0;q.released=0;
 const unsigned os[4]={1,3,0,0x90590818},op[4]={1,3,0,0x9080003b},ok[1][3]={{1,3,0}};
 struct pin_registry_owner c={.source=os[3],.count=1,.keys=ok},d={.source=op[3],.count=1,.keys=ok};
 c.state=(struct full_vef_snapshot){.initialized=1,.native_freed=1};
 d.state=(struct full_vef_snapshot){.initialized=1,.native_freed=1};
 memcpy(c.state.pins[0].self,os,16);memcpy(d.state.pins[0].self,op,16);
 c.state.pins[0].state=d.state.pins[0].state=1;
 memcpy(q.raw+32,os,16);memcpy(q.raw+48,op,16);
 assert(!a7m4_pin_link(&c,&d,c.source,q.raw,&io) && queued && q.released==1 && count==6);
 assert(!c.state.pins[0].state && !d.state.pins[0].state && !c.state.pending);
 assert(!memcmp(c.state.pins[0].peer,op,16) && !memcmp(d.state.pins[0].peer,os,16));
 assert(a7m4_pin_link_release(q.source,queued->raw,release,&rc) && !rc);
 queued=0;q.released=0;fail_send=1;
 memcpy(q.raw+32,s,16);memcpy(q.raw+48,p,16);
 a.state=(struct full_vef_snapshot){.initialized=1,.native_freed=1};
 b.state=(struct full_vef_snapshot){.initialized=1,.native_freed=1};
 memcpy(a.state.pins[0].self,s,16);memcpy(b.state.pins[0].self,p,16);
 a.state.pins[0].state=b.state.pins[0].state=1;
 assert(a7m4_pin_link(&a,&b,a.source,q.raw,&io)==-5 && !q.released && !queued && count==9);
 assert(a.state.pins[0].state==1 && b.state.pins[0].state==1 && !a.state.pending);
 assert(a.state.pins[0].peer[0]==255 && b.state.pins[0].peer[0]==255 && a.state.faults==1 && b.state.faults==1);
 for(unsigned i=6;i<9;i++)assert(messages[i]->released==1);
 for(unsigned i=0;i<count;i++)free(messages[i]);
 puts("A7M4_PIN_LINK_SELFTEST_PASS");return 0;
}
