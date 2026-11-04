/* Diagnostic only: call the original boot screen opener after appFw is loaded.
 * This does not create a replacement UI; it invokes one original function. */
typedef unsigned long size_t;
typedef unsigned long pthread_t;
extern void *dlsym(void *, const char *);
extern int dladdr(void *, void *);
extern int open(const char *, int, ...);
extern long write(int, const void *, size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern int usleep(unsigned int);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
extern int pthread_setname_np(pthread_t, const char *);
extern long read(int, void *, unsigned long);
extern int close(int);
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
static int fd = -1;
static int is_app(void)
{
    char comm[32] = {0};
    int f = open("/proc/self/comm", 0);
    if (f < 0) return 0;
    long n = read(f, comm, sizeof comm - 1);
    close(f);
    return n >= 6 && comm[0] == 'i' && comm[1] == 'm' && comm[2] == '.';
}
static void logf(const char *s) { if (fd >= 0) (void)!write(fd, s, (size_t)snprintf((char[256]){0}, 0, "%s", s)); }
static void log2(const char *tag, unsigned long a, unsigned long b)
{
    char buf[200]; int n = snprintf(buf, sizeof buf, "CALL_UI %s a=%#lx b=%#lx\n", tag, a, b);
    if (n > 0 && fd >= 0) (void)!write(fd, buf, (size_t)n);
}
static void *worker(void *unused)
{
    (void)unused;
    for (int i = 0; i < 300; ++i) {
        void *sym = dlsym((void *)0, "Application_System_Init");
        if (sym) {
            Dl_info info = {0};
            if (dladdr(sym, &info) && info.dli_fbase) {
                unsigned long base = (unsigned long)info.dli_fbase;
                log2("FOUND", base, (unsigned long)sym - base);
                usleep(20000000);
                unsigned long fn = base + 0x272e40cUL;
                log2("CALL", fn, 0);
                ((void (*)(void))fn)();
                log2("RETURN", fn, 0);
                return 0;
            }
        }
        usleep(100000);
    }
    log2("NO_APPFW", 0, 0);
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
