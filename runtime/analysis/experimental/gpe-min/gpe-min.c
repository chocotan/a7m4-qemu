/* Minimal software GPE backend for the A7M4 port.
 * Only the verified libgpe worker slots are redirected; no appFw globals are read.
 * Pixels are written to the original physical bitmap addresses, not to ramfb. */
typedef unsigned long size_t;
typedef unsigned long pthread_t;
extern void *dlsym(void *, const char *);
extern void *dlopen(const char *, int);
extern int open(const char *, int, ...);
extern long write(int, const void *, size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern void *mmap(void *, size_t, int, int, int, long);
extern int munmap(void *, size_t);
extern int mprotect(void *, size_t, int);
extern int pthread_mutex_lock(void *);
extern int pthread_mutex_trylock(void *);
extern int pthread_mutex_unlock(void *);
extern int close(int);
#include "gpe-copy.h"
#include "gpe-sprite.h"
int gpe_uniform_fill(unsigned char *, size_t, const unsigned int *);
int gpe_copy_rows(unsigned char *, unsigned long, const unsigned char *, unsigned long,
                  const struct gpe_copy_plan *);
int gpe_plan_copy(const unsigned int *, struct gpe_copy_plan *);
int gpe_plan_sprite(const unsigned int *, struct gpe_sprite_plan *);
int gpe_draw_sprite(unsigned char *, unsigned long, const unsigned char *, unsigned long,
                    const struct gpe_sprite_plan *);
int gpe_fill_rgba4444(unsigned char *, unsigned long, const unsigned int *);

static void *(*real_dlopen)(const char *, int);
static int (*real_draw_initialize)(int);
static int (*real_gperm_initialize)(const void *);
static int (*real_worker_fill)(int, int, unsigned int, unsigned int, const void *);
static int (*real_worker_blit)(int, int, unsigned int, unsigned int, const void *);
static int (*real_worker_flush)(int, int, unsigned int, unsigned int, const void *);
static int (*real_worker_frames)(int, int, unsigned int, unsigned int, const void *);
static int (*real_draw_complete)(unsigned int, const void *, size_t);
static int raster_fd = -1;
static int logfd = -1;
static pthread_t raster_mutex[6];
int a7m4_raster_trylock(void){return pthread_mutex_trylock(raster_mutex);}
int a7m4_raster_unlock(void){return pthread_mutex_unlock(raster_mutex);}

static void logf(const char *tag, const char *text)
{
    char buf[300]; int n = snprintf(buf, sizeof buf, "GPE_MIN %s %s\n", tag, text ? text : "");
    if (n > 0 && n < (int)sizeof buf && logfd >= 0) (void)!write(logfd, buf, (size_t)n);
}
static unsigned rejected_blits;
static int reject_blit(const void *parameters,const char *reason)
{
    if(__atomic_fetch_add(&rejected_blits,1,__ATOMIC_RELAXED)<16){
        const unsigned int *p=parameters;
        logf("BLIT_REJECT",reason);
        for(unsigned i=0;i<32;i+=8){
            char text[220];snprintf(text,sizeof text,"word=%u raw=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                i,p[i],p[i+1],p[i+2],p[i+3],p[i+4],p[i+5],p[i+6],p[i+7]);
            logf("BLIT_WIRE",text);
        }
    }
    return 5;
}
static int software_draw_initialize(int gpe)
{
    if (gpe != 0) return 5;
    if (raster_fd < 0) raster_fd = open("/dev/mem", 2);
    logf("INIT", raster_fd >= 0 ? "physical bitmap access ready" : "bitmap access unavailable");
    return raster_fd >= 0 ? 0 : 5;
}
static int software_gperm_initialize(const void *object)
{
    return object ? software_draw_initialize(((const int *)object)[2]) : 5;
}
static int raster_completion(unsigned int sequence, unsigned int source, const unsigned char *callback_info)
{
    void (*callback)(const void *) = *(void (**)(const void *))callback_info;
    struct { unsigned int sequence, reserved; unsigned long argument; } info =
        {sequence, 0, *(const unsigned long *)(callback_info + 8)};
    if (callback) callback(&info);
    if (source) { int status = 0; if (!real_draw_complete || !real_draw_complete(source, &status, sizeof status)) return 5; }
    return 0;
}
static int software_fill_worker(int gpe, int core, unsigned int sequence, unsigned int source, const void *parameters)
{
    if (!parameters || gpe != 0 || core != 0 || raster_fd < 0) return 5;
    const unsigned int *p = parameters;
    unsigned long long address = *(const unsigned long long *)parameters;
    size_t bytes = ((size_t)p[3] + p[5]) * p[6] * (p[7] ? 4 : 2);
    if (address < 0x410000000ULL || address >= 0x500000000ULL || !bytes || bytes > 0x4000000 ||
        bytes > 0x500000000ULL - address) return 5;
    unsigned long base = address & ~4095ULL;
    size_t offset = address - base, length = (offset + bytes + 4095) & ~4095UL;
    pthread_mutex_lock(raster_mutex);
    void *mapped = mmap(0, length, 3, 1, raster_fd, (long)base);
    if (mapped == (void *)-1) { pthread_mutex_unlock(raster_mutex); logf("FILL_MAP_FAIL", "mmap"); return 5; }
    int result = gpe_uniform_fill((unsigned char *)mapped + offset, bytes, p);
    if (result) result = gpe_fill_rgba4444((unsigned char *)mapped + offset, bytes, p);
    if (munmap(mapped, length)) result = 5;
    pthread_mutex_unlock(raster_mutex);
    { char t[220]; snprintf(t, sizeof t, "addr=%#llx bytes=%lu x=%u y=%u w=%u h=%u stride=%u fmt=%u result=%d",
        address, (unsigned long)bytes, p[2], p[3], p[4], p[5], p[6], p[7], result); logf("FILL", t); }
    if (result) return result;
    return raster_completion(sequence, source, (const unsigned char *)parameters + 0x28);
}
static int software_blit_worker(int gpe, int core, unsigned int sequence, unsigned int source, const void *parameters)
{
    if (!parameters || gpe != 0 || core != 0 || raster_fd < 0) return 5;
    const unsigned char *raw = parameters;
    struct gpe_copy_plan plan;
    struct gpe_sprite_plan sprite;
    int use_sprite = 0;
    if (*(const unsigned long *)(raw + 0x80)) return reject_blit(parameters,"callback-not-supported");
    if (gpe_plan_copy(parameters, &plan)) {
        if (gpe_plan_sprite(parameters, &sprite)) return reject_blit(parameters,"copy-and-sprite-parameter-domain");
        plan = sprite.memory; use_sprite = 1;
    }
    if (!raw[0x7a] && !source) return reject_blit(parameters,"missing-completion-owner");
    if (!plan.rows) return raster_completion(sequence, source, raw + 0x80);
    unsigned long dst_base = plan.destination & ~4095ULL, src_base = plan.source & ~4095ULL;
    unsigned long dst_offset = plan.destination - dst_base, src_offset = plan.source - src_base;
    unsigned long dst_length = (dst_offset + plan.destination_span + 4095) & ~4095UL;
    unsigned long src_length = (src_offset + plan.source_span + 4095) & ~4095UL;
    pthread_mutex_lock(raster_mutex);
    void *dst = mmap(0, dst_length, 3, 1, raster_fd, (long)dst_base);
    if (dst == (void *)-1) { pthread_mutex_unlock(raster_mutex); logf("COPY_MAP_FAIL", "dst"); return 5; }
    void *src = mmap(0, src_length, 1, 1, raster_fd, (long)src_base);
    if (src == (void *)-1) { munmap(dst, dst_length); pthread_mutex_unlock(raster_mutex); logf("COPY_MAP_FAIL", "src"); return 5; }
    int result = use_sprite
        ? gpe_draw_sprite((unsigned char *)dst + dst_offset, plan.destination_span,
                          (const unsigned char *)src + src_offset, plan.source_span, &sprite)
        : gpe_copy_rows((unsigned char *)dst + dst_offset, plan.destination_span,
                        (const unsigned char *)src + src_offset, plan.source_span, &plan);
    if (munmap(src, src_length)) result = 5;
    if (munmap(dst, dst_length)) result = 5;
    pthread_mutex_unlock(raster_mutex);
    { char t[220]; snprintf(t, sizeof t, "src=%#llx dst=%#llx rows=%u line=%lu sprite=%d result=%d",
        plan.source, plan.destination, plan.rows, plan.line, use_sprite, result); logf("COPY", t); }
    if (result) return result;
    return raster_completion(sequence, source, raw + 0x80);
}
static int observe_frames_worker(int gpe,int core,unsigned int sequence,unsigned int source,const void *parameters)
{
    if(parameters){
        const unsigned int *p=parameters;
        for(unsigned i=0;i<16;i+=8){
            char text[220];snprintf(text,sizeof text,"word=%u source=%#x sequence=%u raw=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                i,source,sequence,p[i],p[i+1],p[i+2],p[i+3],p[i+4],p[i+5],p[i+6],p[i+7]);
            logf("FRAMES_ENTER",text);
        }
    }
    return real_worker_frames?real_worker_frames(gpe,core,sequence,source,parameters):5;
}
static int software_flush_worker(int gpe, int core, unsigned int sequence, unsigned int source, const void *parameters)
{
    if (!parameters || gpe != 0 || core != 0 || raster_fd < 0) return 5;
    logf("FLUSH", "prior raster writes finished");
    return raster_completion(sequence, source, (const unsigned char *)parameters + 8);
}
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void patch_gpe(void *handle)
{
    void *fill = dlsym(handle, "_ZN5mpgpe13GpermFillRectENS_3GpeENS_7IntCoreEPjRKNS_13FillRectParamE");
    if (!fill) { logf("PATCH_FAIL", "GpermFillRect"); return; }
    real_worker_fill = dlsym(handle, "_ZN5mpgpe8FillRectENS_3GpeENS_7IntCoreEjjRKNS_13FillRectParamE");
    real_worker_blit = dlsym(handle, "_ZN5mpgpe7BitBlitENS_3GpeENS_7IntCoreEjjRKNS_12BitBlitParamE");
    real_worker_flush = dlsym(handle, "_ZN5mpgpe5FlushENS_3GpeENS_7IntCoreEjjRKNS_10FlushParamE");
    real_worker_frames = dlsym(handle, "_ZN5mpgpe17DrawVariousFramesENS_3GpeENS_7IntCoreEjjRKNS_15DrawFramesParamE");
    real_draw_complete = dlsym(handle, "_ZN5mpgpe7message12SendDrawCompEjPKvm");
    real_draw_initialize = dlsym(handle, "_ZN5mpgpe14InitializeDrawENS_3GpeE");
    real_gperm_initialize = dlsym(handle, "_ZNK5mpgpe5Gperm4InitEv");
    unsigned long base = (unsigned long)fill - 0xa758UL;
    if (mprotect((void *)(base + 0x29000UL), 4096, 3) != 0) { logf("PATCH_FAIL", "GOT page"); return; }
    *(unsigned long *)(base + 0x29670UL) = (unsigned long)software_draw_initialize;
    *(unsigned long *)(base + 0x29680UL) = (unsigned long)software_fill_worker;
    *(unsigned long *)(base + 0x29160UL) = (unsigned long)software_blit_worker;
    /* A7M4 Gperm+0xa574 passes the same BitBlitParam ABI to this worker. */
    *(unsigned long *)(base + 0x29300UL) = (unsigned long)software_blit_worker;
    *(unsigned long *)(base + 0x29150UL) = (unsigned long)observe_frames_worker;
    *(unsigned long *)(base + 0x29540UL) = (unsigned long)software_gperm_initialize;
    *(unsigned long *)(base + 0x29110UL) = (unsigned long)software_flush_worker;
    logf("PATCH", "six GOT slots bound to software raster including StretchRotateBlit");
}
void *dlopen(const char *name, int flags)
{
    if (!real_dlopen) real_dlopen = (void *(*)(const char *, int))dlsym((void *)-1, "dlopen");
    int effective = flags & ~8;
    void *handle = real_dlopen ? real_dlopen(name, effective) : 0;
    if (handle && name && (same(name, "libgpe.so") || same(name, "/usr/lib/libgpe.so")))
        patch_gpe(handle);
    return handle;
}
__attribute__((constructor)) static void init(void)
{
    real_dlopen = (void *(*)(const char *, int))dlsym((void *)-1, "dlopen");
    logfd = open("/dev/console", 1);
    logf("LOAD", "minimal GPE backend");
}
