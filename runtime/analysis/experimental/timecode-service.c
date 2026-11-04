/* Original A7M4 +35bad24 sends a 28B timecode setting wrapped by the
 * original DataflowInfraSender. Functional completion; no clock/media producer. */
#include <string.h>
#include "timecode-service.h"
static unsigned half(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned word(const unsigned char *p) { return half(p) | half(p + 2) << 16; }
int a7m4_timecode_reply(struct a7m4_timecode_state *state, const void *request,
                        unsigned size, void *reply)
{
    const unsigned char *q = request;
    unsigned char *r = reply;
    if (!state || !q || !r || size != 52 || word(q + 4) != 0xa0a0a0a0u ||
        half(q + 8) != 1 || half(q + 10) != 52 || word(q + 12) != 24 ||
        half(q + 24) != 0x2001 || half(q + 26) != 0x205 ||
        half(q + 28) != 28 || half(q + 30) != 1 || q[32] != 1) return -1024;
    memcpy(state->parameters, q + 32, 20);
    state->updates++;
    memset(r, 0, 36);
    memcpy(r, q, 24);
    r[10] = 36; r[11] = 0;
    r[16] = 36; r[17] = r[18] = r[19] = 0;
    r[20] = r[21] = r[22] = r[23] = 0;
    /* Explicit virtual timecode completion 0x8205, status at inner+8. */
    r[24] = 1; r[25] = 0x20; r[26] = 5; r[27] = 0x82;
    r[28] = 12; r[30] = 1;
    return 0;
}
#ifdef A7M4_TIMECODE_SELFTEST
#include <assert.h>
#include <stdio.h>
int main(void)
{
    unsigned char q[52] = {0};
    q[4] = q[5] = q[6] = q[7] = 0xa0;
    q[8] = 1; q[10] = 52; q[12] = 24; q[16] = 52;
    q[24] = 1; q[25] = 0x20; q[26] = 5; q[27] = 2;
    q[28] = 28; q[30] = 1; q[32] = 1;
    struct a7m4_timecode_state state = {0}; unsigned char r[36];
    assert(!a7m4_timecode_reply(&state,q,52,r));
    assert(state.updates == 1 && !memcmp(state.parameters,q+32,20));
    assert(half(r+10)==36 && word(r+12)==24 && word(r+16)==36);
    assert(half(r+24)==0x2001 && half(r+26)==0x8205 && half(r+28)==12 && !word(r+32));
    struct a7m4_timecode_state before=state;
    for(unsigned i=4;i<16;i++) {
        q[i]^=1;
        assert(a7m4_timecode_reply(&state,q,52,r)==-1024);
        assert(!memcmp(&state,&before,sizeof state));
        q[i]^=1;
    }
    assert(a7m4_timecode_reply(&state,q,51,r)==-1024);
    assert(a7m4_timecode_reply(0,q,52,r)==-1024);
    assert(a7m4_timecode_reply(&state,0,52,r)==-1024);
    assert(a7m4_timecode_reply(&state,q,52,0)==-1024);
    puts("A7M4_TIMECODE_SELFTEST_PASS");return 0;
}
#endif
