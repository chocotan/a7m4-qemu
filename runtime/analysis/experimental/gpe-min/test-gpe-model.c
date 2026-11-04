/* SPDX-License-Identifier: MIT */
#include "gpe-sprite.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(void){
    /* A7M4 libgpe uses completion at +7a and A8 selector at +76; FX30 differs. */
    unsigned int p[36]={0x3a534100,4,20,4,600,416,20,4,600,416,0,0,640,0,0x105243c0,4,0,0,600,416,0,0,608,0,0,0,0,0,0x01ff0001,0,0x10000,0,0,0,0,0};
    struct gpe_sprite_plan plan;
    assert(!gpe_plan_sprite(p,&plan));
    unsigned char *dst=calloc(1,plan.memory.destination_span),*src=calloc(1,plan.memory.source_span);assert(dst && src);
    src[0]=0x0f;src[1]=0xf0;
    assert(!gpe_draw_sprite(dst,plan.memory.destination_span,src,plan.memory.source_span,&plan));
    unsigned long pixel=(4*640UL+20)*2;assert(dst[pixel]==0x0f && dst[pixel+1]==0xf0);
    p[30]=1;assert(gpe_plan_sprite(p,&plan)!=0);p[30]=0x10000;
    free(dst);free(src);
    p[2]=p[3]=p[6]=p[7]=0;p[4]=p[8]=p[18]=2;p[5]=p[9]=p[19]=1;p[12]=p[22]=2;
    p[23]=2;p[27]=0xffdddddd;p[29]=0xe0000;
    assert(!gpe_plan_sprite(p,&plan));
    unsigned char glyph[2]={255,0},out[4]={0};
    assert(!gpe_draw_sprite(out,sizeof out,glyph,sizeof glyph,&plan));
    assert(out[0]==0xdf && out[1]==0xdd && out[2]==0 && out[3]==0);
    puts("A7M4_GPE_MODEL_PIXELS_SELFTEST_PASS");return 0;
}
