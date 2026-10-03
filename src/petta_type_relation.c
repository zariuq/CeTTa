#include "petta_type_policy.h"

#include "eval.h"
#include "petta_semantics.h"
#include "search_machine.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "term_canon.h"

#include <stdlib.h>
#include <string.h>

/* Intrinsic PeTTa candidate order: literal/variable, application, list,
 * declaration; Undefined only when the required-type query has no candidate.
 * Values are inspected, never executed.  The explicit stack handles deeply
 * nested data independently of the evaluator stack. */

typedef struct {
    Atom **items;
    uint32_t length;
    uint32_t capacity;
} PeTTaTypeList;

typedef struct PettaTypeResourceScope {
    CettaEvalCStackBoundary stack;
    CettaEvalCompletion completion;
    struct PettaTypeResourceScope *parent;
} PettaTypeResourceScope;

static _Thread_local PettaTypeResourceScope *g_petta_type_resource_scope;

static bool petta_type_stack_available(void) {
    PettaTypeResourceScope *scope = g_petta_type_resource_scope;
    if (!scope)
        return true;
    uintptr_t here = (uintptr_t)__builtin_frame_address(0);
    uintptr_t delta = here > scope->stack.anchor
        ? here - scope->stack.anchor : scope->stack.anchor - here;
    if (scope->stack.budget_bytes != 0u &&
        (uint64_t)delta <= scope->stack.budget_bytes)
        return true;
    if (scope->completion == CETTA_EVAL_COMPLETE)
        scope->completion = CETTA_EVAL_INCOMPLETE_STACK;
    return false;
}

static bool petta_type_list_push(PeTTaTypeList *list, Atom *type);

enum {
    PETTA_TYPE_FACT_ENTRIES = 4096u,
    PETTA_TYPE_FACT_BYTES = 8u * 1024u * 1024u,
    PETTA_TYPE_FACT_PAYLOAD_BYTES = 64u * 1024u,
};

typedef struct {
    Atom *subject;
    Atom *required;
    Atom *answers;
    /* NULL and a root output variable are distinct query modes. */
    uint8_t mode;
    uint64_t generation;
} PeTTaTypeFact;

/* The intrinsic service has one fixed PeTTa authority in every profile. Its
 * facts are private to the executing thread and the request arena's allocation
 * epoch. Source pointers come from that arena or hash-cons storage; any
 * hash-cons reclamation invalidates the pointer keys before their next use.
 * No pointer from a foreign/resettable arena is retained.
 * A fact owns its hygienic payload, never an input or a caller substitution. */
static _Thread_local struct {
    PeTTaTypeFact *entries;
    Arena payloads;
    SpaceProgramToken program;
    uint32_t arena_identity;
    uint64_t arena_epoch;
    uint64_t hashcons_epoch;
    uint64_t generation;
    HashConsTable *hashcons;
    bool enabled;
    bool configured;
    bool ready;
} petta_type_facts;

void petta_type_facts_free_for_current_thread(void) {
    if (petta_type_facts.ready)
        arena_free(&petta_type_facts.payloads);
    free(petta_type_facts.entries);
    petta_type_facts.entries = NULL;
    petta_type_facts.ready = false;
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_PETTA_TYPE_FACT_RETAINED_BYTES, 0u);
}

static size_t petta_type_fact_retained_bytes(void) {
    return petta_type_facts.payloads.reserved_bytes +
           (size_t)petta_type_facts.payloads.block_count * offsetof(ArenaBlock, data) +
           petta_type_facts.payloads.external_bytes +
           petta_type_facts.payloads.symbol_cache_bytes +
           (petta_type_facts.entries
                ? sizeof(*petta_type_facts.entries) * PETTA_TYPE_FACT_ENTRIES : 0u);
}

static void petta_type_facts_clear(void) {
    if (petta_type_facts.ready)
        arena_free(&petta_type_facts.payloads);
    /* Rollback invalidates the whole index without walking it. The stamp
     * is checked before touching a payload from the released arena. */
    if (++petta_type_facts.generation == 0u) {
        memset(petta_type_facts.entries, 0,
               sizeof(*petta_type_facts.entries) * PETTA_TYPE_FACT_ENTRIES);
        petta_type_facts.generation = 1u;
    }
    arena_init_detached(&petta_type_facts.payloads);
    arena_set_block_capacity(&petta_type_facts.payloads, 4096u);
    petta_type_facts.ready = true;
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_PETTA_TYPE_FACT_RETAINED_BYTES,
        sizeof(*petta_type_facts.entries) * PETTA_TYPE_FACT_ENTRIES);
}

static bool petta_type_fact_operand(Arena *arena, Atom *atom) {
    const uint32_t required = ATOM_FLAG_TERM_STABLE | ATOM_FLAG_ARENA_CLOSED;
    return atom && !atom_has_vars(atom) &&
           (atom->flags & required) == required &&
           !atom_has_registry_refs(atom) &&
           !(atom->flags & ATOM_FLAG_HAS_IDENTITY_GROUNDED) &&
           (atom->arena_id == 0u || atom->arena_id == arena->identity);
}

static bool petta_type_fact_key(Space *space, Arena *arena, Atom *subject,
                                Atom *target, PeTTaTypeFact *key, size_t *slot) {
    if (!petta_type_facts.configured) {
        const char *reference = getenv("CETTA_PETTA_TYPE_FACTS_REFERENCE");
        petta_type_facts.enabled = !reference || strcmp(reference, "1") != 0;
        petta_type_facts.configured = true;
    }
    if (!petta_type_facts.enabled ||
        !petta_type_fact_operand(arena, subject) ||
        (target && target->kind != ATOM_VAR &&
         !petta_type_fact_operand(arena, target)) ||
        !space_type_annotations_have_only_symbol_subjects(space))
        return false;
    uint64_t hashcons_epoch = hashcons_reclamation_epoch();
    if (hashcons_epoch == UINT64_MAX) {
        petta_type_facts_free_for_current_thread();
        return false;
    }
    SpaceProgramToken program = space_program_token(space);
    if (!petta_type_facts.entries) {
        petta_type_facts.entries = calloc(
            PETTA_TYPE_FACT_ENTRIES, sizeof(*petta_type_facts.entries));
        if (!petta_type_facts.entries)
            return false;
    }
    if (!petta_type_facts.ready ||
        !space_program_token_eq(petta_type_facts.program, program) ||
        petta_type_facts.arena_identity != arena->identity ||
        petta_type_facts.arena_epoch != arena->reset_epoch ||
        petta_type_facts.hashcons_epoch != hashcons_epoch ||
        petta_type_facts.hashcons != arena->hashcons) {
        petta_type_facts_clear();
        petta_type_facts.program = program;
        petta_type_facts.arena_identity = arena->identity;
        petta_type_facts.arena_epoch = arena->reset_epoch;
        petta_type_facts.hashcons_epoch = hashcons_epoch;
        petta_type_facts.hashcons = arena->hashcons;
    }
    uint8_t mode = !target ? 0u : target->kind == ATOM_VAR ? 1u : 2u;
    Atom *required = mode == 2u ? target : NULL;
    *key = (PeTTaTypeFact){.subject = subject, .required = required, .mode = mode};
    uintptr_t hash = (uintptr_t)subject;
    hash ^= (uintptr_t)required + UINT64_C(0x9e3779b97f4a7c15) +
            (hash << 6u) + (hash >> 2u);
    hash ^= mode;
    *slot = (size_t)(hash ^ (hash >> 17u)) & (PETTA_TYPE_FACT_ENTRIES - 1u);
    return true;
}

static bool petta_type_fact_lookup(Space *space, Arena *arena, Atom *subject,
                                   Atom *target, PeTTaTypeList *out,
                                   bool *found) {
    *found = false;
    PeTTaTypeFact key;
    size_t slot;
    if (!petta_type_fact_key(space, arena, subject, target, &key, &slot))
        return true;
    PeTTaTypeFact *entry = &petta_type_facts.entries[slot];
    if (entry->generation != petta_type_facts.generation ||
        entry->subject != key.subject ||
        entry->required != key.required || entry->mode != key.mode) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PETTA_TYPE_FACT_MISS);
        return true;
    }
    *found = true;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PETTA_TYPE_FACT_HIT);
    if (entry->answers->expr.len == 0u)
        return true;
    /* One activation of the entire ordered vector retains sharing between
     * its answers while giving every invocation fresh type variables. */
    Atom *fresh = atom_has_vars(entry->answers)
        ? cetta_instantiate_frame_syntax(arena, entry->answers)
        : entry->answers;
    Atom *answers = fresh ? atom_deep_copy(arena, fresh) : NULL;
    if (!answers)
        return false;
    for (CettaExprIndex i = 0u; i < answers->expr.len; i++) {
        if (!petta_type_list_push(out, answers->expr.elems[i]))
            return false;
    }
    return true;
}

/* A bounded walk before copying rejects an oversized or resource-bearing
 * answer scheme. This is store work on a miss, never hit/key work. */
static bool petta_type_fact_payload_size(Atom *const *items, uint32_t count,
                                        size_t *bytes) {
    enum { STACK = PETTA_TYPE_FACT_PAYLOAD_BYTES / sizeof(Atom) };
    Atom *pending[STACK];
    size_t length = 0u;
    /* The owning arena keeps the temporary vector shell as well as the
     * copied scheme. Neither shell belongs to the caller's arena. */
    *bytes = 2u * (sizeof(Atom) + (size_t)count * sizeof(Atom *));
    if (*bytes > PETTA_TYPE_FACT_PAYLOAD_BYTES || count > STACK)
        return false;
    for (uint32_t i = 0u; i < count; i++)
        pending[length++] = items[i];
    while (length > 0u) {
        Atom *atom = pending[--length];
        if (!atom || (atom->kind != ATOM_VAR && atom->kind != ATOM_SYMBOL &&
                      atom->kind != ATOM_EXPR))
            return false;
        size_t shell = sizeof(Atom);
        if (atom->kind == ATOM_EXPR) {
            if (atom->expr.len > STACK - length)
                return false;
            shell += (size_t)atom->expr.len * sizeof(Atom *);
            for (CettaExprIndex i = 0u; i < atom->expr.len; i++)
                pending[length++] = atom->expr.elems[i];
        } else if (atom->kind == ATOM_VAR && atom->name_key) {
            return false;
        }
        if (shell > PETTA_TYPE_FACT_PAYLOAD_BYTES - *bytes)
            return false;
        *bytes += shell;
    }
    return true;
}

static void petta_type_fact_store(Space *space, Arena *arena, Atom *subject,
                                  Atom *target, const PeTTaTypeList *answers) {
    PeTTaTypeFact key;
    size_t slot, bytes;
    if (!petta_type_fact_key(space, arena, subject, target, &key, &slot) ||
        !petta_type_fact_payload_size(answers->items, answers->length, &bytes))
        return;
    if (petta_type_fact_retained_bytes() + bytes + 4096u >
        PETTA_TYPE_FACT_BYTES)
        petta_type_facts_clear();
    Atom *vector = atom_expr(&petta_type_facts.payloads,
                             answers->items, answers->length);
    Atom *payload = vector
        ? atom_deep_copy(&petta_type_facts.payloads, vector) : NULL;
    if (!payload) {
        petta_type_facts_clear();
        return;
    }
    /* Frame-identity owners are accounted separately from arena blocks.
     * Include them before publishing; an oversized copy is just a miss. */
    if (petta_type_fact_retained_bytes() > PETTA_TYPE_FACT_BYTES) {
        petta_type_facts_clear();
        return;
    }
    key.answers = payload;
    key.generation = petta_type_facts.generation;
    petta_type_facts.entries[slot] = key;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PETTA_TYPE_FACT_STORE);
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_PETTA_TYPE_FACT_RETAINED_BYTES,
        petta_type_fact_retained_bytes());
    cetta_runtime_stats_update_max(CETTA_RUNTIME_COUNTER_PETTA_TYPE_FACT_PEAK_BYTES,
        petta_type_fact_retained_bytes());
}

typedef struct {
    Atom *atom;
    Atom *const *elements;
    CettaExprLen length;
    CettaExprIndex next;
    PeTTaTypeList function_types;
    PeTTaTypeList *element_types;
} PeTTaTypeFrame;

static bool petta_type_list_push(PeTTaTypeList *list, Atom *type) {
    if (!list || !type)
        return false;
    if (list->length == list->capacity) {
        if (list->capacity > UINT32_MAX / 2u)
            return false;
        uint32_t capacity = list->capacity ? list->capacity * 2u : 2u;
        Atom **items = realloc(list->items, sizeof(*items) * capacity);
        if (!items)
            return false;
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->length++] = type;
    return true;
}

/* A number's, a string's or a truth value's type, which no declaration
 * overrides; NULL for every other atom. */
static Atom *petta_type_of_literal(Arena *arena, const Atom *atom) {
    if (atom->kind == ATOM_GROUNDED) {
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_BIGINT:
        case GV_RATIONAL:
        case GV_FLOAT:
            return atom_symbol(arena, "Number");
        case GV_STRING:
            return atom_symbol(arena, "String");
        default:
            break;
        }
    }
    bool truth = false;
    return petta_semantics_truth_value(atom, &truth)
        ? atom_symbol(arena, "Bool") : NULL;
}

/* The elements of X when is_list(X) holds: an expression or a list value,
 * or open cells whose last tail is one.  A list pattern, a cell with an
 * open tail, a Prolog compound and every non-expression are not lists. */
static bool petta_type_list_elements(
    Arena *arena, Atom *atom, Atom *const **elements, CettaExprLen *length) {
    if (!petta_semantics_is_open_cons_value(atom))
        return atom_sequence_view(atom, elements, length);
    size_t cells = 0u;
    Atom *end = atom;
    while (petta_semantics_is_open_cons_value(end)) {
        cells++;
        end = end->expr.elems[2];
    }
    Atom *const *tail = NULL;
    CettaExprLen tail_length = 0u;
    if (!atom_sequence_view(end, &tail, &tail_length) ||
        (uint64_t)tail_length > (uint64_t)(SIZE_MAX / sizeof(Atom *)) - cells)
        return false;
    size_t total = cells + (size_t)tail_length;
    Atom **items = total
        ? arena_alloc(arena, sizeof(*items) * total) : NULL;
    if (total && !items)
        return false;
    size_t index = 0u;
    for (Atom *cell = atom; petta_semantics_is_open_cons_value(cell);
         cell = cell->expr.elems[2])
        items[index++] = cell->expr.elems[1];
    for (CettaExprIndex i = 0u; i < tail_length; i++)
        items[index++] = tail[i];
    *elements = items;
    *length = (CettaExprLen)total;
    return true;
}

static bool petta_type_push_declared(
    Space *space, Arena *arena, Atom *subject, PeTTaTypeList *out) {
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(space, arena, subject, &declared);
    bool ok = true;
    for (uint32_t i = 0u; ok && i < count; i++)
        ok = petta_type_list_push(out, declared[i]);
    free(declared);
    return ok;
}

static bool petta_type_answers_bound(
    Space *space, Arena *arena, Atom *subject, Atom *target, PeTTaTypeList *out);
static bool petta_type_answers_bound_uncached(
    Space *space, Arena *arena, Atom *subject, Atom *target, PeTTaTypeList *out);

/* get-type(Argument, Formal) under `bindings`, for each solution the rest
 * of the arguments, then the declaration's result type. Only alternatives
 * need a nested trial; deterministic positions use the current store. */
static bool petta_type_check_arguments(
    Space *space, Arena *arena, Atom *const *arguments,
    Atom *declaration,
    CettaExprIndex index, Bindings *bindings, PeTTaTypeList *out) {
    CettaExprLen arity = declaration->expr.len - 2u;
    while (index < arity) {
        Atom *formal = declaration->expr.elems[index + 1u];
        /* Matching the result requirement may already have bound a variable
         * appearing in this argument. Only a still-free subject skips its check. */
        Atom *resolved_argument = bindings_apply_if_vars(
            bindings, arena, arguments[index]);
        if (!resolved_argument)
            return false;
        if (resolved_argument->kind == ATOM_VAR) {
            index++;
            continue;
        }

        PeTTaTypeList candidates = {0};
        Atom *resolved_formal = bindings_apply_if_vars(bindings, arena, formal);
        if (!resolved_formal || !petta_type_answers_bound(
                space, arena, resolved_argument, resolved_formal, &candidates)) {
            free(candidates.items);
            return false;
        }
        /* This store belongs to the selected declaration trial. A complete
         * singleton can refine it in place; only alternatives need copies. */
        if (candidates.length == 1u) {
            bool matched = match_atoms(candidates.items[0], formal, bindings, arena);
            free(candidates.items);
            if (!matched)
                return true;
            index++;
            continue;
        }
        bool ok = true;
        for (uint32_t i = 0u; ok && i < candidates.length; i++) {
            Bindings trial;
            bindings_init(&trial);
            if (!bindings_clone(&trial, bindings))
                ok = false;
            else if (match_atoms(candidates.items[i], formal, &trial, arena)) {
                ok = petta_type_check_arguments(
                    space, arena, arguments, declaration,
                    index + 1u, &trial, out);
            }
            bindings_free(&trial);
        }
        free(candidates.items);
        return ok;
    }
    return petta_type_list_push(
        out, bindings_apply_if_vars(
                 bindings, arena, declaration->expr.elems[arity + 1u]));
}

/* Function candidates precede structural element queries. */
static bool petta_type_function_answers(
    Space *space, Arena *arena, const PeTTaTypeFrame *frame,
    PeTTaTypeList *out) {
    Atom *head = frame->length > 0u ? frame->elements[0] : NULL;
    if (head && head->kind != ATOM_VAR) {
        Atom **declared = NULL;
        uint32_t count = space_get_declared_types(
            space, arena, head, &declared);
        bool ok = true;
        for (uint32_t i = 0u; ok && i < count; i++) {
            Atom *type = declared[i];
            if (!type || type->kind != ATOM_EXPR || type->expr.len < 2u ||
                type->expr.len - 2u != frame->length - 1u ||
                !atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow))
                continue;
            Bindings bindings;
            bindings_init(&bindings);
            ok = petta_type_check_arguments(
                space, arena, frame->elements + 1u, type, 0u, &bindings, out);
            bindings_free(&bindings);
        }
        free(declared);
        if (!ok)
            return false;
    }
    return true;
}

/* Complete a list after its function trial: structural rows only when that
 * trial was answerless, followed by the subject's declarations. */
static bool petta_type_list_answers(
    Space *space, Arena *arena, const PeTTaTypeFrame *frame,
    PeTTaTypeList *out) {
    if (out->length == 0u) {
        /* maplist(get-type, X, T): every combination of the elements'
         * answers, the last element varying fastest. */
        CettaExprLen length = frame->length;
        uint64_t total = 1u;
        for (CettaExprIndex i = 0u; i < length; i++) {
            total *= frame->element_types[i].length;
            if (total > UINT32_MAX)
                return false;
        }
        uint32_t *indices = length ? calloc((size_t)length, sizeof(*indices))
                                   : NULL;
        Atom **row = length ? malloc(sizeof(*row) * (size_t)length) : NULL;
        if (length && (!indices || !row)) {
            free(indices);
            free(row);
            return false;
        }
        bool ok = true;
        for (uint64_t produced = 0u; ok && produced < total; produced++) {
            for (CettaExprIndex i = 0u; i < length; i++)
                row[i] = frame->element_types[i].items[indices[i]];
            ok = petta_type_list_push(out, atom_expr(arena, row, length));
            for (CettaExprIndex i = length; i > 0u; i--) {
                if (++indices[i - 1u] < frame->element_types[i - 1u].length)
                    break;
                indices[i - 1u] = 0u;
            }
        }
        free(indices);
        free(row);
        if (!ok)
            return false;
    }
    return petta_type_push_declared(space, arena, frame->atom, out);
}

/* The answers of a variable, a literal or an atom that is not a list. */
static bool petta_type_leaf_answers(
    Space *space, Arena *arena, Atom *atom, PeTTaTypeList *out) {
    if (atom->kind == ATOM_VAR)
        return petta_type_list_push(
            out, atom_var_with_id(arena, "__petta_type", fresh_var_id()));
    Atom *literal = petta_type_of_literal(arena, atom);
    if (literal)
        return petta_type_list_push(out, literal);
    return petta_type_push_declared(space, arena, atom, out);
}

static void petta_type_frame_free(PeTTaTypeFrame *frame) {
    free(frame->function_types.items);
    if (frame->element_types) {
        for (CettaExprIndex i = 0u; i < frame->length; i++)
            free(frame->element_types[i].items);
        free(frame->element_types);
    }
}

static bool petta_type_frame_open(
    Space *space, Arena *arena, Atom *atom, PeTTaTypeFrame *frame, bool *is_list) {
    *frame = (PeTTaTypeFrame){.atom = atom};
    *is_list = petta_type_list_elements(
        arena, atom, &frame->elements, &frame->length);
    if (!*is_list)
        return true;
    if (!petta_type_function_answers(space, arena, frame, &frame->function_types)) {
        petta_type_frame_free(frame);
        return false;
    }
    if (frame->function_types.length > 0u || frame->length == 0u)
        return true;
    if ((uint64_t)frame->length > (uint64_t)(SIZE_MAX / sizeof(PeTTaTypeList)))
        return false;
    frame->element_types = calloc(
        (size_t)frame->length, sizeof(*frame->element_types));
    return frame->element_types != NULL;
}

/* get-type(Subject, T) for a fresh T. */
static bool petta_type_answers_fresh(
    Space *space, Arena *arena, Atom *subject, PeTTaTypeList *out) {
    /* Function-headed inference is mutually recursive through
     * frame_open -> function_answers -> check_arguments.  Check at this
     * actual recursive entry as well as at the bound-query entry: optimized
     * builds may inline the intervening bound wrapper. */
    if (!petta_type_stack_available())
        return false;
    bool found = false;
    if (!petta_type_fact_lookup(space, arena, subject, NULL, out, &found))
        return false;
    if (found)
        return true;
    cetta_runtime_stats_inc(atom_has_vars(subject)
        ? CETTA_RUNTIME_COUNTER_PETTA_TYPE_OPEN_VISIT
        : CETTA_RUNTIME_COUNTER_PETTA_TYPE_CLOSED_VISIT);
    PeTTaTypeFrame *stack = NULL;
    size_t depth = 0u;
    size_t capacity = 0u;
    bool is_list = false;
    PeTTaTypeFrame root;
    if (!petta_type_frame_open(space, arena, subject, &root, &is_list))
        return false;
    if (!is_list) {
        bool ok = petta_type_leaf_answers(space, arena, subject, out);
        if (ok && out->length == 0u)
            ok = petta_type_list_push(out, atom_undefined_type(arena));
        if (ok)
            petta_type_fact_store(space, arena, subject, NULL, out);
        return ok;
    }
    bool ok = true;
    stack = malloc(sizeof(*stack) * 8u);
    capacity = 8u;
    if (!stack) {
        petta_type_frame_free(&root);
        return false;
    }
    stack[depth++] = root;
    while (ok && depth > 0u) {
        PeTTaTypeFrame *frame = &stack[depth - 1u];
        if (frame->function_types.length == 0u && frame->next < frame->length) {
            Atom *element = frame->elements[frame->next];
            PeTTaTypeList *slot = &frame->element_types[frame->next];
            bool child_found = false;
            if (!petta_type_fact_lookup(
                    space, arena, element, NULL, slot, &child_found)) {
                ok = false;
                break;
            }
            if (child_found) {
                frame->next++;
                continue;
            }
            cetta_runtime_stats_inc(atom_has_vars(element)
                ? CETTA_RUNTIME_COUNTER_PETTA_TYPE_OPEN_VISIT
                : CETTA_RUNTIME_COUNTER_PETTA_TYPE_CLOSED_VISIT);
            PeTTaTypeFrame child;
            bool child_is_list = false;
            if (!petta_type_frame_open(space, arena, element, &child, &child_is_list)) {
                ok = false;
                break;
            }
            if (child_is_list) {
                if (depth == capacity) {
                    PeTTaTypeFrame *grown = capacity <= SIZE_MAX / (2u * sizeof(*stack))
                        ? realloc(stack, sizeof(*stack) * capacity * 2u) : NULL;
                    if (!grown) {
                        petta_type_frame_free(&child);
                        ok = false;
                        break;
                    }
                    stack = grown;
                    capacity *= 2u;
                }
                stack[depth++] = child;
                continue;
            }
            ok = petta_type_leaf_answers(space, arena, element, slot) &&
                 (slot->length > 0u ||
                 petta_type_list_push(slot, atom_undefined_type(arena)));
            if (ok)
                petta_type_fact_store(space, arena, element, NULL, slot);
            frame->next++;
            continue;
        }
        PeTTaTypeList answers = frame->function_types;
        frame->function_types = (PeTTaTypeList){0};
        ok = petta_type_list_answers(space, arena, frame, &answers) &&
             (answers.length > 0u ||
              petta_type_list_push(&answers, atom_undefined_type(arena)));
        if (ok)
            petta_type_fact_store(space, arena, frame->atom, NULL, &answers);
        petta_type_frame_free(frame);
        depth--;
        if (!ok) {
            free(answers.items);
            break;
        }
        if (depth == 0u) {
            *out = answers;
            break;
        }
        PeTTaTypeFrame *parent = &stack[depth - 1u];
        parent->element_types[parent->next++] = answers;
    }
    while (depth > 0u)
        petta_type_frame_free(&stack[--depth]);
    free(stack);
    return ok;
}

/* A supplied closed row already fixes its spine. Query its elements with
 * their actual requirements, keeping deterministic positions iterative. */
static bool petta_type_bound_elements(
    Space *space, Arena *arena, Atom *const *elements, CettaExprLen length,
    Atom *row, CettaExprIndex index, Bindings *bindings, PeTTaTypeList *out) {
    while (index < length) {
        Atom *subject = bindings_apply_if_vars(bindings, arena, elements[index]);
        if (!subject)
            return false;
        /* The variable candidate has an anonymous type operand and commits
         * once. Its private match leaves the incoming requirement untouched. */
        if (subject->kind == ATOM_VAR) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PETTA_TYPE_OPEN_VISIT);
            index++;
            continue;
        }
        Atom *formal = bindings_apply_if_vars(bindings, arena, row->expr.elems[index]);
        PeTTaTypeList answers = {0};
        if (!formal || !petta_type_answers_bound(
                space, arena, subject, formal, &answers)) {
            free(answers.items);
            return false;
        }
        /* The row trial is private to this branch. Keep its refined store
         * for a singleton; each of several answers still gets its own copy. */
        if (answers.length == 1u) {
            bool matched = match_atoms(answers.items[0], formal, bindings, arena);
            free(answers.items);
            if (!matched)
                return true;
            index++;
            continue;
        }
        bool ok = true;
        for (uint32_t i = 0u; ok && i < answers.length; i++) {
            Bindings trial;
            bindings_init(&trial);
            if (!bindings_clone(&trial, bindings))
                ok = false;
            else if (match_atoms(answers.items[i], formal, &trial, arena))
                ok = petta_type_bound_elements(
                    space, arena, elements, length, row, index + 1u, &trial, out);
            bindings_free(&trial);
        }
        free(answers.items);
        return ok;
    }
    return petta_type_list_push(out, bindings_apply_if_vars(bindings, arena, row));
}

/* maplist constructs one output cell before querying that element. An
 * input sharing the output must observe the still-open tail, rather than
 * a complete tuple of fresh fields. Each branch owns its refined spine. */
static bool petta_type_bound_spine(
    Space *space, Arena *arena, Atom *const *elements, CettaExprLen length,
    Atom *result, Atom *tail, CettaExprIndex tail_index, CettaExprIndex index,
    Bindings *bindings, PeTTaTypeList *out) {
    while (index < length) {
        /* Only the root decides the next cell. Resolve its children when
         * visited, so an ordinary tuple tail needs neither substitution of
         * every remaining field nor allocation of a copied suffix. */
        if (tail && tail->kind == ATOM_VAR) {
            BindingValue resolved;
            tail = bindings_resolve_value_exact(
                bindings, binding_value_from_atom(tail), &resolved)
                ? binding_value_materialize(arena, resolved) : NULL;
        }
        if (!tail)
            return false;
        Atom *formal = NULL;
        Atom *rest = NULL;
        CettaExprIndex rest_index = 0u;
        if (tail->kind == ATOM_VAR) {
            formal = atom_var_with_id(arena, "__petta_type", fresh_var_id());
            rest = atom_var_with_id(arena, "__petta_tail", fresh_var_id());
            Atom *cell = petta_semantics_open_cons_value(arena, formal, rest);
            if (!formal || !rest || !cell)
                return false;
            if (!match_atoms(tail, cell, bindings, arena))
                return true;
        } else if (petta_semantics_is_open_cons_value(tail)) {
            formal = tail->expr.elems[1];
            rest = tail->expr.elems[2];
        } else {
            Atom *const *fields = NULL;
            CettaExprLen count = 0u;
            if (!atom_sequence_view(tail, &fields, &count) || tail_index >= count)
                return true;
            formal = fields[tail_index];
            rest = tail;
            rest_index = tail_index + 1u;
        }
        Atom *subject = bindings_apply_if_vars(bindings, arena, elements[index]);
        formal = bindings_apply_if_vars(bindings, arena, formal);
        if (!subject || !formal)
            return false;
        if (subject->kind == ATOM_VAR) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PETTA_TYPE_OPEN_VISIT);
            tail = rest;
            tail_index = rest_index;
            index++;
            continue;
        }
        PeTTaTypeList answers = {0};
        if (!petta_type_answers_bound(space, arena, subject, formal, &answers)) {
            free(answers.items);
            return false;
        }
        if (answers.length == 1u) {
            bool matched = match_atoms(answers.items[0], formal, bindings, arena);
            free(answers.items);
            if (!matched)
                return true;
            tail = rest;
            tail_index = rest_index;
            index++;
            continue;
        }
        bool ok = true;
        for (uint32_t i = 0u; ok && i < answers.length; i++) {
            Bindings trial;
            bindings_init(&trial);
            if (!bindings_clone(&trial, bindings))
                ok = false;
            else if (match_atoms(answers.items[i], formal, &trial, arena))
                ok = petta_type_bound_spine(
                    space, arena, elements, length, result, rest,
                    rest_index, index + 1u, &trial, out);
            bindings_free(&trial);
        }
        free(answers.items);
        return ok;
    }
    if (tail_index > 0u) {
        Atom *const *fields = NULL;
        CettaExprLen count = 0u;
        /* An indexed ordinary tail is empty exactly at the tuple's end.
         * Its fields have already been checked in the same shared store. */
        return !atom_sequence_view(tail, &fields, &count) || tail_index != count ||
            petta_type_list_push(out, bindings_apply_if_vars(bindings, arena, result));
    }
    Atom *empty = atom_unit(arena);
    if (!empty)
        return false;
    return !match_atoms(tail, empty, bindings, arena) ||
        petta_type_list_push(out, bindings_apply_if_vars(bindings, arena, result));
}

static bool petta_type_append_matching(
    Arena *arena, const PeTTaTypeList *candidates, Atom *target, PeTTaTypeList *out) {
    for (uint32_t i = 0u; i < candidates->length; i++) {
        Atom *candidate = candidates->items[i];
        /* A root output variable acquires this candidate at the caller's
         * existing unification boundary; no temporary binding store is
         * needed here. Rigid symbol equality likewise learns no bindings.
         * Structured targets keep the full refining match below. */
        if (target->kind == ATOM_VAR ||
            (target->kind == ATOM_SYMBOL && candidate->kind == ATOM_SYMBOL &&
             target->sym_id == candidate->sym_id)) {
            if (!petta_type_list_push(out, candidate))
                return false;
            continue;
        }
        Bindings trial;
        bindings_init(&trial);
        bool matched = match_atoms(candidate, target, &trial, arena);
        bool ok = !matched || petta_type_list_push(
            out, bindings_apply_if_vars(&trial, arena, target));
        bindings_free(&trial);
        if (!ok)
            return false;
    }
    return true;
}

static bool petta_type_answers_bound(
    Space *space, Arena *arena, Atom *subject, Atom *target, PeTTaTypeList *out) {
    if (!petta_type_stack_available())
        return false;
    /* An independent output variable imposes no input constraint. Infer the
     * ordered answers directly instead of first constructing a private row
     * and repeatedly applying its bindings. A variable shared with the
     * subject must keep the bound query: result matching can refine an
     * argument before its type is checked. */
    if (target->kind == ATOM_VAR && subject->kind != ATOM_VAR &&
        space_type_annotations_have_only_symbol_subjects(space) &&
        !cetta_observation_atom_contains_var(subject, target->var_id))
        return petta_type_answers_fresh(space, arena, subject, out);
    bool found = false;
    if (!petta_type_fact_lookup(space, arena, subject, target, out, &found))
        return false;
    if (found)
        return true;
    cetta_runtime_stats_inc(atom_has_vars(subject)
        ? CETTA_RUNTIME_COUNTER_PETTA_TYPE_OPEN_VISIT
        : CETTA_RUNTIME_COUNTER_PETTA_TYPE_CLOSED_VISIT);
    bool ok = petta_type_answers_bound_uncached(space, arena, subject, target, out);
    if (ok)
        petta_type_fact_store(space, arena, subject, target, out);
    return ok;
}

static bool petta_type_answers_bound_uncached(
    Space *space, Arena *arena, Atom *subject, Atom *target, PeTTaTypeList *out) {
    /* Unlike fresh inference, the variable clause's bound query publishes
     * the existing requirement, without a caller-visible refinement. */
    if (subject->kind == ATOM_VAR)
        return petta_type_list_push(out, target);
    PeTTaTypeList candidates = {0};
    Atom *literal = petta_type_of_literal(arena, subject);
    bool primitive = literal != NULL;
    bool ok = true;
    if (primitive) {
        ok = petta_type_leaf_answers(space, arena, subject, &candidates) &&
             petta_type_append_matching(arena, &candidates, target, out);
        free(candidates.items);
        candidates = (PeTTaTypeList){0};
        if (!ok || out->length > 0u)
            return ok;
    } else {
        Atom *const *elements = NULL;
        CettaExprLen length = 0u;
        if (petta_type_list_elements(arena, subject, &elements, &length)) {
            PeTTaTypeFrame frame = {.elements = elements, .length = length};
            /* An element-type tuple cannot unify with a rigid scalar.  In
             * that case neither its fields nor its negative function probe
             * are needed; function and declaration answers still matter. */
            bool tuple_possible = target->kind == ATOM_VAR ||
                                  target->kind == ATOM_EXPR;
            bool function_present = false;
            if (length > 0u && elements[0]->kind != ATOM_VAR) {
                Atom **declared = NULL;
                uint32_t count = space_get_declared_types(
                    space, arena, elements[0], &declared);
                for (uint32_t i = 0u; ok && i < count; i++) {
                    PettaTypeCall call;
                    if (!petta_type_call_open(declared[i], length - 1u, &call))
                        continue;
                    Bindings bindings;
                    bindings_init(&bindings);
                    if (match_atoms(call.result_type, target, &bindings, arena))
                        ok = petta_type_check_arguments(
                            space, arena, elements + 1u,
                            declared[i], 0u, &bindings, &candidates);
                    bindings_free(&bindings);
                }
                function_present = candidates.length > 0u;
                /* The list alternative tests absence of ANY function
                 * type, not absence of a function type matching target. */
                for (uint32_t i = 0u;
                     ok && tuple_possible && !function_present && i < count; i++) {
                    if (!petta_type_call_open(declared[i], length - 1u, NULL))
                        continue;
                    Bindings bindings;
                    bindings_init(&bindings);
                    PeTTaTypeList probe = {0};
                    ok = petta_type_check_arguments(
                        space, arena, elements + 1u,
                        declared[i], 0u, &bindings, &probe);
                    function_present = probe.length > 0u;
                    free(probe.items);
                    bindings_free(&bindings);
                }
                free(declared);
            }
            ok = ok && petta_type_append_matching(arena, &candidates, target, out);
            free(candidates.items);
            candidates = (PeTTaTypeList){0};
            if (ok && tuple_possible && !function_present) {
                /* Matching fresh private fields against an ordinary row
                 * only aliases them to its requirements. Use those fields
                 * directly; the child queries still refine shared caller
                 * variables and retain every ordered alternative. Other
                 * representations keep their normal shape unification. */
                bool direct_row = target->kind == ATOM_EXPR &&
                    target->expr.len == length && !atom_is_list_form(target) &&
                    !petta_semantics_is_open_cons_value(target) &&
                    atom_petta_value_representation(target) == PETTA_VALUE_ORDINARY;
                Bindings bindings;
                bindings_init(&bindings);
                ok = direct_row
                    ? petta_type_bound_elements(
                        space, arena, frame.elements, length, target, 0u, &bindings, out)
                    : petta_type_bound_spine(
                        space, arena, frame.elements, length, target, target,
                        0u, 0u, &bindings, out);
                bindings_free(&bindings);
            }
        }
    }
    if (ok)
        ok = petta_type_push_declared(space, arena, subject, &candidates) &&
             petta_type_append_matching(arena, &candidates, target, out);
    free(candidates.items);
    if (!ok || out->length > 0u)
        return ok;
    if (target->kind == ATOM_SYMBOL)
        return !atom_is_symbol_id(target, g_builtin_syms.undefined_type) ||
               petta_type_list_push(out, target);
    PeTTaTypeList fallback = {0};
    ok = petta_type_list_push(&fallback, atom_undefined_type(arena)) &&
         petta_type_append_matching(arena, &fallback, target, out);
    free(fallback.items);
    return ok;
}

bool petta_type_intrinsic_answers(
    Space *space, Arena *arena, Atom *subject, Atom *target,
    Atom ***types_out, uint32_t *count_out,
    CettaEvalCompletion *completion_out) {
    if (completion_out)
        *completion_out = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
    if (!types_out || !count_out)
        return false;
    *types_out = NULL;
    *count_out = 0u;
    if (!space || !arena || !subject)
        return false;
    PettaTypeResourceScope scope = {
        .completion = CETTA_EVAL_COMPLETE,
        .parent = g_petta_type_resource_scope,
    };
    eval_c_stack_boundary_capture(&scope.stack);
    g_petta_type_resource_scope = &scope;
    uint64_t identity_exhaustions = cetta_frame_identity_exhaustions();
    PeTTaTypeList answers = {0};
    if (!petta_type_stack_available() ||
        !(target ? petta_type_answers_bound(space, arena, subject, target, &answers)
                 : petta_type_answers_fresh(space, arena, subject, &answers)) ||
        cetta_frame_identity_exhaustions() != identity_exhaustions) {
        /* A failed declaration lookup can appear answerless to an inner
         * traversal. None of that incomplete query's facts may survive. */
        if (scope.completion == CETTA_EVAL_COMPLETE)
            scope.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
        petta_type_facts_free_for_current_thread();
        free(answers.items);
        g_petta_type_resource_scope = scope.parent;
        if (scope.parent && scope.parent->completion == CETTA_EVAL_COMPLETE)
            scope.parent->completion = scope.completion;
        if (completion_out)
            *completion_out = scope.completion;
        return false;
    }
    for (uint32_t i = 0u; i < answers.length; i++) {
        if (petta_semantics_value_contains_observable_open_cons(answers.items[i])) {
            answers.items[i] = petta_semantics_materialize_value(arena, answers.items[i]);
            if (!answers.items[i]) {
                scope.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
                petta_type_facts_free_for_current_thread();
                free(answers.items);
                g_petta_type_resource_scope = scope.parent;
                if (scope.parent &&
                    scope.parent->completion == CETTA_EVAL_COMPLETE)
                    scope.parent->completion = scope.completion;
                if (completion_out)
                    *completion_out = scope.completion;
                return false;
            }
        }
    }
    *types_out = answers.items;
    *count_out = answers.length;
    g_petta_type_resource_scope = scope.parent;
    if (completion_out)
        *completion_out = CETTA_EVAL_COMPLETE;
    return true;
}
