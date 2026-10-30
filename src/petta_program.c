#include "petta_program.h"

#include "eval.h"
#include "grounded.h"
#include "library_binary.h"
#include "generated/cetta_execution_contracts.generated.h"
#include "petta_semantics.h"
#include "shared_transition.h"
#include "stats.h"
#include <assert.h>
#include "symbol.h"
#include "term_canon.h"

#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static atomic_uint_fast64_t petta_source_root_next;

uint64_t cetta_source_root_new(void) {
    uint_fast64_t current = atomic_load_explicit(
        &petta_source_root_next, memory_order_relaxed);
    while (current != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&petta_source_root_next,
                &current, current + 1u, memory_order_relaxed, memory_order_relaxed))
            return current + 1u;
    }
    return 0u;
}

struct PettaCandidateSnapshotStorage {
    atomic_size_t references;
    pthread_mutex_t lock;
    PettaEquationCandidate *items;
    size_t len, capacity;
    bool retired;
    PettaEquationProjection *projections;
};

struct PettaEquationProjection {
    atomic_size_t references;
    pthread_mutex_t lock;
    PettaCandidateSnapshotStorage *source;
    PettaEquationProjection *previous, *next;
    PettaEquationSelectionEntry *entries;
    PettaEquationCandidate *owned;
    size_t first, len, entries_capacity;
};

static PettaCandidateSnapshotStorage *petta_equation_storage_take(
        PettaEquationCandidate *items, size_t len, size_t capacity) {
    PettaCandidateSnapshotStorage *storage = cetta_malloc(sizeof(*storage));
    atomic_init(&storage->references, 1u);
    pthread_mutex_init(&storage->lock, NULL);
    storage->items = items;
    storage->len = len;
    storage->capacity = capacity;
    storage->retired = false;
    storage->projections = NULL;
    return storage;
}

static bool petta_equation_storage_retain(PettaCandidateSnapshotStorage *storage) {
    size_t count = atomic_load_explicit(&storage->references, memory_order_relaxed);
    do {
        if (count == 0u || count == SIZE_MAX)
            return false;
    } while (!atomic_compare_exchange_weak_explicit(
        &storage->references, &count, count + 1u,
        memory_order_relaxed, memory_order_relaxed));
    return true;
}

static void petta_equation_storage_release(PettaCandidateSnapshotStorage *storage) {
    if (storage && atomic_fetch_sub_explicit(
            &storage->references, 1u, memory_order_acq_rel) == 1u) {
        free(storage->items);
        pthread_mutex_destroy(&storage->lock);
        free(storage);
    }
}

static PettaEquationCandidate petta_equation_projection_get_locked(
        const PettaEquationProjection *projection, size_t index) {
    if (projection->owned)
        return projection->owned[index];
    size_t source_index = projection->entries
        ? projection->entries[index].index : projection->first + index;
    PettaEquationCandidate candidate = projection->source->items[source_index];
    if (projection->entries)
        candidate.rhs_plan = projection->entries[index].rhs_plan;
    return candidate;
}

PettaEquationCandidate petta_program_equation_projection_get(
        const PettaEquationProjection *projection, size_t index) {
    pthread_mutex_t *lock = (pthread_mutex_t *)&projection->lock;
    pthread_mutex_lock(lock);
    PettaEquationCandidate candidate = petta_equation_projection_get_locked(projection, index);
    pthread_mutex_unlock(lock);
    return candidate;
}

static void petta_equation_projection_unlink(PettaEquationProjection *projection) {
    PettaCandidateSnapshotStorage *source = projection->source;
    if (projection->previous)
        projection->previous->next = projection->next;
    else
        source->projections = projection->next;
    if (projection->next)
        projection->next->previous = projection->previous;
    projection->source = NULL;
    projection->previous = projection->next = NULL;
}

/* Catalog mutation already requires exclusive program access. Retirement
 * changes only physical storage: every projection retains exactly the same
 * occurrence sequence and completed plans. Atomic reference counts support
 * continuation ownership transfers; they do not authorize concurrent mutation
 * of an executing program. */
static void petta_equation_storage_retire(PettaCandidateSnapshotStorage *storage) {
    if (!petta_equation_storage_retain(storage))
        return;
    pthread_mutex_lock(&storage->lock);
    storage->retired = true;
    size_t projected = 0u;
    for (PettaEquationProjection *p = storage->projections; p; p = p->next) {
        if (p->len >= storage->len - projected) {
            pthread_mutex_unlock(&storage->lock);
            petta_equation_storage_release(storage);
            return; /* Sharing is no larger than separately promoted records. */
        }
        projected += p->len;
    }
    while (storage->projections) {
        PettaEquationProjection *p = storage->projections;
        pthread_mutex_lock(&p->lock);
        PettaEquationCandidate *owned = cetta_malloc(p->len * sizeof(*owned));
        for (size_t i = 0u; i < p->len; i++)
            owned[i] = petta_equation_projection_get_locked(p, i);
        p->owned = owned;
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_PETTA_EQUATION_PROJECTION_PROMOTED_RECORDS, p->len);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_PETTA_EQUATION_PROJECTION_PROMOTED_BYTES,
            p->len * sizeof(*owned));
        free(p->entries);
        p->entries = NULL;
        p->entries_capacity = 0u;
        p->first = 0u;
        petta_equation_projection_unlink(p);
        pthread_mutex_unlock(&p->lock);
        petta_equation_storage_release(storage);
    }
    pthread_mutex_unlock(&storage->lock);
    petta_equation_storage_release(storage);
}

PettaEquationProjection *petta_program_equation_projection_take(
        PettaCandidateSnapshotLease *catalog, PettaEquationSelectionEntry *entries,
        size_t entries_capacity, size_t first, size_t len) {
    if (!catalog || !len || !catalog->storage ||
        len > SIZE_MAX / sizeof(PettaEquationCandidate) ||
        (entries && (entries_capacity < len || entries_capacity > SIZE_MAX / sizeof(*entries))) ||
        (!entries && entries_capacity) ||
        (!entries && (first > catalog->len || len > catalog->len - first)))
        return NULL;
    if (entries) {
        for (size_t i = 0u; i < len; i++)
            if (entries[i].index >= catalog->len)
                return NULL;
    }
    PettaEquationProjection *p = cetta_malloc(sizeof(*p));
    atomic_init(&p->references, 1u);
    pthread_mutex_init(&p->lock, NULL);
    p->source = catalog->storage;
    pthread_mutex_lock(&p->source->lock);
    p->previous = NULL;
    p->next = p->source->projections;
    if (p->next)
        p->next->previous = p;
    p->source->projections = p;
    p->entries = entries;
    p->entries_capacity = entries_capacity;
    p->owned = NULL;
    p->first = first;
    p->len = len;
    PettaCandidateSnapshotStorage *source = p->source;
    bool retired = source->retired;
    if (retired && !petta_equation_storage_retain(source))
        abort();
    pthread_mutex_unlock(&source->lock);
    *catalog = (PettaCandidateSnapshotLease){0};
    if (retired) {
        petta_equation_storage_retire(source);
        petta_equation_storage_release(source);
    }
    return p;
}

bool petta_program_equation_projection_retain(PettaEquationProjection *projection) {
    size_t count = atomic_load_explicit(&projection->references, memory_order_relaxed);
    do {
        if (count == 0u || count == SIZE_MAX)
            return false;
    } while (!atomic_compare_exchange_weak_explicit(
        &projection->references, &count, count + 1u,
        memory_order_relaxed, memory_order_relaxed));
    return true;
}

void petta_program_equation_projection_release(PettaEquationProjection *projection) {
    if (!projection || atomic_fetch_sub_explicit(
            &projection->references, 1u, memory_order_acq_rel) != 1u)
        return;
    pthread_mutex_lock(&projection->lock);
    PettaCandidateSnapshotStorage *source = projection->source;
    if (source && !petta_equation_storage_retain(source))
        abort();
    pthread_mutex_unlock(&projection->lock);
    if (source) {
        pthread_mutex_lock(&source->lock);
        pthread_mutex_lock(&projection->lock);
        bool linked = projection->source != NULL;
        if (linked)
            petta_equation_projection_unlink(projection);
        bool retired = source->retired;
        pthread_mutex_unlock(&projection->lock);
        pthread_mutex_unlock(&source->lock);
        if (linked)
            petta_equation_storage_release(source);
        if (retired)
            petta_equation_storage_retire(source);
        petta_equation_storage_release(source);
    }
    free(projection->entries);
    free(projection->owned);
    pthread_mutex_destroy(&projection->lock);
    free(projection);
}

size_t petta_program_equation_projection_retained_bytes(
        const PettaEquationProjection *projection) {
    pthread_mutex_t *lock = (pthread_mutex_t *)&projection->lock;
    pthread_mutex_lock(lock);
    size_t records = projection->source ? projection->source->capacity : projection->len;
    size_t entries = projection->entries_capacity;
    size_t source_header = projection->source ? sizeof(*projection->source) : 0u;
    pthread_mutex_unlock(lock);
    if (records > (SIZE_MAX - sizeof(*projection) - source_header) / sizeof(PettaEquationCandidate))
        return SIZE_MAX;
    size_t bytes = sizeof(*projection) + source_header + records * sizeof(PettaEquationCandidate);
    if (entries > (SIZE_MAX - bytes) / sizeof(PettaEquationSelectionEntry))
        return SIZE_MAX;
    return bytes + entries * sizeof(PettaEquationSelectionEntry);
}

typedef struct {
    Atom *equation;
    /* Authored occurrence identity is separate from the syntax used to build
     * its plan, which may lower grades or rebind equation variables. */
    Atom *planning_syntax;
    uint64_t source_root;
    const PettaPlanNode *plan;
    const PettaEquationTemplateC0 *equation_template_c0;
    const PettaEquationTemplate *equation_template;
    uint32_t static_variable_count;
    SymbolId head;
    bool rhs_binds_variables;
} PettaProgramEquation;

struct PettaEquationTemplateC0 {
    CettaGsltGroundDenseTermProgramV1 lhs;
    CettaGsltGroundDenseTermProgramV1 rhs;
};

struct PettaEquationTemplate {
    Atom *equation;
    Atom *lhs;
    Atom *rhs;
    const CettaOpenPatternPlan *lhs_match_plan;
    BindingsFrameSchema *frame_schema;
    struct PettaEquationTemplate *next_owned;
    Atom **source_variables;
    uint32_t variable_count;
};

typedef struct {
    SymbolId head;
    size_t *record_indices;
    size_t len;
    size_t cap;
} PettaProgramHeadBucket;

/* A candidate selection observes two coordinates.  The equation token proves
 * that the selected declaration family is unchanged; the prefix epoch proves
 * that the occurrence positions carried by candidates still name the same
 * rows.  Data-only appends preserve both coordinates.  A removal, reorder,
 * opaque backend transition, or equation edit conservatively rejects this
 * key and takes the live selection route. */
typedef struct {
    SpaceEquationToken equations;
    uint64_t prefix_epoch;
} PettaProgramCandidateSnapshotKey;

typedef struct {
    SymbolId head;
    /* This full token distinguishes a directly borrowable cache entry from
     * a selection-equivalent entry whose occurrence provenance must be
     * rebound into an owned lease. */
    SpaceReadToken source;
    PettaProgramCandidateSnapshotKey key;
    PettaEquationCandidate *candidates;
    size_t len;
    PettaCandidateSnapshotStorage *storage;
} PettaProgramCandidateSnapshot;

typedef struct {
    SymbolId head;
    CettaExprLen arity;
    AtomId signature_id;
} PettaProgramInferredSignature;

typedef struct {
    SymbolId subject;
    Atom **types;
    size_t len;
    size_t cap;
} PettaProgramTypeBucket;

typedef struct {
    CettaExprLen supplied;
    bool complete;
} PettaProgramTypeArity;

typedef struct {
    SymbolId head;
    PettaTypeCall *calls;
    uint32_t count;
    bool hold_body;
    PettaProgramTypeArity *arities;
    size_t arity_len, arity_cap;
} PettaProgramTypeFamily;

typedef struct {
    const Space *space;
    uint64_t instance_id;
    uint64_t synchronized_revision;
    /* The space's equations and declarations when the catalog was last
     * synchronized; a data-only mutation leaves it current. */
    SpaceProgramToken synchronized_program;
    bool synchronized_snapshot;
    /* Evidence for a single notified append to that snapshot. */
    uint64_t synchronized_prefix_epoch;
    CettaCount synchronized_atom_count;
    size_t synchronized_equation_len;
    uint64_t synchronized_catalog_generation;
    uint64_t catalog_generation;
    /* Optional validation stamps, allocated only by live translation. They
     * follow current catalog positions; retained plans themselves are immutable. */
    uint64_t *plan_callability_epochs;
    size_t plan_callability_epoch_len, plan_callability_epoch_cap;
    PettaProgramEquation *equations;
    size_t equation_len;
    size_t equation_cap;
    PettaProgramHeadBucket *head_buckets;
    size_t head_bucket_len;
    size_t head_bucket_cap;
    bool head_index_dirty;
    /* The heads a parse pass of a document loaded into the space declared,
     * sorted, less each head an equation of the space has since been added
     * or removed with: SWI-PeTTa's fun/1 registrations that no equation of
     * the space answers for yet. */
    SymbolId *pending_heads;
    size_t pending_head_len;
    size_t pending_head_cap;
    PettaProgramCandidateSnapshot *snapshots;
    size_t snapshot_len;
    size_t snapshot_cap;
    PettaProgramRevisionView *revision_view;
    PettaProgramTypeFamily *type_families;
    size_t type_family_len;
    size_t type_family_cap;
    SpaceProgramToken type_family_program;
    /* Families share one immutable generation region, not a minimum-sized
     * arena block per head. Activations instantiate into their own heap. */
    Arena type_family_storage;
    bool type_families_current;
} PettaProgramSpace;

typedef struct {
    const Space *space;
    uint64_t instance_id;
    PettaProgramInferredSignature *inferred_signatures;
    size_t inferred_signature_len;
    size_t inferred_signature_cap;
    /* The universe the signatures are interned in, which collects them
     * only past the program's listing (petta_program_signature_roots). */
    TermUniverse *signature_universe;
    uint64_t inferred_signature_revision;
    bool inferred_signatures_valid;
    CettaNikDirectAuthorityStampV1 inferred_signature_authority;
    bool inferred_signature_authority_valid;
    Atom **type_annotations;
    size_t type_annotation_len;
    size_t type_annotation_cap;
    PettaProgramTypeBucket *type_buckets;
    size_t type_bucket_len;
    size_t type_bucket_cap;
    bool type_index_dirty;
} PettaProgramAnalysisSpace;

struct PettaTransactionCatalog {
    PettaProgramSpace catalog;
    PettaProgramAnalysisSpace analysis;
    PettaProgram *owner;
    PettaTransactionCatalog *previous, *next;
};

static _Thread_local const PettaProgramPrivateScope *petta_private_scope;

const PettaProgramPrivateScope *petta_program_private_scope_current(void) {
    return petta_private_scope;
}

const PettaProgramPrivateScope *petta_program_private_scope_swap(
        const PettaProgramPrivateScope *scope) {
    const PettaProgramPrivateScope *previous = petta_private_scope;
    petta_private_scope = scope;
    return previous;
}

void petta_program_private_scope_enter(PettaProgramPrivateScope *scope) {
    scope->previous = petta_program_private_scope_swap(scope);
}

void petta_program_private_scope_leave(PettaProgramPrivateScope *scope) {
    assert(petta_private_scope == scope);
    petta_private_scope = scope->previous;
}

static PettaTransactionCatalog *petta_program_private_catalog(
        PettaProgram *program, const Space *space) {
    if (!program || !space) return NULL;
    for (const PettaProgramPrivateScope *scope = petta_private_scope;
         scope; scope = scope->previous) {
        if (scope->program != program || !scope->catalog) continue;
        PettaTransactionCatalog *catalog = scope->catalog(scope->context, space);
        if (catalog) return catalog;
    }
    return NULL;
}

bool petta_program_space_is_private(PettaProgram *program, const Space *space) {
    return petta_program_private_catalog(program, space) != NULL;
}

static bool petta_program_has_private_images(const PettaProgram *program) {
    for (const PettaProgramPrivateScope *scope = petta_private_scope;
         scope; scope = scope->previous) {
        if (scope->program == program && scope->image_at &&
            scope->image_at(scope->context, 0u)) return true;
    }
    return false;
}

static Space *petta_program_private_map(PettaProgram *program, Space *space) {
    /* Each mapping follows original -> working. Repeating over the scope
     * chain also covers a nested evaluator whose transaction has a different
     * machine context from its parent's transaction. */
    for (;;) {
        Space *before = space;
        for (const PettaProgramPrivateScope *scope = petta_private_scope;
             scope; scope = scope->previous)
            if (scope->program == program && scope->map)
                space = scope->map(scope->context, space);
        if (space == before) return space;
    }
}

typedef struct {
    PettaProgramAnalysisSpace *spaces;
    size_t space_len;
    size_t space_cap;
} PettaProgramAnalysisState;

typedef struct {
    SymbolId *named_heads;
    size_t named_len;
    size_t named_cap;
    bool admits_any_head;
} PettaCallabilityDomain;

/* What the callability cache read of one space: its equation projection,
 * the prefix it scanned, and how many rows. */
typedef struct {
    SpaceEquationToken equations;
    SpaceReadToken read;
    CettaCount scanned_len;
} PettaCallabilityScan;

/* Four ways retain colliding inquiries without increasing entry capacity.
 * Hashing selects a bank; reuse still checks the complete program token. */
#define PETTA_TABLE_SAFETY_CACHE_CAP 128u
#define PETTA_TABLE_SAFETY_CACHE_WAYS 4u

/* Keyed on the space program (equation and declaration occurrences), not on
 * the data revision: safety reads the relation's equations and their plans plus
 * static tables and the available native observers, so a data-only mutation
 * leaves the answer valid. An answer
 * that depended on a plan not built yet, or on an allocation failure, is
 * transient and is not stored. */
typedef struct {
    SpaceProgramToken program;
    SymbolId head;
    CettaExprLen arity;
    uint64_t native_observers;
    /* Live translation also reads callable heads from the other program
     * Spaces and the predeclared domain. */
    uint64_t callability_epoch;
    unsigned admitted_rule_forms;
    bool graded_equations;
    PettaRelationSafety safety;
    bool occupied;
} PettaTableSafetyCacheEntry;

/* A plan of a runtime translation, keyed by its content. */
typedef struct {
    uint64_t hash;
    const PettaPlanNode *plan;
} PettaPlanInternEntry;

static void petta_program_signature_roots(const TermUniverse *universe,
                                          TermUniverseRootVisit visit,
                                          void *visit_context, void *ctx);

struct PettaProgram {
    PettaTransactionCatalog *private_catalogs;
    unsigned admitted_rule_forms;
    bool refresh_callability;
    Arena plans;
    bool graded_equations;
    bool (*is_host_intrinsic)(SymbolId head);
    /* Plans of runtime translations (eval, a computed hyperpose branch),
     * one per distinct content, and the arena each is first built in. */
    PettaPlanInternEntry *interned_plans;
    size_t interned_plan_len;
    size_t interned_plan_cap;
    Arena transient_scratch;
    PettaEquationTemplate *equation_templates;
    PettaEquationTemplateC0 **equation_template_c0;
    size_t equation_template_c0_len;
    size_t equation_template_c0_cap;
    PettaProgramSpace *spaces;
    size_t space_len;
    size_t space_cap;
    PettaProgramAnalysisState *analysis;
    /* The universes holding this program's inferred signatures, each of
     * which lists them from the program at a collection. */
    TermUniverse **signature_universes;
    size_t signature_universe_len;
    size_t signature_universe_cap;
    PettaCallabilityDomain predeclared_callability;
    uint64_t predeclared_generation;
    /* Callability is program knowledge: the heads of the equation
       occurrences of the program's spaces and the predeclared heads.  It is
       read once per change of those, not once per planned expression; an
       append adds the heads of the appended rows. */
    PettaCallabilityDomain callability_cache;
    PettaCallabilityScan *callability_cache_tokens;
    size_t callability_cache_token_len;
    size_t callability_cache_token_cap;
    uint64_t callability_cache_predeclared_generation;
    bool callability_cache_valid;
    /* Advances whenever the callability cache is rebuilt. */
    uint64_t callability_epoch;
    /* Plans of goals planned at run time, by the goal's syntax up to the
       names of its variables and by the callability they were built under:
       an alpha-equivalent goal takes its plan again, rather than building
       and keeping another.  At most PETTA_TRANSIENT_PLAN_BOUND entries. */
    struct PettaTransientValue *transient_values;
    PettaTranslationEnvironment transient_environment;
    bool transient_environment_valid;
    struct PettaTransientPlan *transient_plans;
    size_t transient_plan_len;
    size_t transient_plan_cap;
    Arena transient_plan_atoms;
    PettaTableSafetyCacheEntry
        table_safety_cache[PETTA_TABLE_SAFETY_CACHE_CAP];
    uint8_t table_safety_next[PETTA_TABLE_SAFETY_CACHE_CAP /
                              PETTA_TABLE_SAFETY_CACHE_WAYS];
};

struct PettaProgramRevisionView {
    bool graded_equations;
    unsigned admitted_rule_forms;
    _Atomic uint32_t references;
    /* The full token proves that construction observed one coherent source
       state; reuse is keyed by the strictly smaller equation projection. */
    SpaceReadToken source;
    SpaceEquationToken source_equation_token;
    SpaceProgramToken source_program_token;
    uint64_t catalog_generation;
    uint64_t callability_epoch;
    bool callability_certified;
    PettaProgramSpace catalog;
    Atom **source_equations;
    size_t source_equation_len, source_equation_capacity;
};

enum {
    PETTA_OPEN_PATTERN_PLAN_DEPTH_LIMIT = 256u,
};

static bool petta_program_variable_slot(
        const VarId *variable_ids, uint32_t variable_count,
        VarId id, uint32_t *slot_out) {
    if (slot_out)
        *slot_out = 0u;
    if (!variable_ids || variable_count == 0u ||
        var_id_is_none(id) || !slot_out) {
        return false;
    }
    uint32_t low = 0u;
    uint32_t high = variable_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (var_id_lt(variable_ids[middle], id))
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= variable_count || !var_id_eq(variable_ids[low], id))
        return false;
    *slot_out = low;
    return true;
}

static bool petta_program_compile_open_pattern_plan_node(
        PettaProgram *program, Atom *source,
        Atom **ancestors, uint32_t depth,
        const VarId *variable_ids, uint32_t variable_count,
        CettaOpenPatternPlan *out) {
    if (!program || !source || !ancestors || !out ||
        depth > PETTA_OPEN_PATTERN_PLAN_DEPTH_LIMIT) {
        return false;
    }
    for (uint32_t index = 0u; index < depth; index++) {
        if (ancestors[index] == source)
            return false;
    }
    *out = (CettaOpenPatternPlan){
        .source = source,
        .kind = source->kind,
        .variable_ids = variable_count <= 64u
            ? variable_ids : NULL,
    };
    if (source->kind == ATOM_VAR) {
        if (variable_count > 64u)
            return true;
        uint32_t slot = 0u;
        if (!petta_program_variable_slot(
                variable_ids, variable_count,
                source->var_id, &slot)) {
            return false;
        }
        out->variable_mask = UINT64_C(1) << slot;
        return true;
    }
    if (source->kind != ATOM_EXPR)
        return true;
    if ((source->expr.len != 0u && !source->expr.elems) ||
        source->expr.len > SIZE_MAX / sizeof(*out->children)) {
        return false;
    }
    out->child_count = source->expr.len;
    if (source->expr.len == 0u)
        return true;
    CettaOpenPatternPlan *children = arena_alloc(
        &program->plans,
        sizeof(*children) * (size_t)source->expr.len);
    if (!children)
        return false;
    out->children = children;
    ancestors[depth] = source;
    uint64_t seen_variables = 0u;
    for (CettaExprIndex index = 0u;
         index < source->expr.len; index++) {
        if (!petta_program_compile_open_pattern_plan_node(
                program, source->expr.elems[index],
                ancestors, depth + 1u,
                variable_ids, variable_count,
                &children[index])) {
            return false;
        }
        out->repeated_variable_mask |=
            children[index].repeated_variable_mask |
            (seen_variables & children[index].variable_mask);
        seen_variables |= children[index].variable_mask;
    }
    out->variable_mask = seen_variables;
    return true;
}

static bool petta_open_pattern_plan_node_count(
        const CettaOpenPatternPlan *plan, size_t *count_out) {
    if (!plan || !count_out)
        return false;
    size_t count = 1u;
    if (plan->kind == ATOM_EXPR) {
        if ((plan->child_count != 0u && !plan->children) ||
            (size_t)plan->child_count > SIZE_MAX - count) {
            return false;
        }
        for (CettaExprIndex index = 0u;
             index < plan->child_count; index++) {
            size_t child_count = 0u;
            if (!petta_open_pattern_plan_node_count(
                    &plan->children[index], &child_count) ||
                child_count > SIZE_MAX - count) {
                return false;
            }
            count += child_count;
        }
    }
    *count_out = count;
    return true;
}

static bool petta_open_pattern_plan_linearize(
        const CettaOpenPatternPlan *plan,
        CettaOpenPatternInstruction *program,
        size_t program_len, size_t *cursor) {
    if (!plan || !program || !cursor || *cursor >= program_len)
        return false;
    size_t root = (*cursor)++;
    program[root] = (CettaOpenPatternInstruction){
        .source = plan->source,
        .variable_mask = plan->variable_mask,
    };
    if (plan->kind == ATOM_EXPR) {
        if (plan->child_count != 0u && !plan->children)
            return false;
        for (CettaExprIndex index = 0u;
             index < plan->child_count; index++) {
            if (!petta_open_pattern_plan_linearize(
                    &plan->children[index], program,
                    program_len, cursor)) {
                return false;
            }
        }
    }
    size_t span = *cursor - root;
    if (span == 0u || span > UINT32_MAX)
        return false;
    program[root].subtree_span = (uint32_t)span;
    return true;
}

static bool petta_program_compile_open_pattern_linear_program(
        PettaProgram *program, CettaOpenPatternPlan *root) {
    if (!program || !root)
        return false;
    size_t instruction_count = 0u;
    if (!petta_open_pattern_plan_node_count(
            root, &instruction_count) ||
        instruction_count == 0u ||
        instruction_count > UINT32_MAX ||
        instruction_count > SIZE_MAX /
            sizeof(CettaOpenPatternInstruction)) {
        return false;
    }
    CettaOpenPatternInstruction *instructions = arena_alloc(
        &program->plans,
        instruction_count * sizeof(*instructions));
    if (!instructions)
        return false;
    size_t cursor = 0u;
    if (!petta_open_pattern_plan_linearize(
            root, instructions, instruction_count, &cursor) ||
        cursor != instruction_count ||
        instructions[0].source != root->source ||
        instructions[0].variable_mask != root->variable_mask ||
        instructions[0].subtree_span != instruction_count) {
        return false;
    }
    root->linear_program = instructions;
    root->linear_program_len = (uint32_t)instruction_count;
    return true;
}

static const CettaOpenPatternPlan *petta_program_compile_open_pattern_plan(
        PettaProgram *program, Atom *source,
        const VarId *variable_ids, uint32_t variable_count) {
    /* A list meets its counterpart by elements and rest, not by coordinate;
     * a pattern holding one is matched unplanned. */
    if (!program || !source || atom_structural_may_have_list(source))
        return NULL;
    CettaOpenPatternPlan *plan = arena_alloc(
        &program->plans, sizeof(*plan));
    if (!plan)
        return NULL;
    Atom *ancestors[PETTA_OPEN_PATTERN_PLAN_DEPTH_LIMIT + 1u];
    if (!petta_program_compile_open_pattern_plan_node(
            program, source, ancestors, 0u,
            variable_ids, variable_count, plan)) {
        return NULL;
    }
    /* The tree plan remains independently usable if contiguous allocation is
     * unavailable.  Physical representation choice cannot change matching
     * semantics. */
    (void)petta_program_compile_open_pattern_linear_program(
        program, plan);
    return plan;
}

struct PettaDeclarationBlock {
    const PettaPlanNode **plans;
    int plan_count;
};

static bool petta_program_reserve(
    void **items, size_t *capacity, size_t needed, size_t width) {
    if (needed <= *capacity)
        return true;
    if (width == 0u || needed > SIZE_MAX / width)
        return false;
    size_t next = *capacity ? *capacity : 8u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u) {
            next = needed;
            break;
        }
        next *= 2u;
    }
    if (next > SIZE_MAX / width)
        return false;
    *items = *items
        ? cetta_realloc(*items, width * next)
        : cetta_malloc(width * next);
    *capacity = next;
    return true;
}

#define PETTA_EQUATION_TEMPLATE_C0_MAX_VARIABLE_SPAN 65536u

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
} PettaProgramAtomStack;

typedef struct {
    VarId *items;
    Atom **variables;
    size_t len;
    size_t cap;
} PettaProgramVarSet;

static size_t petta_program_var_lower_bound(
    const PettaProgramVarSet *variables, VarId id) {
    size_t low = 0u;
    size_t high = variables ? variables->len : 0u;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (var_id_lt(variables->items[middle], id))
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static bool petta_program_var_insert(
    PettaProgramVarSet *variables, Atom *variable) {
    if (!variables || !variable || variable->kind != ATOM_VAR ||
        var_id_is_none(variable->var_id))
        return false;
    VarId id = variable->var_id;
    size_t index = petta_program_var_lower_bound(variables, id);
    if (index < variables->len && var_id_eq(variables->items[index], id))
        return true;
    if (variables->len == variables->cap) {
        size_t next_cap = variables->cap ? variables->cap * 2u : 8u;
        if (next_cap <= variables->cap ||
            next_cap > SIZE_MAX / sizeof(*variables->items) ||
            next_cap > SIZE_MAX / sizeof(*variables->variables)) {
            return false;
        }
        variables->items = variables->items
            ? cetta_realloc(
                  variables->items,
                  sizeof(*variables->items) * next_cap)
            : cetta_malloc(sizeof(*variables->items) * next_cap);
        variables->variables = variables->variables
            ? cetta_realloc(
                  variables->variables,
                  sizeof(*variables->variables) * next_cap)
            : cetta_malloc(sizeof(*variables->variables) * next_cap);
        variables->cap = next_cap;
    }
    memmove(
        variables->items + index + 1u,
        variables->items + index,
        sizeof(*variables->items) * (variables->len - index));
    memmove(
        variables->variables + index + 1u,
        variables->variables + index,
        sizeof(*variables->variables) * (variables->len - index));
    variables->items[index] = id;
    variables->variables[index] = variable;
    variables->len++;
    return true;
}

static bool petta_program_collect_variables(
    Atom *root, PettaProgramVarSet *variables) {
    PettaProgramAtomStack stack = {0};
    if (!root || !variables ||
        !petta_program_reserve(
            (void **)&stack.items, &stack.cap, 1u,
            sizeof(*stack.items))) {
        return false;
    }
    stack.items[stack.len++] = root;
    bool ok = true;
    while (stack.len != 0u && ok) {
        Atom *atom = stack.items[--stack.len];
        if (!atom) {
            ok = false;
            break;
        }
        if (atom->kind == ATOM_VAR) {
            ok = petta_program_var_insert(
                variables, atom);
            continue;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        if (atom->expr.len != 0u && !atom->expr.elems) {
            ok = false;
            break;
        }
        if (atom->expr.len > SIZE_MAX - stack.len ||
            !petta_program_reserve(
                (void **)&stack.items, &stack.cap,
                stack.len + (size_t)atom->expr.len,
                sizeof(*stack.items))) {
            ok = false;
            break;
        }
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            stack.items[stack.len++] = atom->expr.elems[index];
        }
    }
    free(stack.items);
    return ok;
}

static bool petta_program_variables_contained(
    const PettaProgramVarSet *contained,
    const PettaProgramVarSet *container) {
    if (!contained || !container)
        return false;
    for (size_t index = 0u; index < contained->len; index++) {
        size_t found = petta_program_var_lower_bound(
            container, contained->items[index]);
        if (found >= container->len ||
            !var_id_eq(container->items[found], contained->items[index])) {
            return false;
        }
    }
    return true;
}

/* Whether `right` holds a variable `left` lacks; both sets are sorted. */
static bool petta_program_variables_beyond(
    const PettaProgramVarSet *left, const PettaProgramVarSet *right) {
    size_t left_index = 0u;
    for (size_t right_index = 0u; right_index < right->len; right_index++) {
        while (left_index < left->len &&
               var_id_lt(left->items[left_index], right->items[right_index]))
            left_index++;
        if (left_index >= left->len ||
            !var_id_eq(left->items[left_index], right->items[right_index]))
            return true;
    }
    return false;
}

/* Whether an equation's body binds variables of its own. */
static bool petta_program_rhs_binds_variables(Atom *lhs, Atom *rhs) {
    if (!rhs || !atom_has_vars(rhs))
        return false;
    PettaProgramVarSet lhs_variables = {0};
    PettaProgramVarSet rhs_variables = {0};
    bool binds = !petta_program_collect_variables(lhs, &lhs_variables) ||
        !petta_program_collect_variables(rhs, &rhs_variables) ||
        petta_program_variables_beyond(&lhs_variables, &rhs_variables);
    free(lhs_variables.items);
    free(lhs_variables.variables);
    free(rhs_variables.items);
    free(rhs_variables.variables);
    return binds;
}

static bool petta_program_variable_union_count(
    const PettaProgramVarSet *left,
    const PettaProgramVarSet *right,
    uint32_t *count_out) {
    if (count_out)
        *count_out = 0u;
    if (!left || !right || !count_out)
        return false;
    size_t left_index = 0u;
    size_t right_index = 0u;
    uint64_t count = 0u;
    while (left_index < left->len || right_index < right->len) {
        if (right_index >= right->len ||
            (left_index < left->len &&
             var_id_lt(left->items[left_index], right->items[right_index]))) {
            left_index++;
        } else if (left_index >= left->len ||
                   var_id_lt(right->items[right_index], left->items[left_index])) {
            right_index++;
        } else {
            left_index++;
            right_index++;
        }
        if (++count > UINT32_MAX)
            return false;
    }
    *count_out = (uint32_t)count;
    return true;
}

static const PettaEquationTemplate *petta_program_compile_equation_template(
        PettaProgram *program, Atom *lhs, Atom *rhs,
        uint32_t variable_count) {
    if (!program || !lhs || !rhs)
        return NULL;
    CettaVarMap inventory = {0};
    Atom *compiled_lhs = cetta_compile_frame_syntax(&program->plans, lhs, &inventory);
    Atom *compiled_rhs = compiled_lhs
        ? cetta_compile_frame_syntax(&program->plans, rhs, &inventory) : NULL;
    if (!compiled_rhs || inventory.len != variable_count) {
        cetta_var_map_free(&inventory);
        return NULL;
    }
    PettaEquationTemplate *template = arena_alloc(&program->plans, sizeof(*template));
    Atom *equation_items[] = {
        atom_symbol(&program->plans, "="), compiled_lhs, compiled_rhs,
    };
    *template = (PettaEquationTemplate){
        .equation = atom_expr(&program->plans, equation_items, 3u),
        .lhs = compiled_lhs,
        .rhs = compiled_rhs,
        .variable_count = variable_count,
        .next_owned = program->equation_templates,
    };
    program->equation_templates = template;
    template->source_variables = variable_count ? arena_alloc(
        &program->plans, sizeof(*template->source_variables) * (size_t)variable_count) : NULL;
    VarId *slots = variable_count
        ? cetta_malloc(sizeof(*slots) * (size_t)variable_count) : NULL;
    for (uint32_t slot = 0u; slot < variable_count; slot++) {
        slots[slot] = var_id_from_base((uint64_t)slot + 1u);
        template->source_variables[slot] = inventory.items[slot].mapped_var;
    }
    template->frame_schema = bindings_frame_schema_new_presented(
        slots, template->source_variables, variable_count);
    free(slots);
    cetta_var_map_free(&inventory);
    if (!template->equation || !template->frame_schema)
        return NULL;
    template->lhs_match_plan = petta_program_compile_open_pattern_plan(
        program, compiled_lhs,
        bindings_frame_schema_source_ids(template->frame_schema), variable_count);
    return template;
}

typedef struct {
    Atom *atom;
    PettaPlanNode *plan;
} PettaEquationSlotWorkItem;

static bool petta_equation_template_find_variable_slot(
        const PettaEquationTemplate *template, VarId id,
        uint32_t *slot_out) {
    return template && petta_program_variable_slot(
        bindings_frame_schema_source_ids(template->frame_schema), template->variable_count,
        id, slot_out);
}

static bool petta_plan_assign_equation_variable_slots(
        Atom *root, const PettaPlanNode *root_plan,
        const PettaEquationTemplate *template) {
    if (!root || !root_plan || !template)
        return false;
    PettaEquationSlotWorkItem *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return false;
    }
    work[work_len++] = (PettaEquationSlotWorkItem){
        .atom = root,
        .plan = (PettaPlanNode *)root_plan,
    };
    bool ok = true;
    while (work_len > 0u && ok) {
        PettaEquationSlotWorkItem item = work[--work_len];
        if (!item.atom || !item.plan) {
            ok = false;
            break;
        }
        if (item.atom->kind == ATOM_VAR) {
            uint32_t slot = 0u;
            ok = petta_equation_template_find_variable_slot(
                template, item.atom->var_id, &slot);
            if (ok) {
                item.plan->has_equation_variable_slot = true;
                item.plan->equation_variable_slot = slot;
            }
            continue;
        }
        if (item.atom->kind != ATOM_EXPR) {
            if (item.plan->child_count != 0u)
                ok = false;
            continue;
        }
        if (item.plan->child_count != item.atom->expr.len ||
            (item.atom->expr.len != 0u &&
             (!item.atom->expr.elems || !item.plan->children)) ||
            (size_t)item.atom->expr.len > SIZE_MAX - work_len ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + (size_t)item.atom->expr.len,
                sizeof(*work))) {
            ok = false;
            break;
        }
        for (CettaExprIndex index = item.atom->expr.len;
             index > 0u; index--) {
            CettaExprIndex child = index - 1u;
            work[work_len++] = (PettaEquationSlotWorkItem){
                .atom = item.atom->expr.elems[child],
                .plan = (PettaPlanNode *)&item.plan->children[child],
            };
        }
    }
    free(work);
    return ok;
}

static void petta_equation_template_c0_free(
    PettaEquationTemplateC0 *template) {
    if (!template)
        return;
    cetta_gslt_ground_dense_term_program_free_v1(&template->lhs);
    cetta_gslt_ground_dense_term_program_free_v1(&template->rhs);
    free(template);
}

static PettaEquationTemplateC0 *petta_program_compile_equation_template_c0(
    PettaProgram *program, Atom *lhs, Atom *rhs,
    SymbolId root_symbol, uint32_t *static_variable_count_out,
    bool *open_template_admitted_out,
    const PettaEquationTemplate **equation_template_out) {
    if (static_variable_count_out)
        *static_variable_count_out = 0u;
    if (open_template_admitted_out)
        *open_template_admitted_out = false;
    if (equation_template_out)
        *equation_template_out = NULL;
    if (!program || !lhs || !rhs || root_symbol == SYMBOL_ID_NONE ||
        !static_variable_count_out || !open_template_admitted_out ||
        !equation_template_out)
        return NULL;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_EQUATION_TEMPLATE_C0_ADMISSION_ATTEMPT);
    PettaProgramVarSet lhs_variables = {0};
    PettaProgramVarSet rhs_variables = {0};
    bool variables_collected =
        petta_program_collect_variables(lhs, &lhs_variables) &&
        petta_program_collect_variables(rhs, &rhs_variables) &&
        petta_program_variable_union_count(
            &lhs_variables, &rhs_variables,
            static_variable_count_out);
    /* A list meets its counterpart by elements and rest, which the dense
     * template does not encode. */
    bool open_admitted =
        variables_collected &&
        !petta_semantics_contains_cons_constraint(lhs) &&
        !atom_structural_may_have_list(lhs);
    size_t union_variables =
        (size_t)*static_variable_count_out;
    VarId union_first_variable = var_id_from_base(1u);
    VarId union_last_variable = VAR_ID_NONE;
    if (open_admitted && union_variables != 0u) {
        bool lhs_nonempty = lhs_variables.len != 0u;
        bool rhs_nonempty = rhs_variables.len != 0u;
        union_first_variable = lhs_nonempty && rhs_nonempty
            ? (var_id_lt(lhs_variables.items[0], rhs_variables.items[0])
                   ? lhs_variables.items[0] : rhs_variables.items[0])
            : lhs_nonempty
                ? lhs_variables.items[0] : rhs_variables.items[0];
        VarId lhs_last = lhs_nonempty
            ? lhs_variables.items[lhs_variables.len - 1u] : VAR_ID_NONE;
        VarId rhs_last = rhs_nonempty
            ? rhs_variables.items[rhs_variables.len - 1u] : VAR_ID_NONE;
        union_last_variable = var_id_lt(rhs_last, lhs_last)
            ? lhs_last : rhs_last;
        uint64_t union_span =
            union_last_variable.base - union_first_variable.base + 1u;
        open_admitted =
            union_span <= PETTA_EQUATION_TEMPLATE_C0_MAX_VARIABLE_SPAN;
    }
    *open_template_admitted_out = open_admitted;
    if (variables_collected) {
        *equation_template_out = petta_program_compile_equation_template(
            program, lhs, rhs, *static_variable_count_out);
    }
    bool admitted =
        open_admitted &&
        petta_program_variables_contained(
            &rhs_variables, &lhs_variables) &&
        lhs_variables.len <= UINT32_MAX;
    VarId first_variable = var_id_from_base(1u);
    uint32_t variable_width = 0u;
    if (admitted && lhs_variables.len != 0u) {
        first_variable = lhs_variables.items[0];
        VarId last_variable =
            lhs_variables.items[lhs_variables.len - 1u];
        uint64_t span = last_variable.base - first_variable.base + 1u;
        admitted = span <= PETTA_EQUATION_TEMPLATE_C0_MAX_VARIABLE_SPAN;
        if (admitted)
            variable_width = (uint32_t)span;
    }
    PettaEquationTemplateC0 *template = admitted
        ? calloc(1u, sizeof(*template)) : NULL;
    if (template) {
        cetta_gslt_ground_dense_term_program_init_v1(
            &template->lhs);
        cetta_gslt_ground_dense_term_program_init_v1(
            &template->rhs);
        admitted =
            cetta_gslt_ground_dense_term_compile_v1(
                &template->lhs, lhs, first_variable,
                variable_width, NULL, 0u) &&
            cetta_gslt_ground_dense_term_compile_v1(
                &template->rhs, rhs, first_variable,
                variable_width, NULL, 0u);
    }
    free(lhs_variables.items);
    free(lhs_variables.variables);
    free(rhs_variables.items);
    free(rhs_variables.variables);
    if (!admitted || !template ||
        !petta_program_reserve(
            (void **)&program->equation_template_c0,
            &program->equation_template_c0_cap,
            program->equation_template_c0_len + 1u,
            sizeof(*program->equation_template_c0))) {
        petta_equation_template_c0_free(template);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_EQUATION_TEMPLATE_C0_ARTIFACT_DECLINED);
        return NULL;
    }
    program->equation_template_c0[program->equation_template_c0_len++] = template;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_EQUATION_TEMPLATE_C0_ARTIFACT_BUILT);
    return template;
}

PettaEquationTemplateC0Status petta_equation_template_c0_apply(
    const PettaEquationTemplateC0 *template,
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    Atom *closed_query, Arena *arena, Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    if (!template || !workspace || !closed_query || !arena ||
        !result_out || atom_has_vars(closed_query)) {
        return PETTA_EQUATION_TEMPLATE_C0_NOT_APPLICABLE;
    }
    CettaGsltGroundDenseStatusV1 matched =
        cetta_gslt_ground_dense_term_match_v1(
            workspace, &template->lhs, closed_query, NULL);
    if (matched == CETTA_GSLT_GROUND_DENSE_RESOURCE_V1)
        return PETTA_EQUATION_TEMPLATE_C0_CAPACITY;
    if (matched == CETTA_GSLT_GROUND_DENSE_MISMATCH_V1) {
        cetta_gslt_ground_dense_workspace_discard_match_v1(workspace);
        return PETTA_EQUATION_TEMPLATE_C0_MISMATCH;
    }
    if (matched != CETTA_GSLT_GROUND_DENSE_OK_V1) {
        cetta_gslt_ground_dense_workspace_discard_match_v1(workspace);
        return PETTA_EQUATION_TEMPLATE_C0_NOT_APPLICABLE;
    }
    CettaSurvivorAllocationScope allocation_scope =
        cetta_survivor_allocation_scope_enter(
            CETTA_SURVIVOR_ALLOC_ROLE_EQUATION_RESULT_INSTANTIATION);
    CettaGsltGroundDenseStatusV1 instantiated =
        cetta_gslt_ground_dense_term_instantiate_v1(
            workspace, &template->rhs, arena, result_out, NULL);
    cetta_survivor_allocation_scope_leave(allocation_scope);
    cetta_gslt_ground_dense_workspace_discard_match_v1(workspace);
    if (instantiated == CETTA_GSLT_GROUND_DENSE_OK_V1)
        return PETTA_EQUATION_TEMPLATE_C0_MATCH;
    *result_out = NULL;
    return instantiated == CETTA_GSLT_GROUND_DENSE_RESOURCE_V1
        ? PETTA_EQUATION_TEMPLATE_C0_CAPACITY
        : PETTA_EQUATION_TEMPLATE_C0_NOT_APPLICABLE;
}

bool petta_equation_template_variable_inventory(
        const PettaEquationTemplate *template,
        const VarId **source_ids_out,
        Atom *const **source_variables_out,
        uint32_t *variable_count_out) {
    if (source_ids_out)
        *source_ids_out = NULL;
    if (source_variables_out)
        *source_variables_out = NULL;
    if (variable_count_out)
        *variable_count_out = 0u;
    if (!template || !source_ids_out || !source_variables_out ||
        !variable_count_out) {
        return false;
    }
    *source_ids_out = bindings_frame_schema_source_ids(template->frame_schema);
    *source_variables_out = template->source_variables;
    *variable_count_out = template->variable_count;
    return template->variable_count == 0u ||
        (template->frame_schema && template->source_variables);
}

Atom *petta_equation_template_syntax(const PettaEquationTemplate *template) {
    return template ? template->equation : NULL;
}

BindingsFrameSchema *petta_equation_template_frame_schema(
        const PettaEquationTemplate *template) {
    return template ? template->frame_schema : NULL;
}

const CettaOpenPatternPlan *petta_equation_template_lhs_match_plan(
        const PettaEquationTemplate *template) {
    return template ? template->lhs_match_plan : NULL;
}

bool petta_program_type_is_exclusive_kind(const Atom *type) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len == 0u ||
        type->expr.elems[0]->kind != ATOM_SYMBOL || !g_symbols) {
        return false;
    }
    const char *head = atom_symbol_bytes(type->expr.elems[0]);
    if (!head)
        return false;
    return strcmp(head, "Alias") == 0 ||
           strcmp(head, "Newtype") == 0 ||
           strcmp(head, "SpaceOf") == 0 ||
           strcmp(head, "Foreign") == 0;
}

static void petta_program_space_clear_type_index(
    PettaProgramAnalysisSpace *space) {
    if (!space)
        return;
    for (size_t index = 0u; index < space->type_bucket_len; index++)
        free(space->type_buckets[index].types);
    free(space->type_buckets);
    space->type_buckets = NULL;
    space->type_bucket_len = 0u;
    space->type_bucket_cap = 0u;
    space->type_index_dirty = true;
}

static size_t petta_program_type_bucket_lower_bound(
    const PettaProgramAnalysisSpace *space, SymbolId subject) {
    size_t low = 0u;
    size_t high = space ? space->type_bucket_len : 0u;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (space->type_buckets[middle].subject < subject)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static PettaProgramTypeBucket *petta_program_type_bucket(
    PettaProgramAnalysisSpace *space, SymbolId subject, bool create) {
    if (!space || subject == SYMBOL_ID_NONE)
        return NULL;
    size_t index =
        petta_program_type_bucket_lower_bound(space, subject);
    if (index < space->type_bucket_len &&
        space->type_buckets[index].subject == subject) {
        return &space->type_buckets[index];
    }
    if (!create || !petta_program_reserve(
            (void **)&space->type_buckets,
            &space->type_bucket_cap,
            space->type_bucket_len + 1u,
            sizeof(*space->type_buckets))) {
        return NULL;
    }
    memmove(
        space->type_buckets + index + 1u,
        space->type_buckets + index,
        sizeof(*space->type_buckets) *
            (space->type_bucket_len - index));
    space->type_buckets[index] = (PettaProgramTypeBucket){
        .subject = subject,
    };
    space->type_bucket_len++;
    return &space->type_buckets[index];
}

static bool petta_program_space_rebuild_type_index(
    PettaProgramAnalysisSpace *space) {
    if (!space)
        return false;
    petta_program_space_clear_type_index(space);
    for (size_t index = 0u;
         index < space->type_annotation_len; index++) {
        Atom *annotation = space->type_annotations[index];
        if (!annotation || annotation->kind != ATOM_EXPR ||
            annotation->expr.len != 3u ||
            annotation->expr.elems[1]->kind != ATOM_SYMBOL) {
            continue;
        }
        PettaProgramTypeBucket *bucket =
            petta_program_type_bucket(
                space, atom_symbol_current_id(annotation->expr.elems[1]), true);
        if (!bucket || !petta_program_reserve(
                (void **)&bucket->types, &bucket->cap,
                bucket->len + 1u, sizeof(*bucket->types))) {
            petta_program_space_clear_type_index(space);
            return false;
        }
        bucket->types[bucket->len++] = annotation->expr.elems[2];
    }
    space->type_index_dirty = false;
    return true;
}

static size_t petta_program_head_bucket_lower_bound(
    const PettaProgramSpace *space, SymbolId head) {
    size_t low = 0u;
    size_t high = space ? space->head_bucket_len : 0u;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (space->head_buckets[middle].head < head)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static void petta_program_space_clear_head_index(
    PettaProgramSpace *space) {
    if (!space)
        return;
    for (size_t index = 0u;
         index < space->head_bucket_len; index++) {
        free(space->head_buckets[index].record_indices);
    }
    free(space->head_buckets);
    space->head_buckets = NULL;
    space->head_bucket_len = 0u;
    space->head_bucket_cap = 0u;
}

static void petta_program_space_clear_candidate_snapshots(
    PettaProgramSpace *space) {
    if (!space)
        return;
    for (size_t index = 0u;
         index < space->snapshot_len; index++) {
        petta_equation_storage_retire(space->snapshots[index].storage);
        petta_equation_storage_release(space->snapshots[index].storage);
    }
    free(space->snapshots);
    space->snapshots = NULL;
    space->snapshot_len = 0u;
    space->snapshot_cap = 0u;
}

static size_t petta_program_snapshot_lower_bound(
    const PettaProgramSpace *space, SymbolId head) {
    size_t low = 0u;
    size_t high = space ? space->snapshot_len : 0u;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (space->snapshots[middle].head < head)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static PettaProgramCandidateSnapshotKey
petta_program_candidate_snapshot_key(const Space *space) {
    return (PettaProgramCandidateSnapshotKey){
        .equations = space_equation_token(space),
        .prefix_epoch = space ? space->prefix_epoch : 0u,
    };
}

static bool petta_program_candidate_snapshot_key_matches_live_space(
        PettaProgramCandidateSnapshotKey key, const Space *space) {
    return space && key.prefix_epoch == space->prefix_epoch &&
           space_equation_token_matches_live_space(
               key.equations, space);
}

static const PettaProgramCandidateSnapshot *
petta_program_space_find_candidate_snapshot(
    const PettaProgramSpace *space, SymbolId head,
    const Space *live_space) {
    if (!space)
        return NULL;
    size_t index = petta_program_snapshot_lower_bound(
        space, head);
    if (index >= space->snapshot_len ||
        space->snapshots[index].head != head ||
        !petta_program_candidate_snapshot_key_matches_live_space(
            space->snapshots[index].key, live_space)) {
        return NULL;
    }
    return &space->snapshots[index];
}

static bool petta_program_space_store_candidate_snapshot_take(
    PettaProgramSpace *space, SymbolId head, SpaceReadToken source,
    PettaProgramCandidateSnapshotKey key,
    PettaEquationCandidate *candidates, size_t candidate_count, size_t candidate_capacity) {
    if (!space || !source.space || head == SYMBOL_ID_NONE ||
        candidate_count > candidate_capacity ||
        candidate_capacity > SIZE_MAX / sizeof(*candidates)) {
        return false;
    }

    size_t index = petta_program_snapshot_lower_bound(
        space, head);
    if (index < space->snapshot_len &&
        space->snapshots[index].head == head) {
        petta_equation_storage_retire(space->snapshots[index].storage);
        petta_equation_storage_release(space->snapshots[index].storage);
    } else {
        if (!petta_program_reserve(
                (void **)&space->snapshots,
                &space->snapshot_cap,
                space->snapshot_len + 1u,
                sizeof(*space->snapshots))) {
            return false;
        }
        memmove(
            space->snapshots + index + 1u,
            space->snapshots + index,
            sizeof(*space->snapshots) *
                (space->snapshot_len - index));
        space->snapshot_len++;
    }
    space->snapshots[index] = (PettaProgramCandidateSnapshot){
        .head = head,
        .source = source,
        .key = key,
        .candidates = candidates,
        .len = candidate_count,
        .storage = petta_equation_storage_take(candidates, candidate_count, candidate_capacity),
    };
    return true;
}

static bool petta_program_space_head_index_append(
    PettaProgramSpace *space, SymbolId head,
    size_t record_index) {
    if (!space)
        return false;
    size_t bucket_index = petta_program_head_bucket_lower_bound(
        space, head);
    if (bucket_index == space->head_bucket_len ||
        space->head_buckets[bucket_index].head != head) {
        if (!petta_program_reserve(
                (void **)&space->head_buckets,
                &space->head_bucket_cap,
                space->head_bucket_len + 1u,
                sizeof(*space->head_buckets))) {
            return false;
        }
        memmove(
            space->head_buckets + bucket_index + 1u,
            space->head_buckets + bucket_index,
            sizeof(*space->head_buckets) *
                (space->head_bucket_len - bucket_index));
        space->head_buckets[bucket_index] =
            (PettaProgramHeadBucket){
                .head = head,
            };
        space->head_bucket_len++;
    }
    PettaProgramHeadBucket *bucket =
        &space->head_buckets[bucket_index];
    if (!petta_program_reserve(
            (void **)&bucket->record_indices, &bucket->cap,
            bucket->len + 1u,
            sizeof(*bucket->record_indices))) {
        return false;
    }
    bucket->record_indices[bucket->len++] = record_index;
    return true;
}

static bool petta_program_space_rebuild_head_index(
    PettaProgramSpace *space) {
    if (!space)
        return false;
    petta_program_space_clear_head_index(space);
    for (size_t index = 0u; index < space->equation_len; index++) {
        if (!petta_program_space_head_index_append(
                space, space->equations[index].head, index)) {
            petta_program_space_clear_head_index(space);
            space->head_index_dirty = true;
            return false;
        }
    }
    space->head_index_dirty = false;
    return true;
}

static const PettaProgramHeadBucket *
petta_program_space_find_head_bucket(
    const PettaProgramSpace *space, SymbolId head) {
    if (!space || space->head_index_dirty)
        return NULL;
    size_t index = petta_program_head_bucket_lower_bound(
        space, head);
    return index < space->head_bucket_len &&
           space->head_buckets[index].head == head
        ? &space->head_buckets[index]
        : NULL;
}

static size_t petta_callability_named_lower_bound(
    const PettaCallabilityDomain *domain, SymbolId head) {
    size_t low = 0u;
    size_t high = domain ? domain->named_len : 0u;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (domain->named_heads[middle] < head)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static bool petta_callability_contains_named(
    const PettaCallabilityDomain *domain, SymbolId head) {
    if (!domain || head == SYMBOL_ID_NONE)
        return false;
    size_t index = petta_callability_named_lower_bound(
        domain, head);
    return index < domain->named_len &&
           domain->named_heads[index] == head;
}

static bool petta_callability_admits(
    const PettaCallabilityDomain *domain, SymbolId head) {
    return domain && head != SYMBOL_ID_NONE &&
        (domain->admits_any_head ||
         petta_callability_contains_named(domain, head));
}

static bool petta_callability_insert_named(
    PettaCallabilityDomain *domain, SymbolId head) {
    if (!domain || head == SYMBOL_ID_NONE)
        return false;
    size_t index = petta_callability_named_lower_bound(
        domain, head);
    if (index < domain->named_len &&
        domain->named_heads[index] == head) {
        return true;
    }
    if (!petta_program_reserve(
            (void **)&domain->named_heads,
            &domain->named_cap, domain->named_len + 1u,
            sizeof(*domain->named_heads))) {
        return false;
    }
    memmove(
        domain->named_heads + index + 1u,
        domain->named_heads + index,
        sizeof(*domain->named_heads) *
            (domain->named_len - index));
    domain->named_heads[index] = head;
    domain->named_len++;
    return true;
}

static bool petta_rule_syntax_view(
    Atom *atom, unsigned admitted_forms, bool include_graded,
    Atom **lhs, Atom **rhs, SymbolId *head) {
    if (lhs)
        *lhs = NULL;
    if (rhs)
        *rhs = NULL;
    if (head)
        *head = SYMBOL_ID_NONE;
    CettaRuleDescriptor rule;
    if (!cetta_rule_view(atom, admitted_forms, &rule) &&
        !(include_graded && (admitted_forms & CETTA_RULE_SYNTAX_ORDINARY) &&
          space_equation_view(atom, true, NULL, NULL, NULL)))
        return false;
    Atom *left = atom->expr.elems[1];
    if (!left || left->kind != ATOM_EXPR ||
        left->expr.len == 0u) {
        return false;
    }
    if (lhs)
        *lhs = left;
    if (rhs)
        *rhs = atom->expr.elems[2];
    if (head && left->expr.elems[0]->kind == ATOM_SYMBOL)
        *head = atom_symbol_current_id(left->expr.elems[0]);
    return true;
}

static bool petta_equation_view(
    Atom *atom, Atom **lhs, Atom **rhs, SymbolId *head) {
    return petta_rule_syntax_view(atom, CETTA_RULE_SYNTAX_ORDINARY, true, lhs, rhs, head);
}

static unsigned petta_program_rule_forms(const PettaProgram *program) {
    return program ? program->admitted_rule_forms : CETTA_RULE_SYNTAX_ORDINARY;
}

static bool petta_program_equation_view(const PettaProgram *program,
    Atom *atom, Atom **lhs, Atom **rhs, SymbolId *head) {
    return petta_rule_syntax_view(atom, petta_program_rule_forms(program),
        program && program->graded_equations, lhs, rhs, head);
}

bool petta_program_admits_equation(const PettaProgram *program, Atom *atom) {
    return petta_program_equation_view(program, atom, NULL, NULL, NULL);
}

/* Only a variable in function position can unify with every named head.
 * Structured and grounded heads retain their outer constructor and therefore
 * cannot serve as universal callability evidence. */
static bool petta_equation_lhs_admits_any_named_head(
        const Atom *lhs) {
    return lhs && lhs->kind == ATOM_EXPR && lhs->expr.len > 0u &&
           lhs->expr.elems[0] &&
           lhs->expr.elems[0]->kind == ATOM_VAR;
}

static PettaEquationActivationLayout petta_equation_activation_layout(
    Atom *equation, uint32_t static_variable_count) {
    PettaEquationActivationLayout layout = {0};
    if (petta_rule_syntax_view(equation,
            CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL, true,
            &layout.lhs, &layout.rhs, NULL)) {
        layout.binding = atom_is_symbol_id(equation->expr.elems[0], g_builtin_syms.equals_percent)
            ? CETTA_RULE_BIND_HEAD : CETTA_RULE_BIND_UNIFY;
        layout.static_variable_count = static_variable_count;
        layout.lhs_contains_cons_constraint_valid = true;
        layout.lhs_contains_cons_constraint =
            petta_semantics_contains_cons_constraint(layout.lhs);
    }
    return layout;
}

bool petta_program_is_equation(Atom *atom) {
    return space_equation_view(atom, false, NULL, NULL, NULL) &&
           petta_equation_view(atom, NULL, NULL, NULL);
}

void petta_program_enable_graded_equations(PettaProgram *program) {
    CETTA_SCOPED_SHARED_TRANSITION(program_profile_update);
    if (program) program->graded_equations = true;
}

bool petta_program_interprets_graded_equations(const PettaProgram *program) {
    return program && program->graded_equations;
}

static bool petta_program_type_declaration_view(
        Atom *atom, Atom **subject, Atom **type) {
    if (!atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u ||
        !atom_is_symbol_id(
            atom->expr.elems[0], g_builtin_syms.colon) ||
        atom->expr.elems[1]->kind != ATOM_SYMBOL) {
        return false;
    }
    if (subject)
        *subject = atom->expr.elems[1];
    if (type)
        *type = atom->expr.elems[2];
    return true;
}

bool petta_program_form_id_may_affect_metadata(const TermUniverse *universe,
                                               AtomId id) {
    if (!universe || tu_kind(universe, id) != ATOM_EXPR ||
        tu_arity(universe, id) != 3u)
        return false;
    SymbolId head = tu_head_sym(universe, id);
    return head == g_builtin_syms.equals ||
        head == g_builtin_syms.equals_percent ||
        head == g_builtin_syms.colon;
}

bool petta_program_atom_affects_metadata(Atom *atom) {
    return petta_rule_syntax_view(atom,
               CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL, true,
               NULL, NULL, NULL) ||
           petta_program_type_declaration_view(atom, NULL, NULL);
}

static PettaProgramSpace *petta_program_find_space(
    PettaProgram *program, const Space *space);
static PettaProgramSpace *petta_program_ensure_space(
    PettaProgram *program, Space *space);
static bool petta_program_space_pending(
        const PettaProgramSpace *entry, SymbolId head);
static bool petta_program_space_declare_pending(
        PettaProgramSpace *entry, SymbolId head);

bool petta_program_predeclare_equation(
    PettaProgram *program, Space *space, Atom *atom) {
    CETTA_SCOPED_SHARED_TRANSITION(program_predeclare);
    if (!program || !space || !atom)
        return false;
    Atom *lhs = NULL;
    SymbolId head = SYMBOL_ID_NONE;
    if (!petta_program_equation_view(program, atom, &lhs, NULL, &head))
        return false;
    program->predeclared_generation++;
    if (petta_equation_lhs_admits_any_named_head(lhs)) {
        program->predeclared_callability.admits_any_head = true;
    }
    if (head == SYMBOL_ID_NONE)
        return true;
    PettaProgramSpace *entry = petta_program_ensure_space(program, space);
    return entry && petta_program_space_declare_pending(entry, head) &&
           petta_callability_insert_named(
               &program->predeclared_callability, head);
}

bool petta_program_function_registered(
    PettaProgram *program, Space *space, SymbolId head) {
    CETTA_SCOPED_SHARED_TRANSITION(program_registration_read);
    if (!program || !space || head == SYMBOL_ID_NONE ||
        !petta_program_synchronize_space(program, space))
        return false;
    PettaProgramSpace *entry = petta_program_find_space(program, space);
    if (!entry)
        return false;
    if (petta_program_space_pending(entry, head))
        return true;
    if (!entry->head_index_dirty ||
        petta_program_space_rebuild_head_index(entry)) {
        const PettaProgramHeadBucket *bucket =
            petta_program_space_find_head_bucket(entry, head);
        return bucket && bucket->len > 0u;
    }
    for (size_t index = 0u; index < entry->equation_len; index++) {
        if (entry->equations[index].head == head)
            return true;
    }
    return false;
}

bool petta_program_head_declared(
    const PettaProgram *program, SymbolId head) {
    CETTA_SCOPED_SHARED_TRANSITION(program_head_read);
    if (!program || head == SYMBOL_ID_NONE)
        return false;
    if (petta_callability_contains_named(
            &program->predeclared_callability, head)) {
        return true;
    }
    for (size_t space_index = 0u;
         space_index < program->space_len; space_index++) {
        const PettaProgramSpace *space =
            &program->spaces[space_index];
        if (!space->space ||
            space->instance_id !=
                space_instance_id(space->space)) {
            continue;
        }
        for (size_t equation_index = 0u;
             equation_index < space->equation_len; equation_index++) {
            if (space->equations[equation_index].head == head)
                return true;
        }
    }
    return false;
}

/* Machine-named and typecheck-named heads, interned once per symbol table
 * instance so classification compares identifiers instead of spellings. */
typedef struct {
    const SymbolTable *table;
    uint64_t table_instance_id;
    SymbolId member;
    SymbolId last;
    SymbolId reverse;
    SymbolId empty;
    SymbolId min;
    SymbolId max;
    SymbolId transaction;
    SymbolId with_mutex;
    SymbolId data;
    SymbolId make_list;
    SymbolId the;
} PettaIntrinsicNameIds;

static _Thread_local PettaIntrinsicNameIds g_petta_intrinsic_name_ids;

static const PettaIntrinsicNameIds *petta_intrinsic_name_ids(void) {
    PettaIntrinsicNameIds *ids = &g_petta_intrinsic_name_ids;
    uint64_t table_instance_id = symbol_table_instance_id(g_symbols);
    if (ids->table == g_symbols && g_symbols &&
        ids->table_instance_id == table_instance_id) {
        return ids;
    }
    *ids = (PettaIntrinsicNameIds){
        .table = g_symbols,
        .table_instance_id = table_instance_id,
        .member = SYMBOL_ID_NONE,
        .last = SYMBOL_ID_NONE,
        .reverse = SYMBOL_ID_NONE,
        .empty = SYMBOL_ID_NONE,
        .min = SYMBOL_ID_NONE,
        .max = SYMBOL_ID_NONE,
        .transaction = SYMBOL_ID_NONE,
        .with_mutex = SYMBOL_ID_NONE,
        .data = SYMBOL_ID_NONE,
        .make_list = SYMBOL_ID_NONE,
        .the = SYMBOL_ID_NONE,
    };
    if (!g_symbols)
        return ids;
    ids->member = symbol_intern_cstr(g_symbols, "member");
    ids->last = symbol_intern_cstr(g_symbols, "last");
    ids->reverse = symbol_intern_cstr(g_symbols, "reverse");
    ids->empty = symbol_intern_cstr(g_symbols, "empty");
    ids->min = symbol_intern_cstr(g_symbols, "min");
    ids->max = symbol_intern_cstr(g_symbols, "max");
    ids->transaction = symbol_intern_cstr(g_symbols, "transaction");
    ids->with_mutex = symbol_intern_cstr(g_symbols, "with_mutex");
    ids->data = symbol_intern_cstr(g_symbols, "data");
    ids->make_list = symbol_intern_cstr(g_symbols, "make-list");
    ids->the = symbol_intern_cstr(g_symbols, "the");
    return ids;
}

/* Builtin names CeTTa interns for its own bookkeeping that name no PeTTa
 * operation: metatype and truth names, search-policy and profile names, ABT
 * tags, the list and handle tags, and the query algebra's connectives.  The
 * reference registers none of them as a function, and PeTTa gives none of
 * them a meaning in an equation body or head, so an expression they head is
 * data, as any unregistered head is.  `,` and `|` are connectives only inside
 * a `match` query or space argument, where `match` interprets them.  `&self`
 * names a space, an atom in the reference; as an operation the host would
 * read it as the space object itself.  A name left off this list stays an
 * operation, so the host evaluates the expression it heads. */
static bool petta_program_builtin_names_data(SymbolId head) {
    return head == g_builtin_syms.self ||
           head == g_builtin_syms.llist_cons ||
           head == g_builtin_syms.llist_nil ||
           head == g_builtin_syms.empty ||
           head == g_builtin_syms.error ||
           head == g_builtin_syms.atom ||
           head == g_builtin_syms.prime_proc ||
           head == g_builtin_syms.symbol ||
           head == g_builtin_syms.variable ||
           head == g_builtin_syms.expression ||
           head == g_builtin_syms.grounded ||
           head == g_builtin_syms.grounded_placeholder ||
           head == g_builtin_syms.undefined_type ||
           head == g_builtin_syms.true_text ||
           head == g_builtin_syms.false_text ||
           head == g_builtin_syms.bindings ||
           head == g_builtin_syms.comma ||
           head == g_builtin_syms.pipe ||
           head == g_builtin_syms.search_policy ||
           head == g_builtin_syms.order ||
           head == g_builtin_syms.native ||
           head == g_builtin_syms.mork_text ||
           head == g_builtin_syms.lex ||
           head == g_builtin_syms.shortlex ||
           head == g_builtin_syms.recursive_dependent_proof ||
           head == g_builtin_syms.atp_guided_inhabitation ||
           head == g_builtin_syms.atp_saturation ||
           head == g_builtin_syms.solver_oracle ||
           head == g_builtin_syms.auto_text ||
           head == g_builtin_syms.interpreter ||
           head == g_builtin_syms.bare_minimal ||
           head == g_builtin_syms.max_stack_depth ||
           head == g_builtin_syms.type_scheme ||
           head == g_builtin_syms.chaining_rule ||
           head == g_builtin_syms.type_level_function ||
           head == g_builtin_syms.prime_package ||
           head == g_builtin_syms.nik_colon_check ||
           head == g_builtin_syms.abt_chain_v1 ||
           head == g_builtin_syms.abt_pattern_var_v1 ||
           head == g_builtin_syms.abt_let_scope_v1 ||
           head == g_builtin_syms.abt_let_v1 ||
           head == g_builtin_syms.module_parse_failed ||
           head == g_builtin_syms.native_handle;
}

bool petta_program_head_is_intrinsic(SymbolId head) {
    /* A registered pattern spelling becomes an operation only in its admitted
     * module/profile. Otherwise its call and operands retain ordinary PeTTa
     * interpretation, including named-space symbols. */
    if (grounded_op_is_pattern(head))
        return eval_current_pattern_head &&
               eval_current_pattern_head(head) != SYMBOL_ID_NONE;
    const PettaIntrinsicNameIds *ids =
        head == SYMBOL_ID_NONE ? NULL : petta_intrinsic_name_ids();
    bool machine_named =
        ids && ids->table &&
        (head == ids->member ||
         head == ids->last ||
         head == ids->reverse ||
         head == ids->empty ||
         head == ids->min ||
         head == ids->max ||
         head == ids->transaction ||
         head == ids->with_mutex ||
         petta_semantics_is_mm2_exec(head));
    /* `data` is shared with historical extended PeTTa.  Live `make-list`
     * and `the` forms are owned only by typecheck-v2; extended erases `the`
     * during document ingestion. */
    bool typecheck_named =
        ids && ids->table && cetta_petta_profile_admits_typecheck_ops() &&
        (head == ids->data ||
         (cetta_petta_profile_admits_native_typecheck_v2() &&
          (head == ids->make_list ||
           head == ids->the)));
    PeTTaForm form = petta_semantics_form(head);
    bool intrinsic_form =
        form != PETTA_FORM_NONE &&
        form != PETTA_FORM_PROCESS_METTA_STRING &&
        form != PETTA_FORM_TABLED;
    return head != SYMBOL_ID_NONE &&
           (intrinsic_form ||
            /* Builtin data names are not PeTTa operations.  User
             * definitions still establish callability through ordinary
             * resolution. */
            (symbol_id_is_builtin_for(head, eval_current_language_id ? eval_current_language_id() : CETTA_LANGUAGE_HE) &&
             !petta_program_builtin_names_data(head)) ||
            is_grounded_op(head) ||
            (grounded_op_is_pattern(head) && eval_current_pattern_control_arity &&
             eval_current_pattern_control_arity(head)) ||
            machine_named ||
            typecheck_named);
}

typedef struct {
    Atom *atom;
    PettaPlanNode *plan;
    /* Inside a list: every occurrence is a value. */
    bool value;
    /* The operand of `call`: a direct call, even to a function installed
     * only later. */
    bool direct_call;
} PettaPlanBuildItem;

typedef struct {
    PettaPlanNode *plan;
    bool expanded;
} PettaPlanFeatureItem;

typedef struct {
    Atom *source;
    PettaPlanNode *plan;
    size_t first_child;
    size_t child_count;
} PettaScalarRegionSourceNode;

typedef struct {
    size_t source_node;
    bool expanded;
} PettaScalarRegionTraversal;

typedef struct {
    Atom *source;
    PettaPlanNode *plan;
    bool parent_is_scalar_region;
} PettaScalarRegionRootItem;

static bool petta_plan_source_is_anonymous_variable(
        const Atom *source) {
    return source && source->kind == ATOM_VAR &&
        atom_symbol_current_id(source) != SYMBOL_ID_NONE && g_symbols &&
        atom_symbol_length(source) == 1u &&
        atom_symbol_bytes(source)[0] == '_';
}

static bool petta_head_transports_source_occurrences(
        SymbolId head) {
    return head == g_builtin_syms.hyperpose;
}

static bool petta_plan_finish_features(
    PettaPlanNode *root) {
    if (!root)
        return false;
    PettaPlanFeatureItem *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u,
            sizeof(*work))) {
        return false;
    }
    work[work_len++] = (PettaPlanFeatureItem){
        .plan = root,
    };
    bool ok = true;
    while (work_len > 0u && ok) {
        PettaPlanFeatureItem item = work[--work_len];
        PettaPlanNode *node = item.plan;
        if (!node) {
            ok = false;
            break;
        }
        if (item.expanded) {
            bool descendant_contains_call = false;
            node->contains_call =
                node->role == PETTA_PLAN_STATIC_CALL ||
                node->role == PETTA_PLAN_DYNAMIC_CALL;
            for (CettaExprIndex index = 0u;
                 index < node->child_count; index++) {
                if (node->children[index]
                        .contains_cardinality_call) {
                    node->contains_cardinality_call = true;
                }
                if (node->children[index].contains_call) {
                    descendant_contains_call = true;
                    node->contains_call = true;
                }
                if (node->children[index]
                        .contains_deferred_occurrence_transport) {
                    node->contains_deferred_occurrence_transport = true;
                }
            }
            if (node->plain_scalar_tree && node->child_count > 0u) {
                uint64_t operations = 1u;
                for (CettaExprIndex index = 1u;
                     index < node->child_count; index++) {
                    const PettaPlanNode *child = &node->children[index];
                    if (!child->plain_scalar_tree) {
                        node->plain_scalar_tree = false;
                        operations = 0u;
                        break;
                    }
                    uint32_t child_operations =
                        child->plain_scalar_tree_operations;
                    operations += child_operations;
                    if (operations > UINT32_MAX) {
                        node->plain_scalar_tree = false;
                        operations = 0u;
                        break;
                    }
                }
                node->plain_scalar_tree_operations =
                    node->plain_scalar_tree
                        ? (uint32_t)operations : 0u;
            }
            if (node->execution ==
                    PETTA_PLAN_EXEC_RELATION_SLOTS &&
                descendant_contains_call &&
                !node->relation_head_admitted) {
                node->execution = PETTA_PLAN_EXEC_GENERIC;
            }
            continue;
        }
        if (!cetta_expr_len_fits_size(
                node->child_count) ||
            (size_t)node->child_count >
                SIZE_MAX - work_len - 1u ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + 1u +
                    (size_t)node->child_count,
                sizeof(*work))) {
            ok = false;
            break;
        }
        work[work_len++] = (PettaPlanFeatureItem){
            .plan = node,
            .expanded = true,
        };
        for (CettaExprIndex index = node->child_count;
             index > 0u; index--) {
            work[work_len++] = (PettaPlanFeatureItem){
                .plan = (PettaPlanNode *)
                    &node->children[index - 1u],
            };
        }
    }
    free(work);
    return ok;
}

/* Compile one validated scalar subtree to a source-shape table plus postfix
 * instructions.  Runtime source atoms are supplied separately, so this
 * artifact contains no answer and no equation-instance shortcut. */
static const PettaDeterministicRegionProgram *
petta_plan_compile_scalar_region(
        PettaProgram *program, Atom *root_source,
        PettaPlanNode *root_plan) {
    if (!program || !root_source || !root_plan ||
        !root_plan->plain_scalar_tree ||
        root_plan->plain_scalar_tree_operations == 0u) {
        return NULL;
    }

    PettaScalarRegionSourceNode *nodes = NULL;
    size_t node_len = 0u;
    size_t node_cap = 0u;
    if (!petta_program_reserve(
            (void **)&nodes, &node_cap, 1u, sizeof(*nodes))) {
        return NULL;
    }
    nodes[node_len++] = (PettaScalarRegionSourceNode){
        .source = root_source,
        .plan = root_plan,
    };
    bool valid = true;
    for (size_t cursor = 0u; valid && cursor < node_len; cursor++) {
        PettaScalarRegionSourceNode *node = &nodes[cursor];
        Atom *source = node->source;
        PettaPlanNode *plan = node->plan;
        if (!source || !plan || !plan->plain_scalar_tree) {
            valid = false;
            break;
        }
        if (plan->role == PETTA_PLAN_VALUE) {
            valid = source->kind == ATOM_VAR ||
                (source->kind == ATOM_GROUNDED &&
                 (source->ground.gkind == GV_INT ||
                  source->ground.gkind == GV_FLOAT ||
                  source->ground.gkind == GV_BOOL));
            continue;
        }
        if (plan->role != PETTA_PLAN_STATIC_CALL ||
            plan->execution != PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS ||
            cetta_control_operation_is_control_form(plan->computation.control) ||
            !plan->contains_call || source->kind != ATOM_EXPR ||
            (source->expr.len != 2u && source->expr.len != 3u) ||
            plan->child_count != source->expr.len ||
            !source->expr.elems[0] ||
            source->expr.elems[0]->kind != ATOM_SYMBOL ||
            !cetta_expr_len_fits_size(source->expr.len) ||
            (size_t)source->expr.len - 1u > SIZE_MAX - node_len ||
            !petta_program_reserve(
                (void **)&nodes, &node_cap,
                node_len + (size_t)source->expr.len - 1u,
                sizeof(*nodes))) {
            valid = false;
            break;
        }
        /* The operator head is validated by APPLY; only argument subtrees
         * are scalar dataflow inputs. */
        size_t argument_count = (size_t)source->expr.len - 1u;
        node = &nodes[cursor];
        node->first_child = node_len;
        node->child_count = argument_count;
        for (size_t argument = 0u;
             argument < argument_count; argument++) {
            nodes[node_len++] = (PettaScalarRegionSourceNode){
                .source = source->expr.elems[argument + 1u],
                .plan = (PettaPlanNode *)petta_plan_child(
                    plan, (CettaExprIndex)argument + 1u),
            };
        }
    }

    PettaRegionScalarInstruction *instructions = NULL;
    size_t instruction_len = 0u;
    size_t instruction_cap = 0u;
    PettaScalarRegionTraversal *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    uint32_t operation_count = 0u;
    size_t stack_height = 0u;
    size_t maximum_stack = 0u;
    if (valid) {
        valid = petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work));
    }
    if (valid) {
        work[work_len++] = (PettaScalarRegionTraversal){
            .source_node = 0u,
        };
    }
    while (valid && work_len > 0u) {
        PettaScalarRegionTraversal item = work[--work_len];
        if (item.source_node >= node_len) {
            valid = false;
            break;
        }
        PettaScalarRegionSourceNode *node = &nodes[item.source_node];
        if (!item.expanded && node->child_count > 0u) {
            size_t needed = work_len + 1u + node->child_count;
            if (!petta_program_reserve(
                    (void **)&work, &work_cap,
                    needed, sizeof(*work))) {
                valid = false;
                break;
            }
            work[work_len++] = (PettaScalarRegionTraversal){
                .source_node = item.source_node,
                .expanded = true,
            };
            for (size_t child = node->child_count;
                 child > 0u; child--) {
                work[work_len++] = (PettaScalarRegionTraversal){
                    .source_node =
                        node->first_child + child - 1u,
                };
            }
            continue;
        }
        if (!petta_program_reserve(
                (void **)&instructions, &instruction_cap,
                instruction_len + 1u, sizeof(*instructions))) {
            valid = false;
            break;
        }
        if (node->child_count == 0u) {
            if (stack_height == SIZE_MAX) {
                valid = false;
                break;
            }
            stack_height++;
            if (stack_height > maximum_stack)
                maximum_stack = stack_height;
            instructions[instruction_len++] =
                (PettaRegionScalarInstruction){
                    .kind = PETTA_REGION_SCALAR_LOAD,
                    .source_node = item.source_node,
                };
            continue;
        }
        if (stack_height < node->child_count ||
            operation_count == UINT32_MAX) {
            valid = false;
            break;
        }
        stack_height = stack_height - node->child_count + 1u;
        operation_count++;
        instructions[instruction_len++] =
            (PettaRegionScalarInstruction){
                .kind = PETTA_REGION_SCALAR_APPLY,
                .source_node = item.source_node,
                .argument_count = (uint8_t)node->child_count,
            };
    }

    const PettaDeterministicRegionProgram *result = NULL;
    if (valid && stack_height == 1u &&
        operation_count == root_plan->plain_scalar_tree_operations) {
        if (node_len > SIZE_MAX / sizeof(PettaRegionScalarShapeNode) ||
            instruction_len >
                SIZE_MAX / sizeof(PettaRegionScalarInstruction)) {
            valid = false;
        }
    }
    if (valid && stack_height == 1u &&
        operation_count == root_plan->plain_scalar_tree_operations) {
        bool stable_source =
            term_universe_atom_is_stable(root_source);
        PettaDeterministicRegionProgram *program_out = arena_alloc(
            &program->plans, sizeof(*program_out));
        PettaRegionScalarShapeNode *nodes_out = arena_alloc(
            &program->plans, sizeof(*nodes_out) * node_len);
        PettaRegionScalarInstruction *instructions_out = arena_alloc(
            &program->plans,
            sizeof(*instructions_out) * instruction_len);
        if (program_out && nodes_out && instructions_out) {
            for (size_t index = 0u; index < node_len; index++) {
                nodes_out[index] = (PettaRegionScalarShapeNode){
                    .source_plan = nodes[index].plan,
                    .stable_source = stable_source
                        ? nodes[index].source : NULL,
                    .first_child = nodes[index].first_child,
                    .child_count = nodes[index].child_count,
                };
            }
            memcpy(
                instructions_out, instructions,
                sizeof(*instructions_out) * instruction_len);
            *program_out = (PettaDeterministicRegionProgram){
                .root_plan = root_plan,
                .source_node_count = node_len,
                .instruction_count = instruction_len,
                .operation_count = operation_count,
                .maximum_stack = maximum_stack,
                .source_nodes = nodes_out,
                .instructions = instructions_out,
            };
            result = program_out;
        }
    }
    free(nodes);
    free(instructions);
    free(work);
    return result;
}

/* Attach one compact program to every maximal admitted scalar region.  This
 * pass never recognizes relation names or result values.  Failure leaves a
 * NULL program and therefore the complete generic evaluator. */
static void petta_plan_compile_deterministic_regions(
        PettaProgram *program, Atom *root_source,
        PettaPlanNode *root_plan) {
    if (!program || !root_source || !root_plan)
        return;
    PettaScalarRegionRootItem *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return;
    }
    work[work_len++] = (PettaScalarRegionRootItem){
        .source = root_source,
        .plan = root_plan,
    };
    while (work_len > 0u) {
        PettaScalarRegionRootItem item = work[--work_len];
        Atom *source = item.source;
        PettaPlanNode *plan = item.plan;
        if (!source || !plan)
            continue;
        bool is_scalar_region = plan->plain_scalar_tree &&
            plan->plain_scalar_tree_operations > 0u;
        if (is_scalar_region && !item.parent_is_scalar_region) {
            plan->deterministic_region =
                petta_plan_compile_scalar_region(
                    program, source, plan);
            continue;
        }
        if (source->kind != ATOM_EXPR ||
            plan->child_count != source->expr.len ||
            !cetta_expr_len_fits_size(source->expr.len) ||
            (size_t)source->expr.len > SIZE_MAX - work_len ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + (size_t)source->expr.len,
                sizeof(*work))) {
            continue;
        }
        for (CettaExprIndex index = source->expr.len;
             index > 0u; index--) {
            CettaExprIndex child = index - 1u;
            work[work_len++] = (PettaScalarRegionRootItem){
                .source = source->expr.elems[child],
                .plan = (PettaPlanNode *)petta_plan_child(plan, child),
                .parent_is_scalar_region = is_scalar_region,
            };
        }
    }
    free(work);
}

static PettaRegionHoleProgram *
petta_plan_compile_boolean_region_hole_program(
        PettaProgram *program, Atom *source, PettaPlanNode *plan) {
    if (!program || !source || !plan || source->kind != ATOM_EXPR ||
        source->expr.len != 4u ||
        plan->child_count != source->expr.len ||
        plan->computation.control.operation != CETTA_CONTROL_BRANCH) {
        return NULL;
    }
    CettaExprIndex entry_child = (CettaExprIndex)plan->computation.control.source + 1u;
    CettaExprIndex true_child = (CettaExprIndex)plan->computation.control.first + 1u;
    CettaExprIndex false_child = (CettaExprIndex)plan->computation.control.last + 1u;
    const PettaPlanNode *entry = petta_plan_child(plan, entry_child);
    const PettaPlanNode *when_true = petta_plan_child(plan, true_child);
    const PettaPlanNode *when_false = petta_plan_child(plan, false_child);
    if (!entry || !entry->deterministic_region ||
        !when_true || !when_false) {
        return NULL;
    }
    PettaRegionHoleProgram *program_out = arena_alloc(
        &program->plans, sizeof(*program_out));
    PettaRegionHoleBranch *branches_out = arena_alloc(
        &program->plans, sizeof(*branches_out) * 2u);
    if (!program_out || !branches_out)
        return NULL;
    /* Boolean false/true are branch indices 0/1. */
    branches_out[0] = (PettaRegionHoleBranch){
        .source_child = false_child,
        .plan = when_false,
    };
    branches_out[1] = (PettaRegionHoleBranch){
        .source_child = true_child,
        .plan = when_true,
    };
    *program_out = (PettaRegionHoleProgram){
        .root_plan = plan,
        .stable_source = source,
        .kind = PETTA_REGION_HOLE_BOOLEAN_BRANCH,
        .as.boolean_branch = {
            .entry_region = entry->deterministic_region,
            .entry_source_child = entry_child,
            .branch_count = 2u,
            .branches = branches_out,
        },
    };
    return program_out;
}

static PettaRegionHoleProgram *
petta_plan_compile_binding_region_hole_program(
        PettaProgram *program, Atom *source, PettaPlanNode *plan) {
    if (!program || !source || !plan || source->kind != ATOM_EXPR ||
        source->expr.len != 3u ||
        plan->child_count != source->expr.len ||
        plan->computation.control.operation != CETTA_CONTROL_SEQUENCE_MANY) {
        return NULL;
    }
    Atom *bindings_source = source->expr.elems[1];
    const PettaPlanNode *bindings_plan = petta_plan_child(plan, 1u);
    const PettaPlanNode *body_plan = petta_plan_child(plan, 2u);
    if (!bindings_source || bindings_source->kind != ATOM_EXPR ||
        !bindings_plan ||
        bindings_plan->child_count != bindings_source->expr.len ||
        !body_plan ||
        !cetta_expr_len_fits_size(bindings_source->expr.len) ||
        (size_t)bindings_source->expr.len >
            SIZE_MAX / sizeof(PettaRegionHoleBinding)) {
        return NULL;
    }
    size_t binding_count = (size_t)bindings_source->expr.len;
    PettaRegionHoleBinding *bindings_out = binding_count == 0u
        ? NULL
        : arena_alloc(
              &program->plans, sizeof(*bindings_out) * binding_count);
    if (binding_count != 0u && !bindings_out)
        return NULL;
    for (CettaExprIndex index = 0u;
         index < bindings_source->expr.len; index++) {
        Atom *binding_source = bindings_source->expr.elems[index];
        const PettaPlanNode *binding_plan =
            petta_plan_child(bindings_plan, index);
        if (!binding_source || binding_source->kind != ATOM_EXPR ||
            binding_source->expr.len != 2u || !binding_plan ||
            binding_plan->child_count != binding_source->expr.len) {
            return NULL;
        }
        const PettaPlanNode *pattern_plan =
            petta_plan_child(binding_plan, 0u);
        const PettaPlanNode *producer_plan =
            petta_plan_child(binding_plan, 1u);
        if (!pattern_plan || !producer_plan)
            return NULL;
        bindings_out[index] = (PettaRegionHoleBinding){
            .binding_index = index,
            .pattern_child = 0u,
            .producer_child = 1u,
            .binding_plan = binding_plan,
            .pattern_plan = pattern_plan,
            .producer_plan = producer_plan,
        };
    }
    PettaRegionHoleProgram *program_out = arena_alloc(
        &program->plans, sizeof(*program_out));
    if (!program_out)
        return NULL;
    *program_out = (PettaRegionHoleProgram){
        .root_plan = plan,
        .stable_source = source,
        .kind = PETTA_REGION_HOLE_BINDING_SEQUENCE,
        .as.binding_sequence = {
            .bindings_source_child = 1u,
            .body_source_child = 2u,
            .bindings_plan = bindings_plan,
            .body_plan = body_plan,
            .binding_count = binding_count,
            .bindings = bindings_out,
        },
    };
    return program_out;
}

/* Compile concrete cards from source syntax into the common alternating
 * Region/Hole representation.  No relation name, result value, or workload
 * identity participates in admission. */
static void petta_plan_compile_region_hole_programs(
        PettaProgram *program, Atom *root_source,
        PettaPlanNode *root_plan) {
    if (!program || !root_source || !root_plan)
        return;
    PettaScalarRegionRootItem *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return;
    }
    work[work_len++] = (PettaScalarRegionRootItem){
        .source = root_source,
        .plan = root_plan,
    };
    while (work_len > 0u) {
        PettaScalarRegionRootItem item = work[--work_len];
        Atom *source = item.source;
        PettaPlanNode *plan = item.plan;
        if (!source || !plan)
            continue;

        if (plan->computation.control.operation == CETTA_CONTROL_BRANCH) {
            plan->region_hole_program =
                petta_plan_compile_boolean_region_hole_program(
                    program, source, plan);
        } else if (plan->computation.control.operation == CETTA_CONTROL_SEQUENCE_MANY) {
            plan->region_hole_program =
                petta_plan_compile_binding_region_hole_program(
                    program, source, plan);
        }

        if (source->kind != ATOM_EXPR ||
            plan->child_count != source->expr.len ||
            !cetta_expr_len_fits_size(source->expr.len) ||
            (size_t)source->expr.len > SIZE_MAX - work_len ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + (size_t)source->expr.len,
                sizeof(*work))) {
            continue;
        }
        for (CettaExprIndex index = source->expr.len;
             index > 0u; index--) {
            CettaExprIndex child = index - 1u;
            work[work_len++] = (PettaScalarRegionRootItem){
                .source = source->expr.elems[child],
                .plan = (PettaPlanNode *)petta_plan_child(plan, child),
            };
        }
    }
    free(work);
}

static bool petta_plan_mark_open_template_admitted(
    const PettaPlanNode *root) {
    if (!root)
        return true;
    PettaPlanNode **work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return false;
    }
    work[work_len++] = (PettaPlanNode *)root;
    bool ok = true;
    while (work_len > 0u && ok) {
        PettaPlanNode *node = work[--work_len];
        if (!node || !cetta_expr_len_fits_size(node->child_count) ||
            (size_t)node->child_count > SIZE_MAX - work_len ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + (size_t)node->child_count,
                sizeof(*work))) {
            ok = false;
            break;
        }
        node->open_template_admitted = true;
        for (CettaExprIndex index = 0u;
             index < node->child_count; index++) {
            work[work_len++] =
                (PettaPlanNode *)&node->children[index];
        }
    }
    free(work);
    return ok;
}

/* Push an expression's children as work items with fresh plan nodes; the
 * child at `handler_child`, when nonzero, is dispatched under the handler.
 * The child at `quoted_child`, when nonzero, is a value left unread, and the
 * child at `direct_call_child`, when nonzero, is a direct call. */
static bool petta_plan_push_children(
    Arena *plans, PettaPlanBuildItem **work, size_t *work_len,
    size_t *work_cap, Atom *atom, PettaPlanNode *node, bool value,
    CettaExprIndex handler_child, CettaExprIndex quoted_child,
    CettaExprIndex direct_call_child) {
    if (atom->expr.len == 0u)
        return true;
    if (!cetta_expr_len_mul_fits_size(
            atom->expr.len, sizeof(*node->children)) ||
        !petta_program_reserve(
            (void **)work, work_cap,
            *work_len + (size_t)atom->expr.len,
            sizeof(**work))) {
        return false;
    }
    PettaPlanNode *children = arena_alloc(
        plans,
        sizeof(*children) * (size_t)atom->expr.len);
    if (!children)
        return false;
    memset(
        children, 0,
        sizeof(*children) * (size_t)atom->expr.len);
    if (handler_child != 0u && handler_child < atom->expr.len)
        children[handler_child].dispatch_handler = true;
    node->children = children;
    for (CettaExprIndex index = atom->expr.len;
         index > 0u; index--) {
        CettaExprIndex child = index - 1u;
        if (quoted_child != 0u && child == quoted_child) {
            children[child].role = PETTA_PLAN_VALUE;
            children[child].computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
            children[child].output = PETTA_PLAN_OUTPUT_VALUE;
            continue;
        }
        (*work)[(*work_len)++] = (PettaPlanBuildItem){
            .atom = atom->expr.elems[child],
            .plan = &children[child],
            .value = value,
            .direct_call = direct_call_child != 0u &&
                child == direct_call_child,
        };
    }
    return true;
}

/* The plan of `root`.  A runtime translation (`translation`) leaves the
 * argument of `(quote X)` as the value it is, unread, as PeTTa's translator
 * does: a quoted argument is data, and a value may share its subterms, so
 * reading it could take time exponential in its size. */
/* Whether `atom` is one of the values a derived equation holds: a partial
 * application the specializer substituted for a parameter. */
static bool petta_plan_held_value(
    const Atom *atom, Atom *const *held, size_t held_len) {
    if (held_len == 0u || !petta_semantics_partial_view(atom, NULL, NULL))
        return false;
    for (size_t index = 0u; index < held_len; index++) {
        if (atom_eq((Atom *)atom, held[index]))
            return true;
    }
    return false;
}

static bool petta_translation_is_value(
    PettaProgram *program, const PettaCallabilityDomain *callability,
    const PettaTranslationEnvironment *environment, Atom *root);
static bool petta_translation_value_cached(
    PettaProgram *program, const PettaTranslationEnvironment *environment,
    Atom *atom);
static void petta_translation_value_keep(
    PettaProgram *program, const PettaTranslationEnvironment *environment,
    Atom *atom);

static bool petta_plan_assign_source_positions(PettaPlanNode *root) {
    PettaPlanNode **work = NULL;
    size_t length = 0u, capacity = 0u;
    uint32_t position = 0u;
    if (!petta_program_reserve((void **)&work, &capacity, 1u, sizeof(*work)))
        return false;
    work[length++] = root;
    while (length) {
        PettaPlanNode *node = work[--length];
        if (position == UINT32_MAX || node->child_count > SIZE_MAX - length ||
            !petta_program_reserve((void **)&work, &capacity,
                length + (size_t)node->child_count, sizeof(*work))) {
            free(work);
            return false;
        }
        node->source_position = ++position;
        node->source_debit = 1u;
        for (CettaExprIndex index = node->child_count; index; index--)
            work[length++] = (PettaPlanNode *)&node->children[index - 1u];
    }
    free(work);
    return true;
}

static PettaPlanNode *petta_plan_build_in(
    PettaProgram *program, Arena *plans,
    const PettaCallabilityDomain *callability, Atom *root,
    bool compile_regions, bool translation,
    const PettaTranslationEnvironment *environment,
    Atom *const *held, size_t held_len) {
    if (!program || !plans || !root)
        return NULL;
    PettaPlanNode *plan =
        arena_alloc(plans, sizeof(*plan));
    if (!plan)
        return NULL;
    memset(plan, 0, sizeof(*plan));

    PettaPlanBuildItem *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return NULL;
    }
    work[work_len++] = (PettaPlanBuildItem){
        .atom = root,
        .plan = plan,
    };
    bool ok = true;
    while (work_len > 0u && ok) {
        PettaPlanBuildItem item = work[--work_len];
        Atom *atom = item.atom;
        PettaPlanNode *node = item.plan;
        if (!atom || !node) {
            ok = false;
            break;
        }
        node->literal_case_default = atom->kind == ATOM_EXPR &&
            atom->expr.len == 2u && atom_is_symbol_id(
                atom->expr.elems[0], g_builtin_syms.empty);
        if (translation && environment &&
            petta_translation_value_cached(program, environment, atom)) {
            node->role = PETTA_PLAN_VALUE;
            node->computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
            node->output = PETTA_PLAN_OUTPUT_VALUE;
            continue;
        }
        if (atom->kind != ATOM_EXPR) {
            node->role = PETTA_PLAN_VALUE;
            node->computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
            node->output = PETTA_PLAN_OUTPUT_VALUE;
            node->plain_scalar_tree = atom->kind == ATOM_VAR ||
                (atom->kind == ATOM_GROUNDED &&
                 (atom->ground.gkind == GV_INT ||
                  atom->ground.gkind == GV_FLOAT ||
                  atom->ground.gkind == GV_BOOL));
            continue;
        }
        node->child_count = atom->expr.len;
        /* A list is a value: reading or passing one never evaluates its
         * elements.  Its occurrences keep plans, all values, so equation
         * variables inside it still get their slots.  So is a compiled
         * lambda or closure, and a value a derived equation holds. */
        if (item.value || atom_is_list_form(atom) ||
            atom_is_prime_own_list(atom) ||
            petta_semantics_is_canonical_closure(atom) ||
            petta_plan_held_value(atom, held, held_len)) {
            node->role = PETTA_PLAN_VALUE;
            node->computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
            node->output = PETTA_PLAN_OUTPUT_VALUE;
            if (!petta_plan_push_children(
                    plans, &work, &work_len, &work_cap, atom, node,
                    true, 0u, 0u, 0u)) {
                ok = false;
                break;
            }
            continue;
        }
        if (atom->expr.len == 0u) {
            node->role = PETTA_PLAN_DATA;
            node->computation.control = CETTA_CONTROL_CONSTRUCT_PLAN;
            node->output = PETTA_PLAN_OUTPUT_VALUE;
            continue;
        }
        Atom *head_atom = atom->expr.elems[0];
        if (head_atom->kind == ATOM_SYMBOL) {
            SymbolId head = atom_symbol_current_id(head_atom);
            PeTTaForm form = petta_semantics_form(head);
            if (form == PETTA_FORM_VALUE_LET)
                form = PETTA_FORM_LET;
            node->contains_cardinality_call =
                form == PETTA_FORM_LENGTH ||
                head == g_builtin_syms.size_atom ||
                head == g_builtin_syms.size;
            node->contains_deferred_occurrence_transport =
                petta_head_transports_source_occurrences(head);
            node->computation = petta_semantics_elaborate_computation(
                head, form, atom->expr.len - 1u);
            node->plain_scalar_tree =
                grounded_is_plain_scalar_tree_operator(
                    head_atom, atom->expr.len - 1u);
            node->continuation =
                cetta_control_operation_binding_layout(node->computation.control, 1u) &&
                        atom->expr.len == 4u &&
                        petta_plan_source_is_anonymous_variable(
                            atom->expr.elems[1])
                    ? PETTA_PLAN_CONTINUATION_AFTER_ANONYMOUS_HOLE
                    : PETTA_PLAN_CONTINUATION_GENERIC;
            bool constructor_slot_frame =
                atom->expr.len > 1u &&
                (head == g_builtin_syms.colon ||
                 head == g_builtin_syms.arrow);
            node->relation_head_admitted =
                petta_callability_admits(callability, head);
            bool host_intrinsic = program->is_host_intrinsic &&
                program->is_host_intrinsic(head);
            /* A name the engine provides is applied at any arity, as the
             * reference compiles every registered name: a call at one of
             * its arities, and otherwise a partial application or an
             * over-application, which dispatch decides
             * (RegisteredArity.reference). */
            node->role = constructor_slot_frame
                ? PETTA_PLAN_DATA
                : item.direct_call || host_intrinsic ||
                  petta_program_head_is_intrinsic(head) ||
                  node->relation_head_admitted ||
                  cetta_petta_head_names_extension(head)
                      ? PETTA_PLAN_STATIC_CALL
                      : PETTA_PLAN_DATA;
            node->execution = constructor_slot_frame
                ? PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS
                : host_intrinsic
                    ? PETTA_PLAN_EXEC_GENERIC
                : petta_semantics_grounded_type_pure(head)
                    ? PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS
                    : node->role == PETTA_PLAN_STATIC_CALL &&
                              form == PETTA_FORM_NONE
                        ? PETTA_PLAN_EXEC_RELATION_SLOTS
                        : PETTA_PLAN_EXEC_GENERIC;
        } else {
            node->role = PETTA_PLAN_DYNAMIC_CALL;
            /* Translation already knows a literal closed lambda's callable
             * identity, so its written invocation is a direct call. A head
             * obtained through a variable/partial or another computation
             * retains the implicit run-time-dispatch handler. */
            node->dispatch_handler =
                !petta_semantics_lambda_body(head_atom, NULL) &&
                !petta_semantics_nullary_lambda_body(head_atom, NULL);
        }

        if (node->computation.control.operation == CETTA_CONTROL_NONE) {
            switch (node->role) {
            case PETTA_PLAN_VALUE:
                node->computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
                break;
            case PETTA_PLAN_DATA:
                node->computation.control = CETTA_CONTROL_CONSTRUCT_PLAN;
                break;
            case PETTA_PLAN_STATIC_CALL:
            case PETTA_PLAN_DYNAMIC_CALL:
                node->computation.control = CETTA_CONTROL_INVOKE_PLAN;
                break;
            }
        }
        if (node->role != PETTA_PLAN_STATIC_CALL)
            node->computation.invocation = (CettaInvocationPlan){0};
        if (node->computation.control.operation == CETTA_CONTROL_CONSTRUCT) {
            node->output = PETTA_PLAN_OUTPUT_CONSTRUCTOR;
        } else if (head_atom->kind == ATOM_SYMBOL) {
            SymbolId head = atom_symbol_current_id(head_atom);
            PeTTaForm form = petta_semantics_form(head);
            if (form == PETTA_FORM_VALUE_LET)
                form = PETTA_FORM_LET;
            CettaExprLen nargs = atom_authored_len(atom) - 1u;
            if (head == g_builtin_syms.quote &&
                (nargs == 1u || atom_is_quotation(atom))) {
                node->output = PETTA_PLAN_OUTPUT_QUOTED_CHILD;
                node->output_child = 1u;
            } else if (node->computation.control.operation == CETTA_CONTROL_SCOPED_COMMIT) {
                node->output = PETTA_PLAN_OUTPUT_TRUE;
            } else {
                CettaExprIndex child = cetta_control_operation_series_result(node->computation.control, nargs);
                if (form == PETTA_FORM_TRACE && nargs == 2u)
                    child = 2u;
                else if ((form == PETTA_FORM_LET ||
                          form == PETTA_FORM_CHAIN) && nargs == 3u)
                    child = 3u;
                else if (head == g_builtin_syms.let_star && nargs == 2u) {
                    Atom *pairs = atom->expr.elems[1];
                    bool valid = pairs && pairs->kind == ATOM_EXPR &&
                        pairs->expr.len > 0u;
                    for (CettaExprIndex index = 0u;
                         valid && index < pairs->expr.len; index++) {
                        Atom *pair = pairs->expr.elems[index];
                        valid = pair && pair->kind == ATOM_EXPR &&
                            pair->expr.len == 2u;
                    }
                    if (valid)
                        child = 2u;
                } else if ((head == g_builtin_syms.once ||
                            head == g_builtin_syms.petta_transaction) &&
                           nargs == 1u) {
                    child = 1u;
                } else if (head == g_builtin_syms.petta_with_mutex &&
                           nargs == 2u) {
                    child = 2u;
                } else if (head == g_builtin_syms.match && nargs == 3u) {
                    child = 3u;
                }
                if (child != 0u) {
                    node->output = PETTA_PLAN_OUTPUT_CHILD;
                    node->output_child = child;
                }
            }
        }

        /* The application written in a `reduce` is dispatched at run
         * time, under the handler, whatever its head. */
        bool reduce_application =
            head_atom->kind == ATOM_SYMBOL && atom->expr.len == 2u &&
            petta_semantics_form(atom_symbol_current_id(head_atom)) == PETTA_FORM_REDUCE;
        /* `call` emits a direct call even if this function will only be
         * installed later. Its arguments still use their own
         * translation-time roles. */
        bool call_application =
            head_atom->kind == ATOM_SYMBOL && atom->expr.len == 2u &&
            petta_semantics_form(atom_symbol_current_id(head_atom)) == PETTA_FORM_CALL;
        bool quoted = translation &&
            node->output == PETTA_PLAN_OUTPUT_QUOTED_CHILD;
        if (!petta_plan_push_children(
                plans, &work, &work_len, &work_cap, atom, node, false,
                reduce_application ? 1u : 0u,
                quoted ? node->output_child : 0u,
                call_application ? 1u : 0u)) {
            ok = false;
            break;
        }
    }
    free(work);
    if (!ok || !petta_plan_finish_features(plan) ||
        !petta_plan_assign_source_positions(plan))
        return NULL;
    if (compile_regions) {
        petta_plan_compile_deterministic_regions(
            program, root, plan);
        petta_plan_compile_region_hole_programs(
            program, root, plan);
    }
    return plan;
}

static const PettaPlanNode *petta_plan_build(
    PettaProgram *program,
    const PettaCallabilityDomain *callability, Atom *root) {
    return program
        ? petta_plan_build_in(
              program, &program->plans, callability, root, true, false, NULL,
              NULL, 0u)
        : NULL;
}

/* A subtree without a call is a value where a runtime translation reads it:
 * nothing in it runs, as SWI-PeTTa's translation leaves call-free data as it
 * is.  One postorder pass records positive certificates for every inspected
 * call-free subtree, then replaces each with one VALUE place.  A value held
 * only by its parent (for example quote's argument) is never certified for a
 * different context. */
static PettaPlanNode petta_plan_value_projection(const PettaPlanNode *source) {
    return (PettaPlanNode){
        .role = PETTA_PLAN_VALUE,
        .computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN,
        .output = PETTA_PLAN_OUTPUT_VALUE,
        .literal_case_default = source && source->literal_case_default,
        .source_position = source ? source->source_position : 0u,
        .source_debit = source ? source->source_debit : 0u,
    };
}

static bool petta_plan_collapse_values(
        PettaProgram *program, Atom *root_source, PettaPlanNode *root,
        const PettaTranslationEnvironment *environment) {
    typedef struct {
        Atom *source;
        PettaPlanNode *plan;
        bool finish;
    } Item;
    Item *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return false;
    }
    work[work_len++] = (Item){
        .source = root_source,
        .plan = root,
    };
    bool ok = true;
    while (work_len > 0u) {
        Item item = work[--work_len];
        Atom *source = item.source;
        PettaPlanNode *node = item.plan;
        if (!source || !node) {
            ok = false;
            break;
        }
        if (item.finish) {
            petta_translation_value_keep(program, environment, source);
            *node = petta_plan_value_projection(node);
            continue;
        }
        if (node->role == PETTA_PLAN_VALUE) {
            if (source->kind == ATOM_EXPR &&
                (atom_is_list_form(source) ||
                 petta_semantics_is_canonical_closure(source))) {
                petta_translation_value_keep(program, environment, source);
            }
            *node = petta_plan_value_projection(node);
            continue;
        }
        if (source->kind != ATOM_EXPR ||
            node->child_count != source->expr.len) {
            continue;
        }
        bool value = !node->contains_call &&
            !node->contains_cardinality_call &&
            !node->contains_deferred_occurrence_transport;
        size_t extra = (size_t)node->child_count + (value ? 1u : 0u);
        if (extra > SIZE_MAX - work_len ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + extra,
                sizeof(*work))) {
            ok = false;
            break;
        }
        if (value) {
            work[work_len++] = (Item){
                .source = source,
                .plan = node,
                .finish = true,
            };
        }
        for (CettaExprIndex index = node->child_count;
             index > 0u; index--) {
            CettaExprIndex child = index - 1u;
            work[work_len++] = (Item){
                .source = source->expr.elems[child],
                .plan = (PettaPlanNode *)&node->children[child],
            };
        }
    }
    free(work);
    return ok;
}

/* Every field of a plan node other than its children, as words.  Content
 * equality of plans compares these, so a new field belongs here. */
enum { PETTA_PLAN_NODE_KEY_WORDS = 12 };

static void petta_plan_node_key(
    const PettaPlanNode *node,
    uint64_t key[PETTA_PLAN_NODE_KEY_WORDS]) {
    key[0] = (uint64_t)node->role |
             (uint64_t)cetta_invocation_plan_key(node->computation.invocation) << 32u;
    key[1] = (uint64_t)node->execution;
    key[2] = cetta_control_operation_key(node->computation.control);
    key[3] = (uint64_t)node->continuation;
    key[4] = (uint64_t)node->output;
    key[5] = (uint64_t)node->output_child;
    key[6] = (uint64_t)node->contains_cardinality_call |
             (uint64_t)node->contains_call << 1 |
             (uint64_t)node->contains_deferred_occurrence_transport << 2 |
             (uint64_t)node->plain_scalar_tree << 3 |
             (uint64_t)node->relation_head_admitted << 4 |
             (uint64_t)node->open_template_admitted << 5 |
             (uint64_t)node->has_equation_variable_slot << 6 |
             (uint64_t)node->dispatch_handler << 7 |
             (uint64_t)node->literal_case_default << 8 |
             (uint64_t)node->plain_scalar_tree_operations << 32;
    key[7] = (uint64_t)node->equation_variable_slot;
    key[8] = (uint64_t)(uintptr_t)node->deterministic_region;
    key[9] = (uint64_t)(uintptr_t)node->region_hole_program;
    key[10] = (uint64_t)node->child_count;
    key[11] = (uint64_t)node->source_position |
        (uint64_t)node->source_debit << 32u;
}

static bool petta_plan_content_hash(
    const PettaPlanNode *root, uint64_t *hash_out) {
    const PettaPlanNode **work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return false;
    }
    work[work_len++] = root;
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    bool ok = true;
    while (work_len > 0u) {
        const PettaPlanNode *node = work[--work_len];
        uint64_t key[PETTA_PLAN_NODE_KEY_WORDS];
        petta_plan_node_key(node, key);
        for (size_t word = 0u; word < PETTA_PLAN_NODE_KEY_WORDS; word++) {
            hash ^= key[word];
            hash *= UINT64_C(0x100000001b3);
            hash ^= hash >> 29;
        }
        if ((size_t)node->child_count > SIZE_MAX - work_len ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + (size_t)node->child_count,
                sizeof(*work))) {
            ok = false;
            break;
        }
        for (CettaExprIndex index = node->child_count; index > 0u; index--)
            work[work_len++] = &node->children[index - 1u];
    }
    free(work);
    *hash_out = hash;
    return ok;
}

typedef struct {
    const PettaPlanNode *left;
    const PettaPlanNode *right;
} PettaPlanNodePair;

/* 1 when the plans have the same content, 0 when not, -1 when the
 * comparison could not be made. */
static int petta_plan_content_equal(
    const PettaPlanNode *left, const PettaPlanNode *right) {
    PettaPlanNodePair *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return -1;
    }
    work[work_len++] = (PettaPlanNodePair){left, right};
    int equal = 1;
    while (work_len > 0u) {
        PettaPlanNodePair pair = work[--work_len];
        uint64_t left_key[PETTA_PLAN_NODE_KEY_WORDS];
        uint64_t right_key[PETTA_PLAN_NODE_KEY_WORDS];
        petta_plan_node_key(pair.left, left_key);
        petta_plan_node_key(pair.right, right_key);
        if (memcmp(left_key, right_key, sizeof(left_key)) != 0) {
            equal = 0;
            break;
        }
        if ((size_t)pair.left->child_count > SIZE_MAX - work_len ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + (size_t)pair.left->child_count,
                sizeof(*work))) {
            equal = -1;
            break;
        }
        for (CettaExprIndex index = 0u;
             index < pair.left->child_count; index++) {
            work[work_len++] = (PettaPlanNodePair){
                &pair.left->children[index],
                &pair.right->children[index],
            };
        }
    }
    free(work);
    return equal;
}

static const PettaPlanNode *petta_plan_copy_into(
    Arena *plans, const PettaPlanNode *source, bool retain_compiled) {
    if (!plans || !source) return NULL;
    PettaPlanNode *root = arena_alloc(plans, sizeof(*root));
    if (!root)
        return NULL;
    *root = *source;
    PettaPlanNode **work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    if (!petta_program_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return NULL;
    }
    work[work_len++] = root;
    bool ok = true;
    while (work_len > 0u) {
        PettaPlanNode *node = work[--work_len];
        if (!retain_compiled) {
            node->deterministic_region = NULL;
            node->region_hole_program = NULL;
        }
        if (node->child_count == 0u) {
            node->children = NULL;
            continue;
        }
        if (!cetta_expr_len_mul_fits_size(
                node->child_count, sizeof(*node->children)) ||
            (size_t)node->child_count > SIZE_MAX - work_len ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + (size_t)node->child_count,
                sizeof(*work))) {
            ok = false;
            break;
        }
        PettaPlanNode *children = arena_alloc(
            plans, sizeof(*children) * (size_t)node->child_count);
        if (!children) {
            ok = false;
            break;
        }
        memcpy(children, node->children,
               sizeof(*children) * (size_t)node->child_count);
        node->children = children;
        for (CettaExprIndex index = 0u;
             index < node->child_count; index++) {
            work[work_len++] = &children[index];
        }
    }
    free(work);
    return ok ? root : NULL;
}

const PettaPlanNode *petta_plan_copy_semantics(Arena *owner, const PettaPlanNode *source) {
    return petta_plan_copy_into(owner, source, false);
}

/* Semantic comparison retains the ordinary full plan key except for the
 * two source-pointer-specific realizations erased by copy_semantics. */
static void petta_plan_semantic_key(
    const PettaPlanNode *node, uint64_t key[PETTA_PLAN_NODE_KEY_WORDS]) {
    petta_plan_node_key(node, key);
    key[8] = 0u;
    key[9] = 0u;
}

typedef struct {
    const PettaPlanNode *left;
    const PettaPlanNode *right;
    const Atom *left_syntax;
    const Atom *right_syntax;
} PettaPlanSemanticPair;

static bool petta_plan_comparison_reserve(
    PettaPlanSemanticPair **work, size_t length, size_t *capacity,
    size_t needed, const CettaCheckpointCompare *read) {
    if (needed <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 16u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(**work))
        return false;
    size_t bytes = next * sizeof(**work);
    if (!read->service(read->context, "program.plan.allocate", 0u, bytes))
        return false;
    PettaPlanSemanticPair *replacement = malloc(bytes);
    if (!replacement) {
        read->service_release(read->context, bytes);
        return false;
    }
    for (size_t i = 0u; i < length; i++) {
        if (!read->service(read->context, "program.plan.stack-copy", 1u, 0u)) {
            free(replacement);
            read->service_release(read->context, bytes);
            return false;
        }
        replacement[i] = (*work)[i];
    }
    size_t old_bytes = *capacity * sizeof(**work);
    free(*work);
    if (old_bytes)
        read->service_release(read->context, old_bytes);
    *work = replacement;
    *capacity = next;
    return true;
}

/* Derived planning syntax is not an authored-row copy-map edge. Compare the
 * observations used by callability refresh, under the held symbol environment.
 * VALUE positions are opaque here even when their plan retains child metadata. */
static int petta_plan_callability_compare(
    PettaPlanSemanticPair pair, const CettaCheckpointCompare *read,
    bool *descend) {
    *descend = false;
    if (!pair.left_syntax && !pair.right_syntax)
        return 1;
    if (!pair.left_syntax || !pair.right_syntax)
        return 0;
    if (pair.left->role == PETTA_PLAN_VALUE)
        return 1;
    bool a_expr = pair.left_syntax->kind == ATOM_EXPR;
    bool b_expr = pair.right_syntax->kind == ATOM_EXPR;
    if (!read->scalar(read->context, "program.plan.refresh.expression", a_expr, b_expr))
        return -1;
    if (a_expr != b_expr)
        return 0;
    if (!a_expr)
        return 1;
    CettaExprLen a_len = pair.left_syntax->expr.len;
    CettaExprLen b_len = pair.right_syntax->expr.len;
    if (!read->scalar(read->context, "program.plan.refresh.arity", a_len, b_len))
        return -1;
    if (a_len != b_len || a_len != pair.left->child_count ||
        b_len != pair.right->child_count)
        return 0;
    const Atom *a_head = a_len ? pair.left_syntax->expr.elems[0] : NULL;
    const Atom *b_head = b_len ? pair.right_syntax->expr.elems[0] : NULL;
    if (a_len && (!a_head || !b_head))
        return 0;
    bool a_symbol = a_head && a_head->kind == ATOM_SYMBOL;
    bool b_symbol = b_head && b_head->kind == ATOM_SYMBOL;
    if (!read->scalar(read->context, "program.plan.refresh.symbol", a_symbol, b_symbol))
        return -1;
    if (a_symbol != b_symbol)
        return 0;
    if (a_symbol) {
        SymbolId a_id = atom_symbol_current_id(a_head);
        SymbolId b_id = atom_symbol_current_id(b_head);
        if (!read->scalar(read->context, "program.plan.refresh.head", a_id, b_id))
            return -1;
        if (a_id != b_id)
            return 0;
        if (a_id == SYMBOL_ID_NONE) {
            uint32_t a_bytes = atom_symbol_length(a_head);
            uint32_t b_bytes = atom_symbol_length(b_head);
            if (!read->scalar(read->context, "program.plan.refresh.head.length", a_bytes, b_bytes))
                return -1;
            if (a_bytes != b_bytes)
                return 0;
            const unsigned char *a = (const unsigned char *)atom_symbol_bytes(a_head);
            const unsigned char *b = (const unsigned char *)atom_symbol_bytes(b_head);
            if (a_bytes && (!a || !b))
                return 0;
            for (uint32_t i = 0u; i < a_bytes; i++) {
                if (!read->scalar(read->context, "program.plan.refresh.head.byte", a[i], b[i]))
                    return -1;
                if (a[i] != b[i])
                    return 0;
            }
        }
    }
    *descend = true;
    return 1;
}

/* 1 agrees, 0 differs, -1 could not finish. Both plan owners remain held;
 * temporary stack reservations are released after their buffers are freed. */
static int petta_plan_semantics_compare(
    const PettaPlanNode *left, const PettaPlanNode *right,
    const Atom *left_syntax, const Atom *right_syntax,
    const CettaCheckpointCompare *read) {
    PettaPlanSemanticPair *work = NULL;
    size_t length = 0u, capacity = 0u;
    int result = -1;
    if (!petta_plan_comparison_reserve(&work, 0u, &capacity, 1u, read))
        return -1;
    work[length++] = (PettaPlanSemanticPair){left, right, left_syntax, right_syntax};
    while (length) {
        if (!read->service(read->context, "program.plan.node", 1u, 0u))
            goto done;
        PettaPlanSemanticPair pair = work[--length];
        if (!pair.left || !pair.right) {
            result = 0;
            goto done;
        }
        uint64_t a[PETTA_PLAN_NODE_KEY_WORDS], b[PETTA_PLAN_NODE_KEY_WORDS];
        petta_plan_semantic_key(pair.left, a);
        petta_plan_semantic_key(pair.right, b);
        for (size_t word = 0u; word < PETTA_PLAN_NODE_KEY_WORDS; word++) {
            if (!read->scalar(read->context, "program.plan.key", a[word], b[word]))
                goto done;
            if (a[word] != b[word]) {
                result = 0;
                goto done;
            }
        }
        bool descend = false;
        int syntax_equal = petta_plan_callability_compare(pair, read, &descend);
        if (syntax_equal != 1) {
            result = syntax_equal;
            goto done;
        }
        CettaExprLen count = pair.left->child_count;
        if ((count && (!pair.left->children || !pair.right->children)) ||
            count > SIZE_MAX - length) {
            result = 0;
            goto done;
        }
        if (!petta_plan_comparison_reserve(
                &work, length, &capacity, length + (size_t)count, read))
            goto done;
        for (CettaExprIndex child = count; child > 0u; child--) {
            if (!read->service(read->context, "program.plan.child", 1u, 0u))
                goto done;
            work[length++] = (PettaPlanSemanticPair){
                &pair.left->children[child - 1u],
                &pair.right->children[child - 1u],
                descend ? pair.left_syntax->expr.elems[child - 1u] : NULL,
                descend ? pair.right_syntax->expr.elems[child - 1u] : NULL};
        }
    }
    result = 1;
done:
    free(work);
    read->service_release(read->context, capacity * sizeof(*work));
    return result;
}

const PettaPlanNode *petta_plan_plain_snapshot(
    Arena *owner, const PettaPlanNode *source,
    PettaPlanSnapshotEntry *cache, size_t *cache_len, size_t cache_capacity,
    size_t node_limit) {
    if (!owner || !source || !cache || !cache_len ||
        *cache_len > cache_capacity || node_limit == 0u)
        return NULL;
    const PettaPlanNode **work = NULL;
    size_t len = 0u, cap = 0u, nodes = 0u;
    bool ok = petta_program_reserve((void **)&work, &cap, 1u, sizeof(*work));
    if (ok)
        work[len++] = source;
    while (ok && len) {
        const PettaPlanNode *node = work[--len];
        ok = nodes++ < node_limit && !node->deterministic_region &&
            !node->region_hole_program &&
            (node->child_count == 0u || node->children) &&
            node->child_count <= SIZE_MAX - len &&
            petta_program_reserve((void **)&work, &cap,
                len + node->child_count, sizeof(*work));
        for (CettaExprIndex i = 0u; ok && i < node->child_count; i++)
            work[len++] = &node->children[i];
    }
    free(work);
    uint64_t hash = 0u;
    if (!ok || !petta_plan_content_hash(source, &hash))
        return NULL;
    for (size_t i = 0u; i < *cache_len; i++) {
        if (cache[i].hash != hash)
            continue;
        int equal = petta_plan_content_equal(cache[i].plan, source);
        if (equal < 0)
            return NULL;
        if (equal)
            return cache[i].plan;
    }
    if (*cache_len == cache_capacity)
        return NULL;
    ArenaMark mark = arena_mark(owner);
    const PettaPlanNode *copy = petta_plan_copy_into(owner, source, true);
    if (!copy) {
        arena_reset(owner, mark);
        return NULL;
    }
    cache[(*cache_len)++] = (PettaPlanSnapshotEntry){hash, copy};
    return copy;
}

static bool petta_program_grow_interned_plans(PettaProgram *program) {
    size_t capacity = program->interned_plan_cap
        ? program->interned_plan_cap * 2u : 64u;
    if (capacity < program->interned_plan_cap ||
        capacity > SIZE_MAX / sizeof(PettaPlanInternEntry))
        return false;
    PettaPlanInternEntry *table =
        calloc(capacity, sizeof(*table));
    if (!table)
        return false;
    for (size_t index = 0u;
         index < program->interned_plan_cap; index++) {
        PettaPlanInternEntry entry = program->interned_plans[index];
        if (!entry.plan)
            continue;
        size_t slot = (size_t)entry.hash & (capacity - 1u);
        while (table[slot].plan)
            slot = (slot + 1u) & (capacity - 1u);
        table[slot] = entry;
    }
    free(program->interned_plans);
    program->interned_plans = table;
    program->interned_plan_cap = capacity;
    return true;
}

static const PettaPlanNode *petta_program_intern_plan(
    PettaProgram *program, const PettaPlanNode *built, bool shared_children) {
    uint64_t hash = 0u;
    if (!petta_plan_content_hash(built, &hash))
        return NULL;
    if ((program->interned_plan_len + 1u) * 2u >
            program->interned_plan_cap &&
        !petta_program_grow_interned_plans(program)) {
        return NULL;
    }
    size_t mask = program->interned_plan_cap - 1u;
    size_t slot = (size_t)hash & mask;
    while (program->interned_plans[slot].plan) {
        const PettaPlanInternEntry *entry =
            &program->interned_plans[slot];
        if (entry->hash == hash) {
            int equal = petta_plan_content_equal(entry->plan, built);
            if (equal < 0)
                return NULL;
            if (equal)
                return entry->plan;
        }
        slot = (slot + 1u) & mask;
    }
    const PettaPlanNode *copy = NULL;
    if (shared_children) {
        /* The normalized children already belong to this immutable pool.
         * Copy their descriptors, retaining their shared descendants. */
        PettaPlanNode *root = arena_alloc(&program->plans, sizeof(*root));
        PettaPlanNode *children = built->child_count ? arena_alloc(
            &program->plans, sizeof(*children) * (size_t)built->child_count) : NULL;
        if (root && (!built->child_count || children)) {
            *root = *built;
            if (built->child_count)
                memcpy(children, built->children,
                    sizeof(*children) * (size_t)built->child_count);
            root->children = children;
            copy = root;
        }
    } else {
        copy = petta_plan_copy_into(&program->plans, built, true);
    }
    if (!copy)
        return NULL;
    program->interned_plans[slot] = (PettaPlanInternEntry){
        .hash = hash,
        .plan = copy,
    };
    program->interned_plan_len++;
    return copy;
}

__attribute__((cold, noinline)) const PettaPlanNode *petta_program_project_computation(
        PettaProgram *program, const PettaPlanNode *source,
        CettaComputationPlan computation, bool dynamic, bool relation_head_admitted,
        const PettaPlanNode *const *children, CettaExprLen child_count) {
    if (!program || (child_count && !children) ||
        !cetta_expr_len_mul_fits_size(child_count, sizeof(PettaPlanNode)))
        return NULL;
    CettaControlOperation operation = computation.control.operation;
    if (operation != CETTA_CONTROL_RETURN_VALUE && operation != CETTA_CONTROL_CONSTRUCT &&
        operation != CETTA_CONTROL_INVOKE && operation != CETTA_CONTROL_SEQUENCE &&
        operation != CETTA_CONTROL_BRANCH && operation != CETTA_CONTROL_FAILURE)
        return NULL;
    PettaPlanNode node = {
        .role = operation == CETTA_CONTROL_RETURN_VALUE ? PETTA_PLAN_VALUE
            : operation == CETTA_CONTROL_CONSTRUCT ? PETTA_PLAN_DATA
            : dynamic ? PETTA_PLAN_DYNAMIC_CALL : PETTA_PLAN_STATIC_CALL,
        .computation = computation,
        .source_position = source ? source->source_position : 0u,
        .source_debit = source ? source->source_debit : 0u,
        .relation_head_admitted = relation_head_admitted,
        .open_template_admitted = source && source->open_template_admitted,
        .dispatch_handler = dynamic || (source && source->dispatch_handler),
        .child_count = child_count,
    };
    if (operation == CETTA_CONTROL_RETURN_VALUE) {
        if (child_count)
            return NULL;
        node = petta_plan_value_projection(&node);
    } else if (operation == CETTA_CONTROL_CONSTRUCT) {
        /* Completed fields contain only value ports. Keep the existing
         * runtime translation's one value place, rather than re-entering
         * a constructor and visiting every field on each answer. */
        bool ready = true;
        for (CettaExprIndex index = 0u; index < child_count; index++) {
            if (!children[index])
                return NULL;
            ready &= children[index]->role == PETTA_PLAN_VALUE;
        }
        if (ready) {
            node = petta_plan_value_projection(&node);
            return petta_program_intern_plan(program, &node, true);
        }
        node.output = child_count ? PETTA_PLAN_OUTPUT_CONSTRUCTOR : PETTA_PLAN_OUTPUT_VALUE;
        if (source && source->role == PETTA_PLAN_DATA)
            node.execution = source->execution;
    } else {
        node.contains_call = true;
        if (operation == CETTA_CONTROL_SEQUENCE) {
            if (child_count != 4u ||
                !cetta_control_operation_binding_layout(computation.control, 1u))
                return NULL;
            node.output = PETTA_PLAN_OUTPUT_CHILD;
            node.output_child = 3u;
        } else if (operation == CETTA_CONTROL_INVOKE) {
            /* Surviving calls retain observation/occurrence obligations.
             * Completed ports erase them through the value projection. */
            node.contains_cardinality_call = source && source->contains_cardinality_call;
            node.contains_deferred_occurrence_transport = source && source->contains_deferred_occurrence_transport;
            node.execution = relation_head_admitted ? PETTA_PLAN_EXEC_RELATION_SLOTS
                : source && source->role == PETTA_PLAN_STATIC_CALL ? source->execution
                : PETTA_PLAN_EXEC_GENERIC;
        }
    }
    PettaPlanNode *items = child_count ? malloc(sizeof(*items) * (size_t)child_count) : NULL;
    if (child_count && !items)
        return NULL;
    bool ok = true;
    for (CettaExprIndex index = 0u; index < child_count; index++) {
        if (!children[index]) { ok = false; break; }
        items[index] = *children[index];
        node.contains_call |= items[index].contains_call;
        node.contains_cardinality_call |= items[index].contains_cardinality_call;
        node.contains_deferred_occurrence_transport |= items[index].contains_deferred_occurrence_transport;
    }
    node.children = items;
    const PettaPlanNode *projected = ok ? petta_program_intern_plan(program, &node, true) : NULL;
    free(items);
    return projected;
}

/* Renaming variables does not change control or callability facts.  Rebuild
 * positional code against the compiled syntax so no program retains a source
 * pointer from the authored representation. */
static const PettaPlanNode *petta_plan_rebind_frame_syntax(
        PettaProgram *program, Atom *syntax, const PettaPlanNode *source) {
    if (!program || !syntax || !source)
        return NULL;
    PettaPlanNode *root = arena_alloc(&program->plans, sizeof(*root));
    *root = *source;
    PettaPlanBuildItem *work = NULL;
    size_t len = 0u, cap = 0u;
    if (!petta_program_reserve((void **)&work, &cap, 1u, sizeof(*work)))
        return NULL;
    work[len++] = (PettaPlanBuildItem){.atom = syntax, .plan = root};
    bool ok = true;
    while (len && ok) {
        PettaPlanBuildItem item = work[--len];
        PettaPlanNode *node = item.plan;
        node->deterministic_region = NULL;
        node->region_hole_program = NULL;
        node->has_equation_variable_slot = false;
        node->equation_variable_slot = 0u;
        node->open_template_admitted = false;
        if (node->role == PETTA_PLAN_VALUE && node->child_count == 0u) {
            node->children = NULL;
            continue;
        }
        if (item.atom->kind != ATOM_EXPR) {
            ok = node->child_count == 0u;
            node->children = NULL;
            continue;
        }
        CettaExprLen count = item.atom->expr.len;
        if (count != node->child_count ||
            !cetta_expr_len_mul_fits_size(count, sizeof(*node->children)) ||
            (size_t)count > SIZE_MAX - len ||
            !petta_program_reserve((void **)&work, &cap,
                                  len + (size_t)count, sizeof(*work))) {
            ok = false;
            break;
        }
        PettaPlanNode *children = count ? arena_alloc(
            &program->plans, sizeof(*children) * (size_t)count) : NULL;
        if (count)
            memcpy(children, node->children, sizeof(*children) * (size_t)count);
        node->children = children;
        for (CettaExprIndex child = 0u; child < count; child++)
            work[len++] = (PettaPlanBuildItem){
                .atom = item.atom->expr.elems[child], .plan = &children[child],
            };
    }
    free(work);
    if (!ok)
        return NULL;
    petta_plan_compile_deterministic_regions(program, syntax, root);
    petta_plan_compile_region_hole_programs(program, syntax, root);
    return root;
}

static bool petta_program_space_is_live(const PettaProgramSpace *space) {
    return space->space &&
           space->instance_id == space_instance_id(space->space);
}

/* The spaces read, their equation tokens and the predeclared generation
   still describe the cached domain. */
static bool petta_program_callability_cache_current(
    const PettaProgram *program) {
    if (petta_program_has_private_images(program) ||
        !program->callability_cache_valid ||
        program->callability_cache_predeclared_generation !=
            program->predeclared_generation ||
        program->callability_cache_token_len != program->space_len) {
        return false;
    }
    for (size_t index = 0u; index < program->space_len; index++) {
        const PettaProgramSpace *space = &program->spaces[index];
        const PettaCallabilityScan *scan =
            &program->callability_cache_tokens[index];
        if (!petta_program_space_is_live(space)) {
            if (scan->equations.space)
                return false;
            continue;
        }
        if (!space_equation_token_matches_live_space(
                scan->equations, space->space)) {
            return false;
        }
    }
    return true;
}

static bool petta_callability_copy(
    PettaCallabilityDomain *out, const PettaCallabilityDomain *source) {
    out->admits_any_head = source->admits_any_head;
    out->named_len = 0u;
    if (source->named_len == 0u)
        return true;
    if (!petta_program_reserve(
            (void **)&out->named_heads, &out->named_cap,
            source->named_len, sizeof(*out->named_heads))) {
        return false;
    }
    memcpy(out->named_heads, source->named_heads,
           sizeof(*out->named_heads) * source->named_len);
    out->named_len = source->named_len;
    return true;
}

/* Add the heads of the equations among rows [from, to) of one space. */
static bool petta_program_scan_callability_rows(
    const PettaProgram *program, const Space *space,
    CettaCount from, CettaCount to,
    PettaCallabilityDomain *callability) {
    for (CettaIndex atom_index = from; atom_index < to; atom_index++) {
        /* Only a row shaped as a rule names a head; a fact is not read. */
        if (!space_row_may_be_rule(space, atom_index))
            continue;
        Atom *lhs = NULL;
        SymbolId head = SYMBOL_ID_NONE;
        if (petta_program_equation_view(program,
                space_get_at64(space, atom_index),
                &lhs, NULL, &head)) {
            if (petta_equation_lhs_admits_any_named_head(lhs)) {
                callability->admits_any_head = true;
            } else if (head != SYMBOL_ID_NONE &&
                       !petta_callability_insert_named(
                           callability, head)) {
                return false;
            }
        }
    }
    return true;
}

static PettaCallabilityScan petta_program_callability_scan_of(
    const PettaProgramSpace *space) {
    PettaCallabilityScan scan = {0};
    if (petta_program_space_is_live(space)) {
        scan.equations = space_equation_token(space->space);
        scan.read = space_read_token(space->space);
        scan.scanned_len = space_length64(space->space);
    }
    return scan;
}

/* Bring the cached domain up to date when every space whose equations
 * changed only grew: the rows it scanned are intact, so their heads stand
 * and only the appended rows are read.  False when a space was rewritten,
 * replaced or added; the caller then rescans. */
static bool petta_program_extend_callability(PettaProgram *program) {
    if (petta_program_has_private_images(program) ||
        !program->callability_cache_valid ||
        program->callability_cache_predeclared_generation !=
            program->predeclared_generation ||
        program->callability_cache_token_len != program->space_len) {
        return false;
    }
    for (size_t index = 0u; index < program->space_len; index++) {
        const PettaProgramSpace *space = &program->spaces[index];
        const PettaCallabilityScan *scan =
            &program->callability_cache_tokens[index];
        if (!petta_program_space_is_live(space)) {
            if (scan->equations.space)
                return false;
            continue;
        }
        if (space_equation_token_matches_live_space(
                scan->equations, space->space))
            continue;
        SpaceReadToken now = space_read_token(space->space);
        if (scan->read.space != space->space ||
            !space_read_token_prefix_intact(scan->read) ||
            now.base_dependency_epoch != scan->read.base_dependency_epoch ||
            space_length64(space->space) < scan->scanned_len) {
            return false;
        }
    }
    size_t named_before = program->callability_cache.named_len;
    bool any_before = program->callability_cache.admits_any_head;
    for (size_t index = 0u; index < program->space_len; index++) {
        const PettaProgramSpace *space = &program->spaces[index];
        PettaCallabilityScan *scan =
            &program->callability_cache_tokens[index];
        if (!petta_program_space_is_live(space) ||
            space_equation_token_matches_live_space(
                scan->equations, space->space)) {
            continue;
        }
        if (!petta_program_scan_callability_rows(
                program, space->space, scan->scanned_len,
                space_length64(space->space),
                &program->callability_cache)) {
            program->callability_cache_valid = false;
            return false;
        }
        *scan = petta_program_callability_scan_of(space);
    }
    if (program->callability_cache.named_len != named_before ||
        program->callability_cache.admits_any_head != any_before)
        program->callability_epoch++;
    return true;
}

static bool petta_program_private_space_append(PettaProgram *program,
        Space ***spaces, size_t *len, size_t *cap, Space *source) {
    Space *space = petta_program_private_map(program, source);
    if (!space) return true;
    for (size_t i = 0u; i < *len; i++)
        if ((*spaces)[i] == space) return true;
    if (!petta_program_reserve((void **)spaces, cap, *len + 1u, sizeof(**spaces)))
        return false;
    (*spaces)[(*len)++] = space;
    return true;
}

/* A temporary effective program view, never installed in the shared catalog
 * or callability cache. Include type-only and newly created private Spaces. */
static bool petta_program_private_spaces(PettaProgram *program,
        Space ***out, size_t *length) {
    Space **spaces = NULL;
    size_t len = 0u, cap = 0u;
    for (size_t i = 0u; i < program->space_len; i++) {
        const PettaProgramSpace *entry = &program->spaces[i];
        if (petta_program_space_is_live(entry) &&
            !petta_program_private_space_append(program, &spaces, &len, &cap,
                                               (Space *)entry->space)) goto fail;
    }
    for (size_t i = 0u; program->analysis && i < program->analysis->space_len; i++) {
        const PettaProgramAnalysisSpace *entry = &program->analysis->spaces[i];
        if (entry->space && entry->instance_id == space_instance_id(entry->space) &&
            !petta_program_private_space_append(program, &spaces, &len, &cap,
                                               (Space *)entry->space)) goto fail;
    }
    for (const PettaProgramPrivateScope *scope = petta_private_scope;
         scope; scope = scope->previous) {
        if (scope->program != program || !scope->image_at) continue;
        for (size_t i = 0u;; i++) {
            Space *image = scope->image_at(scope->context, i);
            if (!image) break;
            if (!petta_program_private_space_append(program, &spaces, &len, &cap, image))
                goto fail;
        }
    }
    *out = spaces; *length = len;
    return true;
fail:
    free(spaces);
    return false;
}

static void petta_program_private_spaces_free(Space ***spaces) {
    free(*spaces);
}

static bool petta_program_scan_callability(
    const PettaProgram *program,
    PettaCallabilityDomain *callability) {
    callability->admits_any_head =
        program->predeclared_callability.admits_any_head;
    for (size_t index = 0u;
         index < program->predeclared_callability.named_len;
         index++) {
        if (!petta_callability_insert_named(
                callability,
                program->predeclared_callability
                    .named_heads[index])) {
            return false;
        }
    }
    for (size_t space_index = 0u;
         space_index < program->space_len; space_index++) {
        const PettaProgramSpace *space =
            &program->spaces[space_index];
        if (!petta_program_space_is_live(space))
            continue;
        if (!petta_program_scan_callability_rows(
                program, space->space, 0u,
                space_length64(space->space), callability)) {
            return false;
        }
    }
    return true;
}

/* The caller owns the returned domain's storage.  The scan runs only when
   the program revision it describes has changed. */
static bool petta_program_collect_callability(
    PettaProgram *program,
    PettaCallabilityDomain *callability) {
    if (!program || !callability)
        return false;
    if (petta_program_has_private_images(program)) {
        Space **spaces = NULL;
        size_t count = 0u;
        bool ok = petta_callability_copy(callability, &program->predeclared_callability) &&
                  petta_program_private_spaces(program, &spaces, &count);
        for (size_t i = 0u; ok && i < count; i++)
            ok = petta_program_scan_callability_rows(program, spaces[i], 0u,
                    space_length64(spaces[i]), callability);
        free(spaces);
        return ok;
    }
    if (petta_program_callability_cache_current(program) ||
        petta_program_extend_callability(program))
        return petta_callability_copy(
            callability, &program->callability_cache);
    if (!petta_program_scan_callability(program, callability))
        return false;
    program->callability_cache_valid = false;
    if (!petta_program_reserve(
            (void **)&program->callability_cache_tokens,
            &program->callability_cache_token_cap,
            program->space_len,
            sizeof(*program->callability_cache_tokens)) ||
        !petta_callability_copy(
            &program->callability_cache, callability)) {
        return true;
    }
    for (size_t index = 0u; index < program->space_len; index++)
        program->callability_cache_tokens[index] =
            petta_program_callability_scan_of(&program->spaces[index]);
    program->callability_cache_token_len = program->space_len;
    program->callability_cache_predeclared_generation =
        program->predeclared_generation;
    program->callability_cache_valid = true;
    program->callability_epoch++;
    return true;
}

/* A synchronous reader may borrow the current domain; if caching could not
 * retain it, the caller owns the fallback copy in `scratch`. */
static bool petta_program_callability_view(PettaProgram *program,
        PettaCallabilityDomain *scratch, const PettaCallabilityDomain **view) {
    if (program && scratch && view && petta_program_has_private_images(program)) {
        if (!petta_program_collect_callability(program, scratch)) return false;
        *view = scratch;
        return true;
    }
    if (!program || !scratch || !view ||
        (!petta_program_callability_cache_current(program) &&
         !petta_program_extend_callability(program) &&
         !petta_program_collect_callability(program, scratch)))
        return false;
    *view = program->callability_cache_valid ? &program->callability_cache : scratch;
    return true;
}

static PettaProgramSpace *petta_program_find_space(
    PettaProgram *program, const Space *space) {
    if (!program || !space)
        return NULL;
    PettaTransactionCatalog *private = petta_program_private_catalog(program, space);
    if (private) return &private->catalog;
    uint64_t instance = space_instance_id(space);
    for (size_t index = 0u;
         index < program->space_len; index++) {
        PettaProgramSpace *candidate =
            &program->spaces[index];
        if (candidate->space == space &&
            candidate->instance_id == instance) {
            return candidate;
        }
    }
    return NULL;
}

bool petta_program_revision_view_retain(
        PettaProgramRevisionView *view) {
    if (!view)
        return false;
    uint32_t references = atomic_load_explicit(
        &view->references, memory_order_relaxed);
    for (;;) {
        if (references == 0u || references == UINT32_MAX)
            return false;
        if (atomic_compare_exchange_weak_explicit(
                &view->references, &references, references + 1u,
                memory_order_relaxed, memory_order_relaxed)) {
            return true;
        }
    }
}

static void petta_program_revision_view_destroy(
        PettaProgramRevisionView *view) {
    if (!view)
        return;
    free(view->source_equations);
    petta_program_space_clear_head_index(&view->catalog);
    free(view->catalog.equations);
    free(view);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_AUTHORITY_DESTROY);
}

void petta_program_revision_view_free(
        PettaProgramRevisionView *view) {
    if (!view)
        return;
    uint32_t prior = atomic_fetch_sub_explicit(
        &view->references, 1u, memory_order_acq_rel);
    if (prior == 0u)
        abort();
    if (prior == 1u) {
        atomic_thread_fence(memory_order_acquire);
        petta_program_revision_view_destroy(view);
    }
}

typedef enum {
    PETTA_REVISION_VIEW_INVALIDATE_CATALOG,
    PETTA_REVISION_VIEW_INVALIDATE_SOURCE_KEY,
    PETTA_REVISION_VIEW_INVALIDATE_DISPOSE,
} PettaRevisionViewInvalidationReason;

static void petta_program_space_invalidate_revision_view(
        PettaProgramSpace *entry,
        PettaRevisionViewInvalidationReason reason) {
    if (!entry || !entry->revision_view)
        return;
    PettaProgramRevisionView *retired = entry->revision_view;
    entry->revision_view = NULL;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_AUTHORITY_INVALIDATE);
    switch (reason) {
    case PETTA_REVISION_VIEW_INVALIDATE_CATALOG:
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_AUTHORITY_INVALIDATE_CATALOG);
        break;
    case PETTA_REVISION_VIEW_INVALIDATE_SOURCE_KEY:
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_AUTHORITY_INVALIDATE_SOURCE_KEY);
        break;
    case PETTA_REVISION_VIEW_INVALIDATE_DISPOSE:
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_AUTHORITY_INVALIDATE_DISPOSE);
        break;
    default:
        abort();
    }
    petta_program_revision_view_free(retired);
}

/* One authority owns every authored-equation catalog transition.  The
 * generation is part of the immutable-view key even when the backing Space
 * revision also changes; this keeps internal catalog publication explicit
 * and makes cache invalidation independent of caller protocol. */
static void petta_program_space_commit_catalog_mutation(
        PettaProgramSpace *entry) {
    if (!entry)
        return;
    if (entry->catalog_generation != UINT64_MAX)
        entry->catalog_generation++;
    petta_program_space_invalidate_revision_view(
        entry, PETTA_REVISION_VIEW_INVALIDATE_CATALOG);
    petta_program_space_clear_candidate_snapshots(entry);
}

static void petta_program_space_clear_type_families(PettaProgramSpace *entry) {
    for (size_t i = 0u; i < entry->type_family_len; i++) {
        free(entry->type_families[i].calls);
        free(entry->type_families[i].arities);
    }
    if (entry->type_families_current)
        arena_free(&entry->type_family_storage);
    free(entry->type_families);
    entry->type_families = NULL;
    entry->type_family_len = entry->type_family_cap = 0u;
    entry->type_families_current = false;
}

static void petta_program_space_dispose_catalog(
        PettaProgramSpace *entry) {
    if (!entry)
        return;
    petta_program_space_invalidate_revision_view(
        entry, PETTA_REVISION_VIEW_INVALIDATE_DISPOSE);
    petta_program_space_clear_head_index(entry);
    petta_program_space_clear_candidate_snapshots(entry);
    petta_program_space_clear_type_families(entry);
    free(entry->plan_callability_epochs);
    entry->plan_callability_epochs = NULL;
    entry->plan_callability_epoch_len = entry->plan_callability_epoch_cap = 0u;
    free(entry->equations);
    entry->equations = NULL;
    entry->equation_len = 0u;
    entry->equation_cap = 0u;
}

static size_t petta_program_space_pending_lower_bound(
        const PettaProgramSpace *entry, SymbolId head) {
    size_t low = 0u;
    size_t high = entry->pending_head_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (entry->pending_heads[middle] < head)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static bool petta_program_space_pending(
        const PettaProgramSpace *entry, SymbolId head) {
    size_t index = petta_program_space_pending_lower_bound(entry, head);
    return index < entry->pending_head_len &&
        entry->pending_heads[index] == head;
}

static bool petta_program_space_declare_pending(
        PettaProgramSpace *entry, SymbolId head) {
    (void)symbol_pin(g_symbols, head);
    size_t index = petta_program_space_pending_lower_bound(entry, head);
    if (index < entry->pending_head_len &&
        entry->pending_heads[index] == head)
        return true;
    if (!petta_program_reserve(
            (void **)&entry->pending_heads, &entry->pending_head_cap,
            entry->pending_head_len + 1u, sizeof(*entry->pending_heads)))
        return false;
    memmove(entry->pending_heads + index + 1u, entry->pending_heads + index,
            sizeof(*entry->pending_heads) *
                (entry->pending_head_len - index));
    entry->pending_heads[index] = head;
    entry->pending_head_len++;
    return true;
}

/* An equation with `head` was added to the space or removed from it: SWI-
 * PeTTa's registration of the head no longer waits on it. */
static void petta_program_space_settle_pending(
        PettaProgramSpace *entry, SymbolId head) {
    if (!entry || entry->pending_head_len == 0u)
        return;
    size_t index = petta_program_space_pending_lower_bound(entry, head);
    if (index >= entry->pending_head_len ||
        entry->pending_heads[index] != head)
        return;
    memmove(entry->pending_heads + index, entry->pending_heads + index + 1u,
            sizeof(*entry->pending_heads) *
                (entry->pending_head_len - index - 1u));
    entry->pending_head_len--;
}

static void petta_program_space_free_pending(PettaProgramSpace *entry) {
    free(entry->pending_heads);
    entry->pending_heads = NULL;
    entry->pending_head_len = 0u;
    entry->pending_head_cap = 0u;
}

static bool petta_program_space_reserve_equation(
        PettaProgramSpace *entry) {
    return entry && petta_program_reserve(
        (void **)&entry->equations, &entry->equation_cap,
        entry->equation_len + 1u, sizeof(*entry->equations));
}

static void petta_program_space_append_reserved_equation(
        PettaProgramSpace *entry, PettaProgramEquation equation) {
    if (!entry || entry->equation_len >= entry->equation_cap)
        abort();
#if !CETTA_MANAGED_SYMBOLS
    atom_pin_syntax_symbols(equation.equation);
#endif
    /* Only the callable head is a numeric catalogue key. Body values remain
     * owned by their syntax/plan arenas; registering them would turn retained
     * data into process-lifetime vocabulary. Known nested calls already have
     * identities, while unknown spelled heads can observe later registration. */
    (void)symbol_pin(g_symbols, equation.head);
    size_t record_index = entry->equation_len++;
    entry->equations[record_index] = equation;
    petta_program_space_settle_pending(entry, equation.head);
    if (!entry->head_index_dirty &&
        !petta_program_space_head_index_append(
            entry, equation.head, record_index)) {
        entry->head_index_dirty = true;
    }
    petta_program_space_commit_catalog_mutation(entry);
}

static bool petta_program_space_remove_all_matching_equations(
        PettaProgramSpace *entry, Atom *atom) {
    if (!entry || !atom)
        return false;
    size_t write = 0u;
    for (size_t read = 0u; read < entry->equation_len; read++) {
        if (atom_eq(entry->equations[read].equation, atom))
            continue;
        if (write != read) {
            entry->equations[write] = entry->equations[read];
            if (write < entry->plan_callability_epoch_len)
                entry->plan_callability_epochs[write] =
                    read < entry->plan_callability_epoch_len
                        ? entry->plan_callability_epochs[read] : 0u;
        }
        write++;
    }
    if (write == entry->equation_len)
        return false;
    entry->equation_len = write;
    if (entry->plan_callability_epoch_len > write)
        entry->plan_callability_epoch_len = write;
    entry->head_index_dirty = true;
    petta_program_space_commit_catalog_mutation(entry);
    return true;
}

static void petta_program_space_remove_equation_at(
        PettaProgramSpace *entry, size_t remove) {
    if (!entry || remove >= entry->equation_len)
        abort();
    memmove(
        entry->equations + remove,
        entry->equations + remove + 1u,
        sizeof(*entry->equations) *
            (entry->equation_len - remove - 1u));
    if (remove < entry->plan_callability_epoch_len) {
        memmove(entry->plan_callability_epochs + remove,
                entry->plan_callability_epochs + remove + 1u,
                sizeof(*entry->plan_callability_epochs) *
                    (entry->plan_callability_epoch_len - remove - 1u));
        entry->plan_callability_epoch_len--;
    }
    entry->equation_len--;
    entry->head_index_dirty = true;
    petta_program_space_commit_catalog_mutation(entry);
}

static void petta_program_space_replace_catalog(
        PettaProgramSpace *entry, PettaProgramEquation *equations,
        size_t count) {
    if (!entry)
        abort();
    petta_program_space_clear_head_index(entry);
    free(entry->equations);
    entry->plan_callability_epoch_len = 0u;
    entry->equations = equations;
    entry->equation_len = count;
    entry->equation_cap = count;
    entry->head_index_dirty = true;
    petta_program_space_commit_catalog_mutation(entry);
}

static bool petta_program_refresh_callability(PettaProgram *program,
    PettaProgramSpace *entry, SymbolId head);

PettaProgramRevisionView *petta_program_revision_view_capture(
        PettaProgram *program, Space *source) {
    CETTA_SCOPED_SHARED_TRANSITION(program_view_capture);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_ATTEMPT);
    PettaProgramSpace *entry =
        petta_program_find_space(program, source);
    if (!entry || !source ||
        !petta_program_refresh_callability(program, entry, SYMBOL_ID_NONE)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_DECLINE);
        return NULL;
    }

    bool callability_certified = !program->refresh_callability ||
        (program->callability_epoch != 0u &&
         petta_program_callability_cache_current(program));

    if (entry->revision_view &&
        entry->revision_view->catalog_generation ==
            entry->catalog_generation &&
        entry->revision_view->admitted_rule_forms == petta_program_rule_forms(program) &&
        entry->revision_view->graded_equations == program->graded_equations &&
        entry->revision_view->callability_certified == callability_certified &&
        (!program->refresh_callability ||
         entry->revision_view->callability_epoch == program->callability_epoch) &&
        space_program_token_matches_live_space(
            entry->revision_view->source_program_token, source) &&
        space_equation_token_matches_live_space(
            entry->revision_view->source_equation_token, source)) {
        if (!petta_program_revision_view_retain(
                entry->revision_view)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_DECLINE);
            return NULL;
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_AUTHORITY_REUSE);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_COMMIT);
        return entry->revision_view;
    }
    petta_program_space_invalidate_revision_view(
        entry, PETTA_REVISION_VIEW_INVALIDATE_SOURCE_KEY);

    PettaProgramRevisionView *view = calloc(1u, sizeof(*view));
    if (!view) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_DECLINE);
        return NULL;
    }
    atomic_init(&view->references, 1u);
    view->admitted_rule_forms = petta_program_rule_forms(program);
    view->source = space_read_token(source);
    view->source_equation_token = space_equation_token(source);
    view->source_program_token = space_program_token(source);
    view->catalog_generation = entry->catalog_generation;
    view->callability_epoch = program->callability_epoch;
    view->callability_certified = callability_certified;
    view->graded_equations = program->graded_equations;
    view->catalog = (PettaProgramSpace){
        .space = source,
        .instance_id = view->source.instance_id,
        .synchronized_revision = entry->synchronized_revision,
        .synchronized_program = entry->synchronized_program,
        .synchronized_snapshot = entry->synchronized_snapshot,
        .synchronized_prefix_epoch = entry->synchronized_prefix_epoch,
        .synchronized_atom_count = entry->synchronized_atom_count,
        .synchronized_equation_len = entry->synchronized_equation_len,
        .synchronized_catalog_generation = entry->synchronized_catalog_generation,
        .head_index_dirty = true,
    };
    if (entry->equation_len > SIZE_MAX / sizeof(*entry->equations)) {
        goto decline;
    }
    if (entry->equation_len > 0u) {
        view->catalog.equations = malloc(
            sizeof(*entry->equations) * entry->equation_len);
        if (!view->catalog.equations)
            goto decline;
        memcpy(
            view->catalog.equations, entry->equations,
            sizeof(*entry->equations) * entry->equation_len);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_AUTHORITY_EQUATION_COPY,
            entry->equation_len);
        /* Plans remain immutable in the owning program arena and may be
           borrowed when this view is rebound to its identical source Space.
           Templates are tied more deeply to the source atom graph and are
           not needed by that narrow consumer.  Alpha-equivalent targets use
           the payload-erased equation lease below: they supply no code
           morphism that could make source plan pointers target-relative. */
        for (size_t index = 0u; index < entry->equation_len; index++) {
            view->catalog.equations[index].planning_syntax = NULL;
            view->catalog.equations[index].equation_template_c0 = NULL;
            view->catalog.equations[index].equation_template = NULL;
            view->catalog.equations[index].static_variable_count = 0u;
        }
        view->catalog.equation_len = entry->equation_len;
        view->catalog.equation_cap = entry->equation_len;
    }
    if (!petta_program_space_rebuild_head_index(
            &view->catalog)) {
        goto decline;
    }

    CettaCount source_len = space_length64(source);
    for (CettaIndex index = 0u; index < source_len; index++) {
        Atom *atom = space_get_at64(source, index);
        if (!atom || !petta_rule_syntax_view(atom, view->admitted_rule_forms, view->graded_equations, NULL, NULL, NULL))
            continue;
        if (!petta_program_reserve(
                (void **)&view->source_equations, &view->source_equation_capacity,
                view->source_equation_len + 1u,
                sizeof(*view->source_equations))) {
            goto decline;
        }
        view->source_equations[view->source_equation_len++] = atom;
    }
    if (!space_read_token_matches_live_space(view->source, source) ||
        !space_program_token_matches_live_space(view->source_program_token, source) ||
        !space_equation_token_matches_live_space(
            view->source_equation_token, source))
        goto decline;

    if (!petta_program_revision_view_retain(view))
        goto decline;
    entry->revision_view = view;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_AUTHORITY_BUILD);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_COMMIT);
    return view;

decline:
    petta_program_revision_view_free(view);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_DECLINE);
    return NULL;
}

bool petta_program_revision_view_bind(
        const PettaProgramRevisionView *view, Space *target,
        PettaProgramRevisionProjection *projection) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_BIND_ATTEMPT);
    if (projection)
        memset(projection, 0, sizeof(*projection));
    if (!view || !target || !projection)
        goto decline;

    SpaceReadToken target_read = space_read_token(target);
    SpaceEquationToken target_equations = space_equation_token(target);
    if (target == view->source.space &&
        target_equations.instance_id ==
            view->source_equation_token.instance_id &&
        target_equations.equation_revision ==
            view->source_equation_token.equation_revision &&
        space_equation_token_matches_live_space(
            target_equations, target)) {
        *projection = (PettaProgramRevisionProjection){
            .view = view,
            .target = target_equations,
        };
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_BIND_IDENTITY_COMMIT);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_BIND_COMMIT);
        return true;
    }
    size_t equation_index = 0u;
    CettaCount target_len = space_length64(target);
    for (CettaIndex index = 0u; index < target_len; index++) {
        Atom *atom = space_get_at64(target, index);
        if (!atom || !petta_rule_syntax_view(atom, view->admitted_rule_forms, view->graded_equations, NULL, NULL, NULL))
            continue;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_BIND_ALPHA_COMPARE);
        if (equation_index >= view->source_equation_len ||
            !atom_alpha_eq(
                view->source_equations[equation_index], atom)) {
            goto decline;
        }
        equation_index++;
    }
    if (equation_index != view->source_equation_len ||
        !space_read_token_matches_live_space(target_read, target) ||
        !space_equation_token_matches_live_space(
            target_equations, target)) {
        goto decline;
    }

    *projection = (PettaProgramRevisionProjection){
        .view = view,
        .target = target_equations,
    };
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_BIND_COMMIT);
    return true;

decline:
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_BIND_DECLINE);
    return false;
}

bool petta_program_revision_projection_current(
        const PettaProgramRevisionProjection *projection,
        const Space *target) {
    return projection && projection->view && target &&
        space_equation_token_matches_live_space(
            projection->target, target);
}

bool petta_program_revision_projection_is_source(
        const PettaProgramRevisionProjection *projection, const Space *target) {
    return petta_program_revision_projection_targets_source(projection, target) &&
        petta_program_revision_projection_current(projection, target) &&
        projection->target.equation_revision == projection->view->source_equation_token.equation_revision;
}

bool petta_program_revision_projection_targets_source(
        const PettaProgramRevisionProjection *projection, const Space *target) {
    return projection && projection->view && target &&
        target == projection->view->source_equation_token.space &&
        space_instance_id(target) == projection->view->source_equation_token.instance_id &&
        projection->target.space == target &&
        projection->target.instance_id == space_instance_id(target);
}

static bool petta_program_revision_view_source_plans_current_locked(
        PettaProgram *program, Space *source, PettaProgramSpace *entry,
        const PettaProgramRevisionView *view) {
    return program && source && entry && view && entry->revision_view == view &&
        entry->catalog_generation == view->catalog_generation &&
        view->admitted_rule_forms == petta_program_rule_forms(program) &&
        view->graded_equations == program->graded_equations &&
        view->callability_certified &&
        space_program_token_matches_live_space(view->source_program_token, source) &&
        space_equation_token_matches_live_space(view->source_equation_token, source) &&
        (!program->refresh_callability ||
         (petta_program_callability_cache_current(program) &&
          view->callability_epoch == program->callability_epoch));
}

bool petta_program_revision_view_source_plans_current(
        PettaProgram *program, Space *source, const PettaProgramRevisionView *view) {
    CETTA_SCOPED_SHARED_TRANSITION(program_view_source_check);
    return petta_program_revision_view_source_plans_current_locked(
        program, source, petta_program_find_space(program, source), view);
}

static PettaProgramSpace *petta_program_ensure_space(
    PettaProgram *program, Space *space) {
    PettaProgramSpace *found =
        petta_program_find_space(program, space);
    if (found)
        return found;
    if (!program || !space ||
        !petta_program_reserve(
            (void **)&program->spaces, &program->space_cap,
            program->space_len + 1u,
            sizeof(*program->spaces))) {
        return NULL;
    }
    PettaProgramSpace *created =
        &program->spaces[program->space_len++];
    memset(created, 0, sizeof(*created));
    created->space = space;
    created->instance_id = space_instance_id(space);
    return created;
}

static bool petta_program_type_family_build(
    Space *space, Atom *head, Arena *storage, PettaProgramTypeFamily *family) {
    memset(family, 0, sizeof(*family));
    family->head = atom_symbol_current_id(head);
    ArenaMark mark = arena_mark(storage);
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(space, storage, head, &types);
    family->hold_body = petta_type_body_is_data(types, count);
    count = petta_type_unique_signatures(types, count);
    family->calls = count ? calloc(count, sizeof(*family->calls)) : NULL;
    bool ok = !count || family->calls;
    for (uint32_t i = 0u; ok && i < count; i++) {
        if (!petta_type_call_plan(types[i], 0u, NULL))
            continue;
        ok = petta_type_call_compile(types[i], &family->calls[family->count]);
        if (ok)
            family->count++;
    }
    free(types);
    if (!ok) {
        free(family->calls);
        arena_reset(storage, mark);
    }
    return ok;
}

static bool petta_program_type_family_complete(
    PettaProgram *program, Space *space, PettaProgramTypeFamily *family,
    CettaExprLen supplied, bool retain, bool *complete) {
    size_t low = 0u, high = family->arity_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (family->arities[middle].supplied < supplied)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < family->arity_len && family->arities[low].supplied == supplied) {
        *complete = family->arities[low].complete;
        return true;
    }
    /* Full predicate calls consume every declared domain. Only a genuine
     * partial call may use a longer arrow. This fact shares the family's
     * revision token, including equation changes, so warm calls need no
     * repeated clause traversal. */
    CettaExprLen minimum = 0u, maximum = 0u;
    *complete = false;
    bool source_callable = space_rule_head_arity_bounds(
        space, family->head, &minimum, &maximum, complete, supplied,
        petta_program_rule_forms(program));
    if (!source_callable) {
        for (uint32_t i = 0u; i < family->count; i++)
            *complete = *complete || family->calls[i].arity == supplied;
    }
    if (retain) {
        if (!petta_program_reserve((void **)&family->arities,
                &family->arity_cap, family->arity_len + 1u,
                sizeof(*family->arities)))
            return false;
        memmove(family->arities + low + 1u, family->arities + low,
            (family->arity_len - low) * sizeof(*family->arities));
        family->arities[low] = (PettaProgramTypeArity){
            .supplied = supplied, .complete = *complete,
        };
        family->arity_len++;
    }
    return true;
}

bool petta_program_type_calls(PettaProgram *program, Space *space, Atom *head,
                              CettaExprLen supplied, Arena *arena,
                              PettaTypeCall **calls_out, uint32_t *count_out,
                              bool *hold_body) {
    if (!calls_out || !count_out || !hold_body)
        return false;
    *calls_out = NULL;
    *count_out = 0u;
    *hold_body = false;
    if (!space || !arena || !head || head->kind != ATOM_SYMBOL)
        return false;
    CETTA_SCOPED_SHARED_TRANSITION(type_family_observation);
    SpaceProgramToken token = space_program_token(space);
    PettaProgramSpace *entry = program ? petta_program_ensure_space(program, space) : NULL;
    if (program && !entry)
        return false;
    PettaProgramTypeFamily temporary;
    Arena temporary_storage;
    PettaProgramTypeFamily *family = NULL;
    if (entry) {
        if (!entry->type_families_current ||
            !space_program_token_eq(entry->type_family_program, token)) {
            petta_program_space_clear_type_families(entry);
            arena_init_detached(&entry->type_family_storage);
            entry->type_family_program = token;
            entry->type_families_current = true;
        }
        size_t low = 0u, high = entry->type_family_len;
        while (low < high) {
            size_t middle = low + (high - low) / 2u;
            if (entry->type_families[middle].head < atom_symbol_current_id(head))
                low = middle + 1u;
            else
                high = middle;
        }
        if (low < entry->type_family_len && entry->type_families[low].head == atom_symbol_current_id(head)) {
            family = &entry->type_families[low];
        } else {
            if (!petta_program_reserve((void **)&entry->type_families,
                    &entry->type_family_cap, entry->type_family_len + 1u,
                    sizeof(*entry->type_families)) ||
                !petta_program_type_family_build(space, head,
                    &entry->type_family_storage, &temporary))
                return false;
            memmove(entry->type_families + low + 1u, entry->type_families + low,
                    (entry->type_family_len - low) * sizeof(*entry->type_families));
            entry->type_families[low] = temporary;
            entry->type_family_len++;
            family = &entry->type_families[low];
        }
    } else {
        arena_init_detached(&temporary_storage);
        if (!petta_program_type_family_build(space, head, &temporary_storage, &temporary)) {
            arena_free(&temporary_storage);
            return false;
        }
        family = &temporary;
    }
    bool complete_call = false;
    bool ok = petta_program_type_family_complete(
        program, space, family, supplied, entry != NULL, &complete_call);
    uint32_t count = 0u;
    for (uint32_t i = 0u; ok && i < family->count; i++)
        count += complete_call ? supplied == family->calls[i].arity
                               : supplied <= family->calls[i].arity;
    PettaTypeCall *calls = count ? calloc(count, sizeof(*calls)) : NULL;
    ok = ok && (!count || calls);
    uint32_t next = 0u;
    for (uint32_t i = 0u; ok && i < family->count; i++) {
        if (complete_call ? supplied == family->calls[i].arity
                          : supplied <= family->calls[i].arity)
            ok = petta_type_call_instantiate(arena, &family->calls[i], &calls[next++]);
    }
    ok = ok && space_program_token_matches_live_space(token, space);
    if (ok) {
        *calls_out = calls;
        *count_out = count;
        *hold_body = family->hold_body;
    } else {
        free(calls);
    }
    if (!entry) {
        free(temporary.calls);
        arena_free(&temporary_storage);
    }
    return ok;
}

static PettaProgramAnalysisSpace *petta_program_find_analysis_space(
    PettaProgram *program, const Space *space) {
    if (!program || !program->analysis || !space)
        return NULL;
    PettaTransactionCatalog *private = petta_program_private_catalog(program, space);
    if (private) return &private->analysis;
    uint64_t instance = space_instance_id(space);
    for (size_t index = 0u;
         index < program->analysis->space_len; index++) {
        PettaProgramAnalysisSpace *candidate =
            &program->analysis->spaces[index];
        if (candidate->space == space &&
            candidate->instance_id == instance) {
            return candidate;
        }
    }
    return NULL;
}

static PettaProgramAnalysisSpace *petta_program_ensure_analysis_space(
    PettaProgram *program, Space *space) {
    PettaProgramAnalysisSpace *found =
        petta_program_find_analysis_space(program, space);
    if (found)
        return found;
    if (!program || !program->analysis || !space ||
        !petta_program_reserve(
            (void **)&program->analysis->spaces,
            &program->analysis->space_cap,
            program->analysis->space_len + 1u,
            sizeof(*program->analysis->spaces))) {
        return NULL;
    }
    PettaProgramAnalysisSpace *created =
        &program->analysis->spaces[program->analysis->space_len++];
    memset(created, 0, sizeof(*created));
    created->space = space;
    created->instance_id = space_instance_id(space);
    return created;
}

static void petta_program_private_analysis_clear(PettaProgramAnalysisSpace *entry) {
    petta_program_space_clear_type_index(entry);
    free(entry->inferred_signatures);
    free(entry->type_annotations);
    const Space *space = entry->space;
    uint64_t instance = entry->instance_id;
    *entry = (PettaProgramAnalysisSpace){.space = space, .instance_id = instance};
}

PettaTransactionCatalog *petta_program_transaction_catalog_new(
        PettaProgram *program, const Space *source, Space *image, bool borrow_source) {
    CETTA_SCOPED_SHARED_TRANSITION(private_catalog_capture);
    if (!program || !source || !image) return NULL;
    if (borrow_source && source->native.universe != image->native.universe)
        return NULL;
    PettaTransactionCatalog *result = calloc(1u, sizeof(*result));
    if (!result) return NULL;
    result->catalog.space = result->analysis.space = image;
    result->catalog.instance_id = result->analysis.instance_id = space_instance_id(image);
    result->catalog.head_index_dirty = true;
    result->analysis.type_index_dirty = true;
    const PettaProgramSpace *entry = borrow_source ? petta_program_find_space(program, source) : NULL;
    const PettaProgramAnalysisSpace *analysis = borrow_source
        ? petta_program_find_analysis_space(program, source) : NULL;
    if (entry) {
        if (!petta_program_reserve((void **)&result->catalog.equations,
                &result->catalog.equation_cap, entry->equation_len, sizeof(*entry->equations)) ||
            !petta_program_reserve((void **)&result->catalog.pending_heads,
                &result->catalog.pending_head_cap, entry->pending_head_len, sizeof(*entry->pending_heads)))
            goto fail;
        if (entry->equation_len)
            memcpy(result->catalog.equations, entry->equations,
                   entry->equation_len * sizeof(*entry->equations));
        if (entry->pending_head_len)
            memcpy(result->catalog.pending_heads, entry->pending_heads,
                   entry->pending_head_len * sizeof(*entry->pending_heads));
        result->catalog.equation_len = entry->equation_len;
        result->catalog.pending_head_len = entry->pending_head_len;
    }
    if (analysis) {
        if (!petta_program_reserve((void **)&result->analysis.type_annotations,
                &result->analysis.type_annotation_cap, analysis->type_annotation_len,
                sizeof(*analysis->type_annotations))) goto fail;
        if (analysis->type_annotation_len)
            memcpy(result->analysis.type_annotations, analysis->type_annotations,
                   analysis->type_annotation_len * sizeof(*analysis->type_annotations));
        result->analysis.type_annotation_len = analysis->type_annotation_len;
    }
    result->owner = program;
    result->next = program->private_catalogs;
    if (result->next) result->next->previous = result;
    program->private_catalogs = result;
    return result;
fail:
    petta_program_transaction_catalog_free(result);
    return NULL;
}

void petta_program_transaction_catalog_free(PettaTransactionCatalog *catalog) {
    CETTA_SCOPED_SHARED_TRANSITION(private_catalog_release);
    if (!catalog) return;
    if (catalog->owner) {
        if (catalog->previous) catalog->previous->next = catalog->next;
        else catalog->owner->private_catalogs = catalog->next;
        if (catalog->next) catalog->next->previous = catalog->previous;
    }
    petta_program_space_dispose_catalog(&catalog->catalog);
    petta_program_space_free_pending(&catalog->catalog);
    petta_program_private_analysis_clear(&catalog->analysis);
    free(catalog);
}

bool petta_program_transaction_reserve_publication(PettaProgram *program, size_t count) {
    if (!program) return true;
    if (count > SIZE_MAX - program->space_len ||
        !petta_program_reserve((void **)&program->spaces, &program->space_cap,
            program->space_len + count, sizeof(*program->spaces))) return false;
    return !program->analysis ||
        (count <= SIZE_MAX - program->analysis->space_len &&
         petta_program_reserve((void **)&program->analysis->spaces,
            &program->analysis->space_cap, program->analysis->space_len + count,
            sizeof(*program->analysis->spaces)));
}

void petta_program_transaction_catalog_publish(
        PettaProgram *program, Space *destination, PettaTransactionCatalog *catalog) {
    if (!program || !destination || !catalog) return;
    PettaProgramSpace *target = petta_program_find_space(program, destination);
    if (!target && (catalog->catalog.equation_len || catalog->catalog.pending_head_len ||
                    catalog->analysis.type_annotation_len)) {
        /* Capacity was reserved before semantic publication. */
        assert(program->space_len < program->space_cap);
        target = &program->spaces[program->space_len++];
        *target = (PettaProgramSpace){.space = destination,
                                    .instance_id = space_instance_id(destination)};
    }
    if (target) {
        petta_program_space_free_pending(target);
        petta_program_space_clear_type_families(target);
        petta_program_space_replace_catalog(target, catalog->catalog.equations,
                                            catalog->catalog.equation_len);
        target->equation_cap = catalog->catalog.equation_cap;
        catalog->catalog.equations = NULL;
        catalog->catalog.equation_len = catalog->catalog.equation_cap = 0u;
        target->pending_heads = catalog->catalog.pending_heads;
        target->pending_head_len = catalog->catalog.pending_head_len;
        target->pending_head_cap = catalog->catalog.pending_head_cap;
        catalog->catalog.pending_heads = NULL;
        catalog->catalog.pending_head_len = catalog->catalog.pending_head_cap = 0u;
        target->synchronized_revision = space_revision(destination);
        target->synchronized_program = space_program_token(destination);
        /* Future synchronization preserves these plans and checks any
         * unregistered source rows; do not invent a completeness receipt. */
        target->synchronized_snapshot = false;
    }
    PettaProgramAnalysisSpace *analysis = petta_program_find_analysis_space(program, destination);
    if (!analysis && program->analysis && catalog->analysis.type_annotation_len) {
        assert(program->analysis->space_len < program->analysis->space_cap);
        analysis = &program->analysis->spaces[program->analysis->space_len++];
        *analysis = (PettaProgramAnalysisSpace){.space = destination,
                                              .instance_id = space_instance_id(destination)};
    }
    if (analysis) {
        petta_program_private_analysis_clear(analysis);
        analysis->type_annotations = catalog->analysis.type_annotations;
        analysis->type_annotation_len = catalog->analysis.type_annotation_len;
        analysis->type_annotation_cap = catalog->analysis.type_annotation_cap;
        analysis->type_index_dirty = true;
        catalog->analysis.type_annotations = NULL;
        catalog->analysis.type_annotation_len = catalog->analysis.type_annotation_cap = 0u;
    }
}

PettaProgram *petta_program_new_with_rules_and_host_intrinsics(unsigned admitted_forms,
    bool (*is_host_intrinsic)(SymbolId head)) {
    if (!(admitted_forms & CETTA_RULE_SYNTAX_ORDINARY) ||
        (admitted_forms & ~(CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL)))
        return NULL;
    PettaProgram *program = cetta_malloc(sizeof(*program));
    memset(program, 0, sizeof(*program));
    program->admitted_rule_forms = admitted_forms;
    program->is_host_intrinsic = is_host_intrinsic;
    program->admitted_rule_forms = admitted_forms;
    arena_init(&program->plans);
    arena_set_runtime_kind(
        &program->plans, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&program->plans, NULL);
    arena_init(&program->transient_scratch);
    /* Memo keys belong to the bounded memo. Publishing their fresh variable
     * identities or substituted values in the global hash-cons table would
     * keep them after eviction and make a long-lived evaluator grow. */
    arena_init_detached(&program->transient_plan_atoms);
    /* Bounded source keys usually occupy a small fraction of a worker
     * block; grow the metadata pool only as actual retained keys require. */
    arena_set_block_capacity(&program->transient_plan_atoms, ARENA_BLOCK_SIZE / 16u);
    arena_set_block_growth(&program->transient_plan_atoms, true);
    arena_set_hashcons(&program->transient_scratch, NULL);
    return program;
}

PettaProgram *petta_program_new_with_rules(unsigned admitted_forms) {
    return petta_program_new_with_rules_and_host_intrinsics(admitted_forms, NULL);
}

bool petta_program_enable_callability_refresh(PettaProgram *program) {
    CETTA_SCOPED_SHARED_TRANSITION(program_callability_profile);
    if (!program || program->space_len)
        return false;
    program->refresh_callability = true;
    return true;
}

PettaProgram *petta_program_new(void) {
    return petta_program_new_with_rules(CETTA_RULE_SYNTAX_ORDINARY);
}

PettaProgram *petta_program_new_with_host_intrinsics(
    bool (*is_host_intrinsic)(SymbolId head)) {
    return petta_program_new_with_rules_and_host_intrinsics(
        CETTA_RULE_SYNTAX_ORDINARY, is_host_intrinsic);
}

static void petta_program_analysis_state_free(
    PettaProgramAnalysisState *analysis) {
    if (!analysis)
        return;
    for (size_t index = 0u; index < analysis->space_len; index++) {
        PettaProgramAnalysisSpace *entry = &analysis->spaces[index];
        petta_program_space_clear_type_index(entry);
        free(entry->inferred_signatures);
        free(entry->type_annotations);
    }
    free(analysis->spaces);
    free(analysis);
}

bool petta_program_enable_analysis(PettaProgram *program) {
    CETTA_SCOPED_SHARED_TRANSITION(program_analysis_enable);
    if (!program)
        return false;
    if (program->analysis)
        return true;
    program->analysis = cetta_malloc(sizeof(*program->analysis));
    memset(program->analysis, 0, sizeof(*program->analysis));
    /* Analysis is a replayable view over the live Space.  Enabling it after
     * an ordinary execution therefore reconstructs prior declarations
     * instead of creating a profile-dependent history boundary. */
    for (size_t space_index = 0u;
         space_index < program->space_len; space_index++) {
        const Space *space = program->spaces[space_index].space;
        if (!space || program->spaces[space_index].instance_id !=
                          space_instance_id(space)) {
            continue;
        }
        CettaCount length = space_length64(space);
        for (CettaIndex atom_index = 0u;
             atom_index < length; atom_index++) {
            Atom *atom = space_get_at64(space, atom_index);
            if (!atom || atom->kind != ATOM_EXPR ||
                atom->expr.len != 3u ||
                !atom_is_symbol_id(
                    atom->expr.elems[0], g_builtin_syms.colon) ||
                atom->expr.elems[1]->kind != ATOM_SYMBOL) {
                continue;
            }
            if (!petta_program_note_add(
                    program, (Space *)space, atom, NULL)) {
                petta_program_analysis_state_free(program->analysis);
                program->analysis = NULL;
                return false;
            }
        }
    }
    return true;
}

bool petta_program_analysis_enabled(const PettaProgram *program) {
    CETTA_SCOPED_SHARED_TRANSITION(program_analysis_read);
    return program && program->analysis;
}

void petta_program_free(PettaProgram *program) {
    CETTA_SCOPED_SHARED_TRANSITION(program_retirement);
    if (!program)
        return;
    /* Catalog owners can be retired after the program during cancellation.
     * Detach their back-pointers before dropping the program's root listing. */
    for (PettaTransactionCatalog *catalog=program->private_catalogs;catalog;) {
        PettaTransactionCatalog *next=catalog->next;
        catalog->owner=NULL; catalog->previous=NULL; catalog->next=NULL;
        catalog=next;
    }
    program->private_catalogs=NULL;
    /* A universe freed first has dropped its listings already. */
    for (size_t index = 0u; index < program->signature_universe_len; index++)
        term_universe_remove_root_provider(program->signature_universes[index],
                                           petta_program_signature_roots,
                                           program);
    free(program->signature_universes);
    for (size_t index = 0u;
         index < program->space_len; index++) {
        petta_program_space_dispose_catalog(
            &program->spaces[index]);
        petta_program_space_free_pending(&program->spaces[index]);
    }
    petta_program_analysis_state_free(program->analysis);
    for (size_t index = 0u;
         index < program->equation_template_c0_len; index++) {
        petta_equation_template_c0_free(program->equation_template_c0[index]);
    }
    free(program->equation_template_c0);
    free(program->predeclared_callability.named_heads);
    free(program->callability_cache.named_heads);
    free(program->callability_cache_tokens);
    free(program->spaces);
    for (PettaEquationTemplate *template = program->equation_templates;
         template; template = template->next_owned)
        bindings_frame_schema_release(template->frame_schema);
    arena_free(&program->plans);
    free(program->interned_plans);
    arena_free(&program->transient_scratch);
    free(program->transient_values);
    free(program->transient_plans);
    arena_free(&program->transient_plan_atoms);
    free(program);
}

const PettaPlanNode *petta_program_plan_current(
    PettaProgram *program, Atom *atom) {
    CETTA_SCOPED_SHARED_TRANSITION(program_plan_current);
    if (!program || !atom)
        return NULL;
    PettaCallabilityDomain callability = {0};
    bool ok = petta_program_collect_callability(
        program, &callability);
    const PettaPlanNode *plan =
        ok ? petta_plan_build(program, &callability, atom) : NULL;
    free(callability.named_heads);
    return plan;
}

const PettaPlanNode *petta_program_plan_current_in(
    PettaProgram *program, Arena *plans, Atom *atom) {
    CETTA_SCOPED_SHARED_TRANSITION(program_plan_current_in);
    if (!program || !plans || !atom)
        return NULL;
    PettaCallabilityDomain callability = {0};
    bool ok = petta_program_collect_callability(program, &callability);
    const PettaPlanNode *plan = ok
        ? petta_plan_build_in(program, plans, &callability, atom, false, false,
                             NULL,
                             NULL, 0u)
        : NULL;
    free(callability.named_heads);
    return plan;
}

typedef struct PettaTransientPlan {
    uint64_t hash;
    uint64_t epoch;
    Atom *atom;
    const PettaPlanNode *plan;
} PettaTransientPlan;

enum { PETTA_TRANSIENT_PLAN_BOUND = 4096u };

/* A hash alpha-equivalent atoms share: every variable hashes alike, and
 * atom_alpha_eq decides between atoms that collide.  A quoted argument is
 * not read; `*quoted` tells whether the goal has one, which is then compared
 * with no kept goal. */
/* Source-shape memoization is opportunistic.  A large substituted value is
 * planned against its positive certificates and interned by plan shape; it
 * must not be copied merely to retain a source key. */
enum { PETTA_TRANSIENT_PLAN_KEY_NODE_BOUND = 128u };

static uint64_t petta_alpha_hash(
        Atom *root, bool *quoted, bool *complete) {
    *quoted = false;
    *complete = true;
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    Atom *inline_stack[64];
    Atom **stack = inline_stack;
    size_t len = 0u;
    size_t cap = sizeof(inline_stack) / sizeof(inline_stack[0]);
    size_t visited = 0u;
    stack[len++] = root;
    while (len > 0u) {
        if (visited++ == PETTA_TRANSIENT_PLAN_KEY_NODE_BOUND) {
            *complete = false;
            break;
        }
        Atom *atom = stack[--len];
        uint64_t word;
        switch (atom->kind) {
        case ATOM_VAR:
            word = UINT64_C(0x9e3779b97f4a7c15);
            break;
        case ATOM_SYMBOL:
            word = (uint64_t)atom_symbol_current_id(atom) * UINT64_C(0xff51afd7ed558ccd);
            break;
        case ATOM_GROUNDED:
            word = atom_hash(atom);
            break;
        case ATOM_EXPR:
        default:
            word = UINT64_C(0xc4ceb9fe1a85ec53) ^ (uint64_t)atom->expr.len;
            if (len + atom->expr.len > cap) {
                size_t next = cap * 2u;
                while (next < len + atom->expr.len)
                    next *= 2u;
                Atom **grown = stack == inline_stack
                    ? malloc(sizeof(*grown) * next)
                    : realloc(stack, sizeof(*grown) * next);
                if (!grown) {
                    if (stack != inline_stack)
                        free(stack);
                    *complete = false;
                    return 0u;
                }
                if (stack == inline_stack)
                    memcpy(grown, inline_stack, sizeof(*grown) * len);
                stack = grown;
                cap = next;
            }
            if (atom->expr.len == 2u &&
                atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.quote)) {
                *quoted = true;
                stack[len++] = atom->expr.elems[0];
                break;
            }
            for (CettaExprIndex index = atom->expr.len; index > 0u; index--)
                stack[len++] = atom->expr.elems[index - 1u];
            break;
        }
        hash ^= word;
        hash *= UINT64_C(0x100000001b3);
        hash ^= hash >> 29;
    }
    if (stack != inline_stack)
        free(stack);
    return hash;
}

/* The slot of `atom`'s plan in the memo, or of the empty slot where it
 * would go. */
static PettaTransientPlan *petta_transient_plan_slot(
    PettaProgram *program, uint64_t hash, Atom *atom) {
    size_t mask = program->transient_plan_cap - 1u;
    for (size_t slot = (size_t)hash & mask;; slot = (slot + 1u) & mask) {
        PettaTransientPlan *entry = &program->transient_plans[slot];
        if (!entry->atom ||
            (entry->hash == hash && atom_alpha_eq(entry->atom, atom)))
            return entry;
    }
}

static void petta_transient_plans_clear(PettaProgram *program) {
    if (program->transient_plans)
        memset(program->transient_plans, 0,
               sizeof(*program->transient_plans) *
                   program->transient_plan_cap);
    program->transient_plan_len = 0u;
    arena_reset(&program->transient_plan_atoms, (ArenaMark){0});
}

/* Keep `plan` for goals alpha-equivalent to `atom`; best effort. */
static void petta_transient_plan_keep(PettaProgram *program, uint64_t hash,
                                      Atom *atom, const PettaPlanNode *plan) {
    if (program->transient_plan_len >= PETTA_TRANSIENT_PLAN_BOUND)
        petta_transient_plans_clear(program);
    if ((program->transient_plan_len + 1u) * 2u >
        program->transient_plan_cap) {
        size_t cap = program->transient_plan_cap
            ? program->transient_plan_cap * 2u : 64u;
        PettaTransientPlan *grown = calloc(cap, sizeof(*grown));
        if (!grown)
            return;
        PettaTransientPlan *old = program->transient_plans;
        size_t old_cap = program->transient_plan_cap;
        program->transient_plans = grown;
        program->transient_plan_cap = cap;
        for (size_t index = 0u; index < old_cap; index++) {
            if (!old[index].atom)
                continue;
            *petta_transient_plan_slot(program, old[index].hash,
                                       old[index].atom) = old[index];
        }
        free(old);
    }
    Atom *copy = atom_deep_copy(&program->transient_plan_atoms, atom);
    if (!copy)
        return;
    PettaTransientPlan *entry =
        petta_transient_plan_slot(program, hash, copy);
    if (!entry->atom)
        program->transient_plan_len++;
    *entry = (PettaTransientPlan){
        .hash = hash, .epoch = program->callability_epoch,
        .atom = copy, .plan = plan,
    };
}

/* Positive call-free certificates have two independent validity keys:
 * the program/context which classifies heads and the allocation lifetime
 * of the immutable source node. A hit borrows no source storage. */
typedef struct PettaTransientValue {
    const Atom *atom;
    uint64_t reset_epoch;
    uint64_t callability_epoch;
    uint32_t arena_identity;
} PettaTransientValue;

enum { PETTA_TRANSIENT_VALUE_CAP = 4096u };

static bool petta_translation_environment_equal(
    const PettaTranslationEnvironment *left,
    const PettaTranslationEnvironment *right) {
    return left->host_identity == right->host_identity &&
        left->foreign_instance == right->foreign_instance &&
        left->foreign_revision == right->foreign_revision &&
        left->admission_revision == right->admission_revision &&
        left->symbol_table_instance == right->symbol_table_instance &&
        left->profile_identity == right->profile_identity &&
        left->features == right->features;
}

static PettaTransientValue *petta_transient_value_slot(
    PettaProgram *program, const Arena *source, const Atom *atom) {
    if (!source || !source->identity || atom->arena_id != source->identity)
        return NULL;
    if (!program->transient_values) {
        program->transient_values = calloc(
            PETTA_TRANSIENT_VALUE_CAP, sizeof(*program->transient_values));
        if (!program->transient_values)
            return NULL;
    }
    uint64_t hash = (uint64_t)(uintptr_t)atom >> 4u;
    hash ^= hash >> 33u;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33u;
    return &program->transient_values[hash & (PETTA_TRANSIENT_VALUE_CAP - 1u)];
}

static PettaTransientValue *petta_translation_value_entry(
        PettaProgram *program,
        const PettaTranslationEnvironment *environment, Atom *atom,
        const Arena **source_out) {
    *source_out = NULL;
    if (!environment)
        return NULL;
    for (size_t index = 0u;
         index < environment->source_arena_count; index++) {
        const Arena *candidate = environment->source_arenas[index];
        if (candidate && candidate->identity == atom->arena_id) {
            *source_out = candidate;
            break;
        }
    }
    return petta_transient_value_slot(program, *source_out, atom);
}

static bool petta_translation_value_cached(
        PettaProgram *program,
        const PettaTranslationEnvironment *environment, Atom *atom) {
    const Arena *source = NULL;
    PettaTransientValue *slot =
        petta_translation_value_entry(
            program, environment, atom, &source);
    bool cached = slot && slot->atom == atom &&
        slot->arena_identity == source->identity &&
        slot->reset_epoch == source->reset_epoch &&
        slot->callability_epoch == program->callability_epoch;
    if (cached) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_TRANSLATION_VALUE_REUSE);
    }
    return cached;
}

static void petta_translation_value_keep(
        PettaProgram *program,
        const PettaTranslationEnvironment *environment, Atom *atom) {
    const Arena *source = NULL;
    PettaTransientValue *slot =
        petta_translation_value_entry(
            program, environment, atom, &source);
    if (slot) {
        *slot = (PettaTransientValue){
            .atom = atom, .reset_epoch = source->reset_epoch,
            .arena_identity = source->identity,
            .callability_epoch = program->callability_epoch,
        };
    }
}

/* This is a sufficient fragment of the translator's value rule. A dynamic
 * head or any language/program/extension operation defers to the full
 * translator. Constructor-built list and closure values are opaque there. */
static bool petta_translation_is_value(
    PettaProgram *program, const PettaCallabilityDomain *callability,
    const PettaTranslationEnvironment *environment, Atom *root) {
    typedef struct { Atom *atom; CettaExprIndex next; bool entered; } Frame;
    Frame inline_frames[32];
    Frame *frames = inline_frames;
    size_t len = 1u, cap = 32u;
    frames[0] = (Frame){.atom = root};
    bool value = true;
    while (len && value) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PETTA_TRANSLATION_VALUE_NODE);
        Frame *frame = &frames[len - 1u];
        Atom *atom = frame->atom;
        if (!atom) { value = false; break; }
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
            atom_is_list_form(atom) ||
            petta_semantics_is_canonical_closure(atom)) {
            len--;
            continue;
        }
        if (!frame->entered) {
            if (petta_translation_value_cached(
                    program, environment, atom)) {
                len--;
                continue;
            }
            Atom *head = atom->expr.elems[0];
            if (!head || head->kind != ATOM_SYMBOL ||
                petta_program_head_is_intrinsic(atom_symbol_current_id(head)) ||
                petta_callability_admits(callability, atom_symbol_current_id(head)) ||
                cetta_petta_head_names_extension(atom_symbol_current_id(head)) ||
                petta_head_transports_source_occurrences(atom_symbol_current_id(head)) ||
                atom_symbol_current_id(head) == g_builtin_syms.size ||
                atom_symbol_current_id(head) == g_builtin_syms.size_atom ||
                petta_semantics_form(atom_symbol_current_id(head)) == PETTA_FORM_LENGTH) {
                value = false;
                break;
            }
            frame->entered = true;
            frame->next = 1u;
        }
        if (frame->next == atom->expr.len) {
            petta_translation_value_keep(program, environment, atom);
            len--;
            continue;
        }
        Atom *child = atom->expr.elems[frame->next++];
        if (len == cap) {
            if (cap > SIZE_MAX / (2u * sizeof(*frames))) { value = false; break; }
            size_t next = cap * 2u;
            Frame *grown = frames == inline_frames
                ? malloc(next * sizeof(*grown)) : realloc(frames, next * sizeof(*grown));
            if (!grown) { value = false; break; }
            if (frames == inline_frames) memcpy(grown, frames, len * sizeof(*grown));
            frames = grown;
            cap = next;
        }
        frames[len++] = (Frame){.atom = child};
    }
    if (frames != inline_frames) free(frames);
    return value;
}

const PettaPlanNode *petta_program_plan_transient(
    PettaProgram *program, Atom *atom,
    const PettaTranslationEnvironment *environment) {
    CETTA_SCOPED_SHARED_TRANSITION(program_plan_transient);
    if (!program || !atom)
        return NULL;
    static const PettaPlanNode value_plan = {
        .role = PETTA_PLAN_VALUE, .output = PETTA_PLAN_OUTPUT_VALUE,
        .computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN,
        .source_position = 1u, .source_debit = 1u,
    };
    PettaCallabilityDomain owned = {0};
    const PettaCallabilityDomain *callability =
        &program->callability_cache;
    bool current = petta_program_callability_cache_current(program);
    if (!current) {
        if (!petta_program_collect_callability(program, &owned)) {
            free(owned.named_heads);
            return NULL;
        }
        callability = &owned;
        current = petta_program_callability_cache_current(program);
    }
    if (petta_program_has_private_images(program)) {
        /* Share only content-interned immutable plan shapes. A private
         * callability domain must not retag the public translation/value
         * caches, nor retain request syntax as a global memo key. */
        if (petta_translation_is_value(program, callability, NULL, atom)) {
            free(owned.named_heads);
            return &value_plan;
        }
        ArenaMark mark = arena_mark(&program->transient_scratch);
        PettaPlanNode *built = petta_plan_build_in(program,
            &program->transient_scratch, callability, atom, false, true,
            NULL, NULL, 0u);
        const PettaPlanNode *plan = built &&
                petta_plan_collapse_values(program, atom, built, NULL)
            ? petta_program_intern_plan(program, built, false) : NULL;
        arena_reset(&program->transient_scratch, mark);
        free(owned.named_heads);
        return plan;
    }
    if (!environment || !program->transient_environment_valid ||
        !petta_translation_environment_equal(
            &program->transient_environment, environment)) {
        petta_transient_plans_clear(program);
        if (program->transient_values)
            memset(program->transient_values, 0,
                   PETTA_TRANSIENT_VALUE_CAP * sizeof(*program->transient_values));
        program->transient_environment_valid = environment != NULL;
        if (environment) {
            program->transient_environment = *environment;
            program->transient_environment.source_arenas = NULL;
            program->transient_environment.source_arena_count = 0u;
        }
    }
    if (current && environment &&
        petta_translation_is_value(
            program, callability, environment, atom)) {
        static const PettaPlanNode value_plan = {
            .role = PETTA_PLAN_VALUE, .output = PETTA_PLAN_OUTPUT_VALUE,
            .computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN,
            .source_position = 1u, .source_debit = 1u,
        };
        free(owned.named_heads);
        return &value_plan;
    }
    /* A plan kept for an alpha-equivalent goal under this callability.  A
     * goal with a quoted argument is planned afresh, as cheaply as the
     * translation reads it: comparing it with a kept goal, or keeping it,
     * would read the argument. */
    bool quoted = false;
    bool key_complete = false;
    uint64_t hash = current
        ? petta_alpha_hash(atom, &quoted, &key_complete) : 0u;
    bool keyed = current && environment && key_complete && !quoted;
    if (keyed && program->transient_plan_len > 0u) {
        PettaTransientPlan *entry =
            petta_transient_plan_slot(program, hash, atom);
        if (entry->atom && entry->epoch == program->callability_epoch) {
            free(owned.named_heads);
            return entry->plan;
        }
    }
    ArenaMark mark = arena_mark(&program->transient_scratch);
    PettaPlanNode *built = petta_plan_build_in(
        program, &program->transient_scratch, callability, atom, false,
        true, environment, NULL, 0u);
    const PettaPlanNode *plan =
        built && petta_plan_collapse_values(
            program, atom, built, environment)
            ? petta_program_intern_plan(program, built, false)
            : NULL;
    arena_reset(&program->transient_scratch, mark);
    free(owned.named_heads);
    if (plan && keyed)
        petta_transient_plan_keep(program, hash, atom, plan);
    return plan;
}

PettaDeclarationBlock *petta_program_declaration_block_new(
    PettaProgram *program, const TermUniverse *universe,
    const AtomId *atoms, int atom_count) {
    CETTA_SCOPED_SHARED_TRANSITION(program_declaration_block);
    if (!program || !universe || atom_count < 0 ||
        (atom_count > 0 && !atoms)) {
        return NULL;
    }
    PettaDeclarationBlock *block =
        cetta_malloc(sizeof(*block));
    memset(block, 0, sizeof(*block));
    block->plan_count = atom_count;
    if (atom_count > 0) {
        if ((size_t)atom_count >
            SIZE_MAX / sizeof(*block->plans)) {
            free(block);
            return NULL;
        }
        block->plans = cetta_malloc(
            sizeof(*block->plans) * (size_t)atom_count);
        memset(
            block->plans, 0,
            sizeof(*block->plans) * (size_t)atom_count);
    }

    PettaCallabilityDomain callability = {0};
    bool ok = petta_program_collect_callability(
        program, &callability);
    for (int index = 0; ok && index < atom_count; index++) {
        if (!petta_program_form_id_may_affect_metadata(universe,
                                                       atoms[index]))
            continue;
        Atom *atom = term_universe_get_atom(universe, atoms[index]);
        Atom *lhs = NULL;
        SymbolId head = SYMBOL_ID_NONE;
        if (petta_program_equation_view(program, atom, &lhs, NULL, &head)) {
            if (petta_equation_lhs_admits_any_named_head(lhs)) {
                callability.admits_any_head = true;
            } else if (head != SYMBOL_ID_NONE) {
                ok = petta_callability_insert_named(
                    &callability, head);
            }
        }
    }
    /* Only an equation keeps a plan (petta_program_note_add); a fact or a
     * declaration is stored as it is, so it is not planned. */
    for (int index = 0; ok && index < atom_count; index++) {
        if (!petta_program_form_id_may_affect_metadata(universe,
                                                       atoms[index]))
            continue;
        Atom *atom = term_universe_get_atom(universe, atoms[index]);
        if (!petta_program_admits_equation(program, atom))
            continue;
        block->plans[index] =
            petta_plan_build(program, &callability, atom);
        ok = block->plans[index] != NULL;
    }
    free(callability.named_heads);
    if (!ok) {
        petta_program_declaration_block_free(block);
        return NULL;
    }
    return block;
}

void petta_program_declaration_block_free(
    PettaDeclarationBlock *block) {
    if (!block)
        return;
    free(block->plans);
    free(block);
}

const PettaPlanNode *petta_program_declaration_block_plan_at(
    const PettaDeclarationBlock *block, int index) {
    return block && index >= 0 && index < block->plan_count
        ? block->plans[index] : NULL;
}

static const PettaPlanNode *petta_program_plan_equation(
    PettaProgram *program, Atom *atom, Atom *const *held, size_t held_len);

const PettaPlanNode *petta_program_plan_dynamic_add(
    PettaProgram *program, Atom *atom) {
    return petta_program_plan_equation(program, atom, NULL, 0u);
}

const PettaPlanNode *petta_program_plan_derived_add(
    PettaProgram *program, Atom *atom, Atom *const *held, size_t held_len) {
    return petta_program_plan_equation(program, atom, held, held_len);
}

static const PettaPlanNode *petta_program_plan_equation(
    PettaProgram *program, Atom *atom, Atom *const *held, size_t held_len) {
    CETTA_SCOPED_SHARED_TRANSITION(program_equation_plan);
    if (!program || !atom)
        return NULL;
    PettaCallabilityDomain callability = {0};
    bool ok = petta_program_collect_callability(
        program, &callability);
    Atom *lhs = NULL;
    SymbolId head = SYMBOL_ID_NONE;
    if (ok && petta_program_equation_view(program,
            atom, &lhs, NULL, &head)) {
        if (petta_equation_lhs_admits_any_named_head(lhs)) {
            callability.admits_any_head = true;
        } else if (head != SYMBOL_ID_NONE) {
            ok = petta_callability_insert_named(
                &callability, head);
        }
    }
    const PettaPlanNode *plan =
        ok ? petta_plan_build_in(
                 program, &program->plans, &callability, atom, true, false,
                 NULL,
                 held, held_len)
           : NULL;
    free(callability.named_heads);
    return plan;
}

bool petta_plan_match_template_is_single_data_answer(
    const PettaPlanNode *template_plan) {
    return template_plan &&
        (template_plan->role == PETTA_PLAN_VALUE ||
         (template_plan->role == PETTA_PLAN_DATA &&
          !template_plan->contains_call));
}

/* The live space proves exactly one own-row append and no catalog operation
 * has changed the synchronized prefix independently. */
static bool petta_program_space_single_append(
        const PettaProgramSpace *entry, const Space *space, Atom *atom,
        SpaceReadToken *read_out, SpaceProgramToken *program_out) {
    if (!entry || !space || !atom || !read_out || !program_out ||
        !entry->synchronized_snapshot || entry->space != space ||
        entry->instance_id != space_instance_id(space) ||
        space->overlay_base || space->dep_count ||
        entry->synchronized_prefix_epoch != space->prefix_epoch ||
        entry->synchronized_catalog_generation == UINT64_MAX ||
        entry->synchronized_catalog_generation != entry->catalog_generation ||
        entry->synchronized_equation_len == SIZE_MAX ||
        entry->synchronized_equation_len != entry->equation_len ||
        entry->synchronized_revision == UINT64_MAX ||
        entry->synchronized_atom_count == UINT64_MAX)
        return false;
    SpaceReadToken read = space_read_token(space);
    SpaceProgramToken current = space_program_token(space);
    SpaceProgramToken old = entry->synchronized_program;
    if (read.revision != entry->synchronized_revision + 1u ||
        space_length64(space) != entry->synchronized_atom_count + 1u ||
        old.space != space || old.instance_id != current.instance_id ||
        old.base_dependency_epoch != 0u || current.base_dependency_epoch != 0u ||
        old.equation_revision == UINT64_MAX ||
        current.equation_revision != old.equation_revision + 1u ||
        current.declaration_revision != old.declaration_revision)
        return false;
    Atom *last = space_get_at64(space, entry->synchronized_atom_count);
    if (!last || !atom_eq(last, atom))
        return false;
    *read_out = read;
    *program_out = current;
    return true;
}

static bool petta_program_note_add_with_execution_plan(
    PettaProgram *program, Space *space, Atom *atom,
    const PettaPlanNode *plan, bool transferred_execution_plan) {
    CETTA_SCOPED_SHARED_TRANSITION(program_catalog_add);
    if (!program || !space || !atom)
        return false;
    Atom *subject = NULL;
    Atom *type = NULL;
    if (petta_program_type_declaration_view(
            atom, &subject, &type)) {
#if CETTA_MANAGED_SYMBOLS
        /* The subject indexes the declaration. Its type expression is a
         * retained value, not a request to intern every symbol it contains. */
        if (subject->kind == ATOM_SYMBOL)
            (void)atom_symbol_register_id(subject);
#else
        atom_pin_syntax_symbols(atom);
#endif
        if (!program->analysis)
            return petta_program_ensure_space(program, space) != NULL;
        PettaProgramAnalysisSpace *entry =
            petta_program_ensure_analysis_space(program, space);
        if (!entry)
            return false;
        if (petta_program_type_is_exclusive_kind(type)) {
            for (size_t index = 0u;
                 index < entry->type_annotation_len; index++) {
                Atom *prior = entry->type_annotations[index];
                if (!prior || prior->kind != ATOM_EXPR ||
                    prior->expr.len != 3u ||
                    !atom_eq(prior->expr.elems[1], subject) ||
                    !petta_program_type_is_exclusive_kind(
                        prior->expr.elems[2])) {
                    continue;
                }
                /* Type kinds are source ordered and mutually exclusive.
                 * The first accepted declaration owns the name; identical
                 * repeats are idempotent and conflicting repeats never
                 * become candidates after a later removal. */
                return true;
            }
        }
        if (!petta_program_reserve(
                (void **)&entry->type_annotations,
                &entry->type_annotation_cap,
                entry->type_annotation_len + 1u,
                sizeof(*entry->type_annotations))) {
            return false;
        }
        Atom *copy = atom_deep_copy(&program->plans, atom);
        if (!copy)
            return false;
        entry->type_annotations[entry->type_annotation_len++] = copy;
        entry->type_index_dirty = true;
        return true;
    }
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    SymbolId head = SYMBOL_ID_NONE;
    if (!petta_program_equation_view(program, atom, &lhs, &rhs, &head))
        return true;
    if (lhs->expr.elems[0]->kind == ATOM_SYMBOL)
        head = atom_symbol_register_id(lhs->expr.elems[0]);
    Atom *execution = atom;
    if (atom->expr.len != 3u) {
        execution = space_equation_execution_view(&program->plans, atom);
        if (!execution) return false;
        lhs = execution->expr.elems[1];
        rhs = execution->expr.elems[2];
        if (!transferred_execution_plan || !plan)
            plan = petta_program_plan_equation(program, execution, NULL, 0u);
        if (!plan) return false;
    }
    PettaProgramSpace *entry =
        petta_program_ensure_space(program, space);
    SpaceReadToken append_read = {0};
    SpaceProgramToken append_program = {0};
    bool notified_append = plan && petta_program_space_single_append(
        entry, space, atom, &append_read, &append_program);
    if (!petta_program_space_reserve_equation(entry)) {
        return false;
    }
    uint32_t static_variable_count = 0u;
    bool open_template_admitted = false;
    const PettaEquationTemplate *equation_template = NULL;
    PettaEquationTemplateC0 *equation_template_c0 =
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.equals)
        ? petta_program_compile_equation_template_c0(
            program, lhs, rhs, head,
            &static_variable_count,
            &open_template_admitted,
            &equation_template) : NULL;
    if (equation_template && plan) {
        plan = petta_plan_rebind_frame_syntax(
            program, equation_template->equation, plan);
        if (!plan || !petta_plan_assign_equation_variable_slots(
                equation_template->equation, plan, equation_template))
            return false;
    }
    if (open_template_admitted &&
        !petta_plan_mark_open_template_admitted(
            petta_plan_child(plan, 2u))) {
        return false;
    }
    uint64_t source_root = cetta_source_root_new();
    if (!source_root)
        return false;
    petta_program_space_append_reserved_equation(
        entry,
        (PettaProgramEquation){
            .equation = atom,
            .planning_syntax = equation_template ? equation_template->equation : execution,
            .source_root = source_root,
            .plan = plan,
            .equation_template_c0 = equation_template_c0,
            .equation_template = equation_template,
            .static_variable_count = static_variable_count,
            .head = head,
            .rhs_binds_variables =
                petta_program_rhs_binds_variables(lhs, rhs),
        });
    if (notified_append &&
        entry->catalog_generation == entry->synchronized_catalog_generation + 1u &&
        entry->equation_len == entry->synchronized_equation_len + 1u &&
        space_read_token_matches_live_space(append_read, space) &&
        space_program_token_matches_live_space(append_program, space)) {
        entry->synchronized_revision = append_read.revision;
        entry->synchronized_program = append_program;
        entry->synchronized_prefix_epoch = append_read.prefix_epoch;
        entry->synchronized_atom_count++;
        entry->synchronized_equation_len = entry->equation_len;
        entry->synchronized_catalog_generation = entry->catalog_generation;
    }
    return true;
}

bool petta_program_note_add(PettaProgram *program, Space *space,
        Atom *atom, const PettaPlanNode *plan) {
    return petta_program_note_add_with_execution_plan(program, space, atom, plan, false);
}

/* An unnotified append can use the same certificate when the program's
 * callability cache describes exactly this space's complete view. Otherwise
 * the general synchronization retains authority. */
static bool petta_program_space_sync_one_append(
        PettaProgram *program, PettaProgramSpace *entry, Space *space) {
    if (!program || !entry || !space || program->space_len != 1u ||
        program->predeclared_callability.admits_any_head ||
        program->predeclared_callability.named_len != 0u)
        return false;
    Atom *atom = space_get_at64(space, entry->synchronized_atom_count);
    SpaceReadToken read = {0};
    SpaceProgramToken token = {0};
    if (!atom || !petta_program_space_single_append(entry, space, atom, &read, &token) ||
        !petta_program_equation_view(program, atom, NULL, NULL, NULL))
        return false;
    PettaCallabilityDomain callability = {0};
    const PettaCallabilityDomain *view = NULL;
    bool collected = petta_program_callability_view(program, &callability, &view);
    const PettaPlanNode *plan = collected
        ? petta_plan_build(program, view, atom) : NULL;
    free(callability.named_heads);
    if (!plan || !space_read_token_matches_live_space(read, space) ||
        !space_program_token_matches_live_space(token, space) ||
        !petta_program_note_add(program, space, atom, plan))
        return false;
    return entry->synchronized_snapshot &&
        entry->synchronized_revision == read.revision &&
        space_read_token_matches_live_space(read, space) &&
        space_program_token_matches_live_space(token, space);
}

/* Check the same callability observations that formed this source plan.
 * Value positions retain their interpretation; evaluating a held value is a
 * separate translation boundary. The traversal is iterative for deep terms. */
static bool petta_plan_callability_changed(const Atom *syntax,
        const PettaPlanNode *plan, const PettaCallabilityDomain *domain,
        bool *changed) {
    typedef struct {
        const Atom *syntax;
        const PettaPlanNode *plan;
    } Read;
    Read *reads = NULL;
    size_t len = 0u, cap = 0u;
    *changed = !plan;
    if (!plan)
        return true;
    if (!petta_program_reserve((void **)&reads, &cap, 1u, sizeof(*reads)))
        return false;
    reads[len++] = (Read){syntax, plan};
    while (len > 0u) {
        Read read = reads[--len];
        if (!read.syntax || !read.plan) {
            free(reads);
            return false;
        }
        if (read.plan->role == PETTA_PLAN_VALUE ||
            read.syntax->kind != ATOM_EXPR)
            continue;
        CettaExprLen count = read.syntax->expr.len;
        if (count != read.plan->child_count) {
            free(reads);
            return false;
        }
        if (count && read.syntax->expr.elems[0]->kind == ATOM_SYMBOL &&
            read.plan->relation_head_admitted != petta_callability_admits(
                domain, atom_symbol_current_id(read.syntax->expr.elems[0]))) {
            *changed = true;
            break;
        }
        if (!petta_program_reserve((void **)&reads, &cap, len + count,
                                    sizeof(*reads))) {
            free(reads);
            return false;
        }
        for (CettaExprIndex i = 0u; i < count; i++)
            reads[len++] = (Read){read.syntax->expr.elems[i],
                                 petta_plan_child(read.plan, i)};
    }
    free(reads);
    return true;
}

/* Refresh only the selected declaration family. Existing leases keep the
 * immutable plans they captured; new selections see the complete replacement
 * batch. A global callability epoch is just a fast rejection stamp: the source
 * plan's actual reads decide whether recompilation is necessary. */
static bool petta_program_refresh_callability(PettaProgram *program,
        PettaProgramSpace *entry, SymbolId head) {
    if (!program || !entry || !program->refresh_callability)
        return true;
    if (!petta_program_reserve((void **)&entry->plan_callability_epochs,
            &entry->plan_callability_epoch_cap, entry->equation_len,
            sizeof(*entry->plan_callability_epochs)))
        return false;
    if (entry->plan_callability_epoch_len < entry->equation_len) {
        memset(entry->plan_callability_epochs + entry->plan_callability_epoch_len,
               0, sizeof(*entry->plan_callability_epochs) *
                    (entry->equation_len - entry->plan_callability_epoch_len));
        entry->plan_callability_epoch_len = entry->equation_len;
    }
    PettaCallabilityDomain local = {0};
    const PettaCallabilityDomain *domain = NULL;
    if (!petta_program_callability_view(program, &local, &domain)) {
        free(local.named_heads);
        return false;
    }
    bool cached = domain == &program->callability_cache;
    typedef struct {
        size_t index;
        const PettaPlanNode *plan;
    } Update;
    Update *updates = NULL;
    size_t len = 0u, cap = 0u;
    bool indexed = head != SYMBOL_ID_NONE &&
        (!entry->head_index_dirty ||
         petta_program_space_rebuild_head_index(entry));
    const PettaProgramHeadBucket *buckets[2] = {
        indexed ? petta_program_space_find_head_bucket(entry, head) : NULL,
        indexed ? petta_program_space_find_head_bucket(entry, SYMBOL_ID_NONE) : NULL,
    };
    bool ok = true;
    for (size_t family = 0u; ok && family < (indexed ? 2u : 1u); family++) {
        const PettaProgramHeadBucket *bucket = buckets[family];
        size_t count = indexed ? (bucket ? bucket->len : 0u) : entry->equation_len;
        for (size_t at = 0u; ok && at < count; at++) {
            size_t index = indexed ? bucket->record_indices[at] : at;
            PettaProgramEquation *record = &entry->equations[index];
            if (head != SYMBOL_ID_NONE && record->head != SYMBOL_ID_NONE &&
                record->head != head)
                continue;
            if (cached && program->callability_epoch &&
                entry->plan_callability_epochs[index] == program->callability_epoch)
                continue;
            Atom *syntax = record->planning_syntax;
            bool changed = false;
            ok = petta_plan_callability_changed(syntax, record->plan, domain, &changed);
            if (!ok)
                break;
            if (!changed) {
                entry->plan_callability_epochs[index] =
                    cached ? program->callability_epoch : 0u;
                continue;
            }
            const PettaPlanNode *plan = petta_plan_build(program, domain, syntax);
            ok = plan && (!record->equation_template ||
                petta_plan_assign_equation_variable_slots(
                    syntax, plan, record->equation_template));
            const PettaPlanNode *old_rhs = petta_plan_child(record->plan, 2u);
            if (ok && old_rhs && old_rhs->open_template_admitted)
                ok = petta_plan_mark_open_template_admitted(petta_plan_child(plan, 2u));
            if (ok)
                ok = petta_program_reserve((void **)&updates, &cap,
                                           len + 1u, sizeof(*updates));
            if (ok)
                updates[len++] = (Update){index, plan};
        }
    }
    if (ok) {
        for (size_t i = 0u; i < len; i++) {
            PettaProgramEquation *record = &entry->equations[updates[i].index];
            record->plan = updates[i].plan;
            entry->plan_callability_epochs[updates[i].index] =
                cached ? program->callability_epoch : 0u;
        }
        if (len)
            petta_program_space_commit_catalog_mutation(entry);
    }
    free(updates);
    free(local.named_heads);
    return ok;
}


bool petta_program_observe_addition(
    PettaProgram *program, Space *space, Arena *storage,
    Atom *atom, const PettaPlanNode *plan) {
    CETTA_SCOPED_SHARED_TRANSITION(program_catalog_observe_addition);
    if (!program || !space || !storage || !atom)
        return false;
    if (!petta_program_atom_affects_metadata(atom))
        return true;
    Atom *recorded = space_store_atom(space, storage, atom);
    return recorded &&
        petta_program_note_add(program, space, recorded, plan);
}

void petta_program_note_remove_all(
    PettaProgram *program, Space *space, Atom *atom) {
    CETTA_SCOPED_SHARED_TRANSITION(program_catalog_remove_all);
    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    if (!atom)
        return;
    SymbolId removed_head = SYMBOL_ID_NONE;
    if (entry && petta_program_equation_view(program, atom, NULL, NULL, &removed_head))
        petta_program_space_settle_pending(entry, removed_head);
    PettaProgramAnalysisSpace *analysis =
        petta_program_find_analysis_space(program, space);
    if (analysis) {
        size_t annotation_write = 0u;
        for (size_t read = 0u;
             read < analysis->type_annotation_len; read++) {
            if (atom_eq(analysis->type_annotations[read], atom))
                continue;
            analysis->type_annotations[annotation_write++] =
                analysis->type_annotations[read];
        }
        analysis->type_annotation_len = annotation_write;
        analysis->type_index_dirty = true;
    }
    if (!entry)
        return;
    (void)petta_program_space_remove_all_matching_equations(
        entry, atom);
}

void petta_program_note_remove_one(
    PettaProgram *program, Space *space, Atom *atom) {
    CETTA_SCOPED_SHARED_TRANSITION(program_catalog_remove_one);
    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    if (!atom)
        return;
    SymbolId removed_head = SYMBOL_ID_NONE;
    if (entry && petta_program_equation_view(program, atom, NULL, NULL, &removed_head))
        petta_program_space_settle_pending(entry, removed_head);
    PettaProgramAnalysisSpace *analysis =
        petta_program_find_analysis_space(program, space);
    if (analysis) {
        for (size_t index = 0u;
             index < analysis->type_annotation_len; index++) {
            if (!atom_eq(analysis->type_annotations[index], atom))
                continue;
            memmove(
                analysis->type_annotations + index,
                analysis->type_annotations + index + 1u,
                sizeof(*analysis->type_annotations) *
                    (analysis->type_annotation_len - index - 1u));
            analysis->type_annotation_len--;
            analysis->type_index_dirty = true;
            break;
        }
    }
    if (!entry)
        return;
    size_t exact = SIZE_MAX;
    size_t alpha = SIZE_MAX;
    size_t alpha_count = 0u;
    for (size_t index = 0u;
         index < entry->equation_len; index++) {
        Atom *candidate = entry->equations[index].equation;
        if (atom_eq(candidate, atom)) {
            exact = index;
            break;
        }
        if (atom_alpha_eq(candidate, atom)) {
            alpha = index;
            alpha_count++;
        }
    }
    size_t remove =
        exact != SIZE_MAX
            ? exact
            : (alpha_count == 1u ? alpha : SIZE_MAX);
    if (remove == SIZE_MAX)
        return;
    petta_program_space_remove_equation_at(entry, remove);
}

bool petta_program_synchronize_space(
        PettaProgram *program, Space *space) {
    CETTA_SCOPED_SHARED_TRANSITION(program_catalog_synchronize);
    if (!program || !space)
        return false;
    PettaProgramSpace *current =
        petta_program_find_space(program, space);
    uint64_t revision = space_revision(space);
    if (current && current->synchronized_snapshot) {
        if (current->synchronized_revision == revision)
            return true;
        /* A data-only mutation leaves the program as it was. */
        if (space_program_token_matches_live_space(
                current->synchronized_program, space)) {
            current->synchronized_revision = revision;
            current->synchronized_prefix_epoch = space->prefix_epoch;
            current->synchronized_atom_count = space_length64(space);
            return true;
        }
        if (petta_program_space_sync_one_append(program, current, space))
            return true;
    }

    SpaceReadToken read = space_read_token(space);
    SpaceProgramToken program_token = space_program_token(space);
    CettaCount atom_count = space_length64(space);
    CettaCount view_count = space_view_length64(space);
    PettaCallabilityDomain callability = {0};
    bool ok = true;
    /* The view's first rows are the space's own; the equations among them,
     * in order, are the ones compiled below. */
    CettaIndex *own_rules = NULL;
    size_t own_rule_len = 0u, own_rule_cap = 0u;
    /* Imported heads are callable in this view, while each equation remains
     * owned by its defining space's occurrence catalog below. */
    for (CettaIndex index = 0u; ok && index < view_count; index++) {
        if (!space_view_row_may_be_rule(space, index))
            continue;
        Atom *atom = space_view_get_at64(space, index);
        Atom *lhs = NULL;
        SymbolId head = SYMBOL_ID_NONE;
        if (!atom) {
            ok = false;
        } else if (petta_program_equation_view(program,
                       atom, &lhs, NULL, &head)) {
            if (index < atom_count) {
                if (own_rule_len == own_rule_cap) {
                    size_t cap = own_rule_cap ? own_rule_cap * 2u : 16u;
                    CettaIndex *grown =
                        realloc(own_rules, cap * sizeof(*own_rules));
                    if (!grown) {
                        ok = false;
                        break;
                    }
                    own_rules = grown;
                    own_rule_cap = cap;
                }
                own_rules[own_rule_len++] = index;
            }
            if (petta_equation_lhs_admits_any_named_head(lhs)) {
                callability.admits_any_head = true;
            } else if (head != SYMBOL_ID_NONE) {
                ok = petta_callability_insert_named(
                    &callability, head);
            }
        }
    }

    /*
     * PeTTa compiles an equation once, against the program as it stood
     * when the equation was defined.  The catalog's equations were
     * compiled that way as they were added, so each keeps its plan here;
     * only an equation the program has not seen is compiled now.  The
     * space's equations are matched against the catalog's in source
     * order, which PeTTa's own additions and removals preserve.
     */
    PettaProgramSpace *entry =
        ok ? petta_program_ensure_space(program, space) : NULL;
    PettaProgramEquation *known = NULL;
    size_t known_len = 0u;
    bool *taken = NULL;
    if (entry) {
        entry->synchronized_snapshot = false;
        known = entry->equations;
        known_len = entry->equation_len;
        entry->equations = NULL;
        entry->equation_len = 0u;
        entry->equation_cap = 0u;
        petta_program_space_dispose_catalog(entry);
        taken = known_len ? calloc(known_len, sizeof(*taken)) : NULL;
        ok = !known_len || taken;
    } else {
        ok = false;
    }
    size_t cursor = 0u;
    for (size_t rule = 0u; ok && rule < own_rule_len; rule++) {
        Atom *atom = space_get_at64(space, own_rules[rule]);
        size_t match = known_len;
        if (cursor < known_len && !taken[cursor] &&
            atom_eq(known[cursor].equation, atom)) {
            match = cursor++;
        } else {
            for (size_t candidate = 0u;
                 candidate < known_len; candidate++) {
                if (!taken[candidate] &&
                    atom_eq(known[candidate].equation, atom)) {
                    match = candidate;
                    break;
                }
            }
        }
        if (match < known_len) {
            taken[match] = true;
            ok = petta_program_space_reserve_equation(entry);
            if (ok)
                petta_program_space_append_reserved_equation(
                    entry, known[match]);
            continue;
        }
        const PettaPlanNode *plan =
            petta_plan_build(program, &callability, atom);
        ok = plan && petta_program_note_add(
            program, space, atom, plan);
    }
    free(taken);
    free(known);
    free(own_rules);
    bool empty_callability = !callability.admits_any_head && !callability.named_len;
    free(callability.named_heads);

    if (!ok || !space_read_token_matches_live_space(read, space)) {
        petta_program_forget_space(program, space);
        return false;
    }
    PettaProgramSpace *installed =
        petta_program_ensure_space(program, space);
    if (!installed) {
        petta_program_forget_space(program, space);
        return false;
    }
    installed->synchronized_revision = read.revision;
    installed->synchronized_program = program_token;
    installed->synchronized_prefix_epoch = read.prefix_epoch;
    installed->synchronized_atom_count = atom_count;
    installed->synchronized_equation_len = installed->equation_len;
    installed->synchronized_catalog_generation = installed->catalog_generation;
    installed->synchronized_snapshot = true;
    /* A newly registered fact-only space contributes no callable heads.
     * Extend the cache's resource inventory without invalidating the domain
     * or the immutable plans already certified against it. */
    if (empty_callability && program->callability_cache_valid &&
        program->callability_cache_token_len + 1u == program->space_len &&
        installed == &program->spaces[program->space_len - 1u] &&
        petta_program_reserve((void **)&program->callability_cache_tokens,
            &program->callability_cache_token_cap, program->space_len,
            sizeof(*program->callability_cache_tokens)))
        program->callability_cache_tokens[program->callability_cache_token_len++] =
            petta_program_callability_scan_of(installed);
    return true;
}

static int petta_program_snapshot_catalog(
    const PettaProgram *program, const Space *space,
    const CettaCheckpointCompare *read, const PettaProgramSpace **out) {
    *out = NULL;
    if (!read->service(read->context, "program.catalog.header", 1u, 0u))
        return -1;
    if (!program || !space || program->space_len > program->space_cap ||
        (program->space_len && !program->spaces) ||
        space->kind != SPACE_KIND_ATOM || space->overlay_base ||
        space->dep_count || space->imported_len)
        return 0;
    uint64_t instance = space_instance_id(space);
    for (size_t i = 0u; i < program->space_len; i++) {
        if (!read->service(read->context, "program.catalog.entry", 1u, 0u))
            return -1;
        const PettaProgramSpace *entry = &program->spaces[i];
        if (entry->space != space || entry->instance_id != instance)
            continue;
        if (*out)
            return 0;
        *out = entry;
    }
    if (!read->service(read->context, "program.catalog.current", 1u, 0u))
        return -1;
    const PettaProgramSpace *entry = *out;
    return entry && entry->synchronized_snapshot &&
        entry->synchronized_revision == space_revision(space) &&
        space_program_token_matches_live_space(entry->synchronized_program, space) &&
        entry->equation_len <= entry->equation_cap &&
        (!entry->equation_len || entry->equations) ? 1 : 0;
}

int petta_program_snapshot_semantics_compare(
    const PettaProgram *left, const Space *left_space,
    const PettaProgram *right, const Space *right_space,
    const CettaCheckpointCompare *read) {
    if (!read || !read->scalar || !read->atom || !read->service ||
        !read->service_release)
        return -1;
    const PettaProgramSpace *a = NULL, *b = NULL;
    int found = petta_program_snapshot_catalog(left, left_space, read, &a);
    if (found != 1)
        return found;
    found = petta_program_snapshot_catalog(right, right_space, read, &b);
    if (found != 1)
        return found;
    if (!read->service(read->context, "program.snapshot.tokens", 1u, 0u))
        return -1;
    SpaceReadToken left_read = space_read_token(left_space);
    SpaceReadToken right_read = space_read_token(right_space);
    uint64_t left_generation = a->catalog_generation;
    uint64_t right_generation = b->catalog_generation;
    if (!read->scalar(read->context, "program.graded", left->graded_equations,
                       right->graded_equations))
        return -1;
    if (left->graded_equations != right->graded_equations)
        return 0;
    if (!read->scalar(read->context, "program.callability-refresh", left->refresh_callability,
                       right->refresh_callability))
        return -1;
    if (left->refresh_callability != right->refresh_callability)
        return 0;
    if (!read->scalar(read->context, "program.rule-forms", left->admitted_rule_forms,
                       right->admitted_rule_forms))
        return -1;
    if (left->admitted_rule_forms != right->admitted_rule_forms)
        return 0;
    if (!read->service(read->context, "program.intrinsic-policy", 1u, 0u))
        return -1;
    if (left->is_host_intrinsic != right->is_host_intrinsic)
        return 0;
    CettaCount rows = space_length64(left_space);
    CettaCount other_rows = space_length64(right_space);
    if (!read->scalar(read->context, "program.rows", rows, other_rows))
        return -1;
    if (rows != other_rows)
        return 0;
    size_t equation = 0u;
    for (CettaIndex i = 0u; i < rows; i++) {
        if (!read->service(read->context, "program.row", 1u, 0u))
            return -1;
        Atom *x = space_get_at64(left_space, i);
        Atom *y = space_get_at64(right_space, i);
        if (!x || !y)
            return 0;
        if (!read->atom(read->context, "program.row.image", x, y))
            return -1;
        bool ax = petta_program_admits_equation(left, x);
        bool by = petta_program_admits_equation(right, y);
        if (!read->scalar(read->context, "program.row.equation", ax, by))
            return -1;
        if (ax != by)
            return 0;
        if (!ax)
            continue;
        if (!read->service(read->context, "program.occurrence", 1u, 0u))
            return -1;
        if (equation >= a->equation_len || equation >= b->equation_len)
            return 0;
        const PettaProgramEquation *p = &a->equations[equation];
        const PettaProgramEquation *q = &b->equations[equation];
        /* Current row pointers are the occurrence map. Equal old catalog
         * syntax is insufficient to authenticate a borrowed source root. */
        if (p->equation != x || q->equation != y || !p->plan || !q->plan ||
            !p->planning_syntax || !q->planning_syntax)
            return 0;
        if (!read->scalar(read->context, "program.equation.head", p->head, q->head))
            return -1;
        if (p->head != q->head)
            return 0;
        int equal = petta_plan_semantics_compare(p->plan, q->plan,
            left->refresh_callability ? p->planning_syntax : NULL,
            right->refresh_callability ? q->planning_syntax : NULL, read);
        if (equal != 1)
            return equal;
        equation++;
    }
    if (!read->scalar(read->context, "program.catalog.length", a->equation_len,
                       b->equation_len))
        return -1;
    if (equation != a->equation_len || equation != b->equation_len)
        return 0;
    if (!read->service(read->context, "program.snapshot.current", 1u, 0u))
        return -1;
    return a->catalog_generation == left_generation &&
        b->catalog_generation == right_generation &&
        space_read_token_matches_live_space(left_read, left_space) &&
        space_read_token_matches_live_space(right_read, right_space) &&
        space_program_token_matches_live_space(a->synchronized_program, left_space) &&
        space_program_token_matches_live_space(b->synchronized_program, right_space) ? 1 : 0;
}

typedef enum {
    PETTA_PORTABLE_RELATION_VISITING = 1,
    PETTA_PORTABLE_RELATION_ACCEPTED,
    PETTA_PORTABLE_RELATION_REJECTED,
} PettaPortableRelationState;

typedef struct {
    SymbolId head;
    CettaExprLen arity;
    PettaPortableRelationState state;
} PettaPortableRelation;

typedef struct {
    PettaProgram *program;
    PettaProgramSpace *catalog;
    PettaPortableRelation *relations;
    size_t relation_len;
    size_t relation_cap;
    bool eager_arguments;
} PettaPortableExecutionCheck;

static bool petta_portable_relation_presence(
        const PettaPortableExecutionCheck *check,
        SymbolId head, CettaExprLen arity,
        bool *exact_out, bool *open_out) {
    if (exact_out)
        *exact_out = false;
    if (open_out)
        *open_out = false;
    if (!check || !check->catalog || head == SYMBOL_ID_NONE ||
        !exact_out || !open_out) {
        return false;
    }
    for (size_t index = 0u;
         index < check->catalog->equation_len; index++) {
        Atom *lhs = NULL;
        if (!petta_rule_syntax_view(
                check->catalog->equations[index].equation,
                CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL, true,
                &lhs, NULL, NULL) ||
            !lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
            lhs->expr.len - 1u != arity) {
            continue;
        }
        Atom *lhs_head = lhs->expr.elems[0];
        if (!lhs_head || lhs_head->kind != ATOM_SYMBOL) {
            *open_out = true;
        } else if (atom_symbol_current_id(lhs_head) == head) {
            *exact_out = true;
        }
    }
    return true;
}

static bool petta_portable_structural_data(
        PettaPortableExecutionCheck *check, Atom *atom) {
    if (!check || !atom)
        return false;
    if (atom->kind != ATOM_EXPR)
        return true;
    if (atom->expr.len == 0u)
        return true;
    Atom *head = atom->expr.elems[0];
    if (!head)
        return false;
    if (head->kind != ATOM_SYMBOL) {
        if (!check->eager_arguments)
            return false;
        /* An inert tuple head stays data. A computed or captured callable
         * is outside this structural execution qualification. */
        if (head->kind == ATOM_VAR ||
            !petta_portable_structural_data(check, head))
            return false;
        for (CettaExprIndex i = 1u; i < atom->expr.len; ++i)
            if (!petta_portable_structural_data(check, atom->expr.elems[i]))
                return false;
        return true;
    }
    if (
        petta_semantics_is_open_cons_value(atom) ||
        petta_semantics_is_cons_constraint(atom) ||
        petta_program_head_is_intrinsic(atom_symbol_current_id(head)) ||
        is_grounded_op(atom_symbol_current_id(head))) {
        return false;
    }
    bool exact = false;
    bool open = false;
    if (!petta_portable_relation_presence(
            check, atom_symbol_current_id(head), atom->expr.len - 1u,
            &exact, &open) || exact || open) {
        return false;
    }
    for (CettaExprIndex index = 1u;
         index < atom->expr.len; index++) {
        if (!petta_portable_structural_data(
                check, atom->expr.elems[index])) {
            return false;
        }
    }
    return true;
}

static bool petta_portable_relation_check(
    PettaPortableExecutionCheck *check,
    SymbolId head, CettaExprLen arity);

static bool petta_portable_executable(
        PettaPortableExecutionCheck *check, Atom *atom) {
    if (!check || !atom)
        return false;
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return true;
    if (check->eager_arguments && atom_is_error(atom))
        return true;
    Atom *head = atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return check->eager_arguments &&
            petta_portable_structural_data(check, atom);

    bool exact = false;
    bool open = false;
    if (!petta_portable_relation_presence(
            check, atom_symbol_current_id(head), atom->expr.len - 1u,
            &exact, &open) || open) {
        return false;
    }
    if (check->eager_arguments) {
        if ((atom_symbol_current_id(head) == g_builtin_syms.if_text &&
             (atom->expr.len == 3u || atom->expr.len == 4u)) ||
            (atom_symbol_current_id(head) == g_builtin_syms.let && atom->expr.len == 4u)) {
            /* HE retains the raw let pattern and the held control bodies;
             * their later interpretation uses its typed-call host service. */
            CettaExprIndex start = atom_symbol_current_id(head) == g_builtin_syms.let ? 2u : 1u;
            for (CettaExprIndex i = start; i < atom->expr.len; ++i)
                if (!petta_portable_executable(check, atom->expr.elems[i]))
                    return false;
            return true;
        }
        if (is_grounded_op(atom_symbol_current_id(head)) &&
            petta_semantics_form(atom_symbol_current_id(head)) == PETTA_FORM_NONE &&
            petta_program_classify_resolved_call(check->program,
                (Space *)check->catalog->space, atom) == PETTA_RESOLVED_CALL_MACHINE_LOCAL) {
            /* Arguments reach the host only after their HE domain demand;
             * the host retains errors and the scalar conversion contract. */
            for (CettaExprIndex i = 1u; i < atom->expr.len; ++i)
                if (!petta_portable_executable(check, atom->expr.elems[i]))
                    return false;
            return true;
        }
        if (space_head_declares_type((Space *)check->catalog->space, atom_symbol_current_id(head))) {
            Arena scratch;
            arena_init(&scratch);
            Atom **types = NULL;
            uint32_t count = space_get_declared_types(
                (Space *)check->catalog->space, &scratch, head, &types);
            bool arrows = count > 0u;
            for (uint32_t i = 0u; arrows && i < count; ++i)
                arrows = types[i] && types[i]->kind == ATOM_EXPR &&
                    types[i]->expr.len >= 2u && atom_is_symbol_id(
                        types[i]->expr.elems[0], g_builtin_syms.arrow);
            free(types);
            arena_free(&scratch);
            if (!arrows) return false;
        }
        if (petta_program_head_is_intrinsic(atom_symbol_current_id(head)) ||
            is_grounded_op(atom_symbol_current_id(head)) ||
            petta_semantics_form(atom_symbol_current_id(head)) != PETTA_FORM_NONE ||
            petta_semantics_is_open_cons_value(atom) ||
            petta_semantics_is_cons_constraint(atom))
            return false;
        if (exact && !petta_portable_relation_check(
                check, atom_symbol_current_id(head), atom->expr.len - 1u))
            return false;
        for (CettaExprIndex i = 1u; i < atom->expr.len; ++i)
            if (!petta_portable_executable(check, atom->expr.elems[i]))
                return false;
        return true;
    }
    if (exact)
        return petta_portable_relation_check(
            check, atom_symbol_current_id(head), atom->expr.len - 1u);
    return petta_portable_structural_data(check, atom);
}

static bool petta_portable_lhs_argument(
        PettaPortableExecutionCheck *check, Atom *atom) {
    if (!check || !atom)
        return false;
    if (atom->kind != ATOM_EXPR)
        return true;
    if (atom->expr.len == 0u)
        return true;
    Atom *head = atom->expr.elems[0];
    if (check->eager_arguments && head && head->kind != ATOM_SYMBOL)
        return petta_portable_structural_data(check, atom);
    if (!head || head->kind != ATOM_SYMBOL ||
        petta_semantics_is_open_cons_value(atom) ||
        petta_semantics_is_cons_constraint(atom) ||
        petta_program_head_is_intrinsic(atom_symbol_current_id(head)) ||
        is_grounded_op(atom_symbol_current_id(head))) {
        return false;
    }
    bool exact = false;
    bool open = false;
    if (!petta_portable_relation_presence(
            check, atom_symbol_current_id(head), atom->expr.len - 1u,
            &exact, &open) || exact || open) {
        return false;
    }
    for (CettaExprIndex index = 1u;
         index < atom->expr.len; index++) {
        if (!petta_portable_lhs_argument(
                check, atom->expr.elems[index])) {
            return false;
        }
    }
    return true;
}

static PettaPortableRelation *petta_portable_relation_slot(
        PettaPortableExecutionCheck *check,
        SymbolId head, CettaExprLen arity) {
    if (!check || head == SYMBOL_ID_NONE)
        return NULL;
    for (size_t index = 0u;
         index < check->relation_len; index++) {
        if (check->relations[index].head == head &&
            check->relations[index].arity == arity) {
            return &check->relations[index];
        }
    }
    if (!petta_program_reserve(
            (void **)&check->relations,
            &check->relation_cap,
            check->relation_len + 1u,
            sizeof(*check->relations))) {
        return NULL;
    }
    PettaPortableRelation *slot =
        &check->relations[check->relation_len++];
    *slot = (PettaPortableRelation){
        .head = head,
        .arity = arity,
        .state = PETTA_PORTABLE_RELATION_VISITING,
    };
    return slot;
}

static bool petta_portable_relation_check(
        PettaPortableExecutionCheck *check,
        SymbolId head, CettaExprLen arity) {
    if (!check || !check->catalog || head == SYMBOL_ID_NONE)
        return false;
    for (size_t index = 0u;
         index < check->relation_len; index++) {
        PettaPortableRelation *known = &check->relations[index];
        if (known->head != head || known->arity != arity)
            continue;
        return known->state != PETTA_PORTABLE_RELATION_REJECTED;
    }
    PettaPortableRelation *slot =
        petta_portable_relation_slot(check, head, arity);
    if (!slot)
        return false;
    size_t slot_index = (size_t)(slot - check->relations);

    bool saw_equation = false;
    bool accepted = true;
    for (size_t index = 0u;
         accepted && index < check->catalog->equation_len; index++) {
        Atom *lhs = NULL;
        Atom *rhs = NULL;
        if (!petta_rule_syntax_view(
                check->catalog->equations[index].equation,
                CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL, true,
                &lhs, &rhs, NULL) ||
            !lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
            lhs->expr.len - 1u != arity) {
            continue;
        }
        Atom *lhs_head = lhs->expr.elems[0];
        if (!lhs_head || lhs_head->kind != ATOM_SYMBOL) {
            accepted = false;
            break;
        }
        if (atom_symbol_current_id(lhs_head) != head)
            continue;
        saw_equation = true;
        if (!petta_program_is_equation(check->catalog->equations[index].equation)) {
            accepted = false;
            break;
        }
        for (CettaExprIndex argument = 1u;
             accepted && argument < lhs->expr.len; argument++) {
            accepted = petta_portable_lhs_argument(
                check, lhs->expr.elems[argument]);
        }
        if (accepted)
            accepted = petta_portable_executable(check, rhs);
    }
    check->relations[slot_index].state = accepted && saw_equation
        ? PETTA_PORTABLE_RELATION_ACCEPTED
        : PETTA_PORTABLE_RELATION_REJECTED;
    return check->relations[slot_index].state ==
        PETTA_PORTABLE_RELATION_ACCEPTED;
}

CettaRelationalExecutionClass
petta_program_relational_execution_class(
        PettaProgram *program, Space *space, Atom *call) {
    CETTA_SCOPED_SHARED_TRANSITION(program_relational_class);
    if (!program || !space || !call ||
        call->kind != ATOM_EXPR || call->expr.len == 0u ||
        !call->expr.elems[0] ||
        call->expr.elems[0]->kind != ATOM_SYMBOL) {
        return CETTA_RELATIONAL_EXECUTION_UNQUALIFIED;
    }
    PettaProgramSpace *catalog =
        petta_program_find_space(program, space);
    if (!catalog || !catalog->synchronized_snapshot ||
        catalog->synchronized_revision != space_revision(space)) {
        return CETTA_RELATIONAL_EXECUTION_UNQUALIFIED;
    }
    PettaPortableExecutionCheck check = {
        .catalog = catalog,
    };
    bool accepted = true;
    for (CettaExprIndex argument = 1u;
         accepted && argument < call->expr.len; argument++) {
        accepted = petta_portable_structural_data(
            &check, call->expr.elems[argument]);
    }
    if (accepted) {
        accepted = petta_portable_relation_check(
            &check, atom_symbol_current_id(call->expr.elems[0]),
            call->expr.len - 1u);
    }
    free(check.relations);
    return accepted
        ? CETTA_RELATIONAL_EXECUTION_STRUCTURAL_EQUATIONS_V1
        : CETTA_RELATIONAL_EXECUTION_UNQUALIFIED;
}

bool petta_program_native_eager_equations(
        PettaProgram *program, Space *space, Atom *expression) {
    CETTA_SCOPED_SHARED_TRANSITION(program_eager_equations);
    if (!program || !space || !expression)
        return false;
    PettaProgramSpace *catalog = petta_program_find_space(program, space);
    if (!catalog || !catalog->synchronized_snapshot ||
        catalog->synchronized_revision != space_revision(space))
        return false;
    if (!space_type_annotations_have_only_symbol_subjects(space))
        return false;
    PettaPortableExecutionCheck check = {
        .program = program, .catalog = catalog, .eager_arguments = true,
    };
    bool accepted = petta_portable_executable(&check, expression);
    free(check.relations);
    return accepted;
}

bool petta_program_capture_projections(PettaProgram *program,
        SpaceProgramToken **reads, size_t *length) {
    CETTA_SCOPED_SHARED_TRANSITION(program_projection_capture);
    if (!program || !reads || !length)
        return false;
    *reads = NULL;
    *length = 0u;
    Space **spaces = NULL;
    size_t count = 0u;
    if (!petta_program_private_spaces(program, &spaces, &count))
        return false;
    SpaceProgramToken *tokens = count && count <= SIZE_MAX / sizeof(*tokens)
        ? calloc(count, sizeof(*tokens)) : NULL;
    if (count && !tokens) {
        free(spaces);
        return false;
    }
    for (size_t i = 0u; i < count; i++)
        tokens[i] = space_program_token(spaces[i]);
    free(spaces);
    *reads = tokens;
    *length = count;
    return true;
}

bool petta_program_registered_space_token(PettaProgram *program,
        const Space *identity, uint64_t instance, SpaceProgramToken *token) {
    CETTA_SCOPED_SHARED_TRANSITION(program_projection_lookup);
    if (!program || !identity || !instance || !token)
        return false;
    for (size_t i = 0u; i < program->space_len; i++) {
        const PettaProgramSpace *entry = &program->spaces[i];
        if (entry->space == identity && entry->instance_id == instance) {
            *token = space_program_token(entry->space);
            return token->instance_id == instance;
        }
    }
    for (size_t i = 0u; program->analysis && i < program->analysis->space_len; i++) {
        const PettaProgramAnalysisSpace *entry = &program->analysis->spaces[i];
        if (entry->space == identity && entry->instance_id == instance) {
            *token = space_program_token(entry->space);
            return token->instance_id == instance;
        }
    }
    return false;
}

void petta_program_forget_space(
    PettaProgram *program, const Space *space) {
    CETTA_SCOPED_SHARED_TRANSITION(program_catalog_forget);
    if (!program || !space)
        return;
    PettaTransactionCatalog *private = petta_program_private_catalog(program, space);
    if (private) {
        petta_program_space_dispose_catalog(&private->catalog);
        petta_program_space_free_pending(&private->catalog);
        private->catalog.synchronized_snapshot = false;
        petta_program_private_analysis_clear(&private->analysis);
        return;
    }
    for (size_t index = 0u;
         index < program->space_len; index++) {
        PettaProgramSpace *entry = &program->spaces[index];
        if (entry->space != space)
            continue;
        if (program->callability_cache_valid &&
            program->callability_cache_token_len == program->space_len &&
            !entry->equation_len && entry->synchronized_snapshot &&
            space_program_token_matches_live_space(entry->synchronized_program, space) &&
            space_equation_token_matches_live_space(
                program->callability_cache_tokens[index].equations, space)) {
            memmove(&program->callability_cache_tokens[index],
                &program->callability_cache_tokens[index + 1u],
                (program->space_len - index - 1u) *
                    sizeof(*program->callability_cache_tokens));
            program->callability_cache_token_len--;
        }
        petta_program_space_dispose_catalog(entry);
        petta_program_space_free_pending(entry);
        memmove(
            entry, entry + 1u,
            sizeof(*entry) *
                (program->space_len - index - 1u));
        program->space_len--;
        break;
    }
    if (!program->analysis)
        return;
    for (size_t index = 0u;
         index < program->analysis->space_len; index++) {
        PettaProgramAnalysisSpace *entry =
            &program->analysis->spaces[index];
        if (entry->space != space)
            continue;
        petta_program_space_clear_type_index(entry);
        free(entry->inferred_signatures);
        free(entry->type_annotations);
        memmove(
            entry, entry + 1u,
            sizeof(*entry) *
                (program->analysis->space_len - index - 1u));
        program->analysis->space_len--;
        break;
    }
}

bool petta_program_transfer_space_roles(PettaProgram *program,
        const Space *source, Space *destination) {
    CETTA_SCOPED_SHARED_TRANSITION(program_role_transport);
    if (!program || !source || !destination || source == destination ||
        space_length64(source) != space_length64(destination))
        return false;
    PettaProgramSpace *entry = petta_program_find_space(program, source);
    PettaProgramAnalysisSpace *analysis = petta_program_find_analysis_space(program, source);
    if (!entry && !analysis)
        return true;
    /* Adding the destination may move program->spaces. Retain the source
     * records while both syntax owners are alive, before allocating its entry. */
    size_t count = entry ? entry->equation_len : 0u;
    PettaProgramEquation *records = count ? calloc(count, sizeof(*records)) : NULL;
    bool *used = count ? calloc(count, sizeof(*used)) : NULL;
    if (count && (!records || !used)) {
        free(records);
        free(used);
        return false;
    }
    if (count)
        memcpy(records, entry->equations, count * sizeof(*records));
    bool ok = true;
    size_t next = 0u, copied = 0u;
    CettaCount length = space_length64(source);
    for (CettaIndex i = 0u; ok && i < length; i++) {
        Atom *old = space_get_at64(source, i);
        Atom *target = space_get_at64(destination, i);
        if (!old || !target) {
            ok = false;
            break;
        }
        if (petta_program_type_declaration_view(old, NULL, NULL)) {
            ok = petta_program_note_add(program, destination, target, NULL);
            continue;
        }
        size_t found = count;
        if (next < count && !used[next] && atom_eq(records[next].equation, old)) {
            found = next++;
        } else {
            for (size_t j = 0u; j < count; j++)
                if (!used[j] && atom_eq(records[j].equation, old)) {
                    found = j;
                    break;
                }
        }
        if (found == count)
            continue;
        Atom *execution = target->kind == ATOM_EXPR && target->expr.len != 3u
            ? space_equation_execution_view(&program->plans, target) : target;
        const PettaPlanNode *role = records[found].plan;
        const PettaPlanNode *rebound = role && execution
            ? petta_plan_rebind_frame_syntax(program, execution, role) : NULL;
        ok = execution && (!role || rebound) &&
            petta_program_note_add_with_execution_plan(
                program, destination, target, rebound, true);
        used[found] = true;
        copied++;
    }
    ok = ok && copied == count;
    free(records);
    free(used);
    if (!ok)
        petta_program_forget_space(program, destination);
    return ok;
}

static bool petta_program_copy_records(
    PettaProgram *program, const Space *source,
    Space *destination, bool replace) {
    CETTA_SCOPED_SHARED_TRANSITION(program_catalog_copy);
    if (!program || !source || !destination)
        return false;
    if (source == destination)
        return true;
    if (replace)
        petta_program_forget_space(program, destination);
    PettaProgramSpace *source_entry =
        petta_program_find_space(program, source);
    PettaProgramAnalysisSpace *source_analysis =
        petta_program_find_analysis_space(program, source);
    if ((!source_entry || source_entry->equation_len == 0u) &&
        (!source_analysis ||
         source_analysis->type_annotation_len == 0u)) {
        return true;
    }

    /*
     * ensure_space can reallocate program->spaces, invalidating the source
     * entry pointer.  Copy the records first, then install them.
     */
    size_t count = source_entry ? source_entry->equation_len : 0u;
    PettaProgramEquation *copy = count
        ? cetta_malloc(sizeof(*copy) * count) : NULL;
    if (count)
        memcpy(copy, source_entry->equations, sizeof(*copy) * count);
    size_t annotation_count = source_analysis
        ? source_analysis->type_annotation_len : 0u;
    Atom **annotation_copy = annotation_count
        ? cetta_malloc(sizeof(*annotation_copy) * annotation_count)
        : NULL;
    if (annotation_count) {
        memcpy(
            annotation_copy, source_analysis->type_annotations,
            sizeof(*annotation_copy) * annotation_count);
    }
    PettaProgramSpace *destination_entry = count
        ? petta_program_ensure_space(program, destination) : NULL;
    PettaProgramAnalysisSpace *destination_analysis =
        annotation_count
            ? petta_program_ensure_analysis_space(
                  program, destination)
            : NULL;
    if ((count && !destination_entry) ||
        (annotation_count && !destination_analysis)) {
        free(copy);
        free(annotation_copy);
        return false;
    }
    if (destination_entry) {
        petta_program_space_replace_catalog(
            destination_entry, copy, count);
    }
    if (destination_analysis) {
        petta_program_space_clear_type_index(destination_analysis);
        free(destination_analysis->inferred_signatures);
        free(destination_analysis->type_annotations);
        destination_analysis->inferred_signatures = NULL;
        destination_analysis->inferred_signature_len = 0u;
        destination_analysis->inferred_signature_cap = 0u;
        destination_analysis->inferred_signatures_valid = false;
        destination_analysis->inferred_signature_revision = 0u;
        destination_analysis->inferred_signature_authority =
            (CettaNikDirectAuthorityStampV1){0};
        destination_analysis->inferred_signature_authority_valid = false;
        destination_analysis->type_annotations = annotation_copy;
        destination_analysis->type_annotation_len = annotation_count;
        destination_analysis->type_annotation_cap = annotation_count;
        destination_analysis->type_index_dirty = true;
    }
    return true;
}

bool petta_program_clone_space(
    PettaProgram *program, const Space *source, Space *destination) {
    return petta_program_copy_records(
        program, source, destination, true);
}

bool petta_program_replace_space(
    PettaProgram *program, Space *destination, const Space *source) {
    return petta_program_copy_records(
        program, source, destination, true);
}

static bool petta_equation_lhs_matches(
    Atom *equation, SymbolId head) {
    Atom *lhs = NULL;
    if (!petta_rule_syntax_view(
            equation, CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL, true,
            &lhs, NULL, NULL)) {
        return false;
    }
    Atom *lhs_head = lhs->expr.elems[0];
    return lhs_head->kind != ATOM_SYMBOL ||
           atom_symbol_current_id(lhs_head) == head;
}

void petta_program_candidate_snapshot_lease_release(
        PettaCandidateSnapshotLease *lease) {
    if (!lease)
        return;
    petta_equation_storage_release(lease->storage);
    free(lease->owned_items);
    memset(lease, 0, sizeof(*lease));
}

bool petta_program_candidate_snapshot_lease_pin(PettaCandidateSnapshotLease *lease) {
    if (!lease || (lease->len && !lease->items) ||
        lease->len > SIZE_MAX / sizeof(*lease->items))
        return false;
    if (lease->storage || !lease->len)
        return true;
    PettaEquationCandidate *items = lease->owned_items;
    size_t capacity = lease->owned_capacity;
    if (items && capacity < lease->len) return false;
    if (!items) {
        capacity = lease->len;
        items = cetta_malloc(lease->len * sizeof(*items));
        memcpy(items, lease->items, lease->len * sizeof(*items));
    }
    lease->storage = petta_equation_storage_take(items, lease->len, capacity);
    /* A private host array has no cache owner to retire it later. Its full
     * observation is owned by this lease; narrower projections may compact
     * immediately when they escape into a suspended choice. */
    lease->storage->retired = true;
    lease->items = items;
    lease->owned_items = NULL;
    lease->owned_capacity = 0u;
    return true;
}

bool petta_program_candidate_snapshot_lease_clone(
        const PettaCandidateSnapshotLease *source, PettaCandidateSnapshotLease *out) {
    if (!source || !out || source == out ||
        (source->len && !source->items) ||
        source->len > SIZE_MAX / sizeof(*source->items))
        return false;
    *out = (PettaCandidateSnapshotLease){0};
    if (source->storage) {
        if (!petta_equation_storage_retain(source->storage))
            return false;
        *out = *source;
        return true;
    }
    out->items = source->items;
    out->len = source->len;
    return petta_program_candidate_snapshot_lease_pin(out);
}

static bool petta_program_candidate_snapshot_lease_from_entry(
    const PettaProgramSpace *entry,
    PettaProgramSpace *publication_authority,
    bool admit_local_execution_payload, unsigned admitted_forms, bool include_graded,
    Space *space, SymbolId head,
    PettaCandidateSnapshotLease *lease,
    PettaCandidateSnapshotStats *stats) {
    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (lease)
        memset(lease, 0, sizeof(*lease));
    if (!space || !lease) {
        return false;
    }
    if (stats)
        stats->snapshots = 1u;

    SpaceReadToken source = space_read_token(space);
    PettaProgramCandidateSnapshotKey key =
        petta_program_candidate_snapshot_key(space);
    if (!space_read_token_matches_live_space(source, space) ||
        !petta_program_candidate_snapshot_key_matches_live_space(key, space)) {
        return false;
    }
    const PettaProgramCandidateSnapshot *cached = publication_authority
        ? petta_program_space_find_candidate_snapshot(
              publication_authority, head, space)
        : NULL;
    if (cached) {
        /* The snapshot key is the equation projection and prefix epoch.
         * Data-only appends bump `revision` but leave that key intact, so
         * the cached bag is the pin: do not copy it to restamp read tokens. */
        if (!petta_equation_storage_retain(cached->storage))
            return false;
        lease->items = cached->candidates;
        lease->len = cached->len;
        lease->storage = cached->storage;
        if (stats) {
            stats->cache_hits = 1u;
            stats->candidates_emitted = cached->len;
        }
        return true;
    }
    typedef struct {
        Atom *equation;
        SpaceEquationOccurrenceId occurrence;
    } PettaLiveEquation;
    typedef struct {
        Atom *equation;
        size_t first;
        size_t last;
    } PettaLivePointerBucket;
    size_t actual_len = 0u;
    size_t actual_cap = 0u;
    PettaLiveEquation *actual = NULL;
    SpaceEquationCursor cursor;
    if (!space_rule_cursor_init_view(space, head, admitted_forms, include_graded, &cursor))
        return false;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        if (step == SPACE_EQUATION_CURSOR_INVALIDATED) {
            free(actual);
            return false;
        }
        SpaceEquationOccurrence occurrence;
        if (!space_rule_occurrence_resolve_view(id, admitted_forms, include_graded, &occurrence)) {
            free(actual);
            return false;
        }
        if (stats)
            stats->live_occurrences_scanned++;
        if (!petta_program_reserve(
                (void **)&actual, &actual_cap, actual_len + 1u,
                sizeof(*actual))) {
            free(actual);
            return false;
        }
        actual[actual_len++] = (PettaLiveEquation){
            .equation = occurrence.equation,
            .occurrence = occurrence.id,
        };
    }

    bool *used = actual_len
        ? cetta_malloc(sizeof(*used) * actual_len)
        : NULL;
    if (actual_len > 0u && !used) {
        free(actual);
        return false;
    }
    if (used)
        memset(used, 0, sizeof(*used) * actual_len);
    size_t pointer_bucket_cap = 0u;
    PettaLivePointerBucket *pointer_buckets = NULL;
    size_t *pointer_next = NULL;
    if (actual_len > 0u) {
        if (actual_len > SIZE_MAX / 2u) {
            free(used);
            free(actual);
            return false;
        }
        size_t needed = actual_len * 2u;
        pointer_bucket_cap = 16u;
        while (pointer_bucket_cap < needed) {
            if (pointer_bucket_cap > SIZE_MAX / 2u) {
                free(used);
                free(actual);
                return false;
            }
            pointer_bucket_cap *= 2u;
        }
        pointer_buckets = calloc(
            pointer_bucket_cap, sizeof(*pointer_buckets));
        pointer_next = malloc(sizeof(*pointer_next) * actual_len);
        if (!pointer_buckets || !pointer_next) {
            free(pointer_buckets);
            free(pointer_next);
            free(used);
            free(actual);
            return false;
        }
        for (size_t index = 0u; index < actual_len; index++) {
            pointer_next[index] = SIZE_MAX;
            uintptr_t key = (uintptr_t)actual[index].equation;
            key >>= 3u;
            key ^= key >> 17u;
            key *= (uintptr_t)0xed5ad4bbu;
            key ^= key >> 11u;
            size_t slot = (size_t)key & (pointer_bucket_cap - 1u);
            while (pointer_buckets[slot].equation &&
                   pointer_buckets[slot].equation !=
                       actual[index].equation) {
                slot = (slot + 1u) & (pointer_bucket_cap - 1u);
            }
            PettaLivePointerBucket *bucket = &pointer_buckets[slot];
            if (!bucket->equation) {
                bucket->equation = actual[index].equation;
                bucket->first = index;
                bucket->last = index;
            } else {
                pointer_next[bucket->last] = index;
                bucket->last = index;
            }
        }
    }
    size_t length = 0u;
    size_t capacity = 0u;
    PettaEquationCandidate *items = NULL;

    /*
     * The private record stream preserves PeTTa declaration order.  Its
     * derived head buckets select only records that can join the live
     * occurrence stream; exact and variable-head buckets are merged by source
     * position.  Space remains semantic authority: a selected record
     * contributes only when an equal live occurrence exists, and every
     * unmatched live occurrence is appended as an unplanned oracle fallback.
     * This remains coherent when an unordered native Space swaps storage
     * slots after removing an unrelated fact.  If the derived index cannot be
     * rebuilt, the complete record stream is the correctness fallback.
    */
    if (entry) {
        bool indexed = !entry->head_index_dirty ||
            (publication_authority &&
             petta_program_space_rebuild_head_index(
                 publication_authority));
        const PettaProgramHeadBucket *exact = indexed
            ? petta_program_space_find_head_bucket(entry, head)
            : NULL;
        const PettaProgramHeadBucket *wildcard = indexed
            ? petta_program_space_find_head_bucket(
                  entry, SYMBOL_ID_NONE)
            : NULL;
        size_t exact_position = 0u;
        size_t wildcard_position = 0u;
        size_t fallback_position = 0u;
        for (;;) {
            size_t record_index = SIZE_MAX;
            if (indexed) {
                size_t exact_index =
                    exact && exact_position < exact->len
                        ? exact->record_indices[exact_position]
                        : SIZE_MAX;
                size_t wildcard_index =
                    wildcard && wildcard_position < wildcard->len
                        ? wildcard->record_indices[wildcard_position]
                        : SIZE_MAX;
                if (exact_index == SIZE_MAX &&
                    wildcard_index == SIZE_MAX) {
                    break;
                }
                if (exact_index <= wildcard_index) {
                    record_index = exact_index;
                    exact_position++;
                } else {
                    record_index = wildcard_index;
                    wildcard_position++;
                }
            } else {
                if (fallback_position >= entry->equation_len)
                    break;
                record_index = fallback_position++;
            }
            if (record_index >= entry->equation_len)
                continue;
            const PettaProgramEquation *record =
                &entry->equations[record_index];
            if (stats)
                stats->declaration_records_examined++;
            if (!petta_equation_lhs_matches(
                    record->equation, head)) {
                continue;
            }
            size_t matched = SIZE_MAX;
            if (pointer_buckets) {
                uintptr_t key = (uintptr_t)record->equation;
                key >>= 3u;
                key ^= key >> 17u;
                key *= (uintptr_t)0xed5ad4bbu;
                key ^= key >> 11u;
                size_t slot =
                    (size_t)key & (pointer_bucket_cap - 1u);
                while (pointer_buckets[slot].equation &&
                       pointer_buckets[slot].equation !=
                           record->equation) {
                    slot = (slot + 1u) &
                        (pointer_bucket_cap - 1u);
                }
                PettaLivePointerBucket *bucket =
                    &pointer_buckets[slot];
                while (bucket->equation &&
                       bucket->first != SIZE_MAX &&
                       used[bucket->first]) {
                    bucket->first = pointer_next[bucket->first];
                }
                if (bucket->equation && bucket->first != SIZE_MAX) {
                    matched = bucket->first;
                    bucket->first = pointer_next[matched];
                    if (stats)
                        stats->pointer_identity_hits++;
                }
            }
            for (size_t index = 0u; index < actual_len; index++) {
                if (matched != SIZE_MAX)
                    break;
                if (used[index])
                    continue;
                if (stats)
                    stats->structural_equality_checks++;
                if (record->equation == actual[index].equation ||
                    atom_eq(record->equation,
                            actual[index].equation)) {
                    matched = index;
                    break;
                }
            }
            if (matched == SIZE_MAX) {
                for (size_t index = 0u;
                     index < actual_len; index++) {
                    if (used[index])
                        continue;
                    if (stats)
                        stats->alpha_equality_checks++;
                    if (atom_alpha_eq(
                            record->equation,
                            actual[index].equation)) {
                        matched = index;
                        break;
                    }
                }
            }
            if (matched == SIZE_MAX)
                continue;
            if (!petta_program_reserve(
                    (void **)&items, &capacity, length + 1u,
                    sizeof(*items))) {
                free(used);
                free(actual);
                free(pointer_buckets);
                free(pointer_next);
                free(items);
                return false;
            }
            used[matched] = true;
            items[length++] = (PettaEquationCandidate){
                .equation = actual[matched].equation,
                .source_root = record->source_root,
                .rhs_plan = admit_local_execution_payload
                    ? petta_plan_child(record->plan, 2u) : NULL,
                .equation_template_c0 = admit_local_execution_payload
                    ? record->equation_template_c0 : NULL,
                .equation_template = admit_local_execution_payload
                    ? record->equation_template : NULL,
                .activation_layout =
                    petta_equation_activation_layout(
                        admit_local_execution_payload && record->equation_template
                            ? record->equation_template->equation
                            : actual[matched].equation,
                        admit_local_execution_payload
                            ? record->static_variable_count : 0u),
                .occurrence = actual[matched].occurrence,
                .rhs_binds_variables = record->rhs_binds_variables,
            };
        }
    }
    for (size_t index = 0u; index < actual_len; index++) {
        if (used[index])
            continue;
        if (!petta_program_reserve(
                (void **)&items, &capacity, length + 1u,
                sizeof(*items))) {
            free(used);
            free(actual);
            free(pointer_buckets);
            free(pointer_next);
            free(items);
            return false;
        }
        uint64_t source_root = cetta_source_root_new();
        if (!source_root) {
            free(used);
            free(actual);
            free(pointer_buckets);
            free(pointer_next);
            free(items);
            return false;
        }
        items[length++] = (PettaEquationCandidate){
            .equation = actual[index].equation,
            .source_root = source_root,
            .activation_layout =
                petta_equation_activation_layout(
                    actual[index].equation, 0u),
            .occurrence = actual[index].occurrence,
        };
    }
    free(used);
    free(actual);
    free(pointer_buckets);
    free(pointer_next);
    if (stats)
        stats->candidates_emitted = length;
    if (publication_authority &&
        space_read_token_matches_live_space(source, space) &&
        petta_program_candidate_snapshot_key_matches_live_space(key, space) &&
        petta_program_space_store_candidate_snapshot_take(
            publication_authority, head, source, key, items, length, capacity)) {
        const PettaProgramCandidateSnapshot *stored =
            petta_program_space_find_candidate_snapshot(
                publication_authority, head, space);
        if (!stored)
            return false;
        if (!petta_equation_storage_retain(stored->storage))
            return false;
        lease->items = stored->candidates;
        lease->len = stored->len;
        lease->storage = stored->storage;
    } else {
        lease->items = items;
        lease->len = length;
        lease->owned_items = items;
        lease->owned_capacity = capacity;
    }
    return true;
}

bool petta_program_candidate_snapshot_lease_profiled(
    PettaProgram *program, Space *space, SymbolId head,
    PettaCandidateSnapshotLease *lease,
    PettaCandidateSnapshotStats *stats) {
    /* A live Space is the source of truth.  Shared mutable catalogs are
       optimization evidence, so concurrent workers may read only an
       explicitly captured immutable revision view. */
    PettaProgramSpace *entry =
        program && (!cetta_shared_transition_scope_active() ||
                    petta_program_space_is_private(program, space))
            ? petta_program_find_space(program, space)
            : NULL;
    if (!petta_program_refresh_callability(program, entry, head))
        return false;
    return petta_program_candidate_snapshot_lease_from_entry(
        entry, entry, true, petta_program_rule_forms(program), program && program->graded_equations, space, head, lease, stats);
}

bool petta_program_revision_source_live_lease(
        PettaProgram *program, const PettaProgramRevisionProjection *projection,
        Space *space, SymbolId head, PettaCandidateSnapshotLease *lease,
        PettaCandidateSnapshotStats *stats) {
    CETTA_SCOPED_SHARED_TRANSITION(live_source_catalog);
    if (!petta_program_revision_projection_targets_source(projection, space))
        return false;
    PettaProgramSpace *entry = petta_program_find_space(program, space);
    if (!petta_program_refresh_callability(program, entry, head)) return false;
    return petta_program_candidate_snapshot_lease_from_entry(entry, entry, true,
        petta_program_rule_forms(program), program && program->graded_equations,
        space, head, lease, stats);
}

bool petta_rule_snapshot_lease(Space *space, SymbolId head, unsigned admitted_forms,
    PettaCandidateSnapshotLease *lease) {
    return petta_program_candidate_snapshot_lease_from_entry(NULL, NULL, false,
        admitted_forms, false, space, head, lease, NULL);
}

bool petta_program_private_image_candidate_lease(
    PettaProgram *program, const PettaProgramRevisionProjection *projection,
    Space *source, Space *image, SymbolId head,
    PettaCandidateSnapshotLease *lease, PettaCandidateSnapshotStats *stats) {
    const PettaProgramSpace *entry = NULL;
    const PettaProgramSpace *local = NULL;
    if (projection) {
        /* Only an identity presentation licenses borrowing source plans.
         * An alpha-equivalent target alone does not transport pointer-bound
         * execution payloads into its term universe. */
        if (petta_program_revision_projection_is_source(projection, source))
            entry = &projection->view->catalog;
    } else if (!cetta_shared_transition_scope_active()) {
        entry = petta_program_find_space(program, source);
        local = source != image ? petta_program_find_space(program, image) : NULL;
    }
    /* An explicit add inside a serial transaction may already have compiled
     * an image-local occurrence. Retain that role too; neither record stream
     * grants permission to publish a derived catalog for this read. */
    PettaProgramSpace joined = {0};
    if (local && local->equation_len) {
        size_t base = entry ? entry->equation_len : 0u;
        if (local->equation_len > SIZE_MAX - base ||
            base + local->equation_len > SIZE_MAX / sizeof(*joined.equations))
            return false;
        joined.equation_len = base + local->equation_len;
        joined.equations = cetta_malloc(joined.equation_len * sizeof(*joined.equations));
        if (base)
            memcpy(joined.equations, entry->equations, base * sizeof(*joined.equations));
        memcpy(joined.equations + base, local->equations,
               local->equation_len * sizeof(*joined.equations));
        joined.head_index_dirty = true;
        entry = &joined;
    }
    bool ok = petta_program_candidate_snapshot_lease_from_entry(
        entry, NULL, true, petta_program_rule_forms(program),
        program && program->graded_equations, image, head, lease, stats);
    free(joined.equations);
    return ok;
}

bool petta_program_revision_view_equation_lease(
    const PettaProgramRevisionProjection *projection,
    Space *space, SymbolId head,
    PettaCandidateSnapshotLease *lease,
    PettaCandidateSnapshotStats *stats) {
    if (petta_program_revision_projection_current(
            projection, space)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_QUERY_COMMIT);
        return petta_program_candidate_snapshot_lease_from_entry(
            &projection->view->catalog, NULL, false, projection->view->admitted_rule_forms, projection->view->graded_equations,
            space, head, lease, stats);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_QUERY_STALE_FALLBACK);
    return petta_program_candidate_snapshot_lease_from_entry(
        NULL, NULL, false, projection && projection->view
            ? projection->view->admitted_rule_forms : CETTA_RULE_SYNTAX_ORDINARY,
        projection && projection->view && projection->view->graded_equations,
        space, head, lease, stats);
}

bool petta_program_revision_view_source_candidate_lease(
        const PettaProgramRevisionProjection *projection,
        Space *space, SymbolId head,
        PettaCandidateSnapshotLease *lease,
        PettaCandidateSnapshotStats *stats) {
    if (!petta_program_revision_projection_is_source(projection, space)) {
        if (lease)
            memset(lease, 0, sizeof(*lease));
        if (stats)
            memset(stats, 0, sizeof(*stats));
        return false;
    }
    return petta_program_candidate_snapshot_lease_from_entry(
        &projection->view->catalog, NULL, true, projection->view->admitted_rule_forms, projection->view->graded_equations,
        space, head, lease, stats);
}

bool petta_program_candidate_snapshot_profiled(
    PettaProgram *program, Space *space, SymbolId head,
    PettaEquationCandidate **candidates, size_t *candidate_count,
    PettaCandidateSnapshotStats *stats) {
    if (candidates)
        *candidates = NULL;
    if (candidate_count)
        *candidate_count = 0u;
    if (!candidates || !candidate_count)
        return false;
    PettaCandidateSnapshotLease lease = {0};
    if (!petta_program_candidate_snapshot_lease_profiled(
            program, space, head, &lease, stats)) {
        return false;
    }
    if (lease.len > SIZE_MAX / sizeof(**candidates)) {
        petta_program_candidate_snapshot_lease_release(&lease);
        return false;
    }
    PettaEquationCandidate *copy = lease.len
        ? cetta_malloc(sizeof(*copy) * lease.len)
        : NULL;
    if (lease.len) {
        memcpy(copy, lease.items, sizeof(*copy) * lease.len);
    }
    *candidates = copy;
    *candidate_count = lease.len;
    petta_program_candidate_snapshot_lease_release(&lease);
    return true;
}

bool petta_program_candidate_snapshot(
    PettaProgram *program, Space *space, SymbolId head,
    PettaEquationCandidate **candidates, size_t *candidate_count) {
    return petta_program_candidate_snapshot_profiled(
        program, space, head, candidates, candidate_count, NULL);
}

bool petta_program_equation_snapshot(
    PettaProgram *program, Space *space,
    Atom ***equations, size_t *equation_count) {
    CETTA_SCOPED_SHARED_TRANSITION(program_equation_snapshot);
    if (!program || !space || !equations || !equation_count)
        return false;
    *equations = NULL;
    *equation_count = 0u;
    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    if (!entry || entry->equation_len == 0u)
        return true;
    if (entry->equation_len > SIZE_MAX / sizeof(**equations))
        return false;
    Atom **copy = cetta_malloc(
        sizeof(*copy) * entry->equation_len);
    for (size_t index = 0u; index < entry->equation_len; index++)
        copy[index] = entry->equations[index].equation;
    *equations = copy;
    *equation_count = entry->equation_len;
    return true;
}

static size_t petta_program_inferred_signature_lower_bound(
    const PettaProgramAnalysisSpace *space, SymbolId head,
    CettaExprLen arity) {
    size_t low = 0u;
    size_t high = space ? space->inferred_signature_len : 0u;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        const PettaProgramInferredSignature *entry =
            &space->inferred_signatures[middle];
        if (entry->head < head ||
            (entry->head == head && entry->arity < arity)) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

static bool petta_program_inferred_signatures_current_internal(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority) {
    CETTA_SCOPED_SHARED_TRANSITION(program_signatures_current);
    PettaProgramAnalysisSpace *entry =
        petta_program_find_analysis_space(program, space);
    if (!entry || !entry->inferred_signatures_valid ||
        entry->inferred_signature_revision != space_revision(space)) {
        return false;
    }
    if (!authority)
        return !entry->inferred_signature_authority_valid;
    return entry->inferred_signature_authority_valid &&
           cetta_nik_direct_authority_stamp_v1_equal(
               &entry->inferred_signature_authority, authority);
}

bool petta_program_inferred_signatures_current(
    PettaProgram *program, Space *space) {
    return petta_program_inferred_signatures_current_internal(
        program, space, NULL);
}

bool petta_program_inferred_signatures_current_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority) {
    return authority &&
           petta_program_inferred_signatures_current_internal(
               program, space, authority);
}

static bool petta_program_inferred_signature_lookup_internal(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom **signature_out) {
    CETTA_SCOPED_SHARED_TRANSITION(program_signature_lookup);
        CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    if (signature_out)
        *signature_out = NULL;
    if (!signature_out || !arena || head == SYMBOL_ID_NONE ||
        !petta_program_inferred_signatures_current_internal(
            program, space, authority) ||
        !space->native.universe) {
        return false;
    }
    PettaProgramAnalysisSpace *entry =
        petta_program_find_analysis_space(program, space);
    size_t index = petta_program_inferred_signature_lower_bound(
        entry, head, arity);
    if (index >= entry->inferred_signature_len ||
        entry->inferred_signatures[index].head != head ||
        entry->inferred_signatures[index].arity != arity) {
        return false;
    }
    *signature_out = term_universe_copy_atom_epoch(
        space->native.universe, arena,
        entry->inferred_signatures[index].signature_id,
        cetta_frame_identity_scope_fresh(&frame_identity_scope));
    return *signature_out != NULL;
}

bool petta_program_inferred_signature_lookup(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom **signature_out) {
    return petta_program_inferred_signature_lookup_internal(
        program, space, NULL, head, arity, arena, signature_out);
}

bool petta_program_inferred_signature_lookup_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom **signature_out) {
    return authority &&
           petta_program_inferred_signature_lookup_internal(
               program, space, authority, head, arity,
               arena, signature_out);
}

static bool petta_program_inferred_signatures_lookup_internal(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom ***signatures_out, size_t *count_out) {
    CETTA_SCOPED_SHARED_TRANSITION(program_signatures_lookup);
    if (signatures_out)
        *signatures_out = NULL;
    if (count_out)
        *count_out = 0u;
    if (!signatures_out || !count_out || !arena ||
        head == SYMBOL_ID_NONE) {
        return false;
    }
    if (!petta_program_inferred_signatures_current_internal(
            program, space, authority) ||
        !space->native.universe) {
        return true;
    }
    PettaProgramAnalysisSpace *entry =
        petta_program_find_analysis_space(program, space);
    size_t first = petta_program_inferred_signature_lower_bound(
        entry, head, arity);
    size_t last = first;
    while (last < entry->inferred_signature_len &&
           entry->inferred_signatures[last].head == head &&
           entry->inferred_signatures[last].arity == arity) {
        last++;
    }
    size_t count = last - first;
    if (count == 0u)
        return true;
    if (count > SIZE_MAX / sizeof(**signatures_out))
        return false;
    Atom **copies = cetta_malloc(sizeof(*copies) * count);
    for (size_t index = 0u; index < count; index++) {
        CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
        copies[index] = term_universe_copy_atom_epoch(
            space->native.universe, arena,
            entry->inferred_signatures[first + index].signature_id,
            cetta_frame_identity_scope_fresh(&frame_identity_scope));
        if (!copies[index]) {
            free(copies);
            return false;
        }
    }
    *signatures_out = copies;
    *count_out = count;
    return true;
}

bool petta_program_inferred_signatures_lookup(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom ***signatures_out, size_t *count_out) {
    return petta_program_inferred_signatures_lookup_internal(
        program, space, NULL, head, arity, arena,
        signatures_out, count_out);
}

bool petta_program_inferred_signatures_lookup_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom ***signatures_out, size_t *count_out) {
    return authority &&
           petta_program_inferred_signatures_lookup_internal(
               program, space, authority, head, arity, arena,
               signatures_out, count_out);
}

static void petta_program_inferred_signatures_reset_internal(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority) {
    CETTA_SCOPED_SHARED_TRANSITION(program_signatures_reset);
    PettaProgramAnalysisSpace *entry =
        petta_program_ensure_analysis_space(program, space);
    if (!entry)
        return;
    entry->inferred_signature_len = 0u;
    entry->inferred_signature_revision = space_revision(space);
    entry->inferred_signatures_valid = true;
    entry->inferred_signature_authority = authority
        ? *authority : (CettaNikDirectAuthorityStampV1){0};
    entry->inferred_signature_authority_valid = authority != NULL;
}

void petta_program_inferred_signatures_reset(
    PettaProgram *program, Space *space) {
    petta_program_inferred_signatures_reset_internal(
        program, space, NULL);
}

void petta_program_inferred_signatures_reset_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority) {
    if (authority) {
        petta_program_inferred_signatures_reset_internal(
            program, space, authority);
    }
}

/* An inferred signature keeps its interned id from one statement to the
 * next, so the program lists the ids it keeps in a universe whenever that
 * universe collects. */
static void petta_program_signature_roots(const TermUniverse *universe,
                                          TermUniverseRootVisit visit,
                                          void *visit_context, void *ctx) {
    const PettaProgram *program = ctx;
    if (!program) return;
    for (size_t index = 0u; program->analysis && index < program->analysis->space_len; index++) {
        const PettaProgramAnalysisSpace *entry =
            &program->analysis->spaces[index];
        if (entry->signature_universe != universe)
            continue;
        for (size_t signature = 0u;
             signature < entry->inferred_signature_len; signature++)
            visit(visit_context,
                  entry->inferred_signatures[signature].signature_id);
    }
    /* Private catalogs can outlive their current stack scope while a
     * continuation is paused; their owner list is the lifetime authority. */
    for (const PettaTransactionCatalog *entry=program->private_catalogs;entry;entry=entry->next) {
        if (entry->analysis.signature_universe!=universe) continue;
        for (size_t i=0u;i<entry->analysis.inferred_signature_len;i++)
            visit(visit_context,entry->analysis.inferred_signatures[i].signature_id);
    }
}

static bool petta_program_list_signatures_in(PettaProgram *program,
                                             TermUniverse *universe) {
    for (size_t index = 0u; index < program->signature_universe_len;
         index++) {
        if (program->signature_universes[index] == universe)
            return true;
    }
    if (!petta_program_reserve(
            (void **)&program->signature_universes,
            &program->signature_universe_cap,
            program->signature_universe_len + 1u,
            sizeof(*program->signature_universes)) ||
        !term_universe_add_root_provider(
            universe, petta_program_signature_roots, program))
        return false;
    program->signature_universes[program->signature_universe_len++] =
        universe;
    return true;
}

static bool petta_program_inferred_signature_put_internal(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity, Atom *signature) {
    CETTA_SCOPED_SHARED_TRANSITION(program_signature_put);
    if (!program || !space || !space->native.universe ||
        head == SYMBOL_ID_NONE || !signature) {
        return false;
    }
    PettaProgramAnalysisSpace *entry =
        petta_program_ensure_analysis_space(program, space);
    if (!entry)
        return false;
    if (entry->inferred_signature_len > 0u &&
        !petta_program_inferred_signatures_current_internal(
            program, space, authority)) {
        return false;
    }
    if (!petta_program_list_signatures_in(program, space->native.universe))
        return false;
    entry->signature_universe = space->native.universe;
    AtomId signature_id = term_universe_store_atom_id(
        space->native.universe,
        space->native.universe->persistent_arena,
        signature);
    if (signature_id == CETTA_ATOM_ID_NONE)
        return false;
    size_t index = petta_program_inferred_signature_lower_bound(
        entry, head, arity);
    while (index < entry->inferred_signature_len &&
           entry->inferred_signatures[index].head == head &&
           entry->inferred_signatures[index].arity == arity) {
        index++;
    }
    if (!petta_program_reserve(
            (void **)&entry->inferred_signatures,
            &entry->inferred_signature_cap,
            entry->inferred_signature_len + 1u,
            sizeof(*entry->inferred_signatures))) {
        return false;
    }
    memmove(
        entry->inferred_signatures + index + 1u,
        entry->inferred_signatures + index,
        sizeof(*entry->inferred_signatures) *
            (entry->inferred_signature_len - index));
    entry->inferred_signatures[index] =
        (PettaProgramInferredSignature){
            .head = head,
            .arity = arity,
            .signature_id = signature_id,
        };
    entry->inferred_signature_len++;
    entry->inferred_signature_revision = space_revision(space);
    entry->inferred_signatures_valid = true;
    entry->inferred_signature_authority = authority
        ? *authority : (CettaNikDirectAuthorityStampV1){0};
    entry->inferred_signature_authority_valid = authority != NULL;
    return true;
}

bool petta_program_inferred_signature_put(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity, Atom *signature) {
    return petta_program_inferred_signature_put_internal(
        program, space, NULL, head, arity, signature);
}

bool petta_program_inferred_signature_put_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity, Atom *signature) {
    return authority &&
           petta_program_inferred_signature_put_internal(
               program, space, authority, head, arity, signature);
}

void petta_program_inferred_signature_remove_head(
    PettaProgram *program, Space *space, SymbolId head) {
    CETTA_SCOPED_SHARED_TRANSITION(program_signature_remove);
    PettaProgramAnalysisSpace *entry =
        petta_program_find_analysis_space(program, space);
    if (!entry || head == SYMBOL_ID_NONE)
        return;
    size_t write = 0u;
    for (size_t read = 0u;
         read < entry->inferred_signature_len; read++) {
        if (entry->inferred_signatures[read].head == head)
            continue;
        if (write != read) {
            entry->inferred_signatures[write] =
                entry->inferred_signatures[read];
        }
        write++;
    }
    entry->inferred_signature_len = write;
}

static void petta_program_inferred_signatures_rebase_internal(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority) {
    CETTA_SCOPED_SHARED_TRANSITION(program_signatures_rebase);
    PettaProgramAnalysisSpace *entry =
        petta_program_find_analysis_space(program, space);
    if (!entry || !entry->inferred_signatures_valid ||
        (authority
             ? !entry->inferred_signature_authority_valid ||
                   !cetta_nik_direct_authority_stamp_v1_equal(
                       &entry->inferred_signature_authority, authority)
             : entry->inferred_signature_authority_valid)) {
        return;
    }
    entry->inferred_signature_revision = space_revision(space);
}

void petta_program_inferred_signatures_rebase(
    PettaProgram *program, Space *space) {
    petta_program_inferred_signatures_rebase_internal(
        program, space, NULL);
}

void petta_program_inferred_signatures_rebase_under_authority(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority) {
    if (authority) {
        petta_program_inferred_signatures_rebase_internal(
            program, space, authority);
    }
}

bool petta_program_type_annotation_snapshot(
    PettaProgram *program, Atom ***annotations_out, size_t *count_out) {
    CETTA_SCOPED_SHARED_TRANSITION(program_annotations_snapshot);
    if (annotations_out)
        *annotations_out = NULL;
    if (count_out)
        *count_out = 0u;
    if (!annotations_out || !count_out)
        return false;
    if (!program || !program->analysis)
        return true;
    bool private = petta_program_has_private_images(program);
    __attribute__((cleanup(petta_program_private_spaces_free)))
    Space **private_spaces = NULL;
    size_t private_count = 0u;
    if (private && !petta_program_private_spaces(program, &private_spaces, &private_count))
        return false;
    Atom **annotations = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    for (size_t space_index = 0u;
         space_index < (private ? private_count : program->analysis->space_len); space_index++) {
        PettaProgramAnalysisSpace *entry = private
            ? petta_program_find_analysis_space(program, private_spaces[space_index])
            : &program->analysis->spaces[space_index];
        if (!entry) continue;
        for (size_t annotation_index = 0u;
             annotation_index < entry->type_annotation_len;
             annotation_index++) {
            Atom *annotation = entry->type_annotations[annotation_index];
            if (!annotation || annotation->kind != ATOM_EXPR ||
                annotation->expr.len != 3u)
                continue;
            if (!petta_program_reserve(
                    (void **)&annotations, &capacity, count + 1u,
                    sizeof(*annotations))) {
                free(annotations);
                return false;
            }
            annotations[count++] = annotation;
        }
    }
    *annotations_out = annotations;
    *count_out = count;
    return true;
}

uint32_t petta_program_declared_types(
    PettaProgram *program, Atom *subject,
    Arena *arena, Atom ***types_out) {
    CETTA_SCOPED_SHARED_TRANSITION(program_declared_types);
    if (types_out)
        *types_out = NULL;
    if (!program || !program->analysis || !subject ||
        subject->kind != ATOM_SYMBOL ||
        !arena || !types_out)
        return 0u;
    bool private = petta_program_has_private_images(program);
    __attribute__((cleanup(petta_program_private_spaces_free)))
    Space **private_spaces = NULL;
    size_t private_count = 0u;
    if (private && !petta_program_private_spaces(program, &private_spaces, &private_count))
        return 0u;
    Atom **types = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    for (size_t space_index = 0u;
         space_index < (private ? private_count : program->analysis->space_len); space_index++) {
        PettaProgramAnalysisSpace *entry = private
            ? petta_program_find_analysis_space(program, private_spaces[space_index])
            : &program->analysis->spaces[space_index];
        if (!entry) continue;
        if (entry->type_index_dirty &&
            !petta_program_space_rebuild_type_index(entry)) {
            free(types);
            return 0u;
        }
        PettaProgramTypeBucket *bucket =
            petta_program_type_bucket(
                entry, atom_symbol_current_id(subject), false);
        for (size_t type_index = 0u;
             bucket && type_index < bucket->len; type_index++) {
            Atom *type = bucket->types[type_index];
            bool duplicate = false;
            for (size_t prior = 0u; prior < count; prior++) {
                if (atom_alpha_eq(types[prior], type)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            if (!petta_program_reserve(
                    (void **)&types, &capacity, count + 1u,
                    sizeof(*types))) {
                free(types);
                return 0u;
            }
            Atom *fresh = cetta_instantiate_frame_syntax(arena, type);
            if (!fresh) {
                free(types);
                return 0u;
            }
            types[count++] = fresh;
        }
    }
    if (count > UINT32_MAX) {
        free(types);
        return 0u;
    }
    *types_out = types;
    return (uint32_t)count;
}

typedef struct {
    SymbolId head;
    CettaExprLen arity;
} PettaTableSafetyRelation;

typedef struct {
    Atom *atom;
    const PettaPlanNode *plan;
} PettaTableSafetyNode;

static bool petta_table_safety_relation_equal(
    PettaTableSafetyRelation left,
    PettaTableSafetyRelation right) {
    return left.head == right.head &&
           left.arity == right.arity;
}

static bool petta_table_safety_add_relation(
    PettaTableSafetyRelation **relations,
    size_t *length, size_t *capacity,
    SymbolId head, CettaExprLen arity) {
    if (!relations || !length || !capacity ||
        head == SYMBOL_ID_NONE) {
        return false;
    }
    PettaTableSafetyRelation key = {
        .head = head,
        .arity = arity,
    };
    for (size_t index = 0u; index < *length; index++) {
        if (petta_table_safety_relation_equal(
                (*relations)[index], key)) {
            return true;
        }
    }
    if (!petta_program_reserve(
            (void **)relations, capacity, *length + 1u,
            sizeof(**relations))) {
        return false;
    }
    (*relations)[(*length)++] = key;
    return true;
}

static bool petta_table_safety_push_node(
    PettaTableSafetyNode **nodes,
    size_t *length, size_t *capacity,
    Atom *atom, const PettaPlanNode *plan) {
    if (!nodes || !length || !capacity || !atom || !plan ||
        !petta_program_reserve(
            (void **)nodes, capacity, *length + 1u,
            sizeof(**nodes))) {
        return false;
    }
    (*nodes)[(*length)++] = (PettaTableSafetyNode){
        .atom = atom,
        .plan = plan,
    };
    return true;
}

/* `let*` binding patterns are match data, not calls.  Only each binding's
 * producer and the final body execute.  Following the generic plan tree
 * through the binding-list spine would grant pattern-shaped data ambient
 * effect authority and falsely classify ordinary destructuring as a
 * dynamic call. */
static bool petta_table_safety_push_let_star(
    PettaTableSafetyNode **nodes,
    size_t *length, size_t *capacity,
    Atom *atom, const PettaPlanNode *plan) {
    if (!nodes || !length || !capacity || !atom || !plan ||
        atom->kind != ATOM_EXPR || atom_authored_len(atom) != 3u ||
        plan->child_count != atom->expr.len) {
        return false;
    }

    Atom *bindings = atom->expr.elems[1];
    const PettaPlanNode *bindings_plan =
        petta_plan_child(plan, 1u);
    if (!bindings || bindings->kind != ATOM_EXPR ||
        !bindings_plan ||
        bindings_plan->child_count != bindings->expr.len ||
        !petta_table_safety_push_node(
            nodes, length, capacity,
            atom->expr.elems[2], petta_plan_child(plan, 2u))) {
        return false;
    }

    for (CettaExprIndex index = 0u;
         index < bindings->expr.len; index++) {
        Atom *binding = bindings->expr.elems[index];
        const PettaPlanNode *binding_plan =
            petta_plan_child(bindings_plan, index);
        if (!binding || binding->kind != ATOM_EXPR ||
            atom_authored_len(binding) != 2u || !binding_plan ||
            binding_plan->child_count != binding->expr.len ||
            !petta_table_safety_push_node(
                nodes, length, capacity,
                binding->expr.elems[1],
                petta_plan_child(binding_plan, 1u))) {
            return false;
        }
    }
    return true;
}

/* A case pattern is match data.  Only the scrutinee and each branch result
 * execute, so following the generic plan through a pattern would invent a
 * dynamic call whenever a pattern has a variable in function position. */
static bool petta_table_safety_push_case(
    PettaTableSafetyNode **nodes,
    size_t *length, size_t *capacity,
    Atom *atom, const PettaPlanNode *plan) {
    if (!nodes || !length || !capacity || !atom || !plan ||
        atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
        plan->child_count != atom->expr.len)
        return false;
    CettaControlOperationPlan control = plan->computation.control;
    CettaExprLen arity = atom->expr.len - 1u;
    if (!cetta_control_operation_match_branches_layout(control, arity))
        return false;
    CettaExprIndex key_child = (CettaExprIndex)control.source + 1u;
    CettaExprIndex arms_child = (CettaExprIndex)control.first + 1u;
    if (!petta_table_safety_push_node(nodes, length, capacity,
            atom->expr.elems[key_child], petta_plan_child(plan, key_child)))
        return false;
    Atom *branches = atom->expr.elems[arms_child];
    const PettaPlanNode *branches_plan =
        petta_plan_child(plan, arms_child);
    if (!branches || branches->kind != ATOM_EXPR ||
        !branches_plan ||
        branches_plan->child_count != branches->expr.len) {
        return false;
    }
    for (CettaExprIndex index = 0u;
         index < branches->expr.len; index++) {
        Atom *branch = branches->expr.elems[index];
        const PettaPlanNode *branch_plan =
            petta_plan_child(branches_plan, index);
        if (!branch || branch->kind != ATOM_EXPR ||
            atom_authored_len(branch) != 2u || !branch_plan ||
            branch_plan->child_count != branch->expr.len ||
            !petta_table_safety_push_node(
                nodes, length, capacity,
                branch->expr.elems[1],
                petta_plan_child(branch_plan, 1u))) {
            return false;
        }
    }
    return true;
}

/* A binding pattern is data, just like a case pattern. Following it as an
 * expression invents a dynamic call for ordinary destructuring binders. */
static bool petta_table_safety_push_binding(
    PettaTableSafetyNode **nodes, size_t *length, size_t *capacity,
    Atom *atom, const PettaPlanNode *plan, PeTTaForm form) {
    if (!atom || !plan || atom->kind != ATOM_EXPR || atom->expr.len != 4u ||
        plan->child_count != atom->expr.len)
        return false;
    CettaExprIndex value_index = form == PETTA_FORM_CHAIN ? 1u : 2u;
    return petta_table_safety_push_node(nodes, length, capacity,
               atom->expr.elems[value_index], petta_plan_child(plan, value_index)) &&
           petta_table_safety_push_node(nodes, length, capacity,
               atom->expr.elems[3u], petta_plan_child(plan, 3u));
}

/*
 * These forms are pure provided every executable child is pure.  Forms
 * which invoke an argument as a callable (map/fold/forall), perform I/O or
 * mutation, cross an FFI boundary, or alter search commitment are excluded.
 * Exclusion refuses this conservative classification; it does not prove that
 * the call has effects. Tabling and contextual activation consume the result
 * under their own admission rules. Ordinary evaluation remains available.
 */
static bool petta_table_safety_form_is_pure(
    PeTTaForm form, bool *opaque) {
    if (opaque)
        *opaque = false;
    switch (form) {
    case PETTA_FORM_IF:
    case PETTA_FORM_AND_THEN:
    case PETTA_FORM_OR_ELSE:
    case PETTA_FORM_PROGN:
    case PETTA_FORM_PROG1:
    case PETTA_FORM_ID:
    case PETTA_FORM_APPEND:
    case PETTA_FORM_CONS:
    case PETTA_FORM_INT_ADD:
    case PETTA_FORM_STREAM_UNIQUE:
    case PETTA_FORM_STREAM_ALPHA_UNIQUE:
    case PETTA_FORM_STREAM_UNION:
    case PETTA_FORM_STREAM_INTERSECTION:
    case PETTA_FORM_STREAM_SUBTRACTION:
    case PETTA_FORM_LENGTH:
    case PETTA_FORM_MSORT:
    case PETTA_FORM_FIRST_FROM_PAIR:
    case PETTA_FORM_SECOND_FROM_PAIR:
    case PETTA_FORM_IS_VAR:
    case PETTA_FORM_IS_GROUND:
    case PETTA_FORM_IS_EXPR:
    case PETTA_FORM_IS_SPACE:
    case PETTA_FORM_IS_MEMBER:
    case PETTA_FORM_IS_ALPHA_MEMBER:
    case PETTA_FORM_ALPHA_UNIQUE:
    case PETTA_FORM_LIST_TO_SET:
    case PETTA_FORM_EXCLUDE_ITEM:
    case PETTA_FORM_REPRA:
    case PETTA_FORM_SREAD:
    case PETTA_FORM_LET:
    case PETTA_FORM_CHAIN:
        return true;
    case PETTA_FORM_LAMBDA:
        if (opaque)
            *opaque = true;
        return true;
    case PETTA_FORM_NONE:
    case PETTA_FORM_VALUE_LET:
    case PETTA_FORM_TEST:
    case PETTA_FORM_TRACE:
    case PETTA_FORM_FOLDALL:
    case PETTA_FORM_FORALL:
    case PETTA_FORM_MAPLIST:
    case PETTA_FORM_MAP_ATOM:
    case PETTA_FORM_FOLDL:
    case PETTA_FORM_BIND_STATE:
    case PETTA_FORM_GET_STATE:
    case PETTA_FORM_CHANGE_STATE:
    case PETTA_FORM_NEW_STATE:
    case PETTA_FORM_CALL:
    case PETTA_FORM_EVAL:
    case PETTA_FORM_REDUCE:
    case PETTA_FORM_PREDICATE:
    case PETTA_FORM_TRANSLATE_PREDICATE:
    case PETTA_FORM_IMPORT_PROLOG_FUNCTION:
    case PETTA_FORM_PROCESS_METTA_STRING:
    case PETTA_FORM_CALL_PREDICATE:
    case PETTA_FORM_ASSERTA_PREDICATE:
    case PETTA_FORM_ASSERTZ_PREDICATE:
    case PETTA_FORM_RETRACT_PREDICATE:
    case PETTA_FORM_TABLED:
    case PETTA_FORM_ADD_TRANSLATOR_RULE:
    case PETTA_FORM_REMOVE_TRANSLATOR_RULE:
    case PETTA_FORM_CUT:
    case PETTA_FORM_CATCH:
        return false;
    }
    return false;
}

static bool petta_table_safety_primitive(
    SymbolId head, CettaExprLen arity, uint64_t native_observers, bool *opaque) {
    if (opaque)
        *opaque = false;
    if (head == SYMBOL_ID_NONE)
        return false;
    if (grounded_op_is_type_pure(head))
        return true;
    if ((native_observers & CETTA_NATIVE_IMPORT_BINARY) &&
        cetta_binary_total_observer(head, arity))
        return true;
    CettaGsltPreparedPureIntrinsicInstruction instruction;
    if (cetta_gslt_prepared_intrinsic_program(head, arity, CETTA_GSLT_INTRINSIC_DIALECT_PETTA, NULL,
                                             &instruction, &g_builtin_syms) &&
        (instruction == CETTA_GSLT_PREPARED_PURE_INTRINSIC_DECONSTRUCT_NONEMPTY_EXPRESSION ||
         instruction == CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONSTRUCT_EXPRESSION_CONS ||
         instruction == CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONCATENATE_EXPRESSIONS))
        return true;

    PeTTaForm form = petta_semantics_form(head);
    if (form != PETTA_FORM_NONE)
        return petta_table_safety_form_is_pure(
            form, opaque);

    /* Typed-data constructors evaluate each field through its authored plan,
     * so purity depends on those children. */
    if (head == g_builtin_syms.colon ||
        head == g_builtin_syms.arrow)
        return true;
    /* Prime's `lift` builds code from code without running it; the machine
     * hands it to the evaluator whole (cetta_prime_lift_head). */
    if (head == g_builtin_syms.quote ||
        head == g_builtin_syms.return_text ||
        cetta_prime_lift_head(head) ||
        cetta_prime_scope_declaration_head(head)) {
        if (opaque)
            *opaque = true;
        return true;
    }
    if (head == g_builtin_syms.case_text ||
        head == g_builtin_syms.superpose ||
        head == g_builtin_syms.hyperpose ||
        head == g_builtin_syms.collapse ||
        head == g_builtin_syms.reify ||
        head == g_builtin_syms.once ||
        head == g_builtin_syms.match ||
        head == g_builtin_syms.let_star ||
        head == g_builtin_syms.unify) {
        return true;
    }

    const PettaIntrinsicNameIds *ids = petta_intrinsic_name_ids();
    return ids->table &&
           (head == ids->empty ||
            head == ids->member ||
            head == ids->last ||
            head == ids->reverse ||
            head == ids->min ||
            head == ids->max);
}

/* A trace request is process-constant.  Read the environment once per
 * thread rather than on every relation-safety classification, which runs
 * once per space query. */
static bool petta_table_safety_trace_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0)
        enabled = getenv("CETTA_PETTA_TABLE_SAFETY_TRACE") != NULL;
    return enabled != 0;
}

/* `transient_out` is raised when an UNSAFE answer does not follow from the
 * program itself: an equation whose plan is not built yet, or an allocation
 * failure.  Such an answer must not be memoized under the program token. */
static PettaRelationSafety petta_table_safety_scan_relation(
    PettaProgram *program, Space *space,
    PettaTableSafetyRelation relation,
    uint64_t native_observers,
    PettaTableSafetyRelation **relations,
    size_t *relation_len, size_t *relation_cap,
    bool *transient_out) {
    PettaCandidateSnapshotLease lease = {0};
    bool acquired = false;
    if (cetta_shared_transition_scope_active()) {
        /* The relation check holds the shared transition guard. A catalog
         * view may lend plans only to its own still-current source and only
         * while the callability domain used to build those plans is current. */
        PettaProgramSpace *entry = petta_program_find_space(program, space);
        PettaProgramRevisionView *view = entry ? entry->revision_view : NULL;
        if (petta_program_revision_view_source_plans_current_locked(
                program, space, entry, view)) {
            PettaProgramRevisionProjection projection = {
                .view = view, .target = view->source_equation_token,
            };
            acquired = petta_program_revision_view_source_candidate_lease(
                &projection, space, relation.head, &lease, NULL);
        }
    } else {
        acquired = petta_program_candidate_snapshot_lease_profiled(
            program, space, relation.head, &lease, NULL);
    }
    if (!acquired) {
        if (transient_out)
            *transient_out = true;
        return PETTA_RELATION_SAFETY_UNSAFE;
    }
    const PettaEquationCandidate *candidates = lease.items;
    size_t candidate_count = lease.len;

    PettaTableSafetyNode *nodes = NULL;
    size_t node_len = 0u;
    size_t node_cap = 0u;
    bool saw_matching_equation = false;
    bool safe = true;
    bool guarded_dynamic = false;
    bool trace = petta_table_safety_trace_enabled();
    for (size_t index = 0u;
         safe && index < candidate_count; index++) {
        Atom *lhs = NULL;
        Atom *rhs = NULL;
        if (candidates[index].activation_layout.binding != CETTA_RULE_BIND_UNIFY) {
            safe = false;
            break;
        }
        if (!petta_equation_view(
                candidates[index].equation,
                &lhs, &rhs, NULL) ||
            !lhs || lhs->expr.len == 0u ||
            lhs->expr.len - 1u != relation.arity) {
            continue;
        }
        Atom *lhs_head = lhs->expr.elems[0];
        if (lhs_head->kind == ATOM_SYMBOL &&
            atom_symbol_current_id(lhs_head) != relation.head) {
            continue;
        }
        saw_matching_equation = true;
        if (candidates[index].equation->expr.len != 3u) {
            safe = false;
            break;
        }
        safe = petta_table_safety_push_node(
            &nodes, &node_len, &node_cap,
            rhs, candidates[index].rhs_plan);
        if (!safe && transient_out)
            *transient_out = true;
        if (!safe && trace) {
            fprintf(
                stderr,
                "[petta-table-safety] head=%s arity=%u "
                "missing-or-invalid-rhs-plan equation=%zu\n",
                symbol_bytes(g_symbols, relation.head),
                (unsigned)relation.arity, index);
        }
    }
    petta_program_candidate_snapshot_lease_release(&lease);

    while (safe && node_len > 0u) {
        PettaTableSafetyNode item = nodes[--node_len];
        Atom *atom = item.atom;
        const PettaPlanNode *plan = item.plan;
        if (plan->role == PETTA_PLAN_VALUE) {
            continue;
        }
        if (plan->role == PETTA_PLAN_DATA) {
            if (atom->kind != ATOM_EXPR ||
                plan->child_count != atom->expr.len) {
                safe = false;
                break;
            }
            for (CettaExprIndex child = 1u;
                 safe && child < atom->expr.len; child++) {
                safe = petta_table_safety_push_node(
                    &nodes, &node_len, &node_cap,
                    atom->expr.elems[child],
                    &plan->children[child]);
                if (!safe && transient_out)
                    *transient_out = true;
            }
            continue;
        }
        if (plan->role == PETTA_PLAN_DYNAMIC_CALL) {
            if (atom->kind != ATOM_EXPR ||
                atom->expr.len == 0u ||
                plan->child_count != atom->expr.len) {
                safe = false;
                if (trace) {
                    fprintf(
                        stderr,
                        "[petta-table-safety] head=%s arity=%u "
                        "malformed-dynamic-call\n",
                        symbol_bytes(g_symbols, relation.head),
                        (unsigned)relation.arity);
                }
                break;
            }
            guarded_dynamic = true;
            for (CettaExprIndex child = 1u;
                 safe && child < atom->expr.len; child++) {
                safe = petta_table_safety_push_node(
                    &nodes, &node_len, &node_cap,
                    atom->expr.elems[child],
                    &plan->children[child]);
                if (!safe && transient_out)
                    *transient_out = true;
            }
            continue;
        }
        if (atom->kind != ATOM_EXPR ||
            atom->expr.len == 0u ||
            atom->expr.elems[0]->kind != ATOM_SYMBOL) {
            safe = false;
            if (trace) {
                fprintf(
                    stderr,
                    "[petta-table-safety] head=%s arity=%u "
                    "malformed-static-call role=%u atom=",
                    symbol_bytes(g_symbols, relation.head),
                    (unsigned)relation.arity,
                    (unsigned)plan->role);
                atom_print(atom, stderr);
                fputc('\n', stderr);
            }
            break;
        }

        SymbolId call_head =
            atom_symbol_current_id(atom->expr.elems[0]);
        if (plan->computation.control.operation == CETTA_CONTROL_MATCH_BRANCHES) {
            safe = petta_table_safety_push_case(
                &nodes, &node_len, &node_cap,
                atom, plan);
            if (!safe && trace) {
                fprintf(
                    stderr,
                    "[petta-table-safety] head=%s arity=%u "
                    "malformed-case\n",
                    symbol_bytes(g_symbols, relation.head),
                    (unsigned)relation.arity);
            }
            continue;
        }
        if (call_head == g_builtin_syms.let_star) {
            safe = petta_table_safety_push_let_star(
                &nodes, &node_len, &node_cap,
                atom, plan);
            if (!safe && trace) {
                fprintf(
                    stderr,
                    "[petta-table-safety] head=%s arity=%u "
                    "malformed-let-star\n",
                    symbol_bytes(g_symbols, relation.head),
                    (unsigned)relation.arity);
            }
            continue;
        }
        PeTTaForm binding_form = petta_semantics_form(call_head);
        if (binding_form == PETTA_FORM_LET || binding_form == PETTA_FORM_CHAIN) {
            safe = petta_table_safety_push_binding(
                &nodes, &node_len, &node_cap, atom, plan, binding_form);
            continue;
        }
        bool opaque = false;
        if (petta_table_safety_primitive(
                call_head, atom->expr.len - 1u, native_observers, &opaque)) {
            if (opaque)
                continue;
            if (plan->child_count != atom->expr.len) {
                safe = false;
                if (trace) {
                    fprintf(
                        stderr,
                        "[petta-table-safety] head=%s arity=%u "
                        "plan-child-mismatch\n",
                        symbol_bytes(g_symbols, relation.head),
                        (unsigned)relation.arity);
                }
                break;
            }
            for (CettaExprIndex child = 1u;
                 safe && child < atom->expr.len; child++) {
                safe = petta_table_safety_push_node(
                    &nodes, &node_len, &node_cap,
                    atom->expr.elems[child],
                    &plan->children[child]);
                if (!safe && transient_out)
                    *transient_out = true;
            }
            continue;
        }

        if (petta_program_head_is_intrinsic(call_head) ||
            !petta_table_safety_add_relation(
                relations, relation_len, relation_cap,
                call_head, atom->expr.len - 1u)) {
            safe = false;
            if (trace) {
                fprintf(
                    stderr,
                    "[petta-table-safety] head=%s arity=%u "
                    "unsupported-call=%s\n",
                    symbol_bytes(g_symbols, relation.head),
                    (unsigned)relation.arity,
                    symbol_bytes(g_symbols, call_head));
            }
        }
    }
    free(nodes);
    if (trace && !saw_matching_equation) {
        fprintf(
            stderr,
            "[petta-table-safety] head=%s arity=%u no-equation\n",
            symbol_bytes(g_symbols, relation.head),
            (unsigned)relation.arity);
    }
    if (!safe || !saw_matching_equation)
        return PETTA_RELATION_SAFETY_UNSAFE;
    return guarded_dynamic
        ? PETTA_RELATION_SAFETY_GUARDED_DYNAMIC
        : PETTA_RELATION_SAFETY_STATIC;
}

static size_t petta_table_safety_cache_slot(
    const Space *space, SymbolId head, CettaExprLen arity) {
    uint64_t hash =
        space_instance_id(space) * UINT64_C(0x9e3779b97f4a7c15);
    hash ^= (uint64_t)head * UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= (uint64_t)arity * UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 32u;
    return ((size_t)hash &
            (PETTA_TABLE_SAFETY_CACHE_CAP / PETTA_TABLE_SAFETY_CACHE_WAYS - 1u)) *
           PETTA_TABLE_SAFETY_CACHE_WAYS;
}

PettaRelationSafety petta_program_relation_safety(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity) {
    if (!program || !space || head == SYMBOL_ID_NONE)
        return PETTA_RELATION_SAFETY_UNSAFE;

    /* Safety classification and its program-keyed cache entry form one
     * derived observation.  Concurrent evaluators may compute the same fact,
     * but the mutable cache has one publication authority until it is
     * replaced by an immutable once-published index. */
    CETTA_SCOPED_SHARED_TRANSITION(relation_safety_observation);

    /* Live plans can change when another Space introduces or removes a
     * callable head. Refresh on the owning thread; a worker with a stale
     * domain has no current plan certificate and must not reuse a verdict. */
    bool callability_current = true;
    if (program->refresh_callability) {
        callability_current = petta_program_callability_cache_current(program);
        if (!callability_current && !cetta_shared_transition_scope_active()) {
            PettaProgramSpace *entry = petta_program_find_space(program, space);
            if (!petta_program_refresh_callability(program, entry, head))
                return PETTA_RELATION_SAFETY_UNSAFE;
            callability_current = petta_program_callability_cache_current(program);
        }
        if (!callability_current && cetta_shared_transition_scope_active())
            return PETTA_RELATION_SAFETY_UNSAFE;
    }
    uint64_t callability_epoch = program->refresh_callability
        ? program->callability_epoch : 0u;

    size_t cache_slot =
        petta_table_safety_cache_slot(space, head, arity);
    SpaceProgramToken program_token = space_program_token(space);
    uint64_t native_observers = eval_current_native_observer_mask
        ? eval_current_native_observer_mask() : 0u;
    PettaTableSafetyCacheEntry *cached = NULL;
    for (size_t way = 0u; way < PETTA_TABLE_SAFETY_CACHE_WAYS; way++) {
        PettaTableSafetyCacheEntry *entry =
            &program->table_safety_cache[cache_slot + way];
        if (entry->occupied && entry->head == head && entry->arity == arity &&
            entry->program.space == program_token.space &&
            entry->program.instance_id == program_token.instance_id) {
            if (callability_current &&
                entry->callability_epoch == callability_epoch &&
                entry->admitted_rule_forms == petta_program_rule_forms(program) &&
                entry->graded_equations == program->graded_equations &&
                entry->native_observers == native_observers &&
                space_program_token_eq(entry->program, program_token)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PETTA_RELATION_SAFETY_CACHE_HIT);
                return entry->safety;
            }
            /* Reuse this key's obsolete observation; never keep two revisions
             * of it merely because another way happens to be empty. */
            cached = entry;
        } else if (!entry->occupied && !cached) {
            cached = entry;
        }
    }
    if (!cached) {
        uint8_t *next = &program->table_safety_next[
            cache_slot / PETTA_TABLE_SAFETY_CACHE_WAYS];
        cached = &program->table_safety_cache[cache_slot + *next];
        *next = (uint8_t)((*next + 1u) % PETTA_TABLE_SAFETY_CACHE_WAYS);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_RELATION_SAFETY_CACHE_MISS);

    PettaTableSafetyRelation *relations = NULL;
    size_t relation_len = 0u;
    size_t relation_cap = 0u;
    bool transient = !callability_current;
    bool safe = petta_table_safety_add_relation(
        &relations, &relation_len, &relation_cap,
        head, arity);
    if (!safe)
        transient = true;
    PettaRelationSafety safety = safe
        ? PETTA_RELATION_SAFETY_STATIC
        : PETTA_RELATION_SAFETY_UNSAFE;
    for (size_t index = 0u;
         safe && index < relation_len; index++) {
        PettaRelationSafety relation_safety =
            petta_table_safety_scan_relation(
            program, space, relations[index], native_observers,
            &relations, &relation_len, &relation_cap,
            &transient);
        safe = relation_safety != PETTA_RELATION_SAFETY_UNSAFE;
        if (safe &&
            relation_safety ==
                PETTA_RELATION_SAFETY_GUARDED_DYNAMIC) {
            safety = PETTA_RELATION_SAFETY_GUARDED_DYNAMIC;
        }
    }
    free(relations);
    if (!safe)
        safety = PETTA_RELATION_SAFETY_UNSAFE;

    if (petta_table_safety_trace_enabled()) {
        const char *classification =
            safety == PETTA_RELATION_SAFETY_STATIC
                ? "static"
                : safety == PETTA_RELATION_SAFETY_GUARDED_DYNAMIC
                    ? "guarded-dynamic"
                    : "unsafe";
        fprintf(
            stderr,
            "[petta-table-safety] head=%s arity=%u safety=%s\n",
            symbol_bytes(g_symbols, head), (unsigned)arity,
            classification);
    }

    if (!transient) {
        *cached = (PettaTableSafetyCacheEntry){
            .program = program_token,
            .head = head,
            .arity = arity,
            .native_observers = native_observers,
            .callability_epoch = callability_epoch,
            .admitted_rule_forms = petta_program_rule_forms(program),
            .graded_equations = program->graded_equations,
            .safety = safety,
            .occupied = true,
        };
    }
    return safety;
}

bool petta_program_relation_table_safe(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity) {
    return petta_program_relation_safety(
               program, space, head, arity) ==
           PETTA_RELATION_SAFETY_STATIC;
}

PettaResolvedCallClass petta_program_classify_resolved_call(
    PettaProgram *program, Space *space, Atom *call) {
    if (!program || !space || !call ||
        call->kind != ATOM_EXPR || call->expr.len == 0u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL) {
        return PETTA_RESOLVED_CALL_UNSAFE;
    }
    SymbolId head = atom_symbol_current_id(call->expr.elems[0]);
    bool opaque = false;
    uint64_t native_observers = cetta_binary_total_observer(head, call->expr.len - 1u) &&
        eval_current_native_observer_mask ? eval_current_native_observer_mask() : 0u;
    if (petta_table_safety_primitive(head, call->expr.len - 1u,
                                   native_observers, &opaque))
        return PETTA_RESOLVED_CALL_MACHINE_LOCAL;
    if (petta_program_head_is_intrinsic(head))
        return PETTA_RESOLVED_CALL_UNSAFE;
    if (!space_equations_may_match_known_head(space, head) &&
        (!(program->admitted_rule_forms & CETTA_RULE_SYNTAX_DIRECTIONAL) ||
         !space_directional_rules_may_match_known_head(space, head)))
        return PETTA_RESOLVED_CALL_MACHINE_LOCAL;
    return petta_program_relation_safety(
               program, space, head, call->expr.len - 1u) ==
                   PETTA_RELATION_SAFETY_UNSAFE
        ? PETTA_RESOLVED_CALL_UNSAFE
        : PETTA_RESOLVED_CALL_RELATION;
}

/* The program and every borrowed Space/Atom backing owner are held and
 * quiescent throughout this census. Plans/templates live in their owning
 * arenas; separately allocated indexes and reference-counted catalogs are
 * counted by allocation identity. Weak projection backlinks are not roots. */
static bool petta_program_storage_array(CettaStorageCensus *census, const void *array,
    size_t length, size_t capacity, size_t width) {
    if (length > capacity || (capacity && !array) || !width ||
        capacity > SIZE_MAX / width) {
        cetta_storage_refuse(census, "ProgramArrayShape");
        return false;
    }
    (void)cetta_storage_array(census, array, capacity, width, 0u);
    return !census->incomplete && !census->overflow;
}

static void petta_equation_storage_census(const PettaCandidateSnapshotStorage *storage,
        CettaStorageCensus *census) {
    if (!storage || !cetta_storage_object(census, storage, sizeof(*storage), CETTA_STORAGE_BLOCK))
        return;
    (void)petta_program_storage_array(census, storage->items, storage->len,
        storage->capacity, sizeof(*storage->items));
}

void petta_program_candidate_snapshot_lease_storage_census(
        const PettaCandidateSnapshotLease *lease, CettaStorageCensus *census) {
    if (!lease) return;
    if (lease->storage) {
        if (lease->owned_items || lease->owned_capacity || lease->items != lease->storage->items ||
            lease->len != lease->storage->len) {
            cetta_storage_refuse(census, "ProgramSnapshotShape");
            return;
        }
        petta_equation_storage_census(lease->storage, census);
    } else if (lease->owned_items) {
        if (lease->items != lease->owned_items) {
            cetta_storage_refuse(census, "ProgramSnapshotShape");
            return;
        }
        (void)petta_program_storage_array(census, lease->owned_items,
            lease->len, lease->owned_capacity, sizeof(*lease->owned_items));
    } else if (lease->len) {
        cetta_storage_refuse(census, "UnpinnedProgramSnapshot");
    }
}

void petta_program_equation_projection_storage_census(
        const PettaEquationProjection *projection, CettaStorageCensus *census) {
    if (!projection || !cetta_storage_object(census, projection,
            sizeof(*projection), CETTA_STORAGE_BLOCK)) return;
    if (projection->owned) {
        if (projection->source || projection->entries) {
            cetta_storage_refuse(census, "ProgramProjectionShape"); return;
        }
        (void)petta_program_storage_array(census, projection->owned,
            projection->len, projection->len, sizeof(*projection->owned));
    } else if (projection->source) {
        const PettaCandidateSnapshotStorage *source = projection->source;
        if (!projection->entries && (projection->first > source->len ||
                projection->len > source->len - projection->first)) {
            cetta_storage_refuse(census, "ProgramProjectionShape"); return;
        }
        if (projection->entries) {
            if (!petta_program_storage_array(census, projection->entries,
                    projection->len, projection->entries_capacity, sizeof(*projection->entries))) return;
            for (size_t i = 0u; i < projection->len; i++) {
                if (projection->entries[i].index >= source->len) {
                    cetta_storage_refuse(census, "ProgramProjectionShape"); return;
                }
            }
        }
        petta_equation_storage_census(source, census);
    } else {
        cetta_storage_refuse(census, "ProgramProjectionShape");
    }
}

static void petta_program_catalog_storage(const PettaProgramSpace *entry,
    CettaStorageCensus *census) {
    if (!entry || !cetta_storage_object(census, entry, 0u, CETTA_STORAGE_VISITED)) return;
    if (!petta_program_storage_array(census, entry->equations,
            entry->equation_len, entry->equation_cap, sizeof(*entry->equations)) ||
        !petta_program_storage_array(census, entry->plan_callability_epochs,
            entry->plan_callability_epoch_len, entry->plan_callability_epoch_cap,
            sizeof(*entry->plan_callability_epochs)) ||
        !petta_program_storage_array(census, entry->head_buckets,
            entry->head_bucket_len, entry->head_bucket_cap, sizeof(*entry->head_buckets)) ||
        !petta_program_storage_array(census, entry->pending_heads,
            entry->pending_head_len, entry->pending_head_cap, sizeof(*entry->pending_heads)) ||
        !petta_program_storage_array(census, entry->snapshots,
            entry->snapshot_len, entry->snapshot_cap, sizeof(*entry->snapshots)) ||
        !petta_program_storage_array(census, entry->type_families,
            entry->type_family_len, entry->type_family_cap, sizeof(*entry->type_families))) return;
    for (size_t i = 0u; i < entry->head_bucket_len; i++) {
        const PettaProgramHeadBucket *bucket = &entry->head_buckets[i];
        if (!petta_program_storage_array(census, bucket->record_indices,
                bucket->len, bucket->cap, sizeof(*bucket->record_indices))) return;
    }
    for (size_t i = 0u; i < entry->snapshot_len; i++) {
        const PettaProgramCandidateSnapshot *snapshot = &entry->snapshots[i];
        const PettaCandidateSnapshotStorage *storage = snapshot->storage;
        if (!storage || snapshot->candidates != storage->items || snapshot->len != storage->len) {
            cetta_storage_refuse(census, "ProgramSnapshotShape"); return;
        }
        petta_equation_storage_census(storage, census);
    }
    for (size_t i = 0u; i < entry->type_family_len; i++) {
        const PettaProgramTypeFamily *family = &entry->type_families[i];
        if (!petta_program_storage_array(census, family->calls,
                family->count, family->count, sizeof(*family->calls)) ||
            !petta_program_storage_array(census, family->arities,
                family->arity_len, family->arity_cap, sizeof(*family->arities))) return;
    }
    if (entry->type_families_current) arena_storage_census(&entry->type_family_storage, census);
    petta_program_revision_view_storage_census(entry->revision_view, census);
}

void petta_program_transaction_catalog_storage(
        const PettaTransactionCatalog *catalog, CettaStorageCensus *census) {
    if (!catalog || !cetta_storage_object(census, catalog, sizeof(*catalog),
                                         CETTA_STORAGE_BLOCK)) return;
    petta_program_catalog_storage(&catalog->catalog, census);
    const PettaProgramAnalysisSpace *entry = &catalog->analysis;
    if (!petta_program_storage_array(census, entry->type_annotations,
            entry->type_annotation_len, entry->type_annotation_cap, sizeof(*entry->type_annotations)) ||
        !petta_program_storage_array(census, entry->inferred_signatures,
            entry->inferred_signature_len, entry->inferred_signature_cap, sizeof(*entry->inferred_signatures)) ||
        !petta_program_storage_array(census, entry->type_buckets,
            entry->type_bucket_len, entry->type_bucket_cap, sizeof(*entry->type_buckets))) return;
    for (size_t i = 0u; i < entry->type_bucket_len; i++)
        if (!petta_program_storage_array(census, entry->type_buckets[i].types,
                entry->type_buckets[i].len, entry->type_buckets[i].cap,
                sizeof(*entry->type_buckets[i].types))) return;
}

void petta_program_revision_view_storage_census(
        const PettaProgramRevisionView *view, CettaStorageCensus *census) {
    if (!view || !cetta_storage_object(census, view, sizeof(*view), CETTA_STORAGE_BLOCK)) return;
    if (!petta_program_storage_array(census, view->source_equations,
            view->source_equation_len, view->source_equation_capacity,
            sizeof(*view->source_equations))) return;
    petta_program_catalog_storage(&view->catalog, census);
}

void petta_program_storage_census(const PettaProgram *program, CettaStorageCensus *census) {
    if (!program || !cetta_storage_object(census, program, sizeof(*program), CETTA_STORAGE_BLOCK)) return;
    arena_storage_census(&program->plans, census);
    arena_storage_census(&program->transient_scratch, census);
    arena_storage_census(&program->transient_plan_atoms, census);
    if (!petta_program_storage_array(census, program->interned_plans,
            program->interned_plan_len, program->interned_plan_cap, sizeof(*program->interned_plans)) ||
        !petta_program_storage_array(census, program->equation_template_c0,
            program->equation_template_c0_len, program->equation_template_c0_cap,
            sizeof(*program->equation_template_c0)) ||
        !petta_program_storage_array(census, program->spaces,
            program->space_len, program->space_cap, sizeof(*program->spaces)) ||
        !petta_program_storage_array(census, program->transient_plans,
            program->transient_plan_len, program->transient_plan_cap, sizeof(*program->transient_plans)) ||
        !petta_program_storage_array(census, program->predeclared_callability.named_heads,
            program->predeclared_callability.named_len, program->predeclared_callability.named_cap,
            sizeof(*program->predeclared_callability.named_heads)) ||
        !petta_program_storage_array(census, program->callability_cache.named_heads,
            program->callability_cache.named_len, program->callability_cache.named_cap,
            sizeof(*program->callability_cache.named_heads)) ||
        !petta_program_storage_array(census, program->callability_cache_tokens,
            program->callability_cache_token_len, program->callability_cache_token_cap,
            sizeof(*program->callability_cache_tokens))) return;
    for (size_t i = 0u; i < program->space_len; i++)
        petta_program_catalog_storage(&program->spaces[i], census);
    for (size_t i = 0u; i < program->equation_template_c0_len; i++) {
        const PettaEquationTemplateC0 *compiled = program->equation_template_c0[i];
        if (compiled && cetta_storage_object(census, compiled, sizeof(*compiled), CETTA_STORAGE_BLOCK)) {
            cetta_gslt_ground_dense_term_program_storage_v1(&compiled->lhs, census);
            cetta_gslt_ground_dense_term_program_storage_v1(&compiled->rhs, census);
        }
    }
    for (const PettaEquationTemplate *template = program->equation_templates;
         template; template = template->next_owned) {
        if (!cetta_storage_object(census, template, 0u, CETTA_STORAGE_VISITED)) break;
        bindings_frame_schema_storage_census(template->frame_schema, census);
    }
    const PettaProgramAnalysisState *analysis = program->analysis;
    if (analysis && cetta_storage_object(census, analysis, sizeof(*analysis), CETTA_STORAGE_BLOCK)) {
        if (!petta_program_storage_array(census, analysis->spaces,
                analysis->space_len, analysis->space_cap, sizeof(*analysis->spaces))) return;
        for (size_t i = 0u; i < analysis->space_len; i++) {
            const PettaProgramAnalysisSpace *entry = &analysis->spaces[i];
            if (!petta_program_storage_array(census, entry->inferred_signatures,
                    entry->inferred_signature_len, entry->inferred_signature_cap,
                    sizeof(*entry->inferred_signatures)) ||
                !petta_program_storage_array(census, entry->type_annotations,
                    entry->type_annotation_len, entry->type_annotation_cap,
                    sizeof(*entry->type_annotations)) ||
                !petta_program_storage_array(census, entry->type_buckets,
                    entry->type_bucket_len, entry->type_bucket_cap, sizeof(*entry->type_buckets))) return;
            for (size_t j = 0u; j < entry->type_bucket_len; j++) {
                const PettaProgramTypeBucket *bucket = &entry->type_buckets[j];
                if (!petta_program_storage_array(census, bucket->types, bucket->len,
                        bucket->cap, sizeof(*bucket->types))) return;
            }
        }
    }
}
