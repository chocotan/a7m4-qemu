#define _GNU_SOURCE
#include <dlfcn.h>
/* Patch AdjChkConfig's three absent-service GOT slots when the library is loaded.
 * This lets the original app_adjchk_ui::main_loop() get past its SIRCS setup. */
typedef unsigned long size_t;
extern int mprotect(void *, size_t, int);
extern int open(const char *, int, ...);
extern long write(int, const void *, size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern void *malloc(size_t);
extern void free(void *);
extern void *memset(void *, int, size_t);
static int (*real_snd)(unsigned int, void *);
static int (*real_alloc)(unsigned int, void **, unsigned int, unsigned int);
static int (*real_rcv)(unsigned int, void **, long);
static void *(*real_dlopen)(const char *, int);
static int fd = -1;
static unsigned int hits;
static void log2(const char *tag, unsigned long a, unsigned long b)
{
    char buf[180]; int n = snprintf(buf, sizeof buf, "SIRCS_FIX %s a=%#lx b=%#lx hits=%u\n", tag, a, b, hits);
    if (n > 0 && fd >= 0) (void)!write(fd, buf, (size_t)n);
}
static void resolve(void)
{
    if (real_snd) return;
    real_snd = (int (*)(unsigned int, void *))dlsym((void *)-1, "osal_snd_msg");
    real_alloc = (int (*)(unsigned int, void **, unsigned int, unsigned int))dlsym((void *)-1, "osal_valloc_msg_wait");
    real_rcv = (int (*)(unsigned int, void **, long))dlsym((void *)-1, "osal_rcv_msg_tmo");
}
static int stub_alloc(unsigned int id, void **message, unsigned int size, unsigned int flags)
{
    resolve();
    if (id == 0x807d0035u) {
        void *p = malloc(size ? size : 16);
        if (!p) return -1;
        memset(p, 0, size ? size : 16);
        *message = p;
        if (hits++ < 8) log2("ALLOC", id, size);
        return 0;
    }
    return real_alloc ? real_alloc(id, message, size, flags) : -38;
}
static int stub_snd(unsigned int id, void *message)
{
    resolve();
    if (id == 0x807d0035u) {
        if (hits++ < 8) log2("ACK", id, 0);
        free(message);
        return 0;
    }
    return real_snd ? real_snd(id, message) : -38;
}
static int stub_rcv(unsigned int id, void **message, long timeout)
{
    resolve();
    if (id == 0x80290119u) return -0x3fe;
    return real_rcv ? real_rcv(id, message, timeout) : -38;
}
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void patch_adj(void *handle)
{
    void *sym = dlsym(handle, "_ZN13app_adjchk_ui9main_loopEv");
    Dl_info info = {0};
    if (!sym || !dladdr(sym, &info) || !info.dli_fbase) { log2("ADJ_NO_SYMBOL", 0, 0); return; }
    unsigned long base = (unsigned long)info.dli_fbase;
    unsigned long page = (base + 0x23000UL) & ~4095UL;
    if (mprotect((void *)page, 8192, 3) != 0) { log2("ADJ_MPROTECT_FAIL", base, 0); return; }
    *(unsigned long *)(base + 0x23178UL) = (unsigned long)stub_snd;
    *(unsigned long *)(base + 0x231b8UL) = (unsigned long)stub_rcv;
    *(unsigned long *)(base + 0x23220UL) = (unsigned long)stub_alloc;
    log2("ADJ_PATCH", base, 0);
}
void *dlopen(const char *name, int flags)
{
    if (!real_dlopen) real_dlopen = (void *(*)(const char *, int))dlsym((void *)-1, "dlopen");
    int effective = flags & ~8;
    void *handle = real_dlopen ? real_dlopen(name, effective) : 0;
    if (handle && name && (same(name, "AdjChkConfig.so") || same(name, "/usr/lib/AdjChkConfig.so")))
        patch_adj(handle);
    return handle;
}
__attribute__((constructor)) static void init(void)
{
    fd = open("/dev/console", 1);
    log2("LOAD", 0, 0);
}
