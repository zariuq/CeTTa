#define _POSIX_C_SOURCE 200809L
#include "library_act.h"
#include "act_space.h"
#include "act_artifact.h"
#include "eval.h"
#include "shared_transition.h"
#include "storage_census.h"
#include "native_handle.h"
#include "space_source_snapshot.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  Space space;
  Arena retention;
  atomic_size_t references;
  atomic_bool closed;
} Owned;
static void close_space(Owned *s) {
  if (!atomic_exchange(&s->closed, true)) {
    space_free(&s->space);
    arena_free(&s->retention);
  }
}
static void retain(void *raw) {
  atomic_fetch_add(&((Owned *)raw)->references, 1);
}
static void release(void *raw) {
  Owned *s = raw;
  if (atomic_fetch_sub(&s->references, 1) == 1) {
    close_space(s);
    free(s);
  }
}
static bool current(const void *raw) {
  return !atomic_load(&((const Owned *)raw)->closed);
}
static void owner_storage(const void *raw, CettaStorageCensus *census) {
  const Owned *s = raw;
  if (cetta_storage_object(census, s, sizeof(*s), CETTA_STORAGE_BLOCK)) {
    arena_storage_census(&s->retention, census);
    space_storage_census(&s->space, census);
  }
}
static const SpaceOwnerOps owner_ops = {.retain = retain,
                                        .release = release,
                                        .current = current,
                                        .storage = owner_storage};
Atom *cetta_act_validation_owned_space(Arena *a,CettaActSpaceValidation *v) {
  Owned *s=calloc(1,sizeof(*s));if (!s) return NULL;
  atomic_init(&s->references,1);atomic_init(&s->closed,false);
  arena_init_detached(&s->retention);space_init(&s->space);
  CettaActStatus status=cetta_act_space_validation_capture(v,&s->space);
  s->space.lifetime=(SpaceOwner){s,&owner_ops};
  Atom *out=status==CETTA_ACT_OK ? atom_space(a,&s->space) : NULL;
  release(s);return out;
}
static Atom *error(Arena *arena, Atom *head, CettaActStatus s) {
  return atom_expr3(arena, atom_symbol(arena, "Error"), head,
                    atom_symbol(arena, cetta_act_status_name(s)));
}
static const char *path(Atom *atom) {
  if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_STRING)
    return NULL;
  const char *p = atom->ground.sval;
  size_t n = atom_string_len(atom);
  return p && strlen(p) == n ? p : NULL;
}
static const char *export_kind="act-export";
static void free_export(void *raw) {cetta_act_space_export_close(raw);}
static void export_storage(const void *raw,CettaStorageCensus *census) {cetta_act_space_export_storage(raw,census);}
static Atom *export_dispatch(CettaLibraryContext *ctx,Arena *a,Atom *head,Atom **args,uint32_t n) {
  bool open=atom_is_symbol(head,"__cetta_lib_act_export_open");
  bool mork=atom_is_symbol(head,"__cetta_lib_act_export_mork_open");
  bool step=atom_is_symbol(head,"__cetta_lib_act_export_step");
  bool info=atom_is_symbol(head,"__cetta_lib_act_export_info");
  bool close=atom_is_symbol(head,"__cetta_lib_act_export_close");
  if (!open && !mork && !step && !info && !close) return NULL;
  CETTA_SCOPED_SHARED_TRANSITION(transition);
  if (!ctx || !a || n!=(open || mork ? 3u : step ? 2u : 1u)) return error(a,head,CETTA_ACT_CORRUPT);
  if (open || mork) {
    Space *space=resolve_space(eval_current_registry(),args[0]);const char *p=path(args[1]);
    if (!space || !space_owner_is_current(space) || !p || args[2]->kind!=ATOM_GROUNDED ||
        args[2]->ground.gkind!=GV_INT || args[2]->ground.ival<=0 || (uint64_t)args[2]->ground.ival>SIZE_MAX)
      return error(a,head,CETTA_ACT_CORRUPT);
    char *destination=strdup(p);if (!destination) return error(a,head,CETTA_ACT_NOMEM);
    Arena retained;arena_init_detached(&retained);(void)atom_space(&retained,space);
    size_t maximum=cetta_library_value_byte_limit(ctx),memory=(size_t)args[2]->ground.ival;
    const char *operation=mork ? "__cetta_lib_act_export_mork_open" : "__cetta_lib_act_export_open";
    cetta_shared_transition_guard_leave(&transition);
    CettaActSpaceExport *job=NULL;
    CettaActStatus status=cetta_act_space_export_open(space,destination,mork,maximum,memory,&job);free(destination);
    cetta_shared_transition_guard_enter(&transition);arena_free(&retained);
    if (status!=CETTA_ACT_OK) return error(a,atom_symbol(a,operation),status);
    uint64_t id;
    if (!cetta_native_handle_alloc(ctx,export_kind,job,free_export,&id)) {free_export(job);return error(a,atom_symbol(a,operation),CETTA_ACT_NOMEM);}
    if (!cetta_native_handle_set_storage(ctx,export_kind,id,export_storage)) {
      cetta_native_handle_close(ctx,export_kind,id);return error(a,atom_symbol(a,operation),CETTA_ACT_NOMEM);
    }
    Atom *handle=cetta_native_handle_owned_atom(ctx,a,export_kind,id);
    if (!handle) {cetta_native_handle_close(ctx,export_kind,id);return error(a,atom_symbol(a,operation),CETTA_ACT_NOMEM);}
    return handle;
  }
  if (close) {
    uint64_t id;
    if (!cetta_native_handle_get_owned(ctx,export_kind,args[0]) || !cetta_native_handle_arg(args[0],export_kind,&id))
      return error(a,head,CETTA_ACT_CORRUPT);
    return atom_bool(a,cetta_native_handle_close(ctx,export_kind,id));
  }
  CettaNativeHandleUse use={0};
  if (!cetta_native_handle_acquire_owned(ctx,export_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return error(a,head,CETTA_ACT_CORRUPT);
  CettaActSpaceExport *job=cetta_native_handle_use_resource(&use);
  Atom *result=NULL;
  if (step) {
    if (args[1]->kind!=ATOM_GROUNDED || args[1]->ground.gkind!=GV_INT || args[1]->ground.ival<0) result=error(a,head,CETTA_ACT_CORRUPT);
    else {
      uint64_t work=(uint64_t)args[1]->ground.ival;
      cetta_shared_transition_guard_leave(&transition);
      CettaActStatus status=cetta_act_space_export_step(job,work);
      cetta_shared_transition_guard_enter(&transition);
      result=status==CETTA_ACT_OK ? atom_bool(a,true) : status==CETTA_ACT_PROGRESS ? atom_symbol(a,"act:pending")
        : error(a,atom_symbol(a,"__cetta_lib_act_export_step"),status);
    }
  } else {
    CettaActSpaceExportInfo state;
    if (!cetta_act_space_export_info(job,&state) || state.occurrences>INT64_MAX || state.payload_bytes>INT64_MAX ||
        state.trie_bytes>INT64_MAX || state.temporary_bytes>INT64_MAX || state.heap_bytes>INT64_MAX) result=error(a,head,CETTA_ACT_LIMIT);
    else result=atom_expr(a,(Atom *[]){atom_symbol(a,"act:export-state"),atom_int(a,state.phase),
      atom_int(a,(int64_t)state.occurrences),atom_int(a,(int64_t)state.payload_bytes),atom_int(a,(int64_t)state.trie_bytes),
      atom_int(a,(int64_t)state.temporary_bytes),atom_int(a,(int64_t)state.heap_bytes),atom_bool(a,state.complete)},8);
  }
  cetta_native_handle_use_release(&use);return result;
}
Atom *cetta_act_dispatch(CettaLibraryContext *ctx, Arena *a, Atom *head,
                         Atom **args, uint32_t n) {
  Atom *artifact=cetta_act_artifact_dispatch(ctx,a,head,args,n);if (artifact) return artifact;
  Atom *exported=export_dispatch(ctx,a,head,args,n);if (exported) return exported;
  bool open = atom_is_symbol(head, "__cetta_lib_act_open");
  bool snapshot = atom_is_symbol(head,"__cetta_lib_act_snapshot");
  bool close = atom_is_symbol(head, "__cetta_lib_act_close");
  bool workspace = atom_is_symbol(head, "__cetta_lib_act_workspace");
  bool dump = atom_is_symbol(head, "__cetta_lib_act_dump");
  bool mork = atom_is_symbol(head, "__cetta_lib_act_dump_mork");
  bool load = atom_is_symbol(head, "__cetta_lib_act_load");
  bool info = atom_is_symbol(head, "__cetta_lib_act_info");
  if (!open && !snapshot && !workspace && !close && !dump && !mork && !load && !info)
    return NULL;
  CETTA_SCOPED_SHARED_TRANSITION(transition);
  if (!ctx || !a || n != (open || snapshot || workspace || close || info ? 1u : 2u))
    return error(a, head, CETTA_ACT_CORRUPT);
  size_t maximum = cetta_library_value_byte_limit(ctx);
  if (open || snapshot || workspace) {
    const char *p = open ? path(args[0]) : NULL;
    Space *base =
        (workspace || snapshot) ? resolve_space(eval_current_registry(), args[0]) : NULL;
    if ((open && !p) || ((workspace || snapshot) && (!base || !space_owner_is_current(base))))
      return error(a, head, CETTA_ACT_CORRUPT);
    if (base && base->match_backend.kind != SPACE_ENGINE_SOURCE)
      return error(a, head, CETTA_ACT_UNSUPPORTED);
    Owned *s = calloc(1, sizeof(*s));
    if (!s)
      return error(a, head, CETTA_ACT_NOMEM);
    atomic_init(&s->references, 1);
    atomic_init(&s->closed, false);
    arena_init_detached(&s->retention);
    if (workspace)
      (void)atom_space(&s->retention, base);
    space_init(&s->space);
    CettaActStatus status =
        open ? cetta_act_space_capture(&s->space, p, maximum)
        : (snapshot ? space_source_snapshot_init(&s->space,base) : space_init_overlay_local(&s->space, base)) ? CETTA_ACT_OK
                                                    : CETTA_ACT_CORRUPT;
    s->space.lifetime = (SpaceOwner){s, &owner_ops};
    Atom *result = status == CETTA_ACT_OK ? atom_space(a, &s->space)
                                          : error(a, head, status);
    release(s);
    return result;
  }
  Space *space = resolve_space(eval_current_registry(), args[0]);
  if (!space || !space_owner_is_current(space))
    return error(a, head, CETTA_ACT_CORRUPT);
  if (info) {
    CettaActSpaceInfo details;
    cetta_shared_transition_guard_leave(&transition);
    CettaActStatus status=cetta_act_space_info(space,&details);
    cetta_shared_transition_guard_enter(&transition);
    if (status!=CETTA_ACT_OK) return error(a,head,status);
    if (details.occurrences>INT64_MAX || details.bytes>INT64_MAX || details.maximum_bytes>INT64_MAX)
      return error(a,head,CETTA_ACT_LIMIT);
    return atom_expr(a,(Atom *[]){atom_symbol(a,"ActSnapshotInfo"),
      atom_symbol(a,details.typed ? "CACT0001" : "ACTree03"),
      atom_int(a,(int64_t)details.occurrences),atom_int(a,(int64_t)details.bytes),
      atom_string(a,details.digest),atom_int(a,(int64_t)details.maximum_bytes)},6);
  }
  if (close) {
    if (space->lifetime.ops != &owner_ops)
      return error(a, head, CETTA_ACT_CORRUPT);
    close_space(space->lifetime.owner);
    return atom_bool(a, true);
  }
  const char *p = path(args[1]);
  if (!p)
    return error(a, head, CETTA_ACT_CORRUPT);
  if (load) {
    CettaActStatus status=cetta_act_space_load(space,eval_current_persistent_arena(),p,maximum);
    return status==CETTA_ACT_OK ? atom_bool(a,true) : error(a,head,status);
  }
  /* Retain runtime inputs before releasing the physical-transition guard.
   * The export owns a captured scan and uses short guards only at observation
   * boundaries; private sorting and file synchronization hold no such guard. */
  char *destination=strdup(p);
  if (!destination) return error(a,head,CETTA_ACT_NOMEM);
  Arena retained;arena_init_detached(&retained);
  (void)atom_space(&retained,space);
  const char *operation=mork ? "__cetta_lib_act_dump_mork" : "__cetta_lib_act_dump";
  cetta_shared_transition_guard_leave(&transition);
  CettaActStatus status=cetta_act_space_dump(space,destination,mork,maximum);
  free(destination);
  cetta_shared_transition_guard_enter(&transition);
  arena_free(&retained);
  return status==CETTA_ACT_OK ? atom_bool(a,true) : error(a,atom_symbol(a,operation),status);
}
