/* Functional metadata connection using the existing four-stage registry core.
 * Both endpoint owners consume actual allocated peer request/reply messages.
 * Format/data remain pending; this never reports a frame or capture ready. */
#include <string.h>
#include <stdio.h>
#include "device-models/pin-registry-core.h"
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void *live_reply,*retained_request;
static unsigned reply_owner;
static int faulted;
static unsigned word(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static unsigned half(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static void put(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static void frame(void *p,unsigned bytes,unsigned command,unsigned inner,unsigned flags){
    unsigned char *o=p;memset(o,0,bytes);put(o+4,0xa0a0a0a0);put(o+8,1|(bytes<<16));put(o+12,24);put(o+16,bytes);
    put(o+24,0x1000|(command<<16));put(o+28,inner|(flags<<16));
}
static int packet(const void *p,unsigned bytes,unsigned cmd,unsigned inner,unsigned flags){
    const unsigned char *q=p;
    return word(q+4)==0xa0a0a0a0 && word(q+8)==(1|(bytes<<16)) && word(q+12)==24 && word(q+16)==bytes &&
           half(q+24)==0x1000 && half(q+26)==cmd && half(q+28)==inner && half(q+30)==flags;
}
int a7m4_pin_link(struct pin_registry_owner *source,struct pin_registry_owner *peer,
                   unsigned destination,void *request,const struct full_imaging_io *io)
{
    if(!source || !peer || source==peer || !request || !io || !io->size || !io->source || !io->allocate ||
       !io->release || !io->send || !io->set_source || destination!=source->source || io->size(request)!=64)return -1024;
    const unsigned char *q=request;unsigned dst=io->source(request),s[4],p[4];
    memcpy(s,q+32,16);memcpy(p,q+48,16);
    int direction_pair=(s[1]==2 && p[1]==1) ||
        (s[0]==1 && s[1]==3 && p[1]==3 && s[2]==0 && p[2]==0 &&
         s[3]==0x90590818 && p[3]==0x9080003b);
    if(!packet(q,64,1,40,2) || word(q)!=dst || (dst>>28)!=8 || !(dst&0x0fffffffu) ||
       s[3]!=source->source || p[3]!=peer->source || s[0]!=p[0] || !direction_pair)return -1024;
    pthread_mutex_lock(&lock);
    if(faulted || live_reply){pthread_mutex_unlock(&lock);return -1021;}
    struct full_vef_snapshot a,b;pin_registry_snapshot(source,&a);pin_registry_snapshot(peer,&b);
    if(!a.initialized || !b.initialized || a.faults || b.faults || a.pending || b.pending ||
       a.live_replies || b.live_replies || !a.native_freed || !b.native_freed){pthread_mutex_unlock(&lock);return -1021;}
    int source_found=0,peer_found=0;
    for(unsigned i=0;i<source->count;i++)if(!memcmp(a.pins[i].self,s,16) && a.pins[i].state==1)source_found=1;
    for(unsigned i=0;i<peer->count;i++)if(!memcmp(b.pins[i].self,p,16) && b.pins[i].state==1)peer_found=1;
    if(!source_found || !peer_found){pthread_mutex_unlock(&lock);return -1024;}
    void *forward=0,*answer=0,*final=0;int rc=-1021;
    if(io->allocate(dst,&forward,64,1) || !forward)goto failed;
    if(forward==request){forward=0;goto failed;}
    if(io->allocate(dst,&answer,72,1) || !answer)goto failed;
    if(answer==request || answer==forward){answer=0;goto failed;}
    if(io->allocate(dst,&final,56,1) || !final)goto failed;
    if(final==request || final==answer || final==forward){final=0;goto failed;}
    if(io->size(forward)!=64 || io->size(answer)!=72 || io->size(final)!=56)goto failed;
    frame(forward,64,0x100,40,1);memcpy((char *)forward+32,s,16);memcpy((char *)forward+48,p,16);
    io->set_source(forward,source->source);
    if(pin_registry_connect_step(source,peer,s,p,1))goto failed;
    if(!packet(forward,64,0x100,40,1) || io->source(forward)!=source->source || memcmp((char *)forward+32,s,16) || memcmp((char *)forward+48,p,16) ||
       pin_registry_connect_step(source,peer,s,p,2))goto failed;
    frame(answer,72,0x8100,48,1);memcpy((char *)answer+32,s,16);memcpy((char *)answer+48,p,16);
    io->set_source(answer,peer->source);
    if(!packet(answer,72,0x8100,48,1) || io->source(answer)!=peer->source || word((const unsigned char *)answer+64) ||
       pin_registry_connect_step(source,peer,s,p,3))goto failed;
    if(io->release(dst,forward))goto failed;
    forward=0;
    if(io->release(dst,answer))goto failed;
    answer=0;
    if(pin_registry_connect_step(source,peer,s,p,4))goto failed;
    pin_registry_snapshot(source,&a);pin_registry_snapshot(peer,&b);
    int source_ok=0,peer_ok=0;
    for(unsigned i=0;i<source->count;i++)if(!memcmp(a.pins[i].self,s,16) && !memcmp(a.pins[i].peer,p,16) && !a.pins[i].state)source_ok=1;
    for(unsigned i=0;i<peer->count;i++)if(!memcmp(b.pins[i].self,p,16) && !memcmp(b.pins[i].peer,s,16) && !b.pins[i].state)peer_ok=1;
    if(!source_ok || !peer_ok || a.pending || b.pending)goto failed;
    frame(final,56,0x8001,32,2);memcpy((char *)final+32,s,16);io->set_source(final,destination);
    if(io->source(final)!=destination)goto failed;
    live_reply=final;reply_owner=dst;
    rc=io->send(dst,final);
    if(rc){live_reply=0;goto failed;}
    final=0;
    rc=io->release(destination,request);
    if(rc){retained_request=request;faulted=1;pin_registry_connect_fault(source,peer);}
    if(io->record){char line[300];snprintf(line,sizeof line,"source=%u,%u,%u,%#x peer=%u,%u,%u,%#x steps_consumed=4 source_state=0 peer_state=0 format_pending=1 pixels_consumed=0 request_free_rc=%d retained_request=%p",
        s[0],s[1],s[2],s[3],p[0],p[1],p[2],p[3],rc,retained_request);io->record(rc?"PIN_LINK_REQUEST_FREE_FAULT":"PIN_LINK_METADATA",line);}
    pthread_mutex_unlock(&lock);return 0;
failed:
    pin_registry_connect_cancel(source,peer,s,p);
    faulted=1;pin_registry_connect_fault(source,peer);
    if(forward)io->release(dst,forward);
    if(answer)io->release(dst,answer);
    if(final)io->release(dst,final);
    if(io->record)io->record("PIN_LINK_FAULT","connection rolled back; request retained; no success reply");
    pthread_mutex_unlock(&lock);return rc?rc:-1021;
}
int a7m4_pin_link_release(unsigned destination,void *reply,int (*release)(unsigned,void *),int *rc)
{
    pthread_mutex_lock(&lock);
    if(!reply || reply!=live_reply){pthread_mutex_unlock(&lock);return 0;}
    *rc=destination==reply_owner?release(destination,reply):-1021;
    if(*rc)faulted=1;else live_reply=0;
    pthread_mutex_unlock(&lock);return 1;
}
