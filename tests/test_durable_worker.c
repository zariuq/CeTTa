#define _GNU_SOURCE
#include "durable_worker.h"
#include "durable_value.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static CettaDurableStore *store;
static CettaWorkerEndpoint *endpoint;
static char socket_path[108];
static int listener(void) {
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0); assert(fd>=0);
    struct sockaddr_un a={.sun_family=AF_UNIX}; strcpy(a.sun_path,socket_path);
    assert(bind(fd,(struct sockaddr *)&a,sizeof(a))==0 && listen(fd,32)==0); return fd;
}
static int client(void) {
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0); assert(fd>=0);
    struct sockaddr_un a={.sun_family=AF_UNIX}; strcpy(a.sun_path,socket_path);
    assert(connect(fd,(struct sockaddr *)&a,sizeof(a))==0); return fd;
}
static void pump(void) {
    size_t requests; assert(cetta_worker_endpoint_step(endpoint,1,&requests)==DURABLE_OK);
}
static void send_request(int fd, int code, const char *id, const void *body, size_t size) {
    size_t n=strlen(id); unsigned char *p=malloc(6+n+size); assert(p && n<256);
    memcpy(p,"CWP1",4); p[4]=(unsigned char)code; p[5]=(unsigned char)n; memcpy(p+6,id,n);
    if (size) memcpy(p+6+n,body,size);
    assert(send(fd,p,6+n+size,MSG_NOSIGNAL)==(ssize_t)(6+n+size)); free(p);
}
static void expect(int fd, int code, const char *id, const char *body) {
    unsigned char b[CETTA_WORKER_PACKET_MAX]; ssize_t n=-1;
    for (int i=0;i<1000;++i) {
        pump(); n=recv(fd,b,sizeof(b),MSG_DONTWAIT);
        if (n>=0) break;
        assert(errno==EAGAIN || errno==EWOULDBLOCK);
    }
    size_t ids=strlen(id), bytes=strlen(body);
    if (n!=(ssize_t)(6+ids+bytes) || n<6 || b[4]!=code)
        fprintf(stderr,"reply code=%d expected=%d bytes=%lld expected=%zu\n",n>=6?b[4]:-1,code,(long long)n,6+ids+bytes);
    assert(n==(ssize_t)(6+ids+bytes) && !memcmp(b,"CWP1",4) && b[4]==code && b[5]==ids &&
        !memcmp(b+6,id,ids) && !memcmp(b+6+ids,body,bytes));
}
static void rpc(int fd, int command, const char *id, const char *body, int code, const char *reply_id, const char *answer) {
    send_request(fd,command,id,body,strlen(body)); expect(fd,code,reply_id,answer);
}
static size_t count(const char *space) {
    CettaDurableSnapshot s; assert(cetta_durable_snapshot(store,space,&s)==DURABLE_OK);
    size_t n=s.count; cetta_durable_snapshot_free(&s); return n;
}
static void consume(const char *id) {
    char key[160]; snprintf(key,sizeof(key),"worker/brain/%s",id);
    CettaDurableScope q={DURABLE_KEY,"host.inbox",key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    CettaDurableOp op={DURABLE_REMOVE,"host.inbox",key,NULL,0}; int64_t revision;
    assert(cetta_durable_commit_observed(store,o,&op,1,&revision)==DURABLE_OK);
    cetta_durable_observation_free(o);
}
static void expect_submission(const char *key, const char *body) {
    char name[160], input[180]; snprintf(name,sizeof(name),"brain/%s",key);
    snprintf(input,sizeof(input),"submission/brain/%s",key);
    CettaDurableScope q[]={{DURABLE_KEY,"host.worker-submissions",name},{DURABLE_KEY,"host.inbox",input}};
    CettaDurableObservation *o=NULL; assert(cetta_durable_observe(store,q,2,&o)==DURABLE_OK);
    const CettaDurableSnapshot *ledger=cetta_durable_observation_view(o,0), *inbox=cetta_durable_observation_view(o,1);
    CettaDurableField fields[]={
        {.kind=DURABLE_FIELD_SYMBOL,.text={"host:worker-submission",22}},
        {.kind=DURABLE_FIELD_INT,.integer=1},
        {.kind=DURABLE_FIELD_TEXT,.text={"brain",5}},
        {.kind=DURABLE_FIELD_TEXT,.text={key,strlen(key)}},
        {.kind=DURABLE_FIELD_TEXT,.text={body,strlen(body)}}};
    CettaDurableField value={.kind=DURABLE_FIELD_EXPR,.expression={fields,5}};
    unsigned char *data=NULL; size_t size=0; assert(cetta_durable_fields_encode(&value,&data,&size)==DURABLE_OK);
    assert(ledger->count==1 && ledger->records[0].size==size && !memcmp(ledger->records[0].data,data,size));
    assert(inbox->count==1 && inbox->records[0].size==size && !memcmp(inbox->records[0].data,data,size));
    assert(ledger->records[0].revision==inbox->records[0].revision);
    free(data); cetta_durable_observation_free(o);
}
static void consume_submission(const char *key) {
    char input[180]; snprintf(input,sizeof(input),"submission/brain/%s",key);
    CettaDurableScope q={DURABLE_KEY,"host.inbox",input}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    CettaDurableOp op={DURABLE_REMOVE,"host.inbox",input,NULL,0}; int64_t revision;
    assert(cetta_durable_commit_observed(store,o,&op,1,&revision)==DURABLE_OK);
    cetta_durable_observation_free(o);
}
static void stop(void) {
    cetta_worker_endpoint_free(endpoint); endpoint=NULL; cetta_durable_close(store); store=NULL;
    assert(!unlink(socket_path));
}
static void check_receipt(void) {
    CettaDurableScope q[]={{DURABLE_KEY,"host.worker-tasks","brain/id-1"},
        {DURABLE_KEY,"host.worker-results","brain/id-1"},
        {DURABLE_KEY,"host.inbox","worker/brain/id-1"}};
    CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,q,3,&o)==DURABLE_OK);
    const CettaDurableSnapshot *task=cetta_durable_observation_view(o,0),
        *receipt=cetta_durable_observation_view(o,1), *input=cetta_durable_observation_view(o,2);
    assert(task->count==1 && receipt->count==1 && input->count==1);
    CettaDurableField fields[]={
        {.kind=DURABLE_FIELD_SYMBOL,.text={"host:worker-result",18}},
        {.kind=DURABLE_FIELD_INT,.integer=1},
        {.kind=DURABLE_FIELD_TEXT,.text={"brain",5}},
        {.kind=DURABLE_FIELD_TEXT,.text={"id-1",4}},
        {.kind=DURABLE_FIELD_INT,.integer=task->records[0].revision},
        {.kind=DURABLE_FIELD_TEXT,.text={"(println! data-not-code)",sizeof("(println! data-not-code)")-1}}};
    CettaDurableField value={.kind=DURABLE_FIELD_EXPR,.expression={fields,6}};
    unsigned char *data=NULL; size_t size=0;
    assert(cetta_durable_fields_encode(&value,&data,&size)==DURABLE_OK);
    assert(receipt->records[0].size==size && !memcmp(receipt->records[0].data,data,size));
    assert(input->records[0].size==size && !memcmp(input->records[0].data,data,size));
    assert(receipt->records[0].revision==input->records[0].revision);
    free(data); cetta_durable_observation_free(o);
}
int main(void) {
    /* No symbol table, arenas or evaluator initialization: arbitrary peer text
     * is stored as closed string data, never parsed as executable source. */
    char directory[]="/tmp/cetta-worker-XXXXXX"; assert(mkdtemp(directory));
    char db[256]; snprintf(db,sizeof(db),"%s/journal.db",directory);
    snprintf(socket_path,sizeof(socket_path),"%s/worker.sock",directory);
    assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    int pair[2]; assert(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair)==0);
    assert(cetta_worker_endpoint_new(store,pair[0],getuid(),"brain",&endpoint)==DURABLE_INVALID && !endpoint);
    close(pair[0]); close(pair[1]);
    assert(cetta_worker_endpoint_new(store,listener(),getuid(),"brain",&endpoint)==DURABLE_OK);
    int fd=client(); rpc(fd,WORKER_NEXT,"","",WORKER_IDLE,"","");
    rpc(fd,WORKER_RECEIPT,"missing","",WORKER_UNKNOWN,"missing","");
    rpc(fd,WORKER_RESULT,"missing","{}",WORKER_UNKNOWN,"missing","");
    assert(!count("host.worker-results") && !count("host.inbox"));
    assert(cetta_worker_publish(store,"brain","id-1","{\"text\":\"hello 😀\"}",strlen("{\"text\":\"hello 😀\"}"))==DURABLE_OK);
    const char *observation="{\"text\":\"hello 😀\"}";
    assert(cetta_worker_publish(store,"brain","id-1",observation,strlen(observation))==DURABLE_OK);
    assert(cetta_worker_publish(store,"brain","id-1","different",9)==DURABLE_CONFLICT);
    assert(cetta_worker_publish(store,"other","hidden","private",7)==DURABLE_OK);
    rpc(fd,WORKER_NEXT,"","",WORKER_TASK,"id-1",observation);
    close(fd); fd=client(); rpc(fd,WORKER_NEXT,"","",WORKER_TASK,"id-1",observation);
    rpc(fd,WORKER_RECEIPT,"id-1","",WORKER_PENDING,"id-1","");
    rpc(fd,WORKER_RESULT,"other/hidden","{}",WORKER_INVALID,"","");
    rpc(fd,WORKER_RESULT,"hidden","{}",WORKER_UNKNOWN,"hidden","");
    rpc(fd,WORKER_RESULT,"id-1","(println! data-not-code)",WORKER_STORED,"id-1","");
    assert(count("host.worker-results")==1 && count("host.inbox")==1);
    check_receipt();
    rpc(fd,WORKER_RESULT,"id-1","changed",WORKER_CONFLICT,"id-1","");
    consume("id-1");
    rpc(fd,WORKER_RESULT,"id-1","(println! data-not-code)",WORKER_STORED,"id-1","");
    rpc(fd,WORKER_RECEIPT,"id-1","",WORKER_STORED,"id-1","");
    rpc(fd,WORKER_NEXT,"","",WORKER_IDLE,"",""); assert(!count("host.inbox"));
    assert(cetta_worker_publish(store,"brain","bad","\xc0\xaf",2)==DURABLE_INVALID);
    assert(cetta_worker_publish(store,"brain","bad","\xed\xa0\x80",3)==DURABLE_INVALID);
    assert(cetta_worker_publish(store,"brain","bad","\xf4\x90\x80\x80",4)==DURABLE_INVALID);
    assert(cetta_worker_publish(store,"brain","bad","x\0y",3)==DURABLE_INVALID);
    send_request(fd,WORKER_RESULT,"id-1","x\0y",3); expect(fd,WORKER_INVALID,"","");
    send_request(fd,WORKER_RESULT,"id-1","\xf0\x9f",2); expect(fd,WORKER_INVALID,"","");
    assert(send(fd,"bad",3,0)==3); expect(fd,WORKER_INVALID,"","");
    rpc(fd,99,"id-1","",WORKER_INVALID,"","");
    char *large=malloc(CETTA_WORKER_BODY_MAX+2); assert(large);
    memset(large,'x',CETTA_WORKER_BODY_MAX+1); large[CETTA_WORKER_BODY_MAX+1]=0;
    send_request(fd,WORKER_RESULT,"id-1",large,CETTA_WORKER_BODY_MAX+1); expect(fd,WORKER_INVALID,"","");
    char long_id[65]; memset(long_id,'a',64); long_id[64]=0;
    send_request(fd,WORKER_RESULT,long_id,large,CETTA_WORKER_BODY_MAX+1); expect(fd,WORKER_INVALID,"","");
    large[CETTA_WORKER_BODY_MAX]=0;
    assert(cetta_worker_publish(store,"brain","large",large,CETTA_WORKER_BODY_MAX)==DURABLE_OK);
    rpc(fd,WORKER_NEXT,"","",WORKER_TASK,"large",large);
    int slow=client();
    /* Fill another peer's response window; this peer must still make progress. */
    for (unsigned i=0;i<50;++i) { send_request(slow,WORKER_NEXT,"",NULL,0); pump(); }
    rpc(fd,WORKER_NEXT,"","",WORKER_TASK,"large",large);
    rpc(fd,WORKER_RESULT,"large",large,WORKER_STORED,"large",""); close(slow);
    free(large); close(fd); pump(); pump();
    int peers[16];
    for (unsigned i=0;i<16;++i) { peers[i]=client(); pump(); }
    int excess=client(); pump(); char byte;
    assert(recv(excess,&byte,1,MSG_DONTWAIT)==0); close(excess);
    rpc(peers[0],WORKER_NEXT,"","",WORKER_IDLE,"","");
    for (unsigned i=0;i<16;++i) close(peers[i]);
    stop();
    /* Peer UID comes from the socket; a payload cannot claim another user. */
    assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    assert(cetta_worker_endpoint_new(store,listener(),getuid()+1,"brain",&endpoint)==DURABLE_OK);
    fd=client(); pump(); assert(recv(fd,&byte,1,MSG_DONTWAIT)==0); close(fd); stop();
    /* A child service commits, then exits without the client reading its ack. */
    assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    assert(cetta_worker_publish(store,"brain","crash","observation",11)==DURABLE_OK); cetta_durable_close(store); store=NULL;
    int listen_fd=listener(), signal_fd[2]; assert(pipe(signal_fd)==0);
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        close(signal_fd[0]); assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
        assert(cetta_worker_endpoint_new(store,listen_fd,getuid(),"brain",&endpoint)==DURABLE_OK);
        for (;;) {
            size_t requests; assert(cetta_worker_endpoint_step(endpoint,100,&requests)==DURABLE_OK);
            if (requests) { assert(write(signal_fd[1],"c",1)==1); _exit(0); }
        }
    }
    close(signal_fd[1]); fd=client(); send_request(fd,WORKER_RESULT,"crash","durable",7);
    assert(read(signal_fd[0],&byte,1)==1); close(signal_fd[0]); close(fd);
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    /* The supervisor can keep the listener descriptor across service death. */
    assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    assert(cetta_worker_endpoint_new(store,listen_fd,getuid(),"brain",&endpoint)==DURABLE_OK);
    fd=client(); rpc(fd,WORKER_RECEIPT,"crash","",WORKER_STORED,"crash","");
    rpc(fd,WORKER_RESULT,"crash","durable",WORKER_STORED,"crash","");
    assert(count("host.worker-results")==3); consume("crash");
    rpc(fd,WORKER_NEXT,"","",WORKER_IDLE,"","");
    /* Pending bound is independent of retained result history. */
    for (unsigned i=0;i<128;++i) {
        char id[32]; snprintf(id,sizeof(id),"task-%u",i);
        assert(cetta_worker_publish(store,"brain",id,"{}",2)==DURABLE_OK);
    }
    assert(cetta_worker_publish(store,"brain","overflow","{}",2)==DURABLE_LIMIT);
    rpc(fd,WORKER_NEXT,"","",WORKER_TASK,"task-0","{}");
    rpc(fd,WORKER_RESULT,"task-0","{}",WORKER_STORED,"task-0","");
    assert(cetta_worker_publish(store,"brain","overflow","{}",2)==DURABLE_OK);
    close(fd); stop();
    /* No ack and no removal when the ledger+inbox transaction exceeds quota. */
    snprintf(db,sizeof(db),"%s/quota.db",directory);
    CettaDurableLimits limits=cetta_durable_default_limits(); limits.records=2;
    assert(cetta_durable_open(db,&limits,&store)==DURABLE_OK);
    assert(cetta_worker_publish(store,"brain","limited","{}",2)==DURABLE_OK);
    assert(cetta_worker_endpoint_new(store,listener(),getuid(),"brain",&endpoint)==DURABLE_OK);
    fd=client(); rpc(fd,WORKER_RESULT,"limited","{}",WORKER_LIMIT,"limited","");
    assert(!count("host.worker-results") && !count("host.inbox") && count("host.worker-ready")==1);
    close(fd); stop();
    assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    assert(cetta_worker_endpoint_new(store,listener(),getuid(),"brain",&endpoint)==DURABLE_OK);
    fd=client(); rpc(fd,WORKER_NEXT,"","",WORKER_TASK,"limited","{}");
    rpc(fd,WORKER_RESULT,"limited","{}",WORKER_STORED,"limited","");
    close(fd); stop();
    /* Batched reading: NEXT after a pending ID, without answering anything. */
    snprintf(db,sizeof(db),"%s/channel.db",directory);
    assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    assert(cetta_worker_endpoint_new(store,listener(),getuid(),"brain",&endpoint)==DURABLE_OK);
    assert(cetta_worker_publish(store,"brain","m1","one",3)==DURABLE_OK);
    assert(cetta_worker_publish(store,"brain","m2","two",3)==DURABLE_OK);
    assert(cetta_worker_publish(store,"brain","m3","three",5)==DURABLE_OK);
    fd=client();
    rpc(fd,WORKER_NEXT,"","",WORKER_TASK,"m1","one");
    rpc(fd,WORKER_NEXT,"m1","",WORKER_TASK,"m2","two");
    rpc(fd,WORKER_NEXT,"m2","",WORKER_TASK,"m3","three");
    rpc(fd,WORKER_NEXT,"m3","",WORKER_IDLE,"","");
    rpc(fd,WORKER_RESULT,"m2","[]",WORKER_STORED,"m2","");
    rpc(fd,WORKER_NEXT,"m1","",WORKER_TASK,"m3","three");
    rpc(fd,WORKER_NEXT,"m2","",WORKER_UNKNOWN,"m2","");
    rpc(fd,WORKER_NEXT,"never","",WORKER_UNKNOWN,"never","");
    send_request(fd,WORKER_NEXT,"m1","x",1); expect(fd,WORKER_INVALID,"","");
    /* Unsolicited submissions: immutable per key, recorded with their input. */
    const char *batch="{\"chat\":7,\"actions\":[]}";
    rpc(fd,WORKER_SUBMIT,"7.0.00000000000000000001",batch,WORKER_STORED,"7.0.00000000000000000001","");
    expect_submission("7.0.00000000000000000001",batch);
    size_t inputs=count("host.inbox");
    rpc(fd,WORKER_SUBMIT,"7.0.00000000000000000001",batch,WORKER_STORED,"7.0.00000000000000000001","");
    rpc(fd,WORKER_SUBMIT,"7.0.00000000000000000001","{}",WORKER_CONFLICT,"7.0.00000000000000000001","");
    assert(count("host.inbox")==inputs && count("host.worker-submissions")==1);
    consume_submission("7.0.00000000000000000001");
    /* A lost acknowledgment is recovered by resending: no second occurrence. */
    rpc(fd,WORKER_SUBMIT,"7.0.00000000000000000001",batch,WORKER_STORED,"7.0.00000000000000000001","");
    assert(count("host.inbox")==inputs-1 && count("host.worker-submissions")==1);
    send_request(fd,WORKER_SUBMIT,"bad-utf8","\xc0\xaf",2); expect(fd,WORKER_INVALID,"","");
    send_request(fd,WORKER_SUBMIT,"empty","",0); expect(fd,WORKER_INVALID,"","");
    send_request(fd,WORKER_SUBMIT,"bad/key","{}",2); expect(fd,WORKER_INVALID,"","");
    assert(count("host.worker-submissions")==1);
    for (unsigned i=0;i<128;++i) {
        char id[32]; snprintf(id,sizeof(id),"pending-%u",i);
        rpc(fd,WORKER_SUBMIT,id,"{}",WORKER_STORED,id,"");
    }
    rpc(fd,WORKER_SUBMIT,"overflow","{}",WORKER_LIMIT,"overflow","");
    consume_submission("pending-0");
    rpc(fd,WORKER_SUBMIT,"overflow","{}",WORKER_STORED,"overflow","");
    close(fd); stop();
    const char *files[]={"journal.db","quota.db","channel.db"};
    for (size_t i=0;i<3;++i) {
        char file[300]; snprintf(file,sizeof(file),"%s/%s",directory,files[i]); unlink(file);
        snprintf(file,sizeof(file),"%s/%s-wal",directory,files[i]); unlink(file);
        snprintf(file,sizeof(file),"%s/%s-shm",directory,files[i]); unlink(file);
    }
    assert(!rmdir(directory));
    puts("durable worker: local peer identity, bounded data, immutable tasks, atomic replies, lost acknowledgments, process restart, quota rollback, batched reads and unsolicited submissions passed");
}
