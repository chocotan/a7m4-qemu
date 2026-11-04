/* A7M4 appFw 364c098 reads 02cf0014 into model1bf3; 364c0ac writes
 * the mirrored bytes below. Original setup completion clears model1bf3.
 * 23ae1a8 sets tutorial model2483=1; original Backup catalog3085968
 * owns 02cf1701 (IS_DISPLAYED_ICON_TOUCH_FUNCTION_GUIDE_SCREEN).
 * Guest Backup_save=0 only; never edit the source Backup.bin or firmware. */
#include <stdio.h>
#include <string.h>
extern int Backup_get_datasize(unsigned int);
extern int Backup_nread(unsigned int,void *,int);
extern int Backup_write(unsigned int,unsigned int,const void *);
static const unsigned ids[3]={0x02cf0014,0x01070140,0x02cf1701};
static const unsigned char completed[3]={0,0,1};
static int read_byte(unsigned id,unsigned char *value)
{
    return Backup_get_datasize(id)==1 && Backup_nread(id,value,1)==1?0:-1;
}
int seed_setup_complete(void)
{
    unsigned char original[3],verify;
    for(unsigned i=0;i<3;i++){
        if(read_byte(ids[i],&original[i]) || original[i]>1){
            printf("SETUP_SEED_REJECT id=%#x reason=size-read-or-enum\n",ids[i]);return 3;
        }
        printf("SETUP_SEED_ORIGINAL id=%#x bytes=1 value=%u\n",ids[i],original[i]);
    }
    for(unsigned i=0;i<3;i++){
        int rc=Backup_write(ids[i]>>16,ids[i],&completed[i]);
        int bad=read_byte(ids[i],&verify);
        if(rc<0 || bad || verify!=completed[i]){
            printf("SETUP_SEED_FAILED id=%#x write_rc=%d readback_rc=%d\n",ids[i],rc,bad);
            int rollback_bad=0;
            for(unsigned j=0;j<3;j++){
                int restore=Backup_write(ids[j]>>16,ids[j],&original[j]);
                int mismatch=read_byte(ids[j],&verify) || verify!=original[j];
                printf("SETUP_SEED_ROLLBACK id=%#x write_rc=%d matches=%u\n",ids[j],restore,!mismatch);
                rollback_bad|=restore<0 || mismatch;
            }
            return rollback_bad?5:4;
        }
        printf("SETUP_SEED_READBACK id=%#x bytes=1 value=%u\n",ids[i],verify);
    }
    puts("SETUP_COMPLETED_READBACK_OK fields=02cf0014:0,01070140:0,02cf1701:1 source=original-completion-state");
    return 0;
}
#ifndef SETUP_SEED_TEST
extern int __libc_start_main(int (*)(int,char **),int,char **,void (*)(void),void (*)(void),void (*)(void),void *);
__asm__(".global _start\n.type _start, %function\n_start:\nmov x0, sp\nbl entry\nbrk #0\n");
static int run(int argc,char **argv){(void)argv;return argc==1?seed_setup_complete():2;}
__attribute__((noreturn)) void entry(unsigned long *stack)
{
    __libc_start_main(run,(int)stack[0],(char **)(stack+1),0,0,0,stack);
    __builtin_trap();
}
#endif
