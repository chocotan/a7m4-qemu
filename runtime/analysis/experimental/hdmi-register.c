/* SPDX-License-Identifier: MIT
 * Wire layout adapted from fx30-qemu/guest/ui/fx30-hdmi-service.c.
 * A7M4 run ui-trace-20261104-130854 stops in appFw+0x35b8ab4;
 * seq-20261104-110901 recorded this exact request. No cable event is emitted. */
#include <string.h>

int a7m4_hdmi_register(unsigned *observer, const void *request, unsigned size, void *reply)
{
    static const unsigned char expected[12] = {
        0, 0x25, 0x27, 1, 12, 0, 1, 0, 0x16, 1, 0x2a, 0x80
    };
    static const unsigned char completion[12] = {
        0, 0x25, 0x27, 0x81, 12, 0, 1, 0, 0, 0, 0, 0
    };
    if (!observer || !request || !reply || size != sizeof expected ||
        memcmp(request, expected, sizeof expected)) return -1024;
    *observer = 0x802a0116u;
    memcpy(reply, completion, sizeof completion);
    return 0;
}

#ifdef A7M4_HDMI_REGISTER_SELFTEST
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const unsigned char request[12] = {0, 0x25, 0x27, 1, 12, 0, 1, 0, 0x16, 1, 0x2a, 0x80};
    const unsigned char completion[12] = {0, 0x25, 0x27, 0x81, 12, 0, 1, 0, 0, 0, 0, 0};
    unsigned observer = 0;
    unsigned char reply[12], invalid[13], untouched[12];
    memset(untouched, 0xa5, sizeof untouched);
    for (unsigned i = 0; i < sizeof request; i++) {
        memcpy(invalid, request, sizeof request);
        invalid[i] ^= 1;
        memcpy(reply, untouched, sizeof reply);
        assert(a7m4_hdmi_register(&observer, invalid, 12, reply) == -1024);
        assert(!observer && !memcmp(reply, untouched, sizeof reply));
    }
    assert(a7m4_hdmi_register(&observer, request, 11, reply) == -1024);
    assert(a7m4_hdmi_register(&observer, request, 13, reply) == -1024);
    assert(a7m4_hdmi_register(0, request, 12, reply) == -1024);
    assert(a7m4_hdmi_register(&observer, 0, 12, reply) == -1024);
    assert(a7m4_hdmi_register(&observer, request, 12, 0) == -1024);
    assert(!observer);
    assert(!a7m4_hdmi_register(&observer, request, 12, reply));
    assert(observer == 0x802a0116u && !memcmp(reply, completion, 12));
    assert(!a7m4_hdmi_register(&observer, request, 12, reply));
    assert(observer == 0x802a0116u);
    puts("A7M4_HDMI_REGISTER_SELFTEST_PASS");
    return 0;
}
#endif
