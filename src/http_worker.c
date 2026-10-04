#define _POSIX_C_SOURCE 200809L
#include "http_worker.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef CETTA_BUILD_HTTP_PROVIDER_CURL
#define CETTA_BUILD_HTTP_PROVIDER_CURL 0
#endif

CettaHttpWorkerLimits cetta_http_worker_default_limits(void) {
    return (CettaHttpWorkerLimits){64,8*1024*1024,32*1024*1024};
}

void cetta_http_result_free(CettaHttpResult *r) {
    if (r) { free(r->body); memset(r,0,sizeof(*r)); }
}

#if CETTA_BUILD_HTTP_PROVIDER_CURL
#include <curl/curl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#if LIBCURL_VERSION_NUM < 0x075500
#error "HTTP worker requires libcurl 7.85 or newer with thread-safe initialization and asynchronous DNS"
#endif

typedef enum { QUEUED, PREPARING, RUNNING, OBSERVING, READY } JobState;
typedef struct Job {
    CettaHttpRequest request;
    CettaHttpResult result;
    JobState state;
    bool cancel, abandon;
    CettaHttpRecordMode record_mode;
    unsigned record_attempts;
    uint64_t retry_at;
    size_t request_bytes, response_capacity;
    CURL *easy;
    struct curl_slist *headers;
    struct Job *next;
    struct CettaHttpWorker *owner;
} Job;

/* submit's stack-owned reservation is visible only while holding the mutex. */
typedef struct Reservation {
    uint64_t id;
    struct Reservation *next;
} Reservation;

struct CettaHttpWorker {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    CURLM *multi;
    Job *jobs, *tail;
    Reservation *reservations;
    CettaHttpWorkerLimits limits;
    CettaHttpWorkerHooks hooks;
    size_t count, request_bytes, response_bytes, unacknowledged;
    uint64_t generation;
    bool initialized, available, stopping;
};

static pthread_once_t curl_once=PTHREAD_ONCE_INIT;
static bool curl_ready;
static void init_curl(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) return;
    const curl_version_info_data *v=curl_version_info(CURLVERSION_NOW);
    curl_ready=v && (v->features&CURL_VERSION_THREADSAFE) && (v->features&CURL_VERSION_ASYNCHDNS);
}

static uint64_t milliseconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}

static void changed(CettaHttpWorker *w) {
    ++w->generation;
    pthread_cond_broadcast(&w->changed);
}

static void wake(CettaHttpWorker *w) {
    if (w->multi) (void)curl_multi_wakeup(w->multi);
}

static Job *find_job(CettaHttpWorker *w, uint64_t id) {
    for (Job *j=w->jobs;j;j=j->next) if (j->request.id==id) return j;
    return NULL;
}

static void free_job(Job *j) {
    /* URLs may hold bearer credentials. Volatile stores also work on libc
     * targets without explicit_bzero; this covers our copy, not curl's. */
    if (j->request.url) {
        volatile char *url=(volatile char *)j->request.url;
        size_t n=strlen(j->request.url);
        while (n--) *url++=0;
    }
    free((char *)j->request.method); free((char *)j->request.url);
    free((char *)j->request.proxy); free((char *)j->request.ca_file);
    for (size_t i=0;i<j->request.header_count;++i) free((char *)j->request.headers[i]);
    free((char **)j->request.headers); free((void *)j->request.body);
    cetta_http_result_free(&j->result); free(j);
}

/* Caller holds mutex; no live curl handles may remain. */
static void retire(CettaHttpWorker *w, Job *j) {
    Job **link=&w->jobs, *previous=NULL;
    while (*link && *link!=j) { previous=*link; link=&(*link)->next; }
    if (!*link) abort();
    *link=j->next; if (w->tail==j) w->tail=previous; --w->count;
    w->request_bytes-=j->request_bytes;
    w->response_bytes-=j->response_capacity;
    free_job(j); changed(w);
}

static size_t receive(char *data, size_t size, size_t count, void *context) {
    Job *j=context; CettaHttpWorker *w=j->owner;
    if (size && count>SIZE_MAX/size) return 0;
    size_t n=size*count;
    pthread_mutex_lock(&w->mutex);
    if (n>j->request.max_response_bytes-j->result.body_size) {
        j->result.response_too_large=true;
        pthread_mutex_unlock(&w->mutex); return 0;
    }
    size_t needed=j->result.body_size+n+1;
    if (needed>j->response_capacity) {
        size_t capacity=j->response_capacity?j->response_capacity*2:256;
        if (capacity<needed) capacity=needed;
        if (capacity>j->request.max_response_bytes+1) capacity=j->request.max_response_bytes+1;
        size_t available=w->limits.response_bytes-w->response_bytes;
        if (capacity-j->response_capacity>available) capacity=j->response_capacity+available;
        size_t increment=capacity-j->response_capacity;
        if (capacity<needed) {
            j->result.response_budget_exceeded=true;
            pthread_mutex_unlock(&w->mutex); return 0;
        }
        unsigned char *body=realloc(j->result.body,capacity);
        if (!body) {
            j->result.allocation_failed=true;
            pthread_mutex_unlock(&w->mutex); return 0;
        }
        j->result.body=body; w->response_bytes+=increment; j->response_capacity=capacity;
    }
    if (n) memcpy(j->result.body+j->result.body_size,data,n);
    j->result.body_size+=n; j->result.body[j->result.body_size]=0;
    pthread_mutex_unlock(&w->mutex);
    return n;
}

/* These functions execute only on the owner thread. */
static CURLcode configure(Job *j) {
    j->easy=curl_easy_init();
    if (!j->easy) return CURLE_OUT_OF_MEMORY;
    for (size_t i=0;i<j->request.header_count;++i) {
        struct curl_slist *next=curl_slist_append(j->headers,j->request.headers[i]);
        if (!next) return CURLE_OUT_OF_MEMORY;
        j->headers=next;
    }
#define SET(option, value) do { CURLcode c=curl_easy_setopt(j->easy,option,value); if (c!=CURLE_OK) return c; } while (0)
    SET(CURLOPT_URL,j->request.url);
    SET(CURLOPT_WRITEFUNCTION,receive); SET(CURLOPT_WRITEDATA,j); SET(CURLOPT_PRIVATE,j);
    SET(CURLOPT_NOSIGNAL,1L); SET(CURLOPT_FOLLOWLOCATION,j->request.follow_redirects?1L:0L);
    SET(CURLOPT_MAXREDIRS,8L);
    /* Curl may silently resend even a POST after a dead reused connection.
     * Opt into pooling only when repeating the operation is explicitly safe. */
    SET(CURLOPT_FRESH_CONNECT,j->request.idempotent?0L:1L);
    SET(CURLOPT_FORBID_REUSE,j->request.idempotent?0L:1L);
    if (j->request.proxy) SET(CURLOPT_PROXY,j->request.proxy);
    if (j->request.ca_file) SET(CURLOPT_CAINFO,j->request.ca_file);
    SET(CURLOPT_PROTOCOLS_STR,"http,https"); SET(CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
    SET(CURLOPT_ACCEPT_ENCODING,"");
    if (j->request.timeout_ms) {
        SET(CURLOPT_TIMEOUT_MS,(long)j->request.timeout_ms);
        SET(CURLOPT_CONNECTTIMEOUT_MS,(long)j->request.timeout_ms);
    }
    if (j->headers) SET(CURLOPT_HTTPHEADER,j->headers);
    if (!strcmp(j->request.method,"GET")) SET(CURLOPT_HTTPGET,1L);
    else {
        SET(CURLOPT_CUSTOMREQUEST,j->request.method);
        SET(CURLOPT_POSTFIELDS,j->request.body);
        SET(CURLOPT_POSTFIELDSIZE_LARGE,(curl_off_t)j->request.body_size);
    }
#undef SET
    return CURLE_OK;
}

static void clear_easy(CettaHttpWorker *w, Job *j, bool added) {
    if (j->easy) {
        if (added) curl_multi_remove_handle(w->multi,j->easy);
        curl_easy_cleanup(j->easy); j->easy=NULL;
    }
    curl_slist_free_all(j->headers); j->headers=NULL;
}

static void finish(CettaHttpWorker *w, Job *j, CURLcode code, bool cancelled) {
    long response=0, request_size=0;
    bool known=false;
    if (j->easy) {
        curl_easy_getinfo(j->easy,CURLINFO_RESPONSE_CODE,&response);
        known=curl_easy_getinfo(j->easy,CURLINFO_REQUEST_SIZE,&request_size)==CURLE_OK && request_size>=0;
    }
    clear_easy(w,j,j->result.started);
    pthread_mutex_lock(&w->mutex);
    j->result.transport_code=code; j->result.status=response; j->result.cancelled=cancelled;
    j->result.request_size_known=known; j->result.request_size=request_size;
    j->state=OBSERVING; j->retry_at=0;
    pthread_mutex_unlock(&w->mutex);
}

static bool persistence_pending(CettaHttpWorker *w) {
    if (w->hooks.observe)
        for (Job *j=w->jobs;j;j=j->next) if (j->state==OBSERVING) return true;
    return false;
}

static void process_observations(CettaHttpWorker *w, bool final) {
    /* Only the owner changes OBSERVING jobs. Pollers can remove READY jobs,
     * so select afresh under the mutex after every unlocked callback. */
    for (;;) {
        pthread_mutex_lock(&w->mutex);
        Job *j=NULL; uint64_t now=milliseconds();
        for (Job *p=w->jobs;p;p=p->next)
            if (p->state==OBSERVING && (final || p->retry_at<=now)) { j=p; break; }
        if (!j) { pthread_mutex_unlock(&w->mutex); return; }
        pthread_mutex_unlock(&w->mutex);
        CettaHttpResult minimal=j->result;
        minimal.body=NULL; minimal.body_size=0;
        CettaHttpRecord recorded=w->hooks.observe
            ? w->hooks.observe(w->hooks.context,
                j->record_mode==HTTP_RECORD_FULL?&j->result:&minimal,j->record_mode)
            : HTTP_RECORD_RETRY;
        pthread_mutex_lock(&w->mutex);
        bool stalled=false;
        if (recorded==HTTP_RECORD_ACK || j->abandon) retire(w,j);
        else if (w->hooks.observe) {
            if (recorded==HTTP_RECORD_USE_MINIMAL && j->record_mode==HTTP_RECORD_FULL) {
                j->record_mode=HTTP_RECORD_MINIMAL; j->record_attempts=0; j->retry_at=0;
            } else if (final) { ++w->unacknowledged; retire(w,j); }
            else if (++j->record_attempts>=w->hooks.record_attempts) {
                j->retry_at=UINT64_MAX; stalled=true; changed(w);
            } else {
                unsigned shift=j->record_attempts-1;
                j->retry_at=milliseconds()+(shift<4?(100u<<shift):1000u);
            }
        } else { j->state=READY; changed(w); }
        uint64_t stalled_id=stalled?j->request.id:0;
        CettaHttpRecordMode stalled_mode=stalled?j->record_mode:HTTP_RECORD_FULL;
        unsigned attempts=stalled?j->record_attempts:0;
        pthread_mutex_unlock(&w->mutex);
        if (stalled) w->hooks.recording_stalled(w->hooks.context,stalled_id,stalled_mode,attempts);
    }
}

static void process_jobs(CettaHttpWorker *w) {
    for (;;) {
        pthread_mutex_lock(&w->mutex);
        Job *j=NULL; uint64_t now=milliseconds();
        bool blocked=persistence_pending(w);
        for (Job *p=w->jobs;p;p=p->next) {
            if ((p->state==QUEUED && (p->cancel || (!blocked && p->retry_at<=now))) ||
                (p->state==RUNNING && p->cancel)) { j=p; break; }
        }
        if (!j || w->stopping) { pthread_mutex_unlock(&w->mutex); return; }
        bool cancel=j->cancel, running=j->state==RUNNING;
        j->state=PREPARING;
        pthread_mutex_unlock(&w->mutex);
        if (cancel) { finish(w,j,CURLE_ABORTED_BY_CALLBACK,true); continue; }
        if (running) abort();
        CettaHttpPrepare prepare=w->hooks.prepare
            ? w->hooks.prepare(w->hooks.context,j->request.id) : HTTP_PREPARE_READY;
        pthread_mutex_lock(&w->mutex);
        cancel=j->cancel || w->stopping;
        pthread_mutex_unlock(&w->mutex);
        if (cancel || (prepare!=HTTP_PREPARE_READY && prepare!=HTTP_PREPARE_DEFER)) {
            finish(w,j,CURLE_ABORTED_BY_CALLBACK,true); continue;
        }
        if (prepare==HTTP_PREPARE_DEFER) {
            pthread_mutex_lock(&w->mutex);
            j->state=QUEUED; j->retry_at=milliseconds()+100;
            pthread_mutex_unlock(&w->mutex); continue;
        }
        CURLcode code=configure(j);
        /* Serialize the last cancellation check and start admission. No
         * networking occurs until this owner later calls multi_perform. */
        pthread_mutex_lock(&w->mutex);
        cancel=j->cancel || w->stopping;
        if (!cancel && code==CURLE_OK && curl_multi_add_handle(w->multi,j->easy)!=CURLM_OK)
            code=CURLE_FAILED_INIT;
        if (!cancel && code==CURLE_OK) { j->result.started=true; j->state=RUNNING; }
        pthread_mutex_unlock(&w->mutex);
        if (cancel || code!=CURLE_OK) finish(w,j,cancel?CURLE_ABORTED_BY_CALLBACK:code,cancel);
    }
}

static void *owner(void *context) {
    CettaHttpWorker *w=context;
    sigset_t blocked; sigemptyset(&blocked); sigaddset(&blocked,SIGPIPE);
    pthread_sigmask(SIG_BLOCK,&blocked,NULL);
    CURLM *multi=curl_ready?curl_multi_init():NULL;
    pthread_mutex_lock(&w->mutex);
    w->multi=multi; w->available=multi!=NULL; w->initialized=true;
    pthread_cond_broadcast(&w->changed);
    pthread_mutex_unlock(&w->mutex);
    if (!multi) return NULL;
    for (;;) {
        process_observations(w,false);
        process_jobs(w);
        pthread_mutex_lock(&w->mutex); bool stop=w->stopping; pthread_mutex_unlock(&w->mutex);
        if (stop) break;
        int active;
        CURLMcode rc=curl_multi_perform(multi,&active);
        if (rc!=CURLM_OK) {
            /* Fail running handles into observations instead of spinning. */
            for (;;) {
                pthread_mutex_lock(&w->mutex);
                Job *j=w->jobs; while (j && j->state!=RUNNING) j=j->next;
                pthread_mutex_unlock(&w->mutex);
                if (!j) break;
                finish(w,j,CURLE_RECV_ERROR,false);
            }
        }
        int remaining; CURLMsg *message;
        while ((message=curl_multi_info_read(multi,&remaining))) {
            if (message->msg!=CURLMSG_DONE) continue;
            Job *j=NULL;
            curl_easy_getinfo(message->easy_handle,CURLINFO_PRIVATE,&j);
            if (j) finish(w,j,message->data.result,false);
        }
        process_observations(w,false);
        pthread_mutex_lock(&w->mutex);
        bool retry=false, blocked=persistence_pending(w);
        for (Job *j=w->jobs;j;j=j->next)
            if ((j->state==QUEUED && !blocked) ||
                (j->state==OBSERVING && j->retry_at!=UINT64_MAX)) { retry=true; break; }
        pthread_mutex_unlock(&w->mutex);
        int events;
        if (curl_multi_poll(multi,NULL,0,retry?100:1000,&events)!=CURLM_OK) {
            /* A broken polling backend must not spin or keep accepting work. */
            pthread_mutex_lock(&w->mutex); w->stopping=true; changed(w); pthread_mutex_unlock(&w->mutex);
        }
    }
    for (;;) {
        pthread_mutex_lock(&w->mutex);
        Job *j=w->jobs;
        while (j && (j->state==READY || j->state==OBSERVING)) j=j->next;
        pthread_mutex_unlock(&w->mutex);
        if (!j) break;
        finish(w,j,CURLE_ABORTED_BY_CALLBACK,true);
    }
    process_observations(w,true);
    pthread_mutex_lock(&w->mutex); w->multi=NULL; changed(w); pthread_mutex_unlock(&w->mutex);
    curl_multi_cleanup(multi);
    return NULL;
}

CettaHttpWorkerStatus cetta_http_worker_new(const CettaHttpWorkerLimits *limits,
        const CettaHttpWorkerHooks *hooks, CettaHttpWorker **out) {
    if (!out) return HTTP_WORKER_INVALID;
    *out=NULL;
    CettaHttpWorkerLimits l=limits?*limits:cetta_http_worker_default_limits();
    if (!l.jobs || l.jobs>65536 || !l.request_bytes || !l.response_bytes ||
        l.request_bytes>INT_MAX || l.response_bytes>INT_MAX ||
        (hooks && (!hooks->prepare || !hooks->observe || !hooks->recording_stalled ||
                   hooks->record_attempts>1000))) return HTTP_WORKER_INVALID;
    CettaHttpWorker *w=calloc(1,sizeof(*w));
    if (!w) return HTTP_WORKER_NOMEM;
    w->limits=l; if (hooks) w->hooks=*hooks;
    if (!w->hooks.record_attempts) w->hooks.record_attempts=5;
    if (pthread_mutex_init(&w->mutex,NULL)) { free(w); return HTTP_WORKER_NOMEM; }
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr)) { pthread_mutex_destroy(&w->mutex); free(w); return HTTP_WORKER_NOMEM; }
    int rc=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
    if (!rc) rc=pthread_cond_init(&w->changed,&attr);
    pthread_condattr_destroy(&attr);
    if (rc) { pthread_mutex_destroy(&w->mutex); free(w); return HTTP_WORKER_UNAVAILABLE; }
    pthread_once(&curl_once,init_curl);
    if (pthread_create(&w->thread,NULL,owner,w)) {
        pthread_cond_destroy(&w->changed); pthread_mutex_destroy(&w->mutex); free(w);
        return HTTP_WORKER_UNAVAILABLE;
    }
    pthread_mutex_lock(&w->mutex);
    while (!w->initialized) pthread_cond_wait(&w->changed,&w->mutex);
    bool available=w->available; pthread_mutex_unlock(&w->mutex);
    if (!available) { cetta_http_worker_free(w); return HTTP_WORKER_UNAVAILABLE; }
    *out=w; return HTTP_WORKER_OK;
}

static CettaHttpWorkerStatus measure_request(const CettaHttpRequest *r, size_t *bytes) {
    if (!r || !r->id || !r->method || !*r->method || !r->url ||
        (strncasecmp(r->url,"http://",7) && strncasecmp(r->url,"https://",8)) ||
        r->header_count>256 || (r->header_count && !r->headers) ||
        (r->body_size && !r->body) || r->max_response_bytes>=INT_MAX ||
        r->timeout_ms>INT_MAX || r->body_size>INT_MAX) return HTTP_WORKER_INVALID;
    size_t method_size=strnlen(r->method,32), url_size=strnlen(r->url,8193);
    if (method_size>31 || url_size>8192) return HTTP_WORKER_INVALID;
    for (size_t i=0;i<method_size;++i) {
        unsigned char c=(unsigned char)r->method[i];
        if (c<=32 || c>=127 || strchr("()<>@,;:\\\"/[]?={}",c)) return HTTP_WORKER_INVALID;
    }
    size_t total=sizeof(Job)+(r->header_count+1)*sizeof(char *)+method_size+url_size+r->body_size+3;
    const char *options[]={r->proxy,r->ca_file};
    for (size_t i=0;i<2;++i) if (options[i]) {
        size_t n=strnlen(options[i],4097);
        if (n>4096 || strpbrk(options[i],"\r\n")) return HTTP_WORKER_INVALID;
        total+=n+1;
    }
    for (size_t i=0;i<r->header_count;++i) {
        const char *h=r->headers[i];
        if (!h || strnlen(h,8193)>8192 || !strchr(h,':') || strpbrk(h,"\r\n")) return HTTP_WORKER_INVALID;
        total+=strlen(h)+1;
    }
    *bytes=total; return HTTP_WORKER_OK;
}

static CettaHttpWorkerStatus clone_request(const CettaHttpRequest *r, size_t bytes, Job **out) {
    *out=NULL;
    Job *j=calloc(1,sizeof(*j));
    if (!j) return HTTP_WORKER_NOMEM;
    j->request=*r; j->request.method=strdup(r->method); j->request.url=strdup(r->url);
    j->request.proxy=r->proxy?strdup(r->proxy):NULL;
    j->request.ca_file=r->ca_file?strdup(r->ca_file):NULL;
    char **headers=calloc(r->header_count+1,sizeof(*headers)); j->request.headers=(const char *const *)headers;
    j->request.header_count=0;
    unsigned char *body=malloc(r->body_size+1); j->request.body=body;
    if (!j->request.method || !j->request.url || !headers || !body ||
        (r->proxy && !j->request.proxy) || (r->ca_file && !j->request.ca_file)) {
        free_job(j); return HTTP_WORKER_NOMEM;
    }
    if (r->body_size) memcpy(body,r->body,r->body_size);
    body[r->body_size]=0;
    for (size_t i=0;i<r->header_count;++i) {
        headers[i]=strdup(r->headers[i]);
        if (!headers[i]) { free_job(j); return HTTP_WORKER_NOMEM; }
        ++j->request.header_count;
    }
    j->result.id=r->id; j->request_bytes=bytes;
    *out=j; return HTTP_WORKER_OK;
}

CettaHttpWorkerStatus cetta_http_worker_submit(CettaHttpWorker *w, const CettaHttpRequest *request) {
    if (!w) return HTTP_WORKER_INVALID;
    Job *j=NULL; size_t bytes=0;
    CettaHttpWorkerStatus status=measure_request(request,&bytes);
    if (status!=HTTP_WORKER_OK) return status;
    pthread_mutex_lock(&w->mutex);
    bool reserved=false;
    for (Reservation *r=w->reservations;r;r=r->next)
        if (r->id==request->id) { reserved=true; break; }
    if (w->stopping) status=HTTP_WORKER_CLOSED;
    else if (reserved || find_job(w,request->id)) status=HTTP_WORKER_DUPLICATE;
    else if (w->count>=w->limits.jobs || bytes>w->limits.request_bytes-w->request_bytes)
        status=HTTP_WORKER_FULL;
    if (status!=HTTP_WORKER_OK) { pthread_mutex_unlock(&w->mutex); return status; }
    Reservation reservation={request->id,w->reservations}; w->reservations=&reservation;
    ++w->count; w->request_bytes+=bytes;
    pthread_mutex_unlock(&w->mutex);
    /* Reserve bounds and identity before copying; do not block receive on a
     * potentially large allocation/copy. free excludes concurrent API users. */
    status=clone_request(request,bytes,&j);
    pthread_mutex_lock(&w->mutex);
    Reservation **link=&w->reservations;
    while (*link!=&reservation) link=&(*link)->next;
    *link=reservation.next;
    if (w->stopping) status=HTTP_WORKER_CLOSED;
    if (status==HTTP_WORKER_OK) {
        j->owner=w;
        if (w->tail) w->tail->next=j; else w->jobs=j;
        w->tail=j;
        wake(w);
    } else { --w->count; w->request_bytes-=bytes; if (j) free_job(j); changed(w); }
    pthread_mutex_unlock(&w->mutex);
    return status;
}

bool cetta_http_worker_abandon(CettaHttpWorker *w, uint64_t id) {
    if (!w) return false;
    pthread_mutex_lock(&w->mutex);
    Job *j=find_job(w,id);
    bool accepted=j && !w->hooks.observe;
    if (accepted) {
        j->cancel=true; j->abandon=true;
        if (j->state==READY) retire(w,j);
        wake(w);
    }
    pthread_mutex_unlock(&w->mutex); return accepted;
}

CettaHttpCancel cetta_http_worker_cancel(CettaHttpWorker *w,uint64_t id) {
    if (!w) return HTTP_CANCEL_UNKNOWN;
    pthread_mutex_lock(&w->mutex);
    Job *j=find_job(w,id);
    CettaHttpCancel result=HTTP_CANCEL_UNKNOWN;
    if (j) {
        if (j->state==OBSERVING || j->state==READY) result=HTTP_CANCEL_TOO_LATE;
        else {
            j->cancel=true;
            result=j->result.started?HTTP_CANCEL_REQUESTED:HTTP_CANCEL_NOT_STARTED;
            wake(w);
        }
    }
    pthread_mutex_unlock(&w->mutex); return result;
}

bool cetta_http_worker_resume_recording(CettaHttpWorker *w,uint64_t id) {
    if (!w) return false;
    pthread_mutex_lock(&w->mutex);
    Job *j=find_job(w,id);
    bool resumed=!w->stopping && j && j->state==OBSERVING && j->retry_at==UINT64_MAX;
    if (resumed) {
        j->record_mode=HTTP_RECORD_FULL;
        j->record_attempts=0; j->retry_at=0; wake(w);
    }
    pthread_mutex_unlock(&w->mutex); return resumed;
}

bool cetta_http_worker_take(CettaHttpWorker *w, CettaHttpResult *out) {
    if (!w || !out || w->hooks.observe) return false;
    pthread_mutex_lock(&w->mutex);
    Job *j=w->jobs; while (j && j->state!=READY) j=j->next;
    bool taken=j!=NULL;
    if (j) {
        *out=j->result; j->result.body=NULL; j->result.body_size=0;
        retire(w,j);
    }
    pthread_mutex_unlock(&w->mutex); return taken;
}

uint64_t cetta_http_worker_wait(CettaHttpWorker *w,uint64_t generation,uint32_t timeout_ms) {
    if (!w) return generation;
    struct timespec deadline; clock_gettime(CLOCK_MONOTONIC,&deadline);
    deadline.tv_sec+=timeout_ms/1000; deadline.tv_nsec+=(long)(timeout_ms%1000)*1000000;
    if (deadline.tv_nsec>=1000000000) { ++deadline.tv_sec; deadline.tv_nsec-=1000000000; }
    pthread_mutex_lock(&w->mutex);
    while (w->generation==generation)
        if (pthread_cond_timedwait(&w->changed,&w->mutex,&deadline)) break;
    uint64_t result=w->generation; pthread_mutex_unlock(&w->mutex); return result;
}

size_t cetta_http_worker_free(CettaHttpWorker *w) {
    if (!w) return 0;
    pthread_mutex_lock(&w->mutex); w->stopping=true; wake(w); changed(w); pthread_mutex_unlock(&w->mutex);
    pthread_join(w->thread,NULL);
    size_t unacknowledged=w->unacknowledged;
    Job *j=w->jobs; while (j) { Job *next=j->next; free_job(j); j=next; }
    pthread_cond_destroy(&w->changed); pthread_mutex_destroy(&w->mutex); free(w);
    return unacknowledged;
}
#else
CettaHttpWorkerStatus cetta_http_worker_new(const CettaHttpWorkerLimits *l,
    const CettaHttpWorkerHooks *h,CettaHttpWorker **out) {
    (void)l; (void)h; if (out) *out=NULL; return HTTP_WORKER_UNAVAILABLE;
}
CettaHttpWorkerStatus cetta_http_worker_submit(CettaHttpWorker *w,const CettaHttpRequest *r) {
    (void)w; (void)r; return HTTP_WORKER_UNAVAILABLE;
}
CettaHttpCancel cetta_http_worker_cancel(CettaHttpWorker *w,uint64_t id) { (void)w; (void)id; return HTTP_CANCEL_UNKNOWN; }
bool cetta_http_worker_resume_recording(CettaHttpWorker *w,uint64_t id) { (void)w; (void)id; return false; }
bool cetta_http_worker_abandon(CettaHttpWorker *w,uint64_t id) { (void)w; (void)id; return false; }
bool cetta_http_worker_take(CettaHttpWorker *w,CettaHttpResult *r) { (void)w; (void)r; return false; }
uint64_t cetta_http_worker_wait(CettaHttpWorker *w,uint64_t g,uint32_t ms) { (void)w; (void)ms; return g; }
size_t cetta_http_worker_free(CettaHttpWorker *w) { (void)w; return 0; }
#endif
