/* MeTTa-Prime v0: an executable semantic package over CeTTa's shared evaluator
 * and unified dependent type engine. Ordinary judgments run until completion;
 * callers may explicitly request a bounded, resource-reporting judgment. */

#include "prime_semantics.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abt.h"
#include "eval.h"
#include "generated/prime_nik_authorities_v1.generated.h"
#include "he_typing.h"
#include "nik_runtime.h"
#include "space.h"
#include "symbol.h"

#define PRIME_DEF_SCHEMA_VERSION 2
#define PRIME_DIALECT_MAJOR 0
#define PRIME_DIALECT_MINOR 3

static const char *const PRIME_JUDGMENT_NAMES[] = {
    "Form", "Synth", "Check", "Analyze", "Convert", "Refine", "May",
    "Must"};

static const char *const PRIME_RESULT_NAMES[] = {
    "Established", "Refuted", "Undetermined", "Incomplete"};

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

static Atom *prime_named_list(Arena *a, const char *head,
                              const char *const *names, size_t count) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * (count + 1u));
    items[0] = prime_sym(a, head);
    for (size_t i = 0; i < count; i++) items[i + 1u] = prime_sym(a, names[i]);
    return atom_expr(a, items, (CettaExprIndex)(count + 1u));
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

static Atom *prime_nik_authority_atom(
    Arena *a, const char *alias, const char *system_id,
    const char *revision, const char *digest) {
    Atom *items[5] = {
        prime_sym(a, "NIKAuthorityV1"),
        prime_sym(a, alias),
        atom_string(a, system_id),
        atom_string(a, revision),
        atom_string(a, digest)};
    return atom_expr(a, items, 5);
}

static Atom *prime_nik_catalog_atom(Arena *a) {
    size_t count = cetta_prime_nik_authorities_v1_count;
    Atom **items = arena_alloc(a, sizeof(*items) * (count + 2u));
    items[0] = prime_sym(a, "NIKAuthorityCatalogV1");
    items[1] = atom_string(
        a, cetta_prime_nik_authorities_v1_catalog_sha256);
    for (size_t index = 0u; index < count; index++) {
        const CettaNikAuthorityV1 *authority =
            &cetta_prime_nik_authorities_v1[index];
        items[index + 2u] = prime_nik_authority_atom(
            a, authority->alias, authority->system_id,
            authority->revision, authority->digest);
    }
    return atom_expr(a, items, (CettaExprIndex)(count + 2u));
}

static const char *prime_nik_horn_outcome_name(
    bool ran, CettaGsltHornOutcome outcome) {
    if (!ran)
        return "not-run";
    switch (outcome) {
    case CETTA_GSLT_HORN_COMPLETED:
        return "completed";
    case CETTA_GSLT_HORN_RULE_LIMIT:
        return "rule-limit";
    case CETTA_GSLT_HORN_ANSWER_LIMIT:
        return "answer-limit";
    case CETTA_GSLT_HORN_DEPTH_LIMIT:
        return "depth-limit";
    case CETTA_GSLT_HORN_FAULT:
        return "fault";
    }
    return "fault";
}

static Atom *prime_nik_attempt_count(Arena *a, uint64_t attempts) {
    return atom_int(
        a, attempts > (uint64_t)INT64_MAX
               ? INT64_MAX : (int64_t)attempts);
}

static Atom *prime_nik_receipt_atom(
    Arena *a, Atom *requested_authority,
    const CettaNikReceiptV1 *receipt, const char *diagnostic) {
    Atom *authority = requested_authority;
    if (receipt->authority_alias && receipt->system_id &&
        receipt->revision && receipt->authority_digest) {
        authority = prime_nik_authority_atom(
            a, receipt->authority_alias, receipt->system_id,
            receipt->revision, receipt->authority_digest);
    }
    Atom *native_items[4] = {
        prime_sym(a, "NativeReplay"),
        prime_sym(
            a, receipt->native_ran
                   ? cetta_inference_status_name(receipt->native_status)
                   : "not-run"),
        receipt->native_accepted ? atom_true(a) : atom_false(a),
        prime_nik_attempt_count(a, receipt->native_nodes)};
    Atom *reference_items[4] = {
        prime_sym(a, "HornReference"),
        prime_sym(
            a, prime_nik_horn_outcome_name(
                   receipt->reference_ran, receipt->reference_outcome)),
        receipt->reference_accepted ? atom_true(a) : atom_false(a),
        prime_nik_attempt_count(a, receipt->reference_rule_attempts)};
    Atom *compiled_items[4] = {
        prime_sym(a, "CompiledWorklist"),
        prime_sym(
            a, prime_nik_horn_outcome_name(
                   receipt->compiled_ran, receipt->compiled_outcome)),
        receipt->compiled_accepted ? atom_true(a) : atom_false(a),
        prime_nik_attempt_count(a, receipt->compiled_rule_attempts)};
    Atom *realizations_items[4] = {
        prime_sym(a, "Realizations"),
        atom_expr(a, native_items, 4),
        atom_expr(a, reference_items, 4),
        atom_expr(a, compiled_items, 4)};
    bool conclusive = receipt->outcome == CETTA_NIK_ACCEPTED ||
        receipt->outcome == CETTA_NIK_REJECTED;
    bool agreement = conclusive && receipt->native_ran &&
        receipt->reference_ran && receipt->compiled_ran &&
        receipt->native_accepted == receipt->reference_accepted &&
        receipt->reference_accepted == receipt->compiled_accepted;
    Atom *items[8] = {
        prime_sym(a, "NIKReceiptV1"),
        authority,
        prime_expr2(
            a, "Catalog",
            receipt->catalog_digest
                ? atom_string(a, receipt->catalog_digest)
                : prime_sym(a, "Unavailable")),
        prime_expr2(
            a, "Outcome", prime_sym(a, cetta_nik_outcome_name(receipt->outcome))),
        atom_expr(a, realizations_items, 4),
        prime_expr2(a, "Agreement",
                    agreement ? atom_true(a) : atom_false(a)),
        prime_expr2(a, "TotalWork",
                    prime_nik_attempt_count(a, receipt->total_work)),
        prime_expr2(
            a, "Diagnostic",
            diagnostic && diagnostic[0]
                ? atom_string(a, diagnostic)
                : prime_sym(a, "None"))};
    return atom_expr(a, items, 8);
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

static bool arg_budget(Atom *atom, uint64_t *out) {
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

static uint64_t prime_u64_add_sat(uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static const char *const PRIME_RESOURCE_PHASE_NAMES[] = {
    "formation", "synthesis", "normalization", "checking", "refinement",
    "evaluation"};

static void prime_resource_init(PrimeResourceLedger *ledger,
                                bool steps_limited, uint64_t steps) {
    memset(ledger, 0, sizeof(*ledger));
    if (steps_limited)
        he_typing_budget_init(&ledger->typing, steps);
    else
        he_typing_budget_init_unbounded(&ledger->typing);
    ledger->typing.allow_marked_user_type_functions = false;
}

static bool prime_resource_spend(PrimeResourceLedger *ledger,
                                 PrimeResourcePhase phase, uint64_t amount) {
    if (!ledger || phase >= PRIME_RESOURCE_PHASE_COUNT) return false;
    if (!ledger->typing.steps_limited) return true;
    if (ledger->typing.work_steps_observed > UINT64_MAX - amount)
        ledger->typing.work_steps_observed = UINT64_MAX;
    else
        ledger->typing.work_steps_observed += amount;
    ledger->phase_spent[phase] += amount;
    return true;
}

static uint64_t prime_resource_phase_begin(const PrimeResourceLedger *ledger) {
    return ledger && ledger->typing.steps_limited
        ? ledger->typing.work_steps_observed : 0;
}

static void prime_resource_phase_end(PrimeResourceLedger *ledger,
                                     PrimeResourcePhase phase,
                                     uint64_t before) {
    if (!ledger || phase >= PRIME_RESOURCE_PHASE_COUNT) return;
    if (!ledger->typing.steps_limited) return;
    uint64_t after = ledger->typing.work_steps_observed;
    if (after > before) ledger->phase_spent[phase] += after - before;
}

static Atom *prime_resource_ledger_atom(Arena *a,
                                        const PrimeResourceLedger *ledger) {
    uint64_t initial = ledger->typing.steps_initial;
    uint64_t remaining = ledger->typing.steps_remaining;
    Atom **phases = arena_alloc(
        a, sizeof(Atom *) * (PRIME_RESOURCE_PHASE_COUNT + 1u));
    phases[0] = prime_sym(a, "ObservedWork");
    for (uint32_t i = 0; i < PRIME_RESOURCE_PHASE_COUNT; i++) {
        phases[i + 1u] = prime_expr2(
            a, PRIME_RESOURCE_PHASE_NAMES[i],
            atom_int(a, (int64_t)ledger->phase_spent[i]));
    }
    Atom *limits_items[5] = {
        prime_sym(a, "DeclaredLimits"),
        prime_expr2(a, "structural-type-traversal",
                    prime_sym(a, "DynamicWorklists")),
        prime_expr2(a, "type-storage", prime_sym(a, "Dynamic")),
        prime_expr2(a, "applicability-storage", prime_sym(a, "Dynamic")),
        prime_expr2(a, "evaluator-stack-budget-bytes",
                    atom_int(a, (int64_t)
                        eval_current_c_stack_budget_bytes()))};
    Atom *observed_items[5] = {
        prime_sym(a, "Observed"),
        prime_expr2(a, "max-depth",
                    atom_int(a, ledger->typing.max_depth_observed)),
        prime_expr2(a, "type-storage-exhausted",
                    ledger->typing.type_capacity_exhausted
                        ? atom_true(a) : atom_false(a)),
        prime_expr2(a, "evaluator-stack-exhausted",
                    ledger->typing.evaluator_stack_exhausted
                        ? atom_true(a) : atom_false(a)),
        prime_expr2(a, "applicability-storage-exhausted",
                    ledger->typing.evaluator_capacity_exhausted
                        ? atom_true(a) : atom_false(a))};
    Atom *items[8] = {
        prime_sym(a, "ResourceLedgerV1"),
        prime_expr2(a, "mode", prime_sym(a, "ExplicitProducerBound")),
        prime_expr2(a, "producer-initial", atom_int(a, (int64_t)initial)),
        prime_expr2(a, "producer-spent",
                    atom_int(a, (int64_t)ledger->typing.steps_spent)),
        prime_expr2(a, "producer-remaining",
                    atom_int(a, (int64_t)remaining)),
        atom_expr(a, phases, PRIME_RESOURCE_PHASE_COUNT + 1u),
        atom_expr(a, limits_items, 5),
        atom_expr(a, observed_items, 5)};
    return atom_expr(a, items, 8);
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

Atom *prime_semantics_package_atom(Arena *a) {
    CettaHeTypingBudget declared_budget;
    he_typing_budget_init_unbounded(&declared_budget);

    Atom *identity_items[5] = {
        prime_sym(a, "PrimeIdentityV2"),
        prime_expr2(a, "Language", prime_sym(a, "prime")),
        prime_expr2(a, "LongName", prime_sym(a, "metta-prime")),
        prime_expr2(a, "SchemaVersion",
                    atom_int(a, PRIME_DEF_SCHEMA_VERSION)),
        atom_expr3(a, prime_sym(a, "DialectVersion"),
                   atom_int(a, PRIME_DIALECT_MAJOR),
                   atom_int(a, PRIME_DIALECT_MINOR))};
    Atom *identity = atom_expr(a, identity_items, 5);

    Atom *syntax_items[4] = {
        prime_sym(a, "SyntaxV1"), prime_sym(a, "HomoiconicSExpressions"),
        prime_sym(a, "ExplicitBangEvaluation"),
        prime_sym(a, "QuotedJudgmentData")};
    Atom *syntax = atom_expr(a, syntax_items, 4);
    Atom *binder_items[4] = {
        prime_sym(a, "BindersV1"), prime_sym(a, "NamedScopedVariables"),
        prime_sym(a, "InlineTypedTelescopeBinders"),
        prime_sym(a, "DeBruijnCanonicalBinders")};
    Atom *binders = atom_expr(a, binder_items, 4);
    Atom *equation_items[3] = {
        prime_sym(a, "EquationsV1"),
        prime_sym(a, "StructuralAtomIdentity"),
        prime_sym(a, "OrdinaryUserRulesExcludedFromDefinitionalEquality")};
    Atom *equations = atom_expr(a, equation_items, 3);
    Atom *runtime_r_items[4] = {
        prime_sym(a, "RuntimeR"), prime_sym(a, "SharedCeTTaReduction"),
        prime_sym(a, "Directional"), prime_sym(a, "Nondeterministic")};
    Atom *conversion_r_items[4] = {
        prime_sym(a, "ConversionR"),
        prime_sym(a, "TypePureGroundedFragment"),
        prime_expr2(a, "ReplaySchema",
                    prime_sym(a, "PrimeConversionCertificateV1")),
        prime_sym(a, "MarkedUserFunctionsRemainProvisional")};
    Atom *rewrite_items[3] = {
        prime_sym(a, "RewritesV1"), atom_expr(a, runtime_r_items, 4),
        atom_expr(a, conversion_r_items, 4)};
    Atom *rewrites = atom_expr(a, rewrite_items, 3);
    Atom *language_def_items[5] = {
        prime_sym(a, "LanguageDefV1"), syntax, binders, equations, rewrites};
    Atom *language_def = atom_expr(a, language_def_items, 5);

    const char *const variable_classes[] = {
        he_typing_variable_class_name(CETTA_HE_VAR_RIGID),
        he_typing_variable_class_name(CETTA_HE_VAR_SCHEME),
        he_typing_variable_class_name(CETTA_HE_VAR_ELABORATION)};
    Atom *marker_items[3] = {
        prime_sym(a, "SchemeMarkersV1"),
        prime_expr2(a, "ExplicitScheme", prime_sym(a, "type-scheme")),
        prime_expr2(a, "RuleScheme", prime_sym(a, "chaining-rule"))};
    Atom *substitution_items[5] = {
        prime_sym(a, "SubstitutionEvidenceV1"),
        prime_sym(a, "typed-answer-v2"),
        prime_sym(a, "query-substitution-v1"),
        prime_sym(a, "elaboration-substitution-v1"),
        prime_sym(a, "answer-constraints-v1")};
    Atom *context_items[7] = {
        prime_sym(a, "ContextsAndSchemesV1"),
        prime_named_list(a, "VariableClasses", variable_classes, 3),
        atom_expr(a, marker_items, 3),
        prime_sym(a, "UnmarkedOpenDeclarationsNotGeneralized"),
        prime_sym(a, "FreshElaborationVariables"),
        prime_sym(a, "RigidVariablesNeverSolved"),
        atom_expr(a, substitution_items, 5)};
    Atom *contexts = atom_expr(a, context_items, 7);

    Atom *judgments = prime_named_list(
        a, "Judgments", PRIME_JUDGMENT_NAMES,
        sizeof PRIME_JUDGMENT_NAMES / sizeof PRIME_JUDGMENT_NAMES[0]);
    const char *const edge_names[] = {
        he_typing_edge_name(CETTA_HE_EDGE_EXACT),
        he_typing_edge_name(CETTA_HE_EDGE_STRUCTURAL),
        he_typing_edge_name(CETTA_HE_EDGE_DYNAMIC),
        he_typing_edge_name(CETTA_HE_EDGE_TOP),
        he_typing_edge_name(CETTA_HE_EDGE_META_STAGING)};
    Atom *refinement_items[3] = {
        prime_sym(a, "RefinementRulesV1"),
        prime_expr2(a, "IndexRefinementMarker",
                    prime_sym(a, "type-index-refinement")),
        prime_expr2(a, "PredicateRequestMarker",
                    prime_sym(a, "type-level-function"))};
    Atom *dependent_checking_items[8] = {
        prime_sym(a, "PrimeDependentCheckingV1"), judgments,
        prime_named_list(a, "ConsistencyEdges", edge_names, 5),
        prime_named_list(a, "CheckedEdges", edge_names, 2),
        prime_sym(a, "DependentTelescopes"),
        prime_sym(a, "BidirectionalSynthesisAndChecking"),
        prime_expr2(a, "Convert",
                    prime_sym(a, "ComputedConversionEvidenceNotDefEq")),
        atom_expr(a, refinement_items, 3)};
    Atom *checking_items[5] = {
        prime_sym(a, "CheckingV1"),
        prime_expr2(a, "Gradual",
                    prime_sym(a, "UnannotatedProgramsUnchecked")),
        prime_expr2(a, "AuthorityIndexedJudgment", prime_sym(a, "Check")),
        prime_nik_catalog_atom(a),
        atom_expr(a, dependent_checking_items, 8)};
    Atom *checking = atom_expr(a, checking_items, 5);

    Atom *results = prime_named_list(
        a, "Results", PRIME_RESULT_NAMES,
        sizeof PRIME_RESULT_NAMES / sizeof PRIME_RESULT_NAMES[0]);

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
    Atom *nondet_items[7] = {
        prime_sym(a, "NondeterminismV1"),
        prime_sym(a, "ExplicitAnswerBags"),
        prime_sym(a, "CertifiedCompletionRequiredForTotality"),
        prime_sym(a, "PreserveFailures"),
        prime_sym(a, "PreserveDuplicates"),
        prime_sym(a, "BranchIndexedEvidence"),
        prime_sym(a, "RuntimeBagCorrespondenceOpen")};
    Atom *result_algebra_items[4] = {
        prime_sym(a, "ResultAlgebraV1"), results, information,
        atom_expr(a, nondet_items, 7)};
    Atom *result_algebra = atom_expr(a, result_algebra_items, 4);

    Atom *resource_items[11] = {
        prime_sym(a, "ResourcePolicyV1"),
        prime_sym(a, "NoImplicitStepBound"),
        prime_sym(a, "ExplicitProducerBudget"),
        prime_sym(a, "BoundedProducersReportResourceLedger"),
        prime_sym(a, "ResourceStatusVisibleWhenSemanticallyRelevant"),
        prime_sym(a, "UnboundedModeDoesNotMeterSteps"),
        prime_expr2(a, "StructuralTypeTraversal",
                    prime_sym(a, "DynamicWorklists")),
        prime_expr2(a, "TypeStorage", prime_sym(a, "Dynamic")),
        prime_expr2(a, "ApplicabilityStorage", prime_sym(a, "Dynamic")),
        prime_expr2(a, "EvaluatorStackBudget",
                    prime_sym(a, "RuntimeSelectedAndReported")),
        prime_sym(a, "TotalityRequiresCertifiedCompletion")};
    Atom *resources = atom_expr(a, resource_items, 11);
    Atom *effect_items[5] = {
        prime_sym(a, "EffectsV1"), prime_sym(a, "OrdinaryRuntimeEffects"),
        prime_sym(a, "SnapshotContainedMarkedTypeFunctions"),
        prime_sym(a, "DirectEffectfulTypeOperationsInadmissible"),
        prime_sym(a, "TransitiveEffectAdmissionOpen")};
    Atom *effects_resources_items[3] = {
        prime_sym(a, "EffectsAndResourcesV1"),
        atom_expr(a, effect_items, 5), resources};
    Atom *effects_resources = atom_expr(a, effects_resources_items, 3);

    Atom *evidence_items[13] = {
        prime_sym(a, "EvidenceSchemaV2"), prime_sym(a, "PrimeVerdict"),
        prime_sym(a, "PrimeEvidenceV1"), prime_sym(a, "ResourceLedgerV1"),
        prime_sym(a, "NIKReceiptV1"),
        prime_sym(a, "AuthorityBoundProofReplay"),
        prime_sym(a, "typed-answer-v2"),
        prime_sym(a, "answer-substitution-v2"),
        prime_sym(a, "SearchAndEvaluationAreUntrustedProducers"),
        prime_sym(a, "TypingRecheckedBeforeAcceptance"),
        prime_sym(a, "ConversionCertificatesReplayChecked"),
        prime_sym(a, "CertificateCorrespondenceOpen"),
        prime_sym(a, "SourcePackageHashRequiredExternally")};
    Atom *evidence = atom_expr(a, evidence_items, 13);

    Atom *package_items[8] = {
        prime_sym(a, "PrimeDefV2"), identity, language_def, contexts, checking,
        result_algebra, effects_resources, evidence};
    Atom *package = atom_expr(a, package_items, 8);
    return prime_semantics_validate_package(package) ? package : NULL;
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

static bool prime_schema_expr(Atom *atom, const char *head,
                              CettaExprLen len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
           is_symbol_named(atom->expr.elems[0], head);
}

static bool prime_exact_symbol_list(Atom *atom, const char *head,
                                    const char *const *names,
                                    size_t count) {
    if (!prime_schema_expr(atom, head, (CettaExprLen)(count + 1u)))
        return false;
    for (size_t i = 0; i < count; i++)
        if (!is_symbol_named(atom->expr.elems[i + 1u], names[i]))
            return false;
    return true;
}

static bool prime_symbol_field(Atom *atom, const char *head,
                               const char *value) {
    return prime_schema_expr(atom, head, 2) &&
           is_symbol_named(atom->expr.elems[1], value);
}

static bool prime_int_field(Atom *atom, const char *head, int64_t value) {
    return prime_schema_expr(atom, head, 2) &&
           atom->expr.elems[1]->kind == ATOM_GROUNDED &&
           atom->expr.elems[1]->ground.gkind == GV_INT &&
           atom->expr.elems[1]->ground.ival == value;
}

static bool prime_string_value(Atom *atom, const char *value) {
    return atom && atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_STRING && atom->ground.sval &&
           strcmp(atom->ground.sval, value) == 0;
}

static bool prime_nik_authority_valid(
    Atom *atom, const CettaNikAuthorityV1 *expected) {
    return prime_schema_expr(atom, "NIKAuthorityV1", 5) &&
           is_symbol_named(atom->expr.elems[1], expected->alias) &&
           prime_string_value(atom->expr.elems[2], expected->system_id) &&
           prime_string_value(atom->expr.elems[3], expected->revision) &&
           prime_string_value(atom->expr.elems[4], expected->digest);
}

static bool prime_nik_catalog_valid(Atom *catalog) {
    size_t count = cetta_prime_nik_authorities_v1_count;
    if (count > UINT32_MAX - 2u ||
        !prime_schema_expr(
            catalog, "NIKAuthorityCatalogV1",
            (CettaExprLen)(count + 2u)) ||
        !prime_string_value(
            catalog->expr.elems[1],
            cetta_prime_nik_authorities_v1_catalog_sha256)) {
        return false;
    }
    for (size_t index = 0u; index < count; index++) {
        if (!prime_nik_authority_valid(
                catalog->expr.elems[index + 2u],
                &cetta_prime_nik_authorities_v1[index])) {
            return false;
        }
    }
    return count >= 2u;
}

bool prime_semantics_validate_package(Atom *package) {
    static const char *const syntax_names[] = {
        "HomoiconicSExpressions", "ExplicitBangEvaluation",
        "QuotedJudgmentData"};
    static const char *const binder_names[] = {
        "NamedScopedVariables", "InlineTypedTelescopeBinders",
        "DeBruijnCanonicalBinders"};
    static const char *const equation_names[] = {
        "StructuralAtomIdentity",
        "OrdinaryUserRulesExcludedFromDefinitionalEquality"};
    static const char *const runtime_rewrite_names[] = {
        "SharedCeTTaReduction", "Directional", "Nondeterministic"};
    static const char *const variable_class_names[] = {
        "rigid", "scheme", "elaboration"};
    static const char *const consistency_edge_names[] = {
        "exact", "structural", "dynamic", "top", "meta-staging"};
    static const char *const checked_edge_names[] = {"exact", "structural"};
    static const char *const nondeterminism_names[] = {
        "ExplicitAnswerBags", "CertifiedCompletionRequiredForTotality",
        "PreserveFailures", "PreserveDuplicates", "BranchIndexedEvidence",
        "RuntimeBagCorrespondenceOpen"};
    static const char *const effect_names[] = {
        "OrdinaryRuntimeEffects", "SnapshotContainedMarkedTypeFunctions",
        "DirectEffectfulTypeOperationsInadmissible",
        "TransitiveEffectAdmissionOpen"};
    static const char *const evidence_names[] = {
        "PrimeVerdict", "PrimeEvidenceV1", "ResourceLedgerV1",
        "NIKReceiptV1", "AuthorityBoundProofReplay",
        "typed-answer-v2", "answer-substitution-v2",
        "SearchAndEvaluationAreUntrustedProducers",
        "TypingRecheckedBeforeAcceptance",
        "ConversionCertificatesReplayChecked",
        "CertificateCorrespondenceOpen", "SourcePackageHashRequiredExternally"};

    if (!prime_schema_expr(package, "PrimeDefV2", 8)) return false;

    Atom *identity = package->expr.elems[1];
    if (!prime_schema_expr(identity, "PrimeIdentityV2", 5) ||
        !prime_symbol_field(identity->expr.elems[1], "Language", "prime") ||
        !prime_symbol_field(identity->expr.elems[2], "LongName",
                            "metta-prime") ||
        !prime_int_field(identity->expr.elems[3], "SchemaVersion",
                         PRIME_DEF_SCHEMA_VERSION) ||
        !prime_schema_expr(identity->expr.elems[4], "DialectVersion", 3) ||
        identity->expr.elems[4]->expr.elems[1]->kind != ATOM_GROUNDED ||
        identity->expr.elems[4]->expr.elems[1]->ground.gkind != GV_INT ||
        identity->expr.elems[4]->expr.elems[1]->ground.ival !=
            PRIME_DIALECT_MAJOR ||
        identity->expr.elems[4]->expr.elems[2]->kind != ATOM_GROUNDED ||
        identity->expr.elems[4]->expr.elems[2]->ground.gkind != GV_INT ||
        identity->expr.elems[4]->expr.elems[2]->ground.ival !=
            PRIME_DIALECT_MINOR) {
        return false;
    }

    Atom *language = package->expr.elems[2];
    if (!prime_schema_expr(language, "LanguageDefV1", 5) ||
        !prime_exact_symbol_list(language->expr.elems[1], "SyntaxV1",
                                 syntax_names, 3) ||
        !prime_exact_symbol_list(language->expr.elems[2], "BindersV1",
                                 binder_names, 3) ||
        !prime_exact_symbol_list(language->expr.elems[3], "EquationsV1",
                                 equation_names, 2) ||
        !prime_schema_expr(language->expr.elems[4], "RewritesV1", 3)) {
        return false;
    }
    Atom *rewrites = language->expr.elems[4];
    if (!prime_exact_symbol_list(rewrites->expr.elems[1], "RuntimeR",
                                 runtime_rewrite_names, 3) ||
        !prime_schema_expr(rewrites->expr.elems[2], "ConversionR", 4) ||
        !is_symbol_named(rewrites->expr.elems[2]->expr.elems[1],
                         "TypePureGroundedFragment") ||
        !prime_symbol_field(rewrites->expr.elems[2]->expr.elems[2],
                            "ReplaySchema",
                            "PrimeConversionCertificateV1") ||
        !is_symbol_named(rewrites->expr.elems[2]->expr.elems[3],
                         "MarkedUserFunctionsRemainProvisional")) {
        return false;
    }

    Atom *contexts = package->expr.elems[3];
    if (!prime_schema_expr(contexts, "ContextsAndSchemesV1", 7) ||
        !prime_exact_symbol_list(contexts->expr.elems[1], "VariableClasses",
                                 variable_class_names, 3) ||
        !prime_schema_expr(contexts->expr.elems[2], "SchemeMarkersV1", 3) ||
        !prime_symbol_field(contexts->expr.elems[2]->expr.elems[1],
                            "ExplicitScheme", "type-scheme") ||
        !prime_symbol_field(contexts->expr.elems[2]->expr.elems[2],
                            "RuleScheme", "chaining-rule") ||
        !is_symbol_named(contexts->expr.elems[3],
                         "UnmarkedOpenDeclarationsNotGeneralized") ||
        !is_symbol_named(contexts->expr.elems[4],
                         "FreshElaborationVariables") ||
        !is_symbol_named(contexts->expr.elems[5],
                         "RigidVariablesNeverSolved") ||
        !prime_schema_expr(contexts->expr.elems[6],
                           "SubstitutionEvidenceV1", 5)) {
        return false;
    }

    Atom *checking = package->expr.elems[4];
    if (!prime_schema_expr(checking, "CheckingV1", 5) ||
        !prime_symbol_field(checking->expr.elems[1], "Gradual",
                            "UnannotatedProgramsUnchecked") ||
        !prime_symbol_field(checking->expr.elems[2],
                            "AuthorityIndexedJudgment", "Check") ||
        !prime_nik_catalog_valid(checking->expr.elems[3])) {
        return false;
    }
    Atom *dependent = checking->expr.elems[4];
    if (!prime_schema_expr(dependent, "PrimeDependentCheckingV1", 8) ||
        !prime_exact_symbol_list(dependent->expr.elems[1], "Judgments",
                                 PRIME_JUDGMENT_NAMES, 8) ||
        !prime_exact_symbol_list(dependent->expr.elems[2], "ConsistencyEdges",
                                 consistency_edge_names, 5) ||
        !prime_exact_symbol_list(dependent->expr.elems[3], "CheckedEdges",
                                 checked_edge_names, 2) ||
        !is_symbol_named(dependent->expr.elems[4], "DependentTelescopes") ||
        !is_symbol_named(dependent->expr.elems[5],
                         "BidirectionalSynthesisAndChecking") ||
        !prime_symbol_field(dependent->expr.elems[6], "Convert",
                            "ComputedConversionEvidenceNotDefEq") ||
        !prime_schema_expr(dependent->expr.elems[7],
                           "RefinementRulesV1", 3)) {
        return false;
    }

    Atom *algebra = package->expr.elems[5];
    if (!prime_schema_expr(algebra, "ResultAlgebraV1", 4) ||
        !prime_exact_symbol_list(algebra->expr.elems[1], "Results",
                                 PRIME_RESULT_NAMES, 4) ||
        !prime_schema_expr(algebra->expr.elems[2], "InformationOrderV1", 8) ||
        !prime_exact_symbol_list(algebra->expr.elems[3], "NondeterminismV1",
                                 nondeterminism_names, 6)) {
        return false;
    }

    Atom *effects_resources = package->expr.elems[6];
    if (!prime_schema_expr(effects_resources, "EffectsAndResourcesV1", 3) ||
        !prime_exact_symbol_list(effects_resources->expr.elems[1], "EffectsV1",
                                 effect_names, 4) ||
        !prime_schema_expr(effects_resources->expr.elems[2],
                           "ResourcePolicyV1", 11)) {
        return false;
    }
    Atom *resources = effects_resources->expr.elems[2];
    if (!is_symbol_named(resources->expr.elems[1], "NoImplicitStepBound") ||
        !is_symbol_named(resources->expr.elems[2], "ExplicitProducerBudget") ||
        !is_symbol_named(resources->expr.elems[3],
                         "BoundedProducersReportResourceLedger") ||
        !is_symbol_named(resources->expr.elems[4],
                         "ResourceStatusVisibleWhenSemanticallyRelevant") ||
        !is_symbol_named(resources->expr.elems[5],
                         "UnboundedModeDoesNotMeterSteps") ||
        !prime_symbol_field(resources->expr.elems[6],
                            "StructuralTypeTraversal",
                            "DynamicWorklists") ||
        !prime_symbol_field(resources->expr.elems[7], "TypeStorage",
                            "Dynamic") ||
        !prime_symbol_field(resources->expr.elems[8], "ApplicabilityStorage",
                            "Dynamic") ||
        !prime_symbol_field(resources->expr.elems[9],
                            "EvaluatorStackBudget",
                            "RuntimeSelectedAndReported") ||
        !is_symbol_named(resources->expr.elems[10],
                         "TotalityRequiresCertifiedCompletion")) {
        return false;
    }

    return prime_exact_symbol_list(package->expr.elems[7],
                                   "EvidenceSchemaV2", evidence_names, 12);
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

static uint32_t prime_infer_types(Space *space, Arena *a, Atom *term,
                                  PrimeResourceLedger *ledger,
                                  PrimeResourcePhase phase,
                                  bool structural, Atom ***types_out,
                                  bool *complete_out) {
    uint64_t before = prime_resource_phase_begin(ledger);
    CettaTypeInferenceBudget inference = {
        .steps_limited = ledger->typing.steps_limited,
        .steps_remaining = ledger->typing.steps_limited
            ? ledger->typing.steps_remaining : 0,
        .steps_spent = 0,
        .work_steps_observed = 0,
        .type_capacity = ledger->typing.type_capacity,
        .max_depth_observed = ledger->typing.max_depth_observed,
        .complete = true,
        .type_capacity_exhausted = false,
        .evaluator_stack_exhausted = false,
        .evaluator_capacity_exhausted = false,
        .allow_marked_user_type_functions = false,
    };
    uint32_t count = structural
        ? eval_get_atom_types_structural_profiled_budgeted(
              space, a, term, types_out, &inference)
        : eval_get_atom_types_profiled_budgeted(
              space, a, term, types_out, &inference);
    if (ledger->typing.steps_limited)
        ledger->typing.steps_remaining = inference.steps_remaining;
    if (ledger->typing.steps_limited) {
        if (ledger->typing.steps_spent > UINT64_MAX - inference.steps_spent)
            ledger->typing.steps_spent = UINT64_MAX;
        else
            ledger->typing.steps_spent += inference.steps_spent;
        if (ledger->typing.work_steps_observed >
            UINT64_MAX - inference.work_steps_observed) {
            ledger->typing.work_steps_observed = UINT64_MAX;
        } else {
            ledger->typing.work_steps_observed +=
                inference.work_steps_observed;
        }
    }
    if (inference.max_depth_observed > ledger->typing.max_depth_observed)
        ledger->typing.max_depth_observed = inference.max_depth_observed;
    if (inference.type_capacity_exhausted)
        ledger->typing.type_capacity_exhausted = true;
    if (inference.evaluator_stack_exhausted)
        ledger->typing.evaluator_stack_exhausted = true;
    if (inference.evaluator_capacity_exhausted)
        ledger->typing.evaluator_capacity_exhausted = true;
    prime_resource_phase_end(ledger, phase, before);
    if (complete_out) *complete_out = inference.complete;
    return count;
}

static bool inferred_as_type(Space *space, Arena *a, Atom *type,
                             PrimeResourceLedger *ledger,
                             bool *dynamic_only, bool *complete_out) {
    Atom **types = NULL;
    bool complete = true;
    uint32_t count = prime_infer_types(
        space, a, type, ledger, PRIME_RESOURCE_FORMATION, false, &types,
        &complete);
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
    if (complete_out) *complete_out = complete;
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

typedef struct {
    VarId id;
    uint64_t level;
    uint8_t state; /* 0 empty, 1 occupied, 2 tombstone */
} PrimeCanonicalSlot;

typedef struct {
    VarId id;
    uint64_t previous_level;
    bool named;
    bool had_previous;
} PrimeCanonicalFrame;

typedef struct {
    PrimeCanonicalSlot *slots;
    size_t slot_cap;
    size_t slot_count;
    size_t slot_used;
    PrimeCanonicalFrame *frames;
    size_t frame_len;
    size_t frame_cap;
    uint64_t depth;
} PrimeCanonicalScope;

static uint64_t prime_canonical_var_hash(VarId id) {
    uint64_t x = (uint64_t)id;
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static void prime_canonical_scope_init(PrimeCanonicalScope *scope) {
    memset(scope, 0, sizeof *scope);
}

static void prime_canonical_scope_free(PrimeCanonicalScope *scope) {
    free(scope->slots);
    free(scope->frames);
    memset(scope, 0, sizeof *scope);
}

static bool prime_canonical_scope_rehash(PrimeCanonicalScope *scope,
                                         size_t new_cap) {
    if (new_cap < 16u || (new_cap & (new_cap - 1u)) != 0u ||
        new_cap > SIZE_MAX / sizeof(*scope->slots))
        return false;
    PrimeCanonicalSlot *next = calloc(new_cap, sizeof(*next));
    if (!next) return false;
    for (size_t i = 0; i < scope->slot_cap; i++) {
        PrimeCanonicalSlot old = scope->slots[i];
        if (old.state != 1u) continue;
        size_t pos = (size_t)prime_canonical_var_hash(old.id) &
                     (new_cap - 1u);
        while (next[pos].state == 1u) pos = (pos + 1u) & (new_cap - 1u);
        next[pos] = old;
    }
    free(scope->slots);
    scope->slots = next;
    scope->slot_cap = new_cap;
    scope->slot_used = scope->slot_count;
    return true;
}

static PrimeCanonicalSlot *prime_canonical_scope_find(
        PrimeCanonicalScope *scope, VarId id, bool insert) {
    if (!scope->slot_cap) return NULL;
    size_t pos = (size_t)prime_canonical_var_hash(id) &
                 (scope->slot_cap - 1u);
    size_t tombstone = SIZE_MAX;
    for (size_t n = 0; n < scope->slot_cap; n++) {
        PrimeCanonicalSlot *slot = &scope->slots[pos];
        if (slot->state == 0u)
            return insert && tombstone != SIZE_MAX
                ? &scope->slots[tombstone] : slot;
        if (slot->state == 1u && slot->id == id) return slot;
        if (insert && slot->state == 2u && tombstone == SIZE_MAX)
            tombstone = pos;
        pos = (pos + 1u) & (scope->slot_cap - 1u);
    }
    return insert && tombstone != SIZE_MAX ? &scope->slots[tombstone] : NULL;
}

static bool prime_canonical_scope_reserve_frame(PrimeCanonicalScope *scope) {
    if (scope->frame_len < scope->frame_cap) return true;
    size_t next_cap = scope->frame_cap ? scope->frame_cap * 2u : 16u;
    if (next_cap <= scope->frame_cap ||
        next_cap > SIZE_MAX / sizeof(*scope->frames))
        return false;
    PrimeCanonicalFrame *next = realloc(
        scope->frames, sizeof(*next) * next_cap);
    if (!next) return false;
    scope->frames = next;
    scope->frame_cap = next_cap;
    return true;
}

static bool prime_canonical_scope_push(PrimeCanonicalScope *scope,
                                       Atom *binder) {
    if (!scope || scope->depth == UINT64_MAX ||
        (binder && binder->kind != ATOM_VAR) ||
        !prime_canonical_scope_reserve_frame(scope))
        return false;
    if (binder &&
        (!scope->slot_cap ||
         scope->slot_used >= scope->slot_cap - scope->slot_cap / 4u)) {
        size_t next_cap = scope->slot_cap ? scope->slot_cap * 2u : 16u;
        if (next_cap <= scope->slot_cap ||
            !prime_canonical_scope_rehash(scope, next_cap))
            return false;
    }

    PrimeCanonicalFrame frame = {0};
    frame.named = binder != NULL;
    if (binder) {
        PrimeCanonicalSlot *slot = prime_canonical_scope_find(
            scope, binder->var_id, true);
        if (!slot) return false;
        frame.id = binder->var_id;
        frame.had_previous = slot->state == 1u;
        frame.previous_level = frame.had_previous ? slot->level : 0u;
        if (slot->state == 0u) scope->slot_used++;
        if (slot->state != 1u) scope->slot_count++;
        slot->state = 1u;
        slot->id = binder->var_id;
        slot->level = scope->depth;
    }
    scope->frames[scope->frame_len++] = frame;
    scope->depth++;
    return true;
}

static void prime_canonical_scope_pop_to(PrimeCanonicalScope *scope,
                                         size_t frame_len) {
    while (scope->frame_len > frame_len) {
        PrimeCanonicalFrame frame = scope->frames[--scope->frame_len];
        scope->depth--;
        if (!frame.named) continue;
        PrimeCanonicalSlot *slot = prime_canonical_scope_find(
            scope, frame.id, false);
        if (!slot || slot->state != 1u) continue;
        if (frame.had_previous) {
            slot->level = frame.previous_level;
        } else {
            slot->state = 2u;
            scope->slot_count--;
        }
    }
}

static bool prime_canonical_scope_index(PrimeCanonicalScope *scope,
                                        VarId id, uint64_t *index) {
    PrimeCanonicalSlot *slot = prime_canonical_scope_find(scope, id, false);
    if (!slot || slot->state != 1u || slot->level >= scope->depth)
        return false;
    *index = scope->depth - slot->level - 1u;
    return true;
}

static Atom *prime_canonical_idx(Arena *a, uint64_t index) {
    if (index > INT64_MAX) return NULL;
    return atom_expr2(
        a, atom_symbol(a, "idx"), atom_int(a, (int64_t)index));
}

/* Lower the named Prime telescope surface to the neutral canonical ABT waist.
 * This is an elaboration mechanism, not a typing decision: callers must first
 * establish formation, and checked packages must still replay any judgment
 * made about the result.  A scoped level table makes name lookup constant-time;
 * the emitted syntax remains context-independent de Bruijn indices. */
static Atom *prime_canonicalize_type_rec(Arena *a,
                                         PrimeCanonicalScope *scope,
                                         Atom *type) {
    type = unquote_data(type);
    if (!type) return NULL;
    if (type->kind == ATOM_VAR) {
        uint64_t index = 0;
        return prime_canonical_scope_index(scope, type->var_id, &index)
            ? prime_canonical_idx(a, index) : NULL;
    }
    if (type->kind != ATOM_EXPR) return type;

    if (type->expr.len > 0 &&
        atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow)) {
        if (type->expr.len < 2) return NULL;
        CettaExprIndex arity = type->expr.len - 2u;
        if (!cetta_expr_len_mul_fits_size(arity, sizeof(Atom *))) return NULL;
        Atom **domains = arity
            ? arena_alloc(a, sizeof(*domains) * (size_t)arity) : NULL;
        size_t scope_mark = scope->frame_len;
        for (CettaExprIndex i = 0; i < arity; i++) {
            Atom *surface_domain = type->expr.elems[i + 1u];
            Atom *binder = NULL;
            if (surface_domain->kind == ATOM_EXPR &&
                surface_domain->expr.len == 3u &&
                atom_is_symbol_id(surface_domain->expr.elems[0],
                                  g_builtin_syms.colon)) {
                binder = surface_domain->expr.elems[1];
                if (binder->kind != ATOM_VAR) return NULL;
                surface_domain = surface_domain->expr.elems[2];
            }
            domains[i] = prime_canonicalize_type_rec(a, scope, surface_domain);
            if (!domains[i] || !prime_canonical_scope_push(scope, binder)) {
                prime_canonical_scope_pop_to(scope, scope_mark);
                return NULL;
            }
        }
        Atom *result = prime_canonicalize_type_rec(
            a, scope, type->expr.elems[type->expr.len - 1u]);
        prime_canonical_scope_pop_to(scope, scope_mark);
        if (!result) return NULL;
        for (CettaExprIndex i = arity; i > 0; i--)
            result = atom_expr3(
                a, atom_symbol(a, "Pi"), domains[i - 1u], result);
        return result;
    }

    Atom **elems = type->expr.len
        ? arena_alloc(a, sizeof(*elems) * (size_t)type->expr.len)
        : NULL;
    bool changed = false;
    for (CettaExprIndex i = 0; i < type->expr.len; i++) {
        elems[i] = prime_canonicalize_type_rec(a, scope, type->expr.elems[i]);
        if (!elems[i]) return NULL;
        if (elems[i] != type->expr.elems[i]) changed = true;
    }
    return changed ? atom_expr(a, elems, type->expr.len) : type;
}

Atom *prime_semantics_canonicalize_type(Arena *a, Atom *type) {
    if (!a || !type) return NULL;
    PrimeCanonicalScope scope;
    prime_canonical_scope_init(&scope);
    AbtSignature signature;
    abt_signature_init(&signature);
    if (!abt_signature_add_defaults(&signature, a)) {
        abt_signature_free(&signature);
        prime_canonical_scope_free(&scope);
        return NULL;
    }
    Atom *canonical = prime_canonicalize_type_rec(a, &scope, type);
    bool closed = canonical && scope.depth == 0u && scope.frame_len == 0u &&
                  !atom_has_vars(canonical) &&
                  abt_scope_check(&signature, 0u, canonical);
    abt_signature_free(&signature);
    prime_canonical_scope_free(&scope);
    return closed ? canonical : NULL;
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
        if (!context) {
            Atom *canonical = prime_semantics_canonicalize_type(a, type);
            if (!canonical) {
                *detail = prime_expr2(
                    a, "canonical-telescope-elaboration-failed", type);
                return PRIME_FORM_UNDETERMINED;
            }
            *detail = prime_expr3(
                a, "TelescopeFormation", type,
                prime_expr2(a, "CanonicalABT", canonical));
        } else {
            *detail = prime_expr2(a, "TelescopeFormation", type);
        }
        return PRIME_FORM_ESTABLISHED;
    }

    bool dynamic_only = false;
    if (!prime_all_vars_bound(type, context)) {
        *detail = prime_expr2(a, "unbound-type-variable", type);
        return PRIME_FORM_UNDETERMINED;
    }
    bool inference_complete = true;
    if (inferred_as_type(space, a, type, ledger, &dynamic_only,
                         &inference_complete)) {
        *detail = prime_expr2(a, "DeclaredTypeFormation", type);
        return PRIME_FORM_ESTABLISHED;
    }
    if (!inference_complete) {
        *detail = prime_expr2(a, "formation-inference-incomplete", type);
        return PRIME_FORM_INCOMPLETE;
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
    Atom **types = NULL;
    bool complete = true;
    uint32_t count = prime_infer_types(
        space, a, term, ledger, PRIME_RESOURCE_SYNTHESIS, false, &types,
        &complete);
    if (!complete) {
        Atom *reason = prime_expr2(
            a, ledger->typing.type_capacity_exhausted
                   ? "synthesis-type-capacity"
                   : "synthesis-prefix-incomplete",
            atom_int(a, count));
        free(types);
        return prime_incomplete(a, judgment, reason);
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
    case CETTA_HE_NORMALIZE_PROVISIONAL:
        return "normalization-unadmitted-user-rule";
    }
    return "normalization-undetermined";
}

static Atom *prime_conversion_certificate_atom(Arena *a, Atom *left,
                                               Atom *right, Atom *left_nf,
                                               Atom *right_nf, bool equal) {
    Atom *items[5] = {
        prime_sym(a, "PrimeConversionCertificateV1"),
        prime_expr3(a, "Original", left, right),
        prime_expr3(a, "NormalForms", left_nf, right_nf),
        prime_expr2(a, "Relation",
                    prime_sym(a, equal ? "Equal" : "Distinct")),
        prime_expr2(a, "Fragment",
                    prime_sym(a, "TypePureGroundedFragment"))};
    return atom_expr(a, items, 5);
}

static bool prime_replay_conversion_certificate_budgeted(
    Arena *a, Space *space, Atom *certificate, CettaHeTypingBudget *budget,
    bool *equal_out) {
    if (!a || !space || !budget ||
        !prime_schema_expr(certificate, "PrimeConversionCertificateV1", 5) ||
        !prime_schema_expr(certificate->expr.elems[1], "Original", 3) ||
        !prime_schema_expr(certificate->expr.elems[2], "NormalForms", 3) ||
        !prime_schema_expr(certificate->expr.elems[3], "Relation", 2) ||
        !prime_symbol_field(certificate->expr.elems[4], "Fragment",
                            "TypePureGroundedFragment")) {
        return false;
    }

    Atom *relation = certificate->expr.elems[3]->expr.elems[1];
    bool claims_equal = is_symbol_named(relation, "Equal");
    if (!claims_equal && !is_symbol_named(relation, "Distinct")) return false;

    Atom *left = certificate->expr.elems[1]->expr.elems[1];
    Atom *right = certificate->expr.elems[1]->expr.elems[2];
    Atom *claimed_left_nf = certificate->expr.elems[2]->expr.elems[1];
    Atom *claimed_right_nf = certificate->expr.elems[2]->expr.elems[2];
    Atom *left_nf = left;
    Atom *right_nf = right;

    bool prior_user_functions = budget->allow_marked_user_type_functions;
    budget->allow_marked_user_type_functions = false;
    CettaHeNormalizeStatus right_status =
        he_typing_normalize_type_status_budgeted(
            a, space, right, budget, &right_nf);
    CettaHeNormalizeStatus left_status =
        he_typing_normalize_type_status_budgeted(
            a, space, left, budget, &left_nf);
    budget->allow_marked_user_type_functions = prior_user_functions;

    if (left_status != CETTA_HE_NORMALIZE_COMPLETE ||
        right_status != CETTA_HE_NORMALIZE_COMPLETE ||
        !atom_eq(left_nf, claimed_left_nf) ||
        !atom_eq(right_nf, claimed_right_nf)) {
        return false;
    }
    bool equal = atom_eq(left_nf, right_nf);
    if (equal != claims_equal) return false;
    if (equal_out) *equal_out = equal;
    return true;
}

bool prime_semantics_replay_conversion_certificate(
    Arena *a, Space *space, Atom *certificate, bool *equal_out) {
    CettaHeTypingBudget budget;
    he_typing_budget_init_unbounded(&budget);
    budget.allow_marked_user_type_functions = false;
    return prime_replay_conversion_certificate_budgeted(
        a, space, certificate, &budget, equal_out);
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
    bool equal = atom_eq(left_nf, right_nf);
    Atom *certificate = prime_conversion_certificate_atom(
        a, left, right, left_nf, right_nf, equal);
    bool replay_equal = false;
    uint64_t replay_before = prime_resource_phase_begin(ledger);
    bool replayed = prime_replay_conversion_certificate_budgeted(
        a, space, certificate, &ledger->typing, &replay_equal);
    prime_resource_phase_end(ledger, PRIME_RESOURCE_NORMALIZATION,
                             replay_before);
    if (!replayed || replay_equal != equal)
        return prime_undetermined(
            a, judgment,
            prime_expr1(a, "conversion-certificate-replay-failed"));
    return equal
        ? prime_established(a, judgment, certificate)
        : prime_refuted(a, judgment, certificate);
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

    bool evaluation_limited = ledger->typing.steps_limited;
    uint64_t evaluation_budget = evaluation_limited
        ? ledger->typing.steps_remaining : 0;
    if (evaluation_limited && evaluation_budget == 0) {
        out->closure_certified = false;
        out->incomplete_reason = prime_sym(a, "fuel-exhausted");
        return true;
    }
    EvalOutcome outcome;
    eval_outcome_init(&outcome);
    int budget = evaluation_limited ? (int)evaluation_budget : -1;
    metta_eval_outcome(space, a, NULL, source->expr.elems[1], budget,
                       &outcome);
    uint64_t spent = outcome.steps_spent;
    if (ledger->typing.steps_limited) {
        if (spent >= ledger->typing.steps_remaining)
            ledger->typing.steps_remaining = 0;
        else
            ledger->typing.steps_remaining -= spent;
    }
    if (ledger->typing.steps_limited) {
        if (ledger->typing.steps_spent > UINT64_MAX - spent)
            ledger->typing.steps_spent = UINT64_MAX;
        else
            ledger->typing.steps_spent += spent;
        if (ledger->typing.work_steps_observed > UINT64_MAX - spent)
            ledger->typing.work_steps_observed = UINT64_MAX;
        else
            ledger->typing.work_steps_observed += spent;
        ledger->phase_spent[PRIME_RESOURCE_EVALUATION] += spent;
    }

    out->branches = arena_alloc(
        a, sizeof(Atom *) * (outcome.results.len > 0 ? outcome.results.len : 1));
    for (CettaCount i = 0; i < outcome.results.len; i++)
        out->branches[i] = outcome.results.items[i];
    out->branch_count = (uint32_t)outcome.results.len;
    out->branches_wrapped = false;
    out->closure_certified = outcome.completion == CETTA_EVAL_COMPLETE;
    if (!out->closure_certified) {
        if (outcome.completion == CETTA_EVAL_INCOMPLETE_STACK)
            ledger->typing.evaluator_stack_exhausted = true;
        if (outcome.completion == CETTA_EVAL_INCOMPLETE_CAPACITY)
            ledger->typing.evaluator_capacity_exhausted = true;
        out->incomplete_reason =
            prime_sym(a, eval_completion_reason(outcome.completion));
    }
    eval_outcome_free(&outcome);
    return true;
}

static bool parse_answer_branch(const PrimeAnswerBag *bag, Atom *branch,
                                bool *is_value, Atom **payload) {
    if (!bag->branches_wrapped) {
        *is_value = !atom_is_error(branch);
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

static void prime_account_nik_work(
    PrimeResourceLedger *ledger, uint64_t work) {
    if (!ledger->typing.steps_limited)
        return;
    uint64_t spent = work > ledger->typing.steps_remaining
        ? ledger->typing.steps_remaining : work;
    ledger->typing.steps_remaining -= spent;
    ledger->typing.steps_spent = prime_u64_add_sat(
        ledger->typing.steps_spent, spent);
    ledger->typing.work_steps_observed = prime_u64_add_sat(
        ledger->typing.work_steps_observed, spent);
    ledger->phase_spent[PRIME_RESOURCE_CHECKING] = prime_u64_add_sat(
        ledger->phase_spent[PRIME_RESOURCE_CHECKING], spent);
}

static Atom *prime_nik_check(
    Arena *a, Atom *judgment, Atom *authority,
    Atom *claim, Atom *proof, PrimeResourceLedger *ledger) {
    if (!authority || authority->kind != ATOM_SYMBOL) {
        return prime_undetermined(
            a, judgment, prime_expr2(a, "NIKMalformedAuthority", authority));
    }
    if (ledger->typing.steps_limited &&
        ledger->typing.steps_remaining == 0u) {
        return prime_incomplete(
            a, judgment, prime_expr1(a, "nik-work-limit-exhausted"));
    }

    CettaNikLimits limits = {0};
    if (ledger->typing.steps_limited)
        limits.max_total_work = ledger->typing.steps_remaining;
    CettaNikReceiptV1 receipt;
    char diagnostic[512] = {0};
    CettaNikOutcome outcome = cetta_nik_check_v1(
        atom_name_cstr(authority), claim, proof, limits, a, &receipt,
        diagnostic, sizeof(diagnostic));
    prime_account_nik_work(ledger, receipt.total_work);
    Atom *evidence = prime_nik_receipt_atom(
        a, authority, &receipt, diagnostic);

    switch (outcome) {
    case CETTA_NIK_ACCEPTED:
        return prime_established(a, judgment, evidence);
    case CETTA_NIK_REJECTED:
        return prime_refuted(a, judgment, evidence);
    case CETTA_NIK_INCOMPLETE:
        return prime_incomplete(a, judgment, evidence);
    case CETTA_NIK_MALFORMED:
    case CETTA_NIK_UNSUPPORTED:
    case CETTA_NIK_FAULT:
        return prime_undetermined(a, judgment, evidence);
    }
    return prime_undetermined(a, judgment, evidence);
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

    if (strcmp(name, "Check") == 0) {
        if (judgment->expr.len == 4)
            return prime_nik_check(
                a, judgment, judgment->expr.elems[1],
                judgment->expr.elems[2], judgment->expr.elems[3], ledger);
        if (judgment->expr.len != 3)
            return prime_refuted(
                a, judgment, prime_expr1(a, "Check-arity"));
        return prime_check_or_analyze(
            space, a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], ledger, true);
    }

    if (strcmp(name, "Analyze") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(
                a, judgment, prime_expr1(a, "Analyze-arity"));
        return prime_check_or_analyze(
            space, a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], ledger, false);
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
                         bool steps_limited, uint64_t steps) {
    PrimeResourceLedger ledger;
    prime_resource_init(&ledger, steps_limited, steps);
    Atom *verdict = prime_judge_raw(a, space, judgment, &ledger);
    return steps_limited ? prime_attach_ledger(a, verdict, &ledger) : verdict;
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
        Atom *package = prime_semantics_package_atom(a);
        return package
            ? package
            : prime_refuted(a, call,
                            prime_expr1(a, "prime-package-invalid"));
    }

    if (strcmp(name, "prime-judge") == 0) {
        Atom *synthetic = atom_expr(a, (Atom *[]){head}, 1);
        if (nargs != 2 && nargs != 3)
            return prime_refuted(a, synthetic,
                                 prime_expr1(a, "prime-judge-arity"));
        Space *space = NULL;
        if (!arg_space(args[0], &space))
            return prime_refuted(a, unquote_data(args[1]),
                                 prime_expr1(a, "first-argument-not-a-space"));
        bool steps_limited = nargs == 3;
        uint64_t steps = 0;
        if (steps_limited && !arg_budget(args[2], &steps))
            return prime_refuted(a, unquote_data(args[1]),
                                 prime_expr1(a, "budget-not-positive-integer"));
        return prime_judge(a, space, args[1], steps_limited, steps);
    }

    return NULL;
}
