#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int should_fail(int fd) {
    static unsigned calls;
    static int hit;
    const char *point=getenv("CETTA_APPEND_FAULT_AT");
    if(point && !hit) {
        char descriptor[64],path[4096];
        snprintf(descriptor,sizeof(descriptor),"/proc/self/fd/%d",fd);
        ssize_t n=readlink(descriptor,path,sizeof(path)-1);
        if(n>0) {
            path[n]=0;
            if(strstr(path,"/.batch-") && ++calls==strtoul(point,NULL,10)) {
                hit=1;const char *marker=getenv("CETTA_APPEND_FAULT_MARKER");
                if(marker){int out=open(marker,O_WRONLY|O_CREAT|O_EXCL,0600);if(out>=0)close(out);}
                errno=EIO;return 1;
            }
        }
    }
    return 0;
}
ssize_t pwrite(int fd,const void *data,size_t size,off_t offset) {
    static ssize_t (*next)(int,const void *,size_t,off_t);
    if(!next)next=dlsym(RTLD_NEXT,"pwrite");
    return should_fail(fd) ? -1 : next(fd,data,size,offset);
}
ssize_t write(int fd,const void *data,size_t size) {
    static ssize_t (*next)(int,const void *,size_t);
    if(!next)next=dlsym(RTLD_NEXT,"write");
    return should_fail(fd) ? -1 : next(fd,data,size);
}
