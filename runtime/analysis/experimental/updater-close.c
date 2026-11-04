/* Empty virtual updater session only. No firmware transfer/write is implemented.
 * Original libupdaterufp+0x1928 sends {command=4, reserved}; +0x19d0 checks
 * matching command and +0x19e0 checks zero status before releasing its queue. */
#include <string.h>

int a7m4_updater_close(int previously_touched, const void *request, unsigned size, void *reply)
{
    static const unsigned completion[2] = {4, 0};
    unsigned command;
    if (previously_touched || !request || !reply || size != 8) return -1024;
    memcpy(&command, request, sizeof command);
    if (command != 4) return -1024;
    memcpy(reply, completion, sizeof completion);
    return 0;
}

#ifdef A7M4_UPDATER_CLOSE_SELFTEST
#include <assert.h>
#include <stdio.h>
int main(void)
{
    unsigned request[2] = {4, 0xa5a5a5a5}, reply[2] = {7, 7};
    assert(a7m4_updater_close(1, request, 8, reply) == -1024);
    assert(reply[0] == 7 && reply[1] == 7);
    for (unsigned command = 0; command < 9; command++) {
        if (command == 4) continue;
        request[0] = command;
        assert(a7m4_updater_close(0, request, 8, reply) == -1024);
        assert(reply[0] == 7 && reply[1] == 7);
    }
    request[0] = 4;
    assert(a7m4_updater_close(0, request, 7, reply) == -1024);
    assert(a7m4_updater_close(0, request, 9, reply) == -1024);
    assert(a7m4_updater_close(0, 0, 8, reply) == -1024);
    assert(a7m4_updater_close(0, request, 8, 0) == -1024);
    assert(!a7m4_updater_close(0, request, 8, reply));
    assert(reply[0] == 4 && reply[1] == 0);
    assert(!a7m4_updater_close(0, request, 8, reply));
    puts("A7M4_UPDATER_CLOSE_SELFTEST_PASS");
    return 0;
}
#endif
