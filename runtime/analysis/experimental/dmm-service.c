#include "dmm-service.h"
static unsigned int get(const unsigned char *p, unsigned int n) {
    unsigned int v = 0;
    for (unsigned int i = 0; i < n; ++i) v |= (unsigned int)p[i] << (8 * i);
    return v;
}
static void put(unsigned char *p, unsigned int v, unsigned int n) {
    for (unsigned int i = 0; i < n; ++i) p[i] = v >> (8 * i);
}
int dmm_service_reply(struct dmm_service *s, const unsigned char *q, unsigned int size, unsigned char *r) {
    if (!s || !q || !r || size < 40) return -1024;
    if (!s->memory || s->physical != DMM_TEST_PHYSICAL) return -1021;
    unsigned int op = get(q, 2), option = get(q + 22, 2), handle = get(q + 8, 4);
    if (op < 0x20 || op > 0x23) return -1024;
    for (int i = 0; i < 40; ++i) r[i] = 0;
    put(r, op | 0x8000, 2);
    put(r + 6, get(q + 20, 2), 2);
    put(r + 12, op == 0x20 ? 0xffffffffU : handle, 4);
    int raw = 0;
    unsigned int index = handle & 0x3ff;
    struct dmm_slot *slot = 0;
    if (op == 0x20) {
        if (option > 1) raw = -4; /* Other original options are outside this test model. */
        else if (get(q + 4, 4) != 0x63a && get(q + 4, 4) != 0x70817) raw = -3;
        else {
            unsigned int unit=get(q + 4,4),limit=unit==0x63a?2:8;
            for (index = unit==0x63a?0:2; index < limit && s->slots[index].allocated; ++index) {}
            if (index == limit) raw = -0x201;
            else {
                slot = s->slots + index;
                /* FX30 CP DmmCfg: unit70817@97bc -> extente7@1778, size c800. */
                slot->generation = (slot->generation + 1) & 31;
                handle = 0x8000U | (slot->generation << 10) | index;
                slot->handle = handle; slot->allocated = 1; slot->open_count = option;slot->unit=unit;
                unsigned int extent=index>=2?0xc800:0x80000,offset=index>=2?0x100000+(index-2)*0x10000:index*0x80000;
                for (unsigned int i = 0; i < extent; ++i) s->memory[offset + i] = 0;
                put(r + 12, handle, 4);
                put(r + 16, unit, 4);
            }
        }
    } else {
        if (index >= 8) raw = -0x105;
        else if (s->slots[index].handle != handle) raw = -0x205;
        else if (!s->slots[index].allocated) raw = -1;
        else {
            slot = s->slots + index;
            if (op == 0x21) {
                if (option > 1) raw = -4;
                else if (slot->open_count == 255) raw = -1;
                else ++slot->open_count;
            } else if (op == 0x22) {
                if (option) raw = -4;
                else if (!slot->open_count) raw = 1;
                else --slot->open_count;
            } else {
                if (option != 0 && option != 2) raw = -4;
                else {
                    if (option == 2 && slot->open_count) --slot->open_count;
                    raw = slot->open_count ? 3 : 0;
                    slot->open_count = 0; slot->allocated = 0;
                }
            }
        }
    }
    if (!raw && slot && ((op == 0x20 && option == 1) || op == 0x21)) {
        unsigned long long cp_address = s->physical - 0x400000000ULL + (index>=2?0x100000+(index-2)*0x10000:index*0x80000);
        put(r + 8, 1, 2);
        put(r + 24, (unsigned int)cp_address, 4);
        put(r + 28, (unsigned int)(cp_address >> 32), 4);
        put(r + 32, index>=2?0xc800:0x80000, 4);
    }
    int converted = raw == -0x201 ? -1 : (raw == -0x105 || raw == -0x205) ? -5 : raw;
    put(r + 10, (unsigned int)raw, 2);
    put(r + 2, (unsigned int)converted, 2);
    return 0;
}
static unsigned char sound_reference[6][0xc800];
static unsigned int sound_reference_handle[6],sound_reference_bytes[6];
int dmm_service_copy_sound(struct dmm_service *s,unsigned int handle,unsigned int offset,const unsigned char *source,unsigned int bytes) {
    if(!s || !s->memory || !source || !bytes || offset>0xc800 || bytes>0xc800-offset)return -1;
    unsigned int index=handle&0x3ff;if(index<2 || index>=8)return -1;
    struct dmm_slot *slot=&s->slots[index];index-=2;
    if(!slot->allocated || slot->handle!=handle || slot->unit!=0x70817 || !slot->open_count)return -1;
    unsigned char *destination=s->memory+0x100000+index*0x10000+offset;
    for(unsigned int i=0;i<bytes;i++)destination[i]=source[i];
    for(unsigned int i=0;i<bytes;i++)if(destination[i]!=source[i])return -1;
    if(!offset){
        for(unsigned int i=0;i<bytes;i++)sound_reference[index][i]=source[i];
        sound_reference_handle[index]=handle;sound_reference_bytes[index]=bytes;
    }
    return 0;
}
int dmm_service_sound_slice(struct dmm_service *s,unsigned long long address,unsigned int bytes,const unsigned char **out) {
    unsigned long long bus=DMM_TEST_PHYSICAL-0x400000000ULL+0x100000;
    if(!s || !s->memory || !out || address<bus || address-bus>=0x60000 || !bytes)return -1;
    unsigned int index=(unsigned int)(address-bus)/0x10000,offset=(unsigned int)(address-bus)%0x10000;
    if(offset>sound_reference_bytes[index] || bytes>sound_reference_bytes[index]-offset)return -1;
    struct dmm_slot *slot=&s->slots[index+2];
    if(!slot->allocated || !slot->open_count || slot->unit!=0x70817 || slot->handle!=sound_reference_handle[index])return -1;
    const unsigned char *memory=s->memory+0x100000+index*0x10000+offset;
    for(unsigned int i=0;i<bytes;i++)if(memory[i]!=sound_reference[index][offset+i])return -1;
    *out=memory;return 0;
}
#ifdef DMM_SERVICE_SELFTEST
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    struct dmm_service s = {.physical = DMM_TEST_PHYSICAL};
    s.memory = malloc(DMM_TEST_SIZE); assert(s.memory);
    unsigned char q[40] = {0x20}, r[40]; put(q + 4, 0x63a, 4); put(q + 22, 1, 2);
    assert(!dmm_service_reply(&s, q, 40, r));
    unsigned int first = get(r + 12, 4);
    assert(get(r + 2, 2) == 0 && get(r + 8, 2) == 1 && get(r + 24, 4) == 0x1c000000);
    assert(get(r + 32, 4) == 0x80000 && s.slots[0].open_count == 1 && !s.memory[0]);
    assert(!dmm_service_reply(&s, q, 40, r) && get(r + 24, 4) == 0x1c080000);
    assert(!dmm_service_reply(&s, q, 40, r) && (short)get(r + 2, 2) == -1 && (short)get(r + 10, 2) == -0x201);
    q[0] = 0x23; put(q + 8, first, 4); put(q + 22, 2, 2);
    s.memory[0] = 0xab;
    assert(!dmm_service_reply(&s, q, 40, r) && !get(r + 2, 2) && !s.slots[0].allocated);
    assert(!dmm_service_reply(&s, q, 40, r) && (short)get(r + 2, 2) == -1);
    q[0] = 0x20; put(q + 22, 1, 2);
    assert(!dmm_service_reply(&s, q, 40, r) && get(r + 12, 4) != first && !s.memory[0]);
    unsigned int current = get(r + 12, 4);
    put(q+4,0x70817,4);
    assert(!dmm_service_reply(&s,q,40,r) && !get(r+2,2) && get(r+32,4)==0xc800 && get(r+24,4)==0x1c100000);
    unsigned int sound=get(r+12,4);unsigned char sample[]={0x17,0x02,0xf4,0x0a};
    assert(sound&0x8000);
    assert(!dmm_service_copy_sound(&s,sound,0,sample,sizeof sample) && s.memory[0x100002]==0xf4);
    const unsigned char *slice=0;
    assert(!dmm_service_sound_slice(&s,0x1c100001,2,&slice) && slice[1]==0xf4);
    s.memory[0x100002]^=1;assert(dmm_service_sound_slice(&s,0x1c100001,2,&slice));s.memory[0x100002]^=1;
    assert(dmm_service_copy_sound(&s,sound,0xc7ff,sample,sizeof sample));
    assert(dmm_service_copy_sound(&s,sound^0x400,0,sample,sizeof sample));
    for(unsigned int i=1;i<6;i++)assert(!dmm_service_reply(&s,q,40,r) && !get(r+2,2) && get(r+24,4)==0x1c100000+i*0x10000);
    assert(!dmm_service_reply(&s,q,40,r) && (short)get(r+2,2)==-1);
    q[0]=0x23;put(q+8,sound,4);put(q+22,2,2);
    assert(!dmm_service_reply(&s,q,40,r) && !get(r+2,2) && !s.slots[2].allocated);
    assert(dmm_service_copy_sound(&s,sound,0,sample,sizeof sample));
    assert(dmm_service_sound_slice(&s,0x1c100000,2,&slice));
    q[0] = 0x21;put(q+22,1,2); put(q + 8, current, 4);
    assert(!dmm_service_reply(&s, q, 40, r) && s.slots[0].open_count == 2);
    q[0] = 0x22; put(q + 22, 0, 2);
    assert(!dmm_service_reply(&s, q, 40, r) && s.slots[0].open_count == 1);
    assert(!dmm_service_reply(&s, q, 40, r) && !s.slots[0].open_count);
    assert(!dmm_service_reply(&s, q, 40, r) && get(r + 2, 2) == 1);
    q[0] = 0x21;
    assert(!dmm_service_reply(&s, q, 40, r) && s.slots[0].open_count == 1);
    q[0] = 0x23;
    assert(!dmm_service_reply(&s, q, 40, r) && get(r + 2, 2) == 3 && !s.slots[0].allocated);
    q[0] = 0x21; put(q + 8, first, 4);
    assert(!dmm_service_reply(&s, q, 40, r) && (short)get(r + 2, 2) == -5);
    put(q + 8, 0xffffffffU, 4);
    assert(!dmm_service_reply(&s, q, 40, r) && (short)get(r + 2, 2) == -5);
    assert(dmm_service_reply(&s, q, 39, r) == -1024);
    free(s.memory); puts("DMM_SERVICE_SELFTEST_PASS");
}
#endif
