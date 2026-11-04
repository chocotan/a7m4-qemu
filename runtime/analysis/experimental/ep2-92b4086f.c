/* Local completion for the A7M4 0x92b4086f imaging-pipeline request.
 * The kernel routes this id outside the ordinary queue table, so a registered
 * callback never sees it. Interpose the send, build one echo/status completion,
 * and deliver it to the source queue the caller waits on. Functional model only. */
typedef unsigned long size_t;
extern void *dlsym(void *, const char *);
extern long write(int, const void *, size_t);
extern int open(const char *, int, ...);
extern int snprintf(char *, size_t, const char *, ...);
extern void *memset(void *, int, size_t);
extern void *memcpy(void *, const void *, size_t);
extern int strncmp(const char *, const char *, size_t);
extern long read(int, void *, size_t);
extern int close(int);

static int (*real_snd)(unsigned int, void *);
static int (*real_sync)(unsigned int, void *);
static int (*alloc)(unsigned int, void **, unsigned int, unsigned int);
static int (*free_msg)(unsigned int, void *);
static unsigned int (*msg_size)(void *);
static unsigned int (*get_src)(void *);
static int (*set_src)(void *, unsigned int);
static int console = -1;
static unsigned int handled, attempts;
static int app_process;

static void *resolve(const char *name) { return dlsym((void *)-1, name); }
static void logl(const char *tag, unsigned int a, unsigned int b, unsigned int c)
{
    char buf[180]; int n = snprintf(buf, sizeof buf, "A7M4_EP2 %s a=%#x b=%#x c=%#x\n", tag, a, b, c);
    if (n > 0 && console >= 0) (void)!write(console, buf, (size_t)n);
}
static void init_symbols(void)
{
    if (real_snd) return;
    real_snd = (int (*)(unsigned int, void *))resolve("osal_snd_msg");
    real_sync = (int (*)(unsigned int, void *))resolve("osal_snd_sync_msg");
    alloc = (int (*)(unsigned int, void **, unsigned int, unsigned int))resolve("osal_valloc_msg_wait");
    free_msg = (int (*)(unsigned int, void *))resolve("osal_free_msg");
    msg_size = (unsigned int (*)(void *))resolve("osal_msg_size");
    get_src = (unsigned int (*)(void *))resolve("osal_get_msg_src");
    set_src = (int (*)(void *, unsigned int))resolve("osal_set_msg_src");
    console = open("/dev/console", 1);
}
static int complete(unsigned int dst, void *message)
{
    init_symbols();
    if (!message || !real_snd || !alloc || !free_msg || !msg_size || !get_src || !set_src) return -38;
    unsigned int src = get_src(message);
    unsigned int size = msg_size(message);
    const unsigned char *req = message;
    if (attempts++ < 12) logl("REQ", dst, src, size);
    if (!src || size < 8) return -1024;
    void *reply = 0;
    if (alloc(src, &reply, size, 1) || !reply) { logl("ALLOC_FAIL", src, size, 0); return -1023; }
    memset(reply, 0, size);
    memcpy(reply, req, size < 8 ? size : 8);
    unsigned char *r = reply;
    unsigned int cmd = req[2] | (req[3] << 8);
    unsigned int answer = cmd | 0x1000u;
    r[2] = answer & 0xff; r[3] = (answer >> 8) & 0xff;
    r[4] = size & 0xff; r[5] = (size >> 8) & 0xff;
    r[6] = 1; r[7] = 0;
    set_src(reply, dst);
    int rc = real_snd(src, reply);
    handled++;
    if (attempts <= 12) logl(rc ? "REPLY_FAIL" : "REPLY", src, size, (unsigned int)rc);
    if (rc) free_msg(src, reply);
    return 0;
}
int osal_snd_msg(unsigned int id, void *message)
{
    init_symbols();
    if (app_process && id == 0x92b4086fu) return complete(id, message);
    return real_snd ? real_snd(id, message) : -38;
}
int osal_snd_sync_msg(unsigned int id, void *message)
{
    init_symbols();
    if (app_process && id == 0x92b4086fu) return complete(id, message);
    return real_sync ? real_sync(id, message) : -38;
}
__attribute__((constructor)) static void init(void)
{
    char comm[32] = {0};
    int fd = open("/proc/self/comm", 0);
    if (fd >= 0) { long n = read(fd, comm, sizeof comm - 1); close(fd); (void)n; }
    console = open("/dev/console", 1);
    app_process = strncmp(comm, "im.elf", 6) == 0;
    if (app_process) {
        init_symbols();
        logl("ARM", 0x92b4086fu, 0, (unsigned int)(unsigned long)real_snd);
    }
}
