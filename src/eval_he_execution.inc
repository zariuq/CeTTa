/* How collected values flow through bodies (he_raw_flow_walk).  A
 * collection's tuple and the set operations' results are returned as they
 * are, where every other value HE computes is evaluated; HE evaluates such a
 * value again wherever a binder instantiates it into syntax it evaluates. */
typedef struct {
    /* A collected value is read where HE evaluates it again. */
    bool raw_read;
    /* A call's value is read so: collected, if some head answers one. */
    bool call_read;
    /* Some body answers a collected value. */
    bool raw_tail;
    /* A set operation takes a literal HE would evaluate further. */
    bool raw_literal;
} HeRawFlowFlags;

/* Whether a collected value can reach a reader that evaluates it again. */
static bool he_raw_flow_rereads(const HeRawFlowFlags *flow) {
    return flow->raw_read || (flow->raw_tail && flow->call_read);
}

/* The structural eager adapter qualifies source before allocating a search
 * episode. Its equation and return policies are independent of PeTTa's source
 * translation. Effectful and typed applications retain their ordinary owner. */
typedef struct {
    Atom **terms;
    size_t length, capacity;
    /* The expressions already queued: what a term contributes is the same
     * wherever it occurs, so a subterm a value shares is judged once. */
    AtomVisitSet queued;
    bool queued_fault;
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
    /* Whether some equation body has a variable that neither its head nor a
     * binder within the body binds: a call can answer with it unbound. */
    bool fresh_variables;
    /* Whether some equation body reads, outside a match, a variable that the
     * match's pattern binds (he_match_bindings_escape), and whether a body
     * that introduces a variable also collects alternatives. */
    bool match_bindings_escape;
    bool collected_fresh;
    /* Whether some equation body lacks the shape under which the compiled
     * machine, raising HE's errors, keeps HE's order (he_body_raise_shaped). */
    bool raise_unshaped;
    /* How the bodies' collected values flow. */
    HeRawFlowFlags raw_flow;
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
    /* Some equation body has a variable no binder binds, and some reads a
     * match pattern's variable outside the match. */
    bool fresh_variables;
    bool match_bindings_escape;
    /* A body introduces a variable and collects alternatives. */
    bool collected_fresh;
    /* Some body lacks the shape he_body_raise_shaped describes. */
    bool raise_unshaped;
    HeRawFlowFlags raw_flow;
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
    bool open_root, bool stream, bool admitted,
    const HeExecutionAdmission *admission) {
    HeClosureDecision *slot = he_closure_decision_slot(head, open_root, stream);
    *slot = (HeClosureDecision){.head = head, .open_root = open_root,
        .stream = stream, .admitted = admitted,
        .effects = admitted && admission->effects,
        .queries = admitted && admission->queries,
        .structured_binder = admitted && admission->structured_binder,
        .fresh_variables = admitted && admission->fresh_variables,
        .match_bindings_escape = admitted && admission->match_bindings_escape,
        .collected_fresh = admitted && admission->collected_fresh,
        .raise_unshaped = admitted && admission->raise_unshaped,
        .raw_flow = admitted ? admission->raw_flow : (HeRawFlowFlags){0},
        .token = space_program_token(space)};
}

static bool he_stream_head_total(Space *space, SymbolId head,
                                 CettaExprLen arity);

static bool he_execution_intrinsic(SymbolId head) {
    return head == g_builtin_syms.let || head == g_builtin_syms.if_text ||
        head == g_builtin_syms.op_plus || head == g_builtin_syms.op_minus ||
        head == g_builtin_syms.op_mul || head == g_builtin_syms.op_div ||
        head == g_builtin_syms.op_mod || head == g_builtin_syms.op_eq ||
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
    return eval_is_reify_head(head) || head == g_builtin_syms.superpose ||
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
    if (term && term->kind == ATOM_EXPR && term->expr.len > 1u &&
        !admission->queued_fault &&
        !atom_visit_set_add(&admission->queued, term,
                                 &admission->queued_fault) &&
        !admission->queued_fault)
        return true;
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
                var_id_eq(parts[1]->var_id, parts[2]->var_id) &&
                he_execution_type_name(parts[3], "Bool");
        } else {
            bool arithmetic = head == g_builtin_syms.op_plus ||
                head == g_builtin_syms.op_minus ||
                head == g_builtin_syms.op_mul ||
                head == g_builtin_syms.op_div || head == g_builtin_syms.op_mod;
            admitted = he_execution_type_name(parts[1], "Number") &&
                he_execution_type_name(parts[2], "Number") &&
                he_execution_type_name(parts[3], arithmetic ? "Number" : "Bool");
        }
    }
    free(types);
    return admitted;
}

/* The standard let row: three distinct variables on the left, and the right
 * `(unify <atom> <pattern> <template> Empty)` using those same variables.
 * Names are irrelevant. One such row is the library fast path; any other
 * multiset, including a duplicate of this wiring, is not. */
static bool he_let_library_wiring(const Atom *lhs, const Atom *rhs) {
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 4u ||
        atom_head_symbol_id(lhs) != g_builtin_syms.let)
        return false;
    Atom *pattern = lhs->expr.elems[1];
    Atom *atom = lhs->expr.elems[2];
    Atom *template = lhs->expr.elems[3];
    if (!pattern || !atom || !template ||
        pattern->kind != ATOM_VAR || atom->kind != ATOM_VAR ||
        template->kind != ATOM_VAR ||
        var_id_eq(pattern->var_id, atom->var_id) ||
        var_id_eq(pattern->var_id, template->var_id) ||
        var_id_eq(atom->var_id, template->var_id))
        return false;
    if (!rhs || rhs->kind != ATOM_EXPR || rhs->expr.len != 5u ||
        atom_head_symbol_id(rhs) != g_builtin_syms.unify)
        return false;
    Atom *value = rhs->expr.elems[1];
    Atom *matched = rhs->expr.elems[2];
    Atom *body = rhs->expr.elems[3];
    return value && matched && body &&
        value->kind == ATOM_VAR && var_id_eq(value->var_id, atom->var_id) &&
        matched->kind == ATOM_VAR && var_id_eq(matched->var_id, pattern->var_id) &&
        body->kind == ATOM_VAR && var_id_eq(body->var_id, template->var_id) &&
        atom_is_symbol_id(rhs->expr.elems[4], g_builtin_syms.empty);
}

static bool he_let_proven_library_snapshot(Space *space) {
    SpaceEquationCursor cursor;
    if (!space || !space_equation_cursor_init(space, g_builtin_syms.let, &cursor))
        return false;
    unsigned count = 0u;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        SpaceEquationOccurrence occurrence;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            !space_rule_occurrence_resolve(id, eval_admitted_rule_forms(), &occurrence) ||
            !he_let_library_wiring(occurrence.lhs, occurrence.rhs))
            return false;
        if (++count > 1u)
            return false;
    }
    return count == 1u;
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
            /* HE's if rows are keyed by its Bool values only; PeTTa's
             * lowercase spellings are ordinary symbols in HE. */
            bool truth = lhs->expr.len >= 2u && is_true_atom(lhs->expr.elems[1]);
            if ((lhs->expr.len != 3u && lhs->expr.len != 4u) ||
                (!truth && !is_false_atom(lhs->expr.elems[1])) ||
                lhs->expr.elems[2]->kind != ATOM_VAR ||
                (lhs->expr.len == 4u &&
                 lhs->expr.elems[3]->kind != ATOM_VAR))
                return false;
            bit = lhs->expr.len == 3u ? (truth ? 1u : 2u) : (truth ? 4u : 8u);
            if (lhs->expr.len == 3u && !truth) {
                if (!atom_is_computation_zero_form(rhs))
                    return false;
            } else if (!rhs || rhs->kind != ATOM_VAR ||
                       !var_id_eq(rhs->var_id, lhs->expr.elems[truth ? 2u : 3u]->var_id)) {
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

static bool he_equation_introduces_variables(const Atom *lhs,
                                             const Atom *rhs);
static bool he_match_bindings_escape(const Atom *lhs, const Atom *rhs);
static bool he_term_collects(const Atom *term, uint32_t depth);
static void he_raw_flow_body(Space *space, const Atom *body, bool tail,
                             HeRawFlowFlags *out);

static void he_raw_flow_merge(HeRawFlowFlags *into,
                              const HeRawFlowFlags *from) {
    into->raw_read |= from->raw_read;
    into->call_read |= from->call_read;
    into->raw_tail |= from->raw_tail;
    into->raw_literal |= from->raw_literal;
}

/* Heads whose single answer is being decided (he_head_single_answer_in). */
typedef struct {
    SymbolId *heads;
    size_t len, cap;
} HeSingleAnswerScope;

static bool he_head_single_answer_in(Space *space, SymbolId head,
                                     HeSingleAnswerScope *scope,
                                     uint32_t depth);

/* Whether evaluating `term` gives at most one answer.  A superpose, a
 * match or a space's atoms may give several; a collection gives one tuple,
 * the atom an add-atom or a remove-atom stores is not evaluated, and a
 * value gives itself.  A binder, a branch and an intrinsic give one answer
 * from single parts, and a call of a head with equations does when the
 * head answers once (he_head_single_answer_in).  Past the depth bound it
 * answers no. */
static bool he_term_single_answer_in(Space *space, const Atom *term,
                                     HeSingleAnswerScope *scope,
                                     uint32_t depth) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u)
        return true;
    if (depth >= 256u)
        return false;
    const Atom *head = term->expr.elems[0];
    CettaExprLen len = term->expr.len;
    SymbolId id = head->kind == ATOM_SYMBOL ? atom_symbol_current_id(head)
                                            : SYMBOL_ID_NONE;
    if (eval_is_reify_head(id) || id == g_builtin_syms.quote ||
        id == g_builtin_syms.add_atom || id == g_builtin_syms.remove_atom)
        return true;
    if (id == g_builtin_syms.superpose || id == g_builtin_syms.match ||
        id == g_builtin_syms.get_atoms)
        return false;
    if (id == g_builtin_syms.let && len == 4u)
        return he_term_single_answer_in(space, term->expr.elems[2], scope,
                                        depth + 1u) &&
            he_term_single_answer_in(space, term->expr.elems[3], scope,
                                     depth + 1u);
    if (id == g_builtin_syms.let_star && len == 3u) {
        const Atom *bindings = term->expr.elems[1];
        if (bindings->kind != ATOM_EXPR)
            return false;
        for (CettaExprIndex index = 0u; index < bindings->expr.len; index++) {
            const Atom *binding = bindings->expr.elems[index];
            if (binding->kind != ATOM_EXPR || binding->expr.len != 2u ||
                !he_term_single_answer_in(space, binding->expr.elems[1],
                                          scope, depth + 1u))
                return false;
        }
        return he_term_single_answer_in(space, term->expr.elems[2], scope,
                                        depth + 1u);
    }
    if (id == g_builtin_syms.case_text && len == 3u) {
        const Atom *branches = term->expr.elems[2];
        if (branches->kind != ATOM_EXPR ||
            !he_term_single_answer_in(space, term->expr.elems[1], scope,
                                      depth + 1u))
            return false;
        for (CettaExprIndex index = 0u; index < branches->expr.len; index++) {
            const Atom *branch = branches->expr.elems[index];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u ||
                !he_term_single_answer_in(space, branch->expr.elems[1],
                                          scope, depth + 1u))
                return false;
        }
        return true;
    }
    if (id != SYMBOL_ID_NONE && !he_execution_intrinsic(id) &&
        eval_space_has_equations(space, id) &&
        !he_head_single_answer_in(space, id, scope, depth + 1u))
        return false;
    for (CettaExprIndex index = id != SYMBOL_ID_NONE ? 1u : 0u;
         index < len; index++) {
        if (!he_term_single_answer_in(space, term->expr.elems[index], scope,
                                      depth + 1u))
            return false;
    }
    return true;
}

/* Whether every call of `head` gives at most one answer: it has one
 * equation, whose head's arguments are distinct variables, so a call of its
 * arity takes that equation and any other call is its own value, and the
 * equation's body gives at most one answer.  A head whose answer is being
 * decided counts as single where it recurs, which holds by induction on the
 * evaluation; only the outermost decision is kept, once per program
 * revision, since an inner one may rest on an assumption that fails. */
typedef struct {
    SymbolId head;
    bool single;
    SpaceProgramToken token;
} HeSingleAnswerDecision;

#define HE_SINGLE_ANSWER_DECISIONS 256u

static bool he_head_single_answer_in(Space *space, SymbolId head,
                                     HeSingleAnswerScope *scope,
                                     uint32_t depth) {
    HeSingleAnswerDecision *decisions = he_admission_cache(
        &g_he_admission_caches.single_answer_decisions,
        sizeof(HeSingleAnswerDecision) * HE_SINGLE_ANSWER_DECISIONS);
    HeSingleAnswerDecision *slot = &decisions[
        (uint32_t)((uint64_t)head * 2654435761u) % HE_SINGLE_ANSWER_DECISIONS];
    if (slot->head == head && slot->token.space == space &&
        space_program_token_is_current(slot->token))
        return slot->single;
    for (size_t index = 0u; index < scope->len; index++)
        if (scope->heads[index] == head)
            return true;
    if (depth >= 256u)
        return false;
    SpaceEquationCursor cursor;
    if (!space_rule_cursor_init(space, head, eval_admitted_rule_forms(),
                                &cursor))
        return false;
    const Atom *body = NULL;
    bool single = true;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        SpaceEquationOccurrence occurrence;
        if (step != SPACE_EQUATION_CURSOR_ITEM || body ||
            !space_rule_occurrence_resolve(id, eval_admitted_rule_forms(),
                                           &occurrence) ||
            !occurrence.lhs || occurrence.lhs->kind != ATOM_EXPR) {
            single = false;
            break;
        }
        const Atom *lhs = occurrence.lhs;
        for (CettaExprIndex i = 1u; single && i < lhs->expr.len; i++) {
            single = lhs->expr.elems[i]->kind == ATOM_VAR;
            for (CettaExprIndex j = 1u; single && j < i; j++)
                single = !var_id_eq(lhs->expr.elems[j]->var_id, lhs->expr.elems[i]->var_id);
        }
        body = occurrence.rhs;
    }
    if (single && body) {
        if (scope->len == scope->cap) {
            scope->cap = scope->cap ? scope->cap * 2u : 8u;
            scope->heads = cetta_realloc(scope->heads,
                                         scope->cap * sizeof(*scope->heads));
        }
        scope->heads[scope->len++] = head;
        single = he_term_single_answer_in(space, body, scope, depth + 1u);
        scope->len--;
    } else {
        single = false;
    }
    if (scope->len == 0u)
        *slot = (HeSingleAnswerDecision){.head = head, .single = single,
            .token = space_program_token(space)};
    return single;
}

/* Whether evaluating `term` may give more than one answer
 * (he_term_single_answer_in, from no assumption).  The shared machine's
 * completion reads any call of a head with equations as one that may
 * (petta_machine_he_may_branch), which only completes more. */
static bool he_term_may_branch(Space *space, const Atom *term,
                               uint32_t depth) {
    HeSingleAnswerScope scope = {0};
    bool single = he_term_single_answer_in(space, term, &scope, depth);
    free(scope.heads);
    return !single;
}

/* Whether `term`, read where its value is consumed in place, keeps every
 * evaluation within it that may give more than one answer out of that
 * place: such an evaluation must not be there, and a collection within it
 * must itself be in shape. */
static bool he_answers_leave_in_tail(Space *space, const Atom *term,
                                     bool tail, uint32_t depth);

static bool he_value_in_place(Space *space, const Atom *term,
                              uint32_t depth) {
    return !he_term_may_branch(space, term, depth) &&
        he_answers_leave_in_tail(space, term, false, depth);
}

/* Whether each evaluation within `term` that may give more than one answer
 * runs where its answers leave the equation (`tail`) or where a collection
 * takes them.  HE completes every evaluation before its consumer reads it;
 * the compiled machine runs answers depth first.  The two orders differ
 * only where a consumer inside the equation runs between one producer's
 * answers: a let's value, a condition, a scrutinee, an argument, or a call
 * or a match whose value the body goes on to use.  Past the depth bound it
 * answers no. */
static bool he_answers_leave_in_tail(Space *space, const Atom *term,
                                     bool tail, uint32_t depth) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u)
        return true;
    if (depth >= 256u)
        return false;
    const Atom *head = term->expr.elems[0];
    SymbolId id = head->kind == ATOM_SYMBOL ? atom_symbol_current_id(head) : SYMBOL_ID_NONE;
    CettaExprLen len = term->expr.len;
    if (eval_is_reify_head(id) && len == 2u)
        return he_answers_leave_in_tail(space, term->expr.elems[1], true,
                                        depth + 1u);
    if (id == g_builtin_syms.quote || id == g_builtin_syms.add_atom ||
        id == g_builtin_syms.remove_atom)
        return true;
    if (id == g_builtin_syms.let && len == 4u)
        return he_value_in_place(space, term->expr.elems[2], depth + 1u) &&
            he_answers_leave_in_tail(space, term->expr.elems[3], tail,
                                     depth + 1u);
    if (id == g_builtin_syms.let_star && len == 3u) {
        const Atom *bindings = term->expr.elems[1];
        if (bindings->kind != ATOM_EXPR)
            return false;
        for (CettaExprIndex index = 0u; index < bindings->expr.len; index++) {
            const Atom *binding = bindings->expr.elems[index];
            if (binding->kind != ATOM_EXPR || binding->expr.len != 2u ||
                !he_value_in_place(space, binding->expr.elems[1], depth + 1u))
                return false;
        }
        return he_answers_leave_in_tail(space, term->expr.elems[2], tail,
                                        depth + 1u);
    }
    if (id == g_builtin_syms.if_text && (len == 3u || len == 4u))
        return he_value_in_place(space, term->expr.elems[1], depth + 1u) &&
            he_answers_leave_in_tail(space, term->expr.elems[2], tail,
                                     depth + 1u) &&
            (len == 3u ||
             he_answers_leave_in_tail(space, term->expr.elems[3], tail,
                                      depth + 1u));
    if (id == g_builtin_syms.case_text && len == 3u) {
        const Atom *branches = term->expr.elems[2];
        if (!he_value_in_place(space, term->expr.elems[1], depth + 1u) ||
            branches->kind != ATOM_EXPR)
            return false;
        for (CettaExprIndex index = 0u; index < branches->expr.len; index++) {
            const Atom *branch = branches->expr.elems[index];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u ||
                !he_answers_leave_in_tail(space, branch->expr.elems[1], tail,
                                          depth + 1u))
                return false;
        }
        return true;
    }
    /* A match's template instances answer for the match. */
    if (id == g_builtin_syms.match && len == 4u)
        return tail && he_answers_leave_in_tail(space, term->expr.elems[3],
                                                true, depth + 1u);
    if (id == g_builtin_syms.superpose)
        return false;
    if (id != SYMBOL_ID_NONE && !he_execution_intrinsic(id) && !tail &&
        eval_space_has_equations(space, id) &&
        he_term_may_branch(space, term, depth))
        return false;
    for (CettaExprIndex index = id != SYMBOL_ID_NONE ? 1u : 0u;
         index < len; index++) {
        if (!he_value_in_place(space, term->expr.elems[index], depth + 1u))
            return false;
    }
    return true;
}

/* Whether `term` has only forms whose values the compiled machine, raising
 * HE's errors, gives as HE does: no case, which takes an error as a value,
 * and no superpose, whose alternatives HE runs in an order of its own.  A
 * collection's tuple, errors included, is as HE gives it: a run that
 * raises reads no collected value where HE would evaluate it again
 * (he_raw_flow_rereads).  Past the depth bound it answers no. */
static bool he_raise_forms_admitted(const Atom *term, uint32_t depth) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u)
        return true;
    if (depth >= 256u)
        return false;
    SymbolId id = atom_head_symbol_id(term);
    if (id == g_builtin_syms.quote)
        return true;
    if (id == g_builtin_syms.case_text || id == g_builtin_syms.superpose)
        return false;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++)
        if (!he_raise_forms_admitted(term->expr.elems[index], depth + 1u))
            return false;
    return true;
}

/* Whether an equation body keeps HE's order on the compiled machine where
 * errors raise to the innermost collection (raises_he_errors): its answers
 * leave in tail (he_answers_leave_in_tail), and its forms are those
 * he_raise_forms_admitted takes. */
static bool he_body_raise_shaped(Space *space, const Atom *body) {
    return he_answers_leave_in_tail(space, body, true, 0u) &&
        he_raise_forms_admitted(body, 0u);
}

/* HE's library collection functions: `unique` collects its argument and
 * keeps each value's first occurrence, and `union`, `intersection` and
 * `subtraction` collect both arguments and combine them.  Each takes its
 * arguments unevaluated (Atom parameters) and returns the combined values
 * as alternatives.  Where `term` calls one whose only equation in the space
 * is the library's own,
 *   (= (f $a) (let $c (reify $a) (let $u (f-atom $c) (superpose $u))))
 * or its two-argument form, the aggregate is f-atom's head; otherwise none.
 * A program's own definition of the name keeps its meaning. */
static SymbolId he_library_stream_aggregate(Space *space, const Atom *term) {
    if (!space || !term || term->kind != ATOM_EXPR || term->expr.len < 2u ||
        term->expr.elems[0]->kind != ATOM_SYMBOL)
        return SYMBOL_ID_NONE;
    SymbolId head = atom_symbol_current_id(term->expr.elems[0]);
    uint32_t arity = (uint32_t)term->expr.len - 1u;
    SymbolId aggregate = SYMBOL_ID_NONE;
    switch (petta_semantics_form(head)) {
    case PETTA_FORM_STREAM_UNIQUE:
        aggregate = arity == 1u ? g_builtin_syms.unique_atom : SYMBOL_ID_NONE;
        break;
    case PETTA_FORM_STREAM_UNION:
        aggregate = arity == 2u ? g_builtin_syms.union_atom : SYMBOL_ID_NONE;
        break;
    case PETTA_FORM_STREAM_INTERSECTION:
        aggregate = arity == 2u ? g_builtin_syms.intersection_atom
                                : SYMBOL_ID_NONE;
        break;
    case PETTA_FORM_STREAM_SUBTRACTION:
        aggregate = arity == 2u ? g_builtin_syms.subtraction_atom
                                : SYMBOL_ID_NONE;
        break;
    default:
        break;
    }
    if (aggregate == SYMBOL_ID_NONE)
        return SYMBOL_ID_NONE;
    SpaceEquationCursor cursor;
    if (!space_rule_cursor_init(space, head, eval_admitted_rule_forms(),
                                &cursor))
        return SYMBOL_ID_NONE;
    SpaceEquationOccurrence occurrence = {0};
    uint32_t count = 0u;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        if (step != SPACE_EQUATION_CURSOR_ITEM || ++count > 1u ||
            !space_rule_occurrence_resolve(id, eval_admitted_rule_forms(),
                                           &occurrence))
            return SYMBOL_ID_NONE;
    }
    const Atom *lhs = occurrence.lhs;
    const Atom *rhs = occurrence.rhs;
    if (count != 1u || !lhs || lhs->kind != ATOM_EXPR ||
        lhs->expr.len != arity + 1u)
        return SYMBOL_ID_NONE;
    const Atom *params[2] = {NULL, NULL};
    for (uint32_t index = 0u; index < arity; index++) {
        params[index] = lhs->expr.elems[index + 1u];
        if (params[index]->kind != ATOM_VAR)
            return SYMBOL_ID_NONE;
    }
    if (arity == 2u && var_id_eq(params[0]->var_id, params[1]->var_id))
        return SYMBOL_ID_NONE;
    /* The collections bind in order, then the aggregate, then the emit. */
    const Atom *collected[2] = {NULL, NULL};
    const Atom *body = rhs;
    for (uint32_t index = 0u; index < arity; index++) {
        if (!body || body->kind != ATOM_EXPR || body->expr.len != 4u ||
            !atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.let) ||
            body->expr.elems[1]->kind != ATOM_VAR)
            return SYMBOL_ID_NONE;
        const Atom *value = body->expr.elems[2];
        if (value->kind != ATOM_EXPR || value->expr.len != 2u ||
            !eval_is_reify_head(atom_head_symbol_id(value)) ||
            value->expr.elems[1]->kind != ATOM_VAR ||
            !var_id_eq(value->expr.elems[1]->var_id, params[index]->var_id))
            return SYMBOL_ID_NONE;
        collected[index] = body->expr.elems[1];
        body = body->expr.elems[3];
    }
    if (!body || body->kind != ATOM_EXPR || body->expr.len != 4u ||
        !atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.let) ||
        body->expr.elems[1]->kind != ATOM_VAR)
        return SYMBOL_ID_NONE;
    const Atom *combined = body->expr.elems[2];
    const Atom *result = body->expr.elems[1];
    const Atom *emit = body->expr.elems[3];
    if (combined->kind != ATOM_EXPR || combined->expr.len != arity + 1u ||
        !atom_is_symbol_id(combined->expr.elems[0], aggregate))
        return SYMBOL_ID_NONE;
    for (uint32_t index = 0u; index < arity; index++)
        if (combined->expr.elems[index + 1u]->kind != ATOM_VAR ||
            !var_id_eq(combined->expr.elems[index + 1u]->var_id, collected[index]->var_id))
            return SYMBOL_ID_NONE;
    if (emit->kind != ATOM_EXPR || emit->expr.len != 2u ||
        !atom_is_symbol_id(emit->expr.elems[0], g_builtin_syms.superpose) ||
        emit->expr.elems[1]->kind != ATOM_VAR ||
        !var_id_eq(emit->expr.elems[1]->var_id, result->var_id))
        return SYMBOL_ID_NONE;
    return aggregate;
}

/* Current machine coverage, not a restriction on HE. A head rejected here
 * retains the canonical interpreter. Keep the root precheck and the full
 * admission walk on the same predicate, including the library exception. */
static bool he_execution_head_requires_canonical(Space *space, SymbolId head,
    bool intrinsic, bool collection, bool library_stream) {
    return !intrinsic && head != SYMBOL_ID_NONE && !collection &&
        !library_stream && (is_grounded_op(head) || symbol_id_is_builtin_for(head, eval_current_language_id()) ||
                           space_head_declares_type(space, head));
}

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
        if (!term || he_execution_term_holds_resource(term) ||
            he_not_reducible(term))
            return false;
        /* NotReducible is HE's call-return marker: a body that is
         * NotReducible leaves its own call unreduced, at that call's
         * boundary, which the canonical owner keeps. */
        if (atom_is_symbol_id(term, g_builtin_syms.not_reducible))
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
        /* Extended semantic handlers keep their owned coefficient,
         * demand and return continuations on the native evaluator.
         * Check callable symbol values through the same source view. */
        Atom head_call = {.kind = ATOM_EXPR,
            .expr = {.elems = &term, .len = 1u}};
        if (prepared_pure_requires_native_handler(admission->space,
                expression ? term : &head_call))
            return false;
        SymbolId head = expression ? atom_head_symbol_id(term) : atom_symbol_current_id(term);
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
        /* A call of a library collection function is its definition, which
         * the machine runs with the arguments in place: its declared type
         * passes them unevaluated, as the definition reads them. */
        bool library_stream = expression && !admission->stream &&
            he_library_stream_aggregate(admission->space, term) !=
                SYMBOL_ID_NONE;
        if (!intrinsic && head != SYMBOL_ID_NONE && !collection &&
            !library_stream && petta_semantics_form(head) != PETTA_FORM_NONE)
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
            /* A literal condition is admitted only as an HE Bool value: any
             * other symbol, PeTTa's lowercase false included, leaves the if
             * call unreduced, which the canonical evaluator answers. */
            if ((term->expr.len != 3u && term->expr.len != 4u) ||
                (!is_true_atom(term->expr.elems[1]) &&
                 !is_false_atom(term->expr.elems[1]) &&
                 (!he_execution_condition_head(
                      atom_head_symbol_id(term->expr.elems[1])) ||
                  expr_nargs(term->expr.elems[1]) != 2u)))
                return false;
        }
        bool let_library = head == g_builtin_syms.let &&
            he_let_proven_library_snapshot(admission->space);
        /* He-compat specializes let only for that one library row. No row,
         * a different row, or a duplicate falls through to the equations. */
        if (intrinsic && head == g_builtin_syms.let &&
            active_profile_uses_rust_he_compat_semantics() && !let_library)
            return false;
        if (intrinsic && eval_space_has_equations(admission->space, head) &&
            !let_library &&
            ((head != g_builtin_syms.if_text && head != g_builtin_syms.empty_form) ||
             !he_execution_intrinsic_equations(admission->space, head)))
            return false;
        if (he_execution_head_requires_canonical(admission->space, head,
                intrinsic, collection, library_stream))
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
                admission->fresh_variables |= decided->fresh_variables;
                admission->match_bindings_escape |=
                    decided->match_bindings_escape;
                admission->collected_fresh |= decided->collected_fresh;
                admission->raise_unshaped |= decided->raise_unshaped;
                he_raw_flow_merge(&admission->raw_flow, &decided->raw_flow);
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
                    bool introduces = he_equation_introduces_variables(
                        occurrence.lhs, occurrence.rhs);
                    admission->fresh_variables |= introduces;
                    admission->collected_fresh |= introduces &&
                        he_term_collects(occurrence.rhs, 0u);
                    if (he_match_bindings_escape(occurrence.lhs,
                                                 occurrence.rhs))
                        admission->match_bindings_escape = true;
                    if (!admission->raise_unshaped &&
                        !he_body_raise_shaped(admission->space,
                                              occurrence.rhs))
                        admission->raise_unshaped = true;
                    he_raw_flow_body(admission->space, occurrence.rhs, true,
                                     &admission->raw_flow);
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
                general = !var_id_eq(lhs->expr.elems[j]->var_id, argument->var_id);
        }
        if (general)
            return true;
    }
}

/* Whether HE evaluates `symbol` further: it has equations, or it is a
 * grounded operation or a builtin.  Decided once per symbol and program
 * revision, since rows and collected values ask it of every symbol. */
typedef struct {
    SymbolId symbol;
    bool callable;
    SpaceProgramToken token;
} HeSymbolCallability;

#define HE_SYMBOL_CALLABILITIES 1024u

static bool he_symbol_callable(Space *space, SymbolId symbol) {
    HeSymbolCallability *entries = he_admission_cache(
        &g_he_admission_caches.symbol_callabilities,
        sizeof(HeSymbolCallability) * HE_SYMBOL_CALLABILITIES);
    HeSymbolCallability *slot = &entries[
        (uint32_t)((uint64_t)symbol * 2654435761u) % HE_SYMBOL_CALLABILITIES];
    if (slot->symbol == symbol && slot->token.space == space &&
        space_program_token_is_current(slot->token))
        return slot->callable;
    bool callable = eval_space_has_equations(space, symbol) ||
        is_grounded_op(symbol) || symbol_id_is_builtin_for(symbol, eval_current_language_id());
    *slot = (HeSymbolCallability){.symbol = symbol, .callable = callable,
        .token = space_program_token(space)};
    return callable;
}

/* A value HE would not evaluate further: no variable, and no symbol or
 * expression head with equations, a grounded operation or a builtin. */
/* A subexpression met again was inert where it was first met: the walk
 * stops at the first part that is not. */
static bool he_stream_value_inert_in(Space *space, const Atom *value,
                                     AtomVisitSet *seen, bool *fault) {
    if (!value)
        return false;
    switch (value->kind) {
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        return true;
    case ATOM_SYMBOL:
        return !he_symbol_callable(space, atom_symbol_current_id(value));
    case ATOM_EXPR:
        if (value->expr.len > 1u && !*fault &&
            !atom_visit_set_add(seen, value, fault) && !*fault)
            return true;
        for (CettaExprIndex i = 0u; i < value->expr.len; i++)
            if (!he_stream_value_inert_in(space, value->expr.elems[i],
                                          seen, fault))
                return false;
        return true;
    }
    return false;
}

static bool he_stream_value_inert(Space *space, const Atom *value) {
    AtomVisitSet seen = {0};
    bool fault = false;
    bool inert = he_stream_value_inert_in(space, value, &seen, &fault);
    atom_visit_set_free(&seen);
    return inert;
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
    if (!he_stream_head_admitted(space, arena, atom_symbol_current_id(call->expr.elems[0]),
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
                                       true, true, true, &admission);
    } else {
        he_closure_decision_record(space, head, true, true, false,
                                   &admission);
    }
    free(admission.terms);
    free(admission.heads);
    atom_visit_set_free(&admission.queued);
    return admitted;
}

/* Whether closed calls of `head` may run on the compiled open-equation
 * machine.  That machine takes HE's rules as its policy: an equation's head
 * is a pattern, and a call no head takes is its own value.  It runs answers
 * depth first, where HE completes a producer, effects included, before its
 * consumer reads it.  In a run without effects the two agree on answers,
 * and an error withdraws the cursor so that the run is evaluated again.  A
 * run with effects cannot be (`raises`): its errors raise to the innermost
 * collection, which keeps HE's order for closures of the shape
 * he_body_raise_shaped describes.  Its cells carry no authored name, where
 * HE answers with a fresh variable under its own, so a closure whose bodies
 * introduce variables stays with the shared machine.  So the relation's
 * closure needs the shared machine's own admission of a closed call, no
 * introduced variable and, where it raises, that shape, decided once per
 * program revision. */
static bool he_compiled_admitted(Space *space, Arena *arena, SymbolId head,
                                 bool raises) {
    if (!space || head == SYMBOL_ID_NONE || space_has_dependencies(space) ||
        !eval_space_has_equations(space, head))
        return false;
    const HeClosureDecision *decided =
        he_closure_decision_find(space, head, false, false);
    if (decided)
        return decided->admitted && !(raises && decided->raise_unshaped) &&
            !decided->fresh_variables && !decided->match_bindings_escape &&
            !decided->collected_fresh &&
            !(decided->queries && decided->structured_binder);
    HeExecutionAdmission admission = {.space = space, .arena = arena,
        .open_root = false, .stream = false};
    bool admitted = he_execution_source_admitted(
        &admission, atom_symbol_id(arena, head));
    if (admitted) {
        for (size_t index = 0u; index < admission.head_count; index++)
            he_closure_decision_record(space, admission.heads[index],
                                       false, false, true, &admission);
    } else {
        he_closure_decision_record(space, head, false, false, false,
                                   &admission);
    }
    free(admission.terms);
    free(admission.heads);
    atom_visit_set_free(&admission.queued);
    return admitted && !(raises && admission.raise_unshaped) &&
        !admission.fresh_variables &&
        !admission.match_bindings_escape && !admission.collected_fresh &&
        !(admission.queries && admission.structured_binder);
}

/* A compiled call takes its arguments as values, as the streamed one does. */
static bool he_compiled_call_admitted(Space *space, Arena *arena, Atom *call,
                                      bool raises) {
    if (!call || call->kind != ATOM_EXPR || !call->expr.len ||
        call->expr.elems[0]->kind != ATOM_SYMBOL ||
        !he_compiled_admitted(space, arena, atom_symbol_current_id(call->expr.elems[0]),
                              raises))
        return false;
    for (CettaExprIndex i = 1u; i < call->expr.len; i++)
        if (!he_stream_value_inert(space, call->expr.elems[i]))
            return false;
    return true;
}

typedef struct {
    VarId *items;
    size_t len, cap;
} HeBoundVariables;

static bool he_bound_contains(const HeBoundVariables *bound, VarId id) {
    for (size_t index = bound->len; index > 0u; index--)
        if (var_id_eq(bound->items[index - 1u], id))
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

/* The set operations HE applies to their tuples as given, returning the
 * result as it is: neither is evaluated again where it is passed. */
static bool he_raw_set_operation(SymbolId id) {
    return id == g_builtin_syms.unique_atom || id == g_builtin_syms.union_atom ||
        id == g_builtin_syms.subtraction_atom ||
        id == g_builtin_syms.intersection_atom;
}

typedef struct {
    Space *space;
    /* Variables bound to a collected value, and to a call's value. */
    HeBoundVariables raw;
    HeBoundVariables called;
    HeRawFlowFlags flags;
    /* The ground subterms walked, with their depth and tail position: a
     * ground subterm reads no binder, so what walking it adds to `flags` is
     * the same wherever it occurs at that depth and position. */
    CettaNodeMemo walked;
} HeRawFlow;

enum { HE_RAW_VALUE = 1, HE_CALL_VALUE = 2 };

/* What `term`'s value may be: collected (a collection, a set operation, or a
 * variable bound to one), a call's (a call of a head with equations), or
 * either through a binder's body, a branch, a template or an alternative. */
static unsigned he_raw_flow_source(HeRawFlow *flow, const Atom *term,
                                   uint32_t depth) {
    if (!term)
        return 0u;
    if (term->kind == ATOM_VAR)
        return (he_bound_contains(&flow->raw, term->var_id)
                    ? HE_RAW_VALUE : 0u) |
            (he_bound_contains(&flow->called, term->var_id)
                 ? HE_CALL_VALUE : 0u);
    if (term->kind != ATOM_EXPR || term->expr.len == 0u)
        return 0u;
    if (depth >= 256u)
        return HE_RAW_VALUE | HE_CALL_VALUE;
    SymbolId id = atom_head_symbol_id(term);
    CettaExprLen len = term->expr.len;
    if (eval_is_reify_head(id) || he_raw_set_operation(id))
        return HE_RAW_VALUE;
    if (id == g_builtin_syms.quote || id == g_builtin_syms.add_atom ||
        id == g_builtin_syms.remove_atom)
        return 0u;
    if (id == g_builtin_syms.let && len == 4u)
        return he_raw_flow_source(flow, term->expr.elems[3], depth + 1u) |
            he_raw_flow_source(flow, term->expr.elems[2], depth + 1u);
    if (id == g_builtin_syms.let_star && len == 3u &&
        term->expr.elems[1]->kind == ATOM_EXPR) {
        unsigned source = he_raw_flow_source(flow, term->expr.elems[2],
                                             depth + 1u);
        const Atom *bindings = term->expr.elems[1];
        for (CettaExprIndex index = 0u; index < bindings->expr.len; index++) {
            const Atom *binding = bindings->expr.elems[index];
            if (binding->kind != ATOM_EXPR || binding->expr.len != 2u)
                return HE_RAW_VALUE | HE_CALL_VALUE;
            source |= he_raw_flow_source(flow, binding->expr.elems[1],
                                         depth + 1u);
        }
        return source;
    }
    if (id == g_builtin_syms.if_text && (len == 3u || len == 4u))
        return he_raw_flow_source(flow, term->expr.elems[2], depth + 1u) |
            (len == 4u
                 ? he_raw_flow_source(flow, term->expr.elems[3], depth + 1u)
                 : 0u);
    if (id == g_builtin_syms.match && len == 4u)
        return he_raw_flow_source(flow, term->expr.elems[3], depth + 1u);
    /* A case's value is a branch's, which may be its scrutinee's through a
     * pattern variable.  A superpose's alternatives are its items, each
     * evaluated; the elements of a tuple it is given are evaluated values. */
    if (id == g_builtin_syms.case_text && len == 3u &&
        term->expr.elems[2]->kind == ATOM_EXPR) {
        unsigned source = he_raw_flow_source(flow, term->expr.elems[1],
                                             depth + 1u);
        const Atom *branches = term->expr.elems[2];
        for (CettaExprIndex index = 0u; index < branches->expr.len; index++) {
            const Atom *branch = branches->expr.elems[index];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u)
                return HE_RAW_VALUE | HE_CALL_VALUE;
            source |= he_raw_flow_source(flow, branch->expr.elems[1],
                                         depth + 1u);
        }
        return source;
    }
    if (id == g_builtin_syms.superpose && len == 2u) {
        const Atom *items = term->expr.elems[1];
        if (items->kind != ATOM_EXPR)
            return 0u;
        unsigned source = 0u;
        for (CettaExprIndex index = 0u; index < items->expr.len; index++)
            source |= he_raw_flow_source(flow, items->expr.elems[index],
                                         depth + 1u);
        return source;
    }
    if (id != SYMBOL_ID_NONE && !he_execution_intrinsic(id) &&
        eval_space_has_equations(flow->space, id))
        return HE_CALL_VALUE;
    return 0u;
}

static void he_raw_flow_bind(HeRawFlow *flow, const Atom *pattern,
                             unsigned source) {
    if (source & HE_RAW_VALUE)
        he_bound_add_pattern(&flow->raw, pattern);
    if (source & HE_CALL_VALUE)
        he_bound_add_pattern(&flow->called, pattern);
}

/* Walks `term`, evaluated by HE where it stands, with the binders in scope.
 * A variable bound to a collected value is read again wherever it is
 * instantiated into syntax HE evaluates: a body, a branch, a template, an
 * argument, a condition.  It is not where a set operation takes it, where
 * an add-atom stores it, or under a quote.  A collected value passed to a
 * call of a head with equations is read where the callee's body reads its
 * parameter.  `tail` marks the equation's answer. */
static void he_raw_flow_walk(HeRawFlow *flow, const Atom *term, bool tail,
                             uint32_t depth) {
    if (!term)
        return;
    if (term->kind == ATOM_VAR) {
        if (he_bound_contains(&flow->raw, term->var_id))
            flow->flags.raw_read = true;
        if (he_bound_contains(&flow->called, term->var_id))
            flow->flags.call_read = true;
        return;
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0u)
        return;
    if (depth >= 256u) {
        flow->flags.raw_read = flow->flags.raw_literal = true;
        return;
    }
    if (!atom_has_vars(term) && term->expr.len > 1u) {
        uint32_t position = depth * 2u + (tail ? 1u : 0u);
        uintptr_t walked = 0u;
        if (cetta_node_memo_find(&flow->walked, term, position, &walked))
            return;
        cetta_node_memo_store(&flow->walked, term, position, 1u);
    }
    SymbolId id = atom_head_symbol_id(term);
    CettaExprLen len = term->expr.len;
    if (id == g_builtin_syms.quote)
        return;
    if ((id == g_builtin_syms.add_atom || id == g_builtin_syms.remove_atom) &&
        len == 3u) {
        he_raw_flow_walk(flow, term->expr.elems[1], false, depth + 1u);
        return;
    }
    if (eval_is_reify_head(id) && len == 2u) {
        flow->flags.raw_tail |= tail;
        he_raw_flow_walk(flow, term->expr.elems[1], false, depth + 1u);
        return;
    }
    if (he_raw_set_operation(id)) {
        flow->flags.raw_tail |= tail;
        for (CettaExprIndex index = 1u; index < len; index++) {
            const Atom *argument = term->expr.elems[index];
            if (argument->kind != ATOM_VAR &&
                !he_stream_value_inert(flow->space, argument))
                flow->flags.raw_literal = true;
        }
        return;
    }
    if (id == g_builtin_syms.let && len == 4u) {
        he_raw_flow_walk(flow, term->expr.elems[2], false, depth + 1u);
        unsigned source = he_raw_flow_source(flow, term->expr.elems[2],
                                             depth + 1u);
        size_t raw = flow->raw.len, called = flow->called.len;
        he_raw_flow_bind(flow, term->expr.elems[1], source);
        he_raw_flow_walk(flow, term->expr.elems[3], tail, depth + 1u);
        flow->raw.len = raw;
        flow->called.len = called;
        return;
    }
    if (id == g_builtin_syms.let_star && len == 3u &&
        term->expr.elems[1]->kind == ATOM_EXPR) {
        const Atom *bindings = term->expr.elems[1];
        size_t raw = flow->raw.len, called = flow->called.len;
        for (CettaExprIndex index = 0u; index < bindings->expr.len; index++) {
            const Atom *binding = bindings->expr.elems[index];
            if (binding->kind != ATOM_EXPR || binding->expr.len != 2u) {
                flow->flags.raw_read = true;
                continue;
            }
            he_raw_flow_walk(flow, binding->expr.elems[1], false, depth + 1u);
            he_raw_flow_bind(flow, binding->expr.elems[0],
                             he_raw_flow_source(flow, binding->expr.elems[1],
                                                depth + 1u));
        }
        he_raw_flow_walk(flow, term->expr.elems[2], tail, depth + 1u);
        flow->raw.len = raw;
        flow->called.len = called;
        return;
    }
    if (id == g_builtin_syms.case_text && len == 3u &&
        term->expr.elems[2]->kind == ATOM_EXPR) {
        he_raw_flow_walk(flow, term->expr.elems[1], false, depth + 1u);
        unsigned source = he_raw_flow_source(flow, term->expr.elems[1],
                                             depth + 1u);
        const Atom *branches = term->expr.elems[2];
        for (CettaExprIndex index = 0u; index < branches->expr.len; index++) {
            const Atom *branch = branches->expr.elems[index];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u)
                continue;
            size_t raw = flow->raw.len, called = flow->called.len;
            he_raw_flow_bind(flow, branch->expr.elems[0], source);
            he_raw_flow_walk(flow, branch->expr.elems[1], tail, depth + 1u);
            flow->raw.len = raw;
            flow->called.len = called;
        }
        return;
    }
    if (id == g_builtin_syms.match && len == 4u) {
        he_raw_flow_walk(flow, term->expr.elems[1], false, depth + 1u);
        he_raw_flow_walk(flow, term->expr.elems[3], tail, depth + 1u);
        return;
    }
    if (id == g_builtin_syms.if_text && (len == 3u || len == 4u)) {
        he_raw_flow_walk(flow, term->expr.elems[1], false, depth + 1u);
        for (CettaExprIndex index = 2u; index < len; index++)
            he_raw_flow_walk(flow, term->expr.elems[index], tail, depth + 1u);
        return;
    }
    if (id == g_builtin_syms.superpose && len == 2u &&
        term->expr.elems[1]->kind == ATOM_EXPR) {
        const Atom *items = term->expr.elems[1];
        for (CettaExprIndex index = 0u; index < items->expr.len; index++)
            he_raw_flow_walk(flow, items->expr.elems[index], tail, depth + 1u);
        return;
    }
    /* A call of a head with equations reads its arguments again in its
     * body; any other expression evaluates them where they stand. */
    bool callee = id != SYMBOL_ID_NONE && !he_execution_intrinsic(id) &&
        eval_space_has_equations(flow->space, id);
    for (CettaExprIndex index = 0u; index < len; index++) {
        const Atom *argument = term->expr.elems[index];
        if (callee && index > 0u) {
            unsigned source = he_raw_flow_source(flow, argument, depth + 1u);
            flow->flags.raw_read |= (source & HE_RAW_VALUE) != 0u;
            flow->flags.call_read |= (source & HE_CALL_VALUE) != 0u;
        }
        he_raw_flow_walk(flow, argument, false, depth + 1u);
    }
}

/* The collected-value flow of a body; `tail` when its value is an
 * equation's answer. */
static void he_raw_flow_body(Space *space, const Atom *body, bool tail,
                             HeRawFlowFlags *out) {
    HeRawFlow flow = {.space = space};
    he_raw_flow_walk(&flow, body, tail, 0u);
    he_raw_flow_merge(out, &flow.flags);
    free(flow.raw.items);
    free(flow.called.items);
    cetta_node_memo_free(&flow.walked);
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

/* The occurrences of the variable `id` in `term`; past the depth bound,
 * SIZE_MAX, which reads as an escape and only keeps the closure canonical. */
static size_t he_variable_occurrences(const Atom *term, VarId id,
                                      uint32_t depth) {
    if (!term || !atom_has_vars(term))
        return 0u;
    if (depth >= 256u)
        return SIZE_MAX;
    if (term->kind == ATOM_VAR)
        return var_id_eq(term->var_id, id);
    size_t count = 0u;
    if (term->kind == ATOM_EXPR)
        for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
            size_t part = he_variable_occurrences(term->expr.elems[index], id,
                                                  depth + 1u);
            if (part == SIZE_MAX)
                return SIZE_MAX;
            count += part;
        }
    return count;
}

/* Whether a variable of `pattern` outside `bound` occurs in `rhs` more often
 * than in `match`. */
static bool he_pattern_variable_outside(const HeBoundVariables *bound,
                                        const Atom *pattern, const Atom *match,
                                        const Atom *rhs, uint32_t depth) {
    if (!pattern || !atom_has_vars(pattern))
        return false;
    if (depth >= 256u)
        return true;
    if (pattern->kind == ATOM_VAR) {
        if (he_bound_contains(bound, pattern->var_id))
            return false;
        size_t in_body = he_variable_occurrences(rhs, pattern->var_id, 0u);
        size_t in_match = he_variable_occurrences(match, pattern->var_id, 0u);
        return in_body == SIZE_MAX || in_match == SIZE_MAX ||
            in_body > in_match;
    }
    if (pattern->kind == ATOM_EXPR)
        for (CettaExprIndex index = 0u; index < pattern->expr.len; index++)
            if (he_pattern_variable_outside(bound, pattern->expr.elems[index],
                                            match, rhs, depth + 1u))
                return true;
    return false;
}

/* Walks `term` with the binders in scope, as he_free_variable_under does: a
 * match's pattern variables that no binder in scope binds yet are the
 * match's own, and one of them occurring in `rhs` past the match escapes. */
static bool he_match_bindings_escape_under(HeBoundVariables *bound,
                                           const Atom *term, const Atom *rhs,
                                           uint32_t depth) {
    if (!term || term->kind != ATOM_EXPR || !atom_has_vars(term))
        return false;
    if (depth >= 256u)
        return true;
    SymbolId head = atom_head_symbol_id(term);
    size_t mark = bound->len;
    bool escape = false;
    if (head == g_builtin_syms.match && term->expr.len == 4u) {
        escape = he_match_bindings_escape_under(bound, term->expr.elems[1],
                                                rhs, depth + 1u) ||
            he_pattern_variable_outside(bound, term->expr.elems[2], term, rhs,
                                        0u);
        he_bound_add_pattern(bound, term->expr.elems[2]);
        escape = escape || he_match_bindings_escape_under(
            bound, term->expr.elems[3], rhs, depth + 1u);
    } else if (head == g_builtin_syms.let && term->expr.len == 4u) {
        escape = he_match_bindings_escape_under(bound, term->expr.elems[2],
                                                rhs, depth + 1u);
        he_bound_add_pattern(bound, term->expr.elems[1]);
        escape = escape || he_match_bindings_escape_under(
            bound, term->expr.elems[3], rhs, depth + 1u);
    } else if (head == g_builtin_syms.let_star && term->expr.len == 3u &&
               term->expr.elems[1]->kind == ATOM_EXPR) {
        const Atom *pairs = term->expr.elems[1];
        for (CettaExprIndex index = 0u; !escape && index < pairs->expr.len;
             index++) {
            const Atom *pair = pairs->expr.elems[index];
            if (pair->kind != ATOM_EXPR || pair->expr.len != 2u) {
                escape = true;
                break;
            }
            escape = he_match_bindings_escape_under(bound, pair->expr.elems[1],
                                                    rhs, depth + 1u);
            he_bound_add_pattern(bound, pair->expr.elems[0]);
        }
        escape = escape || he_match_bindings_escape_under(
            bound, term->expr.elems[2], rhs, depth + 1u);
    } else if (head == g_builtin_syms.case_text && term->expr.len == 3u &&
               term->expr.elems[2]->kind == ATOM_EXPR) {
        escape = he_match_bindings_escape_under(bound, term->expr.elems[1],
                                                rhs, depth + 1u);
        const Atom *branches = term->expr.elems[2];
        for (CettaExprIndex index = 0u; !escape && index < branches->expr.len;
             index++) {
            const Atom *branch = branches->expr.elems[index];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u) {
                escape = true;
                break;
            }
            size_t branch_mark = bound->len;
            he_bound_add_pattern(bound, branch->expr.elems[0]);
            escape = he_match_bindings_escape_under(bound, branch->expr.elems[1],
                                                    rhs, depth + 1u);
            bound->len = branch_mark;
        }
    } else {
        for (CettaExprIndex index = 0u; !escape && index < term->expr.len;
             index++)
            escape = he_match_bindings_escape_under(bound, term->expr.elems[index],
                                                    rhs, depth + 1u);
    }
    bound->len = mark;
    return escape;
}

/* Whether the body of `(= lhs rhs)` reads, outside a match, a variable the
 * match's pattern binds first (no binder in scope binding it yet).  HE's
 * match answers carry their row bindings to that reader as raw row values,
 * which HE evaluates again where it instantiates them, while the shared
 * machine keeps a variable's value as it stands.  Past the depth bound the
 * walks answer yes, which only keeps the closure canonical. */
static bool he_match_bindings_escape(const Atom *lhs, const Atom *rhs) {
    HeBoundVariables bound = {0};
    he_bound_add_pattern(&bound, lhs);
    bool escape = he_match_bindings_escape_under(&bound, rhs, rhs, 0u);
    free(bound.items);
    return escape;
}

/* Whether the body of `(= lhs rhs)` has a variable that neither the head
 * nor a binder in whose scope it stands binds: a call can answer with it
 * unbound.  Scopes are he_free_variable_under's, so a binder in one branch
 * binds only within that branch. */
static bool he_equation_introduces_variables(const Atom *lhs,
                                             const Atom *rhs) {
    HeBoundVariables bound = {0};
    he_bound_add_pattern(&bound, lhs);
    bool introduced = he_free_variable_under(&bound, rhs);
    free(bound.items);
    return introduced;
}

/* Whether evaluating `term` collects alternatives (a collapse).  Past the
 * depth bound it answers yes, which only keeps the closure canonical. */
/* The answer at a subterm depends on it and its depth, which keep the
 * bound exact: a subterm a value shares is asked once per depth. */
static bool he_term_collects_in(const Atom *term, uint32_t depth,
                                CettaNodeMemo *memo) {
    if (!term || term->kind != ATOM_EXPR)
        return false;
    if (depth >= 256u ||
        eval_is_reify_head(atom_head_symbol_id(term)))
        return true;
    uintptr_t known = 0u;
    if (cetta_node_memo_find(memo, term, depth, &known))
        return known != 0u;
    bool collects = false;
    for (CettaExprIndex index = 0u; !collects && index < term->expr.len;
         index++)
        collects = he_term_collects_in(term->expr.elems[index], depth + 1u,
                                       memo);
    cetta_node_memo_store(memo, term, depth, collects);
    return collects;
}

static bool he_term_collects(const Atom *term, uint32_t depth) {
    CettaNodeMemo memo = {0};
    bool collects = he_term_collects_in(term, depth, &memo);
    cetta_node_memo_free(&memo);
    return collects;
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
            (is_grounded_op(head) || symbol_id_is_builtin_for(head, eval_current_language_id()) ||
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
    SymbolId id = atom_symbol_current_id(head);
    bool structural = id == g_builtin_syms.match || id == g_builtin_syms.comma ||
        he_execution_collection(id);
    if (!structural && (eval_space_has_equations(space, id) ||
                        is_grounded_op(id) || symbol_id_is_builtin_for(id, eval_current_language_id())))
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

/* What the admission of a source to HE's shared execution decided for
 * its run: the run's effects, whether it reads a collected value where HE
 * evaluates it again, and whether a caller can bind or observe one of its
 * variables. */
typedef struct {
    bool effects;
    bool rereads;
    bool open_root;
} HeSharedExecutionPlan;

/* The checks that need no read window: the language, the switches, the
 * evaluation's context, and the source's head. */
static inline __attribute__((always_inline)) bool he_shared_execution_candidate(
    Space *space, Atom *source, Atom *type, int fuel, bool observe_boundaries) {
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
        g_active_search_controller_auto || observe_boundaries ||
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
        (is_grounded_op(source_head) || symbol_id_is_builtin_for(source_head, eval_current_language_id())) &&
        source_head != g_builtin_syms.match &&
        !he_execution_collection(source_head))
        return false;
    /* Typed ordinary heads are refused by full admission before their
     * closure is visited. Do not first walk their argument/flow trees just
     * to rediscover that refusal. This is an uncached fallback selection;
     * the canonical call still observes the current declarations and body. */
    if (source_head != g_builtin_syms.match &&
        he_execution_head_requires_canonical(space, source_head, false,
            he_execution_collection(source_head),
            he_library_stream_aggregate(space, source) != SYMBOL_ID_NONE))
        return false;
    /* Such a root gains on the machine only when something in it computes;
     * a match that only builds data is the canonical frame's single query. */
    if ((source_head == g_builtin_syms.match ||
         he_execution_collection(source_head)) &&
        !he_execution_term_computes(space, source))
        return false;
    /* A closed data tuple computes only in its fields.  Each field that is a
     * call meets its own call boundary, where a closed realization is tried
     * before any program is synchronized.  A match and a collection are no
     * data: they compute themselves, judged above. */
    if (source_head != SYMBOL_ID_NONE && !atom_has_vars(source) &&
        !eval_space_has_equations(space, source_head) &&
        source_head != g_builtin_syms.match &&
        !he_execution_collection(source_head))
        return false;
    return true;
}

/* The admission, inside the caller's read window: the source and the
 * closure it reaches keep HE's rules on the shared machine.  Records the
 * closure decisions as it walks. */
static bool he_shared_execution_admit(Space *space, Arena *arena,
                                      Atom *source, bool preserve_bindings,
                                      HeSharedExecutionPlan *plan) {
    SymbolId source_head = atom_head_symbol_id(source);
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
    /* The source is a body too: a match's bindings it reads past the
     * match, and a variable it shares across collected alternatives, keep
     * the canonical owner, as they do in an equation of the closure. */
    admission.match_bindings_escape = he_match_bindings_escape(NULL, source);
    admission.collected_fresh = he_term_collects(source, 0u) &&
        he_execution_has_free_variable(source);
    /* The run's collected values flow through the source and its closure;
     * the closure's own flow is what its heads' decisions keep. */
    HeRawFlowFlags run_flow = {0};
    he_raw_flow_body(space, source, false, &run_flow);
    bool admitted = !admission.match_bindings_escape &&
        !admission.collected_fresh &&
        he_binding_order_holds(space, NULL, source,
                               &admission.structured_binder) &&
        he_execution_source_admitted(&admission, source) &&
        !(admission.queries && admission.structured_binder) &&
        !admission.match_bindings_escape && !admission.collected_fresh;
    he_raw_flow_merge(&run_flow, &admission.raw_flow);
    /* A collected value read where HE evaluates it again is checked where
     * it is collected, and a run that meets one that would evaluate further
     * is declined and evaluated again, which a run with effects cannot be.
     * A set operation's literal is not collected, so it is not checked. */
    admitted = admitted && !run_flow.raw_literal &&
        !(admission.effects && he_raw_flow_rereads(&run_flow));
    /* Every closure walked to the end is admitted; a refused call with plain
     * arguments refuses its head's closure. */
    if (admitted) {
        for (size_t index = 0u; index < admission.head_count; index++)
            he_closure_decision_record(space, admission.heads[index],
                                       admission.open_root, false, true,
                                       &admission);
    } else if (source_head != SYMBOL_ID_NONE &&
               !symbol_id_is_builtin_for(source_head, eval_current_language_id()) &&
               he_execution_plain_arguments(source) &&
               eval_space_has_equations(space, source_head)) {
        he_closure_decision_record(space, source_head, admission.open_root,
                                   false, false, &admission);
    }
    free(admission.terms);
    free(admission.heads);
    atom_visit_set_free(&admission.queued);
    *plan = (HeSharedExecutionPlan){
        .effects = admission.effects,
        .rereads = he_raw_flow_rereads(&run_flow),
        .open_root = admission.open_root,
    };
    return admitted;
}

static bool he_shared_execution_try(Space *space, Arena *arena, Atom *source,
    Atom *type, int fuel, const Bindings *environment, bool preserve_bindings,
    OutcomeSet *outcomes);
static inline __attribute__((always_inline)) bool he_shared_execution_try_kernel(
    Space *space, Arena *arena, Atom *source,
    Atom *type, int fuel, const Bindings *environment, bool preserve_bindings,
    OutcomeSet *outcomes, bool observe_boundaries) {
    if (!he_shared_execution_candidate(space, source, type, fuel,
                                        observe_boundaries))
        return false;
    /* The source qualification and the program observation form one read
     * window. A suspended program owner alone cannot establish currency.
     * Workers retain their canonical adapter until shared program mutation
     * and compiled episodes have a separately qualified concurrent contract. */
    CETTA_SCOPED_SHARED_TRANSITION(he_execution_observation);
    HeSharedExecutionPlan plan;
    if (!he_shared_execution_admit(space, arena, source, preserve_bindings,
                                   &plan))
        return false;
    /* A bounded call (see he_canonical_call_bounded) asks for the successes
     * its selection still needs. The machine completes an effectful closure
     * before it delivers an answer, so it would perform the effects of the
     * alternatives the selection skips: the canonical owner runs it. */
    if (plan.effects && g_he_answer_bound.root &&
        source == g_he_answer_bound.root)
        return false;
    SymbolId source_head = atom_head_symbol_id(source);
    /* Retain the existing qualified closed realizations before allocating a
     * relational episode. A decline publishes nothing and leaves the source
     * for the general machine; open calls never enter this closed attempt. */
    if (!plan.open_root && source && source->kind == ATOM_EXPR &&
        source_head != SYMBOL_ID_NONE && !symbol_id_is_builtin_for(source_head, eval_current_language_id())) {
        Bindings empty;
        bindings_init(&empty);
        const Bindings *outer = environment ? environment : &empty;
        PreparedPureProgramCache *cache = eval_execution_prepared_cache();
        CettaCallOutcome result;
        if (prepared_pure_closed_result_try(
                space, arena, source, fuel, false, false, cache,
                PREPARED_PURE_PROGRAM_OPTIONAL_VALUE, &result)) {
            if (result.kind == CETTA_CALL_VALUE)
                outcome_set_add_prefixed(arena, outcomes, result.term, &empty,
                    outer, preserve_bindings);
            return true;
        }
        if (eval_unwinding() || eval_cancel_check())
            return true;
        CettaPreparedPureAnswersResult answers = prepared_pure_closed_answers_try(
            space, arena, source, type, fuel, cache, outer, preserve_bindings, outcomes);
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
    g_he_shared_execution_effect_free = !plan.effects;
    g_he_shared_execution_rereads = plan.rereads;
    g_active_petta_search_machine = true;
    bool complete = petta_eval_machine_run(space, arena, source, type, fuel,
        environment, preserve_bindings, outcomes, PETTA_RELATION_SAFETY_STATIC,
        CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST, false, false);
    g_active_petta_search_machine = enclosing_active;
    g_he_shared_execution = false;
    return complete;
}

CETTA_PREPARED_ENTRY(bool, he_shared_execution_try,
    (Space *space, Arena *arena, Atom *source,
    Atom *type, int fuel, const Bindings *environment, bool preserve_bindings,
    OutcomeSet *outcomes),
    (space, arena, source, type, fuel, environment, preserve_bindings, outcomes))
