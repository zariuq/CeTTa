/* Integration fixture: create ordinary pending tasks through the native owner.
 * The channel has already initialized the journal and must be stopped. */
#include "durable_worker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
    if (argc!=4) return 2;
    char *end=NULL;unsigned long count=strtoul(argv[3],&end,10);
    if (!end || *end || !count) return 2;
    CettaDurableStore *store=NULL;
    if (cetta_durable_open(argv[1],NULL,&store)!=DURABLE_OK) return 1;
    const char *body="[\"input\",\"42.0\",\"message\",\"operator\",{}]";
    int result=0;
    for (unsigned long i=0;i<count;++i) {
        char id[65];snprintf(id,sizeof(id),"fixture-%lu",i);
        if (cetta_worker_publish(store,argv[2],id,body,strlen(body))!=DURABLE_OK) {result=1;break;}
    }
    cetta_durable_close(store);return result;
}
