#define _POSIX_C_SOURCE 200809L
#include "act_artifact.h"
#include "act_space.h"
#include "library_act.h"
#include "native_handle.h"
#include "native_sha256.h"
#include "storage_census.h"
#include "shared_transition.h"
#include "eval.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *root_kind="act-artifact-root",*artifact_kind="act-artifact";
typedef struct {int directory;} Root;
enum { RECEIVING,BUILDING,HASHING,VALIDATING,READY,PUBLISHED,FAILED };
typedef struct {
  int fd;
  unsigned phase;
  uint64_t size,received,hashed;
  size_t maximum;
  CettaActStatus failure;
  char expected[65],digest[65];
  CettaNativeSha256 sha;
  CettaActSpaceExport *exporter;
  CettaActPublication *publication;
  CettaActSpaceValidation *validation;
} Artifact;
static const char *text(const Atom *a) {
  return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING &&
    !memchr(a->ground.sval,0,atom_string_len(a)) ? a->ground.sval : NULL;
}
static bool number(const Atom *a,uint64_t *n) {
  if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT || a->ground.ival<0) return false;
  *n=(uint64_t)a->ground.ival;return true;
}
static bool digest_text(const Atom *a) {
  const char *s=text(a);if (!s || strlen(s)!=64) return false;
  for (size_t i=0;i<64;i++) if (!((s[i]>='0' && s[i]<='9') || (s[i]>='a' && s[i]<='f'))) return false;
  return true;
}
static Atom *error(Arena *a,Atom *head,CettaActStatus s) {
  return atom_expr3(a,atom_symbol(a,"Error"),head,atom_symbol(a,cetta_act_status_name(s)));
}
static void root_close(void *raw) {Root *r=raw;if (r) {close(r->directory);free(r);}}
static void root_storage(const void *raw,CettaStorageCensus *c) {cetta_storage_object(c,raw,sizeof(Root),CETTA_STORAGE_BLOCK);}
static void artifact_close(void *raw) {
  Artifact *v=raw;if (!v) return;
  cetta_act_space_validation_close(v->validation);cetta_act_space_export_close(v->exporter);
  cetta_act_publication_close(v->publication);if (v->fd>=0) close(v->fd);free(v);
}
static void artifact_storage(const void *raw,CettaStorageCensus *c) {
  const Artifact *v=raw;if (!v || !cetta_storage_object(c,v,sizeof(*v),CETTA_STORAGE_BLOCK)) return;
  cetta_act_space_export_storage(v->exporter,c);cetta_act_space_validation_storage(v->validation,c);
}
static Atom *own(CettaLibraryContext *ctx,Arena *a,const char *kind,void *value,
    CettaNativeHandleFreeFn close,CettaStorageVisitor storage) {
  uint64_t id;
  if (!cetta_native_handle_alloc(ctx,kind,value,close,&id)) {close(value);return NULL;}
  if (!cetta_native_handle_set_storage(ctx,kind,id,storage)) {cetta_native_handle_close(ctx,kind,id);return NULL;}
  Atom *out=cetta_native_handle_owned_atom(ctx,a,kind,id);
  if (!out) cetta_native_handle_close(ctx,kind,id);
  return out;
}
static bool io(int fd,void *raw,size_t n,uint64_t at,bool write) {
  if (at>INT64_MAX || n>(uint64_t)INT64_MAX-at) return false;
  unsigned char *p=raw;
  while (n) {
    ssize_t got=write ? pwrite(fd,p,n,(off_t)at) : pread(fd,p,n,(off_t)at);
    if (got<0 && errno==EINTR) continue;
    if (got<=0) return false;
    n-=(size_t)got;p+=got;at+=(uint64_t)got;
  }
  return true;
}
static CettaActStatus step(Artifact *v,uint64_t work) {
  if (v->phase==FAILED) return v->failure;
  if (v->phase==READY || v->phase==PUBLISHED) return CETTA_ACT_OK;
  if (!work) return CETTA_ACT_PROGRESS;
  CettaActStatus s=CETTA_ACT_OK;unsigned char bytes[65536];
  if (v->phase==RECEIVING) {
    if (v->received!=v->size) return CETTA_ACT_PROGRESS;
    v->phase=HASHING;cetta_native_sha256_init(&v->sha);
  }
  if (v->phase==BUILDING) {
    s=cetta_act_space_export_step(v->exporter,work);
    if (s==CETTA_ACT_OK) {
      int fd=cetta_act_space_export_descriptor(v->exporter);struct stat st;
      v->fd=fcntl(fd,F_DUPFD_CLOEXEC,0);
      if (v->fd<0 || fstat(v->fd,&st) || st.st_size<0) s=CETTA_ACT_IO;
      else {v->size=v->received=(uint64_t)st.st_size;v->phase=HASHING;cetta_native_sha256_init(&v->sha);}
    }
  } else if (v->phase==HASHING) {
    size_t n=v->size-v->hashed<sizeof(bytes) ? (size_t)(v->size-v->hashed) : sizeof(bytes);
    if (n>work) n=(size_t)work;
    if (!io(v->fd,bytes,n,v->hashed,false)) s=CETTA_ACT_IO;
    else {
      cetta_native_sha256_update(&v->sha,bytes,n);v->hashed+=n;
      if (v->hashed==v->size) {
        unsigned char digest[32];cetta_native_sha256_finish(&v->sha,digest);
        static const char hex[]="0123456789abcdef";
        for (unsigned i=0;i<32;i++) {v->digest[2*i]=hex[digest[i]>>4];v->digest[2*i+1]=hex[digest[i]&15];}
        if (v->expected[0] && strcmp(v->expected,v->digest)) s=CETTA_ACT_CORRUPT;
        else {s=cetta_act_space_validation_open(v->fd,v->maximum,&v->validation);if (s==CETTA_ACT_OK) v->phase=VALIDATING;}
      }
    }
  } else if (v->phase==VALIDATING) {
    s=cetta_act_space_validation_step(v->validation,work);
    if (s==CETTA_ACT_OK) v->phase=READY;
  }
  if (s!=CETTA_ACT_OK && s!=CETTA_ACT_PROGRESS) {v->phase=FAILED;v->failure=s;return s;}
  return v->phase==READY ? CETTA_ACT_OK : CETTA_ACT_PROGRESS;
}
static Atom *info(Arena *a,const Artifact *v) {
  static const char *phases[]={"receiving","building","hashing","validating","ready","published","failed"};
  CettaActSpaceExportInfo e={0};
  if (v->exporter && !cetta_act_space_export_info(v->exporter,&e)) return NULL;
  if (e.temporary_bytes>INT64_MAX || e.heap_bytes>INT64_MAX) return NULL;
  return atom_expr(a,(Atom *[]){atom_symbol(a,"ActArtifactInfoV1"),atom_symbol(a,phases[v->phase]),
    atom_int(a,(int64_t)v->received),atom_int(a,(int64_t)v->size),atom_string(a,v->digest),
    atom_int(a,(int64_t)e.temporary_bytes),atom_int(a,(int64_t)e.heap_bytes)},7);
}
Atom *cetta_act_artifact_dispatch(CettaLibraryContext *ctx,Arena *a,Atom *head,Atom **args,uint32_t n) {
  if (atom_is_symbol(head,"__cetta_lib_act_artifact_chunk_size")) {
    if (n || !ctx || !a) return NULL;
    size_t maximum=cetta_library_value_byte_limit(ctx)/4;
    return atom_int(a,(int64_t)(maximum<65536 ? maximum : 65536));
  }
  bool root=atom_is_symbol(head,"__cetta_lib_act_artifact_root");
  bool upload=atom_is_symbol(head,"__cetta_lib_act_artifact_upload");
  bool download=atom_is_symbol(head,"__cetta_lib_act_artifact_download");
  bool reader=atom_is_symbol(head,"__cetta_lib_act_artifact_reader");
  bool export=atom_is_symbol(head,"__cetta_lib_act_artifact_export");
  bool write=atom_is_symbol(head,"__cetta_lib_act_artifact_write");
  bool advance=atom_is_symbol(head,"__cetta_lib_act_artifact_step");
  bool state=atom_is_symbol(head,"__cetta_lib_act_artifact_info");
  bool read=atom_is_symbol(head,"__cetta_lib_act_artifact_read");
  bool source=atom_is_symbol(head,"__cetta_lib_act_artifact_space");
  bool publish=atom_is_symbol(head,"__cetta_lib_act_artifact_publish");
  bool closing=atom_is_symbol(head,"__cetta_lib_act_artifact_close");
  if (!root && !upload && !download && !reader && !export && !write && !advance && !state && !read && !source && !publish && !closing) return NULL;
  if (!ctx || !a || n!=(upload || download || export || write || read ? 3u : advance ? 2u : 1u)) return error(a,head,CETTA_ACT_CORRUPT);
  CETTA_SCOPED_SHARED_TRANSITION(guard);
  if (closing) {
    uint64_t id;const char *kinds[]={root_kind,artifact_kind};
    for (size_t i=0;i<2;i++) if (cetta_native_handle_get_owned(ctx,kinds[i],args[0]) && cetta_native_handle_arg(args[0],kinds[i],&id))
      return atom_bool(a,cetta_native_handle_close(ctx,kinds[i],id));
    return error(a,head,CETTA_ACT_CORRUPT);
  }
  if (root) {
    const char *socket_path=text(args[0]);if (!socket_path || !*socket_path) return error(a,head,CETTA_ACT_CORRUPT);
    char *parent=strdup(socket_path);if (!parent) return error(a,head,CETTA_ACT_NOMEM);
    char *slash=strrchr(parent,'/');
    if (!slash || !slash[1]) {free(parent);return error(a,head,CETTA_ACT_CORRUPT);}
    if (slash==parent) slash[1]=0;else *slash=0;
    cetta_shared_transition_guard_leave(&guard);
    int fd=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);struct stat st;free(parent);
    bool valid=fd>=0 && !fstat(fd,&st) && st.st_uid==geteuid() && !(st.st_mode&0077);
    cetta_shared_transition_guard_enter(&guard);
    if (!valid) {if (fd>=0) close(fd);return error(a,head,CETTA_ACT_IO);}
    Root *r=malloc(sizeof(*r));if (!r) {close(fd);return error(a,head,CETTA_ACT_NOMEM);}r->directory=fd;
    Atom *out=own(ctx,a,root_kind,r,root_close,root_storage);return out ? out : error(a,head,CETTA_ACT_NOMEM);
  }
  if (upload || download || reader || export) {
    uint64_t size=0,memory=0;const char *filename=(reader || download) ? text(args[0]) : NULL;
    if ((upload || download) && (!number(args[1],&size) || !digest_text(args[2]))) return error(a,head,CETTA_ACT_CORRUPT);
    if ((reader || download) && !filename) return error(a,head,CETTA_ACT_CORRUPT);
    Space *space=export ? resolve_space(eval_current_registry(),args[1]) : NULL;
    if (export && (!space || !space_owner_is_current(space) || !number(args[2],&memory) || memory>SIZE_MAX)) return error(a,head,CETTA_ACT_CORRUPT);
    CettaNativeHandleUse root_use={0};
    if ((upload || export) && !cetta_native_handle_acquire_owned(ctx,root_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&root_use)) return error(a,head,CETTA_ACT_CORRUPT);
    Artifact *v=calloc(1,sizeof(*v));if (!v) {cetta_native_handle_use_release(&root_use);return error(a,head,CETTA_ACT_NOMEM);}
    v->fd=-1;v->maximum=cetta_library_value_byte_limit(ctx);v->size=size;v->phase=RECEIVING;
    if (upload || download) memcpy(v->expected,text(args[2]),65);
    char *path=filename ? strdup(filename) : NULL;
    if (filename && !path) {artifact_close(v);cetta_native_handle_use_release(&root_use);return error(a,head,CETTA_ACT_NOMEM);}
    Arena retained;arena_init_detached(&retained);if (space) (void)atom_space(&retained,space);
    Root *r=cetta_native_handle_use_resource(&root_use);cetta_shared_transition_guard_leave(&guard);
    CettaActStatus status=CETTA_ACT_OK;
    if (export) {
      status=cetta_act_space_export_open_private(space,r->directory,false,v->maximum,(size_t)memory,&v->exporter);v->phase=BUILDING;
    } else if (upload || download) {
      status=upload ? cetta_act_publication_open_private(r->directory,&v->publication) : cetta_act_publication_open(path,&v->publication);
      if (status==CETTA_ACT_OK) status=cetta_act_publication_sidecar(v->publication,false);
      if (status==CETTA_ACT_OK) {v->fd=fcntl(cetta_act_publication_fd(v->publication),F_DUPFD_CLOEXEC,0);if (v->fd<0) status=CETTA_ACT_IO;}
    } else {
      v->fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;
      if (v->fd<0 || fstat(v->fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0) status=CETTA_ACT_IO;
      else {v->size=v->received=(uint64_t)st.st_size;v->phase=HASHING;cetta_native_sha256_init(&v->sha);}
    }
    free(path);cetta_shared_transition_guard_enter(&guard);arena_free(&retained);cetta_native_handle_use_release(&root_use);
    if (status!=CETTA_ACT_OK) {artifact_close(v);return error(a,head,status);}
    Atom *out=own(ctx,a,artifact_kind,v,artifact_close,artifact_storage);return out ? out : error(a,head,CETTA_ACT_NOMEM);
  }
  CettaNativeHandleUse use={0};
  if (!cetta_native_handle_acquire_owned(ctx,artifact_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return error(a,head,CETTA_ACT_CORRUPT);
  Artifact *v=cetta_native_handle_use_resource(&use);Atom *out=NULL;CettaActStatus status=CETTA_ACT_OK;
  if (state) out=info(a,v);
  else if (source) out=v->phase==READY || v->phase==PUBLISHED ? cetta_act_validation_owned_space(a,v->validation) : error(a,head,CETTA_ACT_CORRUPT);
  else if (advance) {
    uint64_t work;
    if (!number(args[1],&work)) status=CETTA_ACT_CORRUPT;
    else {cetta_shared_transition_guard_leave(&guard);status=step(v,work);cetta_shared_transition_guard_enter(&guard);}
    if (status==CETTA_ACT_OK || status==CETTA_ACT_PROGRESS) out=info(a,v);
  } else if (read) {
    uint64_t offset,maximum;
    if (v->phase!=READY || !number(args[1],&offset) || !number(args[2],&maximum) || offset>v->size || !maximum || maximum>cetta_library_value_byte_limit(ctx)/4) status=CETTA_ACT_CORRUPT;
    else {
      size_t length=v->size-offset<maximum ? (size_t)(v->size-offset) : (size_t)maximum;
      unsigned char *bytes=malloc(length ? length : 1);
      if (!bytes) status=CETTA_ACT_NOMEM;
      else {
        cetta_shared_transition_guard_leave(&guard);bool valid=io(v->fd,bytes,length,offset,false);cetta_shared_transition_guard_enter(&guard);
        out=valid ? atom_expr3(a,atom_symbol(a,"ActArtifactChunkV1"),atom_int(a,(int64_t)offset),atom_string_n(a,(const char *)bytes,length)) : NULL;
        if (!valid) status=CETTA_ACT_IO;
        free(bytes);
      }
    }
  } else if (write) {
    uint64_t offset;
    Atom *bytes=args[2];
    if (v->phase!=RECEIVING || !number(args[1],&offset) || bytes->kind!=ATOM_GROUNDED || bytes->ground.gkind!=GV_STRING ||
        atom_string_len(bytes)>cetta_library_value_byte_limit(ctx)/4 || offset>v->received || offset>v->size || atom_string_len(bytes)>v->size-offset) status=CETTA_ACT_CORRUPT;
    else {
      size_t length=atom_string_len(bytes);unsigned char *copy=malloc(length ? length : 1),*old=NULL;
      if (!copy) status=CETTA_ACT_NOMEM;
      else {
        memcpy(copy,bytes->ground.sval,length);
        if (offset<v->received) {old=malloc(length ? length : 1);if (!old) status=CETTA_ACT_NOMEM;}
        cetta_shared_transition_guard_leave(&guard);
        if (status==CETTA_ACT_OK) {
          if (offset<v->received) {
            if (length>v->received-offset || !io(v->fd,old,length,offset,false) || memcmp(copy,old,length)) status=CETTA_ACT_CORRUPT;
          } else if (!io(v->fd,copy,length,offset,true)) {status=CETTA_ACT_IO;v->phase=FAILED;v->failure=status;}
          else v->received+=length;
        }
        cetta_shared_transition_guard_enter(&guard);free(copy);free(old);
        if (status==CETTA_ACT_OK) out=info(a,v);
      }
    }
  } else if (publish) {
    if (!v->publication || (v->phase!=READY && v->phase!=PUBLISHED)) status=CETTA_ACT_CORRUPT;
    else {
      cetta_shared_transition_guard_leave(&guard);status=cetta_act_publication_commit(v->publication);
      if (status==CETTA_ACT_OK || status==CETTA_ACT_UNCERTAIN) {
        if (cetta_act_publication_sidecar(v->publication,true)!=CETTA_ACT_OK) status=CETTA_ACT_UNCERTAIN;
      }
      cetta_shared_transition_guard_enter(&guard);
      if (status==CETTA_ACT_OK) {v->phase=PUBLISHED;out=atom_bool(a,true);}
    }
  }
  if (!out) out=error(a,head,status==CETTA_ACT_OK ? CETTA_ACT_NOMEM : status);
  cetta_native_handle_use_release(&use);return out;
}
