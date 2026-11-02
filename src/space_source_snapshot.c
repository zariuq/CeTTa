#include "space_source_snapshot.h"
#include "space_source_overlay.h"
#include "atom_wire.h"
#include "storage_census.h"
#include <stdlib.h>

typedef struct {
    const SpaceSourceCursorOps *ops;
    void *prototype;
    uint64_t count;
} Snapshot;
typedef struct { Snapshot snapshot; SpaceMatchBackendOps methods; } OwnedSnapshot;
static const SpaceMatchBackendOps snapshot_ops;
static bool source_open(Space *space,Atom *pattern,void **out,
        SpaceOccurrenceCursorStep *failure) {
    Snapshot *s=space->match_backend.source;
    *out=NULL;
    if (pattern && s->ops->select)
        return s->ops->select(s->prototype,pattern,out,failure);
    return s->ops->clone(s->prototype,true,out);
}
static void source_free(Space *space) {
    Snapshot *s=space->match_backend.source;
    if (s) { s->ops->free(s->prototype);free(s); }
    space->match_backend.source=NULL;space->match_backend.ops=&snapshot_ops;
}
static uint64_t source_count(const Space *space) {
    return ((const Snapshot *)space->match_backend.source)->count;
}
static void source_storage(const Space *space,CettaStorageCensus *census) {
    const Snapshot *s=space->match_backend.source;
    if (s && cetta_storage_object(census,s,sizeof(OwnedSnapshot),CETTA_STORAGE_BLOCK) && s->ops->storage)
        s->ops->storage(s->prototype,census);
}
static bool overlay_local(Space *target,const Space *base) {
    Space *copy=malloc(sizeof(*copy));if (!copy) return false;
    space_init_with_universe(copy,target->native.universe);
    if (space_source_snapshot_init(copy,(Space *)base) &&
            space_source_overlay_init_owned(target,copy,CETTA_ATOM_WIRE_BYTES)) return true;
    space_free(copy);free(copy);return false;
}
/* Each instance copies this table to preserve the source's cursor ABI. */
static const SpaceMatchBackendOps snapshot_ops={
    .name="captured-source",.occurrence_open=source_open,.free=source_free,
    .logical_len=source_count,.storage=source_storage,.overlay_local=overlay_local,
};
bool space_source_snapshot_init(Space *target,Space *source) {
    if (!target || !source || target->match_backend.kind!=SPACE_ENGINE_NATIVE ||
            space_length64(target) || source->match_backend.kind!=SPACE_ENGINE_SOURCE)
        return false;
    const SpaceMatchBackendOps *ops=source->match_backend.ops;
    if (!ops || !ops->occurrence_ops || !ops->occurrence_ops->clone ||
            !ops->occurrence_ops->free || !ops->occurrence_open) return false;
    OwnedSnapshot *s=calloc(1,sizeof(*s));if (!s) return false;
    s->snapshot.ops=ops->occurrence_ops;
    SpaceOccurrenceCursorStep failure=SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    if (!ops->occurrence_open(source,NULL,&s->snapshot.prototype,&failure)) {
        if (s->snapshot.prototype) s->snapshot.ops->free(s->snapshot.prototype);
        free(s);return false;
    }
    s->snapshot.count=space_length64(source);
    s->methods=snapshot_ops;s->methods.occurrence_ops=s->snapshot.ops;
    target->match_backend.kind=SPACE_ENGINE_SOURCE;
    target->match_backend.ops=&s->methods;target->match_backend.source=s;
    target->revision=space_revision(source);target->equation_revision=space_equation_revision(source);
    space_read_view_invalidate(target);return true;
}
