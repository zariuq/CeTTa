#include "petta_program.h"

#include "eval.h"
#include "grounded.h"
#include "petta_semantics.h"
#include "symbol.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    Atom *equation;
    const PettaPlanNode *plan;
    SymbolId head;
} PettaProgramClause;

typedef struct {
    SymbolId head;
    size_t *record_indices;
    size_t len;
    size_t cap;
} PettaProgramHeadBucket;

typedef struct {
    SymbolId head;
    uint64_t revision;
    PettaClauseCandidate *candidates;
    size_t len;
} PettaProgramClauseSnapshot;

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
    PettaProgramClause *clauses;
    size_t clause_len;
    size_t clause_cap;
    PettaProgramHeadBucket *head_buckets;
    size_t head_bucket_len;
    size_t head_bucket_cap;
    bool head_index_dirty;
    PettaProgramClauseSnapshot *snapshots;
    size_t snapshot_len;
    size_t snapshot_cap;
} PettaProgramSpace;

typedef struct {
    const Space *space;
    uint64_t instance_id;
    PettaProgramInferredSignature *inferred_signatures;
    size_t inferred_signature_len;
    size_t inferred_signature_cap;
    uint64_t inferred_signature_revision;
    bool inferred_signatures_valid;
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
    SymbolId *items;
    size_t len;
    size_t cap;
} PettaHeadSet;

#define PETTA_TABLE_SAFETY_CACHE_CAP 128u

typedef struct {
    const Space *space;
    uint64_t instance_id;
    uint64_t revision;
    SymbolId head;
    CettaExprLen arity;
    PettaRelationSafety safety;
    bool occupied;
} PettaTableSafetyCacheEntry;

struct PettaProgram {
    Arena plans;
    PettaProgramSpace *spaces;
    size_t space_len;
    size_t space_cap;
    PettaProgramAnalysisState *analysis;
    PettaHeadSet predeclared_heads;
    PettaTableSafetyCacheEntry
        table_safety_cache[PETTA_TABLE_SAFETY_CACHE_CAP];
};

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

static void petta_program_space_clear_clause_snapshots(
    PettaProgramSpace *space) {
    if (!space)
        return;
    for (size_t index = 0u;
         index < space->snapshot_len; index++) {
        free(space->snapshots[index].candidates);
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

static const PettaProgramClauseSnapshot *
petta_program_space_find_clause_snapshot(
    const PettaProgramSpace *space, SymbolId head,
    uint64_t revision) {
    if (!space)
        return NULL;
    size_t index = petta_program_snapshot_lower_bound(
        space, head);
    if (index >= space->snapshot_len ||
        space->snapshots[index].head != head ||
        space->snapshots[index].revision != revision) {
        return NULL;
    }
    return &space->snapshots[index];
}

static bool petta_program_space_store_clause_snapshot_take(
    PettaProgramSpace *space, SymbolId head, uint64_t revision,
    PettaClauseCandidate *candidates, size_t candidate_count) {
    if (!space || head == SYMBOL_ID_NONE ||
        candidate_count > SIZE_MAX / sizeof(*candidates)) {
        return false;
    }

    size_t index = petta_program_snapshot_lower_bound(
        space, head);
    if (index < space->snapshot_len &&
        space->snapshots[index].head == head) {
        free(space->snapshots[index].candidates);
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
    space->snapshots[index] = (PettaProgramClauseSnapshot){
        .head = head,
        .revision = revision,
        .candidates = candidates,
        .len = candidate_count,
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
    for (size_t index = 0u; index < space->clause_len; index++) {
        if (!petta_program_space_head_index_append(
                space, space->clauses[index].head, index)) {
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

static size_t petta_head_lower_bound(
    const PettaHeadSet *set, SymbolId head) {
    size_t low = 0u;
    size_t high = set ? set->len : 0u;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (set->items[middle] < head)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static bool petta_head_contains(
    const PettaHeadSet *set, SymbolId head) {
    if (!set || head == SYMBOL_ID_NONE)
        return false;
    size_t index = petta_head_lower_bound(set, head);
    return index < set->len && set->items[index] == head;
}

static bool petta_head_insert(
    PettaHeadSet *set, SymbolId head) {
    if (!set || head == SYMBOL_ID_NONE)
        return false;
    size_t index = petta_head_lower_bound(set, head);
    if (index < set->len && set->items[index] == head)
        return true;
    if (!petta_program_reserve(
            (void **)&set->items, &set->cap, set->len + 1u,
            sizeof(*set->items))) {
        return false;
    }
    memmove(
        set->items + index + 1u, set->items + index,
        sizeof(*set->items) * (set->len - index));
    set->items[index] = head;
    set->len++;
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

bool petta_program_is_equation(Atom *atom) {
    return petta_equation_view(
        atom, NULL, NULL, NULL);
}

bool petta_program_predeclare_equation(
    PettaProgram *program, Atom *atom) {
    if (!program || !atom)
        return false;
    SymbolId head = SYMBOL_ID_NONE;
    if (!petta_equation_view(atom, NULL, NULL, &head))
        return false;
    /*
     * A variable-headed equation is a valid PeTTa clause, but it grants no
     * named relation ownership.  Its later clause plan still participates in
     * relational matching; there is simply no static head to predeclare.
     */
    return head == SYMBOL_ID_NONE ||
           petta_head_insert(&program->predeclared_heads, head);
}

bool petta_program_head_is_intrinsic(SymbolId head) {
    const char *name =
        head == SYMBOL_ID_NONE
            ? NULL : symbol_bytes(g_symbols, head);
    bool machine_named =
        name &&
        (strcmp(name, "member") == 0 ||
         strcmp(name, "last") == 0 ||
         strcmp(name, "reverse") == 0 ||
         strcmp(name, "empty") == 0 ||
         strcmp(name, "min") == 0 ||
         strcmp(name, "max") == 0 ||
         strcmp(name, "transaction") == 0 ||
         strcmp(name, "with_mutex") == 0);
    /* `data` is shared with historical extended PeTTa.  Live `make-list`
     * and `the` forms are owned only by typecheck-v2; extended erases `the`
     * during document ingestion. */
    bool typecheck_named =
        name && cetta_petta_profile_admits_typecheck_ops() &&
        (strcmp(name, "data") == 0 ||
         (cetta_petta_profile_admits_native_typecheck_v2() &&
          (strcmp(name, "make-list") == 0 ||
           strcmp(name, "the") == 0)));
    return head != SYMBOL_ID_NONE &&
           (petta_semantics_form(head) != PETTA_FORM_NONE ||
            head <= g_builtin_syms.native_handle ||
            is_grounded_op(head) ||
            machine_named ||
            typecheck_named);
}

typedef struct {
    Atom *atom;
    PettaPlanNode *plan;
    uint32_t depth;
} PettaPlanBuildItem;

typedef struct {
    PettaPlanNode *plan;
    bool expanded;
} PettaPlanFeatureItem;

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
                        .contains_length_call) {
                    node->contains_length_call = true;
                }
                if (node->children[index].contains_call) {
                    descendant_contains_call = true;
                    node->contains_call = true;
                }
            }
            if (node->execution ==
                    PETTA_PLAN_EXEC_RELATION_SLOTS &&
                descendant_contains_call) {
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

static const PettaPlanNode *petta_plan_build(
    PettaProgram *program, const PettaHeadSet *heads, Atom *root) {
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
        if (!atom || !node || item.depth > 2048u) {
            ok = false;
            break;
        }
        if (atom->kind != ATOM_EXPR) {
            node->role = PETTA_PLAN_VALUE;
            continue;
        }
        node->child_count = atom->expr.len;
        if (atom->expr.len == 0u) {
            node->role = PETTA_PLAN_DATA;
            continue;
        }
        Atom *head_atom = atom->expr.elems[0];
        if (head_atom->kind == ATOM_SYMBOL) {
            SymbolId head = head_atom->sym_id;
            node->contains_length_call =
                petta_semantics_form(head) ==
                    PETTA_FORM_LENGTH;
            bool constructor_slot_frame =
                atom->expr.len > 1u &&
                (head == g_builtin_syms.colon ||
                 head == g_builtin_syms.arrow);
            node->role = constructor_slot_frame
                ? PETTA_PLAN_DATA
                : petta_program_head_is_intrinsic(head) ||
                  petta_head_contains(heads, head) ||
                  cetta_petta_source_head_resolves_in_engine(
                      head, atom->expr.len - 1u)
                      ? PETTA_PLAN_STATIC_CALL
                      : PETTA_PLAN_DATA;
            node->execution = constructor_slot_frame
                ? PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS
                : grounded_op_is_type_pure(head)
                    ? PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS
                    : node->role == PETTA_PLAN_STATIC_CALL &&
                              petta_semantics_form(head) ==
                                  PETTA_FORM_NONE
                        ? PETTA_PLAN_EXEC_RELATION_SLOTS
                        : PETTA_PLAN_EXEC_GENERIC;
        } else {
            node->role = PETTA_PLAN_DYNAMIC_CALL;
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
                .depth = item.depth + 1u,
            };
        }
    }
    free(work);
    return ok && petta_plan_finish_features(plan)
        ? plan : NULL;
}

static bool petta_program_collect_live_heads(
    const PettaProgram *program, PettaHeadSet *heads) {
    if (!program || !heads)
        return false;
    for (size_t index = 0u;
         index < program->predeclared_heads.len; index++) {
        if (!petta_head_insert(
                heads, program->predeclared_heads.items[index])) {
            return false;
        }
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
        CettaCount length = space_length64(space->space);
        for (CettaIndex atom_index = 0u;
             atom_index < length; atom_index++) {
            SymbolId head = SYMBOL_ID_NONE;
            if (petta_equation_view(
                    space_get_at64(space->space, atom_index),
                    NULL, NULL, &head) &&
                head != SYMBOL_ID_NONE &&
                !petta_head_insert(heads, head)) {
                return false;
            }
        }
    }
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
    PettaProgram *program = cetta_malloc(sizeof(*program));
    memset(program, 0, sizeof(*program));
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
        petta_program_space_clear_head_index(
            &program->spaces[index]);
        petta_program_space_clear_clause_snapshots(
            &program->spaces[index]);
        free(program->spaces[index].clauses);
    }
    petta_program_analysis_state_free(program->analysis);
    free(program->predeclared_heads.items);
    free(program->spaces);
    arena_free(&program->plans);
    free(program);
}

const PettaPlanNode *petta_program_plan_current(
    PettaProgram *program, Atom *atom) {
    if (!program || !atom)
        return NULL;
    PettaHeadSet heads = {0};
    bool ok = petta_program_collect_live_heads(program, &heads);
    const PettaPlanNode *plan =
        ok ? petta_plan_build(program, &heads, atom) : NULL;
    free(heads.items);
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

    PettaHeadSet heads = {0};
    bool ok = petta_program_collect_live_heads(program, &heads);
    for (int index = 0; ok && index < atom_count; index++) {
        Atom *atom = term_universe_get_atom(universe, atoms[index]);
        SymbolId head = SYMBOL_ID_NONE;
        if (petta_equation_view(
                atom, NULL, NULL, &head) &&
            head != SYMBOL_ID_NONE) {
            ok = petta_head_insert(&heads, head);
        }
    }
    for (int index = 0; ok && index < atom_count; index++) {
        Atom *atom = term_universe_get_atom(universe, atoms[index]);
        block->plans[index] =
            petta_plan_build(program, &heads, atom);
        ok = block->plans[index] != NULL;
    }
    free(heads.items);
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
    PettaHeadSet heads = {0};
    bool ok = petta_program_collect_live_heads(program, &heads);
    SymbolId head = SYMBOL_ID_NONE;
    if (ok && petta_equation_view(
            atom, NULL, NULL, &head) &&
        head != SYMBOL_ID_NONE) {
        ok = petta_head_insert(&heads, head);
    }
    const PettaPlanNode *plan =
        ok ? petta_plan_build(program, &heads, atom) : NULL;
    free(heads.items);
    return plan;
}

bool petta_program_note_add(
    PettaProgram *program, Space *space, Atom *atom,
    const PettaPlanNode *plan) {
    if (!program || !space || !atom)
        return false;
    if (atom->kind == ATOM_EXPR && atom->expr.len == 3u &&
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.colon) &&
        atom->expr.elems[1]->kind == ATOM_SYMBOL) {
        if (!program->analysis)
            return petta_program_ensure_space(program, space) != NULL;
        PettaProgramAnalysisSpace *entry =
            petta_program_ensure_analysis_space(program, space);
        if (!entry)
            return false;
        Atom *subject = atom->expr.elems[1];
        Atom *type = atom->expr.elems[2];
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
    SymbolId head = SYMBOL_ID_NONE;
    if (!petta_equation_view(atom, NULL, NULL, &head))
        return true;
    PettaProgramSpace *entry =
        petta_program_ensure_space(program, space);
    if (!entry ||
        !petta_program_reserve(
            (void **)&entry->clauses, &entry->clause_cap,
            entry->clause_len + 1u,
            sizeof(*entry->clauses))) {
        return false;
    }
    size_t record_index = entry->clause_len++;
    entry->clauses[record_index] =
        (PettaProgramClause){
            .equation = atom,
            .plan = plan,
            .head = head,
        };
    if (!entry->head_index_dirty &&
        !petta_program_space_head_index_append(
            entry, head, record_index)) {
        entry->head_index_dirty = true;
    }
    petta_program_space_clear_clause_snapshots(entry);
    return true;
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
    size_t write = 0u;
    for (size_t read = 0u;
         read < entry->clause_len; read++) {
        if (atom_eq(entry->clauses[read].equation, atom))
            continue;
        if (write != read)
            entry->clauses[write] = entry->clauses[read];
        write++;
    }
    if (write != entry->clause_len) {
        entry->head_index_dirty = true;
        petta_program_space_clear_clause_snapshots(entry);
    }
    entry->clause_len = write;
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
         index < entry->clause_len; index++) {
        Atom *candidate = entry->clauses[index].equation;
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
    memmove(
        entry->clauses + remove,
        entry->clauses + remove + 1u,
        sizeof(*entry->clauses) *
            (entry->clause_len - remove - 1u));
    entry->clause_len--;
    entry->head_index_dirty = true;
    petta_program_space_clear_clause_snapshots(entry);
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
        petta_program_space_clear_head_index(entry);
        petta_program_space_clear_clause_snapshots(entry);
        free(entry->clauses);
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
    if ((!source_entry || source_entry->clause_len == 0u) &&
        (!source_analysis ||
         source_analysis->type_annotation_len == 0u)) {
        return true;
    }

    /*
     * ensure_space can reallocate program->spaces, invalidating the source
     * entry pointer.  Copy the records first, then install them.
     */
    size_t count = source_entry ? source_entry->clause_len : 0u;
    PettaProgramClause *copy = count
        ? cetta_malloc(sizeof(*copy) * count) : NULL;
    if (count)
        memcpy(copy, source_entry->clauses, sizeof(*copy) * count);
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
        petta_program_space_clear_head_index(destination_entry);
        petta_program_space_clear_clause_snapshots(destination_entry);
        free(destination_entry->clauses);
        destination_entry->clauses = copy;
        destination_entry->clause_len = count;
        destination_entry->clause_cap = count;
        destination_entry->head_index_dirty = true;
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

static bool petta_clause_head_matches(
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

void petta_program_clause_snapshot_lease_release(
    PettaClauseSnapshotLease *lease) {
    if (!lease)
        return;
    free(lease->owned_items);
    memset(lease, 0, sizeof(*lease));
}

bool petta_program_clause_snapshot_lease_profiled(
    PettaProgram *program, Space *space, SymbolId head,
    PettaClauseSnapshotLease *lease,
    PettaClauseSnapshotStats *stats) {
    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (lease)
        memset(lease, 0, sizeof(*lease));
    if (!program || !space || head == SYMBOL_ID_NONE ||
        !lease) {
        return false;
    }
    if (stats)
        stats->snapshots = 1u;

    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    uint64_t revision = space_revision(space);
    const PettaProgramClauseSnapshot *cached =
        petta_program_space_find_clause_snapshot(
            entry, head, revision);
    if (cached) {
        lease->items = cached->candidates;
        lease->len = cached->len;
        if (stats) {
            stats->cache_hits = 1u;
            stats->candidates_emitted = cached->len;
        }
        return true;
    }
    typedef struct {
        Atom *equation;
        SpaceEquationOccurrenceId occurrence;
    } PettaLiveClause;
    typedef struct {
        Atom *equation;
        size_t first;
        size_t last;
    } PettaLivePointerBucket;
    size_t actual_len = 0u;
    size_t actual_cap = 0u;
    PettaLiveClause *actual = NULL;
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
        actual[actual_len++] = (PettaLiveClause){
            .equation = occurrence.equation,
            .occurrence = occurrence.id,
        };
    }

    bool *used = actual_len
        ? cetta_malloc(sizeof(*used) * actual_len)
        : NULL;
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
    PettaClauseCandidate *items = NULL;

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
            petta_program_space_rebuild_head_index(entry);
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
                if (fallback_position >= entry->clause_len)
                    break;
                record_index = fallback_position++;
            }
            if (record_index >= entry->clause_len)
                continue;
            PettaProgramClause *record =
                &entry->clauses[record_index];
            if (stats)
                stats->declaration_records_examined++;
            if (!petta_clause_head_matches(
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
            items[length++] = (PettaClauseCandidate){
                .equation = actual[matched].equation,
                .rhs_plan =
                    petta_plan_child(record->plan, 2u),
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
        items[length++] = (PettaClauseCandidate){
            .equation = actual[index].equation,
            .occurrence = actual[index].occurrence,
        };
    }
    free(used);
    free(actual);
    free(pointer_buckets);
    free(pointer_next);
    if (stats)
        stats->candidates_emitted = length;
    if (entry && space_revision(space) == revision &&
        petta_program_space_store_clause_snapshot_take(
            entry, head, revision, items, length)) {
        const PettaProgramClauseSnapshot *stored =
            petta_program_space_find_clause_snapshot(
                entry, head, revision);
        if (!stored)
            return false;
        lease->items = stored->candidates;
        lease->len = stored->len;
    } else {
        lease->items = items;
        lease->len = length;
        lease->owned_items = items;
    }
    return true;
}

bool petta_program_clause_snapshot_profiled(
    PettaProgram *program, Space *space, SymbolId head,
    PettaClauseCandidate **candidates, size_t *candidate_count,
    PettaClauseSnapshotStats *stats) {
    if (candidates)
        *candidates = NULL;
    if (candidate_count)
        *candidate_count = 0u;
    if (!candidates || !candidate_count)
        return false;
    PettaClauseSnapshotLease lease = {0};
    if (!petta_program_clause_snapshot_lease_profiled(
            program, space, head, &lease, stats)) {
        return false;
    }
    if (lease.len > SIZE_MAX / sizeof(**candidates)) {
        petta_program_clause_snapshot_lease_release(&lease);
        return false;
    }
    PettaClauseCandidate *copy = lease.len
        ? cetta_malloc(sizeof(*copy) * lease.len)
        : NULL;
    if (lease.len) {
        memcpy(copy, lease.items, sizeof(*copy) * lease.len);
    }
    *candidates = copy;
    *candidate_count = lease.len;
    petta_program_clause_snapshot_lease_release(&lease);
    return true;
}

bool petta_program_clause_snapshot(
    PettaProgram *program, Space *space, SymbolId head,
    PettaClauseCandidate **candidates, size_t *candidate_count) {
    return petta_program_clause_snapshot_profiled(
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
    if (!entry || entry->clause_len == 0u)
        return true;
    if (entry->clause_len > SIZE_MAX / sizeof(**equations))
        return false;
    Atom **copy = cetta_malloc(
        sizeof(*copy) * entry->clause_len);
    for (size_t index = 0u; index < entry->clause_len; index++)
        copy[index] = entry->clauses[index].equation;
    *equations = copy;
    *equation_count = entry->clause_len;
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

bool petta_program_inferred_signatures_current(
    PettaProgram *program, Space *space) {
    PettaProgramAnalysisSpace *entry =
        petta_program_find_analysis_space(program, space);
    return entry && entry->inferred_signatures_valid &&
           entry->inferred_signature_revision ==
               space_revision(space);
}

bool petta_program_inferred_signature_lookup(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    Arena *arena, Atom **signature_out) {
    if (signature_out)
        *signature_out = NULL;
    if (!signature_out || !arena || head == SYMBOL_ID_NONE ||
        !petta_program_inferred_signatures_current(program, space) ||
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
        fresh_var_suffix());
    return *signature_out != NULL;
}

bool petta_program_inferred_signatures_lookup(
    PettaProgram *program, Space *space,
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
    if (!petta_program_inferred_signatures_current(program, space) ||
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
        copies[index] = term_universe_copy_atom_epoch(
            space->native.universe, arena,
            entry->inferred_signatures[first + index].signature_id,
            fresh_var_suffix());
        if (!copies[index]) {
            free(copies);
            return false;
        }
    }
    *signatures_out = copies;
    *count_out = count;
    return true;
}

void petta_program_inferred_signatures_reset(
    PettaProgram *program, Space *space) {
    PettaProgramAnalysisSpace *entry =
        petta_program_ensure_analysis_space(program, space);
    if (!entry)
        return;
    entry->inferred_signature_len = 0u;
    entry->inferred_signature_revision = space_revision(space);
    entry->inferred_signatures_valid = true;
}

bool petta_program_inferred_signature_put(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity, Atom *signature) {
    if (!program || !space || !space->native.universe ||
        head == SYMBOL_ID_NONE || !signature) {
        return false;
    }
    PettaProgramAnalysisSpace *entry =
        petta_program_ensure_analysis_space(program, space);
    if (!entry)
        return false;
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
    return true;
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

void petta_program_inferred_signatures_rebase(
    PettaProgram *program, Space *space) {
    PettaProgramAnalysisSpace *entry =
        petta_program_find_analysis_space(program, space);
    if (!entry || !entry->inferred_signatures_valid)
        return;
    entry->inferred_signature_revision = space_revision(space);
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
            Atom *fresh = atom_freshen_epoch(
                arena, type, fresh_var_suffix());
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
        head == g_builtin_syms.once ||
        head == g_builtin_syms.match ||
        head == g_builtin_syms.let_star ||
        head == g_builtin_syms.unify) {
        return true;
    }

    const char *name = symbol_bytes(g_symbols, head);
    return name &&
           (strcmp(name, "empty") == 0 ||
            strcmp(name, "member") == 0 ||
            strcmp(name, "last") == 0 ||
            strcmp(name, "reverse") == 0 ||
            strcmp(name, "min") == 0 ||
            strcmp(name, "max") == 0);
}

static PettaRelationSafety petta_table_safety_scan_relation(
    PettaProgram *program, Space *space,
    PettaTableSafetyRelation relation,
    PettaTableSafetyRelation **relations,
    size_t *relation_len, size_t *relation_cap) {
    PettaClauseCandidate *candidates = NULL;
    size_t candidate_count = 0u;
    if (!petta_program_clause_snapshot(
            program, space, relation.head,
            &candidates, &candidate_count)) {
        return PETTA_RELATION_SAFETY_UNSAFE;
    }

    PettaTableSafetyNode *nodes = NULL;
    size_t node_len = 0u;
    size_t node_cap = 0u;
    bool saw_matching_clause = false;
    bool safe = true;
    bool guarded_dynamic = false;
    bool trace = getenv("CETTA_PETTA_TABLE_SAFETY_TRACE") != NULL;
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
        saw_matching_clause = true;
        safe = petta_table_safety_push_node(
            &nodes, &node_len, &node_cap,
            rhs, candidates[index].rhs_plan);
        if (!safe && trace) {
            fprintf(
                stderr,
                "[petta-table-safety] head=%s arity=%u "
                "missing-or-invalid-rhs-plan clause=%zu\n",
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
    if (trace && !saw_matching_clause) {
        fprintf(
            stderr,
            "[petta-table-safety] head=%s arity=%u no-clause\n",
            symbol_bytes(g_symbols, relation.head),
            (unsigned)relation.arity);
    }
    if (!safe || !saw_matching_clause)
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

    size_t cache_slot =
        petta_table_safety_cache_slot(space, head, arity);
    PettaTableSafetyCacheEntry *cached =
        &program->table_safety_cache[cache_slot];
    uint64_t instance_id = space_instance_id(space);
    uint64_t revision = space_revision(space);
    if (cached->occupied &&
        cached->space == space &&
        cached->instance_id == instance_id &&
        cached->revision == revision &&
        cached->head == head &&
        cached->arity == arity) {
        return cached->safety;
    }

    PettaTableSafetyRelation *relations = NULL;
    size_t relation_len = 0u;
    size_t relation_cap = 0u;
    bool safe = petta_table_safety_add_relation(
        &relations, &relation_len, &relation_cap,
        head, arity);
    PettaRelationSafety safety = safe
        ? PETTA_RELATION_SAFETY_STATIC
        : PETTA_RELATION_SAFETY_UNSAFE;
    for (size_t index = 0u;
         safe && index < relation_len; index++) {
        PettaRelationSafety relation_safety =
            petta_table_safety_scan_relation(
            program, space, relations[index],
            &relations, &relation_len, &relation_cap);
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

    *cached = (PettaTableSafetyCacheEntry){
        .space = space,
        .instance_id = instance_id,
        .revision = revision,
        .head = head,
        .arity = arity,
        .safety = safety,
        .occupied = true,
    };
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
