#include "space_source_overlay.h"
#include "atom_wire.h"
#include "shared_transition.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define PACKET_CHUNK_SHIFT 8u
#define PACKET_CHUNK_SIZE  (1u << PACKET_CHUNK_SHIFT)
#define PACKET_CHUNK_MASK  (PACKET_CHUNK_SIZE - 1u)
#define OVERLAY_INDEX_POS_LIMIT 32u

typedef struct {
    unsigned char *bytes;
    size_t size;
    uint64_t serial;
    uint64_t hash;
    uint64_t added_at;
    uint64_t removed_at;
    Arena arena;
    Atom *atom;
} Packet;

typedef struct {
    Packet ***chunks;
    size_t chunk_count;
    size_t chunk_capacity;
    size_t count;
} PacketLog;

typedef struct {
    Packet **buckets;
    size_t capacity;
    size_t occupied;
} TombstoneSet;

typedef struct {
    size_t *slots;
    size_t count;
    size_t capacity;
} SlotList;

typedef struct {
    uint64_t hash;
    unsigned char tag;
    size_t len;
    const char *bytes;
    SlotList slots;
} ExactBucket;

typedef struct {
    uint32_t arity;
    uint32_t pos;
    ExactBucket **buckets;
    size_t bucket_capacity;
    size_t bucket_occupied;
    SlotList any_slots;
} PosIndex;

typedef struct {
    atomic_size_t references;
    Space *base;
    SpaceReadToken read;
    const SpaceSourceCursorOps *ops;
    void *prototype;
    PacketLog added, removed;
    TombstoneSet tombstones;
    PosIndex **pos_indices;
    size_t pos_count, pos_capacity;
    SlotList var_slots;
    size_t added_live, maximum_bytes;
    uint64_t scope, serial, version;
} Overlay;

typedef struct {
    Overlay *owner;
    void *base;
    uint64_t version;
    size_t added_limit, removed_limit, next_added;
    bool filtered;
    const SlotList *exact_list, *any_list, *var_list;
    size_t e_idx, a_idx, v_idx;
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

static uint64_t bytes_hash(const unsigned char *bytes, size_t size) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

static uint64_t exact_key_hash(unsigned char tag, const char *bytes, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    h ^= (uint64_t)tag;
    h *= 1099511628211ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint64_t)(unsigned char)bytes[i];
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

static bool exact_key(const Atom *a, unsigned char *tag, const char **bytes, size_t *len) {
    if (!a) return false;
    if (a->kind == ATOM_SYMBOL) {
        *tag = 'S';
        *bytes = atom_symbol_bytes(a);
        *len = atom_symbol_length(a);
        return *bytes != NULL;
    }
    if (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING) {
        *tag = 'T';
        *bytes = a->ground.sval;
        *len = atom_string_len(a);
        return *bytes != NULL;
    }
    return false;
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
    p->removed_at = UINT64_MAX;
    return p;
}

static void packet_free(Packet *p) {
    if (p) { arena_free(&p->arena); free(p->bytes); free(p); }
}

static void packets_free(Packet **items, size_t count) {
    for (size_t i = 0; i < count; ++i) packet_free(items[i]);
    free(items);
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

static inline Packet *packet_log_at(const PacketLog *log, size_t index) {
    return log->chunks[index >> PACKET_CHUNK_SHIFT][index & PACKET_CHUNK_MASK];
}

static bool packet_log_reserve(PacketLog *log, size_t needed) {
    if (needed <= log->chunk_count * PACKET_CHUNK_SIZE) return true;
    if (needed > SIZE_MAX - PACKET_CHUNK_MASK) return false;
    size_t needed_chunks = (needed + PACKET_CHUNK_MASK) >> PACKET_CHUNK_SHIFT;
    if (needed_chunks > log->chunk_capacity) {
        size_t next = log->chunk_capacity ? log->chunk_capacity : 4;
        const size_t maximum = SIZE_MAX / sizeof(*log->chunks);
        if (needed_chunks > maximum) return false;
        while (next < needed_chunks) next = next > maximum / 2 ? maximum : next * 2;
        Packet ***grown = realloc(log->chunks, next * sizeof(*grown));
        if (!grown) return false;
        log->chunks = grown;
        log->chunk_capacity = next;
    }
    while (log->chunk_count < needed_chunks) {
        Packet **chunk = calloc(PACKET_CHUNK_SIZE, sizeof(*chunk));
        if (!chunk) return false;
        log->chunks[log->chunk_count++] = chunk;
    }
    return true;
}

static inline void packet_log_push_reserved(PacketLog *log, Packet *p) {
    size_t index = log->count++;
    log->chunks[index >> PACKET_CHUNK_SHIFT][index & PACKET_CHUNK_MASK] = p;
}

static void packet_log_free(PacketLog *log) {
    for (size_t i = 0; i < log->count; ++i) packet_free(packet_log_at(log, i));
    for (size_t i = 0; i < log->chunk_count; ++i) free(log->chunks[i]);
    free(log->chunks);
}

static bool tombstone_reserve(TombstoneSet *set, size_t needed) {
    if (needed <= set->capacity / 2) return true;
    const size_t maximum = SIZE_MAX / sizeof(*set->buckets);
    if (needed > maximum / 2) return false;
    size_t next = set->capacity ? set->capacity : 16;
    while (next / 2 < needed) {
        if (next > maximum / 2) return false;
        next *= 2;
    }
    Packet **buckets = calloc(next, sizeof(*buckets));
    if (!buckets) return false;
    size_t mask = next - 1;
    for (size_t i = 0; i < set->capacity; ++i) {
        Packet *p = set->buckets[i];
        if (!p) continue;
        size_t idx = (size_t)p->hash & mask;
        while (buckets[idx]) idx = (idx + 1) & mask;
        buckets[idx] = p;
    }
    free(set->buckets);
    set->buckets = buckets;
    set->capacity = next;
    return true;
}

static void tombstone_insert_reserved(TombstoneSet *set, Packet *p) {
    size_t mask = set->capacity - 1;
    size_t idx = (size_t)p->hash & mask;
    while (set->buckets[idx]) {
        Packet *existing = set->buckets[idx];
        if (existing->hash == p->hash && existing->size == p->size &&
                (!p->size || memcmp(existing->bytes, p->bytes, p->size) == 0))
            return;
        idx = (idx + 1) & mask;
    }
    set->buckets[idx] = p;
    ++set->occupied;
}

static bool tombstone_contains_at(const TombstoneSet *set,
        const unsigned char *bytes, size_t size, uint64_t max_version) {
    if (!set->occupied || !set->capacity) return false;
    uint64_t h = bytes_hash(bytes, size);
    size_t mask = set->capacity - 1;
    for (size_t idx = (size_t)h & mask; set->buckets[idx]; idx = (idx + 1) & mask) {
        Packet *p = set->buckets[idx];
        if (p->hash == h && p->size == size && (!size || memcmp(p->bytes, bytes, size) == 0))
            return p->added_at <= max_version;
    }
    return false;
}

static bool slot_list_push(SlotList *list, size_t slot) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2 : 4;
        if (next < list->count + 1 || next > SIZE_MAX / sizeof(*list->slots)) return false;
        size_t *grown = realloc(list->slots, next * sizeof(*grown));
        if (!grown) return false;
        list->slots = grown;
        list->capacity = next;
    }
    list->slots[list->count++] = slot;
    return true;
}

static void slot_list_rollback(SlotList *list, size_t min_slot) {
    while (list->count && list->slots[list->count - 1] >= min_slot) --list->count;
}

static const ExactBucket *pos_index_find_bucket(const PosIndex *pi,
        unsigned char tag, const char *bytes, size_t len, uint64_t hash) {
    if (!pi || !pi->bucket_occupied || !pi->bucket_capacity) return NULL;
    size_t mask = pi->bucket_capacity - 1;
    for (size_t idx = (size_t)hash & mask; pi->buckets[idx]; idx = (idx + 1) & mask) {
        const ExactBucket *b = pi->buckets[idx];
        if (b->hash == hash && b->tag == tag && b->len == len &&
                (!len || memcmp(b->bytes, bytes, len) == 0))
            return b;
    }
    return NULL;
}

static ExactBucket *pos_index_get_or_create_bucket(PosIndex *pi,
        unsigned char tag, const char *bytes, size_t len, uint64_t hash) {
    if (pi->bucket_occupied && pi->bucket_capacity) {
        size_t mask = pi->bucket_capacity - 1;
        for (size_t idx = (size_t)hash & mask; pi->buckets[idx]; idx = (idx + 1) & mask) {
            ExactBucket *b = pi->buckets[idx];
            if (b->hash == hash && b->tag == tag && b->len == len &&
                    (!len || memcmp(b->bytes, bytes, len) == 0))
                return b;
        }
    }
    if (pi->bucket_occupied + 1 > pi->bucket_capacity / 2) {
        const size_t maximum = SIZE_MAX / sizeof(*pi->buckets);
        if (pi->bucket_occupied + 1 > maximum / 2) return NULL;
        size_t next = pi->bucket_capacity ? pi->bucket_capacity * 2 : 8;
        ExactBucket **buckets = calloc(next, sizeof(*buckets));
        if (!buckets) return NULL;
        size_t mask = next - 1;
        for (size_t i = 0; i < pi->bucket_capacity; ++i) {
            ExactBucket *b = pi->buckets[i];
            if (!b) continue;
            size_t idx = (size_t)b->hash & mask;
            while (buckets[idx]) idx = (idx + 1) & mask;
            buckets[idx] = b;
        }
        free(pi->buckets);
        pi->buckets = buckets;
        pi->bucket_capacity = next;
    }
    ExactBucket *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->hash = hash;
    b->tag = tag;
    b->len = len;
    b->bytes = bytes;
    size_t mask = pi->bucket_capacity - 1;
    size_t idx = (size_t)hash & mask;
    while (pi->buckets[idx]) idx = (idx + 1) & mask;
    pi->buckets[idx] = b;
    ++pi->bucket_occupied;
    return b;
}

static const PosIndex *overlay_find_pos_index(const Overlay *o, uint32_t arity, uint32_t pos) {
    for (size_t i = 0; i < o->pos_count; ++i) {
        const PosIndex *pi = o->pos_indices[i];
        if (pi->arity == arity && pi->pos == pos) return pi;
    }
    return NULL;
}

static PosIndex *overlay_get_or_create_pos_index(Overlay *o, uint32_t arity, uint32_t pos) {
    for (size_t i = 0; i < o->pos_count; ++i) {
        PosIndex *pi = o->pos_indices[i];
        if (pi->arity == arity && pi->pos == pos) return pi;
    }
    if (o->pos_count == o->pos_capacity) {
        size_t next = o->pos_capacity ? o->pos_capacity * 2 : 8;
        if (next < o->pos_count + 1 || next > SIZE_MAX / sizeof(*o->pos_indices)) return NULL;
        PosIndex **grown = realloc(o->pos_indices, next * sizeof(*grown));
        if (!grown) return NULL;
        o->pos_indices = grown;
        o->pos_capacity = next;
    }
    PosIndex *pi = calloc(1, sizeof(*pi));
    if (!pi) return NULL;
    pi->arity = arity;
    pi->pos = pos;
    o->pos_indices[o->pos_count++] = pi;
    return pi;
}

static bool overlay_index_row(Overlay *o, size_t slot, const Atom *atom) {
    if (!atom) return true;
    if (atom->kind == ATOM_VAR) return slot_list_push(&o->var_slots, slot);
    if (atom->kind != ATOM_EXPR) return true;
    uint32_t arity = atom->expr.len;
    uint32_t limit = arity < OVERLAY_INDEX_POS_LIMIT ? arity : OVERLAY_INDEX_POS_LIMIT;
    for (uint32_t pos = 0; pos < limit; ++pos) {
        PosIndex *pi = overlay_get_or_create_pos_index(o, arity, pos);
        if (!pi) return false;
        unsigned char tag = 0;
        const char *bytes = NULL;
        size_t len = 0;
        if (exact_key(atom->expr.elems[pos], &tag, &bytes, &len)) {
            uint64_t h = exact_key_hash(tag, bytes, len);
            ExactBucket *b = pos_index_get_or_create_bucket(pi, tag, bytes, len, h);
            if (!b || !slot_list_push(&b->slots, slot)) return false;
        } else {
            if (!slot_list_push(&pi->any_slots, slot)) return false;
        }
    }
    return true;
}

static void overlay_index_rollback(Overlay *o, size_t min_slot) {
    slot_list_rollback(&o->var_slots, min_slot);
    for (size_t i = 0; i < o->pos_count; ++i) {
        PosIndex *pi = o->pos_indices[i];
        slot_list_rollback(&pi->any_slots, min_slot);
        for (size_t j = 0; j < pi->bucket_capacity; ++j) {
            if (pi->buckets[j]) slot_list_rollback(&pi->buckets[j]->slots, min_slot);
        }
    }
}

static void overlay_release(Overlay *o) {
    if (!o || atomic_fetch_sub(&o->references, 1) != 1) return;
    o->ops->free(o->prototype);
    space_free(o->base); free(o->base);
    packet_log_free(&o->added);
    packet_log_free(&o->removed);
    free(o->tombstones.buckets);
    free(o->var_slots.slots);
    for (size_t i = 0; i < o->pos_count; ++i) {
        PosIndex *pi = o->pos_indices[i];
        for (size_t j = 0; j < pi->bucket_capacity; ++j) {
            if (pi->buckets[j]) {
                free(pi->buckets[j]->slots.slots);
                free(pi->buckets[j]);
            }
        }
        free(pi->buckets);
        free(pi->any_slots.slots);
        free(pi);
    }
    free(o->pos_indices);
    free(o);
}

static void cursor_free(void *raw) {
    Cursor *c = raw;
    if (!c) return;
    if (c->base) c->owner->ops->free(c->base);
    arena_free(&c->row); overlay_release(c->owner); free(c);
}

static Cursor *cursor_allocate(Overlay *o) {
    Cursor *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->owner = o; atomic_fetch_add(&o->references, 1);
    arena_init_detached(&c->row); c->empty = arena_mark(&c->row);
    return c;
}

static void cursor_plan_added(Cursor *c, const Atom *pattern) {
    c->filtered = false;
    c->exact_list = NULL; c->any_list = NULL; c->var_list = NULL;
    c->e_idx = 0; c->a_idx = 0; c->v_idx = 0; c->next_added = 0;
    if (!pattern || pattern->kind != ATOM_EXPR ||
            atom_structural_may_have_internal_tag(pattern))
        return;
    uint32_t arity = pattern->expr.len;
    uint32_t limit = arity < OVERLAY_INDEX_POS_LIMIT ? arity : OVERLAY_INDEX_POS_LIMIT;
    bool found = false;
    size_t best_cost = SIZE_MAX;
    const SlotList *best_exact = NULL;
    const SlotList *best_any = NULL;
    for (uint32_t pos = 0; pos < limit; ++pos) {
        unsigned char tag = 0;
        const char *bytes = NULL;
        size_t len = 0;
        if (!exact_key(pattern->expr.elems[pos], &tag, &bytes, &len)) continue;
        const PosIndex *pi = overlay_find_pos_index(c->owner, arity, pos);
        const SlotList *exact_slots = NULL;
        const SlotList *any_slots = NULL;
        size_t cost = 0;
        if (pi) {
            uint64_t h = exact_key_hash(tag, bytes, len);
            const ExactBucket *b = pos_index_find_bucket(pi, tag, bytes, len, h);
            if (b) { exact_slots = &b->slots; cost += b->slots.count; }
            any_slots = &pi->any_slots;
            cost += pi->any_slots.count;
        }
        if (!found || cost <= best_cost) {
            found = true;
            best_cost = cost;
            best_exact = exact_slots;
            best_any = any_slots;
        }
    }
    if (found) {
        c->filtered = true;
        c->exact_list = best_exact;
        c->any_list = best_any;
        c->var_list = &c->owner->var_slots;
    }
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
    c->version = o->version;
    c->added_limit = o->added.count;
    c->removed_limit = o->removed.count;
    cursor_plan_added(c, pattern);
    if (!open_base(o, pattern, &c->base, failure)) { cursor_free(c); return false; }
    *out = c; return true;
}

static bool cursor_copy(const Cursor *source, bool rewind, Atom *pattern,
        bool select, void **out, SpaceOccurrenceCursorStep *failure) {
    *out = NULL;
    Cursor *c = cursor_allocate(source->owner);
    if (!c) return false;
    bool opened = select ? open_base(source->owner, pattern, &c->base, failure)
        : source->owner->ops->clone(source->base, rewind, &c->base);
    if (!opened) { cursor_free(c); return false; }
    c->version = source->version;
    c->added_limit = source->added_limit;
    c->removed_limit = source->removed_limit;
    if (select) {
        cursor_plan_added(c, pattern);
    } else {
        c->filtered = source->filtered;
        c->exact_list = source->exact_list;
        c->any_list = source->any_list;
        c->var_list = source->var_list;
        if (!rewind) {
            c->next_added = source->next_added;
            c->e_idx = source->e_idx;
            c->a_idx = source->a_idx;
            c->v_idx = source->v_idx;
            c->base_done = source->base_done;
            c->last = source->last;
            c->done = source->done;
            c->terminal = source->terminal;
            c->seen = source->seen;
            c->local = source->local;
            if (source->atom && !(c->atom = atom_deep_copy(&c->row, source->atom))) {
                cursor_free(c); return false;
            }
            if (source->identity && !(c->identity = atom_deep_copy(&c->row, source->identity))) {
                cursor_free(c); return false;
            }
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
            bool removed = c->removed_limit &&
                tombstone_contains_at(&c->owner->tombstones, encoded, size, c->version);
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
        Packet *p = NULL;
        while (!p) {
            size_t slot = SIZE_MAX;
            if (c->filtered) {
                size_t s_e = (c->exact_list && c->e_idx < c->exact_list->count)
                    ? c->exact_list->slots[c->e_idx] : SIZE_MAX;
                size_t s_a = (c->any_list && c->a_idx < c->any_list->count)
                    ? c->any_list->slots[c->a_idx] : SIZE_MAX;
                size_t s_v = (c->var_list && c->v_idx < c->var_list->count)
                    ? c->var_list->slots[c->v_idx] : SIZE_MAX;
                slot = s_e < s_a ? (s_e < s_v ? s_e : s_v) : (s_a < s_v ? s_a : s_v);
                if (slot >= c->added_limit) return stop(c, SPACE_OCCURRENCE_CURSOR_END);
                if (slot == s_e) ++c->e_idx;
                if (slot == s_a) ++c->a_idx;
                if (slot == s_v) ++c->v_idx;
            } else {
                if (c->next_added >= c->added_limit) return stop(c, SPACE_OCCURRENCE_CURSOR_END);
                slot = c->next_added++;
            }
            Packet *candidate = packet_log_at(&c->owner->added, slot);
            if (candidate->removed_at > c->version) p = candidate;
        }
        c->local = p;
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
    return space_length64(o->base) - o->removed.count + o->added_live;
}

static bool source_admit(Space *space, Atom *const *atoms, CettaCount count) {
    Overlay *o = space->match_backend.source;
    if (!space_read_token_is_current(o->read) || (count && !atoms) ||
            count > SIZE_MAX / sizeof(Packet *) - o->added.count ||
            count > INT64_MAX - o->serial || count > UINT64_MAX - source_count(space)) return false;
    if (!count) return true;
    size_t old_count = o->added.count;
    size_t length = old_count + (size_t)count;
    Packet **prepared_rows = calloc((size_t)count, sizeof(*prepared_rows));
    if (!prepared_rows) return false;
    size_t prepared = 0;
    for (; prepared < count; ++prepared) {
        Packet *p = packet_new(atoms[prepared], o->maximum_bytes);
        if (!p) break;
        p->serial = o->serial + prepared + 1;
        prepared_rows[prepared] = p;
    }
    bool ok = prepared == count && packet_log_reserve(&o->added, length);
    for (size_t i = 0; ok && i < count; ++i)
        ok = overlay_index_row(o, old_count + i, prepared_rows[i]->atom);
    if (!ok) {
        overlay_index_rollback(o, old_count);
        packets_free(prepared_rows, prepared);
        return false;
    }
    ++o->version;
    for (size_t i = 0; i < count; ++i) {
        prepared_rows[i]->added_at = o->version;
        packet_log_push_reserved(&o->added, prepared_rows[i]);
    }
    free(prepared_rows);
    o->added_live += (size_t)count;
    o->serial += count;
    return true;
}

static bool remove_selected(Overlay *o, Cursor *c) {
    if (c->local) {
        if (c->local->removed_at != UINT64_MAX) return false;
        ++o->version;
        c->local->removed_at = o->version;
        --o->added_live;
        return true;
    }
    if (o->removed.count == SIZE_MAX / sizeof(Packet *)) return false;
    Packet *p = packet_new(c->identity, o->maximum_bytes);
    if (!p) return false;
    if (!packet_log_reserve(&o->removed, o->removed.count + 1) ||
            !tombstone_reserve(&o->tombstones, o->tombstones.occupied + 1)) {
        packet_free(p); return false;
    }
    ++o->version;
    p->added_at = o->version;
    p->hash = bytes_hash(p->bytes, p->size);
    packet_log_push_reserved(&o->removed, p);
    tombstone_insert_reserved(&o->tombstones, p);
    return true;
}

static bool cursor_wait_pending(Cursor *c, SpaceReadToken read) {
    SpaceSourceWait wait = {0};
    if (!c->owner->ops->pending_wait ||
            !c->owner->ops->pending_wait(c->base, &wait)) return false;
    unsigned parked = cetta_shared_transition_park();
    if (wait.wait) wait.wait(wait.context);
    space_source_wait_clear(&wait);
    cetta_shared_transition_resume(parked);
    return space_read_token_is_current(read);
}

static CettaMatchCompletion source_remove(Space *space, Atom *atom,
        CettaMatchCompletion (*equal)(Atom *, Atom *),
        CettaMatchCompletion (*alpha_equal)(Atom *, Atom *), bool representation) {
    CETTA_SCOPED_SHARED_TRANSITION(publication);
    Overlay *o = space->match_backend.source;
    SpaceReadToken read = space_read_token(space);
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
                if (cursor_wait_pending(c, read)) continue;
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
    Packet **base = NULL, **local_rows = NULL;
    size_t count = 0, capacity = 0, locals = 0, local_capacity = 0;
    void *raw = NULL; SpaceOccurrenceCursorStep failure = SPACE_OCCURRENCE_CURSOR_CAPACITY;
    /* Removing every match may read only the base's selection for the
     * pattern: it holds every row the pattern can match, so it removes the
     * rows a full scan removes. A bounded removal keeps the scan, because a
     * selection need not follow row order (an index yields its exact-value
     * rows before the rows that hold a variable there) and would take other
     * occurrences. */
    if (!source_open(space, maximum == UINT64_MAX ? pattern : NULL, &raw, &failure))
        return CETTA_MATCH_INCOMPLETE;
    Cursor *c = raw; CettaIndex index; SpaceOccurrenceCursorStep step;
    while ((step = cursor_next(c, &index)) == SPACE_OCCURRENCE_CURSOR_ITEM ||
            step == SPACE_OCCURRENCE_CURSOR_PROGRESS) {
        if (step == SPACE_OCCURRENCE_CURSOR_PROGRESS) {
            if (cursor_wait_pending(c, read)) continue;
            step = SPACE_OCCURRENCE_CURSOR_INCOMPLETE; break;
        }
        CettaMatchCompletion comparison = matches(c->atom, pattern);
        if (comparison == CETTA_MATCH_INCOMPLETE) { step = SPACE_OCCURRENCE_CURSOR_INCOMPLETE; break; }
        if (comparison != CETTA_MATCH_MATCHED) continue;
        if (c->local) {
            if (!packets_reserve(&local_rows, &local_capacity, locals + 1)) {
                step = SPACE_OCCURRENCE_CURSOR_CAPACITY; break;
            }
            local_rows[locals++] = c->local;
        } else {
            Packet *identity = packet_new(c->identity, o->maximum_bytes);
            if (!identity || !packets_reserve(&base, &capacity, count + 1)) {
                packet_free(identity); step = SPACE_OCCURRENCE_CURSOR_CAPACITY; break;
            }
            base[count++] = identity;
        }
        if (count + locals == maximum) break;
    }
    bool complete = (step == SPACE_OCCURRENCE_CURSOR_END || count + locals == maximum) &&
        space_read_token_is_current(read) &&
        count <= SIZE_MAX / sizeof(Packet *) - o->removed.count &&
        count <= UINT64_MAX - locals;
    if (complete && count && (!packet_log_reserve(&o->removed, o->removed.count + count) ||
            !tombstone_reserve(&o->tombstones, o->tombstones.occupied + count)))
        complete = false;
    if (!complete) {
        packets_free(base, count); free(local_rows); cursor_free(c);
        return CETTA_MATCH_INCOMPLETE;
    }
    if (count || locals) {
        ++o->version;
        for (size_t i = 0; i < locals; ++i) {
            if (local_rows[i]->removed_at == UINT64_MAX) {
                local_rows[i]->removed_at = o->version;
                --o->added_live;
            }
        }
        for (size_t i = 0; i < count; ++i) {
            base[i]->added_at = o->version;
            base[i]->hash = bytes_hash(base[i]->bytes, base[i]->size);
            packet_log_push_reserved(&o->removed, base[i]);
            tombstone_insert_reserved(&o->tombstones, base[i]);
        }
    }
    *removed = count + locals;
    free(base); free(local_rows); cursor_free(c);
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

static void packet_log_storage(const PacketLog *log, CettaStorageCensus *census) {
    cetta_storage_array(census, log->chunks, log->chunk_capacity, sizeof(*log->chunks), 0);
    for (size_t i = 0; i < log->chunk_count; ++i)
        cetta_storage_array(census, log->chunks[i], PACKET_CHUNK_SIZE, sizeof(**log->chunks), 0);
    for (size_t i = 0; i < log->count; ++i)
        packet_storage(packet_log_at(log, i), census);
}

static void owner_storage(const Overlay *o, CettaStorageCensus *census) {
    if (!cetta_storage_object(census, o, sizeof(*o), CETTA_STORAGE_BLOCK)) return;
    space_storage_census(o->base, census);
    packet_log_storage(&o->added, census);
    packet_log_storage(&o->removed, census);
    cetta_storage_array(census, o->tombstones.buckets, o->tombstones.capacity,
        sizeof(*o->tombstones.buckets), 0);
    cetta_storage_array(census, o->var_slots.slots, o->var_slots.capacity,
        sizeof(*o->var_slots.slots), 0);
    cetta_storage_array(census, o->pos_indices, o->pos_capacity,
        sizeof(*o->pos_indices), 0);
    for (size_t i = 0; i < o->pos_count; ++i) {
        const PosIndex *pi = o->pos_indices[i];
        if (!cetta_storage_object(census, pi, sizeof(*pi), CETTA_STORAGE_BLOCK)) continue;
        cetta_storage_array(census, pi->any_slots.slots, pi->any_slots.capacity,
            sizeof(*pi->any_slots.slots), 0);
        cetta_storage_array(census, pi->buckets, pi->bucket_capacity,
            sizeof(*pi->buckets), 0);
        for (size_t j = 0; j < pi->bucket_capacity; ++j) {
            const ExactBucket *b = pi->buckets[j];
            if (b && cetta_storage_object(census, b, sizeof(*b), CETTA_STORAGE_BLOCK))
                cetta_storage_array(census, b->slots.slots, b->slots.capacity,
                    sizeof(*b->slots.slots), 0);
        }
    }
    if (o->ops->storage) o->ops->storage(o->prototype, census);
}

static void source_storage(const Space *space, CettaStorageCensus *census) {
    owner_storage(space->match_backend.source, census);
}

static void cursor_storage(const void *raw, CettaStorageCensus *census) {
    const Cursor *c = raw;
    if (!cetta_storage_object(census, c, sizeof(*c), CETTA_STORAGE_BLOCK)) return;
    owner_storage(c->owner, census);
    arena_storage_census(&c->row, census);
    if (c->base && c->owner->ops->storage) c->owner->ops->storage(c->base, census);
}

static void source_free(Space *space) {
    overlay_release(space->match_backend.source); space->match_backend.source = NULL;
}

static size_t cursor_value_limit(const void *raw) { return ((const Cursor *)raw)->owner->maximum_bytes; }

static bool cursor_pending_wait(const void *raw, SpaceSourceWait *out) {
    const Cursor *c = raw;
    *out = (SpaceSourceWait){0};
    return !c->base_done && c->base && c->owner->ops->pending_wait &&
        c->owner->ops->pending_wait(c->base, out);
}

static const SpaceSourceCursorOps cursor_ops = {
    .clone = cursor_clone, .select = cursor_select, .next = cursor_next,
    .atom = cursor_atom, .identity = cursor_identity, .value_byte_limit = cursor_value_limit,
    .pending_wait = cursor_pending_wait, .free = cursor_free, .storage = cursor_storage,
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
    if (!space || !space_owner_is_current(space) || space->match_backend.ops != &overlay_ops ||
            !space->match_backend.source || !arena || !additions || !removals) return false;
    Overlay *o = space->match_backend.source;
    if (!space_read_token_is_current(o->read)) return false;
    Atom *lists[2];
    for (unsigned part = 0; part < 2; ++part) {
        size_t count = part ? o->removed.count : o->added_live;
        size_t total = part ? o->removed.count : o->added.count;
        const PacketLog *log = part ? &o->removed : &o->added;
        size_t bytes = 0;
        if (!atom_expr_allocation_bound((CettaExprLen)count, &bytes)) return false;
        lists[part] = atom_expr_builder_begin(arena, (CettaExprLen)count);
        if (!lists[part]) return false;
        size_t out_idx = 0;
        for (size_t i = 0; i < total; ++i) {
            Packet *p = packet_log_at(log, i);
            if (!part && p->removed_at != UINT64_MAX) continue;
            if (out_idx >= count || !(lists[part]->expr.elems[out_idx++] = atom_deep_copy(arena, p->atom)))
                return false;
        }
        if (out_idx != count) return false;
        lists[part] = atom_expr_builder_finish(arena, lists[part]);
        if (!lists[part]) return false;
    }
    *additions = lists[0]; *removals = lists[1]; return true;
}
