#ifndef SONY_QEMU_DMM_SERVICE_H
#define SONY_QEMU_DMM_SERVICE_H
#define DMM_TEST_PHYSICAL 0x41c000000ULL
#define DMM_TEST_SIZE 0x160000U
struct dmm_slot { unsigned int handle, open_count, generation, unit; int allocated; };
struct dmm_service {
    unsigned char *memory;
    unsigned long long physical;
    struct dmm_slot slots[8];
};
int dmm_service_reply(struct dmm_service *, const unsigned char *, unsigned int, unsigned char *);
int dmm_service_copy_sound(struct dmm_service *,unsigned int,unsigned int,const unsigned char *,unsigned int);
int dmm_service_sound_slice(struct dmm_service *,unsigned long long,unsigned int,const unsigned char **);
#endif
