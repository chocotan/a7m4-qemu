/* A7M4 +35f0b2c requires reply+8=0xf000. +35ed6e0 registers the
 * promise by original command/token. No active capture/release in this profile. */
#include <string.h>
#include "release-inhibit.h"
int a7m4_release_reply(struct a7m4_release_state *state, const void *request,
                       unsigned size, void *reply)
{
    unsigned words[4];
    if (!state || !request || !reply || size != 16 || state->active_release) return -1024;
    memcpy(words, request, sizeof words);
    if (words[0] != 0x403d0005 ||
        (words[2] != 0x8000 && words[2] != 0x200000) || words[3] > 1) return -1024;
    if (words[3]) state->inhibit_mask |= words[2];
    else state->inhibit_mask &= ~words[2];
    state->updates++;
    unsigned completion[3] = {0x403d0001, words[1], 0xf000};
    memcpy(reply, completion, sizeof completion);
    return 0;
}
#ifdef A7M4_RELEASE_SELFTEST
#include <assert.h>
#include <stdio.h>
int main(void)
{
    struct a7m4_release_state state = {0};
    unsigned q[4] = {0x403d0005,0x80000002,0x8000,1},r[3];
    assert(!a7m4_release_reply(&state,q,sizeof q,r));
    assert(state.inhibit_mask == 0x8000 && r[0] == 0x403d0001 && r[1] == q[1] && r[2] == 0xf000);
    q[3] = 0;
    assert(!a7m4_release_reply(&state,q,sizeof q,r) && !state.inhibit_mask);
    struct a7m4_release_state before = state;
    q[3] = 2; assert(a7m4_release_reply(&state,q,sizeof q,r) == -1024);
    assert(!memcmp(&state,&before,sizeof state));
    q[3] = 1; q[2] = 0x4000;
    assert(a7m4_release_reply(&state,q,sizeof q,r) == -1024);
    q[2] = 0x200000;
    assert(!a7m4_release_reply(&state,q,sizeof q,r) && state.inhibit_mask == 0x200000);
    q[3] = 0;
    assert(!a7m4_release_reply(&state,q,sizeof q,r) && !state.inhibit_mask);
    q[2] = 0x8000; state.active_release = 1;
    assert(a7m4_release_reply(&state,q,sizeof q,r) == -1024);
    state.active_release = 0;
    assert(a7m4_release_reply(&state,q,15,r) == -1024);
    assert(a7m4_release_reply(0,q,16,r) == -1024);
    assert(a7m4_release_reply(&state,0,16,r) == -1024);
    assert(a7m4_release_reply(&state,q,16,0) == -1024);
    puts("A7M4_RELEASE_SELFTEST_PASS");return 0;
}
#endif
