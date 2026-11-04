/* Functional memory units only, backed by a dedicated reserved RAM range. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "dmm-service.h"

static struct dmm_service service;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int console = -1;
static int (*allocate_msg)(unsigned, void **, unsigned, unsigned);
static int (*free_msg)(unsigned, void *);
static int (*send_msg)(unsigned, void *);
static int (*set_source)(void *, unsigned);
static unsigned (*get_source)(void *);
static unsigned (*message_size)(void *);

static void initialize(void)
{
    allocate_msg = dlsym(RTLD_NEXT, "osal_valloc_msg_wait");
    free_msg = dlsym(RTLD_NEXT, "osal_free_msg");
    send_msg = dlsym(RTLD_NEXT, "osal_snd_msg");
    set_source = dlsym(RTLD_NEXT, "osal_set_msg_src");
    get_source = dlsym(RTLD_NEXT, "osal_get_msg_src");
    message_size = dlsym(RTLD_NEXT, "osal_msg_size");
    console = open("/dev/console", O_WRONLY);
    char line[4096] = {0};
    const char token[] = "memrsv=0x00160000@0x41c000000:uc";
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) return;
    ssize_t n = read(fd, line, sizeof line - 1);
    close(fd);
    if (n <= 0) return;
    char *at = strstr(line, token);
    if (!at || (at != line && at[-1] != ' ') ||
        (at[sizeof token - 1] && at[sizeof token - 1] != ' ' && at[sizeof token - 1] != '\n')) return;
    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) return;
    void *memory = mmap(NULL, DMM_TEST_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DMM_TEST_PHYSICAL);
    close(fd);
    if (memory == MAP_FAILED) return;
    memset(memory, 0, DMM_TEST_SIZE);
    service.memory = memory;
    service.physical = DMM_TEST_PHYSICAL;
}

int a7m4_dmm_send(void *request)
{
    pthread_once(&once, initialize);
    if (!service.memory || !allocate_msg || !free_msg || !send_msg ||
        !set_source || !get_source || !message_size) return -1021;
    if (!request || message_size(request) != 40) return -1024;
    unsigned source = get_source(request);
    if (!source || (source & 0x70000000u)) return -1024;
    void *reply = NULL;
    int rc = allocate_msg(source, &reply, 40, 1);
    if (rc || !reply) return rc ? rc : -1021;
    pthread_mutex_lock(&mutex);
    struct dmm_service previous = service;
    rc = dmm_service_reply(&service, request, 40, reply);
    unsigned handle = 0, bytes = 0;
    unsigned long long address = 0;
    int status = -1024;
    if (!rc) {
        handle = ((unsigned *)reply)[3];
        bytes = ((unsigned *)reply)[8];
        address = ((unsigned long long *)reply)[3];
        status = ((short *)reply)[1];
        set_source(reply, 0x9061003c);
        rc = send_msg(source, reply);
    }
    if (rc) service = previous;
    char text[400];
    int n = snprintf(text, sizeof text,
        "DMM_STORAGE src=%#x op=%#x unit=%#x option=%u transport=%d status=%d handle=%#x address=%#llx bytes=%#x allocated=%d,%d opens=%u,%u\n",
        source, ((const unsigned short *)request)[0], ((const unsigned *)request)[1],
        ((const unsigned short *)request)[11], rc, status, handle, address, bytes,
        service.slots[0].allocated, service.slots[1].allocated,
        service.slots[0].open_count, service.slots[1].open_count);
    if (n > 0 && n < (int)sizeof text && console >= 0) (void)!write(console, text, n);
    pthread_mutex_unlock(&mutex);
    if (rc) { free_msg(source, reply); return rc; }
    free_msg(0x9061003c, request);
    return 0;
}
