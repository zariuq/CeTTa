#define _POSIX_C_SOURCE 200809L
#include "run_guard.h"
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int entry(int argc,char **argv) {
    (void)argc;
    const char *mode=argv[2];
    if (!strcmp(mode,"empty")) return 0;
    cetta_run_guard_declare(2);
    if (!strcmp(mode,"complete")) return 0;
    if (!strcmp(mode,"fault")) return 2;
    if (!strcmp(mode,"oversized-status")) return 256;
    if (!strcmp(mode,"raw-zero")) _exit(0);
    if (!strcmp(mode,"raw-fault")) _exit(2);
    if (!strcmp(mode,"signal")) raise(SIGTERM);
    if (!strcmp(mode,"catalogue-changed")) cetta_run_guard_declare(3);
    if (!strcmp(mode,"closed-channel"))
        for (int fd=3;fd<128;fd++) close(fd);
    return 0;
}
int main(void) {
    const char *modes[]={"empty","complete","fault","oversized-status","raw-zero","raw-fault","signal","catalogue-changed","closed-channel"};
    const int statuses[]={0,0,2,1,1,1,1,1,1};
    for (size_t i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
        char *args[]={"test","--run-contract",(char *)modes[i],NULL};
        assert(cetta_run_guard(3,args,entry)==statuses[i]);
    }
    char *ordinary[]={"test","ordinary","complete",NULL};
    assert(cetta_run_guard(3,ordinary,entry)==0);
    return 0;
}
