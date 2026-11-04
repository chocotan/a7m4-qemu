/* Functional CP storage only. No image producer or capture completion. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "capid-service.h"

#define TABLE_ADDRESS 0x41c200000ULL
#define TABLE_BYTES 0x5000UL
static struct cap_service service;
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
    const char token[] = "memrsv=0x00005000@0x41c200000:uc";
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
    void *table = mmap(NULL, TABLE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, TABLE_ADDRESS);
    close(fd);
    if (table == MAP_FAILED) return;
    memset(table, 0, TABLE_BYTES);
    service.table = table;
    service.address = TABLE_ADDRESS;
}

int a7m4_capid_send(unsigned destination, void *request)
{
    pthread_once(&once, initialize);
    if (!service.table || !allocate_msg || !free_msg || !send_msg ||
        !set_source || !get_source || !message_size) return -1021;
    if (!request) return -1024;
    unsigned input_size = message_size(request);
    if (input_size < (destination == 0x9004015eu ? 24u : 16u)) return -1024;
    const unsigned *words = request;
    unsigned operation = destination == 0x9004015eu ? words[1] : words[2];
    unsigned source = get_source(request);
    if (!source || (source & 0x70000000u)) return -1024;
    if (destination == 0x9004015eu &&
        (input_size != 24 || words[0] || words[2] || words[3] || words[4] || words[5])) return -1024;
    unsigned size = destination == 0x9004015eu ?
        (operation == 0x1000 ? 16 : operation == 0x1003 ? 0 : 8) : 24;
    void *reply = NULL;
    int rc = size ? allocate_msg(source, &reply, size, 1) : 0;
    if (rc || (size && !reply)) return rc ? rc : -1021;
    pthread_mutex_lock(&mutex);
    struct cap_service previous = service;
    struct cap_table previous_table = *service.table;
    unsigned char unused[16];
    long owner = syscall(SYS_gettid);
    if (destination == 0x9004015eu)
        rc = cap_control(&service, operation, owner, reply ? reply : unused, &size);
    else
        rc = cap_sync(&service, request, input_size, reply);
    if (!rc && size) {
        set_source(reply, destination);
        rc = send_msg(source, reply);
    }
    if (rc) { *service.table = previous_table; service = previous; }
    char text[320];
    int n = snprintf(text, sizeof text,
        "CAPID_STORAGE tid=%ld dst=%#x op=%#x src=%#x rc=%d address=%#llx header=%u,%u,%u,%u,%u initialized=%d\n",
        owner, destination, operation, source, rc, service.address,
        service.table->header[0], service.table->header[1], service.table->header[2],
        service.table->header[3], service.table->header[4], service.initialized);
    if (n > 0 && n < (int)sizeof text && console >= 0) (void)!write(console, text, n);
    pthread_mutex_unlock(&mutex);
    if (rc) { if (reply) free_msg(source, reply); return rc; }
    free_msg(destination, request);
    return 0;
}
