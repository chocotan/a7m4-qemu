#include <assert.h>
#define SETUP_SEED_TEST
#include "seed-setup-complete.c"
static unsigned char values[3];
static int scenario,writes;
static unsigned slot(unsigned id){for(unsigned i=0;i<3;i++)if(id==ids[i])return i;assert(0);return 0;}
int Backup_get_datasize(unsigned id){return scenario==1 && slot(id)==1?4:1;}
int Backup_nread(unsigned id,void *p,int bytes){assert(bytes==1);*(unsigned char *)p=values[slot(id)];return scenario==2 && !writes?0:1;}
int Backup_write(unsigned unit,unsigned id,const void *p)
{
    assert(unit==id>>16);writes++;
    if((scenario==4 && writes==2) || (scenario==6 && writes==3))return -55;
    if(scenario==5 && writes==2)return 0;
    values[slot(id)]=*(const unsigned char *)p;return 1;
}
static void reset(int mode){scenario=mode;writes=0;values[0]=values[1]=1;values[2]=0;}
int main(void)
{
    reset(0);assert(!seed_setup_complete() && !values[0] && !values[1] && values[2]==1 && writes==3);
    writes=0;assert(!seed_setup_complete() && writes==3 && values[2]==1);
    for(int mode=1;mode<=3;mode++){
        reset(mode);if(mode==3)values[1]=2;
        assert(seed_setup_complete()==3 && !writes && values[0]==1);
    }
    for(int mode=4;mode<=6;mode++){
        reset(mode);assert(seed_setup_complete()==4 && values[0]==1 && values[1]==1 && values[2]==0);
        assert(writes==(mode==6?6:5));
    }
    puts("A7M4_SETUP_SEED_READBACK_AND_ROLLBACK_SELFTEST_PASS");return 0;
}
