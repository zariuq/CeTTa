#define _POSIX_C_SOURCE 200809L
#include "durable_store.h"
#include "native_journal.h"
#include "space.h"
#include "term_universe.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRAME_BASE 53u
#define ROW_BASE 29u
#define FRAME_OVERHEAD 72u

typedef struct {
    Arena arena;
    TermUniverse universe;
    Space *space;
    int64_t live_bytes;
} Image;

struct CettaDurableStore {
    int fd, lock_fd;
    char *path;
    pthread_mutex_t mutex;
    CettaDurableLimits limits;
    Image *image;
    char epoch[33];
    int64_t revision, history;
    uint64_t file_bytes;
    bool poisoned, reclaim;
    const void *runtime_owner;
};

#ifdef CETTA_DURABLE_TEST
extern void cetta_durable_test_boundary(const char *boundary);
#define BOUNDARY(name) cetta_durable_test_boundary(name)
#else
#define BOUNDARY(name) ((void)0)
#endif

static bool over(uint64_t value, uint64_t limit) { return limit && value>limit; }
static bool valid_name(const char *s) {
    return s && *s && strnlen(s,(size_t)CETTA_STRING_BYTES_MAX+1)<=CETTA_STRING_BYTES_MAX;
}
const char *cetta_durable_status_name(CettaDurableStatus s) {
    static const char *names[]={"ok","invalid","busy","conflict","precondition","limit",
        "allocation","io","corrupt","version","unknown-outcome","poisoned"};
    return (unsigned)s<sizeof(names)/sizeof(*names)?names[s]:"invalid";
}
CettaDurableLimits cetta_durable_default_limits(void) { return (CettaDurableLimits){0}; }
CettaDurableLimits cetta_durable_bounded_limits(void) {
    return (CettaDurableLimits){
        .record_bytes=1024*1024, .batch_bytes=4*1024*1024,
        .live_bytes=64*1024*1024, .history_bytes=16*1024*1024,
        .records=100000, .operations=4096, .storage_pages=65536
    };
}
static void image_free(Image *im) {
    if (!im) return;
    if (im->space) { space_free(im->space); free(im->space); }
    term_universe_free(&im->universe); arena_free(&im->arena); free(im);
}
static Image *image_new(void) {
    Image *im=calloc(1,sizeof(*im)); if (!im) return NULL;
    arena_init_detached(&im->arena); term_universe_init(&im->universe);
    term_universe_set_persistent_arena(&im->universe,&im->arena);
    im->space=calloc(1,sizeof(*im->space));
    if (!im->space) { image_free(im); return NULL; }
    space_init_with_universe(im->space,&im->universe); return im;
}
/* Only inert, ground string/integer tuples enter this private native space.
 * Payload strings are binary-safe and are never interpreted as programs. */
static CettaDurableRecord row(Atom *a) {
    Atom **e=a->expr.elems;
    return (CettaDurableRecord){(char *)e[0]->ground.sval,(char *)e[1]->ground.sval,
        (unsigned char *)e[2]->ground.sval,atom_string_len(e[2]),e[3]->ground.ival,e[4]->ground.ival};
}
static Atom *find_row(const Space *space, const char *name, const char *key, CettaIndex *index) {
    for (CettaIndex i=0;i<space_length64(space);++i) {
        Atom *a=space_get_at64(space,i); CettaDurableRecord r=row(a);
        if (!strcmp(r.space,name) && !strcmp(r.key,key)) { if (index) *index=i; return a; }
    }
    return NULL;
}
static bool image_add_to(Image *im, Space *space, const CettaDurableRecord *r) {
    Atom *e[]={atom_string(&im->arena,r->space),atom_string(&im->arena,r->key),
        atom_string_n(&im->arena,(const char *)r->data,r->size),
        atom_int(&im->arena,r->revision),atom_int(&im->arena,r->position)};
    return space_admit_atom(space,&im->arena,atom_expr(&im->arena,e,5));
}
static bool image_add(Image *im,const CettaDurableRecord *r) { return image_add_to(im,im->space,r); }
static bool matches(const CettaDurableRecord *r,const char *space,const CettaDurableScope *q) {
    return (!space || !strcmp(space,r->space)) && (!q || q->kind==DURABLE_SPACE ||
        (q->kind==DURABLE_KEY ? !strcmp(q->key,r->key) : !strncmp(q->key,r->key,strlen(q->key))));
}
static int compare_rows(const void *left,const void *right) {
    const CettaDurableRecord *a=left,*b=right;
    if (a->revision!=b->revision) return a->revision<b->revision?-1:1;
    if (a->position!=b->position) return a->position<b->position?-1:1;
    int n=strcmp(a->space?a->space:"",b->space?b->space:""); return n?n:strcmp(a->key,b->key);
}
void cetta_durable_snapshot_free(CettaDurableSnapshot *v) {
    if (!v) return;
    for (size_t i=0;i<v->count;++i) { free(v->records[i].space); free(v->records[i].key); free(v->records[i].data); }
    free(v->records); memset(v,0,sizeof(*v));
}
static CettaDurableStatus snapshot_locked(CettaDurableStore *s, Image *im,
        const char *space,const CettaDurableScope *scope,bool metadata,CettaDurableSnapshot *out) {
    memset(out,0,sizeof(*out)); memcpy(out->epoch,s->epoch,33); out->revision=s->revision;
    BOUNDARY("read_meta");
    size_t capacity=0;
    for (CettaIndex i=0;i<space_length64(im->space);++i) {
        CettaDurableRecord src=row(space_get_at64(im->space,i));
        if (!matches(&src,space,scope)) continue;
        if (out->count==capacity) {
            size_t next=capacity?capacity*2:16;
            if (next<capacity || next>SIZE_MAX/sizeof(*out->records)) goto nomem;
            void *p=realloc(out->records,next*sizeof(*out->records)); if (!p) goto nomem;
            out->records=p; capacity=next;
        }
        CettaDurableRecord *r=&out->records[out->count++];
        *r=(CettaDurableRecord){.key=strdup(src.key),.revision=src.revision,.position=src.position};
        if (!r->key) goto nomem;
        if (!metadata) {
            r->space=strdup(src.space); r->data=malloc(src.size?src.size:1); r->size=src.size;
            if (!r->space || !r->data) goto nomem;
            if (src.size) memcpy(r->data,src.data,src.size);
        }
    }
    if (out->count>1) qsort(out->records,out->count,sizeof(*out->records),compare_rows);
    return DURABLE_OK;
nomem:
    cetta_durable_snapshot_free(out); return DURABLE_NOMEM;
}

/* The payload format is CDR1: kind, epoch, revision, row count, then typed
 * byte records. Checkpoints stream one row per frame, so their size is not
 * restricted to one frame. Every delta frame is a complete atomic batch. */
static void put_u(unsigned char **p,uint64_t n,size_t width) {
    for (size_t i=0;i<width;++i) *(*p)++=(unsigned char)(n>>(8*(width-i-1)));
}
static uint64_t get_u(const unsigned char **p,size_t width) {
    uint64_t n=0; for (size_t i=0;i<width;++i) n=(n<<8)|*(*p)++; return n;
}
static unsigned char *frame_new(unsigned kind,const char epoch[33],int64_t revision,uint64_t count,size_t size) {
    unsigned char *b=malloc(size); if (!b) return NULL;
    unsigned char *p=b; memcpy(p,"CDR1",4); p+=4; *p++=(unsigned char)kind;
    memcpy(p,epoch,32); p+=32; put_u(&p,(uint64_t)revision,8); put_u(&p,count,8); return b;
}
static size_t row_size(const CettaDurableRecord *r) { return ROW_BASE+strlen(r->space)+strlen(r->key)+r->size; }
static void frame_row(unsigned char **p,unsigned kind,const CettaDurableRecord *r) {
    *(*p)++=(unsigned char)kind; put_u(p,strlen(r->space),4); put_u(p,strlen(r->key),4); put_u(p,r->size,4);
    put_u(p,(uint64_t)r->revision,8); put_u(p,(uint64_t)r->position,8);
    size_t n=strlen(r->space); memcpy(*p,r->space,n); *p+=n;
    n=strlen(r->key); memcpy(*p,r->key,n); *p+=n;
    if (r->size) { memcpy(*p,r->data,r->size); *p+=r->size; }
}
/* A frame the journal cannot represent is a limit of this store. A write
 * that entered the journal boundary is an I/O failure here; a commit reports
 * it as UNKNOWN and poisons the store. */
static CettaDurableStatus journal_status(CettaJournalStatus r) {
    switch (r) {
    case CETTA_JOURNAL_OK: return DURABLE_OK;
    case CETTA_JOURNAL_NOMEM: return DURABLE_NOMEM;
    case CETTA_JOURNAL_INVALID: return DURABLE_LIMIT;
    case CETTA_JOURNAL_IO: case CETTA_JOURNAL_UNKNOWN: return DURABLE_IO;
    default: return DURABLE_CORRUPT;
    }
}
static bool epoch_valid(const unsigned char *e) {
    for (size_t i=0;i<32;++i) if (!((e[i]>='0'&&e[i]<='9') || (e[i]>='a'&&e[i]<='f'))) return false;
    return true;
}
/* Key uniqueness belongs to a batch, including remove-then-insert. Checking
 * the resulting live space alone would forget a key removed earlier here. */
static bool frame_keys_unique(const unsigned char *p,const unsigned char *end,uint64_t count) {
    const unsigned char *begin=p;
    for (uint64_t i=0;i<count;++i) {
        if ((size_t)(end-p)<ROW_BASE) return false;
        const unsigned char *header=p++; uint64_t ns=get_u(&p,4),nk=get_u(&p,4),nv=get_u(&p,4);
        p+=16;
        if (ns+nk+nv>(uint64_t)(end-p)) return false;
        const unsigned char *prior=begin;
        while (prior<header) {
            ++prior; uint64_t ps=get_u(&prior,4),pk=get_u(&prior,4),pv=get_u(&prior,4); prior+=16;
            if (ps==ns && pk==nk && !memcmp(prior,p,(size_t)(ns+nk))) return false;
            prior+=ps+pk+pv;
        }
        p+=ns+nk+nv;
    }
    return p==end;
}
static CettaDurableStatus read_image(int fd,bool trim,Image **out,char epoch[33],
        int64_t *revision,int64_t *history,uint64_t *file_bytes) {
    *out=NULL; Image *im=image_new(); if (!im) return DURABLE_NOMEM;
    struct stat info;
    if (fstat(fd,&info) || info.st_size<0) { image_free(im); return DURABLE_IO; }
    uint64_t length=(uint64_t)info.st_size,position=0; bool first=true; uint64_t remaining=0; *history=0;
    CettaDurableStatus result=DURABLE_CORRUPT;
    for (;;) {
        CettaJournalRecord frame={0};
        CettaJournalStatus st=cetta_journal_read(fd,position,length,SIZE_MAX,&frame);
        if (st==CETTA_JOURNAL_END || st==CETTA_JOURNAL_TORN) {
            if (first || remaining) break;
            if (st==CETTA_JOURNAL_TORN && trim &&
                cetta_journal_trim_tail(fd,position)!=CETTA_JOURNAL_OK) { result=DURABLE_IO; break; }
            *out=im; *file_bytes=position; return DURABLE_OK;
        }
        if (st!=CETTA_JOURNAL_OK) { result=journal_status(st); break; }
        unsigned char *bytes=frame.data; size_t size=frame.size; uint64_t next=frame.next;
        if (size<FRAME_BASE || memcmp(bytes,"CDR1",4)) {
            result=first?DURABLE_VERSION:DURABLE_CORRUPT; free(bytes); break;
        }
        const unsigned char *p=bytes+4,*end=bytes+size; unsigned kind=*p++;
        if (!epoch_valid(p)) { free(bytes); break; }
        if (first) { memcpy(epoch,p,32); epoch[32]=0; } else if (memcmp(epoch,p,32)) { free(bytes); break; }
        p+=32; uint64_t rev=get_u(&p,8), count=get_u(&p,8);
        if (rev>INT64_MAX || (first && kind!=0) || (!first && kind==0) || kind>2) { free(bytes); break; }
        if (kind==0) {
            if (p!=end) { free(bytes); break; }
            *revision=(int64_t)rev; remaining=count; first=false;
        } else {
            if ((kind==2 && (!remaining || rev!=(uint64_t)*revision || count!=1)) ||
                (kind==1 && (remaining || *revision==INT64_MAX || rev!=(uint64_t)*revision+1 || !count)) ||
                count>(uint64_t)(end-p)/ROW_BASE || !frame_keys_unique(p,end,count)) { free(bytes); break; }
            bool valid=true;
            for (uint64_t i=0;i<count && valid;++i) {
                if ((size_t)(end-p)<ROW_BASE) { valid=false; break; }
                unsigned op=*p++; uint64_t ns=get_u(&p,4),nk=get_u(&p,4),nv=get_u(&p,4);
                uint64_t rv=get_u(&p,8), pos=get_u(&p,8);
                if (!ns || ns>CETTA_STRING_BYTES_MAX || !nk || nk>CETTA_STRING_BYTES_MAX ||
                    nv>CETTA_STRING_BYTES_MAX || ns+nk+nv>(uint64_t)(end-p) ||
                    !rv || rv>rev || pos>INT64_MAX || op<DURABLE_INSERT || op>DURABLE_REMOVE ||
                    (kind==2 && op!=DURABLE_INSERT) || (kind==1 && (rv!=rev || pos!=i)) ||
                    (op==DURABLE_REMOVE && nv) || memchr(p,0,(size_t)(ns+nk))) { valid=false; break; }
                char *name=strndup((const char *)p,(size_t)ns); p+=ns;
                char *key=strndup((const char *)p,(size_t)nk); p+=nk;
                if (!name || !key) { free(name); free(key); result=DURABLE_NOMEM; valid=false; break; }
                CettaIndex index=0; Atom *found=find_row(im->space,name,key,&index);
                if ((op==DURABLE_INSERT)==(found!=NULL)) valid=false;
                if (valid && found) {
                    CettaDurableRecord old=row(found); im->live_bytes-=(int64_t)(old.size+ns+nk);
                    if (!space_remove_atom_id(im->space,space_get_atom_id_at64(im->space,index))) valid=false;
                }
                if (valid && op!=DURABLE_REMOVE) {
                    CettaDurableRecord r={name,key,(unsigned char *)p,(size_t)nv,(int64_t)rv,(int64_t)pos};
                    if (ns+nk+nv>(uint64_t)(INT64_MAX-im->live_bytes) || !image_add(im,&r)) valid=false;
                    if (valid) im->live_bytes+=(int64_t)(ns+nk+nv);
                }
                free(name); free(key);
                if (valid && kind==1) {
                    if (ns+nk+nv>(uint64_t)(INT64_MAX-*history)) valid=false;
                    if (valid) *history+=(int64_t)(ns+nk+nv);
                }
                p+=nv;
            }
            if (!valid || p!=end) { free(bytes); break; }
            if (kind==2) --remaining; else *revision=(int64_t)rev;
        }
        free(bytes); position=next;
    }
    image_free(im); return result;
}
static int sync_parent(const char *path) {
    char *dir=strdup(path); if (!dir) return -1;
    char *slash=strrchr(dir,'/');
    if (slash) { if (slash==dir) slash[1]=0; else *slash=0; }
    else { free(dir); dir=strdup("."); if (!dir) return -1; }
    int fd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC); free(dir);
    int rc=fd<0?-1:fsync(fd); if (fd>=0) close(fd); return rc;
}
static char *side_path(const char *path,const char *suffix) {
    size_t n=strlen(path),m=strlen(suffix); if (n>SIZE_MAX-m-1) return NULL;
    char *p=malloc(n+m+1); if (p) { memcpy(p,path,n); memcpy(p+n,suffix,m+1); } return p;
}
CettaDurableStatus cetta_durable_open(const char *path,const CettaDurableLimits *limits,CettaDurableStore **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL; if (!path || !*path || !strcmp(path,":memory:")) return DURABLE_INVALID;
    CettaDurableStore *s=calloc(1,sizeof(*s)); if (!s) return DURABLE_NOMEM;
    s->fd=s->lock_fd=-1; s->limits=limits?*limits:cetta_durable_default_limits(); s->path=strdup(path);
    if (pthread_mutex_init(&s->mutex,NULL)) { free(s->path); free(s); return DURABLE_NOMEM; }
    char *lock=side_path(path,".lock"); CettaDurableStatus r=DURABLE_NOMEM;
    if (!s->path || !lock) { free(lock); goto fail; }
    s->lock_fd=open(lock,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600); free(lock);
    struct stat st; r=DURABLE_IO;
    if (s->lock_fd<0 || fstat(s->lock_fd,&st) || !S_ISREG(st.st_mode)) goto fail;
    if (st.st_nlink!=1) { r=DURABLE_INVALID; goto fail; }
    if (flock(s->lock_fd,LOCK_EX|LOCK_NB)) { r=errno==EWOULDBLOCK?DURABLE_BUSY:DURABLE_IO; goto fail; }
    s->fd=open(path,O_RDWR|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    if (s->fd<0 || fstat(s->fd,&st) || !S_ISREG(st.st_mode)) goto fail;
    if (st.st_nlink!=1) { r=DURABLE_INVALID; goto fail; }
    if (!st.st_size) {
        unsigned char seed[16]; size_t used=0; int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
        if (random<0) goto fail;
        while (used<sizeof(seed)) {
            ssize_t n=read(random,seed+used,sizeof(seed)-used);
            if (n<0 && errno==EINTR) continue;
            if (n<=0) break;
            used+=(size_t)n;
        }
        close(random); if (used!=sizeof(seed)) goto fail;
        for (size_t i=0;i<sizeof(seed);++i) snprintf(s->epoch+2*i,3,"%02x",seed[i]);
        unsigned char *b=frame_new(0,s->epoch,0,0,FRAME_BASE); if (!b) { r=DURABLE_NOMEM; goto fail; }
        r=journal_status(cetta_journal_append(s->fd,b,FRAME_BASE,true,&s->file_bytes)); free(b);
        if (r!=DURABLE_OK) goto fail;
    } else {
        unsigned char magic[4];
        if (pread(s->fd,magic,4,0)==4 && memcmp(magic,"CSL1",4)) { r=DURABLE_VERSION; goto fail; }
    }
    r=read_image(s->fd,true,&s->image,s->epoch,&s->revision,&s->history,&s->file_bytes);
    if (r!=DURABLE_OK) goto fail;
    if (sync_parent(path)) { r=DURABLE_IO; goto fail; }
    *out=s; return DURABLE_OK;
fail:
    cetta_durable_close(s); return r;
}
void cetta_durable_close(CettaDurableStore *s) {
    if (!s) return;
    image_free(s->image); if (s->fd>=0) close(s->fd); if (s->lock_fd>=0) close(s->lock_fd);
    pthread_mutex_destroy(&s->mutex); free(s->path); free(s);
}
CettaDurableStatus cetta_durable_attach_runtime(CettaDurableStore *s,const void *owner) {
    if (!s || !owner) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus r=s->poisoned?DURABLE_POISONED:s->runtime_owner?DURABLE_BUSY:DURABLE_OK;
    if (r==DURABLE_OK) s->runtime_owner=owner;
    pthread_mutex_unlock(&s->mutex); return r;
}
CettaDurableStatus cetta_durable_detach_runtime(CettaDurableStore *s,const void *owner) {
    if (!s || !owner) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex); CettaDurableStatus r=s->runtime_owner==owner?DURABLE_OK:DURABLE_INVALID;
    if (r==DURABLE_OK) s->runtime_owner=NULL;
    pthread_mutex_unlock(&s->mutex); return r;
}
CettaDurableStatus cetta_durable_snapshot(CettaDurableStore *s,const char *space,CettaDurableSnapshot *out) {
    if (!s || !out || (space && !valid_name(space))) return DURABLE_INVALID;
    memset(out,0,sizeof(*out)); pthread_mutex_lock(&s->mutex);
    CettaDurableStatus r=s->poisoned?DURABLE_POISONED:snapshot_locked(s,s->image,space,NULL,false,out);
    pthread_mutex_unlock(&s->mutex); return r;
}
/* Correct constant-materialization fallback over the native image. Queue
 * partition/ordering indexes can refine this without changing CWP semantics. */
CettaDurableStatus cetta_durable_next_in_partition(CettaDurableStore *s,
        const char *space,const char *partition,const char *after,int64_t minimum,
        CettaDurableSnapshot *out) {
    if (!s || !out || !valid_name(space) || !valid_name(partition) || minimum < -1) return DURABLE_INVALID;
    size_t length=strlen(partition);
    if (length<2 || strchr(partition,'/')!=partition+length-1 ||
        (after && (!valid_name(after) || strncmp(after,partition,length)))) return DURABLE_INVALID;
    memset(out,0,sizeof(*out));pthread_mutex_lock(&s->mutex);
    CettaDurableStatus status=s->poisoned?DURABLE_POISONED:DURABLE_OK;
    memcpy(out->epoch,s->epoch,33);out->revision=s->revision;
    int64_t revision=-1,position=-1;
    if (status==DURABLE_OK && after) {
        Atom *a=find_row(s->image->space,space,after,NULL);
        if (!a) status=DURABLE_PRECONDITION;
        else {CettaDurableRecord r=row(a);revision=r.revision;position=r.position;}
    }
    CettaDurableRecord best={0};bool found=false;
    for (CettaIndex i=0;status==DURABLE_OK && i<space_length64(s->image->space);++i) {
        CettaDurableRecord r=row(space_get_at64(s->image->space,i));
        if (strcmp(r.space,space) || strncmp(r.key,partition,length) || r.revision<=minimum ||
            r.revision<revision || (r.revision==revision && r.position<=position)) continue;
        if (!found || compare_rows(&r,&best)<0) {best=r;found=true;}
    }
    if (status==DURABLE_OK && found) {
        out->records=calloc(1,sizeof(*out->records));
        if (!out->records) status=DURABLE_NOMEM;
        else {
            out->count=1;CettaDurableRecord *r=out->records;
            *r=best;r->space=strdup(best.space);r->key=strdup(best.key);r->data=malloc(best.size?best.size:1);
            if (!r->space || !r->key || !r->data) status=DURABLE_NOMEM;
            else if (best.size) memcpy(r->data,best.data,best.size);
        }
    }
    if (status!=DURABLE_OK) cetta_durable_snapshot_free(out);
    pthread_mutex_unlock(&s->mutex);return status;
}

CettaDurableStatus cetta_durable_recover(CettaDurableStore *s,CettaDurableSnapshot *out) {
    if (!s || !out) return DURABLE_INVALID;
    memset(out,0,sizeof(*out)); pthread_mutex_lock(&s->mutex);
    Image *im=NULL; char epoch[33]; int64_t rev=0,history=0; uint64_t bytes=0;
    CettaDurableStatus r=s->poisoned?DURABLE_POISONED:read_image(s->fd,false,&im,epoch,&rev,&history,&bytes);
    if (r==DURABLE_OK && (strcmp(epoch,s->epoch) || rev!=s->revision)) r=DURABLE_CORRUPT;
    if (r==DURABLE_OK) r=snapshot_locked(s,im,NULL,NULL,false,out);
    if (r==DURABLE_CORRUPT) s->poisoned=true;
    image_free(im); pthread_mutex_unlock(&s->mutex); return r;
}
CettaDurableStatus cetta_durable_usage(CettaDurableStore *s,CettaDurableUsage *out) {
    if (!s || !out) return DURABLE_INVALID;
    memset(out,0,sizeof(*out)); pthread_mutex_lock(&s->mutex);
    CettaDurableStatus r=s->poisoned?DURABLE_POISONED:DURABLE_OK;
    if (r==DURABLE_OK) {
        out->live_bytes=s->image->live_bytes; out->records=(int64_t)space_length64(s->image->space);
        out->history_bytes=s->history; out->storage_pages=(int64_t)((s->file_bytes+4095)/4096);
        out->limits_exceeded=over((uint64_t)out->live_bytes,s->limits.live_bytes) ||
            over((uint64_t)out->records,s->limits.records) || over((uint64_t)s->history,s->limits.history_bytes) ||
            over((uint64_t)out->storage_pages,s->limits.storage_pages);
        for (CettaIndex i=0;i<space_length64(s->image->space);++i)
            if (over(row(space_get_at64(s->image->space,i)).size,s->limits.record_bytes)) out->limits_exceeded=1;
    }
    pthread_mutex_unlock(&s->mutex); return r;
}
struct CettaDurableObservation {
    size_t count;
    CettaDurableScope *scopes;
    CettaDurableSnapshot *views;
};

void cetta_durable_observation_free(CettaDurableObservation *o) {
    if (!o) return;
    for (size_t i=0;i<o->count;++i) {
        if (o->scopes) { free((char *)o->scopes[i].space); free((char *)o->scopes[i].key); }
        if (o->views) cetta_durable_snapshot_free(&o->views[i]);
    }
    free(o->scopes); free(o->views); free(o);
}

const CettaDurableSnapshot *cetta_durable_observation_view(const CettaDurableObservation *o, size_t i) {
    return o && i<o->count ? &o->views[i] : NULL;
}

static bool record_identity(const CettaDurableRecord *record, const char *key, int64_t revision) {
    return !strcmp(record->key,key) && record->revision==revision;
}

static bool view_identity(const CettaDurableSnapshot *saved, const CettaDurableSnapshot *current) {
    if (strcmp(saved->epoch,current->epoch) || saved->count!=current->count) return false;
    for (size_t i=0;i<saved->count;++i)
        if (!record_identity(&saved->records[i],current->records[i].key,current->records[i].revision))
            return false;
    return true;
}

static CettaDurableStatus observe_joined(CettaDurableStore *s,
        const CettaDurableObservation *required, const CettaDurableScope *scopes,
        size_t count, CettaDurableObservation **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!s || (count && !scopes) || (!count && !required)) return DURABLE_INVALID;
    size_t prior=required?required->count:0;
    if (prior>SIZE_MAX-count || over(count+prior,s->limits.operations)) return DURABLE_LIMIT;
    size_t total=count+prior;
    for (size_t i=0;i<count;++i) {
        const CettaDurableScope *q=&scopes[i];
        if (!valid_name(q->space) || q->kind<DURABLE_KEY || q->kind>DURABLE_SPACE ||
            (q->kind==DURABLE_KEY && !valid_name(q->key)) ||
            (q->kind==DURABLE_PREFIX && (!q->key || strnlen(q->key,(size_t)CETTA_STRING_BYTES_MAX+1)>CETTA_STRING_BYTES_MAX))) return DURABLE_INVALID;
    }
    CettaDurableObservation *o=calloc(1,sizeof(*o));
    if (!o) return DURABLE_NOMEM;
    o->count=total; o->scopes=calloc(total,sizeof(*o->scopes)); o->views=calloc(total,sizeof(*o->views));
    if (!o->scopes || !o->views) { cetta_durable_observation_free(o); return DURABLE_NOMEM; }
    for (size_t i=0;i<total;++i) {
        const CettaDurableScope *q=i<count?&scopes[i]:&required->scopes[i-count];
        o->scopes[i]=(CettaDurableScope){q->kind,strdup(q->space),
            strdup(q->kind==DURABLE_SPACE ? "" : q->key)};
        if (!o->scopes[i].space || !o->scopes[i].key) { cetta_durable_observation_free(o); return DURABLE_NOMEM; }
    }
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus result=s->poisoned ? DURABLE_POISONED : DURABLE_OK;
    size_t used=0;
    for (size_t i=0;i<total && result==DURABLE_OK;++i) {
        const CettaDurableScope *q=&o->scopes[i];
        result=snapshot_locked(s,s->image,q->space,q,false,&o->views[i]);
        /* Observations are for decisions, not bulk export. Bound overlapping
         * scopes too; snapshot/read remains available for degraded recovery. */
        for (size_t j=0;j<o->views[i].count && result==DURABLE_OK;++j) {
            CettaDurableRecord *r=&o->views[i].records[j];
            size_t n=r->size+strlen(r->space)+strlen(r->key)+sizeof(*r);
            if (n>SIZE_MAX-used || over(used+n,s->limits.live_bytes)) result=DURABLE_LIMIT; else used+=n;
        }
        if (result==DURABLE_OK && i>=count &&
            !view_identity(&required->views[i-count],&o->views[i])) result=DURABLE_CONFLICT;
    }
    pthread_mutex_unlock(&s->mutex);
    if (result==DURABLE_OK) *out=o; else cetta_durable_observation_free(o);
    return result;
}

CettaDurableStatus cetta_durable_observe(CettaDurableStore *s,
        const CettaDurableScope *scopes, size_t count, CettaDurableObservation **out) {
    return observe_joined(s,NULL,scopes,count,out);
}

CettaDurableStatus cetta_durable_observe_joined(CettaDurableStore *s,
        const CettaDurableObservation *required, const CettaDurableScope *scopes,
        size_t count, CettaDurableObservation **out) {
    if (!required) { if (out) *out=NULL; return DURABLE_INVALID; }
    return observe_joined(s,required,scopes,count,out);
}

static bool scope_contains(const CettaDurableScope *q, const CettaDurableOp *op) {
    return !strcmp(q->space,op->space) && (q->kind==DURABLE_SPACE ||
        (q->kind==DURABLE_KEY ? !strcmp(q->key,op->key) : !strncmp(q->key,op->key,strlen(q->key))));
}

static CettaDurableStatus validate_observation(CettaDurableStore *s, const CettaDurableObservation *o) {
    for (size_t i=0;i<o->count;++i) {
        CettaDurableSnapshot current;
        CettaDurableStatus r=snapshot_locked(s,s->image,o->scopes[i].space,&o->scopes[i],true,&current);
        if (r!=DURABLE_OK) return r;
        bool same=view_identity(&o->views[i],&current);
        cetta_durable_snapshot_free(&current);
        if (!same) return DURABLE_CONFLICT;
    }
    return DURABLE_OK;
}

struct CettaDurableWatch { CettaDurableObservation *observation; size_t bytes;
    bool revision_only; char epoch[33]; int64_t revision; size_t *counts; };
void cetta_durable_watch_free(CettaDurableWatch *w) {
    if (w) { cetta_durable_observation_free(w->observation); free(w->counts); free(w); }
}
size_t cetta_durable_watch_bytes(const CettaDurableWatch *w) { return w?w->bytes:0; }
CettaDurableStatus cetta_durable_watch_new(const CettaDurableObservation *o,
        size_t limit, CettaDurableWatch **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!o || !o->count) return DURABLE_INVALID;
    size_t used=sizeof(CettaDurableWatch)+sizeof(*o);
    size_t per_scope=sizeof(*o->scopes)+sizeof(*o->views)+sizeof(size_t);
    if (used>limit || o->count>(limit-used)/per_scope) return DURABLE_LIMIT;
    used+=o->count*per_scope;
    for (size_t i=0;i<o->count;++i) {
        size_t n=strlen(o->scopes[i].space)+strlen(o->scopes[i].key)+2;
        if (n>limit-used) return DURABLE_LIMIT;
        used+=n;
    }
    CettaDurableWatch *w=calloc(1,sizeof(*w));
    if (!w) return DURABLE_NOMEM;
    w->observation=calloc(1,sizeof(*o)); w->bytes=used;
    if (!w->observation) { cetta_durable_watch_free(w); return DURABLE_NOMEM; }
    CettaDurableObservation *copy=w->observation;
    copy->count=o->count; copy->scopes=calloc(o->count,sizeof(*copy->scopes)); copy->views=calloc(o->count,sizeof(*copy->views));
    w->counts=calloc(o->count,sizeof(*w->counts));
    if (!copy->scopes || !copy->views || !w->counts) goto nomem;
    for (size_t i=0;i<o->count;++i) {
        copy->scopes[i]=(CettaDurableScope){o->scopes[i].kind,strdup(o->scopes[i].space),strdup(o->scopes[i].key)};
        if (!copy->scopes[i].space || !copy->scopes[i].key) goto nomem;
        CettaDurableSnapshot *v=&copy->views[i]; const CettaDurableSnapshot *src=&o->views[i];
        memcpy(v->epoch,src->epoch,sizeof(v->epoch)); v->revision=src->revision;
        w->counts[i]=src->count;
    }
    *out=w; return DURABLE_OK;
nomem:
    cetta_durable_watch_free(w); return DURABLE_NOMEM;
}
CettaDurableStatus cetta_durable_watch_revision_new(const CettaDurableObservation *o,
        size_t limit,CettaDurableWatch **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!o || !o->count) return DURABLE_INVALID;
    if (limit<sizeof(CettaDurableWatch)) return DURABLE_LIMIT;
    CettaDurableWatch *w=calloc(1,sizeof(*w));if (!w) return DURABLE_NOMEM;
    w->revision_only=true;w->bytes=sizeof(*w);w->revision=o->views[0].revision;
    memcpy(w->epoch,o->views[0].epoch,sizeof(w->epoch));*out=w;return DURABLE_OK;
}
CettaDurableStatus cetta_durable_watch_current(CettaDurableStore *s,const CettaDurableWatch *w) {
    if (!s || !w) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus result=s->poisoned?DURABLE_POISONED:DURABLE_OK;
    if (result==DURABLE_OK && w->revision_only) {
        if (strcmp(s->epoch,w->epoch) || s->revision!=w->revision) result=DURABLE_CONFLICT;
    } else if (result==DURABLE_OK) {
        for (size_t i=0;i<w->observation->count && result==DURABLE_OK;++i) {
            const CettaDurableSnapshot *v=&w->observation->views[i];
            const CettaDurableScope *scope=&w->observation->scopes[i];size_t count=0;
            if (strcmp(s->epoch,v->epoch)) {result=DURABLE_CONFLICT;break;}
            for (CettaIndex j=0;j<space_length64(s->image->space);++j) {
                CettaDurableRecord r=row(space_get_at64(s->image->space,j));
                if (!matches(&r,scope->space,scope)) continue;
                ++count;
                if (r.revision>v->revision) {result=DURABLE_CONFLICT;break;}
            }
            if (count!=w->counts[i]) result=DURABLE_CONFLICT;
        }
    }
    pthread_mutex_unlock(&s->mutex);return result;
}

/* Build both the checkpoint bytes and its native projection before rename.
 * The stable sidecar lock survives replacement of the journal inode. */
static CettaDurableStatus checkpoint_locked(CettaDurableStore *s) {
    Image *fresh=image_new(); if (!fresh) return DURABLE_NOMEM;
    char *temporary=side_path(s->path,".checkpoint");
    if (!temporary) { image_free(fresh); return DURABLE_NOMEM; }
    int fd=open(temporary,O_RDWR|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    CettaDurableStatus result=DURABLE_IO; uint64_t written=0; bool temporary_owned=false;
    struct stat st;
    if (fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode)) goto done;
    if (st.st_nlink!=1) { result=DURABLE_INVALID; goto done; }
    temporary_owned=true;
    if (ftruncate(fd,0)) goto done;
    unsigned char *b=frame_new(0,s->epoch,s->revision,space_length64(s->image->space),FRAME_BASE);
    if (!b) { result=DURABLE_NOMEM; goto done; }
    result=journal_status(cetta_journal_append(fd,b,FRAME_BASE,false,&written)); free(b);
    for (CettaIndex i=0;i<space_length64(s->image->space) && result==DURABLE_OK;++i) {
        CettaDurableRecord r=row(space_get_at64(s->image->space,i)); size_t size=FRAME_BASE+row_size(&r);
        b=frame_new(2,s->epoch,s->revision,1,size);
        if (!b) { result=DURABLE_NOMEM; break; }
        unsigned char *p=b+FRAME_BASE; frame_row(&p,DURABLE_INSERT,&r);
        if (!image_add(fresh,&r)) result=DURABLE_LIMIT;
        else result=journal_status(cetta_journal_append(fd,b,size,false,&written));
        free(b);
    }
    if (result!=DURABLE_OK) goto done;
    fresh->live_bytes=s->image->live_bytes;
    if (fsync(fd)) { result=DURABLE_IO; goto done; }
    BOUNDARY("checkpoint");
    if (rename(temporary,s->path)) { result=DURABLE_IO; goto done; }
    /* Rename published the fully prepared projection. Any directory-sync
     * failure is uncertain durability, never permission to retry blindly. */
    close(s->fd); s->fd=fd; fd=-1;
    Image *old=s->image; s->image=fresh; fresh=NULL; image_free(old);
    s->history=0; s->file_bytes=written; s->reclaim=false;
    if (sync_parent(s->path)) { s->poisoned=true; result=DURABLE_UNKNOWN; }
done:
    if (fd>=0) close(fd);
    if (result!=DURABLE_OK && temporary_owned) unlink(temporary);
    free(temporary); image_free(fresh); return result;
}
CettaDurableStatus cetta_durable_checkpoint(CettaDurableStore *s) {
    if (!s) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus r=s->poisoned?DURABLE_POISONED:checkpoint_locked(s);
    pthread_mutex_unlock(&s->mutex); return r;
}
static CettaDurableStatus commit_internal(CettaDurableStore *s,
        const char *epoch,int64_t revision,const CettaDurableObservation *observation,
        const CettaDurableOp *ops,size_t count,int64_t *published) {
    if (published) *published=-1;
    if (!s || !epoch || strlen(epoch)!=32 || revision<0 || !ops || !count || !published) return DURABLE_INVALID;
    if (count>INT64_MAX || over(count,s->limits.operations)) return DURABLE_LIMIT;
    size_t batch=0,frame_bytes=FRAME_BASE; bool removals=true;
    for (size_t i=0;i<count;++i) {
        const CettaDurableOp *op=&ops[i];
        if (!valid_name(op->space) || !valid_name(op->key) || op->kind<DURABLE_INSERT || op->kind>DURABLE_REMOVE ||
            (op->size && !op->data) || (op->kind==DURABLE_REMOVE && op->size)) return DURABLE_INVALID;
        if (op->size>CETTA_STRING_BYTES_MAX || over(op->size,s->limits.record_bytes)) return DURABLE_LIMIT;
        if (observation) {
            bool covered=false;
            for (size_t j=0;j<observation->count;++j) if (scope_contains(&observation->scopes[j],op)) { covered=true; break; }
            if (!covered) return DURABLE_INVALID;
        }
        for (size_t j=0;j<i;++j) if (!strcmp(op->space,ops[j].space) && !strcmp(op->key,ops[j].key)) return DURABLE_PRECONDITION;
        size_t n=op->size+strlen(op->space)+strlen(op->key);
        if (n>SIZE_MAX-batch || n>UINT32_MAX-ROW_BASE || frame_bytes>UINT32_MAX-ROW_BASE-n) return DURABLE_LIMIT;
        batch+=n; frame_bytes+=ROW_BASE+n; if (op->kind!=DURABLE_REMOVE) removals=false;
    }
    if ((!removals && over(batch,s->limits.batch_bytes)) || batch>INT64_MAX) return DURABLE_LIMIT;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus result=s->poisoned?DURABLE_POISONED:DURABLE_OK;
    Space *prepared=NULL; unsigned char *frame=NULL;
    if (result!=DURABLE_OK) goto done;
    if (strcmp(epoch,s->epoch) || (!observation && revision!=s->revision)) { result=DURABLE_CONFLICT; goto done; }
    if (observation) { result=validate_observation(s,observation); if (result!=DURABLE_OK) goto done; }
    if (s->revision==INT64_MAX || batch>(uint64_t)(INT64_MAX-s->history)) { result=DURABLE_LIMIT; goto done; }
    int64_t bytes=s->image->live_bytes, rows=(int64_t)space_length64(s->image->space);
    for (size_t i=0;i<count;++i) {
        const CettaDurableOp *op=&ops[i]; Atom *found=find_row(s->image->space,op->space,op->key,NULL);
        if ((op->kind==DURABLE_INSERT)==(found!=NULL)) { result=DURABLE_PRECONDITION; goto done; }
        int64_t previous_bytes=bytes,previous_rows=rows;
        if (found) { CettaDurableRecord r=row(found); bytes-=(int64_t)(r.size+strlen(r.space)+strlen(r.key)); --rows; }
        if (op->kind!=DURABLE_REMOVE) {
            size_t n=op->size+strlen(op->space)+strlen(op->key);
            if (n>(uint64_t)(INT64_MAX-bytes) || rows==INT64_MAX) { result=DURABLE_LIMIT; goto done; }
            bytes+=(int64_t)n; ++rows;
        }
        if ((over((uint64_t)bytes,s->limits.live_bytes) && bytes>previous_bytes) ||
            (over((uint64_t)rows,s->limits.records) && rows>previous_rows)) { result=DURABLE_LIMIT; goto done; }
    }
    /* Rebuild the arena as well as the space at compaction: retired payloads
     * do not accumulate indefinitely in the private intern universe. */
    uint64_t projected=s->file_bytes+FRAME_OVERHEAD+frame_bytes;
    bool history_full=over((uint64_t)s->history+batch,s->limits.history_bytes);
    if (s->reclaim || (!removals && (history_full || s->history>s->image->live_bytes ||
        over((projected+4095)/4096,s->limits.storage_pages)))) {
        result=checkpoint_locked(s); if (result!=DURABLE_OK) goto done;
        projected=s->file_bytes+FRAME_OVERHEAD+frame_bytes;
    }
    if (!removals && over((projected+4095)/4096,s->limits.storage_pages)) { result=DURABLE_LIMIT; goto done; }
    frame=frame_new(1,s->epoch,s->revision+1,count,frame_bytes);
    if (!frame) { result=DURABLE_NOMEM; goto done; }
    prepared=space_heap_clone_shallow(s->image->space);
    if (!prepared) { result=DURABLE_NOMEM; goto done; }
    unsigned char *p=frame+FRAME_BASE;
    s->reclaim=true;
    for (size_t i=0;i<count;++i) {
        const CettaDurableOp *op=&ops[i]; CettaIndex index=0;
        Atom *found=find_row(prepared,op->space,op->key,&index);
        if (found && !space_remove_atom_id(prepared,space_get_atom_id_at64(prepared,index))) { result=DURABLE_CORRUPT; goto done; }
        CettaDurableRecord r={(char *)op->space,(char *)op->key,(unsigned char *)op->data,op->size,s->revision+1,(int64_t)i};
        if (op->kind!=DURABLE_REMOVE && !image_add_to(s->image,prepared,&r)) { result=DURABLE_LIMIT; goto done; }
        frame_row(&p,op->kind,&r); BOUNDARY("operation");
    }
    BOUNDARY("before_commit");
    result=journal_status(cetta_journal_append(s->fd,frame,frame_bytes,true,&s->file_bytes));
    if (result!=DURABLE_OK) { s->poisoned=true; result=DURABLE_UNKNOWN; goto done; }
    /* No allocation or semantic check remains after the irreversible write. */
    Space *old=s->image->space; s->image->space=prepared; prepared=NULL;
    s->image->live_bytes=bytes; ++s->revision; s->history+=(int64_t)batch; s->reclaim=false;
    space_free(old); free(old); BOUNDARY("after_commit"); *published=s->revision;
done:
    if (prepared) { space_free(prepared); free(prepared); }
    free(frame); if (result==DURABLE_CORRUPT) s->poisoned=true;
    pthread_mutex_unlock(&s->mutex); return result;
}
CettaDurableStatus cetta_durable_commit(CettaDurableStore *s,const char *epoch,int64_t revision,
        const CettaDurableOp *ops,size_t count,int64_t *published) {
    return commit_internal(s,epoch,revision,NULL,ops,count,published);
}
CettaDurableStatus cetta_durable_commit_observed(CettaDurableStore *s,const CettaDurableObservation *o,
        const CettaDurableOp *ops,size_t count,int64_t *published) {
    if (!o || !o->count) { if (published) *published=-1; return DURABLE_INVALID; }
    return commit_internal(s,o->views[0].epoch,o->views[0].revision,o,ops,count,published);
}
