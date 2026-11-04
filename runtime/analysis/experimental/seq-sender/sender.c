/* Functional capture of the missing remote sender. Logs AP->CP sends and
 * returns the same class of failure the kernel used when no sender was
 * registered, so this load does not pretend a CP replied. */
typedef unsigned int u32;
typedef unsigned char u8;
extern int printk(const char *, ...);
extern long osal_reg_public(long);

static u32 send_count;
static long (*sender_handle)(u32, void *);

static long seq_sender(u32 qid, void *msg)
{
    u32 size = msg ? ((u32 *)msg)[-2] : 0;
    const u8 *b = msg;
    u32 w0 = 0, w1 = 0, w2 = 0;
    u32 n = send_count++;
    if (n < 24 && b) {
        if (size >= 4) w0 = b[0] | (b[1] << 8) | (b[2] << 16) | ((u32)b[3] << 24);
        if (size >= 8) w1 = b[4] | (b[5] << 8) | (b[6] << 16) | ((u32)b[7] << 24);
        if (size >= 12) w2 = b[8] | (b[9] << 8) | (b[10] << 16) | ((u32)b[11] << 24);
        printk("\0016SEQ_SEND n=%u qid=%#x size=%u b=%08x,%08x,%08x\n", n, qid, size, w0, w1, w2);
    }
    return (long)0xfffffc04; /* -1020, same UDIF class as an unanswered send */
}

int __attribute__((section(".init.text"))) init_module(void)
{
    long rc;
    sender_handle = seq_sender;
    rc = osal_reg_public((long)&sender_handle);
    printk("\0016SEQ_SENDER_REGISTER rc=%ld\n", rc);
    return rc == 0 ? 0 : (int)rc;
}
