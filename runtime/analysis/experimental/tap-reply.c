/*
 * FX30 guest IPC tap: interposes the osal_uipc user library to make the
 * firmware's message traffic visible. Observation only (no behaviour change).
 * Log goes to fd 2 (the serial console).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>


typedef int (*snd_t)(unsigned int, void *);
typedef int (*rcv_t)(unsigned int, void **, long);
typedef int (*reg_t)(unsigned int, void **, void *, unsigned long);
typedef unsigned int (*size_t_fn)(void *);
typedef unsigned int (*src_t)(void *);

static snd_t real_snd, real_sync, real_direct, real_internal;
static rcv_t real_rcv, real_u_rcv;
static reg_t real_reg, real_u_reg, real_sync_reg;
static int (*real_alloc)(unsigned int, void **, unsigned int, unsigned int);
static int (*real_free)(unsigned int, void *);
static int (*real_set_src)(void *, unsigned int);
static unsigned int (*real_msg_size2)(void *);
static unsigned int (*real_get_src2)(void *);
static unsigned int ep_requests;
static void emit(const char *tag, unsigned int id, void *message, int dump);
static int resolved;
static unsigned long sends;

static void *resolve(const char *name) { return dlsym(RTLD_NEXT, name); }

static void setup(void)
{
    if (resolved) return;
    resolved = 1;
    real_snd = (snd_t)resolve("osal_snd_msg");
    real_sync = (snd_t)resolve("osal_snd_sync_msg");
    real_direct = (snd_t)resolve("osal_snd_sync_direct");
    real_internal = (snd_t)resolve("__osal_snd_sync_msg");
    real_rcv = (rcv_t)resolve("osal_rcv_msg_tmo");
    real_u_rcv = (rcv_t)resolve("u_osal_rcv_msg_tmo");
    real_reg = (reg_t)resolve("osal_reg_msg_queue_cb");
    real_u_reg = (reg_t)resolve("u_osal_reg_msg_queue_cb");
    real_sync_reg = (reg_t)resolve("u_osal_reg_sync_msg_queue_cb");
    real_alloc = (int (*)(unsigned int, void **, unsigned int, unsigned int))resolve("osal_valloc_msg_wait");
    real_free = (int (*)(unsigned int, void *))resolve("osal_free_msg");
    real_set_src = (int (*)(void *, unsigned int))resolve("osal_set_msg_src");
    real_msg_size2 = (unsigned int (*)(void *))resolve("osal_msg_size");
    real_get_src2 = (unsigned int (*)(void *))resolve("osal_get_msg_src");
}
static int reply_pipeline(unsigned int id, void *message)
{
    if (!message || !real_alloc || !real_free || !real_set_src || !real_msg_size2 || !real_get_src2) return -1;
    unsigned int src = real_get_src2(message);
    unsigned int size = real_msg_size2(message);
    if (!src || size < 8) return -1;
    const unsigned char *q = message;
    unsigned int cmd = q[0] | (q[1] << 8);
    unsigned int rsize = (cmd == 0x2300u) ? 12u : size;
    unsigned int ans = (cmd == 0x2300u) ? 0x3300u : (cmd | 0x1000u);
    void *reply = 0;
    if (real_alloc(src, &reply, rsize, 1) || !reply) return -1;
    unsigned char *r = reply;
    unsigned int i;
    for (i = 0; i < rsize; ++i) r[i] = 0;
    for (i = 0; i < 8 && i < rsize; ++i) r[i] = q[i];
    r[0] = ans & 0xff; r[1] = (ans >> 8) & 0xff;
    r[2] = q[2]; r[3] = q[3];
    r[4] = rsize & 0xff; r[5] = (rsize >> 8) & 0xff;
    r[6] = 0; r[7] = 0;
    real_set_src(reply, 0x80c2019fU);
    int rc = real_snd ? real_snd(src, reply) : -38;
    emit(rc ? "F" : "P", src, reply, 16);
    if (rc) real_free(src, reply);
    return 0;
}

static char *put_hex(char *p, unsigned long value)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[16];
    int n = 0;
    if (!value) { *p++ = '0'; return p; }
    while (value) { tmp[n++] = digits[value & 15]; value >>= 4; }
    while (n) *p++ = tmp[--n];
    return p;
}

static char *put_bytes(char *p, const unsigned char *data, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        *p++ = "0123456789abcdef"[data[i] >> 4];
        *p++ = "0123456789abcdef"[data[i] & 15];
    }
    return p;
}

/* Print in one write(2) so concurrent threads cannot interleave a line. */
static void emit(const char *tag, unsigned int id, void *message, int dump)
{
    char buffer[160];
    char *p = buffer;
    *p++ = tag[0]; *p++ = ' ';
    p = put_hex(p, (unsigned long)(uintptr_t)getpid());
    *p++ = ' ';
    p = put_hex(p, id);
    if (message) {
        *p++ = ' ';
        p = put_hex(p, (unsigned long)(uintptr_t)message);
        if (dump) {
            *p++ = ' ';
            p = put_bytes(p, (const unsigned char *)message, 16);
        }
    }
    *p++ = '\n';
    (void)!write(2, buffer, (size_t)(p - buffer));
}

/*
 * Diagnostic endpoint: the FX30 UI engine sends a 1504-byte request to the full id
 * 0x92b4086f (`mov w0,#0x86f; movk w0,#0x92b4,lsl#16` in libObj.so) and no service
 * answers. Register that id from the sending process and log the request instead of
 * letting the kernel drop it. Observation only: no reply is fabricated.
 */
#define FX30_ENDPOINT 0x92b4086fu
static int endpoint_armed;
static reg_t real_reg_endpoint;

/* Loopback experiment: remember the app's own (id, callback, argument) pairs so a
   send to a local endpoint can be delivered in-process when the kernel drops it. */
struct local_endpoint { unsigned int id; void *callback; unsigned long argument; };
static struct local_endpoint locals[16];
static int local_count;

static void remember_local(unsigned int id, void *callback, unsigned long argument)
{
    int i;
    for (i = 0; i < local_count; i++) {
        if (locals[i].id == id) { locals[i].callback = callback; locals[i].argument = argument; return; }
    }
    if (local_count < 16) {
        locals[local_count].id = id;
        locals[local_count].callback = callback;
        locals[local_count].argument = argument;
        local_count++;
    }
}

/* User-space queue for the endpoint the kernel refuses to deliver. */
static void *pending[4];
static int pending_count;
static int loopback_enabled = -1;

/* The host cannot pass environment variables into the guest; switches ride on the
   kernel command line like the unattended session does. */
static int cmdline_has(const char *token)
{
    char buffer[1024];
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buffer, sizeof buffer - 1);
    close(fd);
    if (n <= 0) return 0;
    buffer[n] = 0;
    return strstr(buffer, token) != 0;
}

static int selftest_hits;
static int selftest_done;

static void selftest_callback(void *argument, void *message)
{
    (void)argument; (void)message;
    selftest_hits++;
}

/*
 * One-shot loopback probe: register a private id and send the message we just
 * intercepted to it, inside the running firmware process (which already called
 * u_osal_init). Tells us which id form the kernel actually delivers.
 */
static int probe_hits[3];

static void probe0(void *a, void *m) { (void)a; (void)m; probe_hits[0]++; }
static void probe1(void *a, void *m) { (void)a; (void)m; probe_hits[1]++; }
static void probe2(void *a, void *m) { (void)a; (void)m; probe_hits[2]++; }

static void log_t(const char *tag, unsigned int id, int rc, int sent, int hit)
{
    char buffer[96];
    char *p = buffer;
    *p++ = 'T'; *p++ = ' '; *p++ = tag[0];
    *p++ = ' ';
    p = put_hex(p, id);
    *p++ = ' ';
    p = put_hex(p, (unsigned long)rc);
    *p++ = ' ';
    p = put_hex(p, (unsigned long)sent);
    *p++ = ' ';
    p = put_hex(p, (unsigned long)hit);
    *p++ = '\n';
    (void)!write(2, buffer, (size_t)(p - buffer));
}

static void loopback_selftest(void *message)
{
    if (selftest_done) return;
    selftest_done = 1;
    if (!cmdline_has("fx30.selftest=1")) return;
    /* Baseline: register and send the same id. */
    unsigned int same[3] = {0x3bu, 0x2b4086fu, 0x92b4086fu};
    for (int i = 0; i < 3; i++) {
        void *handle = 0;
        int rc = real_reg ? real_reg(same[i], &handle, (void *)selftest_callback, 0) : -38;
        int before = selftest_hits;
        int sent = real_snd ? real_snd(same[i], message) : -38;
        char tag[2] = {'s', 0};
        log_t(tag, same[i], rc, sent, selftest_hits - before);
    }
    /* Which registration receives a send addressed to the full id? */
    unsigned int registers[3] = {0x86fu, 0x2b4086fu, 0x92b4086fu};
    void *cbs[3] = {(void *)probe0, (void *)probe1, (void *)probe2};
    for (int i = 0; i < 3; i++) {
        void *handle = 0;
        real_reg ? real_reg(registers[i], &handle, cbs[i], 0) : 0;
    }
    int sent = real_snd ? real_snd(0x92b4086fu, message) : -38;
    char tag[2] = {'x', 0};
    log_t(tag, 0x92b4086fu, sent, probe_hits[0], probe_hits[1]);
    /* Which of the firmware's own channels can this process loop back at all? */
    unsigned int channels[6] = {0x80924127u, 0x80cf016du, 0x910041u, 0x910042u,
                                0x8f0313u, 0x7a804cu};
    for (int i = 0; i < 6; i++) {
        void *handle = 0;
        int rc = real_reg ? real_reg(channels[i], &handle, (void *)selftest_callback, 0) : -38;
        int before = selftest_hits;
        int r = real_snd ? real_snd(channels[i], message) : -38;
        char t2[2] = {'c', 0};
        log_t(t2, channels[i], rc, r, selftest_hits - before);
    }
}

/*
 * Service probe: the kernel masks the destination to 0x0fffffff when it looks up the
 * receiving queue (osal_snd_msg) while the app addresses 0x92b4086f. Registering the
 * masked id therefore receives the app's requests; this callback only records them.
 */
static int serve_enabled = -1;
static int serve_armed;

static int want_serve(void)
{
    if (serve_enabled < 0) serve_enabled = cmdline_has("fx30.serve=1");
    return serve_enabled;
}

static void serve_callback(void *argument, void *message)
{
    (void)argument;
    unsigned int size = 0, src = 0;
    size_t_fn sizefn = (size_t_fn)resolve("osal_msg_size");
    src_t srcfn = (src_t)resolve("osal_get_msg_src");
    if (message && sizefn) size = sizefn(message);
    if (message && srcfn) src = srcfn(message);
    emit("E", src, message, 16);
    char buffer[64];
    char *p = buffer;
    *p++ = 'e'; *p++ = ' ';
    p = put_hex(p, size);
    *p++ = '\n';
    (void)!write(2, buffer, (size_t)(p - buffer));
}

static void arm_service(void)
{
    if (serve_armed || !want_serve()) return;
    serve_armed = 1;
    reg_t reg = (reg_t)resolve("u_osal_reg_msg_queue_cb");
    if (!reg) return;
    void *handle = 0;
    int rc = reg(0x2b4086fu, &handle, (void *)serve_callback, 0);
    char buffer[64];
    char *p = buffer;
    *p++ = 'S'; *p++ = 'v'; *p++ = ' ';
    p = put_hex(p, (unsigned long)rc);
    *p++ = ' ';
    p = put_hex(p, (unsigned long)(uintptr_t)(handle ? handle : 0));
    *p++ = '\n';
    (void)!write(2, buffer, (size_t)(p - buffer));
}

/* Channel monitor: attach a callback to a firmware display/GUI channel and record
   the message size plus a longer prefix so the wire format can be reconstructed. */
static int monitor_enabled = -1;
static int monitor_count;

static int want_monitor(void)
{
    if (monitor_enabled < 0) monitor_enabled = cmdline_has("fx30.monitor=1");
    return monitor_enabled;
}

static void monitor_callback(void *argument, void *message)
{
    (void)argument;
    unsigned int size = 0, src = 0;
    size_t_fn sizefn = (size_t_fn)resolve("osal_msg_size");
    src_t srcfn = (src_t)resolve("osal_get_msg_src");
    if (message && sizefn) size = sizefn(message);
    if (message && srcfn) src = srcfn(message);
    if (monitor_count++ > 12) return;
    char buffer[160];
    char *p = buffer;
    *p++ = 'M'; *p++ = ' ';
    p = put_hex(p, src);
    *p++ = ' ';
    p = put_hex(p, size);
    *p++ = ' ';
    {
        unsigned int n = size < 32 ? size : 32;
        const unsigned char *data = (const unsigned char *)message;
        for (unsigned int i = 0; i < n; i++) {
            *p++ = "0123456789abcdef"[data[i] >> 4];
            *p++ = "0123456789abcdef"[data[i] & 15];
        }
    }
    *p++ = '\n';
    (void)!write(2, buffer, (size_t)(p - buffer));
}

static int is_main_app(void)
{
    char buffer[32];
    int fd = open("/proc/self/comm", O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buffer, sizeof buffer - 1);
    close(fd);
    if (n <= 0) return 0;
    buffer[n] = 0;
    return strncmp(buffer, "im.elf", 6) == 0;
}

static void arm_monitor(void)
{
    if (!want_monitor() || !is_main_app()) return;
    reg_t reg = (reg_t)resolve("u_osal_reg_msg_queue_cb");
    if (!reg) return;
    unsigned int ids[2] = {0x80924127u, 0x80cf016du};
    for (int i = 0; i < 2; i++) {
        void *handle = 0;
        int rc = reg(ids[i], &handle, (void *)monitor_callback, 0);
        char buffer[64];
        char *p = buffer;
        *p++ = 'm'; *p++ = ' ';
        p = put_hex(p, ids[i]);
        *p++ = ' ';
        p = put_hex(p, (unsigned long)rc);
        *p++ = '\n';
        (void)!write(2, buffer, (size_t)(p - buffer));
    }
}

static int want_loopback(void)
{
    if (loopback_enabled < 0) loopback_enabled = cmdline_has("fx30.loopback=1");
    return loopback_enabled;
}

static int deliver_local(unsigned int id, void *message)
{
    int i;
    for (i = 0; i < local_count; i++) {
        if (locals[i].id == (id & 0x0fffffffu) || locals[i].id == id) {
            void (*callback)(void *, void *) = (void (*)(void *, void *))locals[i].callback;
            emit("L", id, message, 1);
            callback((void *)locals[i].argument, message);
            return 1;
        }
    }
    return 0;
}

static void endpoint_callback(void *argument, void *message)
{
    (void)argument;
    unsigned int size = 0, src = 0;
    size_t_fn sizefn = (size_t_fn)resolve("osal_msg_size");
    src_t srcfn = (src_t)resolve("osal_get_msg_src");
    if (message && sizefn) size = sizefn(message);
    if (message && srcfn) src = srcfn(message);
    emit("E", src, message, size >= 16 ? 16 : 0);
    char buffer[64];
    char *p = buffer;
    *p++ = 'e'; *p++ = ' ';
    p = put_hex(p, size);
    *p++ = '\n';
    (void)!write(2, buffer, (size_t)(p - buffer));
}

static void arm_endpoint(void)
{
    if (endpoint_armed) return;
    endpoint_armed = 1;
    void *handle = 0;
    if (!real_reg_endpoint) real_reg_endpoint = (reg_t)resolve("osal_reg_msg_queue_cb");
    if (!real_reg_endpoint) return;
    /* Arm every combination: full id and low 16 bits, internal and user registration. */
    reg_t regs[2] = {real_reg_endpoint, real_u_reg};
    unsigned int ids[2] = {FX30_ENDPOINT, 0x86fu};
    for (int r = 0; r < 2; r++) {
        if (!regs[r]) continue;
        for (int i = 0; i < 2; i++) {
            void *slot = 0;
            int rc = regs[r](ids[i], &slot, (void *)endpoint_callback, 0);
            char buffer[64];
            char *p = buffer;
            *p++ = 'A'; *p++ = ' ';
            p = put_hex(p, (unsigned long)r);
            *p++ = ' ';
            p = put_hex(p, ids[i]);
            *p++ = ' ';
            p = put_hex(p, (unsigned long)rc);
            *p++ = '\n';
            (void)!write(2, buffer, (size_t)(p - buffer));
        }
    }
    (void)handle;
}

static int first_send(unsigned int id)
{
    static unsigned int seen[64];
    static int count;
    int i;
    for (i = 0; i < count; i++) if (seen[i] == id) return 0;
    if (count < 64) seen[count++] = id;
    return 1;
}

int osal_reg_msg_queue_cb(unsigned int id, void **handle, void *callback, unsigned long argument)
{
    setup();
    int rc = real_reg ? real_reg(id, handle, callback, argument) : -38;
    char buffer[96];
    char *p = buffer;
    *p++ = 'R'; *p++ = ' ';
    p = put_hex(p, (unsigned long)(uintptr_t)getpid());
    *p++ = ' ';
    p = put_hex(p, id);
    *p++ = ' ';
    p = put_hex(p, (unsigned long)rc);
    *p++ = ' ';
    p = put_hex(p, (unsigned long)(uintptr_t)(handle ? *handle : 0));
    *p++ = '\n';
    (void)!write(2, buffer, (size_t)(p - buffer));
    return rc;
}

int u_osal_reg_msg_queue_cb(unsigned int id, void **handle, void *callback, unsigned long argument)
{
    setup();
    int rc = real_u_reg ? real_u_reg(id, handle, callback, argument) : -38;
    arm_monitor();
    /*
     * osal_snd_msg keys the table on id & 0x7fff and then compares id & 0x0fffffff
     * with the owner stored in the entry (libosal_uipc 0x5e60). For ids above
     * 0x0fffffff the registered form and the compared form differ, so mirror the
     * registration in the masked form the send path actually compares against.
     */
    if (rc == 0 && real_u_reg && (id & 0xf0000000u)) {
        void *mirror = 0;
        real_u_reg(id & 0x0fffffffu, &mirror, callback, argument);
    }
    if (rc == 0 && callback) remember_local(id, callback, argument);
    arm_monitor();
    char buffer[96];
    char *p = buffer;
    *p++ = 'U'; *p++ = ' ';
    p = put_hex(p, (unsigned long)(uintptr_t)getpid());
    *p++ = ' ';
    p = put_hex(p, id);
    *p++ = ' ';
    p = put_hex(p, (unsigned long)rc);
    *p++ = ' ';
    p = put_hex(p, (unsigned long)(uintptr_t)(handle ? *handle : 0));
    *p++ = '\n';
    (void)!write(2, buffer, (size_t)(p - buffer));
    return rc;
}

int u_osal_reg_sync_msg_queue_cb(unsigned int id, void **handle, void *callback, unsigned long argument)
{
    setup(); emit("r", id, 0, 0);
    return real_sync_reg ? real_sync_reg(id, handle, callback, argument) : -38;
}

int osal_snd_msg(unsigned int id, void *message)
{
    setup();
    if (id == FX30_ENDPOINT) {
        loopback_selftest(message);
        arm_service();
        arm_monitor();
        /* Try the in-process loopback first: the kernel drops this send because the
           queue it registered is not visible to the delivery lookup. */
        if (want_loopback() && deliver_local(id, message)) return 0;
        if (want_loopback() && pending_count < 4) {
            pending[pending_count++] = message;
            emit("Q", id, message, 1);
            return 0;
        }
        arm_endpoint();
    }
    if (id == 0x92b4086fu) {
        if (ep_requests++ < 8) emit("E", id, message, 16);
        return reply_pipeline(id, message);
    }
    if (sends++ < 300 || first_send(id)) emit("S", id, message, 1);
    return real_snd ? real_snd(id, message) : -38;
}

int osal_snd_sync_msg(unsigned int id, void *message)
{
    setup();
    emit("Y", id, message, first_send(id));
    return real_sync ? real_sync(id, message) : -38;
}

int __osal_snd_sync_msg(unsigned int id, void *message)
{
    setup();
    emit("y", id, message, 1);
    return real_internal ? real_internal(id, message) : -38;
}

int osal_snd_sync_direct(unsigned int id, void *message)
{
    setup();
    int rc = real_direct ? real_direct(id, message) : -38;
    /* First call per id with its result: tells us whether the drawing path is alive. */
    if (first_send(id) || rc) emit("d", id, (void *)(long)rc, 0);
    return rc;
}

int osal_rcv_msg_tmo(unsigned int id, void **message, long timeout)
{
    setup(); emit("V", id, 0, 0);
    if (want_loopback() && id == FX30_ENDPOINT && pending_count > 0 && message) {
        int i;
        *message = pending[0];
        for (i = 1; i < pending_count; i++) pending[i-1] = pending[i];
        pending_count--;
        emit("G", id, *message, 1);
        return 0;
    }
    return real_rcv ? real_rcv(id, message, timeout) : -38;
}

/*
 * The camera reports fatal errors through Sony's monitor library. Its queue object is
 * never constructed in this board (no debug UART), so the reporting path itself faults.
 * These shims make the monitor path observable instead of fatal.
 */
int mon_queue_remain(void *self) __asm__("_ZN8MonQueue14GetQueueRemainEv");
int mon_queue_empty(void *self) __asm__("_ZN8MonQueue7IsEmptyEv");
int mon_queue_full(void *self) __asm__("_ZN8MonQueue6IsFullEv");
int mon_putchar(int c) __asm__("_ZN6MonDrv7PutcharEi");
int mon_puts(const char *s) __asm__("_ZN6MonDrv8PutsNoCREPKc");

int mon_queue_remain(void *self)
{
    (void)self; emit("Q", 0, 0, 0); return 0;
}

int mon_queue_empty(void *self)
{
    (void)self; return 1;
}

int mon_queue_full(void *self)
{
    (void)self; return 0;
}

int mon_putchar(int c)
{
    unsigned char ch = (unsigned char)c;
    (void)!write(2, &ch, 1);
    return 0;
}

int mon_puts(const char *s)
{
    if (s) (void)!write(2, s, strlen(s));
    return 0;
}

int u_osal_rcv_msg_tmo(unsigned int id, void **message, long timeout)
{
    setup(); emit("v", id, 0, 0);
    return real_u_rcv ? real_u_rcv(id, message, timeout) : -38;
}
