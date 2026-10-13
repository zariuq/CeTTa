#define _POSIX_C_SOURCE 200809L
#include "telegram_transport.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define TOKEN_MAX 512
#define ORIGIN_MAX 1024
#define RESPONSE_MAX (8u*1024u*1024u)

struct CettaTelegramCredential {
    char token[TOKEN_MAX+1];
    size_t token_size, secret_offset;
    uint16_t prefix[TOKEN_MAX];
    char *origin, *ca_file;
};

bool cetta_telegram_credential_source(const CettaTelegramCredential *c, char out[65]) {
    if (!c || !out) return false;
    memcpy(out,"telegram-",9);
    size_t n=c->secret_offset-1;
    memcpy(out+9,c->token,n); out[9+n]=0;
    return true;
}

static void erase(void *bytes, size_t n) {
    volatile unsigned char *p=bytes;
    while (n--) *p++=0;
}
static bool letter(unsigned char c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z');
}
static bool digit(unsigned char c) { return c>='0' && c<='9'; }

static bool origin_valid(const char *origin, bool local_http) {
    size_t n=strnlen(origin,ORIGIN_MAX+1);
    if (n>ORIGIN_MAX) return false;
    bool https=!strncmp(origin,"https://",8);
    if (!https && (!local_http || strncmp(origin,"http://",7))) return false;
    const char *host=origin+(https?8:7), *end=host;
    if (*host=='[') {
        /* The local fixture uses IPv6 loopback; other IPv6 origins may be
         * added with a proper address parser when a host needs them. */
        if (strncmp(host,"[::1]",5)) return false;
        end=host+5;
    } else {
        while (letter((unsigned char)*end) || digit((unsigned char)*end) ||
               *end=='.' || *end=='-') ++end;
        if (end==host || *host=='.' || *host=='-' || end[-1]=='.' || end[-1]=='-') return false;
    }
    if (!https && !((end-host==9 && !memcmp(host,"127.0.0.1",9)) ||
                    (end-host==5 && !memcmp(host,"[::1]",5)))) return false;
    if (*end==':') {
        ++end; unsigned port=0, digits=0;
        while (digit((unsigned char)*end)) {
            if (++digits>5) return false;
            port=port*10+(unsigned)(*end++-'0');
        }
        if (!digits || !port || port>65535) return false;
    }
    return !*end; /* no path, userinfo, query, fragment or alternate scheme */
}

static bool token_valid(const char *s, size_t n) {
    size_t i=0;
    while (i<n && digit((unsigned char)s[i])) ++i;
    if (!i || i>32 || i>=n || s[i++]!=':' || n-i<16) return false;
    for (;i<n;++i)
        if (!letter((unsigned char)s[i]) && !digit((unsigned char)s[i]) &&
            s[i]!='-' && s[i]!='_') return false;
    return true;
}

CettaTelegramCredentialStatus cetta_telegram_credential_read(
    int fd, const CettaTelegramCredentialConfig *config, CettaTelegramCredential **out) {
    if (!out) return TELEGRAM_CREDENTIAL_INVALID;
    *out=NULL;
    const char *origin=config && config->origin?config->origin:"https://api.telegram.org";
    const char *ca=config?config->ca_file:NULL;
    if (!origin_valid(origin,config && config->allow_loopback_http) ||
        (ca && (!*ca || strnlen(ca,4097)>4096 || strpbrk(ca,"\r\n"))))
        return TELEGRAM_CREDENTIAL_INVALID;
    struct stat st;
    if (fstat(fd,&st)) return TELEGRAM_CREDENTIAL_IO;
    if (!S_ISREG(st.st_mode) || (st.st_mode&007) ||
        (st.st_uid!=geteuid() && st.st_uid!=0) || st.st_size<1 || st.st_size>TOKEN_MAX+2)
        return TELEGRAM_CREDENTIAL_INVALID;
    char bytes[TOKEN_MAX+3]={0}; size_t used=0;
    CettaTelegramCredentialStatus status=TELEGRAM_CREDENTIAL_IO;
    while (used<sizeof(bytes)) {
        ssize_t n=pread(fd,bytes+used,sizeof(bytes)-used,(off_t)used);
        if (n<0) { if (errno==EINTR) continue; goto done; }
        if (!n) break;
        used+=(size_t)n;
    }
    status=TELEGRAM_CREDENTIAL_INVALID;
    if (used && bytes[used-1]=='\n') {
        --used; if (used && bytes[used-1]=='\r') --used;
    }
    if (!used || used>TOKEN_MAX || !token_valid(bytes,used)) goto done;
    CettaTelegramCredential *c=calloc(1,sizeof(*c));
    if (!c) { status=TELEGRAM_CREDENTIAL_NOMEM; goto done; }
    memcpy(c->token,bytes,used); c->token_size=used;
    c->secret_offset=(size_t)(strchr(c->token,':')-c->token)+1;
    c->origin=strdup(origin); c->ca_file=ca?strdup(ca):NULL;
    if (!c->origin || (ca && !c->ca_file)) {
        cetta_telegram_credential_free(c); status=TELEGRAM_CREDENTIAL_NOMEM; goto done;
    }
    const char *secret=c->token+c->secret_offset;
    for (size_t i=1,k=0;i<used-c->secret_offset;++i) {
        while (k && secret[i]!=secret[k]) k=c->prefix[k-1];
        if (secret[i]==secret[k]) ++k;
        c->prefix[i]=(uint16_t)k;
    }
    *out=c; status=TELEGRAM_CREDENTIAL_OK;
done:
    erase(bytes,sizeof(bytes)); return status;
}

void cetta_telegram_credential_free(CettaTelegramCredential *c) {
    if (!c) return;
    free(c->origin); free(c->ca_file);
    erase(c,sizeof(*c)); free(c);
}

static CettaHttpWorkerStatus submit(const CettaTelegramCredential *c,
    CettaHttpWorker *w, uint64_t id, const char *method, const char *content_type,
    const void *body, size_t size, uint32_t timeout, size_t maximum, bool poll) {
    if (!c || !method || !*method || strnlen(method,65)>64 ||
        !content_type || !*content_type || strnlen(content_type,257)>256)
        return HTTP_WORKER_INVALID;
    for (const unsigned char *p=(const unsigned char *)method;*p;++p)
        if (!letter(*p)) return HTTP_WORKER_INVALID;
    for (const unsigned char *p=(const unsigned char *)content_type;*p;++p)
        if (*p<32 || *p>=127) return HTTP_WORKER_INVALID;
    /* These change intake/ownership or bot availability. Even an authorized
     * send capability must not be able to redirect or discard future input. */
    const char *admin[]={"getUpdates","setWebhook","deleteWebhook","logOut","close"};
    if (!poll) for (size_t i=0;i<sizeof(admin)/sizeof(*admin);++i)
        if (!strcasecmp(method,admin[i])) return HTTP_WORKER_INVALID;
    char url[ORIGIN_MAX+TOKEN_MAX+80];
    size_t n=strlen(c->origin), m=strlen(method);
    memcpy(url,c->origin,n); memcpy(url+n,"/bot",4); n+=4;
    memcpy(url+n,c->token,c->token_size); n+=c->token_size;
    url[n++]='/'; memcpy(url+n,method,m+1); n+=m;
    char header[272];
    memcpy(header,"Content-Type: ",14); strcpy(header+14,content_type);
    const char *headers[]={header};
    CettaHttpRequest r={.id=id,.method="POST",.url=url,.headers=headers,.header_count=1,
        .body=body,.body_size=size,.timeout_ms=timeout,.max_response_bytes=maximum,
        .follow_redirects=false,.idempotent=poll,.proxy="",.ca_file=c->ca_file};
    CettaHttpWorkerStatus result=cetta_http_worker_submit(w,&r);
    erase(url,n+1); return result;
}

CettaHttpWorkerStatus cetta_telegram_submit_effect(const CettaTelegramCredential *c,
    CettaHttpWorker *w, uint64_t id, const char *method, const char *content_type,
    const void *body, size_t size, uint32_t timeout, size_t maximum) {
    return submit(c,w,id,method,content_type,body,size,timeout,maximum,false);
}
CettaHttpWorkerStatus cetta_telegram_submit_poll(const CettaTelegramCredential *c,
    CettaHttpWorker *w, uint64_t id, const void *body, size_t size,
    uint32_t timeout, size_t maximum) {
    return submit(c,w,id,"getUpdates","application/json",body,size,timeout,maximum,true);
}

static int hex(unsigned char c) {
    if (digit(c)) return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
bool cetta_telegram_response_safe(const CettaTelegramCredential *c, const void *body, size_t n) {
    if (!c || (n && !body) || n>RESPONSE_MAX) return false;
    const char *secret=c->token+c->secret_offset;
    size_t secret_size=c->token_size-c->secret_offset;
    const unsigned char *s=body; size_t matched=0;
    for (size_t i=0;i<n;) {
        unsigned char b=s[i++]; int hi,lo;
        if (b=='%' && n-i>=2 && (hi=hex(s[i]))>=0 && (lo=hex(s[i+1]))>=0) {
            b=(unsigned char)(hi*16+lo); i+=2;
        } else if (b=='\\' && n-i>=5 && s[i]=='u' && s[i+1]=='0' && s[i+2]=='0' &&
                   (hi=hex(s[i+3]))>=0 && (lo=hex(s[i+4]))>=0) {
            b=(unsigned char)(hi*16+lo); i+=5;
        }
        while (matched && b!=(unsigned char)secret[matched]) matched=c->prefix[matched-1];
        if (b==(unsigned char)secret[matched] && ++matched==secret_size) return false;
    }
    return true;
}
