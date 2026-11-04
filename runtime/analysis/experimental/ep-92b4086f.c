/* Minimal virtual endpoint for the A7M4 imaging-pipeline request id.
 * Observation + one echo/status completion per request; not a hardware model. */
typedef unsigned long size_t;
typedef unsigned int u32;
extern void *dlsym(void *, const char *);
extern long write(int, const void *, size_t);
extern int open(const char *, int, ...);
extern int snprintf(char *, size_t, const char *, ...);
extern void *memset(void *, int, size_t);
extern void *memcpy(void *, const void *, size_t);

static int (*reg_async)(unsigned int, void **, void *, unsigned long);
static int (*snd)(unsigned int, void *);
static int (*alloc)(unsigned int, void **, unsigned int, unsigned int);
static int (*free_msg)(unsigned int, void *);
static unsigned int (*msg_size)(void *);
static unsigned int (*get_src)(void *);
static int (*set_src)(void *, unsigned int);
static int console = -1;
static unsigned int requests;

static void *resolve(const char *name) { return dlsym((void *)-1, name); }
static void line(const char *tag, unsigned int a, unsigned int b, unsigned int c)
{
    char buf[160]; int n = snprintf(buf, sizeof buf, "A7M4_EP %s a=%#x b=%#x c=%#x\n", tag, a, b, c);
    if (n > 0 && console >= 0) (void)!write(console, buf, (size_t)n);
}
static void callback(void *arg, void *message)
{
    (void)arg;
    if (!message || !snd || !alloc || !get_src || !set_src || !free_msg || !msg_size) return;
    unsigned int src = get_src(message);
    unsigned int size = msg_size(message);
    const unsigned char *req = message;
    if (requests++ < 8) {
        line("REQ", src, size, size >= 4 ? (req[0] | req[1] << 8 | req[2] << 16 | req[3] << 24) : 0);
    }
    if (!src || size < 8) return;
    void *reply = 0;
    if (alloc(src, &reply, size, 1) || !reply) { line("ALLOC_FAIL", src, size, 0); return; }
    memset(reply, 0, size);
    memcpy(reply, req, size < 8 ? size : 8);
    unsigned char *r = reply;
    unsigned int cmd = req[2] | (req[3] << 8);
    unsigned int answer = cmd | 0x1000u;
    r[2] = answer & 0xff; r[3] = (answer >> 8) & 0xff;
    r[4] = size & 0xff; r[5] = (size >> 8) & 0xff;
    r[6] = 1; r[7] = 0;
    set_src(reply, 0x92b4086fu);
    int rc = snd(src, reply);
    line(rc ? "REPLY_FAIL" : "REPLY", src, size, (unsigned int)rc);
    if (rc) free_msg(src, reply);
}
static void arm(void)
{
    reg_async = (int (*)(unsigned int, void **, void *, unsigned long))resolve("u_osal_reg_msg_queue_cb");
    snd = (int (*)(unsigned int, void *))resolve("osal_snd_msg");
    alloc = (int (*)(unsigned int, void **, unsigned int, unsigned int))resolve("osal_valloc_msg_wait");
    free_msg = (int (*)(unsigned int, void *))resolve("osal_free_msg");
    msg_size = (unsigned int (*)(void *))resolve("osal_msg_size");
    get_src = (unsigned int (*)(void *))resolve("osal_get_msg_src");
    set_src = (int (*)(void *, unsigned int))resolve("osal_set_msg_src");
    console = open("/dev/console", 1);
    if (!reg_async) { line("NO_REG", 0, 0, 0); return; }
    unsigned int ids[2] = {0x2b4086fu, 0x86fu};
    for (unsigned int i = 0; i < 2; ++i) {
        void *h = 0; int rc = reg_async(ids[i], &h, (void *)callback, 0);
        line("ARM", ids[i], (unsigned int)rc, (unsigned int)(unsigned long)h);
    }
}
__attribute__((constructor)) static void init(void) { arm(); }
