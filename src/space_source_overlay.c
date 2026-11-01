#include "space_source_overlay.h"
#include "atom_wire.h"
#include "shared_transition.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    atomic_size_t references;
    unsigned char *bytes;
    size_t size;
    uint64_t serial;
    Arena arena;
    Atom *atom;
} Packet;

typedef struct {
    atomic_size_t references;
    Space *base;
    SpaceReadToken read;
    const SpaceSourceCursorOps *ops;
    void *prototype;
    Packet **added, **removed;
    size_t added_count, removed_count, added_capacity, removed_capacity, maximum_bytes;
    uint64_t scope, serial;
} Overlay;

typedef struct {
    Overlay *owner;
    void *base;
    Packet **added, **removed;
    size_t added_count, removed_count, next_added;
    Arena row;
    ArenaMark empty;
    Atom *atom, *identity;
    CettaIndex last;
    bool base_done, seen, done;
    SpaceOccurrenceCursorStep terminal;
    /* A removal search must distinguish a base identity from a local row. */
    Packet *local;
} Cursor;

static const SpaceMatchBackendOps overlay_ops;
static atomic_uint_fast64_t next_scope = 1;

static uint64_t allocate_scope(void) {
    uint_fast64_t scope = atomic_load(&next_scope);
    while (scope && scope <= INT64_MAX) {
        if (atomic_compare_exchange_weak(&next_scope, &scope, scope + 1)) return scope;
    }
    return 0;
}

static Packet *packet_new(Atom *atom, size_t maximum_bytes) {
    Packet *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    if (cetta_atom_wire_encode_bounded(atom, maximum_bytes, &p->bytes, &p->size) != ATOM_WIRE_OK) {
        free(p); return NULL;
    }
    /* Portability validation is separate from in-process variable identity.
     * Decoding afresh on each visit would rename the staged open atom. */
    arena_init_detached(&p->arena);
    arena_set_block_growth(&p->arena, 4 * sizeof(Atom));
    p->atom = atom_deep_copy(&p->arena, atom);
    if (!p->atom) { arena_free(&p->arena); free(p->bytes); free(p); return NULL; }
    atomic_init(&p->references, 1); return p;
}
static void packet_retain(Packet *p) { atomic_fetch_add(&p->references, 1); }
static void packet_free(Packet *p) {
    if (p && atomic_fetch_sub(&p->references, 1) == 1) { arena_free(&p->arena); free(p->bytes); free(p); }
}
static void packets_free(Packet **items, size_t count) {
    for (size_t i = 0; i < count; ++i) packet_free(items[i]);
    free(items);
}
static bool packets_copy(Packet ***out, Packet *const *items, size_t count) {
    *out = NULL;
    if (!count) return true;
    if (count > SIZE_MAX / sizeof(**out)) return false;
    Packet **copy = malloc(count * sizeof(*copy));
    if (!copy) return false;
    for (size_t i = 0; i < count; ++i) { copy[i] = items[i]; packet_retain(copy[i]); }
    *out = copy; return true;
}
static bool packets_reserve(Packet ***items, size_t *capacity, size_t needed) {
    if (needed <= *capacity) return true;
    size_t next = *capacity ? *capacity : 1;
    const size_t maximum = SIZE_MAX / sizeof(**items);
    if (needed > maximum) return false;
    while (next < needed) next = next > maximum / 2 ? maximum : next * 2;
    Packet **grown = realloc(*items, next * sizeof(*grown));
    if (!grown) return false;
    *items = grown; *capacity = next; return true;
}
static void overlay_release(Overlay *o) {
    if (!o || atomic_fetch_sub(&o->references, 1) != 1) return;
    o->ops->free(o->prototype);
    space_free(o->base); free(o->base);
    packets_free(o->added, o->added_count); packets_free(o->removed, o->removed_count);
    free(o);
}
static void cursor_free(void *raw) {
    Cursor *c = raw;
    if (!c) return;
    if (c->base) c->owner->ops->free(c->base);
    packets_free(c->added, c->added_count); packets_free(c->removed, c->removed_count);
    arena_free(&c->row); overlay_release(c->owner); free(c);
}
static Cursor *cursor_allocate(Overlay *o) {
    Cursor *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->owner = o; atomic_fetch_add(&o->references, 1);
    arena_init_detached(&c->row); c->empty = arena_mark(&c->row);
    return c;
}
static bool cursor_rows(Cursor *c, Packet *const *added, size_t added_count,
        Packet *const *removed, size_t removed_count) {
    if (!packets_copy(&c->added, added, added_count)) return false;
    c->added_count = added_count;
    if (!packets_copy(&c->removed, removed, removed_count)) return false;
    c->removed_count = removed_count; return true;
}
static bool open_base(Overlay *o, Atom *pattern, void **out,
        SpaceOccurrenceCursorStep *failure) {
    *out = NULL;
    if (!space_read_token_is_current(o->read)) {
        *failure = SPACE_OCCURRENCE_CURSOR_INVALIDATED; return false;
    }
    if (pattern && o->ops->select)
        return o->ops->select(o->prototype, pattern, out, failure);
    return o->ops->clone(o->prototype, true, out);
}
static bool source_open(Space *space, Atom *pattern, void **out,
        SpaceOccurrenceCursorStep *failure) {
    CETTA_SCOPED_SHARED_TRANSITION(capture);
    Overlay *o = space->match_backend.source;
    *out = NULL;
    Cursor *c = cursor_allocate(o);
    if (!c) return false;
    if (!cursor_rows(c, o->added, o->added_count, o->removed, o->removed_count) ||
            !open_base(o, pattern, &c->base, failure)) { cursor_free(c); return false; }
    *out = c; return true;
}
static bool cursor_copy(const Cursor *source, bool rewind, Atom *pattern,
        bool select, void **out, SpaceOccurrenceCursorStep *failure) {
    *out = NULL;
    Cursor *c = cursor_allocate(source->owner);
    if (!c) return false;
    bool opened = select ? open_base(source->owner, pattern, &c->base, failure)
        : source->owner->ops->clone(source->base, rewind, &c->base);
    if (!opened || !cursor_rows(c, source->added, source->added_count,
            source->removed, source->removed_count)) { cursor_free(c); return false; }
    if (!rewind && !select) {
        c->next_added = source->next_added; c->base_done = source->base_done;
        c->last = source->last; c->done = source->done; c->terminal = source->terminal;
        c->seen = source->seen; c->local = source->local;
        if (source->atom && !(c->atom = atom_deep_copy(&c->row, source->atom))) {
            cursor_free(c); return false;
        }
        if (source->identity && !(c->identity = atom_deep_copy(&c->row, source->identity))) {
            cursor_free(c); return false;
        }
    }
    *out = c; return true;
}
static bool cursor_clone(const void *raw, bool rewind, void **out) {
    SpaceOccurrenceCursorStep failure = SPACE_OCCURRENCE_CURSOR_CAPACITY;
    return cursor_copy(raw, rewind, NULL, false, out, &failure);
}
static bool cursor_select(const void *raw, Atom *pattern, void **out,
        SpaceOccurrenceCursorStep *failure) {
    return cursor_copy(raw, true, pattern, true, out, failure);
}
static SpaceOccurrenceCursorStep stop(Cursor *c, SpaceOccurrenceCursorStep step) {
    c->done = true; c->terminal = step; return step;
}
static SpaceOccurrenceCursorStep cursor_next(void *raw, CettaIndex *index) {
    Cursor *c = raw; c->seen = false; c->local = NULL;
    if (c->done) return c->terminal;
    if (!space_read_token_is_current(c->owner->read))
        return stop(c, SPACE_OCCURRENCE_CURSOR_INVALIDATED);
    arena_reset(&c->row, c->empty); c->atom = NULL; c->identity = NULL;
    while (!c->base_done) {
        CettaIndex at = 0;
        SpaceOccurrenceCursorStep step = c->owner->ops->next(c->base, &at);
        if (step == SPACE_OCCURRENCE_CURSOR_ITEM) {
            Atom *identity = c->owner->ops->identity(c->base, at);
            Atom *atom = c->owner->ops->atom(c->base, at);
            if (!identity || !atom) return stop(c, SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
            unsigned char *encoded = NULL; size_t size = 0;
            CettaAtomWireStatus status = cetta_atom_wire_encode_bounded(identity,
                c->owner->maximum_bytes, &encoded, &size);
            if (status != ATOM_WIRE_OK) return stop(c, SPACE_OCCURRENCE_CURSOR_CAPACITY);
            bool removed = false;
            for (size_t i = 0; i < c->removed_count; ++i)
                if (size == c->removed[i]->size && !memcmp(encoded, c->removed[i]->bytes, size)) {
                    removed = true; break;
                }
            free(encoded);
            /* Tombstones are local filtering, not a pending source poll.
             * Advance this finite captured traversal past them. A genuine
             * underlying PROGRESS still returns immediately below. */
            if (removed) continue;
            c->atom = atom; c->identity = identity;
        } else if (step == SPACE_OCCURRENCE_CURSOR_END) c->base_done = true;
        else if (step == SPACE_OCCURRENCE_CURSOR_PROGRESS) return step;
        else return stop(c, step);
        break;
    }
    if (!c->atom) {
        if (c->next_added == c->added_count) return stop(c, SPACE_OCCURRENCE_CURSOR_END);
        Packet *p = c->added[c->next_added++]; c->local = p;
        if (!(c->atom = atom_deep_copy(&c->row, p->atom)))
            return stop(c, SPACE_OCCURRENCE_CURSOR_CAPACITY);
        c->identity = atom_expr(&c->row, (Atom *[]){atom_symbol(&c->row, "LocalOccurrence"),
            atom_int(&c->row, (int64_t)c->owner->scope), atom_int(&c->row, (int64_t)p->serial)}, 3);
        if (!c->identity) return stop(c, SPACE_OCCURRENCE_CURSOR_CAPACITY);
    }
    if (c->last == UINT64_MAX) return stop(c, SPACE_OCCURRENCE_CURSOR_CAPACITY);
    *index = ++c->last; c->seen = true; return SPACE_OCCURRENCE_CURSOR_ITEM;
}
static Atom *cursor_atom(const void *raw, CettaIndex index) {
    const Cursor *c = raw; return c->seen && c->last == index ? c->atom : NULL;
}
static Atom *cursor_identity(const void *raw, CettaIndex index) {
    const Cursor *c = raw; return c->seen && c->last == index ? c->identity : NULL;
}
static uint64_t source_count(const Space *space) {
    const Overlay *o = space->match_backend.source;
    return space_length64(o->base) - o->removed_count + o->added_count;
}
static bool source_admit(Space *space, Atom *const *atoms, CettaCount count) {
    Overlay *o = space->match_backend.source;
    if (!space_read_token_is_current(o->read) || (count && !atoms) ||
            count > SIZE_MAX / sizeof(Packet *) - o->added_count ||
            count > INT64_MAX - o->serial || count > UINT64_MAX - source_count(space)) return false;
    if (!count) return true;
    size_t length = o->added_count + (size_t)count;
    Packet **prepared_rows = calloc((size_t)count, sizeof(*prepared_rows));
    if (!prepared_rows) return false;
    size_t prepared = 0;
    for (; prepared < count; ++prepared) {
        Packet *p = packet_new(atoms[prepared], o->maximum_bytes);
        if (!p) break;
        p->serial = o->serial + prepared + 1; prepared_rows[prepared] = p;
    }
    if (prepared != count || !packets_reserve(&o->added, &o->added_capacity, length)) {
        packets_free(prepared_rows, prepared); return false;
    }
    memcpy(o->added + o->added_count, prepared_rows, (size_t)count * sizeof(*prepared_rows));
    free(prepared_rows); o->added_count = length; o->serial += count; return true;
}
static bool remove_selected(Overlay *o, Cursor *c) {
    if (c->local) {
        for (size_t i = 0; i < o->added_count; ++i) if (o->added[i] == c->local) {
            Packet *p = o->added[i];
            memmove(o->added + i, o->added + i + 1, (--o->added_count - i) * sizeof(*o->added));
            packet_free(p); return true;
        }
        return false;
    }
    if (o->removed_count == SIZE_MAX / sizeof(Packet *)) return false;
    Packet *p = packet_new(c->identity, o->maximum_bytes);
    if (!p) return false;
    if (!packets_reserve(&o->removed, &o->removed_capacity, o->removed_count + 1)) { packet_free(p); return false; }
    o->removed[o->removed_count++] = p; return true;
}
static CettaMatchCompletion source_remove(Space *space, Atom *atom,
        CettaMatchCompletion (*equal)(Atom *, Atom *),
        CettaMatchCompletion (*alpha_equal)(Atom *, Atom *), bool representation) {
    CETTA_SCOPED_SHARED_TRANSITION(publication);
    Overlay *o = space->match_backend.source;
    /* Retain the selected identity while establishing global alpha uniqueness. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        SpaceOccurrenceCursorStep failure = SPACE_OCCURRENCE_CURSOR_CAPACITY;
        void *raw = NULL, *selected = NULL;
        /* Structural equality/alpha-equivalence imply unifiability. Arbitrary
         * interpreted comparisons cannot be narrowed by representation keys. */
        if (!source_open(space, representation ? atom : NULL, &raw, &failure)) return CETTA_MATCH_INCOMPLETE;
        Cursor *c = raw; size_t matches = 0; CettaIndex index;
        SpaceOccurrenceCursorStep step;
        while ((step = cursor_next(c, &index)) == SPACE_OCCURRENCE_CURSOR_ITEM ||
                step == SPACE_OCCURRENCE_CURSOR_PROGRESS) {
            if (step == SPACE_OCCURRENCE_CURSOR_PROGRESS) {
                /* A synchronous edit must not spin on an asynchronous source.
                 * No selection is published until completeness is established. */
                step = SPACE_OCCURRENCE_CURSOR_INCOMPLETE; break;
            }
            CettaMatchCompletion comparison = (pass ? alpha_equal : equal)(c->atom, atom);
            if (comparison == CETTA_MATCH_INCOMPLETE) { step = SPACE_OCCURRENCE_CURSOR_INCOMPLETE; break; }
            if (comparison != CETTA_MATCH_MATCHED) continue;
            if (++matches > 1) break;
            if (!pass) {
                bool removed = remove_selected(o, c); cursor_free(c);
                return removed ? CETTA_MATCH_MATCHED : CETTA_MATCH_INCOMPLETE;
            }
            /* Copy just the selected row identity, not a whole traversal. */
            Cursor *choice = cursor_allocate(o);
            if (!choice) { step = SPACE_OCCURRENCE_CURSOR_CAPACITY; break; }
            choice->local = c->local;
            choice->identity = atom_deep_copy(&choice->row, c->identity);
            if (!choice->identity) { cursor_free(choice); step = SPACE_OCCURRENCE_CURSOR_CAPACITY; break; }
            selected = choice;
        }
        bool complete = step == SPACE_OCCURRENCE_CURSOR_END || matches > 1;
        bool removed = complete && matches == 1 && selected && remove_selected(o, selected);
        cursor_free(selected); cursor_free(c);
        if (!complete) return CETTA_MATCH_INCOMPLETE;
        if (matches) return removed ? CETTA_MATCH_MATCHED : matches > 1 ? CETTA_MATCH_REFUTED : CETTA_MATCH_INCOMPLETE;
    }
    return CETTA_MATCH_REFUTED;
}
static CettaMatchCompletion source_remove_up_to(Space *space, Atom *pattern,
        CettaMatchCompletion (*matches)(Atom *, Atom *), CettaCount maximum, CettaCount *removed) {
    *removed = 0;
    if (!maximum) return CETTA_MATCH_REFUTED;
    Overlay *o = space->match_backend.source;
    SpaceReadToken read = space_read_token(space);
    unsigned char *mask = o->added_count ? calloc(o->added_count, 1) : NULL;
    if (o->added_count && !mask) return CETTA_MATCH_INCOMPLETE;
    Packet **base = NULL; size_t count = 0, capacity = 0, locals = 0;
    void *raw = NULL; SpaceOccurrenceCursorStep failure = SPACE_OCCURRENCE_CURSOR_CAPACITY;
    if (!source_open(space, NULL, &raw, &failure)) { free(mask); return CETTA_MATCH_INCOMPLETE; }
    Cursor *c = raw; CettaIndex index; SpaceOccurrenceCursorStep step;
    while ((step = cursor_next(c, &index)) == SPACE_OCCURRENCE_CURSOR_ITEM ||
            step == SPACE_OCCURRENCE_CURSOR_PROGRESS) {
        if (step == SPACE_OCCURRENCE_CURSOR_PROGRESS) {
            step = SPACE_OCCURRENCE_CURSOR_INCOMPLETE; break;
        }
        CettaMatchCompletion comparison = matches(c->atom, pattern);
        if (comparison == CETTA_MATCH_INCOMPLETE) { step = SPACE_OCCURRENCE_CURSOR_INCOMPLETE; break; }
        if (comparison != CETTA_MATCH_MATCHED) continue;
        if (c->local) {
            if (!c->next_added || c->next_added > o->added_count) { step = SPACE_OCCURRENCE_CURSOR_INCOMPLETE; break; }
            mask[c->next_added - 1] = 1; ++locals;
        } else {
            Packet *identity = packet_new(c->identity, o->maximum_bytes);
            if (!identity || !packets_reserve(&base, &capacity, count + 1)) {
                packet_free(identity); step = SPACE_OCCURRENCE_CURSOR_CAPACITY; break;
            }
            base[count++] = identity;
        }
        if (count + locals == maximum) break;
    }
    bool complete = (step == SPACE_OCCURRENCE_CURSOR_END || count + locals == maximum) && space_read_token_is_current(read) &&
        count <= SIZE_MAX / sizeof(Packet *) - o->removed_count && count <= UINT64_MAX - locals;
    if (complete && !packets_reserve(&o->removed, &o->removed_capacity, o->removed_count + count)) complete = false;
    if (!complete) { packets_free(base, count); free(mask); cursor_free(c); return CETTA_MATCH_INCOMPLETE; }
    size_t kept = 0;
    for (size_t i = 0; i < o->added_count; ++i) {
        if (mask[i]) packet_free(o->added[i]);
        else o->added[kept++] = o->added[i];
    }
    o->added_count = kept;
    if (count) memcpy(o->removed + o->removed_count, base, count * sizeof(*base));
    o->removed_count += count; *removed = count + locals;
    free(base); free(mask); cursor_free(c);
    return *removed ? CETTA_MATCH_MATCHED : CETTA_MATCH_REFUTED;
}

static CettaMatchCompletion source_remove_all(Space *space, Atom *pattern,
        CettaMatchCompletion (*matches)(Atom *, Atom *), CettaCount *removed) {
    return source_remove_up_to(space, pattern, matches, UINT64_MAX, removed);
}

static void packet_storage(Packet *p, CettaStorageCensus *census) {
    if (cetta_storage_object(census, p, sizeof(*p), CETTA_STORAGE_BLOCK)) {
        cetta_storage_array(census, p->bytes, p->size, 1, 0);
        arena_storage_census(&p->arena, census);
    }
}
static void owner_storage(const Overlay *o, CettaStorageCensus *census) {
    if (!cetta_storage_object(census, o, sizeof(*o), CETTA_STORAGE_BLOCK)) return;
    space_storage_census(o->base, census);
    cetta_storage_array(census, o->added, o->added_capacity, sizeof(*o->added), 0);
    cetta_storage_array(census, o->removed, o->removed_capacity, sizeof(*o->removed), 0);
    for (size_t i = 0; i < o->added_count; ++i) packet_storage(o->added[i], census);
    for (size_t i = 0; i < o->removed_count; ++i) packet_storage(o->removed[i], census);
    if (o->ops->storage) o->ops->storage(o->prototype, census);
}
static void source_storage(const Space *space, CettaStorageCensus *census) {
    owner_storage(space->match_backend.source, census);
}
static void cursor_storage(const void *raw, CettaStorageCensus *census) {
    const Cursor *c = raw;
    if (!cetta_storage_object(census, c, sizeof(*c), CETTA_STORAGE_BLOCK)) return;
    owner_storage(c->owner, census);
    cetta_storage_array(census, c->added, c->added_count, sizeof(*c->added), 0);
    cetta_storage_array(census, c->removed, c->removed_count, sizeof(*c->removed), 0);
    for (size_t i = 0; i < c->added_count; ++i) packet_storage(c->added[i], census);
    for (size_t i = 0; i < c->removed_count; ++i) packet_storage(c->removed[i], census);
    arena_storage_census(&c->row, census);
    if (c->base && c->owner->ops->storage) c->owner->ops->storage(c->base, census);
}
static void source_free(Space *space) {
    overlay_release(space->match_backend.source); space->match_backend.source = NULL;
}
static const SpaceSourceCursorOps cursor_ops = {
    .clone = cursor_clone, .select = cursor_select, .next = cursor_next,
    .atom = cursor_atom, .identity = cursor_identity, .free = cursor_free, .storage = cursor_storage,
};
static const SpaceSourceEditOps edits = {.admit = source_admit, .remove = source_remove,
    .remove_all = source_remove_all, .remove_up_to = source_remove_up_to};
static const SpaceMatchBackendOps overlay_ops = {
    .name = "source-overlay", .source_edits = &edits, .occurrence_ops = &cursor_ops,
    .occurrence_open = source_open, .logical_len = source_count, .free = source_free, .storage = source_storage,
};
bool space_source_overlay_init_owned(Space *target, Space *base, size_t maximum_bytes) {
    CETTA_SCOPED_SHARED_TRANSITION(capture);
    const SpaceMatchBackendOps *ops = base ? base->match_backend.ops : NULL;
    const SpaceSourceCursorOps *rows = ops ? ops->occurrence_ops : NULL;
    if (!target || target == base || target->kind != SPACE_KIND_ATOM ||
            target->match_backend.kind != SPACE_ENGINE_NATIVE || space_length64(target) ||
            target->overlay_base || space_has_dependencies(target) || target->module_own_view ||
            !base || base->kind != SPACE_KIND_ATOM || base->match_backend.kind != SPACE_ENGINE_SOURCE ||
            base->overlay_base || space_has_dependencies(base) || base->module_own_view ||
            !ops || ops->source_edits || !ops->occurrence_open || !rows || !rows->identity ||
            !rows->clone || !rows->next || !rows->atom || !rows->free || !maximum_bytes) return false;
    Overlay *o = calloc(1, sizeof(*o)); if (!o) return false;
    atomic_init(&o->references, 1); o->base = base; o->read = space_read_token(base);
    o->ops = rows; o->maximum_bytes = maximum_bytes;
    o->scope = allocate_scope();
    if (!o->scope) { free(o); return false; }
    SpaceOccurrenceCursorStep failure = SPACE_OCCURRENCE_CURSOR_CAPACITY;
    if (!ops->occurrence_open(base, NULL, &o->prototype, &failure) || !o->prototype) {
        if (o->prototype) rows->free(o->prototype);
        free(o); return false;
    }
    target->match_backend.kind = SPACE_ENGINE_SOURCE; target->match_backend.ops = &overlay_ops;
    target->match_backend.source = o; space_read_view_invalidate(target); return true;
}
bool space_source_overlay_changes(const Space *space, Arena *arena, Atom **additions, Atom **removals) {
    CETTA_SCOPED_SHARED_TRANSITION(capture);
    if (additions) *additions = NULL;
    if (removals) *removals = NULL;
    if (!space || space->match_backend.ops != &overlay_ops || !arena || !additions || !removals) return false;
    Overlay *o = space->match_backend.source;
    if (!space_read_token_is_current(o->read)) return false;
    Atom *lists[2];
    for (unsigned part = 0; part < 2; ++part) {
        size_t count = part ? o->removed_count : o->added_count;
        Packet **rows = part ? o->removed : o->added;
        size_t bytes = 0;
        if (!atom_expr_allocation_bound((CettaExprLen)count, &bytes)) return false;
        lists[part] = atom_expr_builder_begin(arena, (CettaExprLen)count);
        if (!lists[part]) return false;
        for (size_t i = 0; i < count; ++i)
            if (!(lists[part]->expr.elems[i] = atom_deep_copy(arena, rows[i]->atom))) return false;
        lists[part] = atom_expr_builder_finish(arena, lists[part]);
        if (!lists[part]) return false;
    }
    *additions = lists[0]; *removals = lists[1]; return true;
}
