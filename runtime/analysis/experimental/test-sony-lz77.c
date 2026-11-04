#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sony-lz77.h"
int main(void)
{
    const unsigned char raw[]={0x0f,0,3,0,'a','b','c'};
    unsigned char out[8];memset(out,0xa5,sizeof out);
    assert(!sony_lz77_decode(raw,sizeof raw,out,3) && !memcmp(out,"abc",3));
    const unsigned char compressed[]={0xf0,0x0c,'a','b',0,2,0,0};
    assert(!sony_lz77_decode(compressed,sizeof compressed,out,5) && !memcmp(out,"ababa",5));
    for(unsigned long n=1;n<sizeof compressed;n++){
        memset(out,0xa5,sizeof out);assert(sony_lz77_decode(compressed,n,out,5));
        for(unsigned i=0;i<sizeof out;i++)assert(out[i]==0xa5);
    }
    memset(out,0xa5,sizeof out);
    assert(sony_lz77_decode(raw,sizeof raw,out,4));
    for(unsigned i=0;i<sizeof out;i++)assert(out[i]==0xa5);
    unsigned char overlap[8];memcpy(overlap,raw,sizeof raw);
    assert(sony_lz77_decode(overlap,sizeof raw,overlap+1,3));
    assert(!sony_lz77_decode(raw,sizeof raw,0,3));
    assert(sony_lz77_decode(0,sizeof raw,out,3));
    puts("A7M4_SONY_LZ77_BOUNDED_SELFTEST_PASS");return 0;
}
