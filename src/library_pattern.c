#include "library_pattern.h"

#include "match.h"
#include "term_graph.h"

CettaTermMatchStatus cetta_pattern_bind_many(Arena *arena,
    const CettaTermMatchPair *pairs, size_t count, BindingsBuilder *builder) {
    CettaTermMatch images;
    CettaTermMatchStatus status = term_graph_match_many(arena, pairs, count,
        CETTA_TERM_MATCH_FORWARD, atom_eq, &images);
    if (status != CETTA_TERM_MATCH_OK) return status;
    uint32_t mark = bindings_builder_save(builder);
    for (uint32_t i = 0u; i < images.count; ++i) {
        const Bindings *env = bindings_builder_bindings(builder);
        Atom *variable = images.variables[i];
        Atom *current = bindings_apply_graph((Bindings *)env, arena, variable);
        Atom *image = bindings_apply_graph((Bindings *)env, arena, images.values[i]);
        if (!current || !image) status = CETTA_TERM_MATCH_NO_MEMORY;
        else if (atom_eq(current, variable)) {
            if (!bindings_builder_add_var_fresh(builder, variable, image))
                status = CETTA_TERM_MATCH_NO_MEMORY;
        } else if (!term_graph_value_eq(current, image, atom_eq))
            status = CETTA_TERM_MATCH_MISMATCH;
        if (status != CETTA_TERM_MATCH_OK) {
            bindings_builder_rollback(builder, mark);
            return status;
        }
    }
    return CETTA_TERM_MATCH_OK;
}

uint32_t cetta_pattern_adapter_arity(SymbolId head) {
    if (head == g_builtin_syms.pat_match_forward) return 3u;
    if (head == g_builtin_syms.pat_match_reverse) return 4u;
    if (head == g_builtin_syms.pat_unify_forward) return 4u;
    if (head == g_builtin_syms.pat_let_forward) return 3u;
    if (head == g_builtin_syms.pat_case_forward) return 2u;
    return 0u;
}

CettaTermMatchStatus cetta_pattern_bind_retrieval(Arena *arena,
    Atom *patterns, Atom *rows, Atom *stored_output, bool conjunction,
    BindingsBuilder *builder) {
    size_t count = patterns->expr.len;
    if (rows->expr.len != count) return CETTA_TERM_MATCH_INVALID;
    CettaTermMatchPair *pairs = arena_alloc(arena,
        (count + (stored_output ? 1u : 0u)) * sizeof(*pairs));
    for (size_t i = 0u; i < count; ++i)
        pairs[i] = (CettaTermMatchPair){patterns->expr.elems[i],
            stored_output ? patterns->expr.elems[i] : rows->expr.elems[i]};
    if (stored_output) {
        Atom *value = conjunction ? rows : rows->expr.elems[0];
        pairs[count++] = (CettaTermMatchPair){stored_output, value};
    }
    return cetta_pattern_bind_many(arena, pairs, count, builder);
}

bool cetta_pattern_module_enabled(const CettaLibraryContext *context) {
    if (!context || !(context->active_mask & CETTA_NATIVE_IMPORT_PAT)) return false;
    const CettaProfile *profile = context->session.profile;
    return profile &&
        ((context->session.language_id == CETTA_LANGUAGE_HE &&
          profile->id == CETTA_PROFILE_HE_EXTENDED) ||
         (context->session.language_id == CETTA_LANGUAGE_PETTA &&
          profile->id == CETTA_PROFILE_PETTA_EXTENDED));
}

SymbolId cetta_pattern_resolve_head(const CettaLibraryContext *context, SymbolId head) {
    return head >= g_builtin_syms.pat_solve && head <= g_builtin_syms.pat_is_variant &&
        cetta_pattern_module_enabled(context) ? head : SYMBOL_ID_NONE;
}

static CettaCallOutcome pattern_fault(Arena *arena, Atom *head,
    Atom **arguments, uint32_t count, const char *reason) {
    Atom **elements = arena_alloc(arena, ((size_t)count + 1u) * sizeof(*elements));
    elements[0] = head;
    for (uint32_t i = 0u; i < count; ++i) elements[i + 1u] = arguments[i];
    return cetta_call_raised(atom_error(arena, atom_expr(arena, elements, count + 1u),
                                        atom_symbol(arena, reason)));
}

static CettaCallOutcome pattern_solve(Arena *arena, Atom *head,
    Atom **arguments, uint32_t count) {
    if (count != 2u)
        return pattern_fault(arena, head, arguments, count, "IncorrectNumberOfArguments");
    bool unify = atom_is_symbol(arguments[0], "unify");
    CettaTermMatchMode mode;
    if (unify || atom_is_symbol(arguments[0], "match%")) mode = CETTA_TERM_MATCH_FORWARD;
    else if (atom_is_symbol(arguments[0], "%match")) mode = CETTA_TERM_MATCH_REVERSE;
    else if (atom_is_symbol(arguments[0], "variant")) mode = CETTA_TERM_MATCH_VARIANT;
    else return pattern_fault(arena, head, arguments, count, "UnknownMatchingPolicy");
    Atom *const *rows;
    CettaExprLen length;
    if (!atom_sequence_view(arguments[1], &rows, &length))
        return pattern_fault(arena, head, arguments, count, "ExpectedConstraintPairs");
    CettaTermMatchPair *pairs = length
        ? arena_alloc(arena, (size_t)length * sizeof(*pairs)) : NULL;
    for (CettaExprIndex i = 0u; i < length; ++i) {
        Atom *const *items;
        CettaExprLen arity;
        if (!atom_sequence_view(rows[i], &items, &arity) || arity != 2u)
            return pattern_fault(arena, head, arguments, count, "ExpectedConstraintPairs");
        pairs[i] = (CettaTermMatchPair){items[0], items[1]};
    }
    Bindings bindings;
    bindings_init(&bindings);
    CettaCallOutcome result;
    Atom *witness = NULL;
    if (unify) {
        for (CettaExprIndex i = 0u; i < length; ++i) {
            if (!match_atoms(pairs[i].left, pairs[i].right, &bindings, arena)) {
                result = cetta_call_failure();
                goto done;
            }
        }
    } else {
        CettaTermMatch matched;
        CettaTermMatchStatus status = term_graph_match_many(arena, pairs, length,
                                                           mode, atom_eq, &matched);
        if (status == CETTA_TERM_MATCH_MISMATCH) {
            result = cetta_call_failure();
            goto done;
        }
        if (status != CETTA_TERM_MATCH_OK) {
            result = pattern_fault(arena, head, arguments, count,
                status == CETTA_TERM_MATCH_INVALID ? "InvalidTermGraph" : "TermCapacityLimit");
            goto done;
        }
        witness = bindings_capture_images(arena, matched.variables,
            matched.values, matched.count, mode == CETTA_TERM_MATCH_VARIANT);
    }
    if (unify)
        witness = bindings_capture_value(arena, &bindings);
    result = witness ? cetta_call_completed_value(witness)
        : pattern_fault(arena, head, arguments, count, "BindingPublicationFailed");
done:
    bindings_free(&bindings);
    return result;
}

bool cetta_pattern_module_call(CettaLibraryContext *context, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *out) {
    (void)space;
    if (!head || head->kind != ATOM_SYMBOL || !cetta_pattern_module_enabled(context))
        return false;
    SymbolId operation = cetta_pattern_resolve_head(context, head->sym_id);
    if (operation == g_builtin_syms.pat_is_instance ||
        operation == g_builtin_syms.pat_is_variant) {
        if (count != 2u) {
            *out = pattern_fault(arena, head, arguments, count, "IncorrectNumberOfArguments");
            return true;
        }
        CettaTermMatch images;
        CettaTermMatchStatus status = term_graph_match_many(arena,
            &(CettaTermMatchPair){arguments[1], arguments[0]}, 1u,
            operation == g_builtin_syms.pat_is_instance
                ? CETTA_TERM_MATCH_FORWARD : CETTA_TERM_MATCH_VARIANT,
            atom_eq, &images);
        if (status == CETTA_TERM_MATCH_OK || status == CETTA_TERM_MATCH_MISMATCH) {
            bool matched = status == CETTA_TERM_MATCH_OK;
            Atom *value = context->session.language_id == CETTA_LANGUAGE_PETTA
                ? atom_symbol(arena, matched ? "true" : "false") : atom_bool(arena, matched);
            *out = cetta_call_completed_value(value);
        } else {
            *out = pattern_fault(arena, head, arguments, count,
                status == CETTA_TERM_MATCH_INVALID ? "InvalidTermGraph" : "TermCapacityLimit");
        }
        return true;
    }
    if (operation == g_builtin_syms.pat_query ||
        operation == g_builtin_syms.pat_match_forward ||
        operation == g_builtin_syms.pat_match_reverse)
        return eval_pattern_query_ready(space, arena, head, arguments, count, out);
    if (head->sym_id == g_builtin_syms.pat_solve) {
        *out = pattern_solve(arena, head, arguments, count);
        return true;
    }
    bool apply = head->sym_id == g_builtin_syms.pat_apply;
    bool view = head->sym_id == g_builtin_syms.pat_view;
    if (!apply && !view) return false;
    if (count != (apply ? 2u : 1u)) {
        *out = pattern_fault(arena, head, arguments, count, "IncorrectNumberOfArguments");
        return true;
    }
    if (arguments[0]->kind != ATOM_GROUNDED ||
        arguments[0]->ground.gkind != GV_BINDINGS || !arguments[0]->ground.ptr) {
        *out = pattern_fault(arena, head, arguments, count, "ExpectedMatchingWitness");
        return true;
    }
    const CettaBindingsValue *witness = arguments[0]->ground.ptr;
    Atom *value = apply ? bindings_apply_saved(arena, arguments[0], arguments[1])
        : witness->observe(arena, witness);
    /* Observation, like application, owns its published syntax. */
    if (value && view) value = atom_deep_copy(arena, value);
    *out = value ? cetta_call_completed_value(value)
        : pattern_fault(arena, head, arguments, count, "BindingPublicationFailed");
    return true;
}
