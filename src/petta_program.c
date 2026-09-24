#include "petta_program.h"

#include "eval.h"
#include "grounded.h"
#include "petta_semantics.h"
#include "shared_transition.h"
#include "stats.h"
#include "symbol.h"
#include "term_canon.h"

#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct PettaCandidateSnapshotStorage {
    atomic_size_t references;
    pthread_mutex_t lock;
    PettaEquationCandidate *items;
    size_t len;
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
    size_t first, len;
};

static PettaCandidateSnapshotStorage *petta_equation_storage_take(
        PettaEquationCandidate *items, size_t len) {
    PettaCandidateSnapshotStorage *storage = cetta_malloc(sizeof(*storage));
    atomic_init(&storage->references, 1u);
    pthread_mutex_init(&storage->lock, NULL);
    storage->items = items;
    storage->len = len;
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
        size_t first, size_t len) {
    if (!catalog || !len || !catalog->storage ||
        len > SIZE_MAX / sizeof(PettaEquationCandidate) ||
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
    size_t records = projection->source ? projection->source->len : projection->len;
    size_t entries = projection->entries ? projection->len : 0u;
    pthread_mutex_unlock(lock);
    if (records > (SIZE_MAX - sizeof(*projection)) / sizeof(PettaEquationCandidate))
        return SIZE_MAX;
    size_t bytes = sizeof(*projection) + records * sizeof(PettaEquationCandidate);
    if (entries > (SIZE_MAX - bytes) / sizeof(PettaEquationSelectionEntry))
        return SIZE_MAX;
    return bytes + entries * sizeof(PettaEquationSelectionEntry);
}

typedef struct {
    Atom *equation;
    const PettaPlanNode *plan;
    const PettaEquationTemplateC0 *equation_template_c0;
    const PettaEquationTemplate *equation_template;
    uint32_t static_variable_count;
    SymbolId head;
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
    const Space *space;
    uint64_t instance_id;
    uint64_t synchronized_revision;
    bool synchronized_snapshot;
    uint64_t catalog_generation;
    PettaProgramEquation *equations;
    size_t equation_len;
    size_t equation_cap;
    PettaProgramHeadBucket *head_buckets;
    size_t head_bucket_len;
    size_t head_bucket_cap;
    bool head_index_dirty;
    PettaProgramCandidateSnapshot *snapshots;
    size_t snapshot_len;
    size_t snapshot_cap;
    PettaProgramRevisionView *revision_view;
} PettaProgramSpace;

typedef struct {
    const Space *space;
    uint64_t instance_id;
    PettaProgramInferredSignature *inferred_signatures;
    size_t inferred_signature_len;
    size_t inferred_signature_cap;
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

#define PETTA_TABLE_SAFETY_CACHE_CAP 128u

/* Keyed on the space program (equation and declaration occurrences), not on
 * the data revision: safety reads the relation's equations and their plans plus
 * static tables, so a data-only mutation leaves the answer valid.  An answer
 * that depended on a plan not built yet, or on an allocation failure, is
 * transient and is not stored. */
typedef struct {
    SpaceProgramToken program;
    SymbolId head;
    CettaExprLen arity;
    PettaRelationSafety safety;
    bool occupied;
} PettaTableSafetyCacheEntry;

struct PettaProgram {
    Arena plans;
    bool (*is_host_intrinsic)(SymbolId head);
    PettaEquationTemplate *equation_templates;
    PettaEquationTemplateC0 **equation_template_c0;
    size_t equation_template_c0_len;
    size_t equation_template_c0_cap;
    PettaProgramSpace *spaces;
    size_t space_len;
    size_t space_cap;
    PettaProgramAnalysisState *analysis;
    PettaCallabilityDomain predeclared_callability;
    uint64_t predeclared_generation;
    /* Callability is program knowledge: it changes only with equation or
       declaration occurrences (each space's SpaceProgramToken) and with
       predeclared heads.  It is read once per program revision, not once
       per planned expression. */
    PettaCallabilityDomain callability_cache;
    SpaceProgramToken *callability_cache_tokens;
    size_t callability_cache_token_len;
    size_t callability_cache_token_cap;
    uint64_t callability_cache_predeclared_generation;
    bool callability_cache_valid;
    PettaTableSafetyCacheEntry
        table_safety_cache[PETTA_TABLE_SAFETY_CACHE_CAP];
};

struct PettaProgramRevisionView {
    _Atomic uint32_t references;
    /* The full token proves that construction observed one coherent source
       state; reuse is keyed by the strictly smaller equation projection. */
    SpaceReadToken source;
    SpaceEquationToken source_equation_token;
    uint64_t catalog_generation;
    PettaProgramSpace catalog;
    Atom **source_equations;
    size_t source_equation_len;
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
        id == VAR_ID_NONE || !slot_out) {
        return false;
    }
    uint32_t low = 0u;
    uint32_t high = variable_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (variable_ids[middle] < id)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= variable_count || variable_ids[low] != id)
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
    if (!program || !source)
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
        if (variables->items[middle] < id)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static bool petta_program_var_insert(
    PettaProgramVarSet *variables, Atom *variable) {
    if (!variables || !variable || variable->kind != ATOM_VAR ||
        variable->var_id == VAR_ID_NONE)
        return false;
    VarId id = variable->var_id;
    size_t index = petta_program_var_lower_bound(variables, id);
    if (index < variables->len && variables->items[index] == id)
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
            container->items[found] != contained->items[index]) {
            return false;
        }
    }
    return true;
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
             left->items[left_index] < right->items[right_index])) {
            left_index++;
        } else if (left_index >= left->len ||
                   right->items[right_index] < left->items[left_index]) {
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
        slots[slot] = (VarId)slot + 1u;
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
    bool open_admitted =
        variables_collected &&
        !petta_semantics_contains_cons_constraint(lhs);
    size_t union_variables =
        (size_t)*static_variable_count_out;
    VarId union_first_variable = 1u;
    VarId union_last_variable = 0u;
    if (open_admitted && union_variables != 0u) {
        bool lhs_nonempty = lhs_variables.len != 0u;
        bool rhs_nonempty = rhs_variables.len != 0u;
        union_first_variable = lhs_nonempty && rhs_nonempty
            ? (lhs_variables.items[0] < rhs_variables.items[0]
                   ? lhs_variables.items[0] : rhs_variables.items[0])
            : lhs_nonempty
                ? lhs_variables.items[0] : rhs_variables.items[0];
        VarId lhs_last = lhs_nonempty
            ? lhs_variables.items[lhs_variables.len - 1u] : 0u;
        VarId rhs_last = rhs_nonempty
            ? rhs_variables.items[rhs_variables.len - 1u] : 0u;
        union_last_variable = lhs_last > rhs_last
            ? lhs_last : rhs_last;
        uint64_t union_span =
            union_last_variable - union_first_variable + 1u;
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
    VarId first_variable = 1u;
    uint32_t variable_width = 0u;
    if (admitted && lhs_variables.len != 0u) {
        first_variable = lhs_variables.items[0];
        VarId last_variable =
            lhs_variables.items[lhs_variables.len - 1u];
        uint64_t span = last_variable - first_variable + 1u;
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

static bool petta_program_type_is_exclusive_kind(const Atom *type) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len == 0u ||
        type->expr.elems[0]->kind != ATOM_SYMBOL || !g_symbols) {
        return false;
    }
    const char *head = symbol_bytes(
        g_symbols, type->expr.elems[0]->sym_id);
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
                space, annotation->expr.elems[1]->sym_id, true);
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
    PettaEquationCandidate *candidates, size_t candidate_count) {
    if (!space || !source.space || head == SYMBOL_ID_NONE ||
        candidate_count > SIZE_MAX / sizeof(*candidates)) {
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
        .storage = petta_equation_storage_take(candidates, candidate_count),
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

static bool petta_equation_view(
    Atom *atom, Atom **lhs, Atom **rhs, SymbolId *head) {
    if (lhs)
        *lhs = NULL;
    if (rhs)
        *rhs = NULL;
    if (head)
        *head = SYMBOL_ID_NONE;
    if (!atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u ||
        !atom_is_symbol_id(
            atom->expr.elems[0], g_builtin_syms.equals)) {
        return false;
    }
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
        *head = left->expr.elems[0]->sym_id;
    return true;
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
    if (petta_equation_view(
            equation, &layout.lhs, &layout.rhs, NULL)) {
        layout.static_variable_count = static_variable_count;
        layout.lhs_contains_cons_constraint_valid = true;
        layout.lhs_contains_cons_constraint =
            petta_semantics_contains_cons_constraint(layout.lhs);
    }
    return layout;
}

bool petta_program_is_equation(Atom *atom) {
    return petta_equation_view(
        atom, NULL, NULL, NULL);
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

bool petta_program_atom_affects_metadata(Atom *atom) {
    return petta_program_is_equation(atom) ||
           petta_program_type_declaration_view(atom, NULL, NULL);
}

bool petta_program_predeclare_equation(
    PettaProgram *program, Atom *atom) {
    if (!program || !atom)
        return false;
    Atom *lhs = NULL;
    SymbolId head = SYMBOL_ID_NONE;
    if (!petta_equation_view(atom, &lhs, NULL, &head))
        return false;
    program->predeclared_generation++;
    if (petta_equation_lhs_admits_any_named_head(lhs)) {
        program->predeclared_callability.admits_any_head = true;
    }
    return head == SYMBOL_ID_NONE ||
           petta_callability_insert_named(
               &program->predeclared_callability, head);
}

bool petta_program_head_declared(
    const PettaProgram *program, SymbolId head) {
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

bool petta_program_head_is_intrinsic(SymbolId head) {
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
         head == ids->with_mutex);
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
            /* Shared data tags are not PeTTa operations. User definitions
             * still establish callability through ordinary resolution. */
            (head <= g_builtin_syms.native_handle &&
             head != g_builtin_syms.llist_cons &&
             head != g_builtin_syms.error) ||
            is_grounded_op(head) ||
            machine_named ||
            typecheck_named);
}

typedef struct {
    Atom *atom;
    PettaPlanNode *plan;
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
        source->sym_id != SYMBOL_ID_NONE && g_symbols &&
        symbol_len(g_symbols, source->sym_id) == 1u &&
        symbol_bytes(g_symbols, source->sym_id)[0] == '_';
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
            plan->control != PETTA_PLAN_CONTROL_NONE ||
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
        plan->control != PETTA_PLAN_CONTROL_IF) {
        return NULL;
    }
    const PettaPlanNode *entry = petta_plan_child(plan, 1u);
    const PettaPlanNode *when_true = petta_plan_child(plan, 2u);
    const PettaPlanNode *when_false = petta_plan_child(plan, 3u);
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
        .source_child = 3u,
        .plan = when_false,
    };
    branches_out[1] = (PettaRegionHoleBranch){
        .source_child = 2u,
        .plan = when_true,
    };
    *program_out = (PettaRegionHoleProgram){
        .root_plan = plan,
        .stable_source = source,
        .kind = PETTA_REGION_HOLE_BOOLEAN_BRANCH,
        .as.boolean_branch = {
            .entry_region = entry->deterministic_region,
            .entry_source_child = 1u,
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
        plan->control != PETTA_PLAN_CONTROL_LET_STAR) {
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

        if (plan->control == PETTA_PLAN_CONTROL_IF) {
            plan->region_hole_program =
                petta_plan_compile_boolean_region_hole_program(
                    program, source, plan);
        } else if (plan->control == PETTA_PLAN_CONTROL_LET_STAR) {
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

static const PettaPlanNode *petta_plan_build(
    PettaProgram *program,
    const PettaCallabilityDomain *callability, Atom *root) {
    if (!program || !root)
        return NULL;
    PettaPlanNode *plan =
        arena_alloc(&program->plans, sizeof(*plan));
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
        if (atom->kind != ATOM_EXPR) {
            node->role = PETTA_PLAN_VALUE;
            node->output = PETTA_PLAN_OUTPUT_VALUE;
            node->plain_scalar_tree = atom->kind == ATOM_VAR ||
                (atom->kind == ATOM_GROUNDED &&
                 (atom->ground.gkind == GV_INT ||
                  atom->ground.gkind == GV_FLOAT ||
                  atom->ground.gkind == GV_BOOL));
            continue;
        }
        node->child_count = atom->expr.len;
        if (atom->expr.len == 0u) {
            node->role = PETTA_PLAN_DATA;
            node->output = PETTA_PLAN_OUTPUT_VALUE;
            continue;
        }
        Atom *head_atom = atom->expr.elems[0];
        if (head_atom->kind == ATOM_SYMBOL) {
            SymbolId head = head_atom->sym_id;
            PeTTaForm form = petta_semantics_form(head);
            node->contains_cardinality_call =
                form == PETTA_FORM_LENGTH ||
                head == g_builtin_syms.size_atom ||
                head == g_builtin_syms.size;
            node->contains_deferred_occurrence_transport =
                petta_head_transports_source_occurrences(head);
            node->control = form == PETTA_FORM_IF
                ? PETTA_PLAN_CONTROL_IF
                : form == PETTA_FORM_LET
                    ? PETTA_PLAN_CONTROL_LET
                    : form == PETTA_FORM_CHAIN
                        ? PETTA_PLAN_CONTROL_CHAIN
                        : head == g_builtin_syms.let_star
                            ? PETTA_PLAN_CONTROL_LET_STAR
                            : PETTA_PLAN_CONTROL_NONE;
            node->plain_scalar_tree =
                grounded_is_plain_scalar_tree_operator(
                    head_atom, atom->expr.len - 1u);
            node->continuation =
                node->control == PETTA_PLAN_CONTROL_LET &&
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
            node->role = constructor_slot_frame
                ? PETTA_PLAN_DATA
                : host_intrinsic || petta_program_head_is_intrinsic(head) ||
                  node->relation_head_admitted ||
                  cetta_petta_source_head_resolves_in_engine(
                      head, atom->expr.len - 1u)
                      ? PETTA_PLAN_STATIC_CALL
                      : PETTA_PLAN_DATA;
            node->execution = constructor_slot_frame
                ? PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS
                : host_intrinsic
                    ? PETTA_PLAN_EXEC_GENERIC
                : grounded_op_is_type_pure(head)
                    ? PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS
                    : node->role == PETTA_PLAN_STATIC_CALL &&
                              form == PETTA_FORM_NONE
                        ? PETTA_PLAN_EXEC_RELATION_SLOTS
                        : PETTA_PLAN_EXEC_GENERIC;
        } else {
            node->role = PETTA_PLAN_DYNAMIC_CALL;
        }

        if (node->role == PETTA_PLAN_DATA) {
            node->output = PETTA_PLAN_OUTPUT_CONSTRUCTOR;
        } else if (head_atom->kind == ATOM_SYMBOL) {
            SymbolId head = head_atom->sym_id;
            PeTTaForm form = petta_semantics_form(head);
            CettaExprLen nargs = atom->expr.len - 1u;
            if (head == g_builtin_syms.quote && nargs == 1u) {
                node->output = PETTA_PLAN_OUTPUT_QUOTED_CHILD;
                node->output_child = 1u;
            } else if (form == PETTA_FORM_CUT && nargs == 0u) {
                node->output = PETTA_PLAN_OUTPUT_TRUE;
            } else {
                CettaExprIndex child = 0u;
                if (form == PETTA_FORM_PROGN && nargs > 0u)
                    child = nargs;
                else if (form == PETTA_FORM_PROG1 && nargs > 0u)
                    child = 1u;
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

        if (!cetta_expr_len_mul_fits_size(
                atom->expr.len, sizeof(*node->children)) ||
            !petta_program_reserve(
                (void **)&work, &work_cap,
                work_len + (size_t)atom->expr.len,
                sizeof(*work))) {
            ok = false;
            break;
        }
        PettaPlanNode *children = arena_alloc(
            &program->plans,
            sizeof(*children) * (size_t)atom->expr.len);
        if (!children) {
            ok = false;
            break;
        }
        memset(
            children, 0,
            sizeof(*children) * (size_t)atom->expr.len);
        node->children = children;
        for (CettaExprIndex index = atom->expr.len;
             index > 0u; index--) {
            CettaExprIndex child = index - 1u;
            work[work_len++] = (PettaPlanBuildItem){
                .atom = atom->expr.elems[child],
                .plan = &children[child],
            };
        }
    }
    free(work);
    if (!ok || !petta_plan_finish_features(plan))
        return NULL;
    petta_plan_compile_deterministic_regions(
        program, root, plan);
    petta_plan_compile_region_hole_programs(
        program, root, plan);
    return plan;
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

/* The spaces read, their program tokens and the predeclared generation still
   describe the cached domain. */
static bool petta_program_callability_cache_current(
    const PettaProgram *program) {
    if (!program->callability_cache_valid ||
        program->callability_cache_predeclared_generation !=
            program->predeclared_generation ||
        program->callability_cache_token_len != program->space_len) {
        return false;
    }
    for (size_t index = 0u; index < program->space_len; index++) {
        const PettaProgramSpace *space = &program->spaces[index];
        SpaceProgramToken token =
            program->callability_cache_tokens[index];
        if (!petta_program_space_is_live(space)) {
            if (token.space)
                return false;
            continue;
        }
        if (!space_program_token_matches_live_space(
                token, space->space)) {
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
        CettaCount length = space_length64(space->space);
        for (CettaIndex atom_index = 0u;
             atom_index < length; atom_index++) {
            Atom *lhs = NULL;
            SymbolId head = SYMBOL_ID_NONE;
            if (petta_equation_view(
                    space_get_at64(space->space, atom_index),
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
    if (petta_program_callability_cache_current(program))
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
    for (size_t index = 0u; index < program->space_len; index++) {
        const PettaProgramSpace *space = &program->spaces[index];
        SpaceProgramToken token = {0};
        if (petta_program_space_is_live(space))
            token = space_program_token(space->space);
        program->callability_cache_tokens[index] = token;
    }
    program->callability_cache_token_len = program->space_len;
    program->callability_cache_predeclared_generation =
        program->predeclared_generation;
    program->callability_cache_valid = true;
    return true;
}

static PettaProgramSpace *petta_program_find_space(
    PettaProgram *program, const Space *space) {
    if (!program || !space)
        return NULL;
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

static bool petta_program_revision_view_retain(
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

static void petta_program_space_dispose_catalog(
        PettaProgramSpace *entry) {
    if (!entry)
        return;
    petta_program_space_invalidate_revision_view(
        entry, PETTA_REVISION_VIEW_INVALIDATE_DISPOSE);
    petta_program_space_clear_head_index(entry);
    petta_program_space_clear_candidate_snapshots(entry);
    free(entry->equations);
    entry->equations = NULL;
    entry->equation_len = 0u;
    entry->equation_cap = 0u;
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
    size_t record_index = entry->equation_len++;
    entry->equations[record_index] = equation;
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
        if (write != read)
            entry->equations[write] = entry->equations[read];
        write++;
    }
    if (write == entry->equation_len)
        return false;
    entry->equation_len = write;
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
    entry->equations = equations;
    entry->equation_len = count;
    entry->equation_cap = count;
    entry->head_index_dirty = true;
    petta_program_space_commit_catalog_mutation(entry);
}

PettaProgramRevisionView *petta_program_revision_view_capture(
        PettaProgram *program, Space *source) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_ATTEMPT);
    PettaProgramSpace *entry =
        petta_program_find_space(program, source);
    if (!entry || !source) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_CAPTURE_DECLINE);
        return NULL;
    }

    if (entry->revision_view &&
        entry->revision_view->catalog_generation ==
            entry->catalog_generation &&
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
    view->source = space_read_token(source);
    view->source_equation_token = space_equation_token(source);
    view->catalog_generation = entry->catalog_generation;
    view->catalog = (PettaProgramSpace){
        .space = source,
        .instance_id = view->source.instance_id,
        .synchronized_revision = entry->synchronized_revision,
        .synchronized_snapshot = entry->synchronized_snapshot,
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

    size_t equation_cap = 0u;
    CettaCount source_len = space_length64(source);
    for (CettaIndex index = 0u; index < source_len; index++) {
        Atom *atom = space_get_at64(source, index);
        if (!atom || !petta_program_is_equation(atom))
            continue;
        if (!petta_program_reserve(
                (void **)&view->source_equations, &equation_cap,
                view->source_equation_len + 1u,
                sizeof(*view->source_equations))) {
            goto decline;
        }
        view->source_equations[view->source_equation_len++] = atom;
    }
    if (!space_read_token_matches_live_space(view->source, source) ||
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
        if (!atom || !petta_program_is_equation(atom))
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

static PettaProgramAnalysisSpace *petta_program_find_analysis_space(
    PettaProgram *program, const Space *space) {
    if (!program || !program->analysis || !space)
        return NULL;
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

PettaProgram *petta_program_new(void) {
    return petta_program_new_with_host_intrinsics(NULL);
}

PettaProgram *petta_program_new_with_host_intrinsics(
    bool (*is_host_intrinsic)(SymbolId head)) {
    PettaProgram *program = cetta_malloc(sizeof(*program));
    memset(program, 0, sizeof(*program));
    program->is_host_intrinsic = is_host_intrinsic;
    arena_init(&program->plans);
    arena_set_runtime_kind(
        &program->plans, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&program->plans, NULL);
    return program;
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
    return program && program->analysis;
}

void petta_program_free(PettaProgram *program) {
    if (!program)
        return;
    for (size_t index = 0u;
         index < program->space_len; index++) {
        petta_program_space_dispose_catalog(
            &program->spaces[index]);
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
    free(program);
}

const PettaPlanNode *petta_program_plan_current(
    PettaProgram *program, Atom *atom) {
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

PettaDeclarationBlock *petta_program_declaration_block_new(
    PettaProgram *program, const TermUniverse *universe,
    const AtomId *atoms, int atom_count) {
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
        Atom *atom = term_universe_get_atom(universe, atoms[index]);
        Atom *lhs = NULL;
        SymbolId head = SYMBOL_ID_NONE;
        if (petta_equation_view(atom, &lhs, NULL, &head)) {
            if (petta_equation_lhs_admits_any_named_head(lhs)) {
                callability.admits_any_head = true;
            } else if (head != SYMBOL_ID_NONE) {
                ok = petta_callability_insert_named(
                    &callability, head);
            }
        }
    }
    for (int index = 0; ok && index < atom_count; index++) {
        Atom *atom = term_universe_get_atom(universe, atoms[index]);
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

const PettaPlanNode *petta_program_plan_dynamic_add(
    PettaProgram *program, Atom *atom) {
    if (!program || !atom)
        return NULL;
    PettaCallabilityDomain callability = {0};
    bool ok = petta_program_collect_callability(
        program, &callability);
    Atom *lhs = NULL;
    SymbolId head = SYMBOL_ID_NONE;
    if (ok && petta_equation_view(
            atom, &lhs, NULL, &head)) {
        if (petta_equation_lhs_admits_any_named_head(lhs)) {
            callability.admits_any_head = true;
        } else if (head != SYMBOL_ID_NONE) {
            ok = petta_callability_insert_named(
                &callability, head);
        }
    }
    const PettaPlanNode *plan =
        ok ? petta_plan_build(program, &callability, atom) : NULL;
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

bool petta_program_note_add(
    PettaProgram *program, Space *space, Atom *atom,
    const PettaPlanNode *plan) {
    if (!program || !space || !atom)
        return false;
    Atom *subject = NULL;
    Atom *type = NULL;
    if (petta_program_type_declaration_view(
            atom, &subject, &type)) {
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
    if (!petta_equation_view(atom, &lhs, &rhs, &head))
        return true;
    PettaProgramSpace *entry =
        petta_program_ensure_space(program, space);
    if (!petta_program_space_reserve_equation(entry)) {
        return false;
    }
    uint32_t static_variable_count = 0u;
    bool open_template_admitted = false;
    const PettaEquationTemplate *equation_template = NULL;
    PettaEquationTemplateC0 *equation_template_c0 =
        petta_program_compile_equation_template_c0(
            program, lhs, rhs, head,
            &static_variable_count,
            &open_template_admitted,
            &equation_template);
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
    petta_program_space_append_reserved_equation(
        entry,
        (PettaProgramEquation){
            .equation = atom,
            .plan = plan,
            .equation_template_c0 = equation_template_c0,
            .equation_template = equation_template,
            .static_variable_count = static_variable_count,
            .head = head,
        });
    return true;
}

bool petta_program_observe_addition(
    PettaProgram *program, Space *space, Arena *storage,
    Atom *atom, const PettaPlanNode *plan) {
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
    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    if (!atom)
        return;
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
    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    if (!atom)
        return;
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
    if (!program || !space)
        return false;
    PettaProgramSpace *current =
        petta_program_find_space(program, space);
    uint64_t revision = space_revision(space);
    if (current && current->synchronized_snapshot &&
        current->synchronized_revision == revision) {
        return true;
    }

    SpaceReadToken read = space_read_token(space);
    CettaCount atom_count = space_length64(space);
    PettaCallabilityDomain callability = {0};
    bool ok = true;
    for (CettaIndex index = 0u; ok && index < atom_count; index++) {
        Atom *atom = space_get_at64(space, index);
        Atom *lhs = NULL;
        SymbolId head = SYMBOL_ID_NONE;
        if (!atom) {
            ok = false;
        } else if (petta_equation_view(
                       atom, &lhs, NULL, &head)) {
            if (petta_equation_lhs_admits_any_named_head(lhs)) {
                callability.admits_any_head = true;
            } else if (head != SYMBOL_ID_NONE) {
                ok = petta_callability_insert_named(
                    &callability, head);
            }
        }
    }

    if (ok)
        petta_program_forget_space(program, space);
    for (CettaIndex index = 0u; ok && index < atom_count; index++) {
        Atom *atom = space_get_at64(space, index);
        if (!petta_program_is_equation(atom))
            continue;
        const PettaPlanNode *plan =
            petta_plan_build(program, &callability, atom);
        ok = plan && petta_program_note_add(
            program, space, atom, plan);
    }
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
    installed->synchronized_snapshot = true;
    return true;
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
    PettaProgramSpace *catalog;
    PettaPortableRelation *relations;
    size_t relation_len;
    size_t relation_cap;
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
        if (!petta_equation_view(
                check->catalog->equations[index].equation,
                &lhs, NULL, NULL) ||
            !lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
            lhs->expr.len - 1u != arity) {
            continue;
        }
        Atom *lhs_head = lhs->expr.elems[0];
        if (!lhs_head || lhs_head->kind != ATOM_SYMBOL) {
            *open_out = true;
        } else if (lhs_head->sym_id == head) {
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
    if (!head || head->kind != ATOM_SYMBOL ||
        petta_semantics_is_open_cons_value(atom) ||
        petta_semantics_is_cons_constraint(atom) ||
        petta_program_head_is_intrinsic(head->sym_id) ||
        is_grounded_op(head->sym_id)) {
        return false;
    }
    bool exact = false;
    bool open = false;
    if (!petta_portable_relation_presence(
            check, head->sym_id, atom->expr.len - 1u,
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
    Atom *head = atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;

    bool exact = false;
    bool open = false;
    if (!petta_portable_relation_presence(
            check, head->sym_id, atom->expr.len - 1u,
            &exact, &open) || open) {
        return false;
    }
    if (exact) {
        return petta_portable_relation_check(
            check, head->sym_id, atom->expr.len - 1u);
    }
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
    if (!head || head->kind != ATOM_SYMBOL ||
        petta_semantics_is_open_cons_value(atom) ||
        petta_semantics_is_cons_constraint(atom) ||
        petta_program_head_is_intrinsic(head->sym_id) ||
        is_grounded_op(head->sym_id)) {
        return false;
    }
    bool exact = false;
    bool open = false;
    if (!petta_portable_relation_presence(
            check, head->sym_id, atom->expr.len - 1u,
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
        if (!petta_equation_view(
                check->catalog->equations[index].equation,
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
        if (lhs_head->sym_id != head)
            continue;
        saw_equation = true;
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
            &check, call->expr.elems[0]->sym_id,
            call->expr.len - 1u);
    }
    free(check.relations);
    return accepted
        ? CETTA_RELATIONAL_EXECUTION_STRUCTURAL_EQUATIONS_V1
        : CETTA_RELATIONAL_EXECUTION_UNQUALIFIED;
}

void petta_program_forget_space(
    PettaProgram *program, const Space *space) {
    if (!program || !space)
        return;
    for (size_t index = 0u;
         index < program->space_len; index++) {
        PettaProgramSpace *entry = &program->spaces[index];
        if (entry->space != space)
            continue;
        petta_program_space_dispose_catalog(entry);
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

static bool petta_program_copy_records(
    PettaProgram *program, const Space *source,
    Space *destination, bool replace) {
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
    if (!petta_equation_view(
            equation, &lhs, NULL, NULL)) {
        return false;
    }
    Atom *lhs_head = lhs->expr.elems[0];
    return lhs_head->kind != ATOM_SYMBOL ||
           lhs_head->sym_id == head;
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
    if (!items) {
        items = cetta_malloc(lease->len * sizeof(*items));
        memcpy(items, lease->items, lease->len * sizeof(*items));
    }
    lease->storage = petta_equation_storage_take(items, lease->len);
    /* A private host array has no cache owner to retire it later. Its full
     * observation is owned by this lease; narrower projections may compact
     * immediately when they escape into a suspended choice. */
    lease->storage->retired = true;
    lease->items = items;
    lease->owned_items = NULL;
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
    bool admit_local_execution_payload,
    Space *space, SymbolId head,
    PettaCandidateSnapshotLease *lease,
    PettaCandidateSnapshotStats *stats) {
    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (lease)
        memset(lease, 0, sizeof(*lease));
    if (!space || head == SYMBOL_ID_NONE || !lease) {
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
    if (!space_equation_cursor_init(space, head, &cursor))
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
        if (!space_equation_occurrence_resolve(id, &occurrence)) {
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
        items[length++] = (PettaEquationCandidate){
            .equation = actual[index].equation,
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
            publication_authority, head, source, key, items, length)) {
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
        program && !cetta_shared_transition_scope_active()
            ? petta_program_find_space(program, space)
            : NULL;
    return petta_program_candidate_snapshot_lease_from_entry(
        entry, entry, true, space, head, lease, stats);
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
            &projection->view->catalog, NULL, false,
            space, head, lease, stats);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PROGRAM_REVISION_VIEW_QUERY_STALE_FALLBACK);
    return petta_program_candidate_snapshot_lease_from_entry(
        NULL, NULL, false, space, head, lease, stats);
}

bool petta_program_revision_view_source_candidate_lease(
        const PettaProgramRevisionProjection *projection,
        Space *space, SymbolId head,
        PettaCandidateSnapshotLease *lease,
        PettaCandidateSnapshotStats *stats) {
    if (!petta_program_revision_projection_current(
            projection, space) ||
        projection->target.space !=
            projection->view->source_equation_token.space ||
        projection->target.instance_id !=
            projection->view->source_equation_token.instance_id ||
        projection->target.equation_revision !=
            projection->view->source_equation_token.equation_revision) {
        if (lease)
            memset(lease, 0, sizeof(*lease));
        if (stats)
            memset(stats, 0, sizeof(*stats));
        return false;
    }
    return petta_program_candidate_snapshot_lease_from_entry(
        &projection->view->catalog, NULL, true,
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

static bool petta_program_inferred_signature_put_internal(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity, Atom *signature) {
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
    if (annotations_out)
        *annotations_out = NULL;
    if (count_out)
        *count_out = 0u;
    if (!annotations_out || !count_out)
        return false;
    if (!program || !program->analysis)
        return true;
    Atom **annotations = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    for (size_t space_index = 0u;
         space_index < program->analysis->space_len; space_index++) {
        PettaProgramAnalysisSpace *entry =
            &program->analysis->spaces[space_index];
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
    if (types_out)
        *types_out = NULL;
    if (!program || !program->analysis || !subject ||
        subject->kind != ATOM_SYMBOL ||
        !arena || !types_out)
        return 0u;
    Atom **types = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    for (size_t space_index = 0u;
         space_index < program->analysis->space_len; space_index++) {
        PettaProgramAnalysisSpace *entry =
            &program->analysis->spaces[space_index];
        if (entry->type_index_dirty &&
            !petta_program_space_rebuild_type_index(entry)) {
            free(types);
            return 0u;
        }
        PettaProgramTypeBucket *bucket =
            petta_program_type_bucket(
                entry, subject->sym_id, false);
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
        atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
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
            binding->expr.len != 2u || !binding_plan ||
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
        atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        plan->child_count != atom->expr.len ||
        !petta_table_safety_push_node(
            nodes, length, capacity,
            atom->expr.elems[1], petta_plan_child(plan, 1u))) {
        return false;
    }

    Atom *branches = atom->expr.elems[2];
    const PettaPlanNode *branches_plan =
        petta_plan_child(plan, 2u);
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
            branch->expr.len != 2u || !branch_plan ||
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

/*
 * These forms are pure provided every executable child is pure.  Forms
 * which invoke an argument as a callable (map/fold/forall), perform I/O or
 * mutation, cross an FFI boundary, or alter search commitment are excluded.
 * Exclusion only disables tabling; ordinary evaluation remains available.
 */
static bool petta_table_safety_form_is_pure(
    PeTTaForm form, bool *opaque) {
    if (opaque)
        *opaque = false;
    switch (form) {
    case PETTA_FORM_IF:
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
    case PETTA_FORM_TEST:
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
    SymbolId head, bool *opaque) {
    if (opaque)
        *opaque = false;
    if (head == SYMBOL_ID_NONE)
        return false;
    if (grounded_op_is_type_pure(head))
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
    if (head == g_builtin_syms.quote ||
        head == g_builtin_syms.return_text) {
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
    PettaTableSafetyRelation **relations,
    size_t *relation_len, size_t *relation_cap,
    bool *transient_out) {
    PettaEquationCandidate *candidates = NULL;
    size_t candidate_count = 0u;
    if (!petta_program_candidate_snapshot(
            program, space, relation.head,
            &candidates, &candidate_count)) {
        if (transient_out)
            *transient_out = true;
        return PETTA_RELATION_SAFETY_UNSAFE;
    }

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
        if (!petta_equation_view(
                candidates[index].equation,
                &lhs, &rhs, NULL) ||
            !lhs || lhs->expr.len == 0u ||
            lhs->expr.len - 1u != relation.arity) {
            continue;
        }
        Atom *lhs_head = lhs->expr.elems[0];
        if (lhs_head->kind == ATOM_SYMBOL &&
            lhs_head->sym_id != relation.head) {
            continue;
        }
        saw_matching_equation = true;
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
    free(candidates);

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
            atom->expr.elems[0]->sym_id;
        if (call_head == g_builtin_syms.case_text) {
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
        bool opaque = false;
        if (petta_table_safety_primitive(
                call_head, &opaque)) {
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
    return (size_t)hash &
           (PETTA_TABLE_SAFETY_CACHE_CAP - 1u);
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

    size_t cache_slot =
        petta_table_safety_cache_slot(space, head, arity);
    PettaTableSafetyCacheEntry *cached =
        &program->table_safety_cache[cache_slot];
    SpaceProgramToken program_token = space_program_token(space);
    if (cached->occupied &&
        cached->head == head &&
        cached->arity == arity &&
        space_program_token_eq(cached->program, program_token)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_RELATION_SAFETY_CACHE_HIT);
        return cached->safety;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_RELATION_SAFETY_CACHE_MISS);

    PettaTableSafetyRelation *relations = NULL;
    size_t relation_len = 0u;
    size_t relation_cap = 0u;
    bool transient = false;
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
            program, space, relations[index],
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
    SymbolId head = call->expr.elems[0]->sym_id;
    bool opaque = false;
    if (petta_table_safety_primitive(head, &opaque))
        return PETTA_RESOLVED_CALL_MACHINE_LOCAL;
    if (petta_program_head_is_intrinsic(head))
        return PETTA_RESOLVED_CALL_UNSAFE;
    if (!space_equations_may_match_known_head(space, head))
        return PETTA_RESOLVED_CALL_MACHINE_LOCAL;
    return petta_program_relation_safety(
               program, space, head, call->expr.len - 1u) ==
                   PETTA_RELATION_SAFETY_UNSAFE
        ? PETTA_RESOLVED_CALL_UNSAFE
        : PETTA_RESOLVED_CALL_RELATION;
}
