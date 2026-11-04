#define _GNU_SOURCE
#include <dlfcn.h>
/* Diagnostic: run the original app_adjchk_ui::main_loop() on a fresh thread and
 * redirect AdjChkConfig's osal_snd_msg GOT slot so the missing SIRCS start service
 * is acknowledged. This is a functional stub for one absent service, not a host UI. */
typedef unsigned long size_t;
typedef unsigned long pthread_t;
extern void *dlsym(void *, const char *);
extern void *dlopen(const char *, int);
extern int mprotect(void *, size_t, int);
extern int open(const char *, int, ...);
extern long write(int, const void *, size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern int usleep(unsigned int);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
extern long read(int, void *, unsigned long);
extern int close(int);
static int fd = -1;
static int (*real_snd)(unsigned int, void *);
static int (*real_free)(unsigned int, void *);
static int (*real_alloc)(unsigned int, void **, unsigned int, unsigned int);
static int (*real_rcv)(unsigned int, void **, long);
extern void *malloc(size_t);
extern void free(void *);
extern void *memset(void *, int, size_t);
static unsigned int acked;
static void clear_cache(void *start, size_t length)
{
    unsigned long ctr, step, addr = (unsigned long)start & ~63UL;
    unsigned long end = (unsigned long)start + length;
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
    step = 4UL << ((ctr >> 16) & 0xf);
    if (!step) step = 64;
    __asm__ __volatile__("dsb ish" ::: "memory");
    for (; addr < end; addr += step)
        __asm__ __volatile__("ic ivau, %0" :: "r"(addr) : "memory");
    __asm__ __volatile__("dsb ish; isb" ::: "memory");
}
static void log2(const char *tag, unsigned long a, unsigned long b)
{
    char buf[200]; int n = snprintf(buf, sizeof buf, "CALL_ML %s a=%#lx b=%#lx\n", tag, a, b);
    if (n > 0 && fd >= 0) (void)!write(fd, buf, (size_t)n);
}
static void resolve(void)
{
    if (real_snd) return;
    real_snd = (int (*)(unsigned int, void *))dlsym((void *)-1, "osal_snd_msg");
    real_free = (int (*)(unsigned int, void *))dlsym((void *)-1, "osal_free_msg");
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
        if (acked++ < 4) log2("SIRCS_ALLOC", id, size);
        return 0;
    }
    return real_alloc ? real_alloc(id, message, size, flags) : -38;
}
static int stub_rcv(unsigned int id, void **message, long timeout)
{
    resolve();
    if (id == 0x80290119u) return -0x3fe;
    return real_rcv ? real_rcv(id, message, timeout) : -38;
}
static int stub_snd(unsigned int id, void *message)
{
    resolve();
    if (id == 0x807d0035u) {
        if (acked++ < 4) log2("SIRCS_ACK", id, 0);
        free(message);
        return 0;
    }
    return real_snd ? real_snd(id, message) : -38;
}
static int is_app(void)
{
    char comm[32] = {0};
    int f = open("/proc/self/comm", 0);
    if (f < 0) return 0;
    long n = read(f, comm, sizeof comm - 1);
    close(f);
    return n >= 6 && comm[0] == 'i' && comm[1] == 'm' && comm[2] == '.';
}
static unsigned long adj_base;
static void *(*real_create_layer)(void *, const void *);
static void *stub_create_layer(void *self, const void *id)
{
    if (adj_base) {
        void *layer = *(void **)(adj_base + 0x23738UL);
        if (layer) { log2("LAYER_REUSE", (unsigned long)layer, 0); return layer; }
    }
    return real_create_layer ? real_create_layer(self, id) : 0;
}
static void patch_ret(void *handle, const char *name)
{
    void *sym = dlsym(handle, name);
    Dl_info info = {0};
    if (!sym || !dladdr(sym, &info) || !info.dli_fbase) return;
    unsigned long addr = (unsigned long)sym;
    if (mprotect((void *)(addr & ~4095UL), 4096, 7) != 0) return;
    *(unsigned long *)addr = 0xd65f03c0UL;
    clear_cache((void *)addr, 8);
    log2("RET_PATCH", addr, 0);
}
static void *worker(void *unused)
{
    (void)unused;
    void *symbol = dlsym((void *)0, "_ZN13app_adjchk_ui9main_loopEv");
    log2("SYMBOL0", (unsigned long)symbol, 0);
    if (!symbol) {
        void *handle = dlopen("/usr/lib/AdjChkConfig.so", 1);
        log2("DLOPEN1", (unsigned long)handle, 0);
        if (handle) symbol = dlsym(handle, "_ZN13app_adjchk_ui9main_loopEv");
    }
    log2("SYMBOL", (unsigned long)symbol, 0);
    if (!symbol) return 0;
    Dl_info info = {0};
    if (dladdr(symbol, &info) && info.dli_fbase) {
        unsigned long base = (unsigned long)info.dli_fbase;
        unsigned long slot = base + 0x23178UL;
        if (mprotect((void *)(slot & ~4095UL), 8192, 3) == 0) {
            *(unsigned long *)slot = (unsigned long)stub_snd;
            *(unsigned long *)(base + 0x23220UL) = (unsigned long)stub_alloc;
            *(unsigned long *)(base + 0x231b8UL) = (unsigned long)stub_rcv;
            adj_base = base;
            void *obj = dlopen("/usr/lib/libObj.so", 1);
            real_create_layer = (void *(*)(void *, const void *))dlsym(obj, "_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE");
            if (!real_create_layer) real_create_layer = (void *(*)(void *, const void *))dlsym((void *)0, "_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE");
            *(unsigned long *)(base + 0x230f8UL) = (unsigned long)stub_create_layer;
            log2("CREATE_LAYER_PATCH", base + 0x230f8UL, (unsigned long)real_create_layer);
            log2("GOT_PATCH", slot, (unsigned long)stub_snd);
        } else log2("GOT_FAIL", slot, 0);
    }
    usleep(30000000);
    void *app = dlsym((void *)0, "Application_System_Init");
    Dl_info ai = {0};
    if (app && dladdr(app, &ai) && ai.dli_fbase) {
        unsigned long entry = (unsigned long)ai.dli_fbase + 0x183dfe8UL;
        log2("APP_ENTRY", entry, (unsigned long)ai.dli_fbase);
        ((void (*)(void))entry)();
        log2("APP_RETURN", entry, 0);
    } else {
        log2("CALL", (unsigned long)symbol, 0);
        ((void (*)(void))symbol)();
        log2("RETURN", (unsigned long)symbol, 0);
    }
    return 0;
}
__attribute__((constructor)) static void init(void)
{
    if (!is_app()) return;
    fd = open("/dev/console", 1);
    log2("LOAD", 0, 0);
    pthread_t t;
    if (!pthread_create(&t, 0, worker, 0)) pthread_detach(t);
}
