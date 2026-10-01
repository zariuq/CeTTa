#define _GNU_SOURCE
#include "run_guard.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

enum { DECLARED_MAGIC=0x43455144u, FINAL_MAGIC=0x43455146u };
typedef struct { uint32_t magic, status; uint64_t declared; } Receipt;
static int receipt_fd=-1;
static bool declared_sent=false;
static uint64_t declared_queries=0;

void cetta_run_guard_declare(uint64_t queries) {
    declared_queries=queries;
    if (receipt_fd<0 || declared_sent) return;
    Receipt r={DECLARED_MAGIC,0,queries};
    ssize_t n;
    do n=write(receipt_fd,&r,sizeof(r)); while (n<0 && errno==EINTR);
    declared_sent=n==(ssize_t)sizeof(r);
}
static void missing_report(const char *path,bool known,uint64_t declared,int status) {
    if (!path) return;
    FILE *f=fopen(path,"w");
    if (!f) return;
    fprintf(f,"{\"version\":1,\"dialect_exit\":%d,\"contract_exit\":1,"
        "\"document_framing_ok\":false,\"query_catalogue_available\":%s,"
        "\"report_publication_acknowledged\":false,\"queries_declared\":",
        status,known ? "true" : "false");
    if (known) fprintf(f,"%"PRIu64,declared); else fputs("null",f);
    fputs(",\"queries_unsettled\":",f);
    if (known) fprintf(f,"%"PRIu64,declared); else fputs("null",f);
    fputs(",\"queries\":[",f);
    for (uint64_t i=0;known && i<declared;i++)
        fprintf(f,"%s{\"id\":%"PRIu64",\"status\":1,\"observation_complete\":false,"
            "\"tests_passed\":false,\"finalization_ready\":false,\"framing_ok\":false,"
            "\"answers\":0,\"tests\":0,\"faults\":0,\"details_shown\":0,\"details_omitted\":0}",i ? "," : "",i+1u);
    fputs("],\"diagnostics\":[],\"reason\":\"process terminated before finalization\"}\n",f);
    (void)fclose(f);
}
int cetta_run_guard(int argc,char **argv,int (*entry)(int,char **)) {
    bool guarded=false;
    const char *path=NULL;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--")) break;
        if (!strcmp(argv[i],"--run-contract")) guarded=true;
        else if (!strcmp(argv[i],"--run-report")) {
            guarded=true;
            if (i+1<argc) path=argv[++i];
        }
    }
    if (!guarded) return entry(argc,argv);
    int channel[2];
    if (pipe2(channel,O_CLOEXEC)) return 1;
    pid_t child=fork();
    if (child<0) { close(channel[0]);close(channel[1]);return 1; }
    if (!child) {
        close(channel[0]); receipt_fd=channel[1];
        declared_sent=false; declared_queries=0;
        int result=entry(argc,argv);
        Receipt r={FINAL_MAGIC,(uint32_t)result,declared_queries};
        ssize_t n;
        do n=write(receipt_fd,&r,sizeof(r)); while (n<0 && errno==EINTR);
        close(receipt_fd);receipt_fd=-1;
        exit(n==(ssize_t)sizeof(r) ? result : 1);
    }
    close(channel[1]);
    int status=0;
    pid_t waited;
    do waited=waitpid(child,&status,0); while (waited<0 && errno==EINTR);
    /* At most two fixed records. Nonblocking read prevents a forked descendant
     * retaining the channel from keeping the controller alive after its owner. */
    (void)fcntl(channel[0],F_SETFL,O_NONBLOCK);
    unsigned char bytes[2*sizeof(Receipt)+1];
    ssize_t n;
    do n=read(channel[0],bytes,sizeof(bytes)); while (n<0 && errno==EINTR);
    close(channel[0]);
    bool known=false;
    uint64_t declared=0;
    Receipt final={0};
    if (n>=(ssize_t)sizeof(Receipt)) {
        memcpy(&final,bytes,sizeof(final));
        if (final.magic==DECLARED_MAGIC && final.status==0) {
            known=true;declared=final.declared;
            if (n==(ssize_t)(2*sizeof(Receipt))) memcpy(&final,bytes+sizeof(Receipt),sizeof(final));
        }
    }
    bool valid=(n==(ssize_t)sizeof(Receipt) || (known && n==(ssize_t)(2*sizeof(Receipt)))) &&
        final.magic==FINAL_MAGIC && final.status<=255u &&
        (!known || final.declared==declared) && waited==child &&
        WIFEXITED(status) && WEXITSTATUS(status)==(int)final.status;
    if (valid) return (int)final.status;
    fputs("error: process terminated without a final run receipt\n",stderr);
    missing_report(path,known,declared,waited==child && WIFEXITED(status) ? WEXITSTATUS(status) : 1);
    return 1;
}
