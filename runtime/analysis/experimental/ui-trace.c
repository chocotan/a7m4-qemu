/* Trace original UI messages without calling UI functions on another thread. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include "hdmi-config.h"
#include "display-service.h"
#include "release-inhibit.h"
#include "timecode-service.h"

static int fd = -1;
static __thread unsigned receives, upm_logs;
static unsigned events, remote_sends, sync_logs;
static uintptr_t app_base;
static int (*receive_next)(unsigned, void **, long);
static int (*send_next)(unsigned, void *);
static int (*sync_next)(unsigned, void *);
static unsigned (*size_next)(void *);
static unsigned (*source_next)(void *);
static int (*allocate_next)(unsigned, void **, unsigned, unsigned);
static int (*free_next)(unsigned, void *);
static int (*set_source_next)(void *, unsigned);
static unsigned hdmi_observer;
extern int a7m4_hdmi_register(unsigned *, const void *, unsigned, void *);
extern int a7m4_updater_close(int, const void *, unsigned, void *);
extern int a7m4_capid_send(unsigned, void *);
extern int a7m4_dmm_send(void *);
extern int sa_address_reply(const unsigned char *, unsigned char *);
extern int a7m4_pin_dispatch(unsigned, void *);
extern int a7m4_pin_handles(unsigned);
extern int a7m4_audio_dispatch(unsigned, void *, int *);
extern void a7m4_resource_lz_arm(void);
extern int a7m4_pin_release(unsigned, void *, int (*)(unsigned, void *), int *);
static unsigned updater_touched;
static struct a7m4_hdmi_config hdmi_config;
static struct a7m4_display_state display_state;
static struct a7m4_release_state release_state;
static struct a7m4_timecode_state timecode_state;
static pthread_mutex_t hdmi_mutex = PTHREAD_MUTEX_INITIALIZER;
static void (*create_next)(void *, void *, void *, void *, void *, void *, void *, void *);

static void record(const char *tag, unsigned id, void *message, long result, void *caller)
{
    char text[1800], comm[32] = {0};
    Dl_info info = {0};
    unsigned size = message && size_next ? size_next(message) : 0;
    unsigned source = message && source_next ? source_next(message) : 0;
    unsigned words[8] = {0};
    int f = open("/proc/thread-self/comm", O_RDONLY);
    if (f >= 0) {
        ssize_t n = read(f, comm, sizeof comm - 1);
        close(f);
        if (n > 0 && comm[n - 1] == '\n') comm[n - 1] = 0;
    }
    if (message && size) memcpy(words, message, size < sizeof words ? size : sizeof words);
    dladdr(caller, &info);
    int n = snprintf(text, sizeof text,
        "UI_TRACE %s tid=%ld comm=%s id=%#x size=%u src=%#x rc=%ld caller=%s+%#lx words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
        tag, syscall(SYS_gettid), comm, id, size, source, result,
        info.dli_fname ? info.dli_fname : "?",
        info.dli_fbase ? (unsigned long)caller - (unsigned long)info.dli_fbase : (unsigned long)caller,
        words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
    if (fd < 0) fd = open("/dev/console", O_WRONLY);
    if (n > 0 && n < (int)sizeof text && fd >= 0) (void)!write(fd, text, n);
}

static void setup(void)
{
    if (!receive_next) receive_next = dlsym(RTLD_NEXT, "osal_rcv_msg_tmo");
    if (!send_next) send_next = dlsym(RTLD_NEXT, "osal_snd_msg");
    if (!sync_next) sync_next = dlsym(RTLD_NEXT, "osal_snd_sync_msg");
    if (!size_next) size_next = dlsym(RTLD_NEXT, "osal_msg_size");
    if (!source_next) source_next = dlsym(RTLD_NEXT, "osal_get_msg_src");
    if (!allocate_next) allocate_next = dlsym(RTLD_NEXT, "osal_valloc_msg_wait");
    if (!free_next) free_next = dlsym(RTLD_NEXT, "osal_free_msg");
    if (!set_source_next) set_source_next = dlsym(RTLD_NEXT, "osal_set_msg_src");
}

static void observe_create(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h)
{
    record("CREATE_SCREEN_ENTER", (unsigned)(uintptr_t)b, NULL, (long)(uintptr_t)c, __builtin_return_address(0));
    create_next(a, b, c, d, e, f, g, h);
    record("CREATE_SCREEN_RETURN", (unsigned)(uintptr_t)b, NULL, 0, __builtin_return_address(0));
}

static void arm(void)
{
    Dl_info info = {0};
    void *symbol = dlsym(RTLD_DEFAULT, "Application_System_Init");
    if (!symbol || !dladdr(symbol, &info) || !info.dli_fbase) return;
    uintptr_t base = (uintptr_t)info.dli_fbase;
    if (base == app_base) return;
    uintptr_t *slot = (uintptr_t *)(base + 0x584e0a8UL);
    /* Refuse an unknown image/slot rather than overwrite an unrelated pointer. */
    if (*slot != base + 0x186a51cUL) {
        record("SLOT_MISMATCH", 0, NULL, *slot - base, symbol);
        return;
    }
    if (mprotect((void *)((uintptr_t)slot & ~4095UL), 4096, PROT_READ | PROT_WRITE)) {
        record("SLOT_MPROTECT_FAIL", 0, NULL, -1, symbol);
        return;
    }
    create_next = (void *)*slot;
    *slot = (uintptr_t)observe_create;
    app_base = base;
    record("SCREEN_ARMED", 0, NULL, *slot, symbol);
    char text[400];
    int n = snprintf(text, sizeof text, "UI_TRACE CONFIG base=%#lx config=%.255s\n", (unsigned long)base, (char *)(base + 0x5db99a8UL));
    if (n > 0 && n < (int)sizeof text && fd >= 0) (void)!write(fd, text, n);
}

int osal_rcv_msg_tmo(unsigned id, void **message, long timeout)
{
    setup();
    void *caller = __builtin_return_address(0);
    Dl_info info = {0};
    int app_caller = dladdr(caller, &info) && info.dli_fname && strstr(info.dli_fname, "appFw.so");
    if (app_caller) {
        if (getenv("A7M4_DISPLAY_MODEL")) a7m4_resource_lz_arm();
        arm();
    }
    unsigned n = receives++;
    int frame_queue=id==0x8288076cu || id==0x80820884u || id==0x8082088bu;
    if (!n || ((id == 0x5503e0u || app_caller || frame_queue) && n < 100)) record("RECV_WAIT", id, NULL, timeout, caller);
    int rc = receive_next ? receive_next(id, message, timeout) : -38;
    if (!rc && (!n || ((id == 0x5503e0u || app_caller || frame_queue) && n < 100))) record("RECV_GOT", id, message ? *message : NULL, rc, caller);
    return rc;
}

int osal_snd_msg(unsigned id, void *message)
{
    setup();
    if (id==0x8288076cu || id==0x80820884u || id==0x8082088bu)
        record("FRAME_LOCAL_SEND",id,message,0,__builtin_return_address(0));
    int audio_rc;
    if (getenv("A7M4_DISPLAY_MODEL") && a7m4_audio_dispatch(id,message,&audio_rc)) return audio_rc;
    if (getenv("A7M4_HDMI_OBSERVER") && message && size_next && size_next(message) >= 8 &&
        (((id == 0x92840747u) && ((const unsigned short *)message)[0] == 0x200d &&
          ((const unsigned short *)message)[1] == 0x32d) ||
         ((id == 0x909a015au) && ((const unsigned short *)message)[0] == 0x2500 &&
          ((const unsigned short *)message)[1] == 0x102))) {
        if (!hdmi_observer) return -1020;
        if (!source_next || !allocate_next || !free_next || !set_source_next || !send_next) return -1021;
        unsigned source = source_next(message);
        if (!source || (source & 0x70000000u)) return -1024;
        unsigned char wire[12];
        pthread_mutex_lock(&hdmi_mutex);
        struct a7m4_hdmi_config staged = hdmi_config;
        int rc = a7m4_hdmi_config_reply(&staged, message, size_next(message), wire);
        if (rc) { pthread_mutex_unlock(&hdmi_mutex); return rc; }
        void *reply = NULL;
        rc = allocate_next(source, &reply, sizeof wire, 1);
        if (rc || !reply) { pthread_mutex_unlock(&hdmi_mutex); return rc ? rc : -1021; }
        memcpy(reply, wire, sizeof wire);
        set_source_next(reply, id);
        record("HDMI_CONFIG_REPLY", id, reply, 0, __builtin_return_address(0));
        rc = send_next(source, reply);
        if (!rc) hdmi_config = staged;
        pthread_mutex_unlock(&hdmi_mutex);
        if (rc) { free_next(source, reply); return rc; }
        record("HDMI_CONFIG_STORED", id, message, 0, __builtin_return_address(0));
        free_next(id, message);
        return 0;
    }
    if (id == 0x909a015au) {
        arm();
        record("HDMI_REQUEST", id, message, 0, __builtin_return_address(0));
        if (getenv("A7M4_HDMI_OBSERVER") && size_next && source_next &&
            allocate_next && set_source_next && free_next && send_next) {
            unsigned char wire[12];
            unsigned staged_observer = hdmi_observer;
            unsigned size = message ? size_next(message) : 0;
            int rc = a7m4_hdmi_register(&staged_observer, message, size, wire);
            unsigned source = message ? source_next(message) : 0;
            if (!rc && (source >> 28) == 8) {
                void *reply = NULL;
                rc = allocate_next(source, &reply, sizeof wire, 1);
                if (rc || !reply) return rc ? rc : -1021;
                memcpy(reply, wire, sizeof wire);
                set_source_next(reply, id);
                rc = send_next(source, reply);
                record("HDMI_REGISTER_COMPLETE", source, rc ? reply : NULL, rc, __builtin_return_address(0));
                if (rc) { free_next(source, reply); return rc; }
                hdmi_observer = staged_observer;
                free_next(id, message);
                return 0;
            }
        }
    }
    if (id == 0x912503d0u && message && size_next) {
        unsigned size = size_next(message);
        unsigned char wire[8];
        record("UPDATER_REQUEST", id, message, 0, __builtin_return_address(0));
        if (size == 20 && *(unsigned *)message == 0 && getenv("A7M4_UPDATER_CLOSE")) {
            record("LENS_VERSION_UNAVAILABLE", id, message, -1020, __builtin_return_address(0));
            return -1020;
        }
        if (size < 4 || *(unsigned *)message != 4)
            __atomic_store_n(&updater_touched, 1, __ATOMIC_RELEASE);
        if (getenv("A7M4_UPDATER_CLOSE") && source_next && allocate_next &&
            set_source_next && free_next && send_next &&
            !a7m4_updater_close(__atomic_load_n(&updater_touched, __ATOMIC_ACQUIRE), message, size, wire)) {
            unsigned source = source_next(message);
            if (!source || (source & 0x70000000u)) return -1024;
            void *reply = NULL;
            int rc = allocate_next(source, &reply, sizeof wire, 1);
            if (rc || !reply) return rc ? rc : -1021;
            memcpy(reply, wire, sizeof wire);
            set_source_next(reply, id);
            rc = send_next(source, reply);
            record("UPDATER_EMPTY_CLOSED", source, rc ? reply : NULL, rc, __builtin_return_address(0));
            if (rc) { free_next(source, reply); return rc; }
            free_next(id, message);
            return 0;
        }
    }
    if (id == 0x5503e0u || (message && size_next && size_next(message) >= 4 &&
        (*(unsigned *)message == 0x920143u || *(unsigned *)message == 0x920145u))) {
        if (__atomic_fetch_add(&events, 1, __ATOMIC_RELAXED) < 150)
            record("SEND", id, message, 0, __builtin_return_address(0));
    }
    if (getenv("A7M4_DISPLAY_MODEL") && message && size_next && size_next(message) >= 8 &&
        ((id == 0x92840747u && (((const unsigned short *)message)[1] == 0x169 ||
                                  ((const unsigned short *)message)[1] == 0x13c ||
                                  ((const unsigned short *)message)[1] == 0x118 ||
                                  ((const unsigned short *)message)[1] == 0x119 ||
                                  ((const unsigned short *)message)[1] == 0x11a ||
                                  ((const unsigned short *)message)[1] == 0x158 ||
                                  ((const unsigned short *)message)[1] == 0x14b ||
                                  ((const unsigned short *)message)[1] == 0x146 ||
                                  ((const unsigned short *)message)[1] == 0x160 ||
                                  ((const unsigned short *)message)[1] == 0x141 ||
                                  ((const unsigned short *)message)[1] == 0x159 ||
                                  ((const unsigned short *)message)[1] == 0x15f ||
                                  ((const unsigned short *)message)[1] == 0x11b ||
                                  ((const unsigned short *)message)[1] == 0x144 ||
                                  ((const unsigned short *)message)[1] == 0x164 ||
                                  ((const unsigned short *)message)[1] == 0x14f)) ||
         (id == 0x909c015bu && ((const unsigned short *)message)[1] == 0x101) ||
         (id == 0x909a015au && (((const unsigned short *)message)[1] == 0x109 ||
                                  ((const unsigned short *)message)[1] == 0x10a ||
                                  ((const unsigned short *)message)[1] == 0x101 ||
                                  ((const unsigned short *)message)[1] == 0x117)))) {
        if (!allocate_next || !free_next || !source_next || !set_source_next || !send_next) return -1021;
        unsigned source = source_next(message), size = 0;
        if (!source || (source & 0x70000000u)) return -1024;
        unsigned char wire[18];
        if (((const unsigned short *)message)[1] == 0x146) {
            const unsigned char *raw = message;
            unsigned bytes = size_next(message);
            for (unsigned offset = 0; offset < bytes && offset < 584; offset += 32) {
                char line[180];
                int n = snprintf(line, sizeof line, "DISPLAY_WIRE offset=%u raw=", offset);
                for (unsigned i = offset; i < bytes && i < offset + 32; i++)
                    n += snprintf(line + n, sizeof line - n, "%02x", raw[i]);
                line[n++] = '\n';
                if (fd >= 0) (void)!write(fd, line, n);
            }
        }
        pthread_mutex_lock(&hdmi_mutex);
        struct a7m4_display_state staged = display_state;
        int rc = a7m4_display_reply(&staged, message, size_next(message), wire, &size);
        if (rc) { pthread_mutex_unlock(&hdmi_mutex); return rc; }
        void *reply = NULL;
        if (size) {
            rc = allocate_next(source, &reply, size, 1);
            if (rc || !reply) { pthread_mutex_unlock(&hdmi_mutex); return rc ? rc : -1021; }
            memcpy(reply, wire, size);
            set_source_next(reply, id);
            record("DISPLAY_REPLY", id, reply, 0, __builtin_return_address(0));
            rc = send_next(source, reply);
        }
        if (!rc) display_state = staged;
        pthread_mutex_unlock(&hdmi_mutex);
        if (rc) { if (reply) free_next(source, reply); return rc; }
        record("DISPLAY_STORED", id, message, 0, __builtin_return_address(0));
        free_next(id, message);
        return 0;
    }
    if (id == 0x00930101u && message && size_next) {
        record("SA_ADDRESS_REQUEST", id, message, 0, __builtin_return_address(0));
        unsigned size = size_next(message);
        const unsigned char *raw = message;
        if (getenv("A7M4_SA_ADDRESS") && size >= 80 && raw[0] == 0x50 && raw[1] == 0xb8 &&
            allocate_next && free_next && set_source_next && send_next) {
            unsigned source = ((const unsigned *)message)[2];
            if (((const unsigned *)message)[3] || !source || (source & 0x70000000u)) return -1024;
            void *reply = NULL;
            int rc = allocate_next(source, &reply, 80, 1);
            if (rc || !reply) return rc ? rc : -1021;
            rc = sa_address_reply(raw, reply);
            if (rc) { free_next(source, reply); return rc; }
            record("SA_ADDRESS_RESULT", id, reply, 0, __builtin_return_address(0));
            set_source_next(reply, id);
            rc = send_next(source, reply);
            if (rc) { free_next(source, reply); return rc; }
            free_next(id, message);
            return 0;
        }
    }
    if (getenv("A7M4_DISPLAY_MODEL") && id == 0x90280964u) {
        /* libInfraOptionInstall+17c60 branches to its own Backup fallback when
         * this absent CP time service reports failure; retain caller ownership. */
        record("CP_TIME_UNAVAILABLE", id, message, -1020, __builtin_return_address(0));
        return -1020;
    }
    if (getenv("A7M4_DISPLAY_MODEL") && id == 0x90040193u && message && size_next &&
        size_next(message) >= 4 && ((const unsigned short *)message)[1] == 0x403d &&
        allocate_next && free_next && source_next && set_source_next && send_next) {
        unsigned source = source_next(message);
        if (source != 0x80020002u) return -1024;
        unsigned char wire[12];
        pthread_mutex_lock(&hdmi_mutex);
        struct a7m4_release_state staged = release_state;
        int rc = a7m4_release_reply(&staged, message, size_next(message), wire);
        record("RELEASE_REQUEST", id, message, rc, __builtin_return_address(0));
        if (rc) { pthread_mutex_unlock(&hdmi_mutex); return rc; }
        void *reply = NULL;
        rc = allocate_next(source, &reply, sizeof wire, 1);
        if (rc || !reply) { pthread_mutex_unlock(&hdmi_mutex); return rc ? rc : -1021; }
        memcpy(reply, wire, sizeof wire); set_source_next(reply, id);
        record("RELEASE_REPLY", id, reply, 0, __builtin_return_address(0));
        rc = send_next(source, reply);
        if (!rc) release_state = staged;
        pthread_mutex_unlock(&hdmi_mutex);
        if (rc) { free_next(source, reply); return rc; }
        free_next(id, message);
        return 0;
    }
    if (getenv("A7M4_DISPLAY_MODEL") && id == 0x9059000eu && message && size_next &&
        size_next(message) >= 28 && ((const unsigned short *)message)[13] == 0x205 &&
        source_next && allocate_next && free_next && set_source_next && send_next) {
        unsigned source = source_next(message);
        if (!source || (source & 0x70000000u)) return -1024;
        unsigned char wire[36];
        pthread_mutex_lock(&hdmi_mutex);
        struct a7m4_timecode_state staged = timecode_state;
        int rc = a7m4_timecode_reply(&staged, message, size_next(message), wire);
        record("TIMECODE_REQUEST", id, message, rc, __builtin_return_address(0));
        if (rc) { pthread_mutex_unlock(&hdmi_mutex); return rc; }
        void *reply = NULL;
        rc = allocate_next(source, &reply, sizeof wire, 1);
        if (rc || !reply) { pthread_mutex_unlock(&hdmi_mutex); return rc ? rc : -1021; }
        memcpy(reply, wire, sizeof wire); set_source_next(reply, id);
        record("TIMECODE_REPLY", id, reply, 0, __builtin_return_address(0));
        rc = send_next(source, reply);
        if (!rc) timecode_state = staged;
        pthread_mutex_unlock(&hdmi_mutex);
        if (rc) { free_next(source, reply); return rc; }
        free_next(id, message);
        return 0;
    }
    if (message && size_next && size_next(message) == 48 &&
        ((const unsigned *)message)[1] == 0xa0a0a0a0u &&
        ((const unsigned short *)message)[12] == 0x1000 && ((const unsigned short *)message)[13] == 0) {
        const unsigned *key = (const unsigned *)message + 8;
        record("PIN_GET_KEYS", id, message, 0, __builtin_return_address(0));
        char detail[220];int n=snprintf(detail,sizeof detail,"PIN_QUERY destination=%#x type=%u dir=%u index=%u owner=%#x\n",id,key[0],key[1],key[2],key[3]);
        if(fd>=0 && n>0 && n<(int)sizeof detail)(void)!write(fd,detail,n);
    }
    if (getenv("A7M4_DISPLAY_MODEL") && a7m4_pin_handles(id) && message && size_next &&
        size_next(message) >= 28 && (((const unsigned short *)message)[13] == 0 ||
                                   ((const unsigned short *)message)[13] == 1))
        return a7m4_pin_dispatch(id, message);
    if (id == 0x9284074au && message && size_next && size_next(message) == 48) {
        const unsigned *words = message;
        char text[240];
        int n=snprintf(text,sizeof text,"PIN_GET_REQUEST keys=%u,%u,%u,%#x header=%08x,%08x,%08x,%08x\n",
                       words[8],words[9],words[10],words[11],words[6],words[7],words[2],words[3]);
        if(fd>=0 && n>0)(void)!write(fd,text,n);
    }
    if (getenv("A7M4_CAPID_STORAGE") && (id == 0x9004015eu || id == 0x92850750u))
        return a7m4_capid_send(id, message);
    if (getenv("A7M4_DMM_STORAGE") && id == 0x9061003cu)
        return a7m4_dmm_send(message);
    if ((id & 0xf0000000u) == 0x90000000u && id != 0x92b4086fu && id != 0x92a208fcu &&
        __atomic_fetch_add(&remote_sends, 1, __ATOMIC_RELAXED) < 80)
        record("REMOTE_SEND", id, message, 0, __builtin_return_address(0));
    return send_next ? send_next(id, message) : -38;
}

int osal_free_msg(unsigned destination, void *message)
{
    setup();
    if (!free_next) return -38;
    int rc;
    if (a7m4_pin_release(destination, message, free_next, &rc)) return rc;
    return free_next(destination, message);
}

int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                           const struct timespec *deadline)
{
    static int (*next)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
    static __thread int selected, inspected;
    if (!next) next = dlsym(RTLD_NEXT, "pthread_cond_timedwait");
    if (!selected) {
        char comm[32] = {0};
        int f = open("/proc/thread-self/comm", O_RDONLY);
        if (f >= 0) { (void)!read(f, comm, sizeof comm - 1); close(f); }
        selected = strstr(comm, "IMD-RM") ? 2 : 1;
    }
    if (selected == 2 && inspected < 12) {
        struct timeval now;
        gettimeofday(&now, NULL);
        char text[240];
        int n = snprintf(text, sizeof text, "MEDIA_TIMED_WAIT begin=%ld.%06ld deadline=%ld.%09ld\n",
                         now.tv_sec, now.tv_usec, deadline->tv_sec, deadline->tv_nsec);
        if (fd >= 0 && n > 0) (void)!write(fd, text, n);
    }
    int rc = next ? next(condition, mutex, deadline) : -38;
    if (selected == 2 && inspected++ < 12) record("MEDIA_TIMED_RETURN", 0, NULL, rc, __builtin_return_address(0));
    return rc;
}

int osal_snd_sync_msg(unsigned id, void *message)
{
    setup();
    if (id != 0x80924127u && message && size_next && size_next(message) >= 4 &&
        __atomic_fetch_add(&sync_logs, 1, __ATOMIC_RELAXED) < 180)
        record("SYNC_REQUEST", id, message, 0, __builtin_return_address(0));
    int rc = sync_next ? sync_next(id, message) : -38;
    if (id == 0x80924127u && (rc || upm_logs++ < 3))
        record("UPM_BOOT_INFO", id, message, rc, __builtin_return_address(0));
    if (id == 0x004b024eu)
        record("ORIGINAL_VERSION", id, message, rc, __builtin_return_address(0));
    return rc;
}
