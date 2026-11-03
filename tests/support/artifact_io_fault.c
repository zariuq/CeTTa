#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static atomic_ullong writes;
ssize_t pwrite(int fd,const void *bytes,size_t size,off_t offset) {
  static ssize_t (*real_write)(int,const void *,size_t,off_t);
  if (!real_write) *(void **)(&real_write)=dlsym(RTLD_NEXT,"pwrite");
  const char *arm=getenv("CETTA_ARTIFACT_FAULT_ARM"),*prefix=getenv("CETTA_ARTIFACT_FAULT_DIRECTORY");
  const char *point=getenv("CETTA_ARTIFACT_FAULT_AT"),*marker=getenv("CETTA_ARTIFACT_FAULT_MARKER");
  const char *kind=getenv("CETTA_ARTIFACT_FAULT_KIND");
  if (size && arm && prefix && point && marker && access(arm,F_OK)==0) {
    struct stat st;char link[64],path[4096];
    snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);
    ssize_t n=readlink(link,path,sizeof(path)-1);
    if (n>0 && !fstat(fd,&st) && S_ISREG(st.st_mode)) {
      path[n]=0;
      bool selected=kind && !strcmp(kind,"base") ? strstr(path,"/base-") && strstr(path,".cbase") : st.st_nlink==0;
      if (selected && !strncmp(path,prefix,strlen(prefix)) && atomic_fetch_add(&writes,1)+1==strtoull(point,NULL,10)) {
        int log=open(marker,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
        if (log>=0) {const char text[]="artifact pwrite fault\n";(void)write(log,text,sizeof(text)-1);close(log);}
        errno=EIO;return -1;
      }
    }
  }
  return real_write(fd,bytes,size,offset);
}
