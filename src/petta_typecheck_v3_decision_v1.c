#include "petta_typecheck_v3_decision_v1.h"

#include "match.h"
#include "petta_type_fact_provider_v1.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    V3_GROUND_NO = 0,
    V3_GROUND_YES,
    V3_GROUND_INCOMPLETE,
    V3_GROUND_FAULT,
} V3GroundDecision;

typedef enum {
    V3_UNIQUE_NONE = 0,
    V3_UNIQUE_ONE,
    V3_UNIQUE_MANY,
    V3_UNIQUE_INCOMPLETE,
    V3_UNIQUE_FAULT,
} V3UniqueDecision;

static bool v3_symbol_is(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL && name &&
        strcmp(atom_name_cstr((Atom *)atom), name) == 0;
}

static bool v3_head_is(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom->expr.elems && v3_symbol_is(atom->expr.elems[0], name);
}

static bool v3_is_empty_call(const Atom *atom) {
    return v3_head_is(atom, "empty") && atom->expr.len == 1u;
}

static bool v3_is_two_empty_superposition(const Atom *atom) {
    if (!v3_head_is(atom, "superpose") || atom->expr.len != 2u)
        return false;
    const Atom *alternatives = atom->expr.elems[1];
    return alternatives && alternatives->kind == ATOM_EXPR &&
        alternatives->expr.len == 2u && alternatives->expr.elems &&
        v3_is_empty_call(alternatives->expr.elems[0]) &&
        v3_is_empty_call(alternatives->expr.elems[1]);
}

static bool v3_is_if_all_empty(const Atom *atom) {
    return v3_head_is(atom, "if") && atom->expr.len == 4u &&
        v3_is_empty_call(atom->expr.elems[2]) &&
        v3_is_empty_call(atom->expr.elems[3]);
}

static bool v3_is_case_all_empty(const Atom *atom) {
    if (!v3_head_is(atom, "case") || atom->expr.len != 3u)
        return false;
    const Atom *branches = atom->expr.elems[2];
    if (!branches || branches->kind != ATOM_EXPR ||
        branches->expr.len != 2u || !branches->expr.elems) {
        return false;
    }
    for (CettaExprIndex index = 0u; index < branches->expr.len; index++) {
        const Atom *branch = branches->expr.elems[index];
        if (!branch || branch->kind != ATOM_EXPR ||
            branch->expr.len != 2u || !branch->expr.elems ||
            !v3_is_empty_call(branch->expr.elems[1])) {
            return false;
        }
    }
    return true;
}

static bool v3_is_single_empty_superposition(const Atom *atom) {
    if (!v3_head_is(atom, "superpose") || atom->expr.len != 2u)
        return false;
    const Atom *alternatives = atom->expr.elems[1];
    return alternatives && alternatives->kind == ATOM_EXPR &&
        alternatives->expr.len == 1u && alternatives->expr.elems &&
        v3_is_empty_call(alternatives->expr.elems[0]);
}

static bool v3_is_fold_empty_generator(const Atom *atom) {
    return v3_head_is(atom, "foldall") && atom->expr.len == 4u &&
        (v3_is_empty_call(atom->expr.elems[2]) ||
         v3_is_single_empty_superposition(atom->expr.elems[2]));
}

static Atom *v3_relation2(
    Arena *arena, const char *name, const Atom *left, const Atom *right) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 3u);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, name);
    elements[1] = atom_deep_copy(arena, (Atom *)left);
    elements[2] = atom_deep_copy(arena, (Atom *)right);
    if (!elements[0] || !elements[1] || !elements[2])
        return NULL;
    return atom_expr(arena, elements, 3u);
}

static Atom *v3_expr1(Arena *arena, const char *name, const Atom *argument) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 2u);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, name);
    elements[1] = atom_deep_copy(arena, (Atom *)argument);
    if (!elements[0] || !elements[1])
        return NULL;
    return atom_expr(arena, elements, 2u);
}

static Atom *v3_expr2(
    Arena *arena, const char *name, const Atom *left, const Atom *right) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 3u);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, name);
    elements[1] = atom_deep_copy(arena, (Atom *)left);
    elements[2] = atom_deep_copy(arena, (Atom *)right);
    if (!elements[0] || !elements[1] || !elements[2])
        return NULL;
    return atom_expr(arena, elements, 3u);
}

static Atom *v3_relation3(
    Arena *arena, const char *name, const Atom *first, const Atom *second,
    const Atom *third) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 4u);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, name);
    elements[1] = atom_deep_copy(arena, (Atom *)first);
    elements[2] = atom_deep_copy(arena, (Atom *)second);
    elements[3] = atom_deep_copy(arena, (Atom *)third);
    if (!elements[0] || !elements[1] || !elements[2] || !elements[3])
        return NULL;
    return atom_expr(arena, elements, 4u);
}

static Atom *v3_relation4(
    Arena *arena, const char *name, const Atom *first, const Atom *second,
    const Atom *third, const Atom *fourth) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 5u);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, name);
    elements[1] = atom_deep_copy(arena, (Atom *)first);
    elements[2] = atom_deep_copy(arena, (Atom *)second);
    elements[3] = atom_deep_copy(arena, (Atom *)third);
    elements[4] = atom_deep_copy(arena, (Atom *)fourth);
    if (!elements[0] || !elements[1] || !elements[2] || !elements[3] ||
        !elements[4]) {
        return NULL;
    }
    return atom_expr(arena, elements, 5u);
}

static Atom *v3_quoted_wire_type(
    Arena *arena, const Atom *value, uint32_t depth);

static Atom *v3_quoted_wire_fields(
    Arena *arena, const Atom *expression, uint32_t depth) {
    if (!arena || !expression || expression->kind != ATOM_EXPR ||
        depth > 256u) {
        return NULL;
    }
    Atom *tail = atom_symbol(arena, "TTNil");
    if (!tail)
        return NULL;
    for (CettaExprIndex index = expression->expr.len; index > 0u; index--) {
        Atom *field = v3_quoted_wire_type(
            arena, expression->expr.elems[index - 1u], depth + 1u);
        tail = field ? v3_expr2(arena, "TTCons", field, tail) : NULL;
        if (!tail)
            return NULL;
    }
    return tail;
}

/* Structural code typing never interprets a quoted head as a call. */
static Atom *v3_quoted_wire_type(
    Arena *arena, const Atom *value, uint32_t depth) {
    if (!arena || !value || depth > 256u)
        return NULL;
    if (value->kind == ATOM_VAR)
        return atom_symbol(arena, "TUndefined");
    if (value->kind == ATOM_SYMBOL)
        return atom_symbol(arena, "TAtom");
    if (value->kind == ATOM_EXPR) {
        Atom *fields = v3_quoted_wire_fields(arena, value, depth + 1u);
        return fields ? v3_expr1(arena, "TProduct", fields) : NULL;
    }
    if (value->kind != ATOM_GROUNDED)
        return NULL;
    switch (value->ground.gkind) {
    case GV_INT:
    case GV_FLOAT:
    case GV_BIGINT:
    case GV_RATIONAL:
        return atom_symbol(arena, "TNum");
    case GV_STRING:
        return atom_symbol(arena, "TStr");
    case GV_BOOL:
        return atom_symbol(arena, "TBool");
    default:
        return atom_symbol(arena, "TUndefined");
    }
}

static Atom *v3_quoted_data_marker(Arena *arena, const Atom *body) {
    if (!v3_head_is(body, "quote") || body->expr.len != 2u ||
        !v3_head_is(body->expr.elems[1], "data")) {
        return NULL;
    }
    Atom *fields = v3_quoted_wire_fields(arena, body->expr.elems[1], 0u);
    return fields ? v3_expr1(arena, "V3SourceQuotedProduct", fields) : NULL;
}

static Atom *v3_runtime_evidence_for_type_and_facts(
    Arena *arena, const Atom *type, const char *card,
    const char *proper_list, const char *boolean,
    const char *nonempty_expression) {
    Atom *typed = v3_expr1(arena, "V3TypedResult", type);
    Atom **facts_elements = arena_alloc(arena, sizeof(*facts_elements) * 4u);
    Atom **runtime_elements = arena_alloc(
        arena, sizeof(*runtime_elements) * 5u);
    if (!typed || !facts_elements || !runtime_elements)
        return NULL;
    facts_elements[0] = atom_symbol(arena, "V3Facts");
    facts_elements[1] = atom_symbol(arena, proper_list);
    facts_elements[2] = atom_symbol(arena, boolean);
    facts_elements[3] = atom_symbol(arena, nonempty_expression);
    Atom *facts = atom_expr(arena, facts_elements, 4u);
    runtime_elements[0] = atom_symbol(arena, "V3RuntimeEvidence");
    runtime_elements[1] = atom_symbol(arena, "V3StageEvaluated");
    runtime_elements[2] = typed;
    runtime_elements[3] = atom_symbol(arena, card);
    runtime_elements[4] = facts;
    if (!facts || !runtime_elements[0] || !runtime_elements[1] ||
        !runtime_elements[3]) {
        return NULL;
    }
    return atom_expr(arena, runtime_elements, 5u);
}

static Atom *v3_runtime_evidence_for_type(
    Arena *arena, const Atom *type, const char *card) {
    const char *proper_list = v3_head_is(type, "V3List")
        ? "V3Yes" : "V3No";
    const char *boolean = v3_head_is(type, "V3Prim") &&
        type->expr.len == 2u && v3_symbol_is(type->expr.elems[1], "V3Bool")
        ? "V3Yes" : "V3No";
    const char *nonempty_expression = v3_head_is(type, "V3Arrow")
        ? "V3Yes" : "V3No";
    return v3_runtime_evidence_for_type_and_facts(
        arena, type, card, proper_list, boolean, nonempty_expression);
}

static Atom *v3_grade(Arena *arena, const Atom *card) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 2u);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, "V3Grade");
    elements[1] = atom_deep_copy(arena, (Atom *)card);
    if (!elements[0] || !elements[1])
        return NULL;
    return atom_expr(arena, elements, 2u);
}

static V3GroundDecision v3_run_ground(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornOutcome *outcome,
    char *error,
    size_t error_size) {
    Arena answers;
    arena_init(&answers);
    CettaGsltHornResult result = {0};
    bool ran = query && cetta_gslt_horn_query_with_providers_v1(
        program, providers, &answers, query, limits, &result,
        error, error_size);
    if (outcome)
        *outcome = result.outcome;
    V3GroundDecision decision = V3_GROUND_FAULT;
    if (ran && result.answer_count > 0u) {
        decision = V3_GROUND_YES;
    } else if (ran && result.outcome == CETTA_GSLT_HORN_COMPLETED) {
        decision = V3_GROUND_NO;
    } else if (ran && result.outcome != CETTA_GSLT_HORN_FAULT) {
        decision = V3_GROUND_INCOMPLETE;
    }
    cetta_gslt_horn_result_free(&result);
    arena_free(&answers);
    return decision;
}

static V3UniqueDecision v3_run_unique(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *answers,
    Atom *query,
    CettaGsltHornLimits limits,
    Atom **answer,
    CettaGsltHornOutcome *outcome,
    char *error,
    size_t error_size) {
    if (answer)
        *answer = NULL;
    CettaGsltHornResult result = {0};
    bool ran = query && cetta_gslt_horn_query_with_providers_v1(
        program, providers, answers, query, limits, &result,
        error, error_size);
    if (outcome)
        *outcome = result.outcome;
    V3UniqueDecision decision = V3_UNIQUE_FAULT;
    if (ran && result.outcome == CETTA_GSLT_HORN_COMPLETED) {
        if (result.answer_count == 0u) {
            decision = V3_UNIQUE_NONE;
        } else if (result.answer_count == 1u) {
            if (answer)
                *answer = result.answers[0];
            decision = V3_UNIQUE_ONE;
        } else {
            decision = V3_UNIQUE_MANY;
        }
    } else if (ran && result.outcome != CETTA_GSLT_HORN_FAULT) {
        decision = V3_UNIQUE_INCOMPLETE;
    }
    cetta_gslt_horn_result_free(&result);
    return decision;
}

static const char *v3_seam_outcome_name(
    CettaPettaV3EvidenceOutcomeV1 outcome) {
    switch (outcome) {
    case CETTA_PETTA_V3_EVIDENCE_ESTABLISHED:
        return "V3Established";
    case CETTA_PETTA_V3_EVIDENCE_REFUTED:
        return "V3Refuted";
    case CETTA_PETTA_V3_EVIDENCE_UNDETERMINED:
        return "V3Undetermined";
    case CETTA_PETTA_V3_EVIDENCE_INCOMPLETE:
        return "V3Incomplete";
    }
    return NULL;
}

/* The seam is a fixed, terminating classifier.  Give it enough local fuel
 * even when the caller deliberately constrains the preceding language
 * judgment to test or report an incomplete search. */
static CettaGsltHornLimits v3_seam_limits(CettaGsltHornLimits limits) {
    if (limits.max_rule_attempts < 32u)
        limits.max_rule_attempts = 32u;
    if (limits.max_answers < 2u)
        limits.max_answers = 2u;
    if (limits.max_depth < 16u)
        limits.max_depth = 16u;
    return limits;
}

/* Run the generated seam after checkEvidence has produced its outcome.
 * Exactness is computed by the generated V3ExactTy rules; wrapping the two
 * types in V3Union reuses the proved conjunction law without duplicating a
 * Boolean reducer in C. */
static bool v3_classify_evidence_seam(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    const Atom *evidence,
    const Atom *expected,
    const Atom *demand,
    CettaGsltHornLimits limits,
    CettaPettaV3EvidenceDecisionV1 *decision,
    char *error,
    size_t error_size) {
    if (!program || !providers || !evidence || !expected || !demand ||
        !decision || !v3_head_is(evidence, "V3RuntimeEvidence") ||
        evidence->expr.len != 5u) {
        return false;
    }
    limits = v3_seam_limits(limits);
    const Atom *result = evidence->expr.elems[2];
    const Atom *card = evidence->expr.elems[3];
    bool typed = v3_head_is(result, "V3TypedResult") &&
        result->expr.len == 2u;
    const Atom *actual = typed ? result->expr.elems[1] : NULL;

    Arena scratch;
    arena_init(&scratch);
    Atom *exactness = atom_symbol(&scratch, "V3No");
    CettaGsltHornOutcome search_outcome = CETTA_GSLT_HORN_FAULT;
    if (decision->outcome == CETTA_PETTA_V3_EVIDENCE_ESTABLISHED && typed) {
        Atom *pair = v3_expr2(&scratch, "V3Union", actual, expected);
        Atom *exact_variable = atom_var(&scratch, "v3-seam-exactness");
        Atom *exact_query = pair && exact_variable
            ? v3_relation2(
                &scratch, "V3ExactTy", pair, exact_variable)
            : NULL;
        Atom *exact_answer = NULL;
        V3UniqueDecision exact = v3_run_unique(
            program, providers, &scratch, exact_query, limits,
            &exact_answer, &search_outcome, error, error_size);
        if (exact == V3_UNIQUE_FAULT || exact == V3_UNIQUE_INCOMPLETE ||
            exact != V3_UNIQUE_ONE ||
            !v3_head_is(exact_answer, "V3ExactTy") ||
            exact_answer->expr.len != 3u) {
            if (error && error_size > 0u && !error[0])
                (void)snprintf(
                    error, error_size,
                    "v3 exactness classification did not complete uniquely");
            arena_free(&scratch);
            return false;
        }
        exactness = exact_answer->expr.elems[2];
    }

    const char *outcome_name = v3_seam_outcome_name(decision->outcome);
    Atom *outcome = outcome_name
        ? atom_symbol(&scratch, outcome_name) : NULL;
    Atom *result_kind = atom_symbol(
        &scratch, typed ? "V3TypedEvidence" : "V3UntypedEvidence");
    Atom *kind_variable = atom_var(&scratch, "v3-seam-kind");
    Atom *query = outcome && result_kind && exactness && kind_variable
        ? v3_relation4(
            &scratch, "V3SeamKind", outcome, result_kind,
            exactness, kind_variable)
        : NULL;
    Atom *answer = NULL;
    V3UniqueDecision classified = v3_run_unique(
        program, providers, &scratch, query, limits,
        &answer, &search_outcome, error, error_size);
    if (classified == V3_UNIQUE_FAULT || classified == V3_UNIQUE_INCOMPLETE ||
        classified != V3_UNIQUE_ONE ||
        !v3_head_is(answer, "V3SeamKind") || answer->expr.len != 5u) {
        if (error && error_size > 0u && !error[0])
            (void)snprintf(
                error, error_size,
                "v3 seam classification did not complete uniquely");
        arena_free(&scratch);
        return false;
    }
    const Atom *kind = answer->expr.elems[4];
    if (v3_symbol_is(kind, "V3Exact")) {
        if (!typed || !v3_symbol_is(exactness, "V3Yes")) {
            if (error && error_size > 0u)
                (void)snprintf(
                    error, error_size,
                    "v3 seam attempted to license inexact evidence");
            arena_free(&scratch);
            return false;
        }
        decision->seam_kind = CETTA_PETTA_V3_SEAM_EXACT;
        decision->optimization_license =
            (CettaPettaV3EvidenceOptLicenseV1){
                .issued = true,
                .actual_hash = atom_hash((Atom *)actual),
                .expected_hash = atom_hash((Atom *)expected),
                .cardinality_hash = atom_hash((Atom *)card),
                .demand_hash = atom_hash((Atom *)demand),
            };
    } else if (v3_symbol_is(kind, "V3Gradual")) {
        decision->seam_kind = CETTA_PETTA_V3_SEAM_GRADUAL;
    } else if (v3_symbol_is(kind, "V3Conflict")) {
        decision->seam_kind = CETTA_PETTA_V3_SEAM_CONFLICT;
    } else {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size, "invalid v3 seam kind");
        arena_free(&scratch);
        return false;
    }
    arena_free(&scratch);
    return true;
}

static V3UniqueDecision v3_elaborate_wire_type(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const Atom *wire_type, CettaGsltHornLimits limits,
    Atom **core_type, CettaGsltHornOutcome *search_outcome,
    char *error, size_t error_size) {
    if (core_type)
        *core_type = NULL;
    Atom *core_variable = atom_var(arena, "v3-elaborated-type");
    Atom *query = wire_type && core_variable
        ? v3_relation2(arena, "V3ElaborateType", wire_type, core_variable)
        : NULL;
    Atom *answer = NULL;
    V3UniqueDecision elaboration = v3_run_unique(
        program, providers, arena, query, limits, &answer,
        search_outcome, error, error_size);
    if (elaboration != V3_UNIQUE_ONE)
        return elaboration;
    if (!v3_head_is(answer, "V3ElaborateType") ||
        answer->expr.len != 3u) {
        return V3_UNIQUE_NONE;
    }
    if (core_type)
        *core_type = answer->expr.elems[2];
    return V3_UNIQUE_ONE;
}

static V3UniqueDecision v3_unique_signature(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const Atom *subject, CettaGsltHornLimits limits,
    Atom **signature, CettaGsltHornOutcome *search_outcome,
    char *error, size_t error_size) {
    if (signature)
        *signature = NULL;
    Atom *declarations_variable = atom_var(arena, "v3-signature-declarations");
    Atom *declarations_query = v3_relation2(
        arena, "EnvDeclaredList", subject, declarations_variable);
    Atom *declarations_answer = NULL;
    V3UniqueDecision declarations = v3_run_unique(
        program, providers, arena, declarations_query, limits,
        &declarations_answer, search_outcome, error, error_size);
    if (declarations != V3_UNIQUE_ONE)
        return declarations;
    if (!v3_head_is(declarations_answer, "EnvDeclaredList") ||
        declarations_answer->expr.len != 3u) {
        return V3_UNIQUE_NONE;
    }
    const Atom *list = declarations_answer->expr.elems[2];
    if (!v3_head_is(list, "DCons") || list->expr.len != 3u ||
        !v3_symbol_is(list->expr.elems[2], "DNil") ||
        !v3_head_is(list->expr.elems[1], "Decl") ||
        list->expr.elems[1]->expr.len != 3u) {
        return V3_UNIQUE_NONE;
    }
    const Atom *wire_type = list->expr.elems[1]->expr.elems[2];
    return v3_elaborate_wire_type(
        program, providers, arena, wire_type, limits, signature,
        search_outcome, error, error_size);
}

typedef enum {
    V3_SOURCE_NONE = 0,
    V3_SOURCE_EVIDENCE,
    V3_SOURCE_SHAPE_CONFLICT,
    V3_SOURCE_INCOMPLETE,
    V3_SOURCE_FAULT,
} V3SourceStatus;

typedef struct {
    V3SourceStatus status;
    Atom *evidence;
    const Atom *actual;
    const Atom *required;
    const char *relation;
    CettaGsltHornOutcome search_outcome;
} V3SourceCompilation;

typedef struct {
    VarId variable;
    const Atom *type;
    const Atom *evidence;
} V3SourceBinding;

typedef struct {
    V3SourceBinding bindings[128];
    size_t count;
} V3SourceEnvironment;

typedef enum {
    V3_PATTERN_ADMITTED = 0,
    V3_PATTERN_EMPTY,
    V3_PATTERN_INCOMPLETE,
    V3_PATTERN_FAULT,
} V3PatternStatus;

typedef struct {
    V3PatternStatus status;
    const Atom *actual;
    const Atom *required;
    const char *relation;
    CettaGsltHornOutcome search_outcome;
} V3PatternAdmission;

static const Atom *v3_source_environment_lookup(
    const V3SourceEnvironment *environment, const Atom *variable) {
    if (!environment || !variable || variable->kind != ATOM_VAR)
        return NULL;
    for (size_t index = environment->count; index > 0u; index--) {
        if (environment->bindings[index - 1u].variable == variable->var_id)
            return environment->bindings[index - 1u].type;
    }
    return NULL;
}

static const Atom *v3_source_environment_evidence(
    const V3SourceEnvironment *environment, const Atom *variable) {
    if (!environment || !variable || variable->kind != ATOM_VAR)
        return NULL;
    for (size_t index = environment->count; index > 0u; index--) {
        if (environment->bindings[index - 1u].variable == variable->var_id)
            return environment->bindings[index - 1u].evidence;
    }
    return NULL;
}

static V3UniqueDecision v3_source_callable_signature(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *callable, CettaGsltHornLimits limits,
    Atom **signature, CettaGsltHornOutcome *search_outcome,
    char *error, size_t error_size) {
    if (signature)
        *signature = NULL;
    if (!callable)
        return V3_UNIQUE_NONE;
    if (callable->kind == ATOM_VAR) {
        const Atom *bound = v3_source_environment_lookup(
            environment, callable);
        if (!bound)
            return V3_UNIQUE_NONE;
        if (signature)
            *signature = (Atom *)bound;
        if (search_outcome)
            *search_outcome = CETTA_GSLT_HORN_COMPLETED;
        return V3_UNIQUE_ONE;
    }
    if (callable->kind != ATOM_SYMBOL)
        return V3_UNIQUE_NONE;
    return v3_unique_signature(
        program, providers, arena, callable, limits,
        signature, search_outcome, error, error_size);
}

static const Atom *v3_runtime_evidence_actual(const Atom *evidence) {
    if (!v3_head_is(evidence, "V3RuntimeEvidence") ||
        evidence->expr.len != 5u ||
        !v3_head_is(evidence->expr.elems[2], "V3TypedResult") ||
        evidence->expr.elems[2]->expr.len != 2u) {
        return NULL;
    }
    return evidence->expr.elems[2]->expr.elems[1];
}

static Atom *v3_runtime_evidence_with_card(
    Arena *arena, const Atom *evidence, const Atom *card) {
    if (!v3_head_is(evidence, "V3RuntimeEvidence") ||
        evidence->expr.len != 5u || !card) {
        return NULL;
    }
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 5u);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, "V3RuntimeEvidence");
    elements[1] = atom_deep_copy(arena, evidence->expr.elems[1]);
    elements[2] = atom_deep_copy(arena, evidence->expr.elems[2]);
    elements[3] = atom_deep_copy(arena, (Atom *)card);
    elements[4] = atom_deep_copy(arena, evidence->expr.elems[4]);
    if (!elements[0] || !elements[1] || !elements[2] || !elements[3] ||
        !elements[4]) {
        return NULL;
    }
    return atom_expr(arena, elements, 5u);
}

static Atom *v3_runtime_evidence_from_parts(
    Arena *arena, const Atom *type, const Atom *card, const Atom *facts) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 5u);
    Atom *typed = v3_expr1(arena, "V3TypedResult", type);
    if (!elements || !typed)
        return NULL;
    elements[0] = atom_symbol(arena, "V3RuntimeEvidence");
    elements[1] = atom_symbol(arena, "V3StageEvaluated");
    elements[2] = typed;
    elements[3] = atom_deep_copy(arena, (Atom *)card);
    elements[4] = atom_deep_copy(arena, (Atom *)facts);
    if (!elements[0] || !elements[1] || !elements[3] || !elements[4])
        return NULL;
    return atom_expr(arena, elements, 5u);
}

static Atom *v3_debruijn_index(Arena *arena, size_t index) {
    Atom *term = atom_symbol(arena, "V3IndexZero");
    for (size_t step = 0u; term && step < index; step++)
        term = v3_expr1(arena, "V3IndexSucc", term);
    return term;
}

static V3SourceCompilation v3_compile_source_evidence(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size, uint32_t depth);

static Atom *v3_reify_source_variables(
    Arena *arena, const Atom *expression, uint32_t depth) {
    if (!arena || !expression || depth > 256u)
        return NULL;
    if (expression->kind == ATOM_VAR)
        return atom_symbol(arena, "V3SourceVar");
    if (expression->kind != ATOM_EXPR)
        return atom_deep_copy(arena, (Atom *)expression);
    Atom **elements = arena_alloc(
        arena, sizeof(*elements) * expression->expr.len);
    if (!elements && expression->expr.len != 0u)
        return NULL;
    for (CettaExprIndex index = 0u;
         index < expression->expr.len; index++) {
        elements[index] = v3_reify_source_variables(
            arena, expression->expr.elems[index], depth + 1u);
        if (!elements[index])
            return NULL;
    }
    return atom_expr(arena, elements, expression->expr.len);
}

static V3SourceCompilation v3_query_source_evidence(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size) {
    V3SourceCompilation compiled = {0};
    Atom *reified = v3_reify_source_variables(arena, expression, 0u);
    Atom *variable = atom_var(arena, "v3-recursive-source-evidence");
    Atom *query = v3_relation2(
        arena, "V3ExpressionEvidence", reified, variable);
    Atom *answer = NULL;
    V3UniqueDecision result = v3_run_unique(
        program, providers, arena, query, limits, &answer,
        &compiled.search_outcome, error, error_size);
    if (result == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
    } else if (result == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3ExpressionEvidence";
    } else if (result == V3_UNIQUE_ONE &&
               v3_head_is(answer, "V3ExpressionEvidence") &&
               answer->expr.len == 3u) {
        compiled.status = V3_SOURCE_EVIDENCE;
        compiled.evidence = answer->expr.elems[2];
    }
    return compiled;
}

static V3PatternAdmission v3_pattern_overlap(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const Atom *actual, const Atom *required,
    CettaGsltHornLimits limits, char *error, size_t error_size) {
    V3PatternAdmission admission = {0};
    Atom *query = v3_relation2(arena, "V3MayOverlap", actual, required);
    V3GroundDecision overlap = v3_run_ground(
        program, providers, query, limits, &admission.search_outcome,
        error, error_size);
    if (overlap == V3_GROUND_YES) {
        admission.status = V3_PATTERN_ADMITTED;
    } else if (overlap == V3_GROUND_NO) {
        admission.status = V3_PATTERN_EMPTY;
        admission.actual = actual;
        admission.required = required;
        admission.relation = "V3MayOverlap";
    } else if (overlap == V3_GROUND_INCOMPLETE) {
        admission.status = V3_PATTERN_INCOMPLETE;
        admission.relation = "V3MayOverlap";
    } else {
        admission.status = V3_PATTERN_FAULT;
    }
    return admission;
}

static V3PatternAdmission v3_bind_source_pattern(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, V3SourceEnvironment *environment,
    const Atom *pattern, const Atom *expected,
    CettaGsltHornLimits limits, char *error, size_t error_size,
    uint32_t depth);

static V3PatternAdmission v3_bind_source_variable(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, V3SourceEnvironment *environment,
    const Atom *variable, const Atom *expected,
    CettaGsltHornLimits limits, char *error, size_t error_size) {
    V3PatternAdmission admission = {0};
    for (size_t index = environment->count; index > 0u; index--) {
        V3SourceBinding *binding = &environment->bindings[index - 1u];
        if (binding->variable != variable->var_id)
            continue;
        admission = v3_pattern_overlap(
            program, providers, arena, binding->type, expected,
            limits, error, error_size);
        if (admission.status == V3_PATTERN_ADMITTED &&
            v3_symbol_is(binding->type, "V3Unknown") &&
            !v3_symbol_is(expected, "V3Unknown")) {
            binding->type = expected;
        }
        return admission;
    }
    if (environment->count >=
        sizeof(environment->bindings) / sizeof(environment->bindings[0])) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "v3 pattern environment exceeds 128 bindings");
        admission.status = V3_PATTERN_FAULT;
        return admission;
    }
    environment->bindings[environment->count++] = (V3SourceBinding){
        .variable = variable->var_id,
        .type = expected,
        .evidence = NULL,
    };
    return admission;
}

static const Atom *v3_pattern_representation(
    const Atom *type, uint32_t depth) {
    while (type && depth <= 128u &&
           v3_head_is(type, "V3Newtype") && type->expr.len == 3u) {
        type = type->expr.elems[2];
        depth++;
    }
    return type;
}

/* `is-expr` refines the erased representation of a nominal value without
 * discarding its nominal identity.  Primitive representation eliminators can
 * subsequently inspect the refined representation, while ordinary calls
 * still see the original newtype brand. */
static Atom *v3_refine_expression_representation(
    Arena *arena, const Atom *type, uint32_t depth) {
    if (!arena || depth > 128u)
        return NULL;
    if (v3_head_is(type, "V3Newtype") && type->expr.len == 3u) {
        Atom *representation = v3_refine_expression_representation(
            arena, type->expr.elems[2], depth + 1u);
        return representation ? v3_expr2(
            arena, "V3Newtype", type->expr.elems[1], representation) : NULL;
    }
    Atom *unknown = atom_symbol(arena, "V3Unknown");
    return unknown ? v3_expr1(arena, "V3List", unknown) : NULL;
}

static V3PatternAdmission v3_pattern_arity_empty(
    const Atom *actual, const Atom *required) {
    return (V3PatternAdmission){
        .status = V3_PATTERN_EMPTY,
        .actual = actual,
        .required = required,
        .relation = "V3PatternArity",
        .search_outcome = CETTA_GSLT_HORN_COMPLETED,
    };
}

static V3PatternAdmission v3_bind_pattern_fields(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, V3SourceEnvironment *environment,
    Atom *const *patterns, size_t count, const Atom *fields,
    CettaGsltHornLimits limits, char *error, size_t error_size,
    uint32_t depth) {
    const Atom *remaining = fields;
    for (size_t index = 0u; index < count; index++) {
        if (!v3_head_is(remaining, "V3ArgsCons") ||
            remaining->expr.len != 3u) {
            return v3_pattern_arity_empty(NULL, fields);
        }
        V3PatternAdmission child = v3_bind_source_pattern(
            program, providers, arena, environment, patterns[index],
            remaining->expr.elems[1], limits, error, error_size, depth + 1u);
        if (child.status != V3_PATTERN_ADMITTED)
            return child;
        remaining = remaining->expr.elems[2];
    }
    if (!v3_symbol_is(remaining, "V3ArgsNil"))
        return v3_pattern_arity_empty(NULL, fields);
    return (V3PatternAdmission){0};
}

static V3PatternAdmission v3_bind_source_pattern(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, V3SourceEnvironment *environment,
    const Atom *pattern, const Atom *expected,
    CettaGsltHornLimits limits, char *error, size_t error_size,
    uint32_t depth) {
    V3PatternAdmission admission = {0};
    if (!pattern || !expected || depth > 128u) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "v3 pattern admission exceeded its structural limit");
        admission.status = V3_PATTERN_FAULT;
        return admission;
    }
    if (pattern->kind == ATOM_VAR)
        return v3_bind_source_variable(
            program, providers, arena, environment, pattern, expected,
            limits, error, error_size);

    /* A structural pattern can refine a union when exactly one alternative
     * admits it.  Bind against the selected member so constructor fields keep
     * their types; an ambiguous match remains gradual and falls through to
     * the ordinary overlap judgment below. */
    if (v3_head_is(expected, "V3Union") && expected->expr.len == 3u) {
        V3SourceEnvironment left_environment = *environment;
        V3SourceEnvironment right_environment = *environment;
        V3PatternAdmission left = v3_bind_source_pattern(
            program, providers, arena, &left_environment, pattern,
            expected->expr.elems[1], limits, error, error_size, depth + 1u);
        V3PatternAdmission right = v3_bind_source_pattern(
            program, providers, arena, &right_environment, pattern,
            expected->expr.elems[2], limits, error, error_size, depth + 1u);
        if (left.status == V3_PATTERN_FAULT ||
            right.status == V3_PATTERN_FAULT) {
            return (V3PatternAdmission){
                .status = V3_PATTERN_FAULT,
            };
        }
        if (left.status == V3_PATTERN_INCOMPLETE ||
            right.status == V3_PATTERN_INCOMPLETE) {
            return (V3PatternAdmission){
                .status = V3_PATTERN_INCOMPLETE,
                .relation = left.status == V3_PATTERN_INCOMPLETE
                    ? left.relation : right.relation,
                .search_outcome = left.status == V3_PATTERN_INCOMPLETE
                    ? left.search_outcome : right.search_outcome,
            };
        }
        if (left.status == V3_PATTERN_ADMITTED &&
            right.status == V3_PATTERN_EMPTY) {
            *environment = left_environment;
            return left;
        }
        if (left.status == V3_PATTERN_EMPTY &&
            right.status == V3_PATTERN_ADMITTED) {
            *environment = right_environment;
            return right;
        }
        if (left.status == V3_PATTERN_EMPTY &&
            right.status == V3_PATTERN_EMPTY) {
            return (V3PatternAdmission){
                .status = V3_PATTERN_EMPTY,
                .actual = NULL,
                .required = expected,
                .relation = "V3MayOverlap",
                .search_outcome = CETTA_GSLT_HORN_COMPLETED,
            };
        }
    }

    const Atom *representation = v3_pattern_representation(expected, depth);
    if (pattern->kind == ATOM_EXPR) {
        if (pattern->expr.len == 0u) {
            if ((v3_head_is(representation, "V3List") ||
                 (v3_head_is(representation, "V3Product") &&
                  representation->expr.len == 2u &&
                  v3_symbol_is(
                      representation->expr.elems[1], "V3ArgsNil")))) {
                return admission;
            }
            if (!v3_symbol_is(representation, "V3Unknown"))
                return v3_pattern_arity_empty(
                    representation, atom_symbol(arena, "V3List"));
        }

        if (v3_head_is(pattern, "cons") && pattern->expr.len == 3u &&
            v3_head_is(representation, "V3List") &&
            representation->expr.len == 2u) {
            const Atom *element = representation->expr.elems[1];
            V3PatternAdmission head = v3_bind_source_pattern(
                program, providers, arena, environment,
                pattern->expr.elems[1], element, limits,
                error, error_size, depth + 1u);
            if (head.status != V3_PATTERN_ADMITTED)
                return head;
            return v3_bind_source_pattern(
                program, providers, arena, environment,
                pattern->expr.elems[2], representation, limits,
                error, error_size, depth + 1u);
        }
        if (v3_head_is(pattern, "cons") && pattern->expr.len == 3u &&
            !v3_symbol_is(representation, "V3Unknown")) {
            return v3_pattern_arity_empty(
                representation, atom_symbol(arena, "V3List"));
        }

        if (pattern->expr.len > 0u &&
            pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
            Atom *signature = NULL;
            V3UniqueDecision callable = v3_source_callable_signature(
                program, providers, arena, environment,
                pattern->expr.elems[0], limits, &signature,
                &admission.search_outcome, error, error_size);
            if (callable == V3_UNIQUE_FAULT) {
                admission.status = V3_PATTERN_FAULT;
                return admission;
            }
            if (callable == V3_UNIQUE_INCOMPLETE) {
                admission.status = V3_PATTERN_INCOMPLETE;
                admission.relation = "EnvDeclaredList";
                return admission;
            }
            if (callable == V3_UNIQUE_ONE &&
                v3_head_is(signature, "V3Arrow") &&
                signature->expr.len == 4u) {
                admission = v3_pattern_overlap(
                    program, providers, arena, signature->expr.elems[3],
                    expected, limits, error, error_size);
                if (admission.status != V3_PATTERN_ADMITTED)
                    return admission;
                return v3_bind_pattern_fields(
                    program, providers, arena, environment,
                    &pattern->expr.elems[1], pattern->expr.len - 1u,
                    signature->expr.elems[1], limits,
                    error, error_size, depth + 1u);
            }
        }

        if (v3_head_is(representation, "V3Product") &&
            representation->expr.len == 2u) {
            return v3_bind_pattern_fields(
                program, providers, arena, environment,
                pattern->expr.elems, pattern->expr.len,
                representation->expr.elems[1], limits,
                error, error_size, depth + 1u);
        }
        if (v3_symbol_is(representation, "V3Unknown")) {
            Atom *unknown = atom_symbol(arena, "V3Unknown");
            for (CettaExprIndex index = 0u;
                 unknown && index < pattern->expr.len; index++) {
                admission = v3_bind_source_pattern(
                    program, providers, arena, environment,
                    pattern->expr.elems[index], unknown, limits,
                    error, error_size, depth + 1u);
                if (admission.status != V3_PATTERN_ADMITTED)
                    return admission;
            }
            return unknown ? admission : (V3PatternAdmission){
                .status = V3_PATTERN_FAULT,
            };
        }
    }

    V3SourceCompilation literal = v3_query_source_evidence(
        program, providers, arena, pattern, limits, error, error_size);
    if (literal.status == V3_SOURCE_FAULT) {
        admission.status = V3_PATTERN_FAULT;
        return admission;
    }
    if (literal.status == V3_SOURCE_INCOMPLETE) {
        admission.status = V3_PATTERN_INCOMPLETE;
        admission.relation = literal.relation;
        admission.search_outcome = literal.search_outcome;
        return admission;
    }
    const Atom *actual = literal.status == V3_SOURCE_EVIDENCE
        ? v3_runtime_evidence_actual(literal.evidence) : NULL;
    return actual
        ? v3_pattern_overlap(
            program, providers, arena, actual, expected,
            limits, error, error_size)
        : admission;
}

static V3SourceCompilation v3_compile_ascription(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size, uint32_t depth) {
    V3SourceCompilation compiled = {0};
    if (!v3_head_is(expression, "the") || expression->expr.len != 3u)
        return compiled;

    Atom *wire_type = cetta_petta_type_fact_wire_type_v1(
        arena, expression->expr.elems[1]);
    Atom *ascribed_type = NULL;
    V3UniqueDecision elaboration = v3_elaborate_wire_type(
        program, providers, arena, wire_type, limits, &ascribed_type,
        &compiled.search_outcome, error, error_size);
    if (elaboration == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (elaboration == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3ElaborateType";
        return compiled;
    }
    if (elaboration != V3_UNIQUE_ONE || !ascribed_type)
        return compiled;

    V3SourceCompilation inner = v3_compile_source_evidence(
        program, providers, arena, environment, expression->expr.elems[2],
        limits, error, error_size, depth + 1u);
    if (inner.status != V3_SOURCE_EVIDENCE)
        return inner;
    if (v3_head_is(inner.evidence, "V3HeldEvidence"))
        return inner;
    if (!v3_head_is(inner.evidence, "V3RuntimeEvidence") ||
        inner.evidence->expr.len != 5u) {
        return compiled;
    }

    const Atom *result = inner.evidence->expr.elems[2];
    if (v3_symbol_is(result, "V3EmptyResult"))
        return inner;
    const Atom *actual = v3_runtime_evidence_actual(inner.evidence);
    if (actual) {
        Atom *overlap_query = v3_relation2(
            arena, "V3MayOverlap", actual, ascribed_type);
        V3GroundDecision overlap = v3_run_ground(
            program, providers, overlap_query, limits,
            &compiled.search_outcome, error, error_size);
        if (overlap == V3_GROUND_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        if (overlap == V3_GROUND_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3MayOverlap";
            return compiled;
        }
        if (overlap == V3_GROUND_NO) {
            compiled.status = V3_SOURCE_SHAPE_CONFLICT;
            compiled.actual = actual;
            compiled.required = ascribed_type;
            compiled.relation = "V3MayOverlap";
            return compiled;
        }
    } else if (!v3_symbol_is(result, "V3UnknownResult")) {
        return compiled;
    }

    const Atom *card = inner.evidence->expr.elems[3];
    Atom *evidence = card && card->kind == ATOM_SYMBOL
        ? v3_runtime_evidence_for_type(
            arena, ascribed_type, atom_name_cstr((Atom *)card))
        : NULL;
    compiled.status = evidence ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
    compiled.evidence = evidence;
    return compiled;
}

static V3SourceCompilation v3_compile_brand(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size, uint32_t depth) {
    V3SourceCompilation compiled = {0};
    if (!v3_head_is(expression, "brand") || expression->expr.len != 3u ||
        expression->expr.elems[1]->kind != ATOM_SYMBOL) {
        return compiled;
    }

    Atom *wire_type = cetta_petta_type_fact_wire_type_v1(
        arena, expression->expr.elems[1]);
    Atom *branded_type = NULL;
    V3UniqueDecision elaboration = v3_elaborate_wire_type(
        program, providers, arena, wire_type, limits, &branded_type,
        &compiled.search_outcome, error, error_size);
    if (elaboration == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (elaboration == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3ElaborateType";
        return compiled;
    }
    if (elaboration != V3_UNIQUE_ONE ||
        !v3_head_is(branded_type, "V3Newtype") ||
        branded_type->expr.len != 3u) {
        return compiled;
    }

    V3SourceCompilation payload = v3_compile_source_evidence(
        program, providers, arena, environment,
        expression->expr.elems[2], limits,
        error, error_size, depth + 1u);
    if (payload.status != V3_SOURCE_EVIDENCE)
        return payload;

    const Atom *representation = branded_type->expr.elems[2];
    const Atom *actual = v3_runtime_evidence_actual(payload.evidence);
    const Atom *card = NULL;
    if (actual) {
        if (!v3_head_is(payload.evidence, "V3RuntimeEvidence") ||
            payload.evidence->expr.len != 5u) {
            return compiled;
        }
        card = payload.evidence->expr.elems[3];
    } else if (v3_head_is(payload.evidence, "V3HeldEvidence") &&
               payload.evidence->expr.len == 4u) {
        /* A quote is a value constructed now; the held grade describes what
         * a later eval may do.  Its immediate representation is PeTTa Atom,
         * whose native elaboration is gradual rather than a false exact type. */
        actual = atom_symbol(arena, "V3Unknown");
        card = atom_symbol(arena, "V3Det");
    } else {
        return compiled;
    }

    Atom *shape_query = v3_relation2(
        arena, "V3Consistent", actual, representation);
    V3GroundDecision shape = v3_run_ground(
        program, providers, shape_query, limits,
        &compiled.search_outcome, error, error_size);
    if (shape == V3_GROUND_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (shape == V3_GROUND_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3Consistent";
        return compiled;
    }
    if (shape == V3_GROUND_NO) {
        compiled.status = V3_SOURCE_SHAPE_CONFLICT;
        compiled.actual = actual;
        compiled.required = representation;
        compiled.relation = "V3Consistent";
        return compiled;
    }

    compiled.evidence = card && card->kind == ATOM_SYMBOL
        ? v3_runtime_evidence_for_type(
            arena, branded_type, atom_name_cstr((Atom *)card))
        : NULL;
    compiled.status = compiled.evidence
        ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
    return compiled;
}

static V3SourceCompilation v3_compile_identity_bindings(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size, uint32_t depth) {
    V3SourceCompilation compiled = {0};
    const Atom *binders[64] = {0};
    Atom *sources[64] = {0};
    size_t count = 0u;
    const Atom *body = NULL;
    bool chain = false;

    if ((v3_head_is(expression, "let") ||
         v3_head_is(expression, "chain")) &&
        expression->expr.len == 4u) {
        const Atom *binder = v3_head_is(expression, "let")
            ? expression->expr.elems[1] : expression->expr.elems[2];
        const Atom *value = v3_head_is(expression, "let")
            ? expression->expr.elems[2] : expression->expr.elems[1];
        if (!binder || binder->kind != ATOM_VAR)
            return compiled;
        V3SourceCompilation value_compiled = v3_compile_source_evidence(
            program, providers, arena, environment, value, limits,
            error, error_size, depth + 1u);
        if (value_compiled.status != V3_SOURCE_EVIDENCE)
            return value_compiled;
        sources[0] = v3_expr1(
            arena, "V3ValueSource", value_compiled.evidence);
        binders[0] = binder;
        count = 1u;
        body = expression->expr.elems[3];
        chain = v3_head_is(expression, "chain");
    } else if (v3_head_is(expression, "let*") &&
               expression->expr.len == 3u &&
               expression->expr.elems[1]->kind == ATOM_EXPR) {
        const Atom *bindings = expression->expr.elems[1];
        if (bindings->expr.len > 64u)
            return compiled;
        for (CettaExprIndex position = 0u;
             position < bindings->expr.len; position++) {
            const Atom *binding = bindings->expr.elems[position];
            if (!binding || binding->kind != ATOM_EXPR ||
                binding->expr.len != 2u ||
                binding->expr.elems[0]->kind != ATOM_VAR) {
                return compiled;
            }
            const Atom *value = binding->expr.elems[1];
            Atom *source = NULL;
            if (value->kind == ATOM_VAR) {
                size_t index = 0u;
                bool found = false;
                for (size_t prior = count; prior > 0u; prior--) {
                    if (binders[prior - 1u]->var_id == value->var_id) {
                        index = count - prior;
                        found = true;
                        break;
                    }
                }
                Atom *index_term = found
                    ? v3_debruijn_index(arena, index) : NULL;
                source = index_term
                    ? v3_expr1(arena, "V3AliasSource", index_term) : NULL;
                if (!found) {
                    const Atom *parameter_type =
                        v3_source_environment_lookup(environment, value);
                    Atom *parameter_evidence = parameter_type
                        ? v3_runtime_evidence_for_type(
                            arena, parameter_type, "V3Det") : NULL;
                    source = parameter_evidence
                        ? v3_expr1(
                            arena, "V3ValueSource", parameter_evidence)
                        : NULL;
                }
            } else {
                V3SourceCompilation value_compiled =
                    v3_compile_source_evidence(
                        program, providers, arena, environment, value, limits,
                        error, error_size, depth + 1u);
                if (value_compiled.status != V3_SOURCE_EVIDENCE)
                    return value_compiled;
                source = v3_expr1(
                    arena, "V3ValueSource", value_compiled.evidence);
            }
            if (!source)
                return compiled;
            binders[count] = binding->expr.elems[0];
            sources[count] = source;
            count++;
        }
        body = expression->expr.elems[2];
    } else {
        return compiled;
    }

    if (!body || body->kind != ATOM_VAR)
        return compiled;
    size_t body_index = 0u;
    bool body_found = false;
    for (size_t prior = count; prior > 0u; prior--) {
        if (binders[prior - 1u]->var_id == body->var_id) {
            body_index = count - prior;
            body_found = true;
            break;
        }
    }
    if (!body_found) {
        const Atom *parameter_type =
            v3_source_environment_lookup(environment, body);
        compiled.evidence = parameter_type
            ? v3_runtime_evidence_for_type(arena, parameter_type, "V3Det")
            : NULL;
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_NONE;
        return compiled;
    }

    Atom *binding_terms = atom_symbol(arena, "V3BindingsNil");
    for (size_t position = count; binding_terms && position > 0u; position--)
        binding_terms = v3_expr2(
            arena, "V3BindingsCons", sources[position - 1u], binding_terms);
    Atom *empty_environment = atom_symbol(arena, "V3EnvNil");
    Atom *result_environment = atom_var(arena, "v3-binding-environment");
    Atom *apply_query = binding_terms && empty_environment && result_environment
        ? v3_relation3(
            arena, "V3ApplyBindings", empty_environment,
            binding_terms, result_environment)
        : NULL;
    Atom *apply_answer = NULL;
    V3UniqueDecision applied = v3_run_unique(
        program, providers, arena, apply_query, limits, &apply_answer,
        &compiled.search_outcome, error, error_size);
    if (applied == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (applied == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3ApplyBindings";
        return compiled;
    }
    if (applied != V3_UNIQUE_ONE ||
        !v3_head_is(apply_answer, "V3ApplyBindings") ||
        apply_answer->expr.len != 4u) {
        return compiled;
    }

    Atom *index_term = v3_debruijn_index(arena, body_index);
    Atom *body_evidence = atom_var(arena, "v3-bound-body-evidence");
    Atom *lookup_query = index_term && body_evidence
        ? v3_relation3(
            arena, "V3EnvLookup", apply_answer->expr.elems[3],
            index_term, body_evidence)
        : NULL;
    Atom *lookup_answer = NULL;
    V3UniqueDecision looked_up = v3_run_unique(
        program, providers, arena, lookup_query, limits, &lookup_answer,
        &compiled.search_outcome, error, error_size);
    if (looked_up == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (looked_up == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3EnvLookup";
        return compiled;
    }
    if (looked_up != V3_UNIQUE_ONE ||
        !v3_head_is(lookup_answer, "V3EnvLookup") ||
        lookup_answer->expr.len != 4u) {
        return compiled;
    }
    Atom *evidence = lookup_answer->expr.elems[3];
    if (chain) {
        Atom *card = evidence->expr.len == 5u
            ? evidence->expr.elems[3] : NULL;
        Atom *semidet = atom_symbol(arena, "V3Semidet");
        Atom *result_card = atom_var(arena, "v3-chain-card");
        Atom *card_query = card && semidet && result_card
            ? v3_relation3(
                arena, "V3CardSeq", card, semidet, result_card)
            : NULL;
        Atom *card_answer = NULL;
        V3UniqueDecision sequenced = v3_run_unique(
            program, providers, arena, card_query, limits, &card_answer,
            &compiled.search_outcome, error, error_size);
        if (sequenced != V3_UNIQUE_ONE ||
            !v3_head_is(card_answer, "V3CardSeq") ||
            card_answer->expr.len != 4u) {
            compiled.status = sequenced == V3_UNIQUE_INCOMPLETE
                ? V3_SOURCE_INCOMPLETE : V3_SOURCE_FAULT;
            compiled.relation = "V3CardSeq";
            return compiled;
        }
        evidence = v3_runtime_evidence_with_card(
            arena, evidence, card_answer->expr.elems[3]);
    }
    compiled.status = evidence ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
    compiled.evidence = evidence;
    return compiled;
}

static V3SourceCompilation v3_merge_source_branches(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, Atom *left, Atom *right, CettaGsltHornLimits limits,
    char *error, size_t error_size) {
    V3SourceCompilation compiled = {0};
    Atom *left_some = v3_expr1(arena, "V3SomeEvidence", left);
    Atom *right_some = v3_expr1(arena, "V3SomeEvidence", right);
    Atom *merged = atom_var(arena, "v3-merged-evidence");
    Atom *query = left_some && right_some && merged
        ? v3_relation3(
            arena, "V3BranchEvidence", left_some, right_some, merged)
        : NULL;
    Atom *answer = NULL;
    V3UniqueDecision result = v3_run_unique(
        program, providers, arena, query, limits, &answer,
        &compiled.search_outcome, error, error_size);
    if (result == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
    } else if (result == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3BranchEvidence";
    } else if (result == V3_UNIQUE_ONE &&
               v3_head_is(answer, "V3BranchEvidence") &&
               answer->expr.len == 4u) {
        compiled.status = V3_SOURCE_EVIDENCE;
        compiled.evidence = answer->expr.elems[3];
    } else if (result == V3_UNIQUE_NONE) {
        const Atom *left_type = v3_runtime_evidence_actual(left);
        const Atom *right_type = v3_runtime_evidence_actual(right);
        if (!left_type || !right_type ||
            atom_has_vars((Atom *)left_type) ||
            atom_has_vars((Atom *)right_type) ||
            atom_eq((Atom *)left_type, (Atom *)right_type)) {
            return compiled;
        }

        /* The authored Horn rule covers the equal branch of joinPrecise.
         * Its distinct branch is a ground mechanism: retain both positive
         * branch types as a union while composing their grades and facts
         * through the generated relations. */
        Atom *card = atom_var(arena, "v3-distinct-branch-card");
        Atom *card_query = card ? v3_relation3(
            arena, "V3CardSeq", left->expr.elems[3],
            right->expr.elems[3], card) : NULL;
        Atom *card_answer = NULL;
        V3UniqueDecision card_result = v3_run_unique(
            program, providers, arena, card_query, limits, &card_answer,
            &compiled.search_outcome, error, error_size);
        if (card_result == V3_UNIQUE_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        if (card_result == V3_UNIQUE_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3CardSeq";
            return compiled;
        }
        if (card_result != V3_UNIQUE_ONE ||
            !v3_head_is(card_answer, "V3CardSeq") ||
            card_answer->expr.len != 4u) {
            return compiled;
        }

        Atom *facts = atom_var(arena, "v3-distinct-branch-facts");
        Atom *facts_query = facts ? v3_relation3(
            arena, "V3FactsMeet", left->expr.elems[4],
            right->expr.elems[4], facts) : NULL;
        Atom *facts_answer = NULL;
        V3UniqueDecision facts_result = v3_run_unique(
            program, providers, arena, facts_query, limits, &facts_answer,
            &compiled.search_outcome, error, error_size);
        if (facts_result == V3_UNIQUE_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        if (facts_result == V3_UNIQUE_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3FactsMeet";
            return compiled;
        }
        if (facts_result != V3_UNIQUE_ONE ||
            !v3_head_is(facts_answer, "V3FactsMeet") ||
            facts_answer->expr.len != 4u) {
            return compiled;
        }

        Atom *joined_type = v3_expr2(
            arena, "V3Union", left_type, right_type);
        compiled.evidence = joined_type ? v3_runtime_evidence_from_parts(
            arena, joined_type, card_answer->expr.elems[3],
            facts_answer->expr.elems[3]) : NULL;
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
    }
    return compiled;
}

static Atom *v3_gradual_runtime_evidence(Arena *arena) {
    Atom **facts_elements = arena_alloc(arena, sizeof(*facts_elements) * 4u);
    Atom **evidence_elements = arena_alloc(
        arena, sizeof(*evidence_elements) * 5u);
    if (!facts_elements || !evidence_elements)
        return NULL;
    facts_elements[0] = atom_symbol(arena, "V3Facts");
    facts_elements[1] = atom_symbol(arena, "V3No");
    facts_elements[2] = atom_symbol(arena, "V3No");
    facts_elements[3] = atom_symbol(arena, "V3No");
    Atom *facts = atom_expr(arena, facts_elements, 4u);
    evidence_elements[0] = atom_symbol(arena, "V3RuntimeEvidence");
    evidence_elements[1] = atom_symbol(arena, "V3StageEvaluated");
    evidence_elements[2] = v3_expr1(
        arena, "V3TypedResult", atom_symbol(arena, "V3Unknown"));
    evidence_elements[3] = atom_symbol(arena, "V3Det");
    evidence_elements[4] = facts;
    if (!facts || !evidence_elements[0] || !evidence_elements[1] ||
        !evidence_elements[2] || !evidence_elements[3]) {
        return NULL;
    }
    return atom_expr(arena, evidence_elements, 5u);
}

static V3SourceCompilation v3_compile_selection_call(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size, uint32_t depth) {
    V3SourceCompilation compiled = {0};
    if (!expression || expression->kind != ATOM_EXPR ||
        expression->expr.len != 3u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL ||
        (expression->expr.elems[2]->kind != ATOM_SYMBOL &&
         expression->expr.elems[2]->kind != ATOM_VAR)) {
        return compiled;
    }
    Atom *selector_signature = NULL;
    V3UniqueDecision selector = v3_source_callable_signature(
        program, providers, arena, environment,
        expression->expr.elems[0], limits,
        &selector_signature, &compiled.search_outcome, error, error_size);
    if (selector == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (selector == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "EnvDeclaredList";
        return compiled;
    }
    if (selector != V3_UNIQUE_ONE ||
        !v3_head_is(selector_signature, "V3Arrow") ||
        selector_signature->expr.len != 4u ||
        !v3_head_is(selector_signature->expr.elems[1], "V3ArgsCons") ||
        selector_signature->expr.elems[1]->expr.len != 3u) {
        return compiled;
    }
    const Atom *required = selector_signature->expr.elems[1]->expr.elems[1];
    const Atom *result_type = selector_signature->expr.elems[3];

    V3SourceCompilation input = v3_compile_source_evidence(
        program, providers, arena, environment,
        expression->expr.elems[1], limits,
        error, error_size, depth + 1u);
    if (input.status != V3_SOURCE_EVIDENCE)
        return input;
    const Atom *actual = v3_runtime_evidence_actual(input.evidence);
    if (!actual)
        return compiled;

    Atom *shape_query = v3_relation2(
        arena, "V3Consistent", actual, required);
    V3GroundDecision shape = v3_run_ground(
        program, providers, shape_query, limits,
        &compiled.search_outcome, error, error_size);
    if (shape == V3_GROUND_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (shape == V3_GROUND_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3Consistent";
        return compiled;
    }
    if (shape == V3_GROUND_NO) {
        compiled.status = V3_SOURCE_SHAPE_CONFLICT;
        compiled.actual = actual;
        compiled.required = required;
        compiled.relation = "V3Consistent";
        return compiled;
    }

    const char *requirement_name = NULL;
    if (v3_head_is(required, "V3List")) {
        requirement_name = "V3RequireProperList";
    } else if (v3_head_is(required, "V3Prim") &&
               required->expr.len == 2u &&
               v3_symbol_is(required->expr.elems[1], "V3Bool")) {
        requirement_name = "V3RequireBool";
    } else {
        requirement_name = "V3RequireNonemptyExpression";
    }

    Atom *continuation_signature = NULL;
    V3UniqueDecision continuation = v3_source_callable_signature(
        program, providers, arena, environment,
        expression->expr.elems[2], limits,
        &continuation_signature, &compiled.search_outcome, error, error_size);
    if (continuation == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (continuation == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "EnvDeclaredList";
        return compiled;
    }
    if (continuation != V3_UNIQUE_ONE ||
        !v3_head_is(continuation_signature, "V3Arrow") ||
        continuation_signature->expr.len != 4u ||
        !v3_head_is(continuation_signature->expr.elems[2], "V3Grade") ||
        continuation_signature->expr.elems[2]->expr.len != 2u) {
        return compiled;
    }
    const Atom *continuation_card =
        continuation_signature->expr.elems[2]->expr.elems[1];
    Atom *requirement = atom_symbol(arena, requirement_name);
    Atom *result_card = atom_var(arena, "v3-selection-result-card");
    Atom *selection_query = requirement && result_card
        ? v3_relation4(
            arena, "V3SelectionEvidence", input.evidence, requirement,
            continuation_card, result_card)
        : NULL;
    Atom *selection_answer = NULL;
    V3UniqueDecision selection_result = v3_run_unique(
        program, providers, arena, selection_query, limits,
        &selection_answer, &compiled.search_outcome, error, error_size);
    if (selection_result == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (selection_result == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3SelectionEvidence";
        return compiled;
    }
    if (selection_result != V3_UNIQUE_ONE ||
        !v3_head_is(selection_answer, "V3SelectionEvidence") ||
        selection_answer->expr.len != 5u) {
        return compiled;
    }
    Atom *evidence = v3_runtime_evidence_for_type(
        arena, result_type,
        atom_name_cstr(selection_answer->expr.elems[4]));
    compiled.status = evidence ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
    compiled.evidence = evidence;
    return compiled;
}

static V3UniqueDecision v3_sequence_cards(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const Atom *left, const Atom *right,
    CettaGsltHornLimits limits, Atom **card,
    CettaGsltHornOutcome *search_outcome,
    char *error, size_t error_size) {
    if (card)
        *card = NULL;
    Atom *result = atom_var(arena, "v3-application-card");
    Atom *query = result
        ? v3_relation3(arena, "V3CardSeq", left, right, result) : NULL;
    Atom *answer = NULL;
    V3UniqueDecision decision = v3_run_unique(
        program, providers, arena, query, limits, &answer,
        search_outcome, error, error_size);
    if (decision == V3_UNIQUE_ONE &&
        v3_head_is(answer, "V3CardSeq") && answer->expr.len == 4u) {
        if (card)
            *card = answer->expr.elems[3];
    } else if (decision == V3_UNIQUE_ONE) {
        decision = V3_UNIQUE_NONE;
    }
    return decision;
}

static V3UniqueDecision v3_concretize_mode(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const Atom *mode, CettaGsltHornLimits limits,
    Atom **card, CettaGsltHornOutcome *search_outcome,
    char *error, size_t error_size) {
    if (card)
        *card = NULL;
    Atom *result = atom_var(arena, "v3-concrete-mode-card");
    Atom *query = result
        ? v3_relation2(arena, "V3ConcretizeMode", mode, result) : NULL;
    Atom *answer = NULL;
    V3UniqueDecision decision = v3_run_unique(
        program, providers, arena, query, limits, &answer,
        search_outcome, error, error_size);
    if (decision == V3_UNIQUE_ONE &&
        v3_head_is(answer, "V3ConcretizeMode") &&
        answer->expr.len == 3u) {
        if (card)
            *card = answer->expr.elems[2];
    } else if (decision == V3_UNIQUE_ONE) {
        decision = V3_UNIQUE_NONE;
    }
    return decision;
}

static V3SourceCompilation v3_compose_let_evidence(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const Atom *value, const Atom *body,
    CettaGsltHornLimits limits, char *error, size_t error_size) {
    V3SourceCompilation compiled = {0};
    Atom *result = atom_var(arena, "v3-let-result-evidence");
    Atom *query = result
        ? v3_relation3(arena, "V3LetEvidence", value, body, result) : NULL;
    Atom *answer = NULL;
    V3UniqueDecision decision = v3_run_unique(
        program, providers, arena, query, limits, &answer,
        &compiled.search_outcome, error, error_size);
    if (decision == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
    } else if (decision == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "V3LetEvidence";
    } else if (decision == V3_UNIQUE_ONE &&
               v3_head_is(answer, "V3LetEvidence") &&
               answer->expr.len == 4u) {
        compiled.status = V3_SOURCE_EVIDENCE;
        compiled.evidence = answer->expr.elems[3];
    }
    return compiled;
}

static bool v3_push_lexical_evidence(
    V3SourceEnvironment *environment, const Atom *variable,
    const Atom *type, const Atom *evidence,
    char *error, size_t error_size) {
    if (!environment || !variable || variable->kind != ATOM_VAR || !type ||
        !evidence) {
        return false;
    }
    if (environment->count >=
        sizeof(environment->bindings) / sizeof(environment->bindings[0])) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "v3 lexical environment exceeds 128 bindings");
        return false;
    }
    environment->bindings[environment->count++] = (V3SourceBinding){
        .variable = variable->var_id,
        .type = type,
        .evidence = evidence,
    };
    return true;
}

/* Elaborate lexical variable bindings into a reusable typed environment.
 * Values are analyzed exactly once, later bindings and the body consume that
 * evidence, and V3LetEvidence composes their grades at the calculus boundary. */
static V3SourceCompilation v3_compile_contextual_variable_bindings(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size, uint32_t depth) {
    V3SourceCompilation compiled = {0};
    const Atom *binders[64] = {0};
    const Atom *values[64] = {0};
    const Atom *value_evidence[64] = {0};
    size_t count = 0u;
    const Atom *body = NULL;

    if (v3_head_is(expression, "let") && expression->expr.len == 4u &&
        expression->expr.elems[1]->kind == ATOM_VAR) {
        binders[0] = expression->expr.elems[1];
        values[0] = expression->expr.elems[2];
        count = 1u;
        body = expression->expr.elems[3];
    } else if (v3_head_is(expression, "let*") &&
               expression->expr.len == 3u &&
               expression->expr.elems[1]->kind == ATOM_EXPR) {
        const Atom *bindings = expression->expr.elems[1];
        if (bindings->expr.len > 64u)
            return compiled;
        for (CettaExprIndex index = 0u;
             index < bindings->expr.len; index++) {
            const Atom *binding = bindings->expr.elems[index];
            if (!binding || binding->kind != ATOM_EXPR ||
                binding->expr.len != 2u ||
                binding->expr.elems[0]->kind != ATOM_VAR) {
                return compiled;
            }
            binders[count] = binding->expr.elems[0];
            values[count] = binding->expr.elems[1];
            count++;
        }
        body = expression->expr.elems[2];
    } else {
        return compiled;
    }

    V3SourceEnvironment local = {0};
    if (environment)
        local = *environment;
    for (size_t index = 0u; index < count; index++) {
        V3SourceCompilation value = v3_compile_source_evidence(
            program, providers, arena, &local, values[index], limits,
            error, error_size, depth + 1u);
        if (value.status != V3_SOURCE_EVIDENCE)
            return value;
        const Atom *actual = v3_runtime_evidence_actual(value.evidence);
        if (!actual)
            return compiled;
        if (!v3_push_lexical_evidence(
                &local, binders[index], actual, value.evidence,
                error, error_size)) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        value_evidence[index] = value.evidence;
    }

    V3SourceCompilation body_compiled = v3_compile_source_evidence(
        program, providers, arena, &local, body, limits,
        error, error_size, depth + 1u);
    if (body_compiled.status != V3_SOURCE_EVIDENCE)
        return body_compiled;
    compiled = body_compiled;
    for (size_t index = count; index > 0u; index--) {
        compiled = v3_compose_let_evidence(
            program, providers, arena, value_evidence[index - 1u],
            compiled.evidence, limits, error, error_size);
        if (compiled.status != V3_SOURCE_EVIDENCE)
            return compiled;
    }
    return compiled;
}

static V3SourceCompilation v3_compile_application_call(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size, uint32_t depth) {
    V3SourceCompilation compiled = {0};
    if (!expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        (expression->expr.elems[0]->kind != ATOM_SYMBOL &&
         expression->expr.elems[0]->kind != ATOM_VAR)) {
        return compiled;
    }
    Atom *signature = NULL;
    V3UniqueDecision signature_result = v3_source_callable_signature(
        program, providers, arena, environment,
        expression->expr.elems[0], limits,
        &signature, &compiled.search_outcome, error, error_size);
    if (signature_result == V3_UNIQUE_FAULT) {
        compiled.status = V3_SOURCE_FAULT;
        return compiled;
    }
    if (signature_result == V3_UNIQUE_INCOMPLETE) {
        compiled.status = V3_SOURCE_INCOMPLETE;
        compiled.relation = "EnvDeclaredList";
        return compiled;
    }
    if (signature_result != V3_UNIQUE_ONE ||
        !v3_head_is(signature, "V3Arrow") ||
        signature->expr.len != 4u) {
        return compiled;
    }

    const Atom *mode = signature->expr.elems[2];
    Atom *aggregate_card = NULL;
    if (v3_head_is(mode, "V3Grade") && mode->expr.len == 2u) {
        aggregate_card = atom_deep_copy(arena, mode->expr.elems[1]);
    } else if (v3_symbol_is(mode, "V3Plain")) {
        /* The annotation itself promises no upper bound.  Prefer a grade
         * derived from the live equations; if none is available, nondet is
         * the conservative executable grade admitted by a plain demand. */
        V3SourceCompilation concrete = v3_query_source_evidence(
            program, providers, arena, expression, limits,
            error, error_size);
        if (concrete.status == V3_SOURCE_FAULT ||
            concrete.status == V3_SOURCE_INCOMPLETE) {
            return concrete;
        }
        if (concrete.status == V3_SOURCE_EVIDENCE &&
            v3_head_is(concrete.evidence, "V3RuntimeEvidence") &&
            concrete.evidence->expr.len == 5u) {
            aggregate_card = atom_deep_copy(
                arena, concrete.evidence->expr.elems[3]);
        } else {
            aggregate_card = atom_symbol(arena, "V3Nondet");
        }
    } else if (v3_head_is(mode, "V3Effect") && mode->expr.len == 2u) {
        /* A direct higher-order parameter can expose a concrete effect grade,
         * but that does not prove the combinator's own pattern coverage.  The
         * typed signature therefore keeps its named slot while execution uses
         * the conservative upper bound until a coverage license is present. */
        V3UniqueDecision concretized = v3_concretize_mode(
            program, providers, arena, mode, limits, &aggregate_card,
            &compiled.search_outcome, error, error_size);
        if (concretized == V3_UNIQUE_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        if (concretized == V3_UNIQUE_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3ConcretizeMode";
            return compiled;
        }
        if (concretized != V3_UNIQUE_ONE || !aggregate_card)
            return compiled;
    } else {
        return compiled;
    }
    const Atom *domains = signature->expr.elems[1];
    for (CettaExprIndex index = 1u;
         index < expression->expr.len; index++) {
        if (!v3_head_is(domains, "V3ArgsCons") ||
            domains->expr.len != 3u) {
            return (V3SourceCompilation){0};
        }
        const Atom *required = domains->expr.elems[1];
        V3SourceCompilation argument = v3_compile_source_evidence(
            program, providers, arena, environment,
            expression->expr.elems[index], limits,
            error, error_size, depth + 1u);
        if (argument.status != V3_SOURCE_EVIDENCE)
            return argument;
        const Atom *actual = v3_runtime_evidence_actual(argument.evidence);
        const Atom *argument_card = NULL;
        if (actual &&
            v3_head_is(argument.evidence, "V3RuntimeEvidence") &&
            argument.evidence->expr.len == 5u) {
            argument_card = argument.evidence->expr.elems[3];
        } else if (v3_head_is(argument.evidence, "V3HeldEvidence") &&
                   argument.evidence->expr.len == 4u) {
            /* Passing quoted code constructs one Atom now.  The grade inside
             * V3HeldEvidence is latent until eval and must not be charged to
             * the enclosing call. */
            actual = atom_symbol(arena, "V3Unknown");
            argument_card = atom_symbol(arena, "V3Det");
        } else {
            return (V3SourceCompilation){0};
        }
        Atom *shape_query = v3_relation2(
            arena, "V3Consistent", actual, required);
        V3GroundDecision shape = v3_run_ground(
            program, providers, shape_query, limits,
            &compiled.search_outcome, error, error_size);
        if (shape == V3_GROUND_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        if (shape == V3_GROUND_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3Consistent";
            return compiled;
        }
        if (shape == V3_GROUND_NO) {
            compiled.status = V3_SOURCE_SHAPE_CONFLICT;
            compiled.actual = actual;
            compiled.required = required;
            compiled.relation = "V3Consistent";
            return compiled;
        }
        Atom *sequenced = NULL;
        V3UniqueDecision sequence = v3_sequence_cards(
            program, providers, arena,
            argument_card, aggregate_card,
            limits, &sequenced, &compiled.search_outcome,
            error, error_size);
        if (sequence == V3_UNIQUE_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        if (sequence == V3_UNIQUE_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3CardSeq";
            return compiled;
        }
        if (sequence != V3_UNIQUE_ONE || !sequenced)
            return (V3SourceCompilation){0};
        aggregate_card = sequenced;
        domains = domains->expr.elems[2];
    }
    if (!v3_symbol_is(domains, "V3ArgsNil"))
        return (V3SourceCompilation){0};
    Atom *evidence = v3_runtime_evidence_for_type(
        arena, signature->expr.elems[3],
        atom_name_cstr(aggregate_card));
    compiled.status = evidence ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
    compiled.evidence = evidence;
    return compiled;
}

static V3SourceCompilation v3_compile_source_evidence(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *arena, const V3SourceEnvironment *environment,
    const Atom *expression, CettaGsltHornLimits limits,
    char *error, size_t error_size, uint32_t depth) {
    V3SourceCompilation compiled = {0};
    if (!program || !providers || !arena || !expression || depth > 128u)
        return compiled;

    if (expression->kind == ATOM_VAR) {
        const Atom *bound_evidence = v3_source_environment_evidence(
            environment, expression);
        if (bound_evidence) {
            compiled.status = V3_SOURCE_EVIDENCE;
            compiled.evidence = (Atom *)bound_evidence;
            return compiled;
        }
        const Atom *parameter_type = v3_source_environment_lookup(
            environment, expression);
        compiled.evidence = parameter_type
            ? v3_runtime_evidence_for_type(
                arena, parameter_type, "V3Det")
            : v3_gradual_runtime_evidence(arena);
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
        return compiled;
    }
    if (expression->kind != ATOM_EXPR) {
        V3SourceCompilation queried = v3_query_source_evidence(
            program, providers, arena, expression, limits,
            error, error_size);
        if (queried.status != V3_SOURCE_NONE ||
            expression->kind != ATOM_SYMBOL) {
            return queried;
        }
        Atom *symbol_type = v3_expr1(
            arena, "V3Prim", atom_symbol(arena, "V3Sym"));
        compiled.evidence = symbol_type
            ? v3_runtime_evidence_for_type(arena, symbol_type, "V3Det")
            : NULL;
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
        return compiled;
    }

    if (expression->expr.len == 0u) {
        Atom *unknown = atom_symbol(arena, "V3Unknown");
        Atom *list_type = unknown
            ? v3_expr1(arena, "V3List", unknown) : NULL;
        compiled.evidence = list_type
            ? v3_runtime_evidence_for_type_and_facts(
                arena, list_type, "V3Det", "V3Yes", "V3No", "V3No")
            : NULL;
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
        return compiled;
    }

    if (v3_head_is(expression, "the"))
        return v3_compile_ascription(
            program, providers, arena, environment, expression, limits,
            error, error_size, depth);

    if (v3_head_is(expression, "brand"))
        return v3_compile_brand(
            program, providers, arena, environment, expression, limits,
            error, error_size, depth);

    if (v3_head_is(expression, "let") ||
        v3_head_is(expression, "let*")) {
        V3SourceCompilation contextual =
            v3_compile_contextual_variable_bindings(
                program, providers, arena, environment, expression, limits,
                error, error_size, depth);
        if (contextual.status != V3_SOURCE_NONE)
            return contextual;
    }

    if (v3_head_is(expression, "let") ||
        v3_head_is(expression, "let*") ||
        v3_head_is(expression, "chain")) {
        return v3_compile_identity_bindings(
            program, providers, arena, environment, expression, limits,
            error, error_size, depth);
    }

    if (v3_head_is(expression, "cons") && expression->expr.len == 3u) {
        const Atom *tail = expression->expr.elems[2];
        V3SourceCompilation head = v3_compile_source_evidence(
            program, providers, arena, environment,
            expression->expr.elems[1], limits,
            error, error_size, depth + 1u);
        if (head.status != V3_SOURCE_EVIDENCE)
            return head;
        const Atom *element_type = v3_runtime_evidence_actual(head.evidence);
        if (!element_type)
            return compiled;
        const Atom *card = head.evidence->expr.elems[3];
        const Atom *result_element = element_type;

        if (!tail || tail->kind != ATOM_EXPR || tail->expr.len != 0u) {
            V3SourceCompilation tail_compiled = v3_compile_source_evidence(
                program, providers, arena, environment, tail, limits,
                error, error_size, depth + 1u);
            if (tail_compiled.status != V3_SOURCE_EVIDENCE)
                return tail_compiled;
            const Atom *tail_type =
                v3_runtime_evidence_actual(tail_compiled.evidence);
            if (!v3_head_is(tail_type, "V3List") ||
                tail_type->expr.len != 2u) {
                compiled.status = V3_SOURCE_SHAPE_CONFLICT;
                compiled.actual = tail_type;
                compiled.required = v3_expr1(
                    arena, "V3List", (Atom *)element_type);
                compiled.relation = "V3Consistent";
                return compiled;
            }
            const Atom *tail_element = tail_type->expr.elems[1];
            if (!atom_eq((Atom *)element_type, (Atom *)tail_element)) {
                result_element = v3_expr2(
                    arena, "V3Union", element_type, tail_element);
                if (!result_element) {
                    compiled.status = V3_SOURCE_FAULT;
                    return compiled;
                }
            }

            Atom *sequenced = NULL;
            V3UniqueDecision sequence = v3_sequence_cards(
                program, providers, arena, card,
                tail_compiled.evidence->expr.elems[3], limits,
                &sequenced, &compiled.search_outcome, error, error_size);
            if (sequence == V3_UNIQUE_FAULT) {
                compiled.status = V3_SOURCE_FAULT;
                return compiled;
            }
            if (sequence == V3_UNIQUE_INCOMPLETE) {
                compiled.status = V3_SOURCE_INCOMPLETE;
                compiled.relation = "V3CardSeq";
                return compiled;
            }
            if (sequence != V3_UNIQUE_ONE || !sequenced)
                return compiled;
            card = sequenced;
        }

        Atom *list_type = v3_expr1(
            arena, "V3List", (Atom *)result_element);
        Atom *evidence = list_type && card->kind == ATOM_SYMBOL
            ? v3_runtime_evidence_for_type_and_facts(
                arena, list_type, atom_name_cstr((Atom *)card),
                "V3Yes", "V3No", "V3No")
            : NULL;
        compiled.status = evidence ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
        compiled.evidence = evidence;
        return compiled;
    }

    if (v3_head_is(expression, "superpose") &&
        expression->expr.len == 2u &&
        expression->expr.elems[1]->kind == ATOM_EXPR &&
        expression->expr.elems[1]->expr.len > 0u) {
        const Atom *alternatives = expression->expr.elems[1];
        V3SourceCompilation first = v3_compile_source_evidence(
            program, providers, arena, environment,
            alternatives->expr.elems[0], limits,
            error, error_size, depth + 1u);
        if (first.status != V3_SOURCE_EVIDENCE)
            return first;
        const Atom *first_type = v3_runtime_evidence_actual(first.evidence);
        if (!first_type)
            return compiled;
        for (CettaExprIndex index = 1u;
             index < alternatives->expr.len; index++) {
            V3SourceCompilation next = v3_compile_source_evidence(
                program, providers, arena, environment,
                alternatives->expr.elems[index],
                limits, error, error_size, depth + 1u);
            if (next.status != V3_SOURCE_EVIDENCE)
                return next;
            const Atom *next_type = v3_runtime_evidence_actual(next.evidence);
            if (!next_type || !atom_eq((Atom *)first_type, (Atom *)next_type))
                return compiled;
        }
        Atom *nondet = atom_symbol(arena, "V3Nondet");
        compiled.evidence = v3_runtime_evidence_with_card(
            arena, first.evidence, nondet);
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
        return compiled;
    }

    /* An expression whose first child is itself an expression has no
       callable head in PeTTa.  It is inert data constructed once, while its
       element type remains gradual until an ascription or newtype supplies
       more information. */
    if (expression->expr.elems[0]->kind != ATOM_SYMBOL &&
        expression->expr.elems[0]->kind != ATOM_VAR) {
        compiled.evidence = v3_runtime_evidence_for_type(
            arena, atom_symbol(arena, "V3Unknown"), "V3Det");
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
        return compiled;
    }

    if (v3_head_is(expression, "if") && expression->expr.len == 4u) {
        V3SourceCompilation condition = v3_compile_source_evidence(
            program, providers, arena, environment,
            expression->expr.elems[1], limits,
            error, error_size, depth + 1u);
        V3SourceEnvironment then_environment = {0};
        if (environment)
            then_environment = *environment;
        const V3SourceEnvironment *then_scope = environment;
        const Atom *guard = expression->expr.elems[1];
        if (v3_head_is(guard, "is-expr") && guard->expr.len == 2u &&
            guard->expr.elems[1]->kind == ATOM_VAR) {
            const Atom *known_type = v3_source_environment_lookup(
                environment, guard->expr.elems[1]);
            Atom *guarded_type = v3_refine_expression_representation(
                arena, known_type, 0u);
            Atom *guarded_evidence = guarded_type
                ? v3_runtime_evidence_for_type_and_facts(
                    arena, guarded_type, "V3Det", "V3Yes", "V3No", "V3Yes")
                : NULL;
            if (!guarded_evidence || !v3_push_lexical_evidence(
                    &then_environment, guard->expr.elems[1], guarded_type,
                    guarded_evidence, error, error_size)) {
                compiled.status = V3_SOURCE_FAULT;
                return compiled;
            }
            then_scope = &then_environment;
        }
        V3SourceCompilation left = v3_compile_source_evidence(
            program, providers, arena, then_scope,
            expression->expr.elems[2], limits,
            error, error_size, depth + 1u);
        V3SourceCompilation right = v3_compile_source_evidence(
            program, providers, arena, environment,
            expression->expr.elems[3], limits,
            error, error_size, depth + 1u);
        if (condition.status != V3_SOURCE_EVIDENCE)
            return condition;
        if (left.status != V3_SOURCE_EVIDENCE)
            return left;
        if (right.status != V3_SOURCE_EVIDENCE)
            return right;
        V3SourceCompilation branches = v3_merge_source_branches(
            program, providers, arena, left.evidence, right.evidence,
            limits, error, error_size);
        if (branches.status != V3_SOURCE_EVIDENCE)
            return branches;
        Atom *result = atom_var(arena, "v3-conditional-result");
        Atom *query = v3_relation4(
            arena, "V3ConditionalEvidence", condition.evidence,
            v3_expr1(arena, "V3SomeEvidence", left.evidence),
            v3_expr1(arena, "V3SomeEvidence", right.evidence), result);
        Atom *answer = NULL;
        V3UniqueDecision decision = v3_run_unique(
            program, providers, arena, query, limits, &answer,
            &compiled.search_outcome, error, error_size);
        if (decision == V3_UNIQUE_ONE &&
            v3_head_is(answer, "V3ConditionalEvidence") &&
            answer->expr.len == 5u) {
            compiled.status = V3_SOURCE_EVIDENCE;
            compiled.evidence = answer->expr.elems[4];
        } else if (decision == V3_UNIQUE_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3ConditionalEvidence";
        } else if (decision == V3_UNIQUE_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
        }
        return compiled;
    }

    if ((v3_head_is(expression, "car-atom") ||
         v3_head_is(expression, "cdr-atom")) &&
        expression->expr.len == 2u) {
        V3SourceCompilation input = v3_compile_source_evidence(
            program, providers, arena, environment,
            expression->expr.elems[1], limits,
            error, error_size, depth + 1u);
        if (input.status != V3_SOURCE_EVIDENCE)
            return input;
        const Atom *actual = v3_runtime_evidence_actual(input.evidence);
        const Atom *representation = v3_pattern_representation(actual, 0u);
        if (!v3_head_is(representation, "V3List") ||
            representation->expr.len != 2u) {
            return compiled;
        }
        Atom *requirement = atom_symbol(arena, "V3RequireProperList");
        Atom *continuation = atom_symbol(arena, "V3Det");
        Atom *result_card = atom_var(arena, "v3-list-accessor-card");
        Atom *query = requirement && continuation && result_card
            ? v3_relation4(
                arena, "V3SelectionEvidence", input.evidence,
                requirement, continuation, result_card)
            : NULL;
        Atom *answer = NULL;
        V3UniqueDecision selection = v3_run_unique(
            program, providers, arena, query, limits, &answer,
            &compiled.search_outcome, error, error_size);
        if (selection == V3_UNIQUE_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        if (selection == V3_UNIQUE_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3SelectionEvidence";
            return compiled;
        }
        if (selection != V3_UNIQUE_ONE ||
            !v3_head_is(answer, "V3SelectionEvidence") ||
            answer->expr.len != 5u) {
            return compiled;
        }
        const Atom *result_type = v3_head_is(expression, "car-atom")
            ? representation->expr.elems[1] : representation;
        compiled.evidence = v3_head_is(expression, "car-atom")
            ? v3_runtime_evidence_for_type(
                arena, result_type,
                atom_name_cstr(answer->expr.elems[4]))
            : v3_runtime_evidence_for_type_and_facts(
                arena, result_type, atom_name_cstr(answer->expr.elems[4]),
                "V3Yes", "V3No", "V3Yes");
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
        return compiled;
    }

    if (v3_head_is(expression, "once") && expression->expr.len == 2u) {
        V3SourceCompilation inner = v3_compile_source_evidence(
            program, providers, arena, environment,
            expression->expr.elems[1], limits,
            error, error_size, depth + 1u);
        if (inner.status != V3_SOURCE_EVIDENCE)
            return inner;
        if (!v3_head_is(inner.evidence, "V3RuntimeEvidence") ||
            inner.evidence->expr.len != 5u ||
            inner.evidence->expr.elems[3]->kind != ATOM_SYMBOL) {
            return compiled;
        }
        Atom *result_card = atom_var(arena, "v3-once-result-card");
        Atom *query = result_card
            ? v3_relation2(
                arena, "V3OnceCard", inner.evidence->expr.elems[3],
                result_card)
            : NULL;
        Atom *answer = NULL;
        V3UniqueDecision once = v3_run_unique(
            program, providers, arena, query, limits, &answer,
            &compiled.search_outcome, error, error_size);
        if (once == V3_UNIQUE_FAULT) {
            compiled.status = V3_SOURCE_FAULT;
            return compiled;
        }
        if (once == V3_UNIQUE_INCOMPLETE) {
            compiled.status = V3_SOURCE_INCOMPLETE;
            compiled.relation = "V3OnceCard";
            return compiled;
        }
        if (once != V3_UNIQUE_ONE ||
            !v3_head_is(answer, "V3OnceCard") ||
            answer->expr.len != 3u) {
            return compiled;
        }
        compiled.evidence = v3_runtime_evidence_with_card(
            arena, inner.evidence, answer->expr.elems[2]);
        compiled.status = compiled.evidence
            ? V3_SOURCE_EVIDENCE : V3_SOURCE_FAULT;
        return compiled;
    }

    if (v3_head_is(expression, "case") && expression->expr.len == 3u &&
        expression->expr.elems[2]->kind == ATOM_EXPR &&
        expression->expr.elems[2]->expr.len > 0u) {
        V3SourceCompilation scrutinee = v3_compile_source_evidence(
            program, providers, arena, environment,
            expression->expr.elems[1], limits,
            error, error_size, depth + 1u);
        if (scrutinee.status != V3_SOURCE_EVIDENCE)
            return scrutinee;
        const Atom *scrutinee_type =
            v3_runtime_evidence_actual(scrutinee.evidence);
        if (!scrutinee_type &&
            v3_head_is(scrutinee.evidence, "V3RuntimeEvidence") &&
            scrutinee.evidence->expr.len == 5u &&
            v3_symbol_is(
                scrutinee.evidence->expr.elems[2], "V3UnknownResult")) {
            scrutinee_type = atom_symbol(arena, "V3Unknown");
        }
        if (!scrutinee_type)
            return compiled;
        const Atom *branches = expression->expr.elems[2];
        Atom *merged = NULL;
        for (CettaExprIndex index = 0u; index < branches->expr.len; index++) {
            const Atom *branch = branches->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u) {
                return compiled;
            }
            V3SourceEnvironment branch_environment = {0};
            if (environment)
                branch_environment = *environment;
            V3PatternAdmission admitted = v3_bind_source_pattern(
                program, providers, arena, &branch_environment,
                branch->expr.elems[0], scrutinee_type, limits,
                error, error_size, depth + 1u);
            if (admitted.status == V3_PATTERN_EMPTY)
                continue;
            if (admitted.status == V3_PATTERN_INCOMPLETE) {
                compiled.status = V3_SOURCE_INCOMPLETE;
                compiled.relation = admitted.relation;
                compiled.search_outcome = admitted.search_outcome;
                return compiled;
            }
            if (admitted.status == V3_PATTERN_FAULT) {
                compiled.status = V3_SOURCE_FAULT;
                compiled.relation = admitted.relation;
                compiled.search_outcome = admitted.search_outcome;
                return compiled;
            }
            V3SourceCompilation body = v3_compile_source_evidence(
                program, providers, arena, &branch_environment,
                branch->expr.elems[1], limits,
                error, error_size, depth + 1u);
            if (body.status != V3_SOURCE_EVIDENCE)
                return body;
            if (!merged) {
                merged = body.evidence;
            } else {
                V3SourceCompilation joined = v3_merge_source_branches(
                    program, providers, arena, merged, body.evidence,
                    limits, error, error_size);
                if (joined.status != V3_SOURCE_EVIDENCE)
                    return joined;
                merged = joined.evidence;
            }
        }
        if (!merged)
            return compiled;
        return v3_compose_let_evidence(
            program, providers, arena, scrutinee.evidence, merged,
            limits, error, error_size);
    }

    V3SourceCompilation selection = v3_compile_selection_call(
        program, providers, arena, environment, expression, limits,
        error, error_size, depth);
    if (selection.status != V3_SOURCE_NONE)
        return selection;
    V3SourceCompilation application = v3_compile_application_call(
        program, providers, arena, environment, expression, limits,
        error, error_size, depth);
    if (application.status != V3_SOURCE_NONE)
        return application;
    return v3_query_source_evidence(
        program, providers, arena, expression, limits,
        error, error_size);
}

bool cetta_petta_typecheck_v3_decide_evidence_v1(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    const Atom *evidence,
    const Atom *expected,
    const Atom *demand,
    CettaGsltHornLimits limits,
    CettaPettaV3EvidenceDecisionV1 *decision,
    char *error,
    size_t error_size) {
    if (decision)
        *decision = (CettaPettaV3EvidenceDecisionV1){0};
    if (!program || !providers || !evidence || !expected || !demand ||
        !decision || !v3_head_is(evidence, "V3RuntimeEvidence") ||
        evidence->expr.len != 5u) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "invalid v3 evidence decision request");
        return false;
    }
    const Atom *result_type = evidence->expr.elems[2];
    const Atom *card = evidence->expr.elems[3];
    if (v3_symbol_is(result_type, "V3UnknownResult")) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_UNDETERMINED;
        decision->relation = "v3-evidence-outcome-undetermined";
        return v3_classify_evidence_seam(
            program, providers, evidence, expected, demand, limits,
            decision, error, error_size);
    }
    if (v3_symbol_is(result_type, "V3EmptyResult")) {
        Arena scratch;
        arena_init(&scratch);
        Atom *grade = v3_grade(&scratch, card);
        Atom *query = grade
            ? v3_relation2(&scratch, "V3ModeFits", grade, demand) : NULL;
        CettaGsltHornOutcome search_outcome = CETTA_GSLT_HORN_FAULT;
        V3GroundDecision cardinality = v3_run_ground(
            program, providers, query, limits, &search_outcome,
            error, error_size);
        if (cardinality == V3_GROUND_FAULT) {
            arena_free(&scratch);
            return false;
        }
        decision->search_outcome = search_outcome;
        if (cardinality == V3_GROUND_INCOMPLETE) {
            decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
            decision->relation = "V3ModeFits";
        } else if (cardinality == V3_GROUND_NO) {
            decision->outcome = CETTA_PETTA_V3_EVIDENCE_REFUTED;
            decision->boundary = CETTA_PETTA_V3_BOUNDARY_CARDINALITY;
            decision->relation = "V3ModeFits";
            decision->actual = card;
            decision->required = demand;
        } else {
            decision->outcome = CETTA_PETTA_V3_EVIDENCE_ESTABLISHED;
            decision->relation = "v3-evidence-outcome-empty-established";
        }
        arena_free(&scratch);
        return v3_classify_evidence_seam(
            program, providers, evidence, expected, demand, limits,
            decision, error, error_size);
    }
    if (!v3_head_is(result_type, "V3TypedResult") ||
        result_type->expr.len != 2u) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "v3 evidence has an invalid result-type option");
        return false;
    }
    const Atom *actual = result_type->expr.elems[1];
    if (atom_has_vars((Atom *)actual) || atom_has_vars((Atom *)expected) ||
        atom_has_vars((Atom *)card) || atom_has_vars((Atom *)demand)) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_UNDETERMINED;
        decision->relation = "v3-evidence-outcome-undetermined";
        return v3_classify_evidence_seam(
            program, providers, evidence, expected, demand, limits,
            decision, error, error_size);
    }

    Arena scratch;
    arena_init(&scratch);
    Atom *shape_query = v3_relation2(
        &scratch, "V3Consistent", actual, expected);
    CettaGsltHornOutcome search_outcome = CETTA_GSLT_HORN_FAULT;
    V3GroundDecision shape = v3_run_ground(
        program, providers, shape_query, limits, &search_outcome,
        error, error_size);
    if (shape == V3_GROUND_FAULT) {
        arena_free(&scratch);
        return false;
    }
    if (shape == V3_GROUND_INCOMPLETE) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
        decision->relation = "V3Consistent";
        decision->search_outcome = search_outcome;
        arena_free(&scratch);
        return v3_classify_evidence_seam(
            program, providers, evidence, expected, demand, limits,
            decision, error, error_size);
    }
    if (shape == V3_GROUND_NO) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_REFUTED;
        decision->boundary = CETTA_PETTA_V3_BOUNDARY_SHAPE;
        decision->relation = "V3Consistent";
        decision->actual = actual;
        decision->required = expected;
        decision->search_outcome = search_outcome;
        arena_free(&scratch);
        return v3_classify_evidence_seam(
            program, providers, evidence, expected, demand, limits,
            decision, error, error_size);
    }

    Atom *grade = v3_grade(&scratch, card);
    Atom *cardinality_query = grade
        ? v3_relation2(&scratch, "V3ModeFits", grade, demand) : NULL;
    V3GroundDecision cardinality = v3_run_ground(
        program, providers, cardinality_query, limits, &search_outcome,
        error, error_size);
    if (cardinality == V3_GROUND_FAULT) {
        arena_free(&scratch);
        return false;
    }
    if (cardinality == V3_GROUND_INCOMPLETE) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
        decision->relation = "V3ModeFits";
        decision->search_outcome = search_outcome;
        arena_free(&scratch);
        return v3_classify_evidence_seam(
            program, providers, evidence, expected, demand, limits,
            decision, error, error_size);
    }
    if (cardinality == V3_GROUND_NO) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_REFUTED;
        decision->boundary = CETTA_PETTA_V3_BOUNDARY_CARDINALITY;
        decision->relation = "V3ModeFits";
        decision->actual = card;
        decision->required = demand;
        decision->search_outcome = search_outcome;
        arena_free(&scratch);
        return v3_classify_evidence_seam(
            program, providers, evidence, expected, demand, limits,
            decision, error, error_size);
    }
    decision->outcome = CETTA_PETTA_V3_EVIDENCE_ESTABLISHED;
    decision->relation = "v3-evidence-outcome-established";
    decision->search_outcome = search_outcome;
    arena_free(&scratch);
    return v3_classify_evidence_seam(
        program, providers, evidence, expected, demand, limits,
        decision, error, error_size);
}

static bool v3_decide_definition_internal_v1(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    const Atom *lhs,
    const Atom *subject,
    const Atom *body,
    CettaGsltHornLimits limits,
    CettaPettaV3EvidenceDecisionV1 *decision,
    char *error,
    size_t error_size) {
    if (decision)
        *decision = (CettaPettaV3EvidenceDecisionV1){0};
    if (!program || !providers || !subject || !body || !decision ||
        subject->kind != ATOM_SYMBOL) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "invalid v3 definition request");
        return false;
    }
    Arena scratch;
    arena_init(&scratch);
    CettaGsltHornOutcome search_outcome = CETTA_GSLT_HORN_FAULT;

    const Atom *elaborated_body = body;
    bool synthesized_marker = false;
    Atom *quoted_data = v3_quoted_data_marker(&scratch, body);
    if (quoted_data) {
        elaborated_body = quoted_data;
        synthesized_marker = true;
    } else if (v3_is_two_empty_superposition(body)) {
        elaborated_body = atom_symbol(&scratch, "V3SourceSuperposeTwoEmpty");
        synthesized_marker = true;
    } else if (v3_is_if_all_empty(body)) {
        elaborated_body = atom_symbol(&scratch, "V3SourceIfAllEmpty");
        synthesized_marker = true;
    } else if (v3_is_case_all_empty(body)) {
        elaborated_body = atom_symbol(&scratch, "V3SourceCaseAllEmpty");
        synthesized_marker = true;
    }
    Atom *evidence_variable = synthesized_marker
        ? atom_var(&scratch, "v3-body-evidence") : NULL;
    Atom *evidence_query = synthesized_marker ? v3_relation2(
        &scratch, "V3ExpressionEvidence", elaborated_body, evidence_variable)
        : NULL;
    Atom *evidence_answer = NULL;
    V3UniqueDecision evidence_result = synthesized_marker
        ? v3_run_unique(
            program, providers, &scratch, evidence_query, limits,
            &evidence_answer, &search_outcome, error, error_size)
        : V3_UNIQUE_NONE;
    if (evidence_result == V3_UNIQUE_FAULT) {
        arena_free(&scratch);
        return false;
    }
    if (evidence_result == V3_UNIQUE_INCOMPLETE) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
        decision->relation = "V3ExpressionEvidence";
        decision->search_outcome = search_outcome;
        arena_free(&scratch);
        return true;
    }
    const Atom *evidence = NULL;
    if (evidence_result == V3_UNIQUE_ONE &&
        v3_head_is(evidence_answer, "V3ExpressionEvidence") &&
        evidence_answer->expr.len == 3u) {
        evidence = evidence_answer->expr.elems[2];
    }

    Atom *declarations_variable = atom_var(&scratch, "v3-declarations");
    Atom *declarations_query = v3_relation2(
        &scratch, "EnvDeclaredList", subject, declarations_variable);
    Atom *declarations_answer = NULL;
    V3UniqueDecision declarations = v3_run_unique(
        program, providers, &scratch, declarations_query, limits,
        &declarations_answer, &search_outcome, error, error_size);
    if (declarations == V3_UNIQUE_FAULT) {
        arena_free(&scratch);
        return false;
    }
    if (declarations == V3_UNIQUE_INCOMPLETE) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
        decision->relation = "EnvDeclaredList";
        decision->search_outcome = search_outcome;
        arena_free(&scratch);
        return true;
    }
    if (declarations != V3_UNIQUE_ONE ||
        !v3_head_is(declarations_answer, "EnvDeclaredList") ||
        declarations_answer->expr.len != 3u) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_UNDETERMINED;
        decision->relation = "V3ExpressionEvidence";
        arena_free(&scratch);
        return true;
    }
    const Atom *list = declarations_answer->expr.elems[2];
    if (!v3_head_is(list, "DCons") || list->expr.len != 3u ||
        !v3_symbol_is(list->expr.elems[2], "DNil") ||
        !v3_head_is(list->expr.elems[1], "Decl") ||
        list->expr.elems[1]->expr.len != 3u) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_UNDETERMINED;
        decision->relation = "EnvDeclaredList";
        arena_free(&scratch);
        return true;
    }
    const Atom *wire_type = list->expr.elems[1]->expr.elems[2];
    Atom *core_variable = atom_var(&scratch, "v3-core-signature");
    Atom *elaboration_query = v3_relation2(
        &scratch, "V3ElaborateType", wire_type, core_variable);
    Atom *elaboration_answer = NULL;
    V3UniqueDecision elaboration = v3_run_unique(
        program, providers, &scratch, elaboration_query, limits,
        &elaboration_answer, &search_outcome, error, error_size);
    if (elaboration == V3_UNIQUE_FAULT) {
        arena_free(&scratch);
        return false;
    }
    if (elaboration == V3_UNIQUE_INCOMPLETE) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
        decision->relation = "V3ElaborateType";
        decision->search_outcome = search_outcome;
        arena_free(&scratch);
        return true;
    }
    if (elaboration != V3_UNIQUE_ONE ||
        !v3_head_is(elaboration_answer, "V3ElaborateType") ||
        elaboration_answer->expr.len != 3u) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_UNDETERMINED;
        decision->relation = "V3ElaborateType";
        arena_free(&scratch);
        return true;
    }
    const Atom *signature = elaboration_answer->expr.elems[2];
    if (!v3_head_is(signature, "V3Arrow") ||
        signature->expr.len != 4u) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_UNDETERMINED;
        decision->relation = "V3ElaborateType";
        arena_free(&scratch);
        return true;
    }
    const Atom *demand = signature->expr.elems[2];
    const Atom *expected = signature->expr.elems[3];
    V3SourceEnvironment source_environment = {0};
    if (lhs && lhs->kind == ATOM_EXPR && lhs->expr.len > 0u) {
        const Atom *domains = signature->expr.elems[1];
        for (CettaExprIndex index = 1u; index < lhs->expr.len; index++) {
            if (!v3_head_is(domains, "V3ArgsCons") ||
                domains->expr.len != 3u) {
                decision->outcome = CETTA_PETTA_V3_EVIDENCE_REFUTED;
                decision->boundary = CETTA_PETTA_V3_BOUNDARY_SHAPE;
                decision->relation = "V3PatternArity";
                arena_free(&scratch);
                return true;
            }
            const Atom *pattern = lhs->expr.elems[index];
            V3PatternAdmission admission = v3_bind_source_pattern(
                program, providers, &scratch, &source_environment,
                pattern, domains->expr.elems[1], limits,
                error, error_size, 0u);
            if (admission.status == V3_PATTERN_FAULT) {
                arena_free(&scratch);
                return false;
            }
            if (admission.status == V3_PATTERN_INCOMPLETE) {
                decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
                decision->relation = admission.relation;
                decision->search_outcome = admission.search_outcome;
                arena_free(&scratch);
                return true;
            }
            if (admission.status == V3_PATTERN_EMPTY) {
                /* A statically impossible head contributes no successful
                   rewrite.  It does not refute the program: coverage and
                   exhaustiveness are judgments over the whole relation. */
                decision->outcome = CETTA_PETTA_V3_EVIDENCE_ESTABLISHED;
                decision->boundary = CETTA_PETTA_V3_BOUNDARY_NONE;
                decision->relation = "v3-pattern-empty-clause";
                decision->search_outcome = admission.search_outcome;
                arena_free(&scratch);
                return true;
            }
            domains = domains->expr.elems[2];
        }
        if (!v3_symbol_is(domains, "V3ArgsNil")) {
            decision->outcome = CETTA_PETTA_V3_EVIDENCE_REFUTED;
            decision->boundary = CETTA_PETTA_V3_BOUNDARY_SHAPE;
            decision->relation = "V3PatternArity";
            arena_free(&scratch);
            return true;
        }
    }

    /* An empty fold's result is its initializer.  Reify only the source
       recognition here; the langdef relation performs the evidence step. */
    if (!evidence && v3_is_fold_empty_generator(body)) {
        const Atom *domains = signature->expr.elems[1];
        if (v3_head_is(domains, "V3ArgsCons") &&
            domains->expr.len == 3u) {
            Atom *initializer_evidence = v3_runtime_evidence_for_type(
                &scratch, domains->expr.elems[1], "V3Det");
            Atom *fold_evidence_variable = atom_var(
                &scratch, "v3-fold-evidence");
            Atom *fold_query = initializer_evidence && fold_evidence_variable
                ? v3_relation2(
                    &scratch, "V3FoldEmptyEvidence",
                    initializer_evidence, fold_evidence_variable)
                : NULL;
            Atom *fold_answer = NULL;
            V3UniqueDecision fold_result = v3_run_unique(
                program, providers, &scratch, fold_query, limits,
                &fold_answer, &search_outcome, error, error_size);
            if (fold_result == V3_UNIQUE_FAULT) {
                arena_free(&scratch);
                return false;
            }
            if (fold_result == V3_UNIQUE_INCOMPLETE) {
                decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
                decision->relation = "V3FoldEmptyEvidence";
                decision->search_outcome = search_outcome;
                arena_free(&scratch);
                return true;
            }
            if (fold_result == V3_UNIQUE_ONE &&
                v3_head_is(fold_answer, "V3FoldEmptyEvidence") &&
                fold_answer->expr.len == 3u) {
                evidence = fold_answer->expr.elems[2];
            }
        }
    }
    if (!evidence) {
        V3SourceCompilation compiled = v3_compile_source_evidence(
            program, providers, &scratch, &source_environment, body, limits,
            error, error_size, 0u);
        if (compiled.status == V3_SOURCE_FAULT) {
            arena_free(&scratch);
            return false;
        }
        if (compiled.status == V3_SOURCE_INCOMPLETE) {
            decision->outcome = CETTA_PETTA_V3_EVIDENCE_INCOMPLETE;
            decision->relation = compiled.relation;
            decision->search_outcome = compiled.search_outcome;
            arena_free(&scratch);
            return true;
        }
        if (compiled.status == V3_SOURCE_SHAPE_CONFLICT) {
            decision->outcome = CETTA_PETTA_V3_EVIDENCE_REFUTED;
            decision->boundary = CETTA_PETTA_V3_BOUNDARY_SHAPE;
            decision->relation = compiled.relation;
            decision->search_outcome = compiled.search_outcome;
            arena_free(&scratch);
            return true;
        }
        if (compiled.status == V3_SOURCE_EVIDENCE)
            evidence = compiled.evidence;
    }
    if (!evidence) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_UNDETERMINED;
        decision->relation = "V3ExpressionEvidence";
        arena_free(&scratch);
        return true;
    }

    if (v3_head_is(evidence, "V3HeldEvidence")) {
        decision->outcome = CETTA_PETTA_V3_EVIDENCE_REFUTED;
        decision->boundary = CETTA_PETTA_V3_BOUNDARY_STAGE;
        decision->relation = "V3ExpressionEvidence";
        decision->search_outcome = search_outcome;
        arena_free(&scratch);
        return true;
    }
    bool decided = cetta_petta_typecheck_v3_decide_evidence_v1(
        program, providers, evidence, expected, demand, limits,
        decision, error, error_size);
    decision->actual = NULL;
    decision->required = NULL;
    arena_free(&scratch);
    return decided;
}

bool cetta_petta_typecheck_v3_decide_definition_v1(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    const Atom *subject,
    const Atom *body,
    CettaGsltHornLimits limits,
    CettaPettaV3EvidenceDecisionV1 *decision,
    char *error,
    size_t error_size) {
    return v3_decide_definition_internal_v1(
        program, providers, NULL, subject, body, limits,
        decision, error, error_size);
}

bool cetta_petta_typecheck_v3_decide_equation_v1(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    const Atom *lhs,
    const Atom *body,
    CettaGsltHornLimits limits,
    CettaPettaV3EvidenceDecisionV1 *decision,
    char *error,
    size_t error_size) {
    const Atom *subject = NULL;
    if (lhs && lhs->kind == ATOM_SYMBOL) {
        subject = lhs;
    } else if (lhs && lhs->kind == ATOM_EXPR && lhs->expr.len > 0u &&
               lhs->expr.elems[0]->kind == ATOM_SYMBOL) {
        subject = lhs->expr.elems[0];
    }
    if (!subject) {
        if (decision)
            *decision = (CettaPettaV3EvidenceDecisionV1){
                .outcome = CETTA_PETTA_V3_EVIDENCE_UNDETERMINED,
            };
        if (decision)
            decision->relation = "V3DefinitionHead";
        return decision != NULL;
    }
    return v3_decide_definition_internal_v1(
        program, providers, lhs, subject, body, limits,
        decision, error, error_size);
}
