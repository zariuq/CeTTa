#include "petta_program.h"

#include "eval.h"
#include "grounded.h"
#include "petta_semantics.h"
#include "stats.h"
#include "symbol.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    Atom *equation;
    const PettaPlanNode *plan;
    const PettaEquationTemplateC0 *equation_template_c0;
    const PettaEquationTemplate *equation_template;
    uint32_t static_variable_count;
    SymbolId head;
} PettaProgramClause;

struct PettaEquationTemplateC0 {
    CettaGsltGroundDenseTermProgramV1 lhs;
    CettaGsltGroundDenseTermProgramV1 rhs;
};

struct PettaEquationTemplate {
    Atom *lhs;
    Atom *rhs;
    const CettaOpenPatternPlan *lhs_match_plan;
    VarId *source_ids;
    Atom **source_variables;
    uint32_t variable_count;
};

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
    uint64_t synchronized_revision;
    bool synchronized_snapshot;
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
    PettaEquationTemplateC0 **equation_template_c0;
    size_t equation_template_c0_len;
    size_t equation_template_c0_cap;
    PettaProgramSpace *spaces;
    size_t space_len;
    size_t space_cap;
    PettaProgramAnalysisState *analysis;
    PettaHeadSet predeclared_heads;
    PettaTableSafetyCacheEntry
        table_safety_cache[PETTA_TABLE_SAFETY_CACHE_CAP];
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
        const PettaProgramVarSet *lhs_variables,
        const PettaProgramVarSet *rhs_variables,
        uint32_t variable_count) {
    if (!program || !lhs || !rhs || !lhs_variables || !rhs_variables)
        return NULL;
    PettaEquationTemplate *template = arena_alloc(
        &program->plans, sizeof(*template));
    if (!template)
        return NULL;
    *template = (PettaEquationTemplate){
        .lhs = lhs,
        .rhs = rhs,
        .variable_count = variable_count,
    };
    if (variable_count == 0u) {
        template->lhs_match_plan =
            petta_program_compile_open_pattern_plan(
                program, lhs, NULL, 0u);
        return template;
    }
    template->source_ids = arena_alloc(
        &program->plans,
        sizeof(*template->source_ids) * (size_t)variable_count);
    template->source_variables = arena_alloc(
        &program->plans,
        sizeof(*template->source_variables) * (size_t)variable_count);
    if (!template->source_ids || !template->source_variables)
        return NULL;

    size_t lhs_index = 0u;
    size_t rhs_index = 0u;
    uint32_t write = 0u;
    while (lhs_index < lhs_variables->len ||
           rhs_index < rhs_variables->len) {
        bool take_lhs = rhs_index >= rhs_variables->len ||
            (lhs_index < lhs_variables->len &&
             lhs_variables->items[lhs_index] <=
                 rhs_variables->items[rhs_index]);
        VarId id;
        Atom *variable;
        if (take_lhs) {
            id = lhs_variables->items[lhs_index];
            variable = lhs_variables->variables[lhs_index++];
            if (rhs_index < rhs_variables->len &&
                rhs_variables->items[rhs_index] == id) {
                rhs_index++;
            }
        } else {
            id = rhs_variables->items[rhs_index];
            variable = rhs_variables->variables[rhs_index++];
        }
        if (write >= variable_count || !variable ||
            variable->kind != ATOM_VAR || variable->var_id != id) {
            return NULL;
        }
        template->source_ids[write] = id;
        template->source_variables[write] = variable;
        write++;
    }
    if (write != variable_count)
        return NULL;
    template->lhs_match_plan =
        petta_program_compile_open_pattern_plan(
            program, lhs, template->source_ids,
            template->variable_count);
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
        template->source_ids, template->variable_count,
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
    if (open_admitted) {
        *equation_template_out = petta_program_compile_equation_template(
            program, lhs, rhs, &lhs_variables, &rhs_variables,
            *static_variable_count_out);
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
    *source_ids_out = template->source_ids;
    *source_variables_out = template->source_variables;
    *variable_count_out = template->variable_count;
    return template->variable_count == 0u ||
        (template->source_ids && template->source_variables);
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

bool petta_program_predeclare_equation(
    PettaProgram *program, Atom *atom) {
    if (!program || !atom)
        return false;
    SymbolId head = SYMBOL_ID_NONE;
    if (!petta_equation_view(atom, NULL, NULL, &head))
        return false;
    /*
     * A variable-headed equation is a valid PeTTa clause, but it grants no
     * named relation ownership.  Its later equation plan still participates
     * in relational matching; there is simply no static head to predeclare.
     */
    return head == SYMBOL_ID_NONE ||
           petta_head_insert(&program->predeclared_heads, head);
}

bool petta_program_head_declared(
    const PettaProgram *program, SymbolId head) {
    if (!program || head == SYMBOL_ID_NONE)
        return false;
    if (petta_head_contains(&program->predeclared_heads, head))
        return true;
    for (size_t space_index = 0u;
         space_index < program->space_len; space_index++) {
        const PettaProgramSpace *space =
            &program->spaces[space_index];
        if (!space->space ||
            space->instance_id !=
                space_instance_id(space->space)) {
            continue;
        }
        for (size_t clause_index = 0u;
             clause_index < space->clause_len; clause_index++) {
            if (space->clauses[clause_index].head == head)
                return true;
        }
    }
    return false;
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
        if (!atom || !node) {
            ok = false;
            break;
        }
        if (atom->kind != ATOM_EXPR) {
            node->role = PETTA_PLAN_VALUE;
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
            continue;
        }
        Atom *head_atom = atom->expr.elems[0];
        if (head_atom->kind == ATOM_SYMBOL) {
            SymbolId head = head_atom->sym_id;
            PeTTaForm form = petta_semantics_form(head);
            node->contains_length_call =
                form == PETTA_FORM_LENGTH;
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
                petta_head_contains(heads, head);
            node->role = constructor_slot_frame
                ? PETTA_PLAN_DATA
                : petta_program_head_is_intrinsic(head) ||
                  node->relation_head_admitted ||
                  cetta_petta_source_head_resolves_in_engine(
                      head, atom->expr.len - 1u)
                      ? PETTA_PLAN_STATIC_CALL
                      : PETTA_PLAN_DATA;
            node->execution = constructor_slot_frame
                ? PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS
                : grounded_op_is_type_pure(head)
                    ? PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS
                    : node->role == PETTA_PLAN_STATIC_CALL &&
                              form == PETTA_FORM_NONE
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
    for (size_t index = 0u;
         index < program->equation_template_c0_len; index++) {
        petta_equation_template_c0_free(program->equation_template_c0[index]);
    }
    free(program->equation_template_c0);
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
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    SymbolId head = SYMBOL_ID_NONE;
    if (!petta_equation_view(atom, &lhs, &rhs, &head))
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
    uint32_t static_variable_count = 0u;
    bool open_template_admitted = false;
    const PettaEquationTemplate *equation_template = NULL;
    PettaEquationTemplateC0 *equation_template_c0 =
        petta_program_compile_equation_template_c0(
            program, lhs, rhs, head,
            &static_variable_count,
            &open_template_admitted,
            &equation_template);
    if (equation_template && plan &&
        !petta_plan_assign_equation_variable_slots(
            atom, plan, equation_template)) {
        return false;
    }
    if (open_template_admitted &&
        !petta_plan_mark_open_template_admitted(
            petta_plan_child(plan, 2u))) {
        return false;
    }
    size_t record_index = entry->clause_len++;
    entry->clauses[record_index] =
        (PettaProgramClause){
            .equation = atom,
            .plan = plan,
            .equation_template_c0 = equation_template_c0,
            .equation_template = equation_template,
            .static_variable_count = static_variable_count,
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
    PettaHeadSet heads = {0};
    bool ok = true;
    for (CettaIndex index = 0u; ok && index < atom_count; index++) {
        Atom *atom = space_get_at64(space, index);
        SymbolId head = SYMBOL_ID_NONE;
        if (!atom) {
            ok = false;
        } else if (petta_equation_view(
                       atom, NULL, NULL, &head) &&
                   head != SYMBOL_ID_NONE) {
            ok = petta_head_insert(&heads, head);
        }
    }

    if (ok)
        petta_program_forget_space(program, space);
    for (CettaIndex index = 0u; ok && index < atom_count; index++) {
        Atom *atom = space_get_at64(space, index);
        if (!petta_program_is_equation(atom))
            continue;
        const PettaPlanNode *plan =
            petta_plan_build(program, &heads, atom);
        ok = plan && petta_program_note_add(
            program, space, atom, plan);
    }
    free(heads.items);

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
         index < check->catalog->clause_len; index++) {
        Atom *lhs = NULL;
        if (!petta_equation_view(
                check->catalog->clauses[index].equation,
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

    bool saw_clause = false;
    bool accepted = true;
    for (size_t index = 0u;
         accepted && index < check->catalog->clause_len; index++) {
        Atom *lhs = NULL;
        Atom *rhs = NULL;
        if (!petta_equation_view(
                check->catalog->clauses[index].equation,
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
        saw_clause = true;
        for (CettaExprIndex argument = 1u;
             accepted && argument < lhs->expr.len; argument++) {
            accepted = petta_portable_lhs_argument(
                check, lhs->expr.elems[argument]);
        }
        if (accepted)
            accepted = petta_portable_executable(check, rhs);
    }
    check->relations[slot_index].state = accepted && saw_clause
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
                .equation_template_c0 =
                    record->equation_template_c0,
                .equation_template =
                    record->equation_template,
                .activation_layout =
                    petta_equation_activation_layout(
                        actual[matched].equation,
                        record->static_variable_count),
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
        fresh_var_suffix());
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
        head == g_builtin_syms.reify ||
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

    if (getenv("CETTA_PETTA_TABLE_SAFETY_TRACE")) {
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
