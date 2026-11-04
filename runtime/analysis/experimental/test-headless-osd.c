/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include "headless-osd.c"
#include <assert.h>
static unsigned int frames;
static int fail_frame;
static int frame(struct hosd_frame *f,unsigned char *pixels){
    if(fail_frame)return -1;
    f->physical=0x43a534100;memset(pixels,++frames,HOSD_PIXELS);return 0;
}
static void quiet(const char *tag,const char *text){(void)tag;(void)text;}
int main(void){
    char path[]="/tmp/a7m4-osd-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    struct hosd_state *s=calloc(1,sizeof *s);assert(s);s->have_descriptor=1;s->pending=0x1003;
    unsigned char *d=s->descriptor;d[0]=3;d[1]=0x10;put32(d+8,0x402);d[20]=0x80;d[21]=2;d[22]=0xe0;d[23]=1;
    struct hosd_ops ops={0};ops.frame=frame;ops.log=quiet;
    assert(!consume(s,&ops,path) && s->sequence==1 && s->pixel_source_seq==1 && s->retained_pixels[0]==1);
    s->pending=0x1005;assert(!consume(s,&ops,path) && frames==2 && s->sequence==2 && s->pixel_source_seq==2 && s->retained_pixels[0]==2);
    fail_frame=1;assert(consume(s,&ops,path)==-1 && s->sequence==2);
    FILE *f=fopen(path,"rb");assert(f);unsigned char header[HOSD_HEADER];assert(fread(header,1,sizeof header,f)==sizeof header);
    assert(header[120]==2);assert(!fseek(f,HOSD_HEADER+sizeof s->frozen,SEEK_SET) && fgetc(f)==2);fclose(f);
    unlink(path);free(s);puts("A7M4_OSD_CONTENT_AND_FAILED_CAPTURE_SELFTEST_PASS");return 0;
}
