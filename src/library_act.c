#include "library_act.h"
#include "act_space.h"
#include "eval.h"
#include "shared_transition.h"
#include "storage_census.h"
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
Atom *cetta_act_dispatch(CettaLibraryContext *ctx, Arena *a, Atom *head,
                         Atom **args, uint32_t n) {
  bool open = atom_is_symbol(head, "__cetta_lib_act_open");
  bool close = atom_is_symbol(head, "__cetta_lib_act_close");
  bool workspace = atom_is_symbol(head, "__cetta_lib_act_workspace");
  bool dump = atom_is_symbol(head, "__cetta_lib_act_dump");
  bool mork = atom_is_symbol(head, "__cetta_lib_act_dump_mork");
  bool load = atom_is_symbol(head, "__cetta_lib_act_load");
  if (!open && !workspace && !close && !dump && !mork && !load)
    return NULL;
  CETTA_SCOPED_SHARED_TRANSITION(transition);
  if (!ctx || !a || n != (open || workspace || close ? 1u : 2u))
    return error(a, head, CETTA_ACT_CORRUPT);
  size_t maximum = cetta_library_value_byte_limit(ctx);
  if (open || workspace) {
    const char *p = open ? path(args[0]) : NULL;
    Space *base =
        workspace ? resolve_space(eval_current_registry(), args[0]) : NULL;
    if ((open && !p) || (workspace && (!base || !space_owner_is_current(base))))
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
        : space_init_overlay_local(&s->space, base) ? CETTA_ACT_OK
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
  if (close) {
    if (space->lifetime.ops != &owner_ops)
      return error(a, head, CETTA_ACT_CORRUPT);
    close_space(space->lifetime.owner);
    return atom_bool(a, true);
  }
  const char *p = path(args[1]);
  if (!p)
    return error(a, head, CETTA_ACT_CORRUPT);
  CettaActStatus status =
      load ? cetta_act_space_load(space, eval_current_persistent_arena(), p,
                                  maximum)
           : cetta_act_space_dump(space, p, mork, maximum);
  return status == CETTA_ACT_OK ? atom_bool(a, true) : error(a, head, status);
}
