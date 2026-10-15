#define _POSIX_C_SOURCE 200809L
#include "telegram_transport.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char token[]="123456789:TEST_ONLY_abcdefghijklmnopqrstuvwxyz0123456789";
static CettaTelegramCredential *load(int fd, const char *origin, const char *ca, bool local) {
    CettaTelegramCredential *c=NULL;
    CettaTelegramCredentialConfig config={.origin=origin,.ca_file=ca,.allow_loopback_http=local};
    assert(cetta_telegram_credential_read(fd,&config,&c)==TELEGRAM_CREDENTIAL_OK);
    return c;
}
static CettaHttpResult take(CettaHttpWorker *w) {
    CettaHttpResult r={0}; uint64_t generation=0;
    for (unsigned i=0;i<100;++i) {
        if (cetta_http_worker_take(w,&r)) return r;
        generation=cetta_http_worker_wait(w,generation,100);
    }
    abort();
}
static void write_token(int fd, const char *s, size_t n) {
    assert(!ftruncate(fd,0) && pwrite(fd,s,n,0)==(ssize_t)n);
}
static void malformed(int fd, const char *origin, bool local) {
    CettaTelegramCredential *c=(void *)1;
    CettaTelegramCredentialConfig cfg={.origin=origin,.allow_loopback_http=local};
    assert(cetta_telegram_credential_read(fd,&cfg,&c)==TELEGRAM_CREDENTIAL_INVALID && !c);
}
static void unit(const char *path) {
    int fd=open(path,O_RDWR|O_CREAT|O_EXCL,0600); assert(fd>=0);
    write_token(fd,token,sizeof(token)-1);
    assert(lseek(fd,3,SEEK_SET)==3);
    CettaTelegramCredential *c=load(fd,NULL,NULL,false);
    assert(lseek(fd,0,SEEK_CUR)==3);
    assert(cetta_telegram_response_safe(c,"{\"ok\":true}",11));
    assert(!cetta_telegram_response_safe(c,token,sizeof(token)-1));
    const char *secret=strchr(token,':')+1;
    assert(!cetta_telegram_response_safe(c,secret,strlen(secret)));
    char encoded[6*sizeof(token)+1]; size_t used=0;
    for (size_t i=0;i<sizeof(token)-1;++i)
        used+=(size_t)sprintf(encoded+used,"\\u%04x",(unsigned char)token[i]);
    assert(!cetta_telegram_response_safe(c,encoded,used));
    unsigned char binary[sizeof(token)+2]={0};
    memcpy(binary+1,token,sizeof(token)-1);
    assert(!cetta_telegram_response_safe(c,binary,sizeof(binary)));
    /* A prefix alone is ordinary text; a later complete match still counts. */
    assert(cetta_telegram_response_safe(c,token,sizeof(token)-2));
    snprintf(encoded,sizeof(encoded),"%.12s-%s",token,token);
    assert(!cetta_telegram_response_safe(c,encoded,strlen(encoded)));
    used=0;
    for (size_t i=0;i<sizeof(token)-1;++i)
        used+=(size_t)sprintf(encoded+used,"%%%02x",(unsigned char)token[i]);
    assert(!cetta_telegram_response_safe(c,encoded,used));
    used=0;
    for (size_t i=0;secret[i];++i)
        used+=(size_t)sprintf(encoded+used,"\\u%04x",(unsigned char)secret[i]);
    assert(!cetta_telegram_response_safe(c,encoded,used));
    used=0;
    for (size_t i=0;secret[i];++i)
        used+=(size_t)sprintf(encoded+used,"%%%02x",(unsigned char)secret[i]);
    assert(!cetta_telegram_response_safe(c,encoded,used));
    assert(cetta_telegram_response_safe(c,secret,strlen(secret)-1));
    snprintf(encoded,sizeof(encoded),"%.12s-%s",secret,secret);
    assert(!cetta_telegram_response_safe(c,encoded,strlen(encoded)));
    assert(!cetta_telegram_response_safe(c,NULL,1));
    assert(!cetta_telegram_response_safe(c,"x",8u*1024u*1024u+1));
    cetta_telegram_credential_free(c);
    const char *bad_origins[]={"http://api.telegram.org","http://localhost:80",
        "https://user@api.telegram.org","https://api.telegram.org/path",
        "https://api.telegram.org?x","https://api.telegram.org#x","https://api.telegram.org:0",
        "https://api.telegram.org:65536","https://api.telegram.org:443/",
        "https://api.telegram.org\\evil","file:///tmp/api"};
    for (size_t i=0;i<sizeof(bad_origins)/sizeof(*bad_origins);++i) malformed(fd,bad_origins[i],true);
    malformed(fd,"http://127.0.0.1:8080",false);
    c=load(fd,"http://127.0.0.1:8080",NULL,true); cetta_telegram_credential_free(c);
    c=load(fd,"http://[::1]:8080",NULL,true); cetta_telegram_credential_free(c);
    assert(!fchmod(fd,0644)); malformed(fd,NULL,false); assert(!fchmod(fd,0600));
    const char *bad_tokens[]={"", "foo", "123:tiny", "123456789:abcdefghijklmnop/../../", "123456789:abcdefghijklmnop\nINJECT"};
    for (size_t i=0;i<sizeof(bad_tokens)/sizeof(*bad_tokens);++i) {
        write_token(fd,bad_tokens[i],strlen(bad_tokens[i])); malformed(fd,NULL,false);
    }
    char padded[600]; memset(padded,'x',sizeof(padded)); write_token(fd,padded,sizeof(padded)); malformed(fd,NULL,false);
    snprintf(padded,sizeof(padded),"%s\r\n",token); write_token(fd,padded,strlen(padded));
    c=load(fd,NULL,NULL,false); cetta_telegram_credential_free(c);
    write_token(fd,token,sizeof(token)-1); close(fd);
    c=(void *)1;
    assert(cetta_telegram_credential_read(-1,NULL,&c)==TELEGRAM_CREDENTIAL_IO && !c);
    int pipefd[2]; assert(!pipe(pipefd));
    malformed(pipefd[0],NULL,false); close(pipefd[0]); close(pipefd[1]);
    fd=open(path,O_WRONLY); assert(fd>=0); c=(void *)1;
    assert(cetta_telegram_credential_read(fd,NULL,&c)==TELEGRAM_CREDENTIAL_IO && !c);
    close(fd);
}
int main(int argc, char **argv) {
    assert(argc==2 || argc==4 || argc==5);
    if (argc==2) { unit(argv[1]); puts("credential validation and reflected-token screening passed"); return 0; }
    int fd=open(argv[1],O_RDONLY); assert(fd>=0);
    CettaTelegramCredential *c=load(fd,argv[2],!strcmp(argv[3],"-")?NULL:argv[3],true);
    close(fd);
    CettaHttpWorker *w=NULL; assert(cetta_http_worker_new(NULL,NULL,&w)==HTTP_WORKER_OK);
    assert(cetta_telegram_submit_effect(c,w,1,"../sendMessage","application/json","{}",2,2000,4096)==HTTP_WORKER_INVALID);
    assert(cetta_telegram_submit_effect(c,w,1,"getUpdates","application/json","{}",2,2000,4096)==HTTP_WORKER_INVALID);
    assert(cetta_telegram_submit_effect(c,w,1,"GETUPDATES","application/json","{}",2,2000,4096)==HTTP_WORKER_INVALID);
    const char *admin[]={"setWebhook","deleteWebhook","logOut","close",
        "SETWEBHOOK","DELETEWEBHOOK","LOGOUT","CLOSE","sEtWeBhOoK"};
    for (size_t i=0;i<sizeof(admin)/sizeof(*admin);++i)
        assert(cetta_telegram_submit_effect(c,w,1,admin[i],"application/json","{}",2,2000,4096)==HTTP_WORKER_INVALID);
    assert(cetta_telegram_submit_effect(c,w,1,"sendMessage","application/json\r\nX: bad","{}",2,2000,4096)==HTTP_WORKER_INVALID);
    if (argc==5) {
        assert(!strcmp(argv[4],"tls-failure"));
        assert(cetta_telegram_submit_effect(c,w,1,"sendMessage","application/json","{}",2,2000,4096)==HTTP_WORKER_OK);
        CettaHttpResult r=take(w);
        assert(r.transport_code==60 && r.status==0 && r.body_size==0);
        cetta_http_result_free(&r);
        assert(cetta_http_worker_free(w)==0); cetta_telegram_credential_free(c);
        puts("TLS verification failure remains enabled and returns no request source"); return 0;
    }
    for (unsigned i=1;i<=6;++i) {
        char body[64]; int n=snprintf(body,sizeof(body),"{\"request\":%u}",i);
        CettaHttpWorkerStatus status=(i<4 || i==6)
            ? cetta_telegram_submit_effect(c,w,i,"sendMessage","application/json",body,(size_t)n,2000,4096)
            : cetta_telegram_submit_poll(c,w,i,body,(size_t)n,2000,4096);
        assert(status==HTTP_WORKER_OK);
        CettaHttpResult r=take(w);
        if (i==2) assert(r.started && r.transport_code!=0);
        else if (i==6) assert(r.status==307 && !r.transport_code);
        else if (i==3) {
            assert(r.status==200 && !r.transport_code);
            assert(!cetta_telegram_response_safe(c,r.body,r.body_size));
        } else {
            assert(r.status==200 && !r.transport_code);
            assert(cetta_telegram_response_safe(c,r.body,r.body_size));
        }
        cetta_http_result_free(&r);
    }
    assert(cetta_http_worker_free(w)==0); cetta_telegram_credential_free(c);
    puts("credential-backed requests: no ambient proxy, drop-after-read uncertainty, safe poll reuse and reflection screening and redirect refusal passed");
    return 0;
}
