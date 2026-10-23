#include "library_postings.h"
#include "library.h"
#include "eval.h"
#include "name_key.h"
#include "tensor.h"
#include "space.h"
#include "space_match_backend.h"
#include "shared_transition.h"
#include "prepared_pure_machine.h"
#include "generated/postings_descriptors.generated.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Minimal library/unit builds may omit the Space execution provider.  The
 * borrowed observer is an optional capability there and declines before use. */
extern SpaceMatchPullVisitResult space_match_backend_try_visit_atoms_direct(
    Space *, Arena *, CettaMorkAtomVisitor, void *) __attribute__((weak));
extern void space_match_backend_clear_error(void) __attribute__((weak));
extern bool space_contains_only_exact_atoms(Space *) __attribute__((weak));
extern CettaCount space_length64(const Space *) __attribute__((weak));
extern Atom *space_get_at64(const Space *, CettaIndex) __attribute__((weak));
extern void cetta_shared_transition_guard_enter(CettaSharedTransitionGuard *)
    __attribute__((weak));
extern void cetta_shared_transition_guard_leave(CettaSharedTransitionGuard *)
    __attribute__((weak));
/* Minimal provider tests need no evaluator. The compiled fold is optional
 * there, just as direct source traversal is. */
#pragma weak eval_prepare_pure_value_call
#pragma weak cetta_prepared_pure_program_rebind_closed_entry_call
#pragma weak cetta_prepared_pure_program_execute_closed_controlled
#pragma weak cetta_prepared_pure_program_release_closed_execution
#pragma weak cetta_prepared_pure_program_clear_closed_entry_call
#pragma weak cetta_prepared_pure_program_is_current
#pragma weak cetta_prepared_pure_program_free

/* A prepared observation owns its complete snapshot. The offsets count
 * occurrences, including repeated positions; keys retain source row order. */
typedef struct {
    Arena keys;
    Atom **labels;
    size_t *offsets;
    int64_t *positions;
    size_t groups, count;
    Atom *reference;
} PostingsIndex;

/* A two-level immutable inverted index.  Each authored block contributes
 * one (group, position) occurrence to every outer key it names.  Outer keys
 * retain first-occurrence order; groups and positions are canonical numeric
 * order.  Repeated keys and repeated events remain repeated occurrences. */
typedef struct {
    NameKeyTable *keys;
    size_t *key_group_offsets;
    int64_t *labels;
    size_t *offsets;
    int64_t *positions;
    size_t key_count, groups, count;
    Arena reference_arena;
    Atom *reference;
} PostingsTable;

static const char *const index_kind = "postings-index-v1";
static const char *const table_kind = "postings-table-v1";

typedef enum {
    POSTINGS_COUNT_MODE_UNKNOWN = 0,
#define POSTINGS_COUNT_MODE_ENUM(id, wire, family, extent, result) \
    POSTINGS_COUNT_MODE_##id,
    CETTA_POSTINGS_COUNT_MODE_ROWS(POSTINGS_COUNT_MODE_ENUM)
#undef POSTINGS_COUNT_MODE_ENUM
} PostingsCountMode;

typedef enum {
    POSTINGS_COUNT_FAMILY_INDEX,
    POSTINGS_COUNT_FAMILY_TABLE,
    POSTINGS_COUNT_FAMILY_EITHER,
} PostingsCountFamily;

typedef enum {
    POSTINGS_COUNT_EXTENT_PREFIX,
    POSTINGS_COUNT_EXTENT_COMPLETE,
    POSTINGS_COUNT_EXTENT_METADATA,
} PostingsCountExtent;

typedef enum {
    POSTINGS_COUNT_RESULT_ROWS,
    POSTINGS_COUNT_RESULT_TENSOR,
    POSTINGS_COUNT_RESULT_STATISTICS,
    POSTINGS_COUNT_RESULT_FOLDROWS,
    POSTINGS_COUNT_RESULT_FOLDCOLUMNS,
} PostingsCountResult;

typedef struct {
    PostingsCountMode mode;
    const char *wire;
    PostingsCountFamily family;
    PostingsCountExtent extent;
    PostingsCountResult result;
} PostingsCountModeSpec;

static const PostingsCountModeSpec postings_count_modes[] = {
#define POSTINGS_COUNT_MODE_SPEC(id, wire, family, extent, result) \
    {POSTINGS_COUNT_MODE_##id, wire, POSTINGS_COUNT_FAMILY_##family, \
     POSTINGS_COUNT_EXTENT_##extent, POSTINGS_COUNT_RESULT_##result},
    CETTA_POSTINGS_COUNT_MODE_ROWS(POSTINGS_COUNT_MODE_SPEC)
#undef POSTINGS_COUNT_MODE_SPEC
};

static const PostingsCountModeSpec *postings_count_mode_spec(Atom *value) {
    if (!value || value->kind != ATOM_SYMBOL)
        return NULL;
    for (size_t index = 0u;
         index < sizeof(postings_count_modes) / sizeof(postings_count_modes[0]);
         index++) {
        if (atom_is_symbol(value, postings_count_modes[index].wire))
            return &postings_count_modes[index];
    }
    return NULL;
}

static Atom *fallback(Arena *arena) {
    return atom_symbol(arena, "Postings:Fallback");
}

static bool promoted_key_scalar(const Atom *value, void *context) {
    (void)context;
    return value->kind==ATOM_GROUNDED &&
        (value->ground.gkind==GV_FLOAT || value->ground.gkind==GV_BIGINT ||
         value->ground.gkind==GV_RATIONAL);
}

/* Authored matching may equate different numeric representations, recursively
 * inside a key. The structural name index cannot accelerate that equivalence.
 * Both ingress and lookup must decline: checking only ingress misses a later
 * floating query against an integer key. */
static bool postings_key_is_exact(Atom *key) {
    return name_key_is_admissible(key) &&
        !atom_tree_any(key,promoted_key_scalar,NULL);
}

static Atom *fault(Arena *arena, const char *reason) {
    return atom_expr2(arena, atom_symbol(arena, "Postings:Fault"), atom_symbol(arena, reason));
}

static bool sequence(Atom *value, Atom ***items, size_t *length) {
    if (!value || value->kind != ATOM_EXPR || value->expr.len > SIZE_MAX)
        return false;
    *items = value->expr.elems;
    *length = (size_t)value->expr.len;
    if (*length && (*items)[0] && (*items)[0]->kind == ATOM_GROUNDED &&
        (*items)[0]->ground.gkind == GV_INTERNAL_TAG &&
        (*items)[0]->ground.ival == CETTA_INTERNAL_TAG_LIST) {
        (*items)++; (*length)--;
    }
    return true;
}

static void release_index(void *payload) {
    PostingsIndex *index = payload;
    arena_free(&index->keys);
    free(index->labels);
    free(index->offsets);
    free(index->positions);
    free(index);
}

static void release_table(void *payload) {
    PostingsTable *table = payload;
    if (!table) return;
    name_key_table_delete(table->keys);
    free(table->key_group_offsets);
    free(table->labels);
    free(table->offsets);
    free(table->positions);
    arena_free(&table->reference_arena);
    free(table);
}

static bool tagged_payload(Atom *value, const char *tag, Atom **payload) {
    if (!value || value->kind != ATOM_EXPR || value->expr.len != 2u ||
        !atom_is_symbol(value->expr.elems[0], tag))
        return false;
    if (payload) *payload = value->expr.elems[1];
    return true;
}

static Atom *own_table_reference(CettaLibraryContext *context, Arena *arena,
                                 Atom *blocks) {
    PostingsTable *table = calloc(1, sizeof(*table));
    if (!table) return fault(arena, "allocation-failure");
    arena_init(&table->reference_arena);
    table->reference = atom_deep_copy(&table->reference_arena, blocks);
    if (!table->reference) {
        release_table(table);
        return fault(arena, "allocation-failure");
    }
    uint64_t id = 0u;
    if (!cetta_native_handle_alloc(
            context, table_kind, table, release_table, &id)) {
        release_table(table);
        return fault(arena, "allocation-failure");
    }
    Atom *value = cetta_native_handle_owned_atom(
        context, arena, table_kind, id);
    if (!value) {
        (void)cetta_native_handle_close(context, table_kind, id);
        return fault(arena, "allocation-failure");
    }
    return value;
}

/* Unsupported packing still has the same owned-snapshot lifetime. Its
 * observations run through the authored implementation, not a different
 * public value with a no-op close operation. */
static Atom *own_reference(CettaLibraryContext *context,Arena *arena,Atom *groups) {
    Atom *blocks = NULL;
    if (tagged_payload(groups, "Postings:Blocks", &blocks))
        return own_table_reference(context, arena, groups);
    PostingsIndex *index=calloc(1,sizeof(*index));
    if(!index) return fault(arena,"allocation-failure");
    arena_init(&index->keys);
    index->reference=atom_deep_copy(&index->keys,groups);
    uint64_t id=0;
    if(!cetta_native_handle_alloc(context,index_kind,index,release_index,&id)) {
        release_index(index); return fault(arena,"allocation-failure");
    }
    Atom *value=cetta_native_handle_owned_atom(context,arena,index_kind,id);
    if(!value) { (void)cetta_native_handle_close(context,index_kind,id); return fault(arena,"allocation-failure"); }
    return value;
}

typedef struct {
    NameId key;
    int64_t group;
    int64_t position;
} FlatTablePosting;

static int flat_table_posting_compare(const void *left, const void *right) {
    const FlatTablePosting *a = left, *b = right;
    if (a->key != b->key) return (a->key > b->key) - (a->key < b->key);
    if (a->group != b->group)
        return (a->group > b->group) - (a->group < b->group);
    return (a->position > b->position) - (a->position < b->position);
}

static Atom *publish_flat_table(CettaLibraryContext *context, Arena *arena,
                                PostingsTable *table,
                                FlatTablePosting *flat, size_t count) {
    if (count) qsort(flat, count, sizeof(*flat), flat_table_posting_compare);
    table->key_count = (size_t)name_key_count(table->keys);
    table->count = count;
    for (size_t i = 0u; i < count; i++)
        if (i == 0u || flat[i].key != flat[i - 1u].key ||
            flat[i].group != flat[i - 1u].group)
            table->groups++;

    table->key_group_offsets = calloc(
        table->key_count + 1u, sizeof(*table->key_group_offsets));
    table->labels = table->groups
        ? malloc(table->groups * sizeof(*table->labels)) : NULL;
    table->offsets = malloc((table->groups + 1u) * sizeof(*table->offsets));
    table->positions = count ? malloc(count * sizeof(*table->positions)) : NULL;
    if (!table->key_group_offsets || (table->groups && !table->labels) ||
        !table->offsets || (count && !table->positions)) {
        free(flat);
        release_table(table);
        return fallback(arena);
    }

    size_t group = 0u;
    for (size_t i = 0u; i < count; i++) {
        bool first = i == 0u || flat[i].key != flat[i - 1u].key ||
                     flat[i].group != flat[i - 1u].group;
        if (first) {
            table->labels[group] = flat[i].group;
            table->offsets[group] = i;
            table->key_group_offsets[flat[i].key]++;
            group++;
        }
        table->positions[i] = flat[i].position;
    }
    table->offsets[table->groups] = count;
    for (size_t key = 1u; key <= table->key_count; key++)
        table->key_group_offsets[key] += table->key_group_offsets[key - 1u];
    free(flat);

    uint64_t id = 0u;
    if (!cetta_native_handle_alloc(
            context, table_kind, table, release_table, &id)) {
        release_table(table);
        return fallback(arena);
    }
    Atom *result = cetta_native_handle_owned_atom(
        context, arena, table_kind, id);
    if (!result)
        (void)cetta_native_handle_close(context, table_kind, id);
    return result ? result : fallback(arena);
}

/* Compile blocks shaped as (outer-keys group position).  Validation of the
 * complete carrier precedes publication of a handle.  Internal allocations
 * may be abandoned freely because no partially built table is observable. */
static Atom *prepare_blocks(CettaLibraryContext *context, Arena *arena,
                            Atom *blocks) {
    Atom **rows = NULL;
    size_t row_count = 0u, count = 0u;
    if (!sequence(blocks, &rows, &row_count)) return fallback(arena);
    for (size_t row = 0u; row < row_count; row++) {
        Atom **parts = NULL, **keys = NULL;
        size_t part_count = 0u, key_count = 0u;
        if (!sequence(rows[row], &parts, &part_count) || part_count != 3u ||
            !sequence(parts[0], &keys, &key_count) ||
            !parts[1] || parts[1]->kind != ATOM_GROUNDED ||
            parts[1]->ground.gkind != GV_INT ||
            !parts[2] || parts[2]->kind != ATOM_GROUNDED ||
            parts[2]->ground.gkind != GV_INT ||
            key_count > SIZE_MAX - count) {
            return fallback(arena);
        }
        for (size_t key = 0u; key < key_count; key++)
            if (!postings_key_is_exact(keys[key])) return fallback(arena);
        count += key_count;
        if ((row & 1023u) == 0u && eval_native_interrupted()) return NULL;
    }
    if (count > SIZE_MAX / sizeof(FlatTablePosting)) return fallback(arena);

    PostingsTable *table = calloc(1, sizeof(*table));
    FlatTablePosting *flat = count ? malloc(count * sizeof(*flat)) : NULL;
    if (!table || (count && !flat)) {
        free(flat);
        free(table);
        return fallback(arena);
    }
    arena_init(&table->reference_arena);
    table->keys = name_key_table_new(64u);
    if (!table->keys) {
        free(flat);
        release_table(table);
        return fallback(arena);
    }

    size_t next = 0u;
    for (size_t row = 0u; row < row_count; row++) {
        Atom **parts = NULL, **keys = NULL;
        size_t part_count = 0u, key_count = 0u;
        (void)sequence(rows[row], &parts, &part_count);
        (void)sequence(parts[0], &keys, &key_count);
        for (size_t key = 0u; key < key_count; key++) {
            NameId id = name_key_intern(table->keys, keys[key]);
            if (id == NAME_ID_NONE) {
                free(flat);
                release_table(table);
                return fallback(arena);
            }
            flat[next++] = (FlatTablePosting){
                .key = id,
                .group = parts[1]->ground.ival,
                .position = parts[2]->ground.ival,
            };
        }
        if ((row & 1023u) == 0u && eval_native_interrupted()) {
            free(flat);
            release_table(table);
            return NULL;
        }
    }
    return publish_flat_table(context, arena, table, flat, count);
}

typedef struct {
    PostingsTable *table;
    FlatTablePosting *flat;
    size_t count;
    size_t capacity;
    size_t rows;
    Atom *head;
    Atom **prefix;
    size_t prefix_count;
    Atom **forbidden;
    size_t forbidden_count;
    size_t keys_field;
    size_t group_field;
    size_t position_field;
    size_t row_width;
    bool invalid;
    bool unsupported;
    bool allocation_failure;
    bool interrupted;
} SpaceBlockBuilder;

static bool space_block_push(SpaceBlockBuilder *builder, Atom *key,
                             int64_t group, int64_t position) {
    if (!postings_key_is_exact(key)) {
        builder->invalid = true;
        return true;
    }
    if (builder->count == builder->capacity) {
        size_t capacity = builder->capacity ? builder->capacity * 2u : 1024u;
        if (capacity < builder->capacity ||
            capacity > SIZE_MAX / sizeof(*builder->flat)) {
            builder->allocation_failure = true;
            return false;
        }
        FlatTablePosting *flat = realloc(
            builder->flat, capacity * sizeof(*flat));
        if (!flat) {
            builder->allocation_failure = true;
            return false;
        }
        builder->flat = flat;
        builder->capacity = capacity;
    }
    NameId id = name_key_intern(builder->table->keys, key);
    if (id == NAME_ID_NONE) {
        builder->allocation_failure = true;
        return false;
    }
    builder->flat[builder->count++] = (FlatTablePosting){
        .key = id,
        .group = group,
        .position = position,
    };
    return true;
}

static bool space_block_visit(Atom *row, void *raw_builder) {
    SpaceBlockBuilder *builder = raw_builder;
    if (!builder || !row) return false;
    builder->rows++;
    if ((builder->rows & 1023u) == 0u && eval_native_interrupted()) {
        builder->interrupted = true;
        return false;
    }
    if (row->kind != ATOM_EXPR || row->expr.len == 0u)
        return true;
    Atom *row_head = row->expr.elems[0];
    for (size_t index = 0u; index < builder->forbidden_count; index++) {
        if (atom_eq(row_head, builder->forbidden[index])) {
            builder->unsupported = true;
            return true;
        }
    }
    if (!atom_eq(row_head, builder->head))
        return true;
    if ((size_t)row->expr.len != builder->row_width) {
        builder->invalid = true;
        return true;
    }
    Atom **keys = NULL;
    size_t key_count = 0u;
    Atom *group = row->expr.elems[builder->group_field];
    Atom *position = row->expr.elems[builder->position_field];
    if (!sequence(row->expr.elems[builder->keys_field], &keys, &key_count) ||
        !group || group->kind != ATOM_GROUNDED ||
        group->ground.gkind != GV_INT ||
        !position || position->kind != ATOM_GROUNDED ||
        position->ground.gkind != GV_INT) {
        builder->invalid = true;
        return true;
    }
    for (size_t index = 0u; index < builder->prefix_count; index++)
        if (!space_block_push(builder, builder->prefix[index],
                              group->ground.ival, position->ground.ival))
            return false;
    for (size_t index = 0u; index < key_count; index++)
        if (!space_block_push(builder, keys[index],
                              group->ground.ival, position->ground.ival))
            return false;
    return true;
}

static bool nonnegative_field(Atom *value, size_t *field) {
    if (!value || value->kind != ATOM_GROUNDED ||
        value->ground.gkind != GV_INT || value->ground.ival < 0 ||
        (uint64_t)value->ground.ival > SIZE_MAX)
        return false;
    *field = (size_t)value->ground.ival;
    return true;
}

static SpaceMatchPullVisitResult visit_native_space_blocks(
        Space *source, SpaceBlockBuilder *builder) {
    /* The caller holds the shared read guard and admits the source before
     * choosing either physical visitor. Neither route composes views. */
    if (!space_length64 || !space_get_at64 || !source || !builder ||
        (source->match_backend.kind != SPACE_ENGINE_NATIVE &&
         source->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    CettaCount count = space_length64(source);
    for (CettaIndex index = 0u; index < count; index++) {
        Atom *row = space_get_at64(source, index);
        if (!row || !space_block_visit(row, builder))
            return SPACE_MATCH_PULL_VISIT_TERMINATED;
    }
    return SPACE_MATCH_PULL_VISIT_COMPLETE;
}

static SpaceMatchPullVisitResult visit_exact_space_blocks(
        Space *source, SpaceBlockBuilder *builder) {
    if (!space_contains_only_exact_atoms ||
        !cetta_shared_transition_guard_enter ||
        !cetta_shared_transition_guard_leave)
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (eval_resolve_space_read) source = eval_resolve_space_read(source);
    if (!source || source->overlay_base || space_has_dependencies(source) ||
        !space_contains_only_exact_atoms(source))
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    Arena scratch;
    arena_init(&scratch);
    SpaceMatchPullVisitResult visited = space_match_backend_try_visit_atoms_direct
        ? space_match_backend_try_visit_atoms_direct(
            source, &scratch, space_block_visit, builder)
        : SPACE_MATCH_PULL_VISIT_DECLINED;
    if (visited == SPACE_MATCH_PULL_VISIT_DECLINED)
        visited = visit_native_space_blocks(source, builder);
    arena_free(&scratch);
    /* Keys have been interned into the private builder. Sorting and handle
     * publication do not need to retain the source's shared read guard. */
    return visited;
}

/* Borrow an exact source snapshot and project its declared block rows directly
 * into the same immutable table representation used by prepare_blocks.  This
 * is an attempt: unsupported source/layout cases publish nothing and let the
 * authored block construction remain the semantic authority. */
static Atom *prepare_space_blocks(CettaLibraryContext *context, Arena *arena,
                                  Atom *request) {
    const char *reference = getenv("CETTA_POSTINGS_SPACE_REFERENCE");
    if (reference && *reference && strcmp(reference, "0"))
        return fallback(arena);
    if (!request || request->kind != ATOM_EXPR || request->expr.len != 3u ||
        !atom_is_symbol(request->expr.elems[0], "Postings:SpaceBlocks") ||
        eval_current_effective_fuel_limit() >= 0)
        return fallback(arena);
    Atom *source_atom = request->expr.elems[1];
    Atom *layout = request->expr.elems[2];
    if (!source_atom || source_atom->kind != ATOM_GROUNDED ||
        source_atom->ground.gkind != GV_SPACE || !source_atom->ground.ptr ||
        !layout || layout->kind != ATOM_EXPR ||
        layout->expr.len != CETTA_POSTINGS_BLOCK_LAYOUT_LENGTH ||
        !atom_is_symbol(layout->expr.elems[0], CETTA_POSTINGS_BLOCK_LAYOUT_HEAD) ||
        !layout->expr.elems[CETTA_POSTINGS_BLOCK_ROWHEAD] ||
        layout->expr.elems[CETTA_POSTINGS_BLOCK_ROWHEAD]->kind != ATOM_SYMBOL)
        return fallback(arena);

    Atom **prefix = NULL, **forbidden = NULL;
    size_t prefix_count = 0u, forbidden_count = 0u;
    SpaceBlockBuilder builder = {
        .head = layout->expr.elems[CETTA_POSTINGS_BLOCK_ROWHEAD]};
    if (!nonnegative_field(layout->expr.elems[CETTA_POSTINGS_BLOCK_ROWWIDTH],
                           &builder.row_width) || !builder.row_width ||
        !nonnegative_field(layout->expr.elems[CETTA_POSTINGS_BLOCK_KEYSFIELD],
                           &builder.keys_field) ||
        !nonnegative_field(layout->expr.elems[CETTA_POSTINGS_BLOCK_GROUPFIELD],
                           &builder.group_field) ||
        !nonnegative_field(layout->expr.elems[CETTA_POSTINGS_BLOCK_POSITIONFIELD],
                           &builder.position_field) ||
        builder.keys_field >= builder.row_width ||
        builder.group_field >= builder.row_width ||
        builder.position_field >= builder.row_width ||
        !sequence(layout->expr.elems[CETTA_POSTINGS_BLOCK_PREFIXKEYS],
                  &prefix, &prefix_count) ||
        !sequence(layout->expr.elems[CETTA_POSTINGS_BLOCK_FORBIDDENHEADS],
                  &forbidden, &forbidden_count))
        return fallback(arena);
    for (size_t index = 0u; index < prefix_count; index++)
        if (!postings_key_is_exact(prefix[index])) return fallback(arena);
    for (size_t index = 0u; index < forbidden_count; index++)
        if (!forbidden[index] || forbidden[index]->kind != ATOM_SYMBOL)
            return fallback(arena);
    builder.prefix = prefix;
    builder.prefix_count = prefix_count;
    builder.forbidden = forbidden;
    builder.forbidden_count = forbidden_count;

    builder.table = calloc(1, sizeof(*builder.table));
    if (!builder.table) return fallback(arena);
    arena_init(&builder.table->reference_arena);
    builder.table->keys = name_key_table_new(64u);
    if (!builder.table->keys) {
        release_table(builder.table);
        return fallback(arena);
    }

    SpaceMatchPullVisitResult visited = visit_exact_space_blocks(
        source_atom->ground.ptr, &builder);
    if (builder.interrupted) {
        free(builder.flat);
        release_table(builder.table);
        return NULL;
    }
    if (visited != SPACE_MATCH_PULL_VISIT_COMPLETE || builder.invalid ||
        builder.unsupported || builder.allocation_failure) {
        free(builder.flat);
        release_table(builder.table);
        if (space_match_backend_clear_error)
            space_match_backend_clear_error();
        return fallback(arena);
    }
    return publish_flat_table(
        context, arena, builder.table, builder.flat, builder.count);
}

typedef struct { int64_t key, position; } FlatPosting;
static int flat_posting_compare(const void *a, const void *b) {
    const FlatPosting *x = a, *y = b;
    if (x->key != y->key) return (x->key > y->key) - (x->key < y->key);
    return (x->position > y->position) - (x->position < y->position);
}

/* Canonically key-ordered grouping of an occurrence stream. Every occurrence
 * contributes a position, including repeats; this is not coordinate dedup. */
static Atom *prepare_events(CettaLibraryContext *context, Arena *arena, Atom *events) {
    Atom **rows = NULL; size_t n = 0;
    if (!sequence(events, &rows, &n) || n > INT64_MAX || n > SIZE_MAX / sizeof(FlatPosting))
        return fallback(arena);
    FlatPosting *flat = n ? malloc(n * sizeof(*flat)) : NULL;
    if (n && !flat) return fallback(arena);
    for (size_t i = 0; i < n; i++) {
        Atom **pair = NULL; size_t length = 0;
        if (!sequence(rows[i], &pair, &length) || length != 2 ||
            !pair[0] || !pair[1] || pair[0]->kind != ATOM_GROUNDED || pair[1]->kind != ATOM_GROUNDED ||
            pair[0]->ground.gkind != GV_INT || pair[1]->ground.gkind != GV_INT) {
            free(flat); return fallback(arena);
        }
        if ((i & 1023u) == 0u && eval_native_interrupted()) { free(flat); return NULL; }
        flat[i] = (FlatPosting){pair[0]->ground.ival, pair[1]->ground.ival};
    }
    if (n) qsort(flat, n, sizeof(*flat), flat_posting_compare);
    size_t groups = 0;
    for (size_t i = 0; i < n; i++) if (!i || flat[i].key != flat[i-1].key) groups++;
    PostingsIndex *index = calloc(1, sizeof(*index));
    if (!index) { free(flat); return fallback(arena); }
    arena_init(&index->keys);
    index->labels = groups ? malloc(groups * sizeof(*index->labels)) : NULL;
    index->offsets = malloc((groups + 1) * sizeof(*index->offsets));
    index->positions = n ? malloc(n * sizeof(*index->positions)) : NULL;
    if ((groups && !index->labels) || !index->offsets || (n && !index->positions)) {
        free(flat); release_index(index); return fallback(arena);
    }
    index->groups = groups; index->count = n;
    size_t group = 0;
    for (size_t i = 0; i < n; i++) {
        if (!i || flat[i].key != flat[i-1].key) {
            index->labels[group] = atom_int(&index->keys, flat[i].key);
            index->offsets[group++] = i;
        }
        index->positions[i] = flat[i].position;
    }
    index->offsets[groups] = n; free(flat);
    uint64_t id = 0;
    if (!cetta_native_handle_alloc(context, index_kind, index, release_index, &id)) {
        release_index(index); return fallback(arena);
    }
    Atom *result = cetta_native_handle_owned_atom(context, arena, index_kind, id);
    if (!result) (void)cetta_native_handle_close(context, index_kind, id);
    return result ? result : fallback(arena);
}

static Atom *prepare(CettaLibraryContext *context, Arena *arena, Atom *groups) {
    Atom *blocks = NULL;
    if (groups && groups->kind == ATOM_EXPR && groups->expr.len == 3u &&
        atom_is_symbol(groups->expr.elems[0], "Postings:SpaceBlocks"))
        return prepare_space_blocks(context, arena, groups);
    if (tagged_payload(groups, "Postings:Blocks", &blocks))
        return prepare_blocks(context, arena, blocks);
    if (groups && groups->kind == ATOM_EXPR && groups->expr.len == 2 &&
        atom_is_symbol(groups->expr.elems[0], "Postings:Events"))
        return prepare_events(context, arena, groups->expr.elems[1]);
    Atom **rows = NULL;
    size_t length = 0, count = 0;
    if (!sequence(groups, &rows, &length) || length > SIZE_MAX / sizeof(Atom *) ||
        length >= SIZE_MAX / sizeof(size_t)) return fallback(arena);
    /* Validate before retaining a resource or publishing any result. */
    for (size_t group = 0; group < length; group++) {
        if ((group & 1023u) == 0u && eval_native_interrupted()) return NULL;
        Atom **pair = NULL, **positions = NULL;
        size_t pair_length = 0, n = 0;
        if (!sequence(rows[group], &pair, &pair_length) || pair_length != 2 || !pair[0] ||
            atom_has_vars(pair[0]) || !(pair[0]->flags & ATOM_FLAG_TERM_STABLE) ||
            !sequence(pair[1], &positions, &n) || n > (size_t)INT64_MAX ||
            n > SIZE_MAX / sizeof(int64_t) ||
            count > SIZE_MAX / sizeof(int64_t) - n) return fallback(arena);
        int64_t previous = INT64_MIN;
        for (size_t i = 0; i < n; i++) {
            Atom *position = positions[i];
            if (!position || position->kind != ATOM_GROUNDED ||
                position->ground.gkind != GV_INT || position->ground.ival < previous)
                return fallback(arena);
            previous = position->ground.ival;
        }
        count += n;
    }
    PostingsIndex *index = calloc(1, sizeof(*index));
    if (!index) return fallback(arena);
    arena_init(&index->keys);
    index->labels = length ? malloc(length * sizeof(*index->labels)) : NULL;
    index->offsets = malloc((length + 1) * sizeof(*index->offsets));
    index->positions = count ? malloc(count * sizeof(*index->positions)) : NULL;
    if ((length && !index->labels) || !index->offsets || (count && !index->positions)) {
        release_index(index); return fallback(arena);
    }
    index->groups = length; index->count = count;
    size_t next = 0;
    for (size_t group = 0; group < length; group++) {
        Atom **pair = NULL, **positions = NULL;
        size_t pair_length = 0, n = 0;
        (void)sequence(rows[group], &pair, &pair_length);
        (void)sequence(pair[1], &positions, &n);
        index->labels[group] = atom_deep_copy(&index->keys, pair[0]);
        index->offsets[group] = next;
        for (size_t i = 0; i < n; i++) index->positions[next++] = positions[i]->ground.ival;
    }
    index->offsets[length] = next;
    uint64_t id = 0;
    if (!cetta_native_handle_alloc(context, index_kind, index, release_index, &id)) {
        release_index(index); return fallback(arena);
    }
    Atom *value = cetta_native_handle_owned_atom(context, arena, index_kind, id);
    if (!value) {
        (void)cetta_native_handle_close(context, index_kind, id);
        return fallback(arena);
    }
    return value;
}

static size_t lower_bound(const int64_t *values, size_t start, size_t stop, int64_t bound) {
    size_t lo = start, hi = stop;
    while (lo < hi) {
        size_t middle = lo + (hi - lo) / 2;
        if (values[middle] < bound) lo = middle + 1;
        else hi = middle;
    }
    return lo - start;
}

static Atom *observe(Arena *arena, PostingsIndex *index, Atom *bound, bool all, bool inspect) {
    if(index->reference) return inspect ? atom_deep_copy(arena,index->reference) : fallback(arena);
    if (!inspect && !all && (!bound || bound->kind != ATOM_GROUNDED ||
        bound->ground.gkind != GV_INT)) return fallback(arena);
    Atom **rows = index->groups ? arena_alloc(arena, index->groups * sizeof(*rows)) : NULL;
    size_t published = 0;
    for (size_t group = 0; group < index->groups; group++) {
        if ((group & 1023u) == 0u && eval_native_interrupted()) return NULL;
        size_t start = index->offsets[group], stop = index->offsets[group + 1];
        size_t count = all || inspect ? stop - start :
            lower_bound(index->positions, start, stop, bound->ground.ival);
        if (!inspect && !count) continue;
        Atom *value = NULL;
        if (inspect) {
            Atom **items = count ? arena_alloc(arena, count * sizeof(*items)) : NULL;
            for (size_t i = 0; i < count; i++) items[i] = atom_int(arena, index->positions[start + i]);
            value = atom_expr(arena, items, count);
        } else value = atom_int(arena, (int64_t)count);
        rows[published++] = atom_expr2(arena, atom_deep_copy(arena, index->labels[group]), value);
    }
    return atom_expr(arena, rows, published);
}

static Atom *stats(Arena *arena,PostingsIndex *index) {
    size_t bytes=sizeof(*index),charge;
    size_t arena_bytes=index->keys.reserved_bytes;
    if(__builtin_add_overflow(arena_bytes,index->keys.external_bytes,&arena_bytes) ||
       __builtin_add_overflow(arena_bytes,index->keys.symbol_cache_bytes,&arena_bytes) ||
       __builtin_add_overflow(bytes,arena_bytes,&bytes)) return fault(arena,"size-overflow");
#define CHARGE(n,w) do { if(__builtin_mul_overflow((size_t)(n),(size_t)(w),&charge) || __builtin_add_overflow(bytes,charge,&bytes)) return fault(arena,"size-overflow"); } while(0)
    if(!index->reference) { CHARGE(index->groups,sizeof(Atom *)); CHARGE(index->groups+1,sizeof(size_t)); CHARGE(index->count,sizeof(int64_t)); }
#undef CHARGE
    char text[32]; snprintf(text,sizeof(text),"%zu",bytes);
    Atom *retained=bytes<=INT64_MAX ? atom_int(arena,(int64_t)bytes) : atom_bigint(arena,text);
    return atom_expr3(arena,atom_symbol(arena,"PostingsStats"),
        atom_symbol(arena,index->reference ? "authored" : "packed"),retained);
}

/* Numeric group identities are coordinates, not observations of labels as
 * syntax. Count directly into packed storage without one Atom per count. */
static Atom *tensor_counts(Arena *arena, PostingsIndex *index, Atom *request, bool all) {
    Atom **parts = NULL; size_t length = 0;
    if (index->reference || !sequence(request, &parts, &length) || length != 2 ||
        !parts[1] || parts[1]->kind != ATOM_GROUNDED ||
        parts[1]->ground.gkind != GV_INT || parts[1]->ground.ival < 0 ||
        (uint64_t)parts[1]->ground.ival > SIZE_MAX ||
        (!all && (!parts[0] || parts[0]->kind != ATOM_GROUNDED || parts[0]->ground.gkind != GV_INT)))
        return fallback(arena);
    size_t dimension = (size_t)parts[1]->ground.ival;
    if (index->groups > SIZE_MAX / sizeof(size_t) || index->groups > SIZE_MAX / sizeof(int64_t))
        return fallback(arena);
    for (size_t i = 0; i < index->groups; i++)
        if (!index->labels[i] || index->labels[i]->kind != ATOM_GROUNDED ||
            index->labels[i]->ground.gkind != GV_INT || index->labels[i]->ground.ival < 0 ||
            (uint64_t)index->labels[i]->ground.ival >= dimension)
            return fallback(arena);
    size_t *coordinates = index->groups ? malloc(index->groups * sizeof(*coordinates)) : NULL;
    int64_t *values = index->groups ? malloc(index->groups * sizeof(*values)) : NULL;
    if (index->groups && (!coordinates || !values)) {
        free(coordinates); free(values); return fallback(arena);
    }
    size_t used = 0;
    for (size_t i = 0; i < index->groups; i++) {
        if ((i & 1023u) == 0u && eval_native_interrupted()) {
            free(coordinates); free(values); return NULL;
        }
        size_t begin = index->offsets[i], end = index->offsets[i + 1];
        size_t count = all ? end - begin : lower_bound(index->positions, begin, end, parts[0]->ground.ival);
        if (!count) continue;
        coordinates[used] = (size_t)index->labels[i]->ground.ival;
        values[used++] = (int64_t)count;
    }
    CettaTensor *tensor = NULL;
    CettaTensorStatus status = cetta_tensor_coo(CETTA_TENSOR_I64, &dimension, 1,
        coordinates, values, used, CETTA_TENSOR_REJECT, &tensor);
    free(coordinates); free(values);
    if (status != CETTA_TENSOR_OK) { cetta_tensor_release(tensor); return fallback(arena); }
    Atom *result = cetta_tensor_atom(arena, tensor); cetta_tensor_release(tensor);
    return result;
}

static Atom *table_inspect(Arena *arena, PostingsTable *table) {
    if (table->reference)
        return atom_deep_copy(arena, table->reference);
    Atom **outer = table->key_count
        ? arena_alloc(arena, table->key_count * sizeof(*outer)) : NULL;
    for (size_t key = 0u; key < table->key_count; key++) {
        size_t begin = table->key_group_offsets[key];
        size_t end = table->key_group_offsets[key + 1u];
        Atom **groups = end > begin
            ? arena_alloc(arena, (end - begin) * sizeof(*groups)) : NULL;
        for (size_t group = begin; group < end; group++) {
            size_t first = table->offsets[group];
            size_t last = table->offsets[group + 1u];
            Atom **positions = last > first
                ? arena_alloc(arena, (last - first) * sizeof(*positions)) : NULL;
            for (size_t i = first; i < last; i++)
                positions[i - first] = atom_int(arena, table->positions[i]);
            groups[group - begin] = atom_expr2(
                arena, atom_int(arena, table->labels[group]),
                atom_expr(arena, positions, last - first));
        }
        const Atom *label = name_key_lookup(table->keys, (NameId)(key + 1u));
        if (!label) return fault(arena, "invalid-key-table");
        outer[key] = atom_expr2(
            arena, atom_deep_copy(arena, (Atom *)label),
            atom_expr(arena, groups, end - begin));
        if ((key & 1023u) == 0u && eval_native_interrupted()) return NULL;
    }
    return atom_expr(arena, outer, table->key_count);
}

static Atom *table_counts(Arena *arena, PostingsTable *table, Atom *request,
                          bool all, bool tensor_result) {
    if (table->reference) return fallback(arena);
    Atom **parts = NULL;
    size_t length = 0u;
    if (!sequence(request, &parts, &length) || length != 3u ||
        !postings_key_is_exact(parts[0]) ||
        !parts[1] || parts[1]->kind != ATOM_GROUNDED ||
        parts[1]->ground.gkind != GV_INT ||
        !parts[2] || parts[2]->kind != ATOM_GROUNDED ||
        parts[2]->ground.gkind != GV_INT || parts[2]->ground.ival < 0 ||
        (uint64_t)parts[2]->ground.ival > SIZE_MAX)
        return fallback(arena);
    size_t dimension = (size_t)parts[2]->ground.ival;
    NameId id = name_key_find(table->keys, parts[0]);
    size_t begin = 0u, end = 0u;
    if (id != NAME_ID_NONE) {
        begin = table->key_group_offsets[id - 1u];
        end = table->key_group_offsets[id];
    }
    size_t groups = end - begin;
    size_t *coordinates = tensor_result && groups
        ? malloc(groups * sizeof(*coordinates)) : NULL;
    int64_t *values = tensor_result && groups
        ? malloc(groups * sizeof(*values)) : NULL;
    Atom **rows = !tensor_result && groups
        ? arena_alloc(arena, groups * sizeof(*rows)) : NULL;
    if (tensor_result && groups && (!coordinates || !values)) {
        free(coordinates);
        free(values);
        return fallback(arena);
    }
    size_t used = 0u;
    for (size_t group = begin; group < end; group++) {
        int64_t label = table->labels[group];
        if (tensor_result &&
            (label < 0 || (uint64_t)label >= dimension)) {
            free(coordinates);
            free(values);
            return fallback(arena);
        }
        size_t first = table->offsets[group];
        size_t last = table->offsets[group + 1u];
        size_t count = all ? last - first
            : lower_bound(table->positions, first, last,
                          parts[1]->ground.ival);
        if (!count) continue;
        if (count > INT64_MAX) {
            free(coordinates);
            free(values);
            return fallback(arena);
        }
        if (tensor_result) {
            coordinates[used] = (size_t)label;
            values[used] = (int64_t)count;
        } else {
            rows[used] = atom_expr2(
                arena, atom_int(arena, label), atom_int(arena, (int64_t)count));
        }
        used++;
        if ((group & 1023u) == 0u && eval_native_interrupted()) {
            free(coordinates);
            free(values);
            return NULL;
        }
    }
    if (!tensor_result) return atom_expr(arena, rows, used);
    CettaTensor *tensor = NULL;
    CettaTensorStatus status = cetta_tensor_coo(
        CETTA_TENSOR_I64, &dimension, 1u, coordinates, values, used,
        CETTA_TENSOR_REJECT, &tensor);
    free(coordinates);
    free(values);
    if (status != CETTA_TENSOR_OK) {
        cetta_tensor_release(tensor);
        return fallback(arena);
    }
    Atom *result = cetta_tensor_atom(arena, tensor);
    cetta_tensor_release(tensor);
    return result;
}

typedef struct { bool real; union { int64_t integer; double floating; }; } FoldNumber;
typedef struct {
    int64_t *labels;
    FoldNumber *states;
    size_t *slots;
    size_t used, capacity, width;
} TableFold;

typedef struct { CettaTensorDType dtype; bool real; } FoldColumnType;

static bool fold_column_type(Atom *value, FoldColumnType *type) {
#define COLUMN_TYPE(dtype, wire, floating) \
    if (atom_is_symbol(value, wire)) { \
        *type=(FoldColumnType){CETTA_TENSOR_##dtype, floating}; return true; \
    }
    CETTA_POSTINGS_COLUMN_DTYPE_ROWS(COLUMN_TYPE)
#undef COLUMN_TYPE
    return false;
}

/* Column storage is transposed directly from the private scalar table.
 * Its exact leaf-type gate is stricter than the authored constructors:
 * any required conversion takes that ordinary route before publication. */
static Atom *fold_columns(Arena *arena, const TableFold *fold,
                          const FoldColumnType *types) {
    CettaTensor *labels=NULL, *columns[32]={0};
    Atom *result=NULL;
    size_t size=fold->used;
    if (size>SIZE_MAX/sizeof(int64_t) ||
        cetta_tensor_new(CETTA_TENSOR_I64,&size,1u,fold->labels,
                         size*sizeof(int64_t),&labels)!=CETTA_TENSOR_OK)
        goto cleanup;
    for (size_t column=0; column<fold->width; column++) {
        size_t width=cetta_tensor_itemsize(types[column].dtype);
        if (width!=sizeof(int64_t) || width!=sizeof(double) || size>SIZE_MAX/width)
            goto cleanup;
        unsigned char *data=size ? malloc(size*width) : NULL;
        if (size && !data) goto cleanup;
        bool valid=true;
        for (size_t row=0; row<size; row++) {
            if ((row&1023u)==0u && eval_native_interrupted()) { valid=false; break; }
            FoldNumber value=fold->states[row*fold->width+column];
            if (value.real!=types[column].real) { valid=false; break; }
            if (value.real) memcpy(data+row*width,&value.floating,width);
            else memcpy(data+row*width,&value.integer,width);
        }
        if (!valid) { free(data); goto cleanup; }
        if (cetta_tensor_new_take(types[column].dtype,&size,1u,data,size*width,
                                 &columns[column])!=CETTA_TENSOR_OK) {
            free(data); goto cleanup;
        }
    }
    Atom *values[32], *record[CETTA_POSTINGS_COLUMNS_LENGTH];
    record[0]=atom_symbol(arena,CETTA_POSTINGS_COLUMNS_HEAD);
    record[CETTA_POSTINGS_COLUMNS_LABELSCOLUMN]=cetta_tensor_atom(arena,labels);
    for (size_t column=0; column<fold->width; column++)
        values[column]=cetta_tensor_atom(arena,columns[column]);
    record[CETTA_POSTINGS_COLUMNS_VALUECOLUMNS]=atom_expr(arena,values,fold->width);
    result=atom_expr(arena,record,CETTA_POSTINGS_COLUMNS_LENGTH);
cleanup:
    cetta_tensor_release(labels);
    for (size_t column=0; column<fold->width; column++) cetta_tensor_release(columns[column]);
    return result;
}

static bool fold_number(Atom *value, FoldNumber *number) {
    if (!value || value->kind != ATOM_GROUNDED) return false;
    if (value->ground.gkind == GV_INT) {
        *number = (FoldNumber){.real=false, .integer=value->ground.ival};
        return true;
    }
    if (value->ground.gkind != GV_FLOAT || !isfinite(value->ground.fval))
        return false;
    *number = (FoldNumber){.real=true, .floating=value->ground.fval};
    return true;
}

static bool fold_payload(Atom *value) {
    FoldNumber ignored;
    if (fold_number(value, &ignored)) return true;
    Atom **items; size_t count;
    if (!sequence(value, &items, &count) || count > 64u) return false;
    for (size_t i=0; i<count; i++)
        if (!fold_number(items[i], &ignored)) return false;
    return true;
}

static size_t fold_hash(int64_t label) {
    uint64_t hash=(uint64_t)label;
    hash^=hash>>30; hash*=UINT64_C(0xbf58476d1ce4e5b9);
    hash^=hash>>27; hash*=UINT64_C(0x94d049bb133111eb);
    return (size_t)(hash^(hash>>31));
}

static bool fold_grow(TableFold *fold) {
    size_t capacity=fold->capacity ? fold->capacity*2u : 16u;
    if (capacity < fold->capacity || capacity > SIZE_MAX/(2*sizeof(size_t)) ||
        capacity > SIZE_MAX/(fold->width*sizeof(FoldNumber)) ||
        capacity > SIZE_MAX/sizeof(int64_t)) return false;
    int64_t *labels=realloc(fold->labels,capacity*sizeof(*labels));
    if (!labels) return false;
    fold->labels=labels;
    FoldNumber *states=realloc(fold->states,capacity*fold->width*sizeof(*states));
    if (!states) return false;
    fold->states=states;
    size_t *slots=calloc(capacity*2u,sizeof(*slots));
    if (!slots) return false;
    for (size_t i=0; i<fold->used; i++) {
        size_t slot=fold_hash(labels[i])&(capacity*2u-1u);
        while (slots[slot]) slot=(slot+1u)&(capacity*2u-1u);
        slots[slot]=i+1u;
    }
    free(fold->slots); fold->slots=slots; fold->capacity=capacity;
    return true;
}

static bool fold_contract_supported(void) {
#define OBSERVATION(name, policy) \
    if (!strcmp(name,"fold-table")) return !strcmp(policy, \
        "QueryOrder|NumericGroupOrder|MatchingCompatibleKeys|ProjectedGroupIdentity|OriginalGroupCallback|IndependentGroupStates|ExpressionState|SingleAnswerNumericState|CompleteValidation|AtomicPublication|AuthoredFallback");
    CETTA_POSTINGS_OBSERVATION_ROWS(OBSERVATION)
#undef OBSERVATION
    return false;
}
static bool fold_columns_contract_supported(void) {
#define OBSERVATION(name, policy) \
    if (!strcmp(name,"fold-columns")) return !strcmp(policy, \
        "QueryOrder|NumericGroupOrder|MatchingCompatibleKeys|ProjectedGroupIdentity|OriginalGroupCallback|IndependentGroupStates|ExpressionState|SingleAnswerNumericState|FirstGroupEncounterOrder|DeclaredLeafDTypes|DenseColumns|CompleteValidation|AtomicPublication|AuthoredFallback");
    CETTA_POSTINGS_OBSERVATION_ROWS(OBSERVATION)
#undef OBSERVATION
    return false;
}
static bool postings_callback_stop(void *context) {
    (void)context; return eval_native_interrupted();
}

/* Numeric state is kept in an unpublished table. The existing pure-call
 * compiler owns callback meaning and admission; this loop only supplies
 * each group's previous state and the next exact prefix count. */
static Atom *table_fold(Arena *arena, PostingsTable *table, Atom *request, bool columns) {
    const char *reference=getenv("CETTA_POSTINGS_FOLD_REFERENCE");
    if (table->reference || !fold_contract_supported() ||
        (columns && !fold_columns_contract_supported()) ||
        (reference && *reference && strcmp(reference,"0")) ||
        eval_current_effective_fuel_limit() >= 0 ||
        !eval_prepare_pure_value_call ||
        !cetta_prepared_pure_program_rebind_closed_entry_call ||
        !cetta_prepared_pure_program_execute_closed_controlled ||
        !cetta_prepared_pure_program_release_closed_execution ||
        !cetta_prepared_pure_program_clear_closed_entry_call ||
        !cetta_prepared_pure_program_is_current || !cetta_prepared_pure_program_free)
        return fallback(arena);
    Atom **parts, **queries, **initial; size_t length, count, width;
    if (!sequence(request,&parts,&length) || length!=(columns ? 7u : 6u) ||
        !parts[0] || parts[0]->kind!=ATOM_GROUNDED || parts[0]->ground.gkind!=GV_SPACE ||
        !parts[0]->ground.ptr || !sequence(parts[1],&queries,&count) ||
        !parts[2] || parts[2]->kind!=ATOM_GROUNDED || parts[2]->ground.gkind!=GV_INT ||
        !parts[3] || parts[3]->kind!=ATOM_SYMBOL ||
        !parts[4] || parts[4]->kind!=ATOM_EXPR ||
        !sequence(parts[4],&initial,&width) || !width || width>33u ||
        !parts[5] || parts[5]->kind!=ATOM_SYMBOL)
        return fallback(arena);
    Atom *tag=initial[0] && initial[0]->kind==ATOM_SYMBOL ? initial[0] : NULL;
    if (tag) { initial++; width--; }
    if (!width || width>32u) return fallback(arena);
    FoldNumber seed[32];
    for (size_t i=0; i<width; i++) if (!fold_number(initial[i],&seed[i])) return fallback(arena);
    FoldColumnType types[32];
    if (columns) {
        Atom **requested; size_t column_count;
        if (!sequence(parts[6],&requested,&column_count) || column_count!=width)
            return fallback(arena);
        for (size_t i=0; i<width; i++)
            if (!fold_column_type(requested[i],&types[i])) return fallback(arena);
    }
    for (size_t i=0; i<count; i++) {
        Atom **query; size_t arity;
        if (!sequence(queries[i],&query,&arity) || arity!=2u ||
            !postings_key_is_exact(query[0]) || !fold_payload(query[1]))
            return fallback(arena);
    }
    if (!count) {
        TableFold empty={.width=width};
        Atom *result=columns ? fold_columns(arena,&empty,types) : atom_expr(arena,NULL,0u);
        return result ? result : fallback(arena);
    }
    Space *space=parts[0]->ground.ptr;
    if (eval_resolve_space_read) space=eval_resolve_space_read(space);
    Arena owner,scratch;
    arena_init(&owner); arena_set_hashcons(&owner,NULL);
    arena_init(&scratch); arena_set_hashcons(&scratch,NULL);
    (void)arena_alloc(&scratch,1u);
    ArenaMark origin=arena_mark(&scratch);
    Atom leaves[32], group_leaf, count_leaf, *state_items[33];
    size_t offset=tag ? 1u : 0u;
    if (tag) state_items[0]=tag;
    for (size_t i=0; i<width; i++) {
        if (seed[i].real) atom_scalar_leaf_float(&leaves[i],seed[i].floating);
        else atom_scalar_leaf_int(&leaves[i],seed[i].integer);
        state_items[offset+i]=&leaves[i];
    }
    atom_scalar_leaf_int(&group_leaf,0); atom_scalar_leaf_int(&count_leaf,1);
    Atom *state=atom_expr(&owner,state_items,width+offset);
    Atom **first_query=NULL; size_t first_arity=0;
    (void)sequence(queries[0],&first_query,&first_arity);
    Atom *key_items[]={parts[3],&group_leaf,first_query[1]};
    Atom *key_prototype=atom_expr(&owner,key_items,3u);
    CettaPreparedPureProgram *key_program=eval_prepare_pure_value_call(space,&owner,key_prototype);
    Atom *call_items[]={parts[5],state,&group_leaf,&count_leaf,first_query[1]};
    Atom *prototype=atom_expr(&owner,call_items,5u);
    CettaPreparedPureProgram *program=eval_prepare_pure_value_call(space,&owner,prototype);
    TableFold fold={.width=width};
    Atom *result=NULL;
    if (!program || !key_program) goto cleanup;
    for (size_t query_index=0; query_index<count; query_index++) {
        Atom **query=NULL; size_t arity=0;
        (void)sequence(queries[query_index],&query,&arity);
        NameId id=name_key_find(table->keys,query[0]);
        if (id==NAME_ID_NONE) continue;
        for (size_t group=table->key_group_offsets[id-1u]; group<table->key_group_offsets[id]; group++) {
            if (eval_native_interrupted()) goto cleanup;
            size_t count=lower_bound(table->positions,table->offsets[group],
                                     table->offsets[group+1u],parts[2]->ground.ival);
            if (!count) continue;
            if (count>INT64_MAX) goto cleanup;
            atom_scalar_leaf_int(&group_leaf,table->labels[group]);
            key_items[2]=query[1];
            Atom *key_call=atom_expr(&scratch,key_items,3u), *key_answer=NULL;
            if (!cetta_prepared_pure_program_rebind_closed_entry_call(key_program,key_call) ||
                !cetta_prepared_pure_program_execute_closed_controlled(key_program,&scratch,
                    65536u,postings_callback_stop,NULL,&key_answer) ||
                !key_answer || key_answer->kind!=ATOM_GROUNDED ||
                key_answer->ground.gkind!=GV_INT)
                goto cleanup;
            int64_t label=key_answer->ground.ival;
            cetta_prepared_pure_program_release_closed_execution(key_program);
            cetta_prepared_pure_program_clear_closed_entry_call(key_program);
            if (fold.used==fold.capacity && !fold_grow(&fold)) goto cleanup;
            size_t slot=fold_hash(label)&(fold.capacity*2u-1u);
            while (fold.slots[slot] && fold.labels[fold.slots[slot]-1u]!=label)
                slot=(slot+1u)&(fold.capacity*2u-1u);
            if (!fold.slots[slot]) {
                fold.labels[fold.used]=label;
                memcpy(fold.states+fold.used*width,seed,width*sizeof(*seed));
                fold.slots[slot]=++fold.used;
            }
            FoldNumber *previous=fold.states+(fold.slots[slot]-1u)*width;
            for (size_t i=0; i<width; i++) {
                if (previous[i].real) atom_scalar_leaf_float(&leaves[i],previous[i].floating);
                else atom_scalar_leaf_int(&leaves[i],previous[i].integer);
            }
            atom_scalar_leaf_int(&group_leaf,table->labels[group]);
            atom_scalar_leaf_int(&count_leaf,(int64_t)count);
            call_items[1]=atom_expr(&scratch,state_items,width+offset);
            call_items[4]=query[1];
            Atom *call=atom_expr(&scratch,call_items,5u), *answer=NULL;
            Atom **values; size_t lanes;
            if (!cetta_prepared_pure_program_rebind_closed_entry_call(program,call) ||
                !cetta_prepared_pure_program_execute_closed_controlled(program,&scratch,
                    65536u,postings_callback_stop,NULL,&answer) ||
                !answer || answer->kind!=ATOM_EXPR ||
                !sequence(answer,&values,&lanes) || lanes!=width+offset ||
                (tag && !atom_eq(tag,values[0])))
                goto cleanup;
            for (size_t i=0; i<width; i++)
                if (!fold_number(values[offset+i],&previous[i])) goto cleanup;
            cetta_prepared_pure_program_release_closed_execution(program);
            cetta_prepared_pure_program_clear_closed_entry_call(program);
            arena_reset(&scratch,origin);
        }
    }
    if (!cetta_prepared_pure_program_is_current(program) ||
        !cetta_prepared_pure_program_is_current(key_program)) goto cleanup;
    if (columns) {
        result=fold_columns(arena,&fold,types);
        goto cleanup;
    }
    Atom **rows=fold.used ? arena_alloc(arena,fold.used*sizeof(*rows)) : NULL;
    for (size_t group=0; group<fold.used; group++) {
        Atom *values[33];
        if (tag) values[0]=atom_deep_copy(arena,tag);
        for (size_t i=0; i<width; i++) {
            FoldNumber value=fold.states[group*width+i];
            values[offset+i]=value.real ? atom_float(arena,value.floating) : atom_int(arena,value.integer);
        }
        rows[group]=atom_expr2(arena,atom_int(arena,fold.labels[group]),atom_expr(arena,values,width+offset));
    }
    result=atom_expr(arena,rows,fold.used);
cleanup:
    cetta_prepared_pure_program_free(program);
    cetta_prepared_pure_program_free(key_program);
    free(fold.labels); free(fold.states); free(fold.slots);
    arena_free(&scratch); arena_free(&owner);
    if (eval_native_interrupted()) return NULL;
    return result ? result : fallback(arena);
}

static Atom *table_stats(Arena *arena, PostingsTable *table) {
    size_t bytes = sizeof(*table), charge = 0u, keys = 0u;
#define TABLE_CHARGE(count, width)                                            \
    do {                                                                       \
        if (__builtin_mul_overflow((size_t)(count), (size_t)(width), &charge) ||\
            __builtin_add_overflow(bytes, charge, &bytes))                    \
            return fault(arena, "size-overflow");                            \
    } while (0)
    if (table->reference) {
        if (__builtin_add_overflow(
                bytes, table->reference_arena.reserved_bytes, &bytes) ||
            __builtin_add_overflow(
                bytes, table->reference_arena.external_bytes, &bytes) ||
            __builtin_add_overflow(
                bytes, table->reference_arena.symbol_cache_bytes, &bytes))
            return fault(arena, "size-overflow");
    } else {
        if (!name_key_table_retained_bytes(table->keys, &keys) ||
            __builtin_add_overflow(bytes, keys, &bytes))
            return fault(arena, "size-overflow");
        TABLE_CHARGE(table->key_count + 1u, sizeof(*table->key_group_offsets));
        TABLE_CHARGE(table->groups, sizeof(*table->labels));
        TABLE_CHARGE(table->groups + 1u, sizeof(*table->offsets));
        TABLE_CHARGE(table->count, sizeof(*table->positions));
    }
#undef TABLE_CHARGE
    char text[32];
    snprintf(text, sizeof(text), "%zu", bytes);
    Atom *retained = bytes <= INT64_MAX
        ? atom_int(arena, (int64_t)bytes) : atom_bigint(arena, text);
    return atom_expr3(
        arena, atom_symbol(arena, "PostingsTableStats"),
        atom_symbol(arena, table->reference ? "authored" : "packed"), retained);
}

/* Validate the complete data carrier before reading any source. The second
 * pass follows the ordinary revision getter's current import/transaction
 * view. No per-row Atoms, snapshots or verdict caches are constructed. */
static Atom *sources_current(Arena *arena, Atom *guards) {
    const char *reference = getenv("CETTA_POSTINGS_SOURCES_REFERENCE");
    if ((reference && *reference && strcmp(reference, "0")) ||
        eval_current_effective_fuel_limit() >= 0)
        return fallback(arena);
    Atom *const *rows;
    CettaExprLen count;
    if (!atom_sequence_view(guards, &rows, &count)) return fallback(arena);
    for (CettaExprIndex i = 0; i < count; i++) {
        Atom *const *pair;
        CettaExprLen length;
        if (!atom_sequence_view(rows[i], &pair, &length) || length != 2u ||
            !pair[0] || pair[0]->kind != ATOM_GROUNDED ||
            pair[0]->ground.gkind != GV_SPACE || !pair[0]->ground.ptr ||
            !pair[1] || pair[1]->kind != ATOM_GROUNDED ||
            pair[1]->ground.gkind != GV_INT || pair[1]->ground.ival < 0)
            return fallback(arena);
        if ((i & 1023u) == 0u && eval_native_interrupted()) return NULL;
    }
    bool current = true;
    for (CettaExprIndex i = 0; i < count; i++) {
        Atom *const *pair;
        CettaExprLen length;
        if (!atom_sequence_view(rows[i], &pair, &length)) return fallback(arena);
        Space *source = pair[0]->ground.ptr;
        if (eval_resolve_space_read) source = eval_resolve_space_read(source);
        uint64_t revision = space_revision(source);
        if (revision > (uint64_t)INT64_MAX) return fallback(arena);
        current = current && revision == (uint64_t)pair[1]->ground.ival;
        if ((i & 1023u) == 0u && eval_native_interrupted()) return NULL;
    }
    return atom_bool(arena, current);
}

bool cetta_postings_call(CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (!head || head->kind != ATOM_SYMBOL) return false;
    SymbolId op = head->sym_id;
    if (op != g_builtin_syms.lib_postings_prepare && op != g_builtin_syms.lib_postings_own && op != g_builtin_syms.lib_postings_counts &&
        op != g_builtin_syms.lib_postings_sources_current &&
        op != g_builtin_syms.lib_postings_inspect && op != g_builtin_syms.lib_postings_close) return false;
    if (eval_native_interrupted()) { *out = cetta_call_interrupted(); return true; }
    Atom *result = NULL;
    if (nargs != (op == g_builtin_syms.lib_postings_counts ? 3u : 1u)) {
        *out = cetta_call_completed_value(fault(arena, "arity")); return true;
    }
    if (op == g_builtin_syms.lib_postings_sources_current) result=sources_current(arena,args[0]);
    else if (op == g_builtin_syms.lib_postings_own) result=own_reference(context,arena,args[0]);
    else if (op == g_builtin_syms.lib_postings_prepare) {
        const char *reference = getenv("CETTA_POSTINGS_REFERENCE");
        result = reference && reference[0] && strcmp(reference, "0")
            ? fallback(arena) : prepare(context, arena, args[0]);
    } else {
        PostingsIndex *index = cetta_native_handle_get_owned(context, args[0], index_kind);
        PostingsTable *table = index ? NULL
            : cetta_native_handle_get_owned(context, args[0], table_kind);
        if (!index && !table) result = fault(arena, "closed-or-forged-index");
        else if (op == g_builtin_syms.lib_postings_close) {
            uint64_t id = 0;
            const char *kind = index ? index_kind : table_kind;
            if (cetta_native_handle_arg(args[0], kind, &id))
                (void)cetta_native_handle_close(context, kind, id);
            result = atom_unit(arena);
        } else if (op == g_builtin_syms.lib_postings_inspect) {
            result = table ? table_inspect(arena, table)
                           : observe(arena, index, NULL, false, true);
        } else {
            const PostingsCountModeSpec *mode =
                postings_count_mode_spec(args[1]);
            if (!mode ||
                (mode->family == POSTINGS_COUNT_FAMILY_INDEX && !index) ||
                (mode->family == POSTINGS_COUNT_FAMILY_TABLE && !table)) {
                result = fault(arena, "mode");
            } else if (mode->result == POSTINGS_COUNT_RESULT_STATISTICS) {
                result = table ? table_stats(arena, table) : stats(arena, index);
            } else if (mode->result == POSTINGS_COUNT_RESULT_FOLDROWS ||
                       mode->result == POSTINGS_COUNT_RESULT_FOLDCOLUMNS) {
                result = table_fold(arena,table,args[2],
                    mode->result == POSTINGS_COUNT_RESULT_FOLDCOLUMNS);
            } else if (mode->family == POSTINGS_COUNT_FAMILY_TABLE) {
                result = table_counts(
                    arena, table, args[2],
                    mode->extent == POSTINGS_COUNT_EXTENT_COMPLETE,
                    mode->result == POSTINGS_COUNT_RESULT_TENSOR);
            } else if (mode->result == POSTINGS_COUNT_RESULT_TENSOR) {
                result = tensor_counts(
                    arena, index, args[2],
                    mode->extent == POSTINGS_COUNT_EXTENT_COMPLETE);
            } else {
                result = observe(
                    arena, index, args[2],
                    mode->extent == POSTINGS_COUNT_EXTENT_COMPLETE, false);
            }
        }
    }
    *out = result ? cetta_call_completed_value(result) : cetta_call_interrupted();
    return true;
}
