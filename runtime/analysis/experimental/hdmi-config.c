/* A7M4 appFw+35bc138 and +35bcf18: store cold-boot HDMI settings.
 * These are configuration writes; no cable event or video frame is generated. */
#include <string.h>
#include "hdmi-config.h"

static unsigned half(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
int a7m4_hdmi_config_reply(struct a7m4_hdmi_config *state, const void *request,
                          unsigned size, void *reply)
{
    const unsigned char *r = request;
    unsigned char *out = reply;
    if (!state || !r || !out || size < 10 || r[8] > 1 || r[9]) return -1024;
    unsigned command = half(r + 2), category = half(r), flags = half(r + 6);
    if (category == 0x200d && command == 0x32d && size == 10 && half(r + 4) == 10 && flags == 0x8001) {
        state->format = r[8];
        state->format_updates++;
        command = 0x132d;
    } else if (category == 0x2500 && command == 0x102 && size == 12 && half(r + 4) == 12 && flags == 1 && !r[10] && !r[11]) {
        state->premium_photo = r[8];
        state->photo_updates++;
        command = 0x8102;
    } else return -1024;
    memset(out, 0, 12);
    out[0] = category; out[1] = category >> 8;
    out[2] = command; out[3] = command >> 8;
    out[4] = 12;
    out[6] = flags; out[7] = flags >> 8;
    return 0;
}

#ifdef A7M4_HDMI_CONFIG_SELFTEST
#include <assert.h>
#include <stdio.h>
int main(void)
{
    struct a7m4_hdmi_config state = {0};
    unsigned char format[10] = {0x0d,0x20,0x2d,3,10,0,1,0x80,0,0};
    unsigned char photo[12] = {0,0x25,2,1,12,0,1,0,1,0,0,0};
    unsigned char out[12];
    assert(!a7m4_hdmi_config_reply(&state, format, 10, out));
    assert(!state.format && state.format_updates == 1 && half(out + 2) == 0x132d);
    assert(!a7m4_hdmi_config_reply(&state, photo, 12, out));
    assert(state.premium_photo == 1 && state.photo_updates == 1 && half(out + 2) == 0x8102);
    struct a7m4_hdmi_config before = state;
    for (unsigned i = 0; i < sizeof format; i++) {
        if (i == 8) continue;
        format[i] ^= 0x40;
        assert(a7m4_hdmi_config_reply(&state, format, 10, out) == -1024);
        assert(!memcmp(&state, &before, sizeof state));
        format[i] ^= 0x40;
    }
    format[8] = 2;
    assert(a7m4_hdmi_config_reply(&state, format, 10, out) == -1024);
    assert(a7m4_hdmi_config_reply(&state, photo, 11, out) == -1024);
    photo[11] = 1;
    assert(a7m4_hdmi_config_reply(&state, photo, 12, out) == -1024);
    assert(!memcmp(&state, &before, sizeof state));
    assert(a7m4_hdmi_config_reply(0, photo, 12, out) == -1024);
    assert(a7m4_hdmi_config_reply(&state, 0, 12, out) == -1024);
    assert(a7m4_hdmi_config_reply(&state, photo, 12, 0) == -1024);
    puts("A7M4_HDMI_CONFIG_SELFTEST_PASS");
    return 0;
}
#endif
