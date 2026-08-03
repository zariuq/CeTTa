#include "petta_program.h"

#include "grounded.h"
#include "petta_semantics.h"
#include "symbol.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Atom *equation;
    const PettaPlanNode *plan;
} PettaProgramClause;

typedef struct {
    const Space *space;
    uint64_t instance_id;
    PettaProgramClause *clauses;
    size_t clause_len;
    size_t clause_cap;
} PettaProgramSpace;

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
    bool safe;
    bool occupied;
} PettaTableSafetyCacheEntry;

struct PettaProgram {
    Arena plans;
    PettaProgramSpace *spaces;
    size_t space_len;
    size_t space_cap;
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

static bool petta_plan_intrinsic_head(SymbolId head) {
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
    return head != SYMBOL_ID_NONE &&
           (petta_semantics_form(head) != PETTA_FORM_NONE ||
            head <= g_builtin_syms.native_handle ||
            is_grounded_op(head) ||
            machine_named);
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
            for (CettaExprIndex index = 0u;
                 index < node->child_count; index++) {
                if (node->children[index]
                        .contains_length_call) {
                    node->contains_length_call = true;
                    break;
                }
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
            node->role =
                petta_plan_intrinsic_head(head) ||
                petta_head_contains(heads, head)
                    ? PETTA_PLAN_STATIC_CALL
                    : PETTA_PLAN_DATA;
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

PettaProgram *petta_program_new(void) {
    PettaProgram *program = cetta_malloc(sizeof(*program));
    memset(program, 0, sizeof(*program));
    arena_init(&program->plans);
    arena_set_runtime_kind(
        &program->plans, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&program->plans, NULL);
    return program;
}

void petta_program_free(PettaProgram *program) {
    if (!program)
        return;
    for (size_t index = 0u;
         index < program->space_len; index++) {
        free(program->spaces[index].clauses);
    }
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
    if (!petta_equation_view(atom, NULL, NULL, NULL))
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
    entry->clauses[entry->clause_len++] =
        (PettaProgramClause){
            .equation = atom,
            .plan = plan,
        };
    return true;
}

void petta_program_note_remove_all(
    PettaProgram *program, Space *space, Atom *atom) {
    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    if (!entry || !atom)
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
    entry->clause_len = write;
}

void petta_program_note_remove_one(
    PettaProgram *program, Space *space, Atom *atom) {
    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    if (!entry || !atom)
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
        free(entry->clauses);
        memmove(
            entry, entry + 1u,
            sizeof(*entry) *
                (program->space_len - index - 1u));
        program->space_len--;
        return;
    }
}

static bool petta_program_copy_records(
    PettaProgram *program, const Space *source,
    Space *destination, bool replace) {
    if (!program || !source || !destination)
        return false;
    if (source == destination)
        return true;
    PettaProgramSpace *source_entry =
        petta_program_find_space(program, source);
    if (replace)
        petta_program_forget_space(program, destination);
    if (!source_entry || source_entry->clause_len == 0u)
        return true;

    /*
     * ensure_space can reallocate program->spaces, invalidating the source
     * entry pointer.  Copy the records first, then install them.
     */
    size_t count = source_entry->clause_len;
    PettaProgramClause *copy = cetta_malloc(
        sizeof(*copy) * count);
    memcpy(
        copy, source_entry->clauses,
        sizeof(*copy) * count);
    PettaProgramSpace *destination_entry =
        petta_program_ensure_space(program, destination);
    if (!destination_entry) {
        free(copy);
        return false;
    }
    free(destination_entry->clauses);
    destination_entry->clauses = copy;
    destination_entry->clause_len = count;
    destination_entry->clause_cap = count;
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

bool petta_program_clause_snapshot(
    PettaProgram *program, Space *space, SymbolId head,
    PettaClauseCandidate **candidates, size_t *candidate_count) {
    if (candidates)
        *candidates = NULL;
    if (candidate_count)
        *candidate_count = 0u;
    if (!program || !space || head == SYMBOL_ID_NONE ||
        !candidates || !candidate_count) {
        return false;
    }

    PettaProgramSpace *entry =
        petta_program_find_space(program, space);
    size_t actual_len = 0u;
    size_t actual_cap = 0u;
    Atom **actual = NULL;
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
        if (!petta_program_reserve(
                (void **)&actual, &actual_cap, actual_len + 1u,
                sizeof(*actual))) {
            free(actual);
            return false;
        }
        actual[actual_len++] = occurrence.equation;
    }

    bool *used = actual_len
        ? cetta_malloc(sizeof(*used) * actual_len)
        : NULL;
    if (used)
        memset(used, 0, sizeof(*used) * actual_len);
    size_t length = 0u;
    size_t capacity = 0u;
    PettaClauseCandidate *items = NULL;

    /*
     * The private record stream preserves PeTTa declaration order.  Space is
     * still semantic authority: a record contributes only when an equal live
     * occurrence exists, and every unmatched live occurrence is appended as
     * an unplanned oracle fallback.  This remains coherent when an unordered
     * native Space swaps storage slots after removing an unrelated fact.
     */
    if (entry) {
        for (size_t record_index = 0u;
             record_index < entry->clause_len; record_index++) {
            PettaProgramClause *record =
                &entry->clauses[record_index];
            if (!petta_clause_head_matches(
                    record->equation, head)) {
                continue;
            }
            size_t matched = SIZE_MAX;
            for (size_t index = 0u; index < actual_len; index++) {
                if (!used[index] &&
                    (record->equation == actual[index] ||
                     atom_eq(record->equation, actual[index]))) {
                    matched = index;
                    break;
                }
            }
            if (matched == SIZE_MAX) {
                for (size_t index = 0u;
                     index < actual_len; index++) {
                    if (!used[index] &&
                        atom_alpha_eq(
                            record->equation, actual[index])) {
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
                free(items);
                return false;
            }
            used[matched] = true;
            items[length++] = (PettaClauseCandidate){
                .equation = actual[matched],
                .rhs_plan =
                    petta_plan_child(record->plan, 2u),
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
            free(items);
            return false;
        }
        items[length++] = (PettaClauseCandidate){
            .equation = actual[index],
        };
    }
    free(used);
    free(actual);
    *candidates = items;
    *candidate_count = length;
    return true;
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

static bool petta_table_safety_scan_relation(
    PettaProgram *program, Space *space,
    PettaTableSafetyRelation relation,
    PettaTableSafetyRelation **relations,
    size_t *relation_len, size_t *relation_cap) {
    PettaClauseCandidate *candidates = NULL;
    size_t candidate_count = 0u;
    if (!petta_program_clause_snapshot(
            program, space, relation.head,
            &candidates, &candidate_count)) {
        return false;
    }

    PettaTableSafetyNode *nodes = NULL;
    size_t node_len = 0u;
    size_t node_cap = 0u;
    bool saw_matching_clause = false;
    bool safe = true;
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
    }
    free(candidates);

    while (safe && node_len > 0u) {
        PettaTableSafetyNode item = nodes[--node_len];
        Atom *atom = item.atom;
        const PettaPlanNode *plan = item.plan;
        if (plan->role == PETTA_PLAN_VALUE ||
            plan->role == PETTA_PLAN_DATA) {
            continue;
        }
        if (plan->role == PETTA_PLAN_DYNAMIC_CALL ||
            atom->kind != ATOM_EXPR ||
            atom->expr.len == 0u ||
            atom->expr.elems[0]->kind != ATOM_SYMBOL) {
            safe = false;
            break;
        }

        SymbolId call_head =
            atom->expr.elems[0]->sym_id;
        bool opaque = false;
        if (petta_table_safety_primitive(
                call_head, &opaque)) {
            if (opaque)
                continue;
            if (plan->child_count != atom->expr.len) {
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

        if (petta_plan_intrinsic_head(call_head) ||
            !petta_table_safety_add_relation(
                relations, relation_len, relation_cap,
                call_head, atom->expr.len - 1u)) {
            safe = false;
        }
    }
    free(nodes);
    return safe && saw_matching_clause;
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

bool petta_program_relation_table_safe(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity) {
    if (!program || !space || head == SYMBOL_ID_NONE)
        return false;

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
        return cached->safe;
    }

    PettaTableSafetyRelation *relations = NULL;
    size_t relation_len = 0u;
    size_t relation_cap = 0u;
    bool safe = petta_table_safety_add_relation(
        &relations, &relation_len, &relation_cap,
        head, arity);
    for (size_t index = 0u;
         safe && index < relation_len; index++) {
        safe = petta_table_safety_scan_relation(
            program, space, relations[index],
            &relations, &relation_len, &relation_cap);
    }
    free(relations);

    *cached = (PettaTableSafetyCacheEntry){
        .space = space,
        .instance_id = instance_id,
        .revision = revision,
        .head = head,
        .arity = arity,
        .safe = safe,
        .occupied = true,
    };
    return safe;
}
