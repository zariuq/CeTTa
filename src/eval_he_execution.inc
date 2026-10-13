/* The structural eager adapter qualifies source before allocating a search
 * episode. Its equation and return policies are independent of PeTTa's source
 * translation. Effectful and typed applications retain their ordinary owner. */
typedef struct {
    Atom **terms;
    size_t length, capacity;
    SymbolId *heads;
    size_t head_count, head_capacity;
    Space *space;
    Arena *arena;
    bool open_root;
    /* Streaming: answers may leave before the producer completes, so the
     * closure must produce neither Empty nor an error, which HE's completed
     * frontier would remove beside a success. */
    bool stream;
    /* Whether the closure admitted holds an effect. */
    bool effects;
    /* Whether it queries a space, and whether it unifies a pattern other
     * than a variable with a value that may be open: a stored row with
     * variables can give a stuck call whose fresh variable such a pattern
     * binds, which HE evaluates again and the machine reads as computed. */
    bool queries;
    bool structured_binder;
} HeExecutionAdmission;

/* The admission of a head's equation closure, valid while the space's
 * program (its equations and declarations) is unchanged. */
typedef struct {
    SymbolId head;
    bool open_root;
    bool stream;
    bool admitted;
    /* An admitted closure that may hold an effect (the whole walk that
     * admitted it, conservatively). */
    bool effects;
    /* It queries a space, and it unifies a pattern other than a variable
     * with a value that may be open (see he_execution_source_admitted). */
    bool queries;
    bool structured_binder;
    SpaceProgramToken token;
} HeClosureDecision;

#define HE_CLOSURE_DECISIONS 256u

static HeClosureDecision *he_closure_decision_slot(SymbolId head,
    bool open_root, bool stream) {
    HeClosureDecision *decisions = he_admission_cache(
        &g_he_admission_caches.closure_decisions,
        sizeof(HeClosureDecision) * HE_CLOSURE_DECISIONS);
    uint32_t index = (uint32_t)((((uint64_t)head << 2) | (stream << 1) |
        open_root) * 2654435761u) % HE_CLOSURE_DECISIONS;
    return &decisions[index];
}

static const HeClosureDecision *he_closure_decision_find(Space *space,
    SymbolId head, bool open_root, bool stream) {
    HeClosureDecision *slot = he_closure_decision_slot(head, open_root, stream);
    return slot->head == head && slot->open_root == open_root &&
        slot->stream == stream &&
        slot->token.space == space && space_program_token_is_current(slot->token)
        ? slot : NULL;
}

static void he_closure_decision_record(Space *space, SymbolId head,
    bool open_root, bool stream, bool admitted, bool effects, bool queries,
    bool structured_binder) {
    HeClosureDecision *slot = he_closure_decision_slot(head, open_root, stream);
    *slot = (HeClosureDecision){.head = head, .open_root = open_root,
        .stream = stream, .admitted = admitted, .effects = effects,
        .queries = queries, .structured_binder = structured_binder,
        .token = space_program_token(space)};
}

static bool he_stream_head_total(Space *space, SymbolId head,
                                 CettaExprLen arity);

static bool he_execution_intrinsic(SymbolId head) {
    return head == g_builtin_syms.let || head == g_builtin_syms.if_text ||
        head == g_builtin_syms.op_plus || head == g_builtin_syms.op_minus ||
        head == g_builtin_syms.op_mul || head == g_builtin_syms.op_eq ||
        head == g_builtin_syms.op_lt || head == g_builtin_syms.op_gt ||
        head == g_builtin_syms.op_le || head == g_builtin_syms.op_ge ||
        head == g_builtin_syms.empty_form;
}

static bool he_execution_type_name(Atom *type, const char *name) {
    return type && type->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(type), name) == 0;
}

static bool he_execution_intrinsic_signatures(HeExecutionAdmission *admission,
                                             SymbolId head) {
    Atom **types = NULL;
    uint32_t count = eval_get_atom_types_profiled(admission->space,
        admission->arena, atom_symbol_id(admission->arena, head), &types);
    unsigned mask = 0u;
    bool admitted = true;
    for (uint32_t index = 0u; admitted && index < count; index++) {
        Atom *type = types[index];
        unsigned bit = 0u;
        if (head == g_builtin_syms.empty_form &&
            atom_is_symbol_id(type, g_builtin_syms.undefined_type)) {
            bit = 1u;
        } else if (!is_function_type(type)) {
            admitted = false;
        } else if (head == g_builtin_syms.empty_form) {
            admitted = type->expr.len == 2u &&
                he_execution_type_name(type->expr.elems[1], "Atom");
            bit = 1u;
        } else {
            admitted = (type->expr.len == 4u || type->expr.len == 5u) &&
                he_execution_type_name(type->expr.elems[1], "Bool") &&
                he_execution_type_name(type->expr.elems[2], "Atom");
            if (admitted && type->expr.len == 4u) {
                admitted = atom_is_symbol_id(type->expr.elems[3],
                    g_builtin_syms.undefined_type);
                bit = 1u;
            } else if (admitted) {
                admitted = he_execution_type_name(type->expr.elems[3], "Atom") &&
                    type->expr.elems[4]->kind == ATOM_VAR;
                bit = 2u;
            }
        }
        admitted = admitted && bit && !(mask & bit);
        mask |= bit;
    }
    free(types);
    return admitted && mask == (head == g_builtin_syms.empty_form ? 1u : 3u);
}

/* A tuple headed by a number, string or truth value meets an equation only
 * through a left side without a head symbol. With none visible it is data,
 * whose elements evaluate as a constructor's do. */
static bool he_execution_scalar_head_inert(Space *space, Atom *term) {
    Atom *head = term->expr.elems[0];
    if (!head || head->kind != ATOM_GROUNDED)
        return false;
    switch (head->ground.gkind) {
    case GV_INT: case GV_FLOAT: case GV_BOOL: case GV_STRING:
    case GV_BIGINT: case GV_RATIONAL:
        return !space_equations_may_match_unnamed_head(space);
    default:
        return false;
    }
}

/* A registry entry or a thread-local resource refuses the closure. The walk
 * visits every element of an expression except its head symbol and a let's
 * pattern, which are judged here; a match over the evaluation space, its
 * space included, was admitted before this test. */
static bool he_execution_atom_holds_resource(Atom *atom) {
    return atom_has_registry_refs(atom) ||
        hyperpose_atom_has_thread_local_resource(atom);
}

static bool he_execution_term_holds_resource(Atom *term) {
    if (term->kind != ATOM_EXPR || !term->expr.len)
        return he_execution_atom_holds_resource(term);
    Atom *head = term->expr.elems[0];
    if (head->kind == ATOM_SYMBOL && he_execution_atom_holds_resource(head))
        return true;
    return atom_head_symbol_id(term) == g_builtin_syms.let &&
        term->expr.len > 1u &&
        he_execution_atom_holds_resource(term->expr.elems[1]);
}

/* Collections of completed values: a collapse's tuple, a superpose's items,
 * and the set operations over tuples. A streamed closure keeps them on the
 * canonical owner, since a superposed error or Empty, or a set operation's
 * argument fault, would reach a consumer before completion removes it. */
static bool he_execution_collection(SymbolId head) {
    return head == g_builtin_syms.collapse || head == g_builtin_syms.superpose ||
        head == g_builtin_syms.unique_atom || head == g_builtin_syms.union_atom ||
        head == g_builtin_syms.subtraction_atom ||
        head == g_builtin_syms.intersection_atom;
}

/* Conditions whose value is True, False, or the comparison left unreduced
 * (an argument that is not a number), which leaves the `if` call itself. */
static bool he_execution_condition_head(SymbolId head) {
    return head == g_builtin_syms.op_eq || head == g_builtin_syms.op_lt ||
        head == g_builtin_syms.op_gt || head == g_builtin_syms.op_le ||
        head == g_builtin_syms.op_ge;
}

/* Data with a symbol head that is not a rule or declaration form. */
static bool he_execution_effect_atom_plain(Atom *atom) {
    SymbolId head = atom_head_symbol_id(atom);
    return atom && atom->kind == ATOM_EXPR && head != SYMBOL_ID_NONE &&
        head != g_builtin_syms.equals && head != g_builtin_syms.equals_percent &&
        head != g_builtin_syms.colon;
}

static bool he_execution_push(HeExecutionAdmission *admission, Atom *term) {
    if (admission->length == admission->capacity) {
        size_t capacity = admission->capacity ? admission->capacity * 2u : 64u;
        if (capacity < admission->capacity || capacity > SIZE_MAX / sizeof(Atom *))
            return false;
        admission->terms = cetta_realloc(admission->terms, capacity * sizeof(Atom *));
        admission->capacity = capacity;
    }
    admission->terms[admission->length++] = term;
    return true;
}

static bool he_execution_primitive_signature(HeExecutionAdmission *admission,
                                             SymbolId head) {
    Atom **types = NULL;
    uint32_t count = eval_get_atom_types_profiled(admission->space,
        admission->arena, atom_symbol_id(admission->arena, head), &types);
    bool admitted = count == 1u && is_function_type(types[0]) &&
        types[0]->expr.len == 4u;
    if (admitted) {
        Atom **parts = types[0]->expr.elems;
        if (head == g_builtin_syms.op_eq) {
            admitted = parts[1]->kind == ATOM_VAR &&
                parts[2]->kind == ATOM_VAR &&
                parts[1]->var_id == parts[2]->var_id &&
                he_execution_type_name(parts[3], "Bool");
        } else {
            bool arithmetic = head == g_builtin_syms.op_plus ||
                head == g_builtin_syms.op_minus || head == g_builtin_syms.op_mul;
            admitted = he_execution_type_name(parts[1], "Number") &&
                he_execution_type_name(parts[2], "Number") &&
                he_execution_type_name(parts[3], arithmetic ? "Number" : "Bool");
        }
    }
    free(types);
    return admitted;
}

/* Recognize the structural contracts, including every physical occurrence.
 * An additional, reordered, or altered equation is not silently ignored. */
static bool he_execution_intrinsic_equations(Space *space, SymbolId head) {
    if ((eval_admitted_rule_forms() & CETTA_RULE_SYNTAX_DIRECTIONAL) &&
        space_directional_rules_may_match_known_head(space, head)) return false;
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(space, head, &cursor))
        return false;
    unsigned mask = 0u;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        SpaceEquationOccurrence occurrence;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            !space_rule_occurrence_resolve(id, eval_admitted_rule_forms(), &occurrence))
            return false;
        Atom *lhs = occurrence.lhs;
        Atom *rhs = occurrence.rhs;
        if (!lhs || lhs->kind != ATOM_EXPR || !lhs->expr.len ||
            atom_head_symbol_id(lhs) != head)
            return false;
        unsigned bit;
        if (head == g_builtin_syms.empty_form) {
            if (lhs->expr.len != 1u || !atom_is_legacy_empty_sentinel(rhs))
                return false;
            bit = 1u;
        } else {
            bool truth = false;
            if ((lhs->expr.len != 3u && lhs->expr.len != 4u) ||
                !petta_semantics_truth_value(lhs->expr.elems[1], &truth) ||
                lhs->expr.elems[2]->kind != ATOM_VAR ||
                (lhs->expr.len == 4u &&
                 lhs->expr.elems[3]->kind != ATOM_VAR))
                return false;
            bit = lhs->expr.len == 3u ? (truth ? 1u : 2u) : (truth ? 4u : 8u);
            if (lhs->expr.len == 3u && !truth) {
                if (!atom_is_computation_zero_form(rhs))
                    return false;
            } else if (!rhs || rhs->kind != ATOM_VAR ||
                       rhs->var_id != lhs->expr.elems[truth ? 2u : 3u]->var_id) {
                return false;
            }
        }
        if (mask & bit)
            return false;
        mask |= bit;
    }
    return mask == (head == g_builtin_syms.empty_form ? 1u : 15u);
}

static bool he_binding_order_holds(Space *space, const Atom *head,
                                   const Atom *body, bool *structured_out);

static bool he_execution_source_admitted(HeExecutionAdmission *admission,
                                         Atom *source) {
    if (!he_execution_push(admission, source))
        return false;
    while (admission->length) {
        Atom *term = admission->terms[--admission->length];
        if (term && term->kind == ATOM_EXPR &&
            atom_head_symbol_id(term) == g_builtin_syms.match)
            admission->queries = true;
        /* A match over this space, its rows complete before any template
         * runs. A template that is itself a match is a chain whose legs the
         * canonical owner orders; it stays there. */
        if (term && term->kind == ATOM_EXPR && term->expr.len == 4u &&
            atom_head_symbol_id(term) == g_builtin_syms.match &&
            (!admission->open_root || admission->stream) && g_registry &&
            resolve_registry_space_payload(g_registry,
                term->expr.elems[1]) == admission->space &&
            atom_head_symbol_id(term->expr.elems[3]) != g_builtin_syms.match) {
            /* A conjunction pattern is walked conjunct by conjunct. */
            Atom *pattern = term->expr.elems[2];
            bool conjunction = pattern->kind == ATOM_EXPR &&
                atom_head_symbol_id(pattern) == g_builtin_syms.comma;
            for (CettaExprIndex index = conjunction ? 1u : 0u;
                 index < (conjunction ? pattern->expr.len : 1u); index++)
                if (!he_execution_push(admission,
                        conjunction ? pattern->expr.elems[index] : pattern))
                    return false;
            if (!he_execution_push(admission, term->expr.elems[3]))
                return false;
            continue;
        }
        /* An effect on the evaluation space, where producers complete before
         * their callers consume them: the machine performs it in HE's order.
         * A rule or a declaration added or removed would change the program
         * the machine runs, so the atom must be plain data. */
        if (!admission->stream && term && term->kind == ATOM_EXPR &&
            term->expr.len == 3u &&
            (atom_head_symbol_id(term) == g_builtin_syms.add_atom ||
             atom_head_symbol_id(term) == g_builtin_syms.remove_atom) &&
            he_execution_effect_atom_plain(term->expr.elems[2]) && g_registry &&
            resolve_registry_space_payload(g_registry,
                term->expr.elems[1]) == admission->space) {
            admission->effects = true;
            if (!he_execution_push(admission, term->expr.elems[2]))
                return false;
            continue;
        }
        if (!term || he_execution_term_holds_resource(term))
            return false;
        /* A superpose's argument is a tuple of alternatives, each walked as
         * a term of its own; the tuple itself is no call. */
        if (!admission->stream && term->kind == ATOM_EXPR && term->expr.len == 2u &&
            atom_head_symbol_id(term) == g_builtin_syms.superpose &&
            term->expr.elems[1]->kind == ATOM_EXPR) {
            Atom *alternatives = term->expr.elems[1];
            for (CettaExprIndex index = 0u; index < alternatives->expr.len; index++)
                if (!he_execution_push(admission, alternatives->expr.elems[index]))
                    return false;
            continue;
        }
        /* A case evaluates its scrutinee and one branch body per answer; its
         * branch list is no call. Patterns are judged as a let's are. */
        if (!admission->stream && term->kind == ATOM_EXPR && term->expr.len == 3u &&
            atom_head_symbol_id(term) == g_builtin_syms.case_text &&
            term->expr.elems[2]->kind == ATOM_EXPR) {
            Atom *branches = term->expr.elems[2];
            if (!he_execution_push(admission, term->expr.elems[1]))
                return false;
            for (CettaExprIndex index = 0u; index < branches->expr.len; index++) {
                Atom *branch = branches->expr.elems[index];
                if (branch->kind != ATOM_EXPR || branch->expr.len != 2u ||
                    he_execution_atom_holds_resource(branch->expr.elems[0]) ||
                    !he_execution_push(admission, branch->expr.elems[1]))
                    return false;
            }
            continue;
        }
        /* let* binds in sequence, as nested lets: its values and body are
         * walked, its patterns judged as a let's is. */
        if (term->kind == ATOM_EXPR && term->expr.len == 3u &&
            atom_head_symbol_id(term) == g_builtin_syms.let_star) {
            Atom *bindings = term->expr.elems[1];
            if (bindings->kind != ATOM_EXPR)
                return false;
            for (CettaExprIndex index = 0u; index < bindings->expr.len; index++) {
                Atom *binding = bindings->expr.elems[index];
                if (binding->kind != ATOM_EXPR || binding->expr.len != 2u ||
                    he_execution_atom_holds_resource(binding->expr.elems[0]) ||
                    !he_execution_push(admission, binding->expr.elems[1]))
                    return false;
            }
            if (!he_execution_push(admission, term->expr.elems[2]))
                return false;
            continue;
        }
        /* Names passed as values can later become a selected call head.
         * Inspect their equation closure as well; merely being inert at the
         * current source position is not a purity certificate. */
        bool expression = term->kind == ATOM_EXPR && term->expr.len;
        /* HE compares numbers by value across representations (1 and 1.0);
         * a streamed relation compares them as written. */
        if (admission->stream && term->kind == ATOM_GROUNDED &&
            (term->ground.gkind == GV_INT || term->ground.gkind == GV_FLOAT ||
             term->ground.gkind == GV_BIGINT ||
             term->ground.gkind == GV_RATIONAL))
            return false;
        if (!expression && term->kind != ATOM_SYMBOL)
            continue;
        SymbolId head = expression ? atom_head_symbol_id(term) : term->sym_id;
        bool intrinsic = he_execution_intrinsic(head);
        /* Empty, and the faults of arithmetic and ordering, would reach a
         * consumer before the completed frontier could remove them. */
        if (admission->stream && intrinsic && head != g_builtin_syms.let &&
            head != g_builtin_syms.if_text && head != g_builtin_syms.op_eq)
            return false;
        if (intrinsic && head != g_builtin_syms.let &&
            head != g_builtin_syms.if_text && head != g_builtin_syms.empty_form &&
            !he_execution_primitive_signature(admission, head))
            return false;
        bool collection = !admission->stream && he_execution_collection(head);
        if (!intrinsic && head != SYMBOL_ID_NONE && !collection &&
            petta_semantics_form(head) != PETTA_FORM_NONE)
            return false;
        if ((head == g_builtin_syms.if_text ||
             head == g_builtin_syms.empty_form) &&
            !he_execution_intrinsic_signatures(admission, head))
            return false;
        /* The structural machine interprets computed heads by application.
         * HE can instead match an equation against the entire compound head,
         * including a closed SKI term. That boundary needs HE's canonical
         * owner until the machine has a whole-head equation policy. */
        if (expression && head == SYMBOL_ID_NONE &&
            (admission->open_root || term->expr.elems[0]->kind != ATOM_VAR) &&
            !he_execution_scalar_head_inert(admission->space, term))
            return false;
        if (expression && head == g_builtin_syms.if_text) {
            bool truth;
            if ((term->expr.len != 3u && term->expr.len != 4u) ||
                (!petta_semantics_truth_value(term->expr.elems[1], &truth) &&
                 (!he_execution_condition_head(
                      atom_head_symbol_id(term->expr.elems[1])) ||
                  expr_nargs(term->expr.elems[1]) != 2u)))
                return false;
        }
        if (intrinsic && eval_space_has_equations(admission->space, head) &&
            ((head != g_builtin_syms.if_text && head != g_builtin_syms.empty_form) ||
             !he_execution_intrinsic_equations(admission->space, head)))
            return false;
        if (!intrinsic && head != SYMBOL_ID_NONE && !collection &&
            (is_grounded_op(head) || symbol_id_is_builtin(head) ||
             space_head_declares_type(admission->space, head)))
            return false;
        /* A streamed call of a relation must always meet an equation. */
        if (admission->stream && expression && !intrinsic &&
            head != SYMBOL_ID_NONE &&
            eval_space_has_equations(admission->space, head) &&
            !he_stream_head_total(admission->space, head, term->expr.len - 1u))
            return false;
        if (!intrinsic && head != SYMBOL_ID_NONE &&
            eval_space_has_equations(admission->space, head)) {
            bool seen = false;
            for (size_t index = 0u; index < admission->head_count; index++)
                seen |= admission->heads[index] == head;
            const HeClosureDecision *decided = seen ? NULL :
                he_closure_decision_find(admission->space, head,
                                         admission->open_root, admission->stream);
            if (decided && !decided->admitted)
                return false;
            if (decided) {
                admission->effects |= decided->effects;
                admission->queries |= decided->queries;
                admission->structured_binder |= decided->structured_binder;
            }
            if (!seen && !decided) {
                if (admission->head_count == admission->head_capacity) {
                    admission->head_capacity = admission->head_capacity
                        ? admission->head_capacity * 2u : 16u;
                    admission->heads = cetta_realloc(admission->heads,
                        admission->head_capacity * sizeof(SymbolId));
                }
                admission->heads[admission->head_count++] = head;
                SpaceEquationCursor cursor;
                if (!space_rule_cursor_init(admission->space, head, eval_admitted_rule_forms(), &cursor))
                    return false;
                for (;;) {
                    SpaceEquationOccurrenceId id;
                    SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
                    if (step == SPACE_EQUATION_CURSOR_END)
                        break;
                    SpaceEquationOccurrence occurrence;
                    /* Pattern constants can become callable values through
                     * a fresh match binding, just as body constants can. */
                    if (step != SPACE_EQUATION_CURSOR_ITEM ||
                        !space_rule_occurrence_resolve(id, eval_admitted_rule_forms(), &occurrence) ||
                        !he_binding_order_holds(admission->space, occurrence.lhs,
                                                occurrence.rhs,
                                                &admission->structured_binder) ||
                        !he_execution_push(admission, occurrence.lhs) ||
                        !he_execution_push(admission, occurrence.rhs))
                        return false;
                }
            }
        }
        CettaExprIndex first = head == g_builtin_syms.let ? 2u :
            head == SYMBOL_ID_NONE ? 0u : 1u;
        for (CettaExprIndex index = first; expression && index < term->expr.len; index++) {
            if (!he_execution_push(admission, term->expr.elems[index]))
                return false;
        }
    }
    return true;
}

static bool he_stream_admitted(Space *space, Arena *arena, SymbolId head);

/* HE answers an unmatched call with the call itself; a streamed relation
 * has no answer for it. A head some equation of which takes any arguments
 * of this arity (distinct variables) never leaves a call unmatched. */
static bool he_stream_head_total(Space *space, SymbolId head,
                                 CettaExprLen arity) {
    SpaceEquationCursor cursor;
    if (!space_rule_cursor_init(space, head, eval_admitted_rule_forms(), &cursor))
        return false;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            return false;
        SpaceEquationOccurrence occurrence;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            !space_rule_occurrence_resolve(id, eval_admitted_rule_forms(),
                                           &occurrence))
            return false;
        Atom *lhs = occurrence.lhs;
        if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != arity + 1u)
            continue;
        bool general = true;
        for (CettaExprIndex i = 1u; general && i < lhs->expr.len; i++) {
            Atom *argument = lhs->expr.elems[i];
            general = argument->kind == ATOM_VAR;
            for (CettaExprIndex j = 1u; general && j < i; j++)
                general = lhs->expr.elems[j]->var_id != argument->var_id;
        }
        if (general)
            return true;
    }
}

/* A value HE would not evaluate further: no variable, and no symbol or
 * expression head with equations, a grounded operation or a builtin. */
static bool he_stream_value_inert(Space *space, const Atom *value) {
    if (!value)
        return false;
    switch (value->kind) {
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        return true;
    case ATOM_SYMBOL:
        return !eval_space_has_equations(space, value->sym_id) &&
            !is_grounded_op(value->sym_id) && !symbol_id_is_builtin(value->sym_id);
    case ATOM_EXPR:
        for (CettaExprIndex i = 0u; i < value->expr.len; i++)
            if (!he_stream_value_inert(space, value->expr.elems[i]))
                return false;
        return true;
    }
    return false;
}

/* Whether calls of `head` with `arity` arguments may stream: the relation
 * is admitted and always meets an equation. Decided once per program
 * revision. */
typedef struct {
    SymbolId head;
    CettaExprLen arity;
    bool admitted;
    SpaceProgramToken token;
} HeStreamHeadDecision;

#define HE_STREAM_HEAD_DECISIONS 256u

static bool he_stream_head_admitted(Space *space, Arena *arena,
                                    SymbolId head, CettaExprLen arity) {
    if (!space || head == SYMBOL_ID_NONE)
        return false;
    HeStreamHeadDecision *decisions = he_admission_cache(
        &g_he_admission_caches.stream_head_decisions,
        sizeof(HeStreamHeadDecision) * HE_STREAM_HEAD_DECISIONS);
    HeStreamHeadDecision *slot = &decisions[
        (uint32_t)(((uint64_t)head * 31u + arity) * 2654435761u) %
        HE_STREAM_HEAD_DECISIONS];
    if (slot->head == head && slot->arity == arity &&
        slot->token.space == space && space_program_token_is_current(slot->token))
        return slot->admitted;
    bool admitted = eval_space_has_equations(space, head) &&
        he_stream_head_total(space, head, arity) &&
        he_stream_admitted(space, arena, head);
    *slot = (HeStreamHeadDecision){.head = head, .arity = arity,
        .admitted = admitted, .token = space_program_token(space)};
    return admitted;
}

/* The arguments of a streamed call are values, as the relation tier takes
 * them. */
static bool he_stream_call_admitted(Space *space, Arena *arena, Atom *call) {
    if (!call || call->kind != ATOM_EXPR || !call->expr.len ||
        call->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    if (!he_stream_head_admitted(space, arena, call->expr.elems[0]->sym_id,
                                 call->expr.len - 1u))
        return false;
    for (CettaExprIndex i = 1u; i < call->expr.len; i++)
        if (!he_stream_value_inert(space, call->expr.elems[i]))
            return false;
    return true;
}

/* Whether a call of `head` on the shared machine may stream its answers
 * through the relation tier: its closure, walked under the streaming rules,
 * is decided once per program revision. */
static bool he_stream_admitted(Space *space, Arena *arena, SymbolId head) {
    if (!space || head == SYMBOL_ID_NONE || space_has_dependencies(space) ||
        !eval_space_has_equations(space, head))
        return false;
    const HeClosureDecision *decided =
        he_closure_decision_find(space, head, true, true);
    if (decided)
        return decided->admitted;
    HeExecutionAdmission admission = {.space = space, .arena = arena,
        .open_root = true, .stream = true};
    bool admitted = he_execution_source_admitted(
        &admission, atom_symbol_id(arena, head)) &&
        !(admission.queries && admission.structured_binder);
    if (admitted) {
        for (size_t index = 0u; index < admission.head_count; index++)
            he_closure_decision_record(space, admission.heads[index],
                                       true, true, true, admission.effects,
                                       admission.queries,
                                       admission.structured_binder);
    } else {
        he_closure_decision_record(space, head, true, true, false, false,
                                   false, false);
    }
    free(admission.terms);
    free(admission.heads);
    return admitted;
}

typedef struct {
    VarId *items;
    size_t len, cap;
} HeBoundVariables;

static bool he_bound_contains(const HeBoundVariables *bound, VarId id) {
    for (size_t index = bound->len; index > 0u; index--)
        if (bound->items[index - 1u] == id)
            return true;
    return false;
}

static void he_bound_add_pattern(HeBoundVariables *bound, const Atom *pattern) {
    if (!pattern || !atom_has_vars(pattern))
        return;
    if (pattern->kind == ATOM_VAR) {
        if (bound->len == bound->cap) {
            bound->cap = bound->cap ? bound->cap * 2u : 8u;
            bound->items = cetta_realloc(bound->items,
                                         bound->cap * sizeof(*bound->items));
        }
        bound->items[bound->len++] = pattern->var_id;
        return;
    }
    if (pattern->kind == ATOM_EXPR)
        for (CettaExprIndex index = 0u; index < pattern->expr.len; index++)
            he_bound_add_pattern(bound, pattern->expr.elems[index]);
}

/* Whether `term` has a variable not bound by an enclosing binder of `term`
 * itself. The binders are a match's pattern over its template, a let's
 * pattern over its body, let*'s patterns in sequence, and a case's branch
 * pattern over its branch body. */
static bool he_free_variable_under(HeBoundVariables *bound, const Atom *term) {
    if (!term || !atom_has_vars(term))
        return false;
    if (term->kind == ATOM_VAR)
        return !he_bound_contains(bound, term->var_id);
    if (term->kind != ATOM_EXPR || term->expr.len == 0u)
        return false;
    SymbolId head = atom_head_symbol_id(term);
    size_t mark = bound->len;
    bool free_found = false;
    if (head == g_builtin_syms.match && term->expr.len == 4u) {
        free_found = he_free_variable_under(bound, term->expr.elems[1]);
        he_bound_add_pattern(bound, term->expr.elems[2]);
        free_found = free_found || he_free_variable_under(bound, term->expr.elems[3]);
    } else if (head == g_builtin_syms.let && term->expr.len == 4u) {
        free_found = he_free_variable_under(bound, term->expr.elems[2]);
        he_bound_add_pattern(bound, term->expr.elems[1]);
        free_found = free_found || he_free_variable_under(bound, term->expr.elems[3]);
    } else if (head == g_builtin_syms.let_star && term->expr.len == 3u &&
               term->expr.elems[1]->kind == ATOM_EXPR) {
        Atom *pairs = term->expr.elems[1];
        for (CettaExprIndex index = 0u; !free_found && index < pairs->expr.len; index++) {
            Atom *pair = pairs->expr.elems[index];
            if (pair->kind != ATOM_EXPR || pair->expr.len != 2u) {
                free_found = true;
                break;
            }
            free_found = he_free_variable_under(bound, pair->expr.elems[1]);
            he_bound_add_pattern(bound, pair->expr.elems[0]);
        }
        free_found = free_found || he_free_variable_under(bound, term->expr.elems[2]);
    } else if (head == g_builtin_syms.case_text && term->expr.len == 3u &&
               term->expr.elems[2]->kind == ATOM_EXPR) {
        free_found = he_free_variable_under(bound, term->expr.elems[1]);
        Atom *branches = term->expr.elems[2];
        for (CettaExprIndex index = 0u; !free_found && index < branches->expr.len; index++) {
            Atom *branch = branches->expr.elems[index];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u) {
                free_found = true;
                break;
            }
            size_t branch_mark = bound->len;
            he_bound_add_pattern(bound, branch->expr.elems[0]);
            free_found = he_free_variable_under(bound, branch->expr.elems[1]);
            bound->len = branch_mark;
        }
    } else {
        for (CettaExprIndex index = 0u; !free_found && index < term->expr.len; index++)
            free_found = he_free_variable_under(bound, term->expr.elems[index]);
    }
    bound->len = mark;
    return free_found;
}

static bool he_execution_has_free_variable(const Atom *root) {
    HeBoundVariables bound = {0};
    bool free_found = he_free_variable_under(&bound, root);
    free(bound.items);
    return free_found;
}

/*
 * HE evaluates a form's body after its binders with their bindings applied,
 * so a value computed while one of its variables was unbound is computed
 * again once a later binder binds that variable: `(let $a (+ $b 1) (let $b 1
 * $a))` is 2, and let* is nested lets. The machine reads a bound value as
 * computed, so a body that reads a variable in an evaluated position before
 * the binder that binds it stays with the canonical frames, which evaluate
 * the instantiated body. So do a pattern other than a variable unified with
 * a value that reads an unbound variable, and an equation whose body passes a
 * variable no binder binds to a call (its stuck result can be bound into by a
 * caller's pattern). Bindings are scoped by their form, as in
 * he_free_variable_under; a quoted term is read as data.
 */
typedef struct {
    Space *space;
    bool structured_binder;
    HeBoundVariables bound;
    HeBoundVariables unbound_reads;
    /* Variables read unbound as an argument of a call that can reduce once
     * they are bound: a grounded or builtin operation, or a head with
     * equations. */
    HeBoundVariables stuck_reads;
} HeBindingOrder;

static bool he_binding_order_walk(HeBindingOrder *order, const Atom *term);

static bool he_binding_order_bind(HeBindingOrder *order, const Atom *pattern) {
    if (!pattern || !atom_has_vars(pattern))
        return true;
    if (pattern->kind == ATOM_VAR) {
        if (he_bound_contains(&order->bound, pattern->var_id))
            return true;
        if (he_bound_contains(&order->unbound_reads, pattern->var_id))
            return false;
        he_bound_add_pattern(&order->bound, pattern);
        return true;
    }
    if (pattern->kind == ATOM_EXPR)
        for (CettaExprIndex index = 0u; index < pattern->expr.len; index++)
            if (!he_binding_order_bind(order, pattern->expr.elems[index]))
                return false;
    return true;
}

/* Whether `term` reads a variable that is unbound at this point. A pattern
 * other than a variable, unified with such a value, can bind inside it. */
static bool he_binding_order_reads_unbound(const HeBindingOrder *order,
                                           const Atom *term) {
    if (!term || !atom_has_vars(term))
        return false;
    if (term->kind == ATOM_VAR)
        return !he_bound_contains(&order->bound, term->var_id);
    if (term->kind != ATOM_EXPR ||
        atom_head_symbol_id(term) == g_builtin_syms.quote)
        return false;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++)
        if (he_binding_order_reads_unbound(order, term->expr.elems[index]))
            return true;
    return false;
}

/* A binder of `pattern` to `value`: the value is read first. */
/* A value that may be open: a call, or a term with variables. */
static bool he_binding_order_may_be_open(const Atom *value) {
    return value && (value->kind == ATOM_EXPR || atom_has_vars(value));
}

static bool he_binding_order_pair(HeBindingOrder *order, const Atom *pattern,
                                  const Atom *value) {
    if (pattern && pattern->kind != ATOM_VAR &&
        he_binding_order_may_be_open(value))
        order->structured_binder = true;
    if (pattern && pattern->kind != ATOM_VAR && atom_has_vars(pattern) &&
        he_binding_order_reads_unbound(order, value))
        return false;
    return he_binding_order_walk(order, value) &&
           he_binding_order_bind(order, pattern);
}

static bool he_binding_order_walk(HeBindingOrder *order, const Atom *term) {
    if (!term || !atom_has_vars(term))
        return true;
    if (term->kind == ATOM_VAR) {
        if (!he_bound_contains(&order->bound, term->var_id) &&
            !he_bound_contains(&order->unbound_reads, term->var_id))
            he_bound_add_pattern(&order->unbound_reads, term);
        return true;
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0u)
        return true;
    SymbolId head = atom_head_symbol_id(term);
    if (head == g_builtin_syms.quote)
        return true;
    size_t mark = order->bound.len;
    bool ok = true;
    if ((head == g_builtin_syms.let || head == g_builtin_syms.chain) &&
        term->expr.len == 4u) {
        /* (let pattern value body), (chain value variable body) */
        bool chain = head == g_builtin_syms.chain;
        ok = he_binding_order_pair(order, term->expr.elems[chain ? 2u : 1u],
                                   term->expr.elems[chain ? 1u : 2u]) &&
             he_binding_order_walk(order, term->expr.elems[3]);
    } else if (head == g_builtin_syms.let_star && term->expr.len == 3u &&
               term->expr.elems[1]->kind == ATOM_EXPR) {
        Atom *pairs = term->expr.elems[1];
        for (CettaExprIndex index = 0u; ok && index < pairs->expr.len; index++) {
            Atom *pair = pairs->expr.elems[index];
            if (pair->kind != ATOM_EXPR || pair->expr.len != 2u) {
                ok = he_binding_order_walk(order, pair);
                continue;
            }
            ok = he_binding_order_pair(order, pair->expr.elems[0],
                                       pair->expr.elems[1]);
        }
        ok = ok && he_binding_order_walk(order, term->expr.elems[2]);
    } else if (head == g_builtin_syms.match && term->expr.len == 4u) {
        ok = he_binding_order_walk(order, term->expr.elems[1]) &&
             he_binding_order_bind(order, term->expr.elems[2]) &&
             he_binding_order_walk(order, term->expr.elems[3]);
    } else if (head == g_builtin_syms.unify && term->expr.len == 5u) {
        order->structured_binder = true;
        ok = !he_binding_order_reads_unbound(order, term->expr.elems[1]) &&
             !he_binding_order_reads_unbound(order, term->expr.elems[2]) &&
             he_binding_order_bind(order, term->expr.elems[1]) &&
             he_binding_order_bind(order, term->expr.elems[2]) &&
             he_binding_order_walk(order, term->expr.elems[3]);
        order->bound.len = mark;
        ok = ok && he_binding_order_walk(order, term->expr.elems[4]);
    } else if (head == g_builtin_syms.case_text && term->expr.len == 3u &&
               term->expr.elems[2]->kind == ATOM_EXPR) {
        bool open_scrutinee =
            he_binding_order_reads_unbound(order, term->expr.elems[1]);
        if (he_binding_order_may_be_open(term->expr.elems[1])) {
            Atom *cases = term->expr.elems[2];
            for (CettaExprIndex index = 0u; index < cases->expr.len; index++) {
                Atom *branch = cases->expr.elems[index];
                if (branch->kind == ATOM_EXPR && branch->expr.len == 2u &&
                    branch->expr.elems[0]->kind != ATOM_VAR)
                    order->structured_binder = true;
            }
        }
        ok = he_binding_order_walk(order, term->expr.elems[1]);
        Atom *branches = term->expr.elems[2];
        for (CettaExprIndex index = 0u; ok && index < branches->expr.len; index++) {
            Atom *branch = branches->expr.elems[index];
            size_t branch_mark = order->bound.len;
            ok = branch->kind == ATOM_EXPR && branch->expr.len == 2u
                ? !(open_scrutinee && branch->expr.elems[0]->kind != ATOM_VAR &&
                    atom_has_vars(branch->expr.elems[0])) &&
                  he_binding_order_bind(order, branch->expr.elems[0]) &&
                  he_binding_order_walk(order, branch->expr.elems[1])
                : he_binding_order_walk(order, branch);
            order->bound.len = branch_mark;
        }
    } else {
        bool computes = head != SYMBOL_ID_NONE &&
            (is_grounded_op(head) || symbol_id_is_builtin(head) ||
             (order->space && eval_space_has_equations(order->space, head)));
        for (CettaExprIndex index = 0u; ok && index < term->expr.len; index++) {
            const Atom *child = term->expr.elems[index];
            if (computes && index > 0u && child->kind == ATOM_VAR &&
                !he_bound_contains(&order->bound, child->var_id) &&
                !he_bound_contains(&order->stuck_reads, child->var_id))
                he_bound_add_pattern(&order->stuck_reads, child);
            ok = he_binding_order_walk(order, child);
        }
    }
    order->bound.len = mark;
    return ok;
}

/* Whether every variable a binder of `body` binds is unbound until that
 * binder runs, the equation's own head (when given) binding first. */
static bool he_binding_order_holds(Space *space, const Atom *head,
                                   const Atom *body, bool *structured_out) {
    if (!body || body->kind != ATOM_EXPR)
        return true;
    HeBindingOrder order = {.space = space};
    he_bound_add_pattern(&order.bound, head);
    bool ok = he_binding_order_walk(&order, body);
    /* An equation whose body passes a variable no binder binds to a call
     * returns that call stuck, and a later pattern of its caller can bind
     * inside it. Free variables in data are the caller's to bind. The root's
     * own free variables are its caller's. */
    ok = ok && (!head || order.stuck_reads.len == 0u);
    free(order.bound.items);
    free(order.unbound_reads.items);
    free(order.stuck_reads.items);
    if (structured_out)
        *structured_out |= order.structured_binder;
    return ok;
}

static bool he_shared_execution_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("CETTA_HE_SHARED_EXECUTION");
        enabled = !setting || strcmp(setting, "1") == 0;
    }
    return enabled;
}

/* Whether a term computes anywhere: an application of something with
 * equations, a grounded or builtin operation other than the structural
 * forms around it, or a head that is not a symbol. */
static bool he_execution_term_computes(Space *space, const Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u)
        return false;
    const Atom *head = term->expr.elems[0];
    if (head->kind != ATOM_SYMBOL)
        return true;
    SymbolId id = head->sym_id;
    bool structural = id == g_builtin_syms.match || id == g_builtin_syms.comma ||
        he_execution_collection(id);
    if (!structural && (eval_space_has_equations(space, id) ||
                        is_grounded_op(id) || symbol_id_is_builtin(id)))
        return true;
    for (CettaExprIndex index = 1u; index < term->expr.len; index++)
        if (he_execution_term_computes(space, term->expr.elems[index]))
            return true;
    return false;
}

/* A call whose arguments read no equations: a refusal is its head's. */
static bool he_execution_plain_arguments(Atom *source) {
    for (CettaExprIndex index = 1u; index < source->expr.len; index++)
        if (source->expr.elems[index]->kind == ATOM_EXPR)
            return false;
    return true;
}

static bool he_shared_execution_try(Space *space, Arena *arena, Atom *source,
    Atom *type, int fuel, const Bindings *environment, bool preserve_bindings,
    OutcomeSet *outcomes) {
    if (eval_current_language_id() != CETTA_LANGUAGE_HE)
        return false;
    /* A completed scalar is interpreted by HE's value/cast boundary. Only
     * expression calls can acquire a compiled computation owner. */
    if (!source || source->kind != ATOM_EXPR ||
        !he_shared_execution_enabled() || g_he_shared_execution ||
        cetta_parallel_worker_active() ||
        !g_library_context || fuel >= 0 ||
        (type && !atom_is_symbol_id(type, g_builtin_syms.undefined_type)) ||
        /* An overlay reads its rows through its base; the shared program
         * and the machine's queries read a space's own rows. */
        space->overlay_base ||
        space_has_dependencies(space) || g_active_search_controller_requested ||
        g_active_search_controller_auto || gslt_match_chain_trace_active() ||
        petta_eval_machine_diagnostic_transition_limit())
        return false;
    /* Intrinsic controls retain the canonical demand adapter; their ordinary
     * children may select a compiled realization at their own call boundary. */
    SymbolId source_head = atom_head_symbol_id(source);
    if (he_execution_intrinsic(source_head))
        return false;
    /* A grounded or builtin operation never owns a compiled computation; the
     * admission walk would refuse it at its first term. A match and the
     * collection forms are the builtins the walk judges itself. */
    if (source_head != SYMBOL_ID_NONE &&
        (is_grounded_op(source_head) || symbol_id_is_builtin(source_head)) &&
        source_head != g_builtin_syms.match &&
        !he_execution_collection(source_head))
        return false;
    /* Such a root gains on the machine only when something in it computes;
     * a match that only builds data is the canonical frame's single query. */
    if ((source_head == g_builtin_syms.match ||
         he_execution_collection(source_head)) &&
        !he_execution_term_computes(space, source))
        return false;
    /* A closed data tuple computes only in its fields.  Each field that is a
     * call meets its own call boundary, where a closed realization is tried
     * before any program is synchronized. */
    if (source_head != SYMBOL_ID_NONE && !atom_has_vars(source) &&
        !eval_space_has_equations(space, source_head))
        return false;
    /* The source qualification and the program observation form one read
     * window. A suspended program owner alone cannot establish currency.
     * Workers retain their canonical adapter until shared program mutation
     * and compiled episodes have a separately qualified concurrent contract. */
    CETTA_SCOPED_SHARED_TRANSITION(he_execution_observation);
    /* A root is open when a caller can bind or observe one of its
     * variables: a caller that keeps the root's bindings observes them all,
     * and any caller binds a free one. When the caller keeps none, a
     * variable bound inside the root (by a match pattern within its
     * template, a let pattern within its body, a case pattern within its
     * branch) is the root's own. */
    bool open_root = atom_has_vars(source) &&
        (preserve_bindings || he_execution_has_free_variable(source));
    HeExecutionAdmission admission = {.space = space, .arena = arena,
        .open_root = open_root};
    bool admitted = he_binding_order_holds(space, NULL, source,
                                           &admission.structured_binder) &&
        he_execution_source_admitted(&admission, source) &&
        !(admission.queries && admission.structured_binder);
    /* Every closure walked to the end is admitted; a refused call with plain
     * arguments refuses its head's closure. */
    if (admitted) {
        for (size_t index = 0u; index < admission.head_count; index++)
            he_closure_decision_record(space, admission.heads[index],
                                       admission.open_root, false, true,
                                       admission.effects, admission.queries,
                                       admission.structured_binder);
    } else if (source_head != SYMBOL_ID_NONE &&
               !symbol_id_is_builtin(source_head) &&
               he_execution_plain_arguments(source) &&
               eval_space_has_equations(space, source_head)) {
        he_closure_decision_record(space, source_head, admission.open_root,
                                   false, false, false, false, false);
    }
    free(admission.terms);
    free(admission.heads);
    if (!admitted)
        return false;
    /* Retain the existing qualified closed realizations before allocating a
     * relational episode. A decline publishes nothing and leaves the source
     * for the general machine; open calls never enter this closed attempt. */
    if (!admission.open_root && source && source->kind == ATOM_EXPR &&
        source_head != SYMBOL_ID_NONE && !symbol_id_is_builtin(source_head)) {
        Bindings empty;
        bindings_init(&empty);
        const Bindings *outer = environment ? environment : &empty;
        PreparedPureProgramCache *cache = eval_execution_prepared_cache();
        Atom *single = prepared_pure_closed_call_try(
            space, arena, source, fuel, false, false, cache);
        if (single) {
            outcome_set_add_prefixed(arena, outcomes, single, &empty, outer,
                preserve_bindings);
            return true;
        }
        if (eval_unwinding() || eval_cancel_check())
            return true;
        CettaPreparedPureAnswersResult answers = prepared_pure_closed_answers_try(
            space, arena, source, fuel, cache, outer, preserve_bindings, outcomes);
        if (answers == CETTA_PREPARED_PURE_ANSWERS_COMPLETE ||
            answers == CETTA_PREPARED_PURE_ANSWERS_STOPPED)
            return true;
    }
    if (!g_library_context->petta_program)
        g_library_context->petta_program = petta_program_new_with_rules(eval_admitted_rule_forms());
    if (!g_library_context->petta_program ||
        !petta_program_synchronize_space(g_library_context->petta_program, space))
        return false;
    bool enclosing_active = g_active_petta_search_machine;
    g_he_shared_execution = true;
    g_he_shared_execution_effect_free = !admission.effects;
    g_active_petta_search_machine = true;
    bool complete = petta_eval_machine_run(space, arena, source, type, fuel,
        environment, preserve_bindings, outcomes, PETTA_RELATION_SAFETY_STATIC,
        CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST, false, false);
    g_active_petta_search_machine = enclosing_active;
    g_he_shared_execution = false;
    return complete;
}
