/* One-shot probe: log remote IPC requests and where im.elf threads are waiting.
 * Also wraps view::create_screen so a real call is visible. Not a UI. */
typedef unsigned long size_t;
typedef unsigned long pthread_t;
extern void *dlsym(void *, const char *);
extern int dladdr(void *, void *);
extern int mprotect(void *, size_t, int);
extern int open(const char *, int, ...);
extern long write(int, const void *, size_t);
extern long read(int, void *, unsigned long);
extern int close(int);
extern int snprintf(char *, size_t, const char *, ...);
extern int usleep(unsigned int);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;

static int fd = -1;
static int (*real_snd)(unsigned int, void *);
static int (*real_sync)(unsigned int, void *);
static unsigned int (*msg_size)(void *);
static unsigned int (*get_src)(void *);
static void *(*real_create)(void *, void *, void *, void *, void *, void *, void *, void *);
static unsigned logged;

static void emit(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    if (fd >= 0 && n) (void)!write(fd, s, n);
}
static void log3(const char *tag, unsigned long a, unsigned long b, unsigned long c)
{
    char buf[220];
    int n = snprintf(buf, sizeof buf, "WAIT %s a=%#lx b=%#lx c=%#lx\n", tag, a, b, c);
    if (n > 0 && fd >= 0) (void)!write(fd, buf, (size_t)n);
}
static void dump_file(const char *path, int limit)
{
    char buf[700];
    int f = open(path, 0);
    if (f < 0) return;
    long n = read(f, buf, sizeof buf - 1);
    close(f);
    if (n <= 0) return;
    if (n > limit) n = limit;
    buf[n] = 0;
    emit("WAIT FILE ");
    emit(path);
    emit(" ");
    emit(buf);
    if (buf[n - 1] != '\n') emit("\n");
}
static void dump_threads(void)
{
    int tid;
    for (tid = 1; tid < 480; tid++) {
        char path[64], comm[32];
        int f, n;
        snprintf(path, sizeof path, "/proc/self/task/%d/comm", tid);
        f = open(path, 0);
        if (f < 0) continue;
        n = (int)read(f, comm, sizeof comm - 1);
        close(f);
        if (n <= 0) continue;
        comm[n] = 0;
        log3("TID", (unsigned long)tid, 0, 0);
        emit("WAIT COMM ");
        emit(comm);
        snprintf(path, sizeof path, "/proc/self/task/%d/wchan", tid);
        dump_file(path, 80);
        snprintf(path, sizeof path, "/proc/self/task/%d/stack", tid);
        dump_file(path, 500);
    }
}
static void *wrap_create(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7)
{
    log3("CREATE_SCREEN", (unsigned long)a0, (unsigned long)a1, (unsigned long)a2);
    return real_create ? real_create(a0, a1, a2, a3, a4, a5, a6, a7) : 0;
}
/* appFw+0x3643f3c accepts a sync reply when osal_snd_sync_msg returns 0 and
 * message+4 is a known mode (0), message+8 is 1/2/4, message+12 is 1..4. */
static int answer_display(void *message)
{
    unsigned int *w = message;
    if (!w) return -38;
    w[0] = 0x920111u;
    w[1] = 0;
    w[2] = 1;
    w[3] = 1;
    w[4] = 0;
    w[5] = 0;
    log3("DISP_ACK", 0x80924127UL, w[1], w[2]);
    return 0;
}
static void arm_screen(void)
{
    void *sym = dlsym((void *)0, "Application_System_Init");
    Dl_info info = {0};
    unsigned long base, slot, page;
    if (!sym || !dladdr(sym, &info) || !info.dli_fbase) return;
    base = (unsigned long)info.dli_fbase;
    slot = base + 0x584e0a8UL;
    page = slot & ~4095UL;
    if (mprotect((void *)page, 8192, 3)) {
        log3("SLOT_MPROTECT", slot, 0, 0);
        return;
    }
    real_create = (void *)*(unsigned long *)slot;
    *(unsigned long *)slot = (unsigned long)wrap_create;
    log3("SLOT", slot, (unsigned long)real_create - base, (unsigned long)wrap_create);
}
static void log_remote(const char *tag, unsigned int id, void *message)
{
    unsigned int size = 0, src = 0, w0 = 0, w1 = 0, w2 = 0;
    const unsigned char *b = message;
    if (logged > 40) return;
    if (msg_size && message) size = msg_size(message);
    if (get_src && message) src = get_src(message);
    if (b && size >= 4) w0 = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
    if (b && size >= 8) w1 = b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24);
    if (b && size >= 12) w2 = b[8] | (b[9] << 8) | (b[10] << 16) | (b[11] << 24);
    logged++;
    log3(tag, id, src, size);
    log3("BYTES", w0, w1, w2);
}
int osal_snd_msg(unsigned int id, void *message)
{
    if (!real_snd) real_snd = (int (*)(unsigned int, void *))dlsym((void *)-1, "osal_snd_msg");
    if (!msg_size) msg_size = (unsigned int (*)(void *))dlsym((void *)-1, "osal_msg_size");
    if (!get_src) get_src = (unsigned int (*)(void *))dlsym((void *)-1, "osal_get_msg_src");
    if (id & 0x80000000u) log_remote("SND", id, message);
    return real_snd ? real_snd(id, message) : -38;
}
int osal_snd_sync_msg(unsigned int id, void *message)
{
    if (!real_sync) real_sync = (int (*)(unsigned int, void *))dlsym((void *)-1, "osal_snd_sync_msg");
    if (!msg_size) msg_size = (unsigned int (*)(void *))dlsym((void *)-1, "osal_msg_size");
    if (!get_src) get_src = (unsigned int (*)(void *))dlsym((void *)-1, "osal_get_msg_src");
    if (id == 0x80924127u) {
        if (!real_create) arm_screen();
        return answer_display(message);
    }
    if ((id & 0x80000000u) && logged < 40) log_remote("SYNC", id, message);
    return real_sync ? real_sync(id, message) : -38;
}
static int is_app(void)
{
    char comm[32] = {0};
    int f = open("/proc/self/comm", 0);
    long n;
    if (f < 0) return 0;
    n = read(f, comm, sizeof comm - 1);
    close(f);
    return n >= 3 && comm[0] == 'i' && comm[1] == 'm' && comm[2] == '.';
}
static void *worker(void *unused)
{
    (void)unused;
    usleep(8000000);
    arm_screen();
    dump_threads();
    usleep(20000000);
    emit("WAIT SECOND\n");
    dump_threads();
    return 0;
}
__attribute__((constructor)) static void init(void)
{
    pthread_t t;
    if (!is_app()) return;
    fd = open("/dev/console", 1);
    emit("WAIT LOAD\n");
    if (!pthread_create(&t, 0, worker, 0)) pthread_detach(t);
}
