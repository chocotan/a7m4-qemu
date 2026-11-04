/* Guest-only diagnostic. Stop one original thread, read registers/frames,
 * always detach; no application registers or memory are changed. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

/* Modern _GNU_SOURCE headers redirect strtoul to a symbol absent in glibc 2.25. */
extern unsigned long firmware_strtoul(const char *, char **, int) __asm__("strtoul");
struct arm64_regs { unsigned long regs[31], sp, pc, pstate; };
static unsigned long peek(pid_t tid, unsigned long address, int *ok)
{
    errno = 0;
    unsigned long word = (unsigned long)ptrace(PTRACE_PEEKDATA, tid, (void *)address, 0);
    if (errno) *ok = 0;
    return word;
}
static int run(int argc, char **argv)
{
    if (argc != 2) return 2;
    char *end;
    unsigned long parsed = firmware_strtoul(argv[1], &end, 10);
    if (*end || parsed < 1 || parsed > 0x7fffffffUL) return 2;
    pid_t tid = (pid_t)parsed;
    if (ptrace(PTRACE_ATTACH, tid, 0, 0)) {
        printf("THREAD_INSPECT ATTACH_FAIL tid=%d errno=%d\n", tid, errno);
        return 3;
    }
    int status, result = 0;
    if (waitpid(tid, &status, __WALL) != tid || !WIFSTOPPED(status)) {
        printf("THREAD_INSPECT WAIT_FAIL tid=%d errno=%d\n", tid, errno);
        result = 4;
        goto detach;
    }
    struct arm64_regs regs;
    memset(&regs, 0, sizeof regs);
    struct iovec iov = { &regs, sizeof regs };
    if (ptrace(PTRACE_GETREGSET, tid, (void *)1, &iov) || iov.iov_len != sizeof regs) {
        printf("THREAD_INSPECT REGS_FAIL tid=%d errno=%d size=%lu\n", tid, errno, (unsigned long)iov.iov_len);
        result = 5;
        goto detach;
    }
    printf("THREAD_INSPECT REGS tid=%d pc=%#lx sp=%#lx fp=%#lx lr=%#lx\n",
           tid, regs.pc, regs.sp, regs.regs[29], regs.regs[30]);
    for (unsigned i = 0; i < 31; i++)
        printf("THREAD_INSPECT X tid=%d n=%u value=%#lx\n", tid, i, regs.regs[i]);
    unsigned long fp = regs.regs[29];
    for (unsigned i = 0; i < 24; i++) {
        int ok = 1;
        if (fp < regs.sp || fp - regs.sp > 0x800000UL || (fp & 15)) break;
        unsigned long next = peek(tid, fp, &ok);
        unsigned long lr = peek(tid, fp + 8, &ok);
        if (!ok) { printf("THREAD_INSPECT FRAME_UNREADABLE tid=%d fp=%#lx\n", tid, fp); break; }
        printf("THREAD_INSPECT FRAME tid=%d depth=%u fp=%#lx lr=%#lx\n", tid, i, fp, lr);
        if (next <= fp) break;
        fp = next;
    }
    char path[64], buf[8192];
    snprintf(path, sizeof path, "/proc/%d/maps", tid);
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) > 0) (void)!write(1, buf, n);
        close(fd);
    }
detach:
    if (ptrace(PTRACE_DETACH, tid, 0, 0)) {
        printf("THREAD_INSPECT DETACH_FAIL tid=%d errno=%d\n", tid, errno);
        return 6;
    }
    printf("THREAD_INSPECT DETACHED tid=%d result=%d\n", tid, result);
    return result;
}
extern int __libc_start_main(int (*)(int, char **), int, char **,
                            void (*)(void), void (*)(void), void (*)(void), void *);
__asm__(".global _start\n.type _start,%function\n_start:\nmov x0,sp\nbl entry\nbrk #0\n");
__attribute__((noreturn)) void entry(unsigned long *stack)
{
    __libc_start_main(run, (int)stack[0], (char **)(stack + 1), 0, 0, 0, stack);
    __builtin_trap();
}
