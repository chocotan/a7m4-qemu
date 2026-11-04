/* A7M4 appFw+35bb1c8 consumes 18B panel/EVF geometry. appFw+35ba044
 * stores the display selection. Explicit 640x480 functional displays only. */
#include <string.h>
#include "display-service.h"
static unsigned half(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
int a7m4_display_reply(struct a7m4_display_state *state, const void *request,
                       unsigned size, void *reply, unsigned *reply_size)
{
    const unsigned char *in = request;
    unsigned char *out = reply;
    if (!state || !in || !out || !reply_size || size < 8 || half(in + 4) != size) return -1024;
    unsigned command = half(in + 2);
    if (!half(in) && command == 0x101 && size == 12 && half(in + 6) == 1 &&
        in[8] <= 1 && !in[9] && !in[10] && !in[11]) {
        state->disconnected |= 1u << in[8];
        memset(out, 0, 16); out[2] = 1; out[3] = 0x81;
        out[4] = 16; out[6] = 1; out[12] = in[8];
        *reply_size = 16;
        return 0;
    }
    if (half(in) == 0x2500 && command == 0x109 && size == 12 && half(in + 6) == 1 &&
        in[8] <= 1 && !in[9] && !in[10] && !in[11]) {
        state->hdmi_mode = in[8]; state->hdmi_updates++;
        memset(out, 0, 12); out[1] = 0x25; out[2] = 9; out[3] = 0x81;
        out[4] = 12; out[6] = 1; *reply_size = 12;
        return 0;
    }
    if (half(in) == 0x2500 && command == 0x10a && size == 12 && half(in + 6) == 1 &&
        !in[8] && !in[9] && !in[10] && !in[11]) {
        state->hdmi_fixed_option = in[8]; state->option_updates++;
        memset(out, 0, 12); out[1] = 0x25; out[2] = 10; out[3] = 0x81;
        out[4] = 12; out[6] = 1; *reply_size = 12;
        return 0;
    }
    if (half(in) == 0x2500 && command == 0x101 && size == 16 && half(in + 6) == 1 &&
        !in[8] && !in[9] && !in[10] && !in[11] && in[12] == 6 &&
        !in[13] && !in[14] && !in[15]) {
        memcpy(state->hdmi_format,in+8,sizeof state->hdmi_format); state->hdmi_format_updates++;
        memset(out,0,12);out[1]=0x25;out[2]=1;out[3]=0x81;out[4]=12;out[6]=1;
        *reply_size=12;return 0;
    }
    if (half(in) == 0x2500 && command == 0x117 && size == 12 && half(in + 6) == 1 &&
        in[8] < 5 && in[10] == in[8] && in[11] == in[9] && (in[9] <= 6 || in[9] == 17)) {
        state->frame_rates[in[8]]=in[9];state->frame_rate_updates++;
        memset(out,0,12);out[1]=0x25;out[2]=0x17;out[3]=0x81;out[4]=12;out[6]=1;
        *reply_size=12;return 0;
    }
    if (half(in) != 0x200d || half(in + 6) != 0x8001) return -1024;
    if (command == 0x118 && size == 22 && half(in + 8) == 0x103 &&
        !half(in + 10) && !half(in + 12) && !half(in + 14) && !half(in + 16) &&
        !half(in + 18) && half(in + 20) == 1) {
        memcpy(state->panel, in + 8, sizeof state->panel);
        state->panel_updates++;
        *reply_size = 12;
        memset(out, 0, 12);
    } else if (command == 0x119 && size == 28) {
        /* 35bafc8..35bb0b4 emits exactly these six reverse combinations. */
        unsigned mask=in[8] | (unsigned)in[9]<<1 | (unsigned)in[10]<<2;
        if(in[8]>1 || in[9]>1 || in[10]>1 || mask==1 || mask==7) return -1024;
        for(unsigned i=11;i<28;i++)if(in[i])return -1024;
        memcpy(state->panel_reverse,in+8,sizeof state->panel_reverse);state->panel_reverse_updates++;
        *reply_size=12;memset(out,0,12);
    } else if (command == 0x11a && size == 14 && in[8] == 1 && in[9] == 14 &&
               !in[10] && in[11] == 14 && !in[13] &&
               (in[12] == 1 || in[12] == 2 || in[12] == 4 || in[12] == 6 ||
                in[12] == 8 || in[12] == 10 || in[12] == 14)) {
        memcpy(state->brightness, in + 8, sizeof state->brightness);
        state->brightness_updates++;
        *reply_size = 12;
        memset(out, 0, 12);
    } else if (command == 0x158 && size == 10 && !(in[8] & 0x10) && !in[9]) {
        /* Original +35ba740 sends this asynchronously and does not wait. */
        state->output_mask = in[8]; state->mask_updates++;
        *reply_size = 0;
        return 0;
    } else if (command == 0x14b && size == 40 && in[8] < 8 && in[9] <= 1 &&
               in[10] == 1 && !in[11] && half(in + 16) == 640 && half(in + 18) == 480) {
        memcpy(state->layouts[in[8]], in + 8, 32);
        state->layout_updates++;
        *reply_size = 12;
        memset(out, 0, 12);
    } else if (command == 0x146 && size == 584) {
        /* +35ba3f0 builds 18 records, each 32 bytes, for 640x480 outputs. */
        for (unsigned i = 0; i < 18; i++) {
            const unsigned char *plane = in + 8 + i * 32;
            if ((plane[0] != 1 && plane[0] != 2 && plane[0] != 4) || plane[1] > 7 ||
                plane[2] || plane[3] > 5 ||
                half(plane + 8) != 640 || half(plane + 10) != 480) return -1024;
        }
        memcpy(state->planes, in + 8, sizeof state->planes);
        state->plane_updates++;
        *reply_size = 12;
        memset(out, 0, 12);
    } else if (command == 0x160 && size == 10 && in[8] <= 4 && !in[9]) {
        state->monitor_lut = in[8]; state->lut_updates++;
        *reply_size = 12;
        memset(out, 0, 12);
    } else if ((command == 0x141 || command == 0x159 || command == 0x15f) &&
               size == 10 && in[8] <= 1 && !in[9]) {
        unsigned index = command == 0x141 ? 0 : command == 0x159 ? 1 : 2;
        state->osd_flags[index] = in[8]; state->osd_updates++;
        *reply_size = 0; return 0;
    } else if (command == 0x11b && size == 10 && in[8] == 3 && in[9] >= 1 && in[9] <= 16) {
        state->font_size = in[9]; *reply_size = 0; return 0;
    } else if (command == 0x144 && size == 24 && in[8] <= 1 && in[9] <= 1 &&
               in[10] <= 1 && in[11] <= 1 && in[12] <= 1 && in[13] <= 1 &&
               in[14] <= 1 && !in[15] && !half(in + 20) && !half(in + 22)) {
        memcpy(state->event_settings, in + 8, sizeof state->event_settings);
        state->event_queue = (unsigned)in[16] | (unsigned)in[17] << 8 |
                             (unsigned)in[18] << 16 | (unsigned)in[19] << 24;
        *reply_size = 0; return 0;
    } else if (command == 0x164 && size == 10 && in[8] == 2 && !in[9]) {
        state->monitor_size = in[8]; *reply_size = 12;
        memset(out, 0, 12);
    } else if (command == 0x14f && size == 20 && in[8] >= 1 && in[8] <= 2) {
        for (unsigned i=9;i<20;i++) if (in[i]) return -1024;
        memcpy(state->display_option,in+8,sizeof state->display_option);
        *reply_size = 0; return 0;
    } else if (command == 0x169 && size == 8) {
        *reply_size = 18;
        memset(out, 0, 18);
        out[8] = 0x80; out[9] = 2; /* 640 */
        out[10] = 0xe0; out[11] = 1; /* 480 */
        out[12] = 0x80; out[13] = 2;
        out[14] = 0xe0; out[15] = 1;
    } else if (command == 0x13c && size == 10 && in[8] <= 1 && in[9] <= 2) {
        state->selection = in[8]; state->mode = in[9]; state->updates++;
        *reply_size = 12;
        memset(out, 0, 12);
    } else return -1024;
    out[0] = 0x0d; out[1] = 0x20;
    unsigned completion = command | 0x1000;
    out[2] = completion; out[3] = completion >> 8;
    out[4] = *reply_size; out[6] = 1; out[7] = 0x80;
    return 0;
}
#ifdef A7M4_DISPLAY_SELFTEST
#include <assert.h>
#include <stdio.h>
int main(void)
{
    struct a7m4_display_state state = {0};
    unsigned char query[8] = {0x0d,0x20,0x69,1,8,0,1,0x80}, reply[18];
    unsigned size;
    assert(!a7m4_display_reply(&state, query, 8, reply, &size));
    assert(size == 18 && half(reply + 2) == 0x1169 && half(reply + 8) == 640 && half(reply + 10) == 480);
    assert(half(reply + 12) == 640 && half(reply + 14) == 480 && !reply[16] && !reply[17]);
    unsigned char setting[10] = {0x0d,0x20,0x3c,1,10,0,1,0x80,0,1};
    assert(!a7m4_display_reply(&state, setting, 10, reply, &size));
    assert(size == 12 && state.updates == 1 && state.mode == 1 && half(reply + 2) == 0x113c);
    struct a7m4_display_state before = state;
    setting[8] = 2;
    assert(a7m4_display_reply(&state, setting, 10, reply, &size) == -1024);
    setting[8] = 0; setting[9] = 3;
    assert(a7m4_display_reply(&state, setting, 10, reply, &size) == -1024);
    assert(!memcmp(&before, &state, sizeof state));
    for (unsigned i = 0; i < 8; i++) {
        query[i] ^= 0x40;
        assert(a7m4_display_reply(&state, query, 8, reply, &size) == -1024);
        query[i] ^= 0x40;
    }
    assert(a7m4_display_reply(&state, query, 7, reply, &size) == -1024);
    assert(a7m4_display_reply(0, query, 8, reply, &size) == -1024);
    assert(a7m4_display_reply(&state, 0, 8, reply, &size) == -1024);
    assert(a7m4_display_reply(&state, query, 8, 0, &size) == -1024);
    assert(a7m4_display_reply(&state, query, 8, reply, 0) == -1024);
    unsigned char panel[22] = {0x0d,0x20,0x18,1,22,0,1,0x80,3,1,0,0,0,0,0,0,0,0,0,0,1,0};
    assert(!a7m4_display_reply(&state, panel, 22, reply, &size));
    assert(size == 12 && state.panel_updates == 1 && !memcmp(state.panel, panel + 8, 14));
    assert(half(reply + 2) == 0x1118);
    before = state;
    for (unsigned i = 8; i < 22; i++) {
        panel[i] ^= 1;
        assert(a7m4_display_reply(&state, panel, 22, reply, &size) == -1024);
        assert(!memcmp(&state, &before, sizeof state));
        panel[i] ^= 1;
    }
    unsigned char reverse[28]={0x0d,0x20,0x19,1,28,0,1,0x80};
    const unsigned char original_reverse[6][3]={{0,0,0},{0,1,1},{1,1,0},{0,1,0},{1,0,1},{0,0,1}};
    for(unsigned i=0;i<6;i++){
        memcpy(reverse+8,original_reverse[i],3);
        assert(!a7m4_display_reply(&state,reverse,sizeof reverse,reply,&size));
        assert(!memcmp(state.panel_reverse,original_reverse[i],3));
    }
    before=state;reverse[8]=reverse[9]=reverse[10]=1;
    assert(a7m4_display_reply(&state,reverse,sizeof reverse,reply,&size)==-1024);
    assert(!memcmp(&state,&before,sizeof state));
    reverse[8]=reverse[9]=1;reverse[10]=0;
    assert(!a7m4_display_reply(&state,reverse,sizeof reverse,reply,&size));
    assert(size==12 && half(reply+2)==0x1119 && state.panel_reverse_updates==7);
    assert(!memcmp(state.panel_reverse,reverse+8,20));
    before=state;reverse[9]=0;
    assert(a7m4_display_reply(&state,reverse,sizeof reverse,reply,&size)==-1024);
    assert(!memcmp(&state,&before,sizeof state));reverse[9]=1;reverse[27]=1;
    assert(a7m4_display_reply(&state,reverse,sizeof reverse,reply,&size)==-1024);
    assert(!memcmp(&state,&before,sizeof state));
    unsigned char brightness[14] = {0x0d,0x20,0x1a,1,14,0,1,0x80,1,14,0,14,6,0};
    assert(!a7m4_display_reply(&state, brightness, 14, reply, &size));
    assert(size == 12 && state.brightness_updates == 1 && !memcmp(state.brightness, brightness + 8, 6));
    assert(half(reply + 2) == 0x111a);
    before = state; brightness[12] = 15;
    assert(a7m4_display_reply(&state, brightness, 14, reply, &size) == -1024);
    assert(!memcmp(&state, &before, sizeof state));
    unsigned char layout[40] = {0x0d,0x20,0x4b,1,40,0,1,0x80,7,1,1,0,0,0,0,0,0x80,2,0xe0,1};
    assert(!a7m4_display_reply(&state, layout, 40, reply, &size));
    assert(size == 12 && half(reply + 2) == 0x114b && state.layout_updates == 1);
    assert(!memcmp(state.layouts[7], layout + 8, 32));
    before = state; layout[8] = 8;
    assert(a7m4_display_reply(&state, layout, 40, reply, &size) == -1024);
    assert(!memcmp(&state, &before, sizeof state));
    unsigned char mask[10] = {0x0d,0x20,0x58,1,10,0,1,0x80,7,0};
    assert(!a7m4_display_reply(&state, mask, 10, reply, &size));
    assert(!size && state.output_mask == 7 && state.mask_updates == 1);
    before = state; mask[8] = 0x10;
    assert(a7m4_display_reply(&state, mask, 10, reply, &size) == -1024);
    assert(!memcmp(&state, &before, sizeof state));
    unsigned char planes[584] = {0x0d,0x20,0x46,1,0x48,2,1,0x80};
    for (unsigned i = 0; i < 18; i++) {
        unsigned char *plane = planes + 8 + i * 32;
        plane[0] = 1; plane[1] = 1; plane[3] = i % 6;
        plane[8] = 0x80; plane[9] = 2; plane[10] = 0xe0; plane[11] = 1;
    }
    assert(!a7m4_display_reply(&state, planes, sizeof planes, reply, &size));
    assert(size == 12 && state.plane_updates == 1 && !memcmp(state.planes, planes + 8, 576));
    assert(half(reply + 2) == 0x1146);
    before = state;
    for (unsigned i = 0; i < 18; i++) {
        planes[8 + i * 32 + 8] ^= 1;
        assert(a7m4_display_reply(&state, planes, sizeof planes, reply, &size) == -1024);
        assert(!memcmp(&state, &before, sizeof state));
        planes[8 + i * 32 + 8] ^= 1;
    }
    assert(a7m4_display_reply(&state, planes, 583, reply, &size) == -1024);
    unsigned char disconnect[12] = {0,0,1,1,12,0,1,0,1,0,0,0};
    assert(!a7m4_display_reply(&state, disconnect, 12, reply, &size));
    assert(size == 16 && half(reply + 2) == 0x8101 && reply[12] == 1 && state.disconnected == 2);
    before = state; disconnect[8] = 2;
    assert(a7m4_display_reply(&state, disconnect, 12, reply, &size) == -1024);
    assert(!memcmp(&state, &before, sizeof state));
    unsigned char hdmi[12] = {0,0x25,9,1,12,0,1,0,0,0,0,0};
    assert(!a7m4_display_reply(&state, hdmi, 12, reply, &size));
    assert(size == 12 && half(reply + 2) == 0x8109 && state.hdmi_mode == 0 && state.hdmi_updates == 1);
    before = state; hdmi[8] = 2;
    assert(a7m4_display_reply(&state, hdmi, 12, reply, &size) == -1024);
    assert(!memcmp(&state, &before, sizeof state));
    unsigned char lut[10] = {0x0d,0x20,0x60,1,10,0,1,0x80,0,0};
    for (unsigned i = 0; i <= 4; i++) {
        lut[8] = i;
        assert(!a7m4_display_reply(&state, lut, 10, reply, &size));
        assert(size == 12 && half(reply + 2) == 0x1160 && state.monitor_lut == i);
    }
    before = state; lut[8] = 5;
    assert(a7m4_display_reply(&state, lut, 10, reply, &size) == -1024);
    assert(!memcmp(&state, &before, sizeof state));
    unsigned char value[10] = {0x0d,0x20,0x41,1,10,0,1,0x80,1,0};
    for (unsigned i=0;i<3;i++) {
        value[2]=i==0?0x41:i==1?0x59:0x5f;
        assert(!a7m4_display_reply(&state,value,10,reply,&size) && !size);
        assert(state.osd_flags[i]==1);
    }
    value[2]=0x64; value[8]=2;
    assert(!a7m4_display_reply(&state,value,10,reply,&size) && size==12 && state.monitor_size==2);
    value[2]=0x1b; value[8]=3;value[9]=8;
    assert(!a7m4_display_reply(&state,value,10,reply,&size) && !size && state.font_size==8);
    unsigned char notify[24] = {0x0d,0x20,0x44,1,24,0,1,0x80,0,0,0,1,1,0,0,0,0x90,0x61,0xcf,0x81};
    assert(!a7m4_display_reply(&state,notify,24,reply,&size) && !size && state.event_queue==0x81cf6190);
    before=state; notify[11]=2;
    assert(a7m4_display_reply(&state,notify,24,reply,&size)==-1024 && !memcmp(&state,&before,sizeof state));
    unsigned char fixed[12] = {0,0x25,10,1,12,0,1,0,0,0,0,0};
    assert(!a7m4_display_reply(&state,fixed,12,reply,&size) && size==12 && state.option_updates==1);
    assert(half(reply+2)==0x810a);
    unsigned char option[20] = {0x0d,0x20,0x4f,1,20,0,1,0x80,2};
    assert(!a7m4_display_reply(&state,option,20,reply,&size) && !size && state.display_option[0]==2);
    before=state;option[9]=1;
    assert(a7m4_display_reply(&state,option,20,reply,&size)==-1024 && !memcmp(&state,&before,sizeof state));
    unsigned char hdmi_format[16] = {0,0x25,1,1,16,0,1,0,0,0,0,0,6,0,0,0};
    assert(!a7m4_display_reply(&state,hdmi_format,16,reply,&size) && size==12);
    assert(half(reply+2)==0x8101 && state.hdmi_format_updates==1 && !memcmp(state.hdmi_format,hdmi_format+8,8));
    before=state;hdmi_format[12]=7;
    assert(a7m4_display_reply(&state,hdmi_format,16,reply,&size)==-1024 && !memcmp(&state,&before,sizeof state));
    unsigned char rates[12]={0,0x25,0x17,1,12,0,1,0};
    for(unsigned i=0;i<5;i++){
        rates[8]=rates[10]=i;rates[9]=rates[11]=(i==2||i==3)?17:0;
        assert(!a7m4_display_reply(&state,rates,12,reply,&size) && size==12 && half(reply+2)==0x8117);
        assert(state.frame_rates[i]==rates[9]);
    }
    before=state;rates[10]^=1;
    assert(a7m4_display_reply(&state,rates,12,reply,&size)==-1024 && !memcmp(&state,&before,sizeof state));
    puts("A7M4_DISPLAY_SELFTEST_PASS");
    return 0;
}
#endif
