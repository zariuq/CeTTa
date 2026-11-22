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
    if (!strcmp(mode,"raw-unknown")) _exit(0);
    if (!strcmp(mode,"cpu-unknown")) raise(SIGXCPU);
    cetta_run_guard_declare(!strcmp(mode,"large-catalogue") ? UINT64_MAX : 2u);
    if (!strcmp(mode,"complete")) return 0;
    if (!strcmp(mode,"fault")) return 2;
    if (!strcmp(mode,"oversized-status")) return 256;
    if (!strcmp(mode,"raw-zero")) _exit(0);
    if (!strcmp(mode,"raw-fault")) _exit(2);
    if (!strcmp(mode,"raw-cpu-status")) _exit(128+SIGXCPU);
    if (!strcmp(mode,"raw-kill-status")) _exit(128+SIGKILL);
    if (!strcmp(mode,"large-catalogue")) _exit(0);
    if (!strcmp(mode,"signal")) raise(SIGTERM);
    if (!strcmp(mode,"cpu")) raise(SIGXCPU);
    if (!strcmp(mode,"kill")) raise(SIGKILL);
    if (!strcmp(mode,"catalogue-changed")) cetta_run_guard_declare(3);
    if (!strcmp(mode,"closed-channel"))
        for (int fd=3;fd<128;fd++) close(fd);
    return 0;
}
static int guarded(int argc, char **argv) {
    int parent_status = 1;
    if (!cetta_run_guard_launch(argc==4 ? argv[3] : NULL, &parent_status)) return parent_status;
    exit(cetta_run_guard_finish(entry(argc, argv)));
}
int main(int argc, char **argv) {
    if (argc==4 && !strcmp(argv[1],"--probe")) return guarded(argc,argv);
    const char *modes[]={"empty","complete","fault","oversized-status","raw-zero","raw-fault","signal","catalogue-changed","closed-channel"};
    const int statuses[]={0,0,2,1,1,1,128+SIGTERM,1,1};
    for (size_t i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
        char *args[]={"test","--run-contract",(char *)modes[i],NULL};
        assert(guarded(3,args)==statuses[i]);
    }
    char *ordinary[]={"test","ordinary","complete",NULL};
    assert(entry(3,ordinary)==0);
    return 0;
}
