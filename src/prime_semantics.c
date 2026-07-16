/* MeTTa-Prime v0: a bounded executable semantic package over CeTTa's shared
 * evaluator and unified dependent type engine.  The package exposes explicit
 * judgments and structured four-way verdicts.  It does not make search or a
 * computed normal form into MIK theoremhood; those correspondence obligations
 * are recorded in the public specification. */

#include "prime_semantics.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "he_typing.h"
#include "space.h"
#include "symbol.h"

#define PRIME_SCHEMA_VERSION 0
#define PRIME_DEFAULT_FUEL 100000u

static const char *const PRIME_PACKAGE_CANONICAL =
    "metta-prime-v0|syntax=metta-sexpr|binding=named-scoped|"
    "typing=form,synth,check,analyze,convert,refine,may,must|"
    "unknown=dynamic-nontransitive|top=expected-atom-only|"
    "dependent=explicit-telescope|type-computation=marked-snapshot-bounded|"
    "nondeterminism=explicit-answer-bag-with-completeness|"
    "runtime-bag-correspondence=open|"
    "results=established,refuted,undetermined,incomplete|"
    "information=incomplete-and-undetermined-below-determinate|"
    "resources=one-aggregate-reported-ledger|"
    "effects=ordinary-runtime-plus-checked-type-boundary|"
    "trust=producer-untrusted-checker-rederives";

static Atom *prime_sym(Arena *a, const char *name) {
    return atom_symbol(a, name);
}

static Atom *prime_expr1(Arena *a, const char *head) {
    Atom *items[1] = {prime_sym(a, head)};
    return atom_expr(a, items, 1);
}

static Atom *prime_expr2(Arena *a, const char *head, Atom *x) {
    return atom_expr2(a, prime_sym(a, head), x);
}

static Atom *prime_expr3(Arena *a, const char *head, Atom *x, Atom *y) {
    return atom_expr3(a, prime_sym(a, head), x, y);
}

static Atom *prime_verdict(Arena *a, const char *status, Atom *judgment,
                           Atom *evidence) {
    Atom *items[4] = {prime_sym(a, "PrimeVerdict"), prime_sym(a, status),
                      judgment, evidence};
    return atom_expr(a, items, 4);
}

static Atom *prime_established(Arena *a, Atom *judgment, Atom *evidence) {
    return prime_verdict(a, "Established", judgment, evidence);
}

static Atom *prime_refuted(Arena *a, Atom *judgment, Atom *reason) {
    return prime_verdict(a, "Refuted", judgment, reason);
}

static Atom *prime_undetermined(Arena *a, Atom *judgment, Atom *reason) {
    return prime_verdict(a, "Undetermined", judgment, reason);
}

static Atom *prime_incomplete(Arena *a, Atom *judgment, Atom *reason) {
    return prime_verdict(a, "Incomplete", judgment, reason);
}

static Atom *unquote_data(Atom *atom) {
    while (atom && atom->kind == ATOM_EXPR && atom->expr.len == 2 &&
           atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.quote)) {
        atom = atom->expr.elems[1];
    }
    return atom;
}

static bool is_symbol_named(Atom *atom, const char *name);

static bool arg_space(Atom *atom, Space **out) {
    if (atom && atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_SPACE) {
        *out = (Space *)atom->ground.ptr;
        return *out != NULL;
    }
    return false;
}

static bool arg_fuel(Atom *atom, uint64_t *out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival <= 0 ||
        atom->ground.ival > INT_MAX) {
        return false;
    }
    *out = (uint64_t)atom->ground.ival;
    return true;
}

typedef enum {
    PRIME_RESOURCE_FORMATION = 0,
    PRIME_RESOURCE_SYNTHESIS,
    PRIME_RESOURCE_NORMALIZATION,
    PRIME_RESOURCE_CHECKING,
    PRIME_RESOURCE_REFINEMENT,
    PRIME_RESOURCE_EVALUATION,
    PRIME_RESOURCE_PHASE_COUNT
} PrimeResourcePhase;

typedef struct {
    CettaHeTypingBudget typing;
    uint64_t phase_spent[PRIME_RESOURCE_PHASE_COUNT];
} PrimeResourceLedger;

static const char *const PRIME_RESOURCE_PHASE_NAMES[] = {
    "formation", "synthesis", "normalization", "checking", "refinement",
    "evaluation"};

static void prime_resource_init(PrimeResourceLedger *ledger, uint64_t steps) {
    memset(ledger, 0, sizeof(*ledger));
    he_typing_budget_init(&ledger->typing, steps);
}

static bool prime_resource_spend(PrimeResourceLedger *ledger,
                                 PrimeResourcePhase phase, uint64_t amount) {
    if (!ledger || phase >= PRIME_RESOURCE_PHASE_COUNT ||
        ledger->typing.steps_remaining < amount) {
        if (ledger) ledger->typing.steps_remaining = 0;
        return false;
    }
    ledger->typing.steps_remaining -= amount;
    ledger->phase_spent[phase] += amount;
    return true;
}

static uint64_t prime_resource_phase_begin(const PrimeResourceLedger *ledger) {
    return ledger ? ledger->typing.steps_remaining : 0;
}

static void prime_resource_phase_end(PrimeResourceLedger *ledger,
                                     PrimeResourcePhase phase,
                                     uint64_t before) {
    if (!ledger || phase >= PRIME_RESOURCE_PHASE_COUNT) return;
    uint64_t after = ledger->typing.steps_remaining;
    if (before > after) ledger->phase_spent[phase] += before - after;
}

static Atom *prime_resource_ledger_atom(Arena *a,
                                        const PrimeResourceLedger *ledger) {
    uint64_t initial = ledger->typing.steps_initial;
    uint64_t remaining = ledger->typing.steps_remaining;
    Atom **phases = arena_alloc(
        a, sizeof(Atom *) * (PRIME_RESOURCE_PHASE_COUNT + 1u));
    phases[0] = prime_sym(a, "PhaseSpend");
    for (uint32_t i = 0; i < PRIME_RESOURCE_PHASE_COUNT; i++) {
        phases[i + 1u] = prime_expr2(
            a, PRIME_RESOURCE_PHASE_NAMES[i],
            atom_int(a, (int64_t)ledger->phase_spent[i]));
    }
    Atom *limits_items[4] = {
        prime_sym(a, "DeclaredLimits"),
        prime_expr2(a, "depth", atom_int(a, ledger->typing.depth_limit)),
        prime_expr2(a, "type-capacity",
                    atom_int(a, ledger->typing.type_capacity)),
        prime_expr2(a, "normalization-continuation-reserve", atom_int(a, 1))};
    Atom *observed_items[3] = {
        prime_sym(a, "Observed"),
        prime_expr2(a, "max-depth",
                    atom_int(a, ledger->typing.max_depth_observed)),
        prime_expr2(a, "type-capacity-exhausted",
                    ledger->typing.type_capacity_exhausted
                        ? atom_true(a) : atom_false(a))};
    Atom *items[7] = {
        prime_sym(a, "ResourceLedgerV1"),
        prime_expr2(a, "initial", atom_int(a, (int64_t)initial)),
        prime_expr2(a, "spent", atom_int(a, (int64_t)(initial - remaining))),
        prime_expr2(a, "remaining", atom_int(a, (int64_t)remaining)),
        atom_expr(a, phases, PRIME_RESOURCE_PHASE_COUNT + 1u),
        atom_expr(a, limits_items, 4),
        atom_expr(a, observed_items, 3)};
    return atom_expr(a, items, 7);
}

static Atom *prime_attach_ledger(Arena *a, Atom *verdict,
                                 const PrimeResourceLedger *ledger) {
    if (!verdict || verdict->kind != ATOM_EXPR || verdict->expr.len != 4 ||
        !is_symbol_named(verdict->expr.elems[0], "PrimeVerdict")) {
        return verdict;
    }
    Atom *status = verdict->expr.elems[1];
    bool determinate = is_symbol_named(status, "Established") ||
                       is_symbol_named(status, "Refuted");
    Atom *evidence_items[4] = {
        prime_sym(a, "PrimeEvidenceV1"), verdict->expr.elems[3],
        prime_expr2(a, "InformationClass",
                    prime_sym(a, determinate ? "Determinate"
                                             : "Indeterminate")),
        prime_resource_ledger_atom(a, ledger)};
    Atom *items[4] = {verdict->expr.elems[0], status,
                      verdict->expr.elems[2], atom_expr(a, evidence_items, 4)};
    return atom_expr(a, items, 4);
}

static uint64_t fnv1a64(const char *text) {
    uint64_t hash = UINT64_C(14695981039346656037);
    while (*text) {
        hash ^= (unsigned char)*text++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static Atom *prime_package_atom(Arena *a) {
    char digest[32];
    CettaHeTypingBudget declared_budget;
    he_typing_budget_init(&declared_budget, 0);
    snprintf(digest, sizeof(digest), "%016" PRIx64,
             fnv1a64(PRIME_PACKAGE_CANONICAL));

    Atom *identity_items[6] = {
        prime_sym(a, "Identity"), prime_sym(a, "prime"),
        atom_int(a, PRIME_SCHEMA_VERSION), atom_string(a, "metta-prime-v0"),
        prime_expr3(a, "Digest", prime_sym(a, "FNV1a64"),
                    atom_string(a, digest)),
        prime_expr2(a, "CanonicalDescriptor",
                    atom_string(a, PRIME_PACKAGE_CANONICAL))};
    Atom *identity = atom_expr(a, identity_items, 6);

    Atom *syntax_items[5] = {
        prime_sym(a, "Syntax"), prime_sym(a, "HomoiconicSExpressions"),
        prime_sym(a, "NamedScopedVariables"),
        prime_sym(a, "ExplicitBangEvaluation"),
        prime_sym(a, "QuotedJudgmentData")};
    Atom *syntax = atom_expr(a, syntax_items, 5);

    Atom *judgment_items[9] = {
        prime_sym(a, "Judgments"), prime_sym(a, "Form"),
        prime_sym(a, "Synth"), prime_sym(a, "Check"),
        prime_sym(a, "Analyze"), prime_sym(a, "Convert"),
        prime_sym(a, "Refine"), prime_sym(a, "May"),
        prime_sym(a, "Must")};
    Atom *judgments = atom_expr(a, judgment_items, 9);

    Atom *result_items[5] = {
        prime_sym(a, "Results"), prime_sym(a, "Established"),
        prime_sym(a, "Refuted"), prime_sym(a, "Undetermined"),
        prime_sym(a, "Incomplete")};
    Atom *results = atom_expr(a, result_items, 5);

    Atom *information_items[8] = {
        prime_sym(a, "InformationOrderV1"),
        prime_expr3(a, "Below", prime_sym(a, "Incomplete"),
                    prime_sym(a, "Established")),
        prime_expr3(a, "Below", prime_sym(a, "Incomplete"),
                    prime_sym(a, "Refuted")),
        prime_expr3(a, "Below", prime_sym(a, "Undetermined"),
                    prime_sym(a, "Established")),
        prime_expr3(a, "Below", prime_sym(a, "Undetermined"),
                    prime_sym(a, "Refuted")),
        prime_expr3(a, "Incomparable", prime_sym(a, "Incomplete"),
                    prime_sym(a, "Undetermined")),
        prime_expr3(a, "Incomparable", prime_sym(a, "Established"),
                    prime_sym(a, "Refuted")),
        prime_expr2(a, "BudgetLaw", prime_sym(a, "DeterminateStable"))};
    Atom *information = atom_expr(a, information_items, 8);

    Atom *resource_items[7] = {
        prime_sym(a, "ResourcePolicyV1"),
        prime_sym(a, "CallerDeclaredAggregateSteps"),
        prime_expr2(a, "DepthLimit",
                    atom_int(a, declared_budget.depth_limit)),
        prime_expr2(a, "TypeCapacity",
                    atom_int(a, declared_budget.type_capacity)),
        prime_expr2(a, "NormalizationContinuationReserve", atom_int(a, 1)),
        prime_sym(a, "AllLimitsReportedInVerdict"),
        prime_sym(a, "TotalityRequiresCertifiedCompletion")};
    Atom *resources = atom_expr(a, resource_items, 7);

    Atom *typing_items[10] = {
        prime_sym(a, "Typing"), prime_sym(a, "DependentTelescopes"),
        prime_sym(a, "BidirectionalJudgments"),
        prime_sym(a, "RigidScopedVariables"),
        prime_sym(a, "ExplicitSchemeInstantiation"),
        prime_sym(a, "ReturnedElaborationSubstitution"),
        prime_expr2(a, "DynamicUnknown", atom_undefined_type(a)),
        prime_expr2(a, "ExpectedTop", atom_atom_type(a)),
        prime_sym(a, "CheckedExactOrStructural"),
        prime_sym(a, "GradualEdgesRequireBoundaryEvidence")};
    Atom *typing = atom_expr(a, typing_items, 10);

    Atom *runtime_items[6] = {
        prime_sym(a, "Runtime"),
        prime_sym(a, "SharedCeTTaEvaluator"),
        prime_sym(a, "MarkedTypeFunctionsOnly"),
        prime_sym(a, "SnapshotContained"),
        prime_sym(a, "FuelBounded"),
        prime_sym(a, "UniqueNormalResult")};
    Atom *runtime = atom_expr(a, runtime_items, 6);

    Atom *nondet_items[7] = {
        prime_sym(a, "Nondeterminism"),
        prime_sym(a, "ExplicitAnswerBags"),
        prime_sym(a, "ExplicitCompleteness"),
        prime_sym(a, "PreserveFailures"),
        prime_sym(a, "PreserveDuplicates"),
        prime_sym(a, "BranchIndexedEvidence"),
        prime_sym(a, "RuntimeBagCorrespondenceOpen")};
    Atom *nondet = atom_expr(a, nondet_items, 7);

    Atom *trust_items[6] = {
        prime_sym(a, "Trust"), prime_sym(a, "SearchIsProducer"),
        prime_sym(a, "TypingIsRechecked"),
        prime_sym(a, "ComputedNFIsProvisionalEvidence"),
        prime_sym(a, "CertificateCorrespondenceOpen"),
        prime_sym(a, "SourcePackageHashPinnedExternally")};
    Atom *trust = atom_expr(a, trust_items, 6);

    Atom *package_items[11] = {
        prime_sym(a, "PrimeSemanticPackageV0"), identity, syntax, judgments,
        results, information, resources, typing, runtime, nondet, trust};
    return atom_expr(a, package_items, 11);
}

typedef enum {
    PRIME_FORM_ESTABLISHED = 0,
    PRIME_FORM_REFUTED,
    PRIME_FORM_UNDETERMINED,
    PRIME_FORM_INCOMPLETE
} PrimeFormStatus;

static bool is_symbol_named(Atom *atom, const char *name) {
    return atom && atom_is_symbol(atom, name);
}

static bool is_primitive_type_symbol(Atom *atom) {
    static const char *const names[] = {
        "Type", "%Undefined%", "Atom", "Symbol", "Variable",
        "Expression", "Grounded", "Number", "Bool", "String",
        "ErrorType", NULL};
    if (!atom || atom->kind != ATOM_SYMBOL) return false;
    for (size_t i = 0; names[i]; i++)
        if (atom_is_symbol(atom, names[i])) return true;
    return false;
}

static bool inferred_as_type(Space *space, Arena *a, Atom *type,
                             bool *dynamic_only) {
    Atom **types = NULL;
    uint32_t count = eval_get_atom_types_profiled(space, a, type, &types);
    bool saw_type = false;
    bool saw_dynamic = false;
    for (uint32_t i = 0; i < count; i++) {
        if (is_symbol_named(types[i], "Type"))
            saw_type = true;
        if (atom_is_symbol_id(types[i], g_builtin_syms.undefined_type))
            saw_dynamic = true;
    }
    free(types);
    if (dynamic_only) *dynamic_only = saw_dynamic && !saw_type;
    return saw_type;
}

typedef struct PrimeVarContext {
    VarId id;
    const struct PrimeVarContext *parent;
} PrimeVarContext;

static bool prime_var_is_bound(const PrimeVarContext *context, VarId id) {
    for (const PrimeVarContext *it = context; it; it = it->parent)
        if (it->id == id) return true;
    return false;
}

static bool prime_all_vars_bound(Atom *atom,
                                 const PrimeVarContext *context) {
    if (!atom) return true;
    if (atom->kind == ATOM_VAR)
        return prime_var_is_bound(context, atom->var_id);
    if (atom->kind != ATOM_EXPR) return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++)
        if (!prime_all_vars_bound(atom->expr.elems[i], context)) return false;
    return true;
}

static PrimeFormStatus prime_form_type(Space *space, Arena *a, Atom *type,
                                       PrimeResourceLedger *ledger,
                                       Atom **detail,
                                       const PrimeVarContext *context) {
    if (!prime_resource_spend(ledger, PRIME_RESOURCE_FORMATION, 1)) {
        *detail = prime_expr1(a, "formation-resource-exhausted");
        return PRIME_FORM_INCOMPLETE;
    }
    type = unquote_data(type);

    if (is_primitive_type_symbol(type)) {
        *detail = prime_expr2(a, "PrimitiveTypeFormation", type);
        return PRIME_FORM_ESTABLISHED;
    }

    if (type->kind == ATOM_EXPR && type->expr.len > 0 &&
        is_symbol_named(type->expr.elems[0], "Type")) {
        *detail = prime_expr2(a, "universes-deferred", type);
        return PRIME_FORM_UNDETERMINED;
    }

    if (type->kind == ATOM_VAR) {
        if (prime_var_is_bound(context, type->var_id)) {
            *detail = prime_expr2(a, "RigidVariableFormation", type);
            return PRIME_FORM_ESTABLISHED;
        }
        *detail = prime_expr2(a, "unbound-type-variable", type);
        return PRIME_FORM_UNDETERMINED;
    }

    if (type->kind == ATOM_GROUNDED) {
        *detail = prime_expr2(a, "grounded-value-is-not-a-type", type);
        return PRIME_FORM_REFUTED;
    }


    if (type->kind == ATOM_EXPR && type->expr.len == 0) {
        *detail = prime_expr1(a, "empty-expression-is-not-a-type");
        return PRIME_FORM_REFUTED;
    }

    if (type->kind == ATOM_EXPR && type->expr.len > 0 &&
        atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow)) {
        if (type->expr.len < 2) {
            *detail = prime_expr1(a, "empty-function-telescope");
            return PRIME_FORM_REFUTED;
        }
        PrimeVarContext *frames = arena_alloc(
            a, sizeof(PrimeVarContext) * (size_t)type->expr.len);
        uint32_t frame_count = 0;
        const PrimeVarContext *scope = context;
        for (CettaExprIndex i = 1; i < type->expr.len; i++) {
            Atom *component = type->expr.elems[i];
            Atom *binder = NULL;
            if (i + 1 < type->expr.len && component->kind == ATOM_EXPR &&
                component->expr.len == 3 &&
                atom_is_symbol_id(component->expr.elems[0],
                                  g_builtin_syms.colon)) {
                if (component->expr.elems[1]->kind != ATOM_VAR) {
                    *detail = prime_expr2(a, "binder-name-is-not-a-variable",
                                          component);
                    return PRIME_FORM_REFUTED;
                }
                binder = component->expr.elems[1];
                component = component->expr.elems[2];
            }
            Atom *component_detail = NULL;
            PrimeFormStatus component_status = prime_form_type(
                space, a, component, ledger, &component_detail, scope);
            if (component_status != PRIME_FORM_ESTABLISHED) {
                *detail = prime_expr3(a, "ill-formed-telescope-component",
                                      atom_int(a, (int64_t)(i - 1)),
                                      component_detail);
                return component_status;
            }
            if (binder) {
                frames[frame_count] = (PrimeVarContext){
                    .id = binder->var_id,
                    .parent = scope,
                };
                scope = &frames[frame_count++];
            }
        }
        *detail = prime_expr2(a, "TelescopeFormation", type);
        return PRIME_FORM_ESTABLISHED;
    }

    bool dynamic_only = false;
    if (!prime_all_vars_bound(type, context)) {
        *detail = prime_expr2(a, "unbound-type-variable", type);
        return PRIME_FORM_UNDETERMINED;
    }
    if (inferred_as_type(space, a, type, &dynamic_only)) {
        *detail = prime_expr2(a, "DeclaredTypeFormation", type);
        return PRIME_FORM_ESTABLISHED;
    }
    if (dynamic_only || type->kind == ATOM_SYMBOL) {
        *detail = prime_expr2(a, "undeclared-type-form", type);
        return PRIME_FORM_UNDETERMINED;
    }

    *detail = prime_expr2(a, "invalid-type-application", type);
    return PRIME_FORM_REFUTED;
}

static Atom *prime_synth(Space *space, Arena *a, Atom *judgment, Atom *term,
                         PrimeResourceLedger *ledger) {
    if (!prime_resource_spend(ledger, PRIME_RESOURCE_SYNTHESIS, 1))
        return prime_incomplete(a, judgment,
                                prime_expr1(a, "synthesis-resource-exhausted"));
    Atom **types = NULL;
    uint32_t count = eval_get_atom_types_profiled(space, a, term, &types);
    if (count > ledger->typing.type_capacity) {
        ledger->typing.type_capacity_exhausted = true;
        free(types);
        return prime_incomplete(a, judgment,
                                prime_expr1(a, "type-set-capacity"));
    }
    if (!prime_resource_spend(ledger, PRIME_RESOURCE_SYNTHESIS, count)) {
        free(types);
        return prime_incomplete(a, judgment,
                                prime_expr1(a, "synthesis-resource-exhausted"));
    }
    if (count == 0) {
        free(types);
        return prime_refuted(a, judgment, prime_expr1(a, "no-inferred-type"));
    }

    uint32_t known_count = 0;
    bool dynamic_present = false;
    for (uint32_t i = 0; i < count; i++) {
        if (atom_is_symbol_id(types[i], g_builtin_syms.undefined_type))
            dynamic_present = true;
        else
            known_count++;
    }
    if (known_count == 0) {
        free(types);
        return prime_undetermined(a, judgment,
                                  prime_expr1(a, "dynamic-type-only"));
    }

    Atom **items = arena_alloc(a, sizeof(Atom *) * (count + 3));
    items[0] = prime_sym(a, "InferredTypes");
    items[1] = prime_sym(a, "RuntimeEnumerated");
    items[2] = prime_expr2(a, "DynamicPresent",
                           dynamic_present ? atom_true(a) : atom_false(a));
    for (uint32_t i = 0; i < count; i++) items[i + 3] = types[i];
    Atom *evidence = atom_expr(a, items, count + 3);
    free(types);
    return prime_established(a, judgment, evidence);
}

static Atom *prime_check_or_analyze(Space *space, Arena *a, Atom *judgment,
                                    Atom *term, Atom *expected,
                                    PrimeResourceLedger *ledger,
                                    bool require_exact_or_structural) {
    Atom *formation_detail = NULL;
    PrimeFormStatus formation = prime_form_type(
        space, a, expected, ledger, &formation_detail, NULL);
    if (formation == PRIME_FORM_REFUTED)
        return prime_refuted(a, judgment,
                             prime_expr2(a, "ill-formed-expected-type",
                                         formation_detail));
    if (formation == PRIME_FORM_UNDETERMINED)
        return prime_undetermined(a, judgment,
                                  prime_expr2(a, "expected-type-undetermined",
                                              formation_detail));
    if (formation == PRIME_FORM_INCOMPLETE)
        return prime_incomplete(a, judgment,
                                prime_expr2(a, "expected-type-incomplete",
                                            formation_detail));

    CettaHeTypingEdge edge = CETTA_HE_EDGE_NONE;
    Atom *detail = NULL;
    uint64_t phase_before = prime_resource_phase_begin(ledger);
    CettaHeCheckStatus status = he_typing_check_term_status_budgeted(
        a, space, term, expected, &ledger->typing,
        require_exact_or_structural, &edge, &detail);
    prime_resource_phase_end(ledger, PRIME_RESOURCE_CHECKING, phase_before);
    Atom *edge_atom = prime_sym(a, he_typing_edge_name(edge));
    if (status == CETTA_HE_CHECK_ESTABLISHED) {
        Atom *evidence = prime_expr3(
            a, require_exact_or_structural ? "CheckedTypingEvidence"
                                           : "ConsistencyEvidence",
            edge_atom, detail ? detail : expected);
        return prime_established(a, judgment, evidence);
    }
    if (status == CETTA_HE_CHECK_REFUTED)
        return prime_refuted(a, judgment,
                             detail ? detail : prime_expr1(a, "type-mismatch"));
    if (status == CETTA_HE_CHECK_INCOMPLETE)
        return prime_incomplete(
            a, judgment,
            detail ? detail : prime_expr1(a, "typing-resource-incomplete"));
    return prime_undetermined(
        a, judgment,
        detail ? detail : prime_expr1(a, "typing-boundary-undetermined"));
}

static const char *normalize_reason(CettaHeNormalizeStatus status) {
    switch (status) {
    case CETTA_HE_NORMALIZE_COMPLETE: return "complete";
    case CETTA_HE_NORMALIZE_RESOURCE: return "normalization-resource-exhausted";
    case CETTA_HE_NORMALIZE_DEPTH: return "normalization-depth-exhausted";
    case CETTA_HE_NORMALIZE_AMBIGUOUS: return "normalization-ambiguous";
    case CETTA_HE_NORMALIZE_NO_RESULT: return "normalization-no-result";
    case CETTA_HE_NORMALIZE_INADMISSIBLE: return "normalization-inadmissible-effect";
    }
    return "normalization-undetermined";
}

static Atom *prime_convert(Space *space, Arena *a, Atom *judgment,
                           Atom *left, Atom *right,
                           PrimeResourceLedger *ledger) {
    Atom *left_nf = left;
    Atom *right_nf = right;
    uint64_t phase_before = prime_resource_phase_begin(ledger);
    /* The expected/reference side is normalized first.  This leaves the
       remaining aggregate budget to the candidate without splitting it into
       opaque per-side allowances. */
    CettaHeNormalizeStatus right_status =
        he_typing_normalize_type_status_budgeted(
            a, space, right, &ledger->typing, &right_nf);
    CettaHeNormalizeStatus left_status =
        he_typing_normalize_type_status_budgeted(
            a, space, left, &ledger->typing, &left_nf);
    prime_resource_phase_end(ledger, PRIME_RESOURCE_NORMALIZATION,
                             phase_before);
    if (left_status == CETTA_HE_NORMALIZE_INADMISSIBLE ||
        right_status == CETTA_HE_NORMALIZE_INADMISSIBLE) {
        Atom *reason_items[3] = {
            prime_sym(a, "conversion-inadmissible"),
            prime_sym(a, normalize_reason(left_status)),
            prime_sym(a, normalize_reason(right_status))};
        return prime_refuted(a, judgment, atom_expr(a, reason_items, 3));
    }
    if (left_status == CETTA_HE_NORMALIZE_RESOURCE ||
        right_status == CETTA_HE_NORMALIZE_RESOURCE ||
        left_status == CETTA_HE_NORMALIZE_DEPTH ||
        right_status == CETTA_HE_NORMALIZE_DEPTH) {
        Atom *reason_items[3] = {
            prime_sym(a, "conversion-resource-incomplete"),
            prime_sym(a, normalize_reason(left_status)),
            prime_sym(a, normalize_reason(right_status))};
        return prime_incomplete(a, judgment, atom_expr(a, reason_items, 3));
    }
    if (left_status != CETTA_HE_NORMALIZE_COMPLETE ||
        right_status != CETTA_HE_NORMALIZE_COMPLETE) {
        Atom *reason_items[3] = {
            prime_sym(a, "conversion-incomplete"),
            prime_sym(a, normalize_reason(left_status)),
            prime_sym(a, normalize_reason(right_status))};
        return prime_undetermined(a, judgment, atom_expr(a, reason_items, 3));
    }
    Atom *evidence_items[4] = {
        prime_sym(a, "ComputedNormalFormComparison"), left_nf, right_nf,
        prime_sym(a, "prime-v0-bounded-nf")};
    Atom *evidence = atom_expr(a, evidence_items, 4);
    return atom_eq(left_nf, right_nf)
        ? prime_established(a, judgment, evidence)
        : prime_refuted(a, judgment,
                        prime_expr3(a, "distinct-normal-forms", left_nf,
                                    right_nf));
}

static Atom *prime_refine(Space *space, Arena *a, Atom *judgment,
                          Atom *type, PrimeResourceLedger *ledger) {
    Atom *formation_detail = NULL;
    PrimeFormStatus formation = prime_form_type(
        space, a, type, ledger, &formation_detail, NULL);
    if (formation == PRIME_FORM_REFUTED)
        return prime_refuted(a, judgment,
                             prime_expr2(a, "ill-formed-refinement-type",
                                         formation_detail));
    if (formation == PRIME_FORM_UNDETERMINED)
        return prime_undetermined(a, judgment,
                                  prime_expr2(a, "refinement-type-undetermined",
                                              formation_detail));
    if (formation == PRIME_FORM_INCOMPLETE)
        return prime_incomplete(a, judgment,
                                prime_expr2(a, "refinement-type-incomplete",
                                            formation_detail));

    Atom *detail = NULL;
    uint64_t phase_before = prime_resource_phase_begin(ledger);
    CettaHeRefinementStatus status =
        he_typing_check_refinement_status_budgeted(
            a, space, type, &ledger->typing, &detail);
    prime_resource_phase_end(ledger, PRIME_RESOURCE_REFINEMENT, phase_before);
    if (status == CETTA_HE_REFINEMENT_VALID)
        return prime_established(a, judgment,
                                 prime_expr2(a, "RefinementEvidence", type));
    if (status == CETTA_HE_REFINEMENT_INVALID)
        return prime_refuted(a, judgment,
                             detail ? detail
                                    : prime_expr1(a, "refinement-failed"));
    if (status == CETTA_HE_REFINEMENT_INCOMPLETE)
        return prime_incomplete(
            a, judgment,
            detail ? detail : prime_expr1(a, "refinement-resource-incomplete"));
    return prime_undetermined(
        a, judgment,
        detail ? detail : prime_expr1(a, "refinement-undetermined"));
}

typedef struct {
    bool closure_certified;
    bool branches_wrapped;
    Atom *incomplete_reason;
    Atom **branches;
    uint32_t branch_count;
} PrimeAnswerBag;

static bool parse_answer_bag(Atom *bag, PrimeAnswerBag *out) {
    memset(out, 0, sizeof(*out));
    if (!bag || bag->kind != ATOM_EXPR || bag->expr.len < 2 ||
        !is_symbol_named(bag->expr.elems[0], "PrimeAnswers")) {
        return false;
    }
    Atom *status = bag->expr.elems[1];
    if (is_symbol_named(status, "Complete")) {
        /* A producer's word is not closure evidence.  Prefix witnesses and
           counterexamples remain usable, but totality verdicts stay gated. */
        out->closure_certified = false;
    } else if (status->kind == ATOM_EXPR && status->expr.len == 2 &&
               is_symbol_named(status->expr.elems[0], "Incomplete")) {
        out->closure_certified = false;
        out->incomplete_reason = status->expr.elems[1];
    } else {
        return false;
    }
    out->branches_wrapped = true;
    out->branches = bag->expr.elems + 2;
    out->branch_count = (uint32_t)(bag->expr.len - 2);
    return true;
}

static bool prepare_answer_bag(Space *space, Arena *a, Atom *source,
                               PrimeResourceLedger *ledger,
                               PrimeAnswerBag *out) {
    memset(out, 0, sizeof(*out));
    if (!source || source->kind != ATOM_EXPR || source->expr.len == 0)
        return false;
    if (!is_symbol_named(source->expr.elems[0], "PrimeEvaluate"))
        return parse_answer_bag(source, out);
    if (source->expr.len != 2) return false;

    uint64_t available = ledger->typing.steps_remaining;
    if (available == 0) {
        out->closure_certified = false;
        out->incomplete_reason = prime_sym(a, "fuel-exhausted");
        return true;
    }
    EvalOutcome outcome;
    eval_outcome_init(&outcome);
    int budget = available > (uint64_t)INT_MAX ? INT_MAX : (int)available;
    metta_eval_outcome(space, a, NULL, source->expr.elems[1], budget,
                       &outcome);
    uint64_t spent = outcome.budget_initial - outcome.budget_remaining;
    if (spent >= ledger->typing.steps_remaining)
        ledger->typing.steps_remaining = 0;
    else
        ledger->typing.steps_remaining -= spent;
    ledger->phase_spent[PRIME_RESOURCE_EVALUATION] += spent;

    out->branches = arena_alloc(
        a, sizeof(Atom *) * (outcome.results.len > 0 ? outcome.results.len : 1));
    for (CettaCount i = 0; i < outcome.results.len; i++)
        out->branches[i] = outcome.results.items[i];
    out->branch_count = (uint32_t)outcome.results.len;
    out->branches_wrapped = false;
    out->closure_certified = outcome.completion == CETTA_EVAL_COMPLETE;
    if (!out->closure_certified)
        out->incomplete_reason =
            prime_sym(a, eval_completion_reason(outcome.completion));
    eval_outcome_free(&outcome);
    return true;
}

static bool parse_answer_branch(const PrimeAnswerBag *bag, Atom *branch,
                                bool *is_value, Atom **payload) {
    if (!bag->branches_wrapped) {
        *is_value = !atom_is_error(branch) && !atom_is_empty(branch);
        *payload = branch;
        return true;
    }
    if (!branch || branch->kind != ATOM_EXPR || branch->expr.len != 2)
        return false;
    if (is_symbol_named(branch->expr.elems[0], "PrimeValue")) {
        *is_value = true;
        *payload = branch->expr.elems[1];
        return true;
    }
    if (is_symbol_named(branch->expr.elems[0], "PrimeFailure")) {
        *is_value = false;
        *payload = branch->expr.elems[1];
        return true;
    }
    return false;
}

static Atom *prime_may_or_must(Space *space, Arena *a, Atom *judgment,
                               Atom *bag_atom, Atom *expected,
                               PrimeResourceLedger *ledger, bool must) {
    Atom *formation_detail = NULL;
    PrimeFormStatus formation = prime_form_type(
        space, a, expected, ledger, &formation_detail, NULL);
    if (formation == PRIME_FORM_REFUTED)
        return prime_refuted(a, judgment,
                             prime_expr2(a, "ill-formed-branch-type",
                                         formation_detail));
    if (formation == PRIME_FORM_UNDETERMINED)
        return prime_undetermined(a, judgment,
                                  prime_expr2(a, "branch-type-undetermined",
                                              formation_detail));
    if (formation == PRIME_FORM_INCOMPLETE)
        return prime_incomplete(a, judgment,
                                prime_expr2(a, "branch-type-incomplete",
                                            formation_detail));

    PrimeAnswerBag bag;
    if (!prepare_answer_bag(space, a, bag_atom, ledger, &bag))
        return prime_refuted(a, judgment,
                             prime_expr1(a, "malformed-answer-bag"));

    Atom **branch_evidence = arena_alloc(
        a, sizeof(Atom *) * (bag.branch_count > 0 ? bag.branch_count : 1));
    uint32_t evidence_count = 0;
    uint32_t value_count = 0;
    uint32_t failure_count = 0;
    bool saw_undetermined = false;
    bool saw_incomplete = false;
    Atom *undetermined_detail = NULL;
    Atom *incomplete_detail = NULL;

    for (uint32_t i = 0; i < bag.branch_count; i++) {
        bool is_value = false;
        Atom *payload = NULL;
        if (!parse_answer_branch(&bag, bag.branches[i], &is_value, &payload)) {
            return prime_refuted(
                a, judgment,
                prime_expr3(a, "malformed-answer-branch", atom_int(a, i),
                            bag.branches[i]));
        }
        if (!is_value) {
            failure_count++;
            continue;
        }
        value_count++;
        CettaHeTypingEdge edge = CETTA_HE_EDGE_NONE;
        Atom *detail = NULL;
        uint64_t phase_before = prime_resource_phase_begin(ledger);
        CettaHeCheckStatus status = he_typing_check_term_status_budgeted(
            a, space, payload, expected, &ledger->typing, true, &edge,
            &detail);
        prime_resource_phase_end(ledger, PRIME_RESOURCE_CHECKING,
                                 phase_before);
        if (status == CETTA_HE_CHECK_ESTABLISHED) {
            Atom *branch_items[4] = {
                prime_sym(a, "BranchEvidence"), atom_int(a, i), payload,
                prime_sym(a, he_typing_edge_name(edge))};
            branch_evidence[evidence_count++] = atom_expr(a, branch_items, 4);
            if (!must) {
                Atom *may_items[5] = {
                    prime_sym(a, "MayWitness"), atom_int(a, i), payload,
                    expected, prime_sym(a, he_typing_edge_name(edge))};
                return prime_established(a, judgment,
                                         atom_expr(a, may_items, 5));
            }
            continue;
        }
        if (status == CETTA_HE_CHECK_REFUTED) {
            if (must) {
                Atom *counter_items[4] = {
                    prime_sym(a, "MustCounterexample"), atom_int(a, i),
                    payload, detail ? detail : prime_expr1(a, "type-mismatch")};
                return prime_refuted(a, judgment,
                                     atom_expr(a, counter_items, 4));
            }
            continue;
        }
        if (status == CETTA_HE_CHECK_INCOMPLETE) {
            saw_incomplete = true;
            if (!incomplete_detail) incomplete_detail = detail;
            continue;
        }
        saw_undetermined = true;
        if (!undetermined_detail)
            undetermined_detail = detail;
    }

    if (must) {
        if (saw_incomplete)
            return prime_incomplete(
                a, judgment,
                incomplete_detail
                    ? incomplete_detail
                    : prime_expr1(a, "branch-typing-resource-incomplete"));
        if (saw_undetermined)
            return prime_undetermined(
                a, judgment,
                undetermined_detail
                    ? undetermined_detail
                    : prime_expr1(a, "branch-typing-undetermined"));
        if (!bag.closure_certified) {
            return prime_incomplete(
                a, judgment,
                prime_expr3(a, "must-awaits-complete-bag",
                            bag.incomplete_reason
                                ? bag.incomplete_reason
                                : prime_sym(a, "uncertified-producer-complete"),
                            atom_int(a, value_count)));
        }
        if (value_count == 0)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "no-value-branches"));

        Atom **items = arena_alloc(a, sizeof(Atom *) * (evidence_count + 4));
        items[0] = prime_sym(a, "MustEvidence");
        items[1] = atom_int(a, value_count);
        items[2] = atom_int(a, failure_count);
        items[3] = prime_sym(a, "Complete");
        for (uint32_t i = 0; i < evidence_count; i++)
            items[i + 4] = branch_evidence[i];
        return prime_established(a, judgment,
                                 atom_expr(a, items, evidence_count + 4));
    }

    if (saw_incomplete)
        return prime_incomplete(
            a, judgment,
            incomplete_detail
                ? incomplete_detail
                : prime_expr1(a, "branch-typing-resource-incomplete"));
    if (saw_undetermined)
        return prime_undetermined(
            a, judgment,
            undetermined_detail
                ? undetermined_detail
                : prime_expr1(a, "branch-typing-undetermined"));
    if (!bag.closure_certified)
        return prime_incomplete(
            a, judgment,
            prime_expr3(a, "may-has-no-witness-yet",
                        bag.incomplete_reason
                            ? bag.incomplete_reason
                            : prime_sym(a, "uncertified-producer-complete"),
                        atom_int(a, failure_count)));
    return prime_refuted(a, judgment,
                         prime_expr1(a, "no-value-branch-of-type"));
}

static Atom *prime_judge_raw(Arena *a, Space *space, Atom *judgment,
                             PrimeResourceLedger *ledger) {
    judgment = unquote_data(judgment);
    if (!judgment || judgment->kind != ATOM_EXPR || judgment->expr.len == 0 ||
        judgment->expr.elems[0]->kind != ATOM_SYMBOL) {
        return prime_refuted(a, judgment ? judgment : atom_unit(a),
                             prime_expr1(a, "malformed-judgment"));
    }
    const char *name = atom_name_cstr(judgment->expr.elems[0]);
    if (!name)
        return prime_refuted(a, judgment,
                             prime_expr1(a, "judgment-head-not-a-symbol"));

    if (strcmp(name, "Form") == 0) {
        if (judgment->expr.len != 2)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "Form-arity"));
        Atom *detail = NULL;
        PrimeFormStatus status = prime_form_type(
            space, a, judgment->expr.elems[1], ledger, &detail, NULL);
        if (status == PRIME_FORM_ESTABLISHED)
            return prime_established(a, judgment, detail);
        if (status == PRIME_FORM_REFUTED)
            return prime_refuted(a, judgment, detail);
        if (status == PRIME_FORM_INCOMPLETE)
            return prime_incomplete(a, judgment, detail);
        return prime_undetermined(a, judgment, detail);
    }

    if (strcmp(name, "Synth") == 0) {
        if (judgment->expr.len != 2)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "Synth-arity"));
        return prime_synth(space, a, judgment, judgment->expr.elems[1],
                           ledger);
    }

    if (strcmp(name, "Check") == 0 || strcmp(name, "Analyze") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(
                a, judgment,
                prime_expr1(a, strcmp(name, "Check") == 0
                                   ? "Check-arity"
                                   : "Analyze-arity"));
        return prime_check_or_analyze(
            space, a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], ledger, strcmp(name, "Check") == 0);
    }

    if (strcmp(name, "Convert") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "Convert-arity"));
        return prime_convert(space, a, judgment, judgment->expr.elems[1],
                             judgment->expr.elems[2], ledger);
    }

    if (strcmp(name, "Refine") == 0) {
        if (judgment->expr.len != 2)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "Refine-arity"));
        return prime_refine(space, a, judgment, judgment->expr.elems[1],
                            ledger);
    }

    if (strcmp(name, "May") == 0 || strcmp(name, "Must") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(
                a, judgment,
                prime_expr1(a, strcmp(name, "May") == 0
                                   ? "May-arity"
                                   : "Must-arity"));
        return prime_may_or_must(
            space, a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], ledger, strcmp(name, "Must") == 0);
    }

    return prime_undetermined(a, judgment,
                              prime_expr2(a, "unknown-judgment",
                                          judgment->expr.elems[0]));
}

static Atom *prime_judge(Arena *a, Space *space, Atom *judgment,
                         uint64_t fuel) {
    PrimeResourceLedger ledger;
    prime_resource_init(&ledger, fuel);
    Atom *verdict = prime_judge_raw(a, space, judgment, &ledger);
    return prime_attach_ledger(a, verdict, &ledger);
}

static const char *const PRIME_OP_NAMES[] = {"prime-package", "prime-judge"};

bool prime_semantics_is_op(const char *name) {
    if (!name) return false;
    for (size_t i = 0; i < sizeof PRIME_OP_NAMES / sizeof PRIME_OP_NAMES[0]; i++)
        if (strcmp(name, PRIME_OP_NAMES[i]) == 0) return true;
    return false;
}

bool prime_semantics_op_data_arg(const char *name, uint32_t arg_index) {
    return name && strcmp(name, "prime-judge") == 0 && arg_index == 1;
}

Atom *prime_semantics_dispatch(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs) {
    if (eval_current_language_id() != CETTA_LANGUAGE_PRIME ||
        !head || head->kind != ATOM_SYMBOL) {
        return NULL;
    }
    const char *name = atom_name_cstr(head);
    if (!prime_semantics_is_op(name)) return NULL;

    if (strcmp(name, "prime-package") == 0) {
        Atom *call = atom_expr(a, (Atom *[]){head}, 1);
        if (nargs != 1)
            return prime_refuted(a, call,
                                 prime_expr1(a, "prime-package-arity"));
        Space *space = NULL;
        if (!arg_space(args[0], &space))
            return prime_refuted(a, call,
                                 prime_expr1(a, "first-argument-not-a-space"));
        (void)space;
        return prime_package_atom(a);
    }

    if (strcmp(name, "prime-judge") == 0) {
        Atom *synthetic = atom_expr(a, (Atom *[]){head}, 1);
        if (nargs != 3)
            return prime_refuted(a, synthetic,
                                 prime_expr1(a, "prime-judge-arity"));
        Space *space = NULL;
        if (!arg_space(args[0], &space))
            return prime_refuted(a, unquote_data(args[1]),
                                 prime_expr1(a, "first-argument-not-a-space"));
        uint64_t fuel = PRIME_DEFAULT_FUEL;
        if (!arg_fuel(args[2], &fuel))
            return prime_refuted(a, unquote_data(args[1]),
                                 prime_expr1(a, "fuel-not-positive-integer"));
        return prime_judge(a, space, args[1], fuel);
    }

    return NULL;
}
