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
#include "generated/prime_typing_open_regular_kernel_source_binding_v1.generated.h"
#include "he_typing_authority.h"
#include "library.h"
#include "nik_runtime.h"
#include "prime_regular_kernel.h"
#include "prime_regular_kernel_admission.h"
#include "prime_regular_pattern.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"

#define PRIME_DEF_SCHEMA_VERSION 3
#define PRIME_DIALECT_MAJOR 0
#define PRIME_DIALECT_MINOR 3

const CettaNikDirectAuthorityV1
    cetta_prime_typing_direct_authority_v1 = {
        .alias = "PRIME-TYPING",
        .system_id = "prime.typing",
        .authority_identity = UINT64_C(0x7072696d652e7479),
        .realization_identity = UINT64_C(0x63657474612e7072),
        .authority_revision = 1u,
        .realization_abi = 2u,
    };

static const char *const PRIME_JUDGMENT_NAMES[] = {
    "type:formed", "type:of", "type:check", "type:analyze", "type:eq",
    "type:refine", "type:may", "type:must"};

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
    bool agreement_checked = receipt->native_ran &&
        receipt->reference_ran && receipt->compiled_ran;
    bool agreement = agreement_checked &&
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
        prime_expr2(
            a, "Agreement",
            agreement_checked
                ? (agreement ? atom_true(a) : atom_false(a))
                : prime_sym(a, "not-run")),
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

static CettaPrimeRegularKernelBudget prime_regular_kernel_budget(
    const PrimeResourceLedger *ledger) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(
        &budget, ledger->typing.steps_limited,
        ledger->typing.steps_limited ? ledger->typing.steps_remaining : 0u);
    return budget;
}

static void prime_account_regular_kernel(
    PrimeResourceLedger *ledger, PrimeResourcePhase phase,
    const CettaPrimeRegularKernelBudget *budget) {
    if (!ledger || !budget || !ledger->typing.steps_limited) return;
    ledger->typing.steps_remaining = budget->remaining;
    ledger->typing.steps_spent = prime_u64_add_sat(
        ledger->typing.steps_spent, budget->spent);
    ledger->typing.work_steps_observed = prime_u64_add_sat(
        ledger->typing.work_steps_observed, budget->spent);
    ledger->phase_spent[phase] = prime_u64_add_sat(
        ledger->phase_spent[phase], budget->spent);
}

static Atom *prime_regular_kernel_reason(
    Arena *arena, const CettaPrimeRegularKernelResult *result,
    const char *fallback) {
    return prime_expr1(
        arena, result && result->reason ? result->reason : fallback);
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
        prime_expr2(a, "ConversionEvidence",
                    prime_sym(a, "ComputedConversionEvidenceNotDefEq")),
        atom_expr(a, refinement_items, 3)};
    Atom *checking_items[5] = {
        prime_sym(a, "CheckingV1"),
        prime_expr2(a, "Gradual",
                    prime_sym(a, "UnannotatedProgramsUnchecked")),
        prime_expr2(
            a, "AuthorityIndexedJudgment", prime_sym(a, "type:check")),
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
        prime_sym(a, "ConversionEvidenceComputedOnce"),
        prime_sym(a, "CertificateCorrespondenceOpen"),
        prime_sym(a, "SourcePackageHashRequiredExternally")};
    Atom *evidence = atom_expr(a, evidence_items, 13);

    Atom *package_items[8] = {
        prime_sym(a, "PrimeDefV3"), identity, language_def, contexts, checking,
        result_algebra, effects_resources, evidence};
    Atom *package = atom_expr(a, package_items, 8);
    return prime_semantics_validate_package(package) ? package : NULL;
}

typedef enum {
    PRIME_FORM_ESTABLISHED = 0,
    PRIME_FORM_REFUTED,
    PRIME_FORM_UNDETERMINED,
    PRIME_FORM_INCOMPLETE,
    PRIME_FORM_FAULT
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
        "ConversionEvidenceComputedOnce",
        "CertificateCorrespondenceOpen", "SourcePackageHashRequiredExternally"};

    if (!prime_schema_expr(package, "PrimeDefV3", 8)) return false;

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
                            "AuthorityIndexedJudgment", "type:check") ||
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
        !prime_symbol_field(dependent->expr.elems[6], "ConversionEvidence",
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

/* Lower the named Prime telescope syntax to the neutral canonical ABT waist.
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
            Atom *syntax_domain = type->expr.elems[i + 1u];
            Atom *binder = NULL;
            if (syntax_domain->kind == ATOM_EXPR &&
                syntax_domain->expr.len == 3u &&
                atom_is_symbol_id(syntax_domain->expr.elems[0],
                                  g_builtin_syms.colon)) {
                binder = syntax_domain->expr.elems[1];
                if (binder->kind != ATOM_VAR) return NULL;
                syntax_domain = syntax_domain->expr.elems[2];
            }
            domains[i] = prime_canonicalize_type_rec(a, scope, syntax_domain);
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

static Atom *prime_synth_closed_regular(
    Space *space, Arena *a, Atom *judgment, Atom *term,
    PrimeResourceLedger *ledger, bool *engine_fault_out) {
    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    CettaPrimeRegularKernelAdmittedSynthesisDecisionV1 decision =
        cetta_prime_regular_kernel_resolve_closed_synthesis_v1(
            a, space, term, &budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED) {
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_SYNTHESIS, &budget);
        CettaPrimeRegularKernelResult incomplete = {
            .status = CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .reason = decision.reason,
        };
        return prime_incomplete(
            a, judgment,
            prime_regular_kernel_reason(
                a, &incomplete, "regular-kernel-synthesis-incomplete"));
    }
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE) {
        if (engine_fault_out) *engine_fault_out = true;
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_SYNTHESIS, &budget);
        CettaPrimeRegularKernelResult failure = {
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = decision.reason,
        };
        return prime_undetermined(
            a, judgment,
            prime_regular_kernel_reason(
                a, &failure, "regular-kernel-synthesis-engine-failure"));
    }
    if (decision.status != CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED)
        return NULL;

    prime_account_regular_kernel(
        ledger, PRIME_RESOURCE_SYNTHESIS, &budget);
    if (decision.judgment_status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        Atom *type = term_universe_copy_atom(
            space->native.universe, a, decision.type_id);
        if (!type) return NULL;
        return prime_established(
            a, judgment, prime_expr2(a, "PrimeRegularSynthesis", type));
    }
    if (decision.judgment_status == CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
        CettaPrimeRegularKernelResult refuted = {
            .status = CETTA_PRIME_REGULAR_KERNEL_REFUTED,
            .reason = decision.reason,
        };
        return prime_refuted(
            a, judgment,
            prime_regular_kernel_reason(
                a, &refuted, "regular-kernel-synthesis-refuted"));
    }
    return NULL;
}

static Atom *prime_synth_authored_regular(
    Space *space, Arena *arena, Atom *judgment, Atom *term,
    PrimeResourceLedger *ledger, bool *engine_fault_out,
    Atom **canonical_term_out);

static Atom *prime_synth_declared_regular(
    Space *space, Arena *arena, Atom *judgment, Atom *term,
    PrimeResourceLedger *ledger, bool *engine_fault_out,
    Atom **canonical_term_out);

static Atom *prime_synth(Space *space, Arena *a, Atom *judgment, Atom *term,
                         PrimeResourceLedger *ledger,
                         CettaPrimeTypingRouteV1 *route_out,
                         bool *engine_fault_out,
                         Atom **canonical_term_out) {
    if (route_out) *route_out = CETTA_PRIME_TYPING_ROUTE_NONE;
    if (engine_fault_out) *engine_fault_out = false;
    if (canonical_term_out) *canonical_term_out = NULL;
    if (cetta_prime_regular_kernel_unwrap_scoped(term, NULL, NULL)) {
        CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
        CettaPrimeRegularKernelResult result = cetta_prime_regular_kernel_synth(
            a, term, &budget);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_SYNTHESIS, &budget);
        if (result.status != CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
            result.status != CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED &&
            route_out)
            *route_out = CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR;
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            if (canonical_term_out) *canonical_term_out = term;
            return prime_established(
                a, judgment,
                prime_expr2(a, "PrimeRegularSynthesis", result.type));
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
            return prime_incomplete(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-synthesis-incomplete"));
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE) {
            if (engine_fault_out) *engine_fault_out = true;
            return prime_undetermined(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-synthesis-engine-failure"));
        }
        if (result.status != CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
            result.status != CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED) {
            return prime_refuted(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-synthesis-refuted"));
        }
    }
    /* Try the most contextual presentation first.  The authored recognizer
     * has an empty environment, so letting it see `refl p` first would hide
     * an otherwise exact declaration-context judgment. */
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        Atom *declared = prime_synth_declared_regular(
            space, a, judgment, term, ledger, engine_fault_out,
            canonical_term_out);
        if (declared) {
            if (route_out)
                *route_out = CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR;
            return declared;
        }
    }
    if (cetta_prime_regular_term_maybe_syntax_v1(term) &&
        CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        Atom *native = prime_synth_authored_regular(
            space, a, judgment, term, ledger, engine_fault_out,
            canonical_term_out);
        if (native) {
            if (route_out)
                *route_out = CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR;
            return native;
        }
    }
    if (cetta_prime_regular_kernel_term_maybe_syntax(term) &&
        CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        Atom *native = prime_synth_closed_regular(
            space, a, judgment, term, ledger, engine_fault_out);
        if (native) {
            if (route_out)
                *route_out = CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR;
            if (canonical_term_out && native->kind == ATOM_EXPR &&
                native->expr.len == 4u &&
                is_symbol_named(native->expr.elems[1], "Established")) {
                *canonical_term_out = term;
            }
            return native;
        }
    }
    if (route_out) *route_out = CETTA_PRIME_TYPING_ROUTE_LEGACY_HE;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_SYNTHESIS);
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

static PrimeFormStatus prime_form_scoped_regular_type(
    Arena *arena, Atom *type, PrimeResourceLedger *ledger,
    Atom **detail, bool *owned) {
    if (owned) *owned = false;
    if (!arena || !type || !ledger || !detail || !owned ||
        !cetta_prime_regular_kernel_unwrap_scoped(type, NULL, NULL)) {
        return PRIME_FORM_UNDETERMINED;
    }

    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    CettaPrimeRegularKernelResult synthesis =
        cetta_prime_regular_kernel_synth(arena, type, &budget);
    prime_account_regular_kernel(
        ledger, PRIME_RESOURCE_FORMATION, &budget);
    if (synthesis.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS ||
        synthesis.status == CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED) {
        return PRIME_FORM_UNDETERMINED;
    }

    *owned = true;
    if (synthesis.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        *detail = prime_regular_kernel_reason(
            arena, &synthesis, "regular-kernel-scoped-formation-incomplete");
        return PRIME_FORM_INCOMPLETE;
    }
    if (synthesis.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE) {
        *detail = prime_regular_kernel_reason(
            arena, &synthesis,
            "regular-kernel-scoped-formation-engine-failure");
        return PRIME_FORM_FAULT;
    }
    if (synthesis.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
        *detail = prime_regular_kernel_reason(
            arena, &synthesis, "regular-kernel-scoped-formation-refuted");
        return PRIME_FORM_REFUTED;
    }
    if (synthesis.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        *detail = prime_regular_kernel_reason(
            arena, &synthesis, "regular-kernel-scoped-formation-boundary");
        return PRIME_FORM_UNDETERMINED;
    }
    if (!cetta_prime_regular_kernel_term_is_universe_sort_v1(
            synthesis.type)) {
        /* The scoped term is typed, but its inferred type is not a universe
         * sort, so it cannot be reinterpreted as a formed type. */
        *detail = prime_expr3(
            arena, "PrimeRegularExpectedTypeBoundary", type,
            synthesis.type ? synthesis.type : prime_sym(arena, "missing-type"));
        return PRIME_FORM_UNDETERMINED;
    }
    *detail = prime_expr2(arena, "PrimeRegularTypeFormation", type);
    return PRIME_FORM_ESTABLISHED;
}

static PrimeFormStatus prime_form_closed_regular_type(
    Space *space, Arena *a, Atom *expected,
    PrimeResourceLedger *ledger, Atom **detail, bool *owned,
    bool allow_top_sort) {
    if (owned) *owned = false;
    if (!space || !expected || !detail || !owned ||
        !cetta_prime_regular_kernel_term_maybe_syntax(expected) ||
        !CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        return PRIME_FORM_UNDETERMINED;
    }
    if (allow_top_sort && is_symbol_named(expected, "U1")) {
        *owned = true;
        *detail = prime_expr2(a, "PrimeRegularTypeFormation", expected);
        return PRIME_FORM_ESTABLISHED;
    }

    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    CettaPrimeRegularKernelAdmittedSynthesisDecisionV1 synthesis =
        cetta_prime_regular_kernel_resolve_closed_synthesis_v1(
            a, space, expected, &budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    if (synthesis.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT ||
        synthesis.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID) {
        return PRIME_FORM_UNDETERMINED;
    }
    *owned = true;
    prime_account_regular_kernel(
        ledger, PRIME_RESOURCE_FORMATION, &budget);
    if (synthesis.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED) {
        CettaPrimeRegularKernelResult result = {
            .status = CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .reason = synthesis.reason,
        };
        *detail = prime_regular_kernel_reason(
            a, &result, "regular-kernel-formation-incomplete");
        return PRIME_FORM_INCOMPLETE;
    }
    if (synthesis.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE) {
        CettaPrimeRegularKernelResult result = {
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = synthesis.reason,
        };
        *detail = prime_regular_kernel_reason(
            a, &result, "regular-kernel-formation-engine-failure");
        return PRIME_FORM_FAULT;
    }
    if (synthesis.judgment_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        CettaPrimeRegularKernelResult result = {
            .status = synthesis.judgment_status,
            .reason = synthesis.reason,
        };
        *detail = prime_regular_kernel_reason(
            a, &result, "regular-kernel-expected-not-a-type");
        return PRIME_FORM_REFUTED;
    }
    Atom *inferred = term_universe_get_atom(
        space->native.universe, synthesis.type_id);
    if (!cetta_prime_regular_kernel_term_is_universe_sort_v1(inferred)) {
        *detail = prime_expr3(
            a, "PrimeRegularExpectedTypeMismatch",
            expected, inferred ? inferred : prime_sym(a, "missing-type"));
        return PRIME_FORM_REFUTED;
    }
    *detail = prime_expr2(a, "PrimeRegularTypeFormation", expected);
    return PRIME_FORM_ESTABLISHED;
}

static CettaPrimeRegularKernelAdmittedCheckingDecisionV1
prime_resolve_closed_regular_check(
    Space *space, Arena *a, Atom *term, Atom *expected,
    PrimeResourceLedger *ledger) {
    if (!cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(term) ||
        !cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(expected) ||
        !CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        return (CettaPrimeRegularKernelAdmittedCheckingDecisionV1){
            .status = CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT,
        };
    }
    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    CettaPrimeRegularKernelAdmittedCheckingDecisionV1 decision =
        cetta_prime_regular_kernel_resolve_closed_checking_v1(
            a, space, term, expected, &budget,
            cetta_prime_regular_kernel_closed_checking_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED ||
        decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED ||
        decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE) {
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_CHECKING, &budget);
    }
    return decision;
}

static const char *prime_regular_term_syntax_name(
    CettaPrimeRegularTermSyntaxErrorV1 error) {
    switch (error) {
    case CETTA_PRIME_REGULAR_TERM_WRONG_ARITY:
        return "regular-syntax-wrong-arity";
    case CETTA_PRIME_REGULAR_TERM_EMPTY_BINDER_LIST:
        return "regular-syntax-empty-binder-list";
    case CETTA_PRIME_REGULAR_TERM_INVALID_BINDER_NAME:
        return "regular-syntax-invalid-binder-name";
    case CETTA_PRIME_REGULAR_TERM_MATCHER_BINDER:
        return "regular-syntax-matcher-is-not-lexical-binder";
    case CETTA_PRIME_REGULAR_TERM_BINDER_TYPE_ARITY_MISMATCH:
        return "regular-syntax-binder-type-arity-mismatch";
    case CETTA_PRIME_REGULAR_TERM_TYPED_BINDER_REQUIRES_AUTHORITY:
        return "regular-syntax-typed-binder-not-yet-authorized";
    case CETTA_PRIME_REGULAR_TERM_INVALID_INDEX:
        return "regular-syntax-invalid-index";
    case CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL:
        return "regular-syntax-invalid-level";
    case CETTA_PRIME_REGULAR_TERM_SYNTAX_NONE:
        break;
    }
    return "regular-syntax-syntax-error";
}

static Atom *prime_regular_term_failure_detail(
    Arena *arena, const CettaPrimeRegularTermCheckV1 *result) {
    const char *phase = "regular-syntax";
    switch (result->phase) {
    case CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_SYNTAX:
        phase = "expected-syntax";
        break;
    case CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_PATTERN:
        phase = "expected-pattern";
        break;
    case CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_FORMATION:
        phase = "expected-formation";
        break;
    case CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX:
        phase = "term-syntax";
        break;
    case CETTA_PRIME_REGULAR_TERM_PHASE_TERM_PATTERN:
        phase = "term-pattern";
        break;
    case CETTA_PRIME_REGULAR_TERM_PHASE_TERM_TYPING:
        phase = "term-typing";
        break;
    case CETTA_PRIME_REGULAR_TERM_PHASE_NONE:
        break;
    }
    const char *reason = result->syntax.reason;
    if (result->syntax.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR)
        reason = prime_regular_term_syntax_name(
            result->syntax.syntax_error);
    if (!reason) reason = result->pattern.reason;
    if (!reason) reason = result->judgment.reason;
    return prime_expr3(
        arena, "PrimeRegularTermDiagnostic",
        prime_sym(arena, phase),
        prime_sym(arena, reason ? reason : "regular-syntax-undetermined"));
}

typedef struct {
    bool owned;
    CettaPrimeRegularKernelStatus status;
    Atom *term;
    Atom *detail;
} PrimeAuthoredRegularElaboration;

static bool prime_regular_pattern_error_is_out_of_class(
    CettaPrimeRegularPatternSyntaxErrorV1 error) {
    return error == CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_MULTI_BINDER ||
           error ==
               CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_EXPLICIT_SUBSTITUTION ||
           error == CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_COLLECTION;
}

static PrimeAuthoredRegularElaboration
prime_elaborate_authored_regular(
    Arena *arena, Atom *syntax, PrimeResourceLedger *ledger,
    PrimeResourcePhase resource_phase,
    CettaPrimeRegularTermCheckPhaseV1 syntax_phase,
    CettaPrimeRegularTermCheckPhaseV1 pattern_phase,
    bool own_not_syntax, bool own_out_of_class,
    bool refute_pattern_syntax) {
    if (!cetta_prime_regular_term_maybe_syntax_v1(syntax))
        return (PrimeAuthoredRegularElaboration){0};

    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    CettaPrimeRegularTermElaborationV1 lowered =
        cetta_prime_regular_term_to_pattern_v1(arena, syntax, &budget);
    if (lowered.status != CETTA_PRIME_REGULAR_TERM_OK) {
        prime_account_regular_kernel(ledger, resource_phase, &budget);
        bool owned = lowered.status == CETTA_PRIME_REGULAR_TERM_NOT_SYNTAX
            ? own_not_syntax
            : lowered.status == CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS
                ? own_out_of_class
                : true;
        if (!owned) return (PrimeAuthoredRegularElaboration){0};
        CettaPrimeRegularTermCheckV1 failure = {
            .phase = syntax_phase,
            .syntax = lowered,
        };
        CettaPrimeRegularKernelStatus status =
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
        if (lowered.status == CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED)
            status = CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
        else if (lowered.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR)
            status = CETTA_PRIME_REGULAR_KERNEL_REFUTED;
        else if (lowered.status == CETTA_PRIME_REGULAR_TERM_NOT_SYNTAX ||
                 lowered.status == CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS)
            status = CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
        return (PrimeAuthoredRegularElaboration){
            .owned = true,
            .status = status,
            .detail = prime_regular_term_failure_detail(arena, &failure),
        };
    }

    CettaPrimeRegularPatternEnvironmentV1 empty = {0};
    CettaPrimeRegularPatternElaborationV1 elaborated =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, empty, lowered.pattern, &budget);
    prime_account_regular_kernel(ledger, resource_phase, &budget);
    if (elaborated.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        CettaPrimeRegularTermCheckV1 failure = {
            .phase = pattern_phase,
            .pattern = elaborated,
        };
        CettaPrimeRegularKernelStatus status =
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
        if (elaborated.status ==
            CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED)
            status = CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
        else if (elaborated.status ==
                 CETTA_PRIME_REGULAR_PATTERN_SYNTAX_ERROR) {
            status = refute_pattern_syntax &&
                     !prime_regular_pattern_error_is_out_of_class(
                         elaborated.syntax_error)
                ? CETTA_PRIME_REGULAR_KERNEL_REFUTED
                : CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
        }
        return (PrimeAuthoredRegularElaboration){
            .owned = true,
            .status = status,
            .detail = prime_regular_term_failure_detail(arena, &failure),
        };
    }
    return (PrimeAuthoredRegularElaboration){
        .owned = true,
        .status = CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
        .term = elaborated.term,
    };
}

static PrimeFormStatus prime_form_regular_term_type(
    Space *space, Arena *arena, Atom *expected, PrimeResourceLedger *ledger,
    Atom **detail, bool *owned) {
    PrimeAuthoredRegularElaboration elaborated =
        prime_elaborate_authored_regular(
            arena, expected, ledger, PRIME_RESOURCE_FORMATION,
            CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_SYNTAX,
            CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_PATTERN,
            false, false, true);
    *owned = elaborated.owned;
    if (!elaborated.owned) return PRIME_FORM_UNDETERMINED;
    if (elaborated.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        *detail = elaborated.detail;
        return PRIME_FORM_INCOMPLETE;
    }
    if (elaborated.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
        *detail = elaborated.detail;
        return PRIME_FORM_REFUTED;
    }
    if (elaborated.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE) {
        *detail = elaborated.detail;
        return PRIME_FORM_FAULT;
    }
    if (elaborated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        *detail = elaborated.detail;
        return PRIME_FORM_UNDETERMINED;
    }
    bool intrinsic_owned = false;
    PrimeFormStatus status = prime_form_closed_regular_type(
        space, arena, elaborated.term, ledger, detail, &intrinsic_owned, true);
    if (!intrinsic_owned) {
        *detail = prime_expr1(
            arena, "authored-formation-admission-declined");
        return PRIME_FORM_UNDETERMINED;
    }
    return status;
}

static Atom *prime_synth_authored_regular(
    Space *space, Arena *arena, Atom *judgment, Atom *term,
    PrimeResourceLedger *ledger, bool *engine_fault_out,
    Atom **canonical_term_out) {
    PrimeAuthoredRegularElaboration elaborated =
        prime_elaborate_authored_regular(
            arena, term, ledger, PRIME_RESOURCE_SYNTHESIS,
            CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX,
            CETTA_PRIME_REGULAR_TERM_PHASE_TERM_PATTERN,
            false, true, false);
    if (!elaborated.owned) return NULL;
    if (elaborated.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
        return prime_incomplete(arena, judgment, elaborated.detail);
    if (elaborated.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
        return prime_refuted(arena, judgment, elaborated.detail);
    if (elaborated.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE &&
        engine_fault_out)
        *engine_fault_out = true;
    if (elaborated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return prime_undetermined(arena, judgment, elaborated.detail);
    Atom *admitted = prime_synth_closed_regular(
        space, arena, judgment, elaborated.term, ledger, engine_fault_out);
    if (admitted && canonical_term_out && admitted->kind == ATOM_EXPR &&
        admitted->expr.len == 4u &&
        is_symbol_named(admitted->expr.elems[1], "Established")) {
        *canonical_term_out = elaborated.term;
    }
    return admitted ? admitted
                    : prime_undetermined(
                          arena, judgment,
                          prime_expr1(
                              arena,
                              "authored-synthesis-admission-declined"));
}

typedef struct {
    bool owned;
    CettaPrimeRegularKernelStatus status;
    Atom *detail;
    Atom *canonical_term;
} PrimeRegularTermCheckingDecision;

typedef struct {
    Atom **source_names;
    Atom **pattern_names;
    Atom **constant_keys;
    Atom **types;
    size_t *level_parameter_counts;
    size_t count;
    size_t capacity;
    uint64_t *level_parameters;
    size_t level_parameter_count;
    size_t level_parameter_capacity;
} PrimeRegularDeclarationContext;

typedef struct PrimeRegularDeclarationTrail {
    Atom *name;
    const struct PrimeRegularDeclarationTrail *outer;
} PrimeRegularDeclarationTrail;

static void prime_regular_declaration_context_free(
    PrimeRegularDeclarationContext *context) {
    if (!context) return;
    free(context->source_names);
    free(context->pattern_names);
    free(context->constant_keys);
    free(context->types);
    free(context->level_parameter_counts);
    free(context->level_parameters);
    *context = (PrimeRegularDeclarationContext){0};
}

static bool prime_regular_declaration_context_reserve(
    PrimeRegularDeclarationContext *context, size_t needed) {
    if (needed <= context->capacity) return true;
    size_t capacity = context->capacity ? context->capacity : 4u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) return false;
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(Atom *)) return false;
    context->source_names = cetta_realloc(
        context->source_names, sizeof(Atom *) * capacity);
    context->pattern_names = cetta_realloc(
        context->pattern_names, sizeof(Atom *) * capacity);
    context->constant_keys = cetta_realloc(
        context->constant_keys, sizeof(Atom *) * capacity);
    context->types = cetta_realloc(
        context->types, sizeof(Atom *) * capacity);
    context->level_parameter_counts = cetta_realloc(
        context->level_parameter_counts, sizeof(size_t) * capacity);
    context->capacity = capacity;
    return true;
}

static bool prime_regular_declaration_level_parameters_reserve(
    PrimeRegularDeclarationContext *context, size_t needed) {
    if (needed <= context->level_parameter_capacity) return true;
    size_t capacity = context->level_parameter_capacity
        ? context->level_parameter_capacity : 4u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) return false;
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(uint64_t)) return false;
    context->level_parameters = cetta_realloc(
        context->level_parameters, sizeof(uint64_t) * capacity);
    context->level_parameter_capacity = capacity;
    return true;
}

static bool prime_regular_declaration_level_parameters_append(
    PrimeRegularDeclarationContext *context,
    const uint64_t *parameters, size_t count) {
    if (!context || (count != 0u && !parameters) ||
        count > SIZE_MAX - context->level_parameter_count ||
        !prime_regular_declaration_level_parameters_reserve(
            context, context->level_parameter_count + count))
        return false;
    if (count != 0u)
        memcpy(
            context->level_parameters + context->level_parameter_count,
            parameters, count * sizeof(*parameters));
    context->level_parameter_count += count;
    return true;
}

static bool prime_regular_declaration_charge(
    CettaPrimeRegularKernelBudget *budget, uint64_t work) {
    if (!budget || !budget->limited) return true;
    uint64_t spent = work < budget->remaining ? work : budget->remaining;
    budget->remaining -= spent;
    budget->spent = prime_u64_add_sat(budget->spent, spent);
    return spent == work;
}

static uint64_t prime_regular_declaration_lookup_work(
    const SpaceDeclaredTypeLookupCost *cost) {
    uint64_t work = prime_u64_add_sat(
        cost->indexed_lookups, cost->indexed_rows_examined);
    return prime_u64_add_sat(work, cost->full_space_rows_examined);
}

typedef enum {
    PRIME_LEVEL_SCHEMA_OK = 0,
    PRIME_LEVEL_SCHEMA_OUTSIDE,
    PRIME_LEVEL_SCHEMA_BUDGET,
    PRIME_LEVEL_SCHEMA_RESOURCE
} PrimeRegularLevelSchemaStatus;

typedef struct {
    Atom **variables;
    size_t count;
    size_t capacity;
} PrimeRegularLevelSchemaVariables;

typedef struct {
    PrimeRegularLevelSchemaStatus status;
    Atom *syntax;
    size_t parameter_count;
} PrimeRegularLevelSchemaResult;

static void prime_regular_level_schema_variables_free(
    PrimeRegularLevelSchemaVariables *variables) {
    if (!variables) return;
    free(variables->variables);
    *variables = (PrimeRegularLevelSchemaVariables){0};
}

static bool prime_regular_level_schema_parameter(
    PrimeRegularLevelSchemaVariables *variables, Atom *variable,
    uint64_t *parameter_out) {
    if (!variables || !variable || variable->kind != ATOM_VAR ||
        !parameter_out)
        return false;
    for (size_t index = 0u; index < variables->count; index++) {
        if (variables->variables[index]->var_id != variable->var_id) continue;
        *parameter_out = (uint64_t)index;
        return true;
    }
    if (variables->count > (size_t)INT64_MAX) return false;
    if (variables->count == variables->capacity) {
        size_t capacity = variables->capacity ? variables->capacity * 2u : 4u;
        if (capacity < variables->capacity ||
            capacity > SIZE_MAX / sizeof(Atom *))
            return false;
        variables->variables = cetta_realloc(
            variables->variables, sizeof(Atom *) * capacity);
        variables->capacity = capacity;
    }
    *parameter_out = (uint64_t)variables->count;
    variables->variables[variables->count++] = variable;
    return true;
}

static Atom *prime_regular_level_schema_rec(
    Arena *arena, Atom *syntax,
    PrimeRegularLevelSchemaVariables *variables,
    CettaPrimeRegularKernelBudget *budget,
    PrimeRegularLevelSchemaStatus *status) {
    if (!arena || !syntax || !variables || !status ||
        *status != PRIME_LEVEL_SCHEMA_OK)
        return NULL;
    if (!prime_regular_declaration_charge(budget, 1u)) {
        *status = PRIME_LEVEL_SCHEMA_BUDGET;
        return NULL;
    }
    if (syntax->kind == ATOM_VAR) {
        *status = PRIME_LEVEL_SCHEMA_OUTSIDE;
        return NULL;
    }
    if (syntax->kind != ATOM_EXPR) return syntax;
    if (syntax->expr.len == 2u &&
        atom_is_symbol(syntax->expr.elems[0], "u") &&
        syntax->expr.elems[1]->kind == ATOM_VAR) {
        uint64_t parameter = 0u;
        if (!prime_regular_level_schema_parameter(
                variables, syntax->expr.elems[1], &parameter)) {
            *status = PRIME_LEVEL_SCHEMA_RESOURCE;
            return NULL;
        }
        Atom *marker = cetta_prime_regular_level_parameter_marker_v1(
            arena, parameter);
        if (!marker) {
            *status = PRIME_LEVEL_SCHEMA_RESOURCE;
            return NULL;
        }
        return atom_expr2(arena, syntax->expr.elems[0], marker);
    }
    if (!cetta_expr_len_mul_fits_size(
            syntax->expr.len, sizeof(Atom *))) {
        *status = PRIME_LEVEL_SCHEMA_RESOURCE;
        return NULL;
    }
    Atom **items = arena_alloc(
        arena, sizeof(Atom *) * (size_t)syntax->expr.len);
    for (CettaExprIndex index = 0u; index < syntax->expr.len; index++) {
        items[index] = prime_regular_level_schema_rec(
            arena, syntax->expr.elems[index], variables, budget, status);
        if (!items[index]) return NULL;
    }
    return atom_expr(arena, items, syntax->expr.len);
}

/* Declaration-local matcher variables are schema binders only in universe
 * positions `(u $level)`.  Every other `$` occurrence remains in the ambient
 * MeTTa typing discipline.  First-occurrence numbering makes duplicate
 * declarations compare modulo their authored variable identities. */
static PrimeRegularLevelSchemaResult prime_regular_level_schema(
    Arena *arena, Atom *syntax, CettaPrimeRegularKernelBudget *budget) {
    PrimeRegularLevelSchemaVariables variables = {0};
    PrimeRegularLevelSchemaStatus status = PRIME_LEVEL_SCHEMA_OK;
    Atom *canonical = prime_regular_level_schema_rec(
        arena, syntax, &variables, budget, &status);
    size_t parameter_count = variables.count;
    prime_regular_level_schema_variables_free(&variables);
    return (PrimeRegularLevelSchemaResult){
        .status = status,
        .syntax = status == PRIME_LEVEL_SCHEMA_OK ? canonical : NULL,
        .parameter_count = parameter_count,
    };
}

static Atom *prime_regular_level_parameters_replace_rec(
    Arena *arena, Atom *term, const uint64_t *source_parameters,
    const uint64_t *target_parameters, size_t parameter_count,
    bool source_is_local_index, bool *valid) {
    if (!arena || !term || !valid || !*valid) return NULL;
    if (term->kind == ATOM_EXPR && term->expr.len == 2u &&
        atom_is_symbol(term->expr.elems[0], "LevelParam") &&
        term->expr.elems[1]->kind == ATOM_GROUNDED &&
        term->expr.elems[1]->ground.gkind == GV_INT &&
        term->expr.elems[1]->ground.ival >= 0) {
        uint64_t source = (uint64_t)term->expr.elems[1]->ground.ival;
        size_t parameter_index = SIZE_MAX;
        if (source_is_local_index) {
            if (source < parameter_count)
                parameter_index = (size_t)source;
        } else {
            for (size_t index = 0u; index < parameter_count; index++) {
                if (source_parameters[index] != source) continue;
                parameter_index = index;
                break;
            }
        }
        if (parameter_index == SIZE_MAX) return term;
        if (!target_parameters ||
            target_parameters[parameter_index] > (uint64_t)INT64_MAX) {
            *valid = false;
            return NULL;
        }
        return atom_expr2(
            arena, term->expr.elems[0],
            atom_int(
                arena, (int64_t)target_parameters[parameter_index]));
    }
    if (term->kind != ATOM_EXPR) return term;
    if (!cetta_expr_len_mul_fits_size(term->expr.len, sizeof(Atom *))) {
        *valid = false;
        return NULL;
    }
    Atom **items = arena_alloc(
        arena, sizeof(Atom *) * (size_t)term->expr.len);
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        items[index] = prime_regular_level_parameters_replace_rec(
            arena, term->expr.elems[index], source_parameters,
            target_parameters, parameter_count, source_is_local_index,
            valid);
        if (!items[index]) return NULL;
    }
    return atom_expr(arena, items, term->expr.len);
}

static Atom *prime_regular_level_schema_instantiate_rec(
    Arena *arena, Atom *term, const uint64_t *parameters,
    size_t parameter_count, bool *valid) {
    return prime_regular_level_parameters_replace_rec(
        arena, term, NULL, parameters, parameter_count, true, valid);
}

static Atom *prime_regular_level_parameters_rename_rec(
    Arena *arena, Atom *term, const uint64_t *source_parameters,
    const uint64_t *target_parameters, size_t parameter_count,
    bool *valid) {
    return prime_regular_level_parameters_replace_rec(
        arena, term, source_parameters, target_parameters,
        parameter_count, false, valid);
}

static PrimeRegularTermCheckingDecision prime_declared_term_decision(
    bool owned, CettaPrimeRegularKernelStatus status, Atom *detail,
    Atom *canonical_term) {
    return (PrimeRegularTermCheckingDecision){
        .owned = owned,
        .status = status,
        .detail = detail,
        .canonical_term = canonical_term,
    };
}

typedef struct {
    bool owned;
    CettaPrimeRegularKernelStatus status;
    CettaPrimeRegularTermElaborationV1 lowered;
    Atom *detail;
} PrimeRegularDeclaredElaboration;

static PrimeRegularDeclaredElaboration prime_declared_elaboration(
    bool owned, CettaPrimeRegularKernelStatus status,
    CettaPrimeRegularTermElaborationV1 lowered, Atom *detail) {
    return (PrimeRegularDeclaredElaboration){
        .owned = owned,
        .status = status,
        .lowered = lowered,
        .detail = detail,
    };
}

static bool prime_regular_declaration_context_contains(
    const PrimeRegularDeclarationContext *context, Atom *name) {
    if (!context || !name) return false;
    for (size_t i = 0u; i < context->count; i++)
        if (atom_eq(context->source_names[i], name)) return true;
    return false;
}

static bool prime_regular_declaration_trail_contains(
    const PrimeRegularDeclarationTrail *trail, Atom *name) {
    for (const PrimeRegularDeclarationTrail *cursor = trail;
         cursor; cursor = cursor->outer)
        if (atom_eq(cursor->name, name)) return true;
    return false;
}

/* The arrays are kept in dependency order (innermost first).  Each global
 * declaration has one schema entry; occurrences carry fresh explicit level
 * arguments and never clone the declaration context.  Global names do not
 * consume de Bruijn indices. */
static Atom *prime_regular_declaration_constant_key(
    Arena *arena, Atom *source_name,
    const uint64_t *level_parameters, size_t level_parameter_count) {
    if (!arena || !source_name || source_name->kind != ATOM_SYMBOL ||
        (level_parameter_count != 0u && !level_parameters) ||
        level_parameter_count > SIZE_MAX - 2u ||
        level_parameter_count + 2u > SIZE_MAX / sizeof(Atom *)) {
        return NULL;
    }
    size_t length = level_parameter_count + 2u;
    Atom **items = arena_alloc(arena, length * sizeof(*items));
    items[0] = atom_symbol(arena, "DeclConst");
    items[1] = source_name;
    for (size_t index = 0u; index < level_parameter_count; index++) {
        if (level_parameters[index] > (uint64_t)INT64_MAX) return NULL;
        items[index + 2u] = atom_expr2(
            arena, atom_symbol(arena, "LevelParam"),
            atom_int(arena, (int64_t)level_parameters[index]));
    }
    return atom_expr(arena, items, (CettaExprLen)length);
}

static bool prime_regular_declaration_context_prepend(
    Arena *arena, PrimeRegularDeclarationContext *context,
    Atom *source_name, Atom *pattern_name, Atom *intrinsic_type,
    size_t level_parameter_count) {
    if (!arena || !context || !source_name || !intrinsic_type ||
        source_name->kind != ATOM_SYMBOL ||
        !prime_regular_declaration_context_reserve(
            context, context->count + 1u) ||
        level_parameter_count > SIZE_MAX / sizeof(uint64_t))
        return false;
    uint64_t *local_parameters = level_parameter_count == 0u
        ? NULL : arena_alloc(
              arena, level_parameter_count * sizeof(*local_parameters));
    for (size_t index = 0u; index < level_parameter_count; index++)
        local_parameters[index] = (uint64_t)index;
    Atom *resolved_pattern_name = pattern_name
        ? pattern_name
        : atom_string(arena, atom_name_cstr(source_name));
    Atom *constant_key = prime_regular_declaration_constant_key(
        arena, source_name, local_parameters, level_parameter_count);
    if (!resolved_pattern_name ||
        resolved_pattern_name->kind != ATOM_GROUNDED ||
        resolved_pattern_name->ground.gkind != GV_STRING ||
        !constant_key)
        return false;
    if (context->count > 0u) {
        memmove(
            context->source_names + 1u, context->source_names,
            sizeof(Atom *) * context->count);
        memmove(
            context->pattern_names + 1u, context->pattern_names,
            sizeof(Atom *) * context->count);
        memmove(
            context->constant_keys + 1u, context->constant_keys,
            sizeof(Atom *) * context->count);
        memmove(
            context->types + 1u, context->types,
            sizeof(Atom *) * context->count);
        memmove(
            context->level_parameter_counts + 1u,
            context->level_parameter_counts,
            sizeof(size_t) * context->count);
    }
    context->source_names[0] = source_name;
    context->pattern_names[0] = resolved_pattern_name;
    context->constant_keys[0] = constant_key;
    context->types[0] = intrinsic_type;
    context->level_parameter_counts[0] = level_parameter_count;
    context->count++;
    return true;
}

static Atom *prime_regular_declaration_context_atom(
    Arena *arena, const PrimeRegularDeclarationContext *declarations) {
    Atom *context = atom_symbol(arena, "PrimeCtxNil");
    for (size_t i = declarations->count; i > 0u; i--) {
        Atom *items[4] = {
            atom_symbol(arena, "PrimeCtxDecl"),
            declarations->constant_keys[i - 1u],
            declarations->types[i - 1u], context,
        };
        context = atom_expr(arena, items, 4u);
    }
    return context;
}

typedef struct {
    CettaPrimeRegularKernelStatus status;
    Atom *pattern;
    const char *reason;
} PrimeRegularDeclarationOccurrenceResult;

static PrimeRegularDeclarationOccurrenceResult
prime_regular_declaration_occurrence_result(
    CettaPrimeRegularKernelStatus status, Atom *pattern,
    const char *reason) {
    return (PrimeRegularDeclarationOccurrenceResult){
        .status = status,
        .pattern = pattern,
        .reason = reason,
    };
}

static bool prime_regular_pattern_name_equals(
    Atom *name, const char *text) {
    return name && text && name->kind == ATOM_GROUNDED &&
           name->ground.gkind == GV_STRING && name->ground.sval &&
           strcmp(name->ground.sval, text) == 0;
}

static bool prime_regular_declaration_pattern_index(
    const PrimeRegularDeclarationContext *context, const char *name,
    size_t *index_out) {
    if (!context || !name) return false;
    for (size_t index = 0u; index < context->count; index++) {
        if (!prime_regular_pattern_name_equals(
                context->pattern_names[index], name))
            continue;
        if (index_out) *index_out = index;
        return true;
    }
    return false;
}

static PrimeRegularDeclarationOccurrenceResult
prime_regular_declaration_instantiate_fvar(
    Arena *arena, PrimeRegularDeclarationContext *context,
    Atom *pattern, CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !pattern || !budget ||
        pattern->kind != ATOM_EXPR || pattern->expr.len != 2u ||
        !atom_is_symbol(pattern->expr.elems[0], "FVar") ||
        pattern->expr.elems[1]->kind != ATOM_GROUNDED ||
        pattern->expr.elems[1]->ground.gkind != GV_STRING ||
        !pattern->expr.elems[1]->ground.sval)
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "malformed-declaration-occurrence");

    size_t source_index = 0u;
    if (!prime_regular_declaration_pattern_index(
            context, pattern->expr.elems[1]->ground.sval,
            &source_index))
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "unknown-declaration-occurrence");
    size_t parameter_count =
        context->level_parameter_counts[source_index];
    if (parameter_count == 0u)
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            context->constant_keys[source_index], NULL);
    if (parameter_count > SIZE_MAX / sizeof(uint64_t))
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "invalid-declaration-level-parameter-range");

    uint64_t *fresh_parameters = arena_alloc(
        arena, sizeof(uint64_t) * parameter_count);
    for (size_t index = 0u; index < parameter_count; index++) {
        VarId fresh = VAR_ID_NONE;
        if (!fresh_var_id_try(&fresh))
            return prime_regular_declaration_occurrence_result(
                CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
                "declaration-level-identity-exhausted");
        fresh_parameters[index] = (uint64_t)fresh;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_INSTANCE);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_PARAMETER_FRESH,
        (uint64_t)parameter_count);
    if (!prime_regular_declaration_level_parameters_append(
            context, fresh_parameters, parameter_count))
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "declaration-instance-parameter-resource");
    Atom *instantiated = prime_regular_declaration_constant_key(
        arena, context->source_names[source_index],
        fresh_parameters, parameter_count);
    return instantiated
        ? prime_regular_declaration_occurrence_result(
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
              instantiated, NULL)
        : prime_regular_declaration_occurrence_result(
              CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
              "declaration-instance-pattern-resource");
}

static PrimeRegularDeclarationOccurrenceResult
prime_regular_declaration_instantiate_occurrences_rec(
    Arena *arena, PrimeRegularDeclarationContext *context,
    Atom *pattern, CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !pattern || !budget)
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "invalid-declaration-instantiation-input");
    if (!prime_regular_declaration_charge(budget, 1u))
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
            "declaration-instantiation-budget");
    if (pattern->kind == ATOM_EXPR && pattern->expr.len == 2u &&
        atom_is_symbol(pattern->expr.elems[0], "FVar"))
        return prime_regular_declaration_instantiate_fvar(
            arena, context, pattern, budget);
    if (pattern->kind != ATOM_EXPR)
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, pattern, NULL);
    if (!cetta_expr_len_mul_fits_size(
            pattern->expr.len, sizeof(Atom *)))
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "declaration-instance-pattern-resource");
    Atom **items = arena_alloc(
        arena, sizeof(Atom *) * (size_t)pattern->expr.len);
    for (CettaExprIndex index = 0u; index < pattern->expr.len; index++) {
        PrimeRegularDeclarationOccurrenceResult child =
            prime_regular_declaration_instantiate_occurrences_rec(
                arena, context, pattern->expr.elems[index], budget);
        if (child.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
            !child.pattern)
            return child;
        items[index] = child.pattern;
    }
    return prime_regular_declaration_occurrence_result(
        CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
        atom_expr(arena, items, pattern->expr.len), NULL);
}

static Atom *prime_regular_declaration_quote_intrinsic_rec(
    Arena *arena, const PrimeRegularDeclarationContext *declarations,
    Atom *term, uint64_t binder_depth, bool *complete) {
    if (!arena || !declarations || !term || !complete || !*complete)
        return NULL;
    if (term->kind != ATOM_EXPR) return term;
    if (term->expr.len >= 2u &&
        atom_is_symbol(term->expr.elems[0], "DeclConst") &&
        term->expr.elems[1] &&
        term->expr.elems[1]->kind == ATOM_SYMBOL) {
        return term->expr.elems[1];
    }
    if (term->expr.len == 2u && atom_is_symbol(term->expr.elems[0], "idx") &&
        term->expr.elems[1]->kind == ATOM_GROUNDED &&
        term->expr.elems[1]->ground.gkind == GV_INT &&
        term->expr.elems[1]->ground.ival >= 0) {
        uint64_t index = (uint64_t)term->expr.elems[1]->ground.ival;
        if (index < binder_depth) return term;
        *complete = false;
        return NULL;
    }
    if (term->expr.len > SIZE_MAX / sizeof(Atom *)) {
        *complete = false;
        return NULL;
    }
    Atom **items = arena_alloc(
        arena, sizeof(Atom *) * (size_t)term->expr.len);
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        uint64_t child_depth = binder_depth;
        if ((atom_is_symbol(term->expr.elems[0], "Pi") ||
             atom_is_symbol(term->expr.elems[0], "Sigma")) &&
            term->expr.len == 3u && i == 2u) {
            if (binder_depth == UINT64_MAX) {
                *complete = false;
                return NULL;
            }
            child_depth++;
        } else if (atom_is_symbol(term->expr.elems[0], "Lam") &&
                   ((term->expr.len == 2u && i == 1u) ||
                    (term->expr.len == 3u && i == 2u))) {
            if (binder_depth == UINT64_MAX) {
                *complete = false;
                return NULL;
            }
            child_depth++;
        }
        items[i] = prime_regular_declaration_quote_intrinsic_rec(
            arena, declarations, term->expr.elems[i], child_depth,
            complete);
        if (!items[i]) return NULL;
    }
    return atom_expr(arena, items, term->expr.len);
}

static Atom *prime_regular_declaration_quote_intrinsic(
    Arena *arena, const PrimeRegularDeclarationContext *declarations,
    Atom *term) {
    bool complete = true;
    Atom *quoted = prime_regular_declaration_quote_intrinsic_rec(
        arena, declarations, term, 0u, &complete);
    return complete ? quoted : NULL;
}

static PrimeRegularDeclaredElaboration
prime_elaborate_declared_regular_term_with_trail(
    Space *space, Arena *arena, Atom *term,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget,
    const PrimeRegularDeclarationTrail *trail);

static PrimeRegularDeclaredElaboration
prime_resolve_declared_regular_name(
    Space *space, Arena *arena, Atom *name,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget,
    const PrimeRegularDeclarationTrail *trail) {
    if (!space || !arena || !name || !declarations || !budget ||
        name->kind != ATOM_SYMBOL)
        return (PrimeRegularDeclaredElaboration){0};
    if (prime_regular_declaration_context_contains(declarations, name))
        return prime_declared_elaboration(
            true, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            (CettaPrimeRegularTermElaborationV1){0}, NULL);
    if (prime_regular_declaration_trail_contains(trail, name))
        return (PrimeRegularDeclaredElaboration){0};

    Atom **declared_types = NULL;
    SpaceDeclaredTypeLookupCost cost = {0};
    uint32_t declared_count = space_get_declared_types_costed(
        space, arena, name, &declared_types, &cost);
    bool charged = prime_regular_declaration_charge(
        budget, prime_regular_declaration_lookup_work(&cost));
    if (!charged) {
        free(declared_types);
        return prime_declared_elaboration(
            declared_count > 0u,
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            (CettaPrimeRegularTermElaborationV1){0},
            prime_expr1(arena, "declaration-lookup-budget"));
    }
    if (declared_count == 0u) {
        free(declared_types);
        return (PrimeRegularDeclaredElaboration){0};
    }

    if ((size_t)declared_count > SIZE_MAX / sizeof(Atom *) ||
        (size_t)declared_count > SIZE_MAX / sizeof(size_t)) {
        free(declared_types);
        return prime_declared_elaboration(
            true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            (CettaPrimeRegularTermElaborationV1){0},
            prime_expr1(arena, "declaration-schema-resource"));
    }
    Atom **canonical_types = arena_alloc(
        arena, sizeof(Atom *) * (size_t)declared_count);
    size_t *parameter_counts = arena_alloc(
        arena, sizeof(size_t) * (size_t)declared_count);
    for (uint32_t i = 0u; i < declared_count; i++) {
        PrimeRegularLevelSchemaResult schema = prime_regular_level_schema(
            arena, declared_types[i], budget);
        if (schema.status != PRIME_LEVEL_SCHEMA_OK) {
            free(declared_types);
            if (schema.status == PRIME_LEVEL_SCHEMA_BUDGET)
                return prime_declared_elaboration(
                    true, CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
                    (CettaPrimeRegularTermElaborationV1){0},
                    prime_expr1(arena, "declaration-schema-budget"));
            if (schema.status == PRIME_LEVEL_SCHEMA_RESOURCE)
                return prime_declared_elaboration(
                    true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
                    (CettaPrimeRegularTermElaborationV1){0},
                    prime_expr1(arena, "declaration-schema-resource"));
            return (PrimeRegularDeclaredElaboration){0};
        }
        canonical_types[i] = schema.syntax;
        parameter_counts[i] = schema.parameter_count;
    }

    PrimeRegularDeclarationTrail current = {
        .name = name,
        .outer = trail,
    };
    for (uint32_t i = 0u; i < declared_count; i++) {
        PrimeRegularDeclaredElaboration dependencies =
            prime_elaborate_declared_regular_term_with_trail(
                space, arena, canonical_types[i], declarations, budget,
                &current);
        if (dependencies.status !=
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            free(declared_types);
            return dependencies.status ==
                       CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED ||
                   dependencies.status ==
                       CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE
                ? prime_declared_elaboration(
                      true, dependencies.status, dependencies.lowered,
                      dependencies.detail)
                : (PrimeRegularDeclaredElaboration){0};
        }
    }

    Atom *intrinsic_type = NULL;
    size_t intrinsic_parameter_count = 0u;
    for (uint32_t i = 0u; i < declared_count; i++) {
        PrimeRegularDeclaredElaboration lowered =
            prime_elaborate_declared_regular_term_with_trail(
                space, arena, canonical_types[i], declarations, budget,
                &current);
        if (lowered.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            free(declared_types);
            return lowered.status ==
                       CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED ||
                   lowered.status ==
                       CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE
                ? prime_declared_elaboration(
                      true, lowered.status, lowered.lowered, lowered.detail)
                : (PrimeRegularDeclaredElaboration){0};
        }
        /* Dependency declarations are polymorphic at each occurrence, also
         * while validating another declaration's schema.  Sharing one level
         * instance between two occurrences would silently constrain otherwise
         * independent endpoints (for example List A and List B). */
        PrimeRegularDeclarationOccurrenceResult instantiated_type =
            prime_regular_declaration_instantiate_occurrences_rec(
                arena, declarations, lowered.lowered.pattern, budget);
        if (instantiated_type.status !=
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
            !instantiated_type.pattern) {
            free(declared_types);
            return instantiated_type.status ==
                       CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED ||
                   instantiated_type.status ==
                       CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE
                ? prime_declared_elaboration(
                      true, instantiated_type.status, lowered.lowered,
                      prime_expr1(
                          arena, instantiated_type.reason
                              ? instantiated_type.reason
                              : "declaration-type-instantiation-failed"))
                : (PrimeRegularDeclaredElaboration){0};
        }
        lowered.lowered.pattern = instantiated_type.pattern;
        CettaPrimeRegularPatternEnvironmentV1 environment = {0};
        CettaPrimeRegularPatternElaborationV1 elaborated =
            cetta_prime_regular_pattern_elaborate_v1(
                arena, environment, lowered.lowered.pattern, budget);
        if (elaborated.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
            bool exhausted = elaborated.status ==
                CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED;
            free(declared_types);
            return exhausted
                ? prime_declared_elaboration(
                      true, CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
                      lowered.lowered,
                      prime_expr1(arena, "declaration-type-pattern-budget"))
                : (PrimeRegularDeclaredElaboration){0};
        }
        /* A declaration's own level variables are rigid while its referenced
         * polymorphic declarations are freshly instantiated.  Give the rigid
         * variables identities disjoint from every dependency instance, then
         * let only dependency parameters participate in constraint solving.
         * This checks a schema parametrically instead of proving merely that
         * one convenient closed instance happens to form. */
        Atom *formed_type = elaborated.term;
        uint64_t *rigid_parameters = NULL;
        if (parameter_counts[i] != 0u) {
            if (parameter_counts[i] > SIZE_MAX / sizeof(uint64_t)) {
                free(declared_types);
                return prime_declared_elaboration(
                    true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
                    lowered.lowered,
                    prime_expr1(
                        arena, "declaration-rigid-level-resource"));
            }
            rigid_parameters = arena_alloc(
                arena, sizeof(uint64_t) * parameter_counts[i]);
            for (size_t parameter = 0u;
                 parameter < parameter_counts[i]; parameter++) {
                VarId fresh = VAR_ID_NONE;
                if (!fresh_var_id_try(&fresh)) {
                    free(declared_types);
                    return prime_declared_elaboration(
                        true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
                        lowered.lowered,
                        prime_expr1(
                            arena,
                            "declaration-rigid-level-identity-exhausted"));
                }
                rigid_parameters[parameter] = (uint64_t)fresh;
            }
            bool renamed = true;
            formed_type = prime_regular_level_schema_instantiate_rec(
                arena, formed_type, rigid_parameters,
                parameter_counts[i], &renamed);
            if (!renamed || !formed_type) {
                free(declared_types);
                return prime_declared_elaboration(
                    true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
                    lowered.lowered,
                    prime_expr1(
                        arena, "declaration-rigid-level-renaming-failed"));
            }
        }
        Atom *context = prime_regular_declaration_context_atom(
            arena, declarations);
        CettaPrimeRegularKernelFormedSchemaV1 formed =
            cetta_prime_regular_kernel_form_intrinsic_level_schema_v1(
                arena, context, formed_type,
                declarations->level_parameters,
                declarations->level_parameter_count, budget);
        if (formed.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            bool exhausted = formed.status ==
                CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
            bool failed = formed.status ==
                CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
            free(declared_types);
            if (exhausted || failed)
                return prime_declared_elaboration(
                    true, formed.status, lowered.lowered,
                    prime_expr1(
                        arena, exhausted ? "declaration-type-budget"
                                         : "declaration-type-engine-failure"));
            return (PrimeRegularDeclaredElaboration){0};
        }
        Atom *formed_schema = formed.term;
        if (parameter_counts[i] != 0u) {
            uint64_t *local_parameters = arena_alloc(
                arena, sizeof(uint64_t) * parameter_counts[i]);
            for (size_t parameter = 0u;
                 parameter < parameter_counts[i]; parameter++)
                local_parameters[parameter] = (uint64_t)parameter;
            bool generalized = true;
            formed_schema = prime_regular_level_parameters_rename_rec(
                arena, formed_schema, rigid_parameters,
                local_parameters, parameter_counts[i], &generalized);
            if (!generalized || !formed_schema) {
                free(declared_types);
                return prime_declared_elaboration(
                    true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
                    lowered.lowered,
                    prime_expr1(
                        arena,
                        "declaration-level-generalization-failed"));
            }
        }
        if (!intrinsic_type) {
            intrinsic_type = formed_schema;
            intrinsic_parameter_count = parameter_counts[i];
        } else if (intrinsic_parameter_count != parameter_counts[i] ||
                   !atom_eq(intrinsic_type, formed_schema)) {
            free(declared_types);
            return (PrimeRegularDeclaredElaboration){0};
        }
    }
    free(declared_types);
    if (!intrinsic_type)
        return prime_declared_elaboration(
            true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            (CettaPrimeRegularTermElaborationV1){0},
            prime_expr1(arena, "declaration-context-resource"));

    if (intrinsic_parameter_count != 0u)
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_POLYMORPHIC_LOOKUP);
    if (!prime_regular_declaration_context_prepend(
            arena, declarations, name, NULL, intrinsic_type,
            intrinsic_parameter_count))
        return prime_declared_elaboration(
            true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            (CettaPrimeRegularTermElaborationV1){0},
            prime_expr1(arena, "declaration-context-resource"));
    return prime_declared_elaboration(
        true, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
        (CettaPrimeRegularTermElaborationV1){0}, NULL);
}

/* Extend one declaration context until `term` lowers to the regular Pattern
 * wire.  Declarations lower to named FVars and their schemas are installed
 * only after the acyclic declarations used by their types. */
static PrimeRegularDeclaredElaboration
prime_elaborate_declared_regular_term_with_trail(
    Space *space, Arena *arena, Atom *term,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget,
    const PrimeRegularDeclarationTrail *trail) {
    if (!space || !arena || !term || !declarations || !budget)
        return (PrimeRegularDeclaredElaboration){0};

    for (;;) {
        CettaPrimeRegularTermEnvironmentV1 environment = {
            .names = (const Atom *const *)declarations->source_names,
            .count = declarations->count,
        };
        CettaPrimeRegularTermElaborationV1 lowered =
            cetta_prime_regular_term_to_pattern_in_environment_v1(
                arena, environment, term, budget);
        if (lowered.status == CETTA_PRIME_REGULAR_TERM_OK) {
            return prime_declared_elaboration(
                declarations->count > 0u,
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, lowered, NULL);
        }
        if (lowered.status == CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED) {
            return prime_declared_elaboration(
                declarations->count > 0u,
                CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, lowered,
                prime_expr1(arena, "declaration-elaboration-budget"));
        }
        Atom *name = lowered.unresolved_name;
        if (!name || name->kind != ATOM_SYMBOL)
            return (PrimeRegularDeclaredElaboration){0};

        PrimeRegularDeclaredElaboration resolved =
            prime_resolve_declared_regular_name(
                space, arena, name, declarations, budget, trail);
        if (resolved.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return resolved;
    }
}

static PrimeRegularDeclaredElaboration
prime_elaborate_declared_regular_term(
    Space *space, Arena *arena, Atom *term,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget) {
    return prime_elaborate_declared_regular_term_with_trail(
        space, arena, term, declarations, budget, NULL);
}

/* Resolve the exact acyclic declaration class whose types are already formed
 * by the sealed regular calculus.  Value-indexed evidence such as
 * `p : u0; h : Id u0 p p` is included; variables-as-types remain outside the
 * class because the regular context validator declines them.  Open `$`
 * schemes, conflicts, cycles, and malformed annotations remain ambient. */
static PrimeRegularTermCheckingDecision prime_resolve_declared_regular_term(
    Space *space, Arena *arena, Atom *term, Atom *expected,
    PrimeResourceLedger *ledger, bool synthesize) {
    if (!space || !arena || !term || !ledger ||
        (!synthesize && !expected))
        return (PrimeRegularTermCheckingDecision){0};

    CettaNikDirectAuthorityTokenV1 authority_token;
    if (!cetta_prime_typing_direct_authority_token_v1(
            space, UINT32_C(0x4445434c), &authority_token))
        return (PrimeRegularTermCheckingDecision){0};

    CettaPrimeRegularKernelBudget recognition_budget;
    cetta_prime_regular_kernel_budget_init(
        &recognition_budget, true, UINT64_MAX);
    PrimeRegularDeclarationContext declarations = {0};
    PrimeRegularDeclaredElaboration declaration_elaboration =
        prime_elaborate_declared_regular_term(
            space, arena, term, &declarations, &recognition_budget);
    if (declaration_elaboration.status !=
        CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        prime_regular_declaration_context_free(&declarations);
        if (!declaration_elaboration.owned)
            return (PrimeRegularTermCheckingDecision){0};
        return prime_declared_term_decision(
            true, declaration_elaboration.status,
            declaration_elaboration.detail, NULL);
    }
    CettaPrimeRegularTermElaborationV1 lowered =
        declaration_elaboration.lowered;

    CettaPrimeRegularTermElaborationV1 expected_lowered = {0};
    if (!synthesize) {
        PrimeRegularDeclaredElaboration expected_elaboration =
            prime_elaborate_declared_regular_term(
                space, arena, expected, &declarations,
                &recognition_budget);
        if (expected_elaboration.status !=
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            prime_regular_declaration_context_free(&declarations);
            if (!expected_elaboration.owned)
                return (PrimeRegularTermCheckingDecision){0};
            return prime_declared_term_decision(
                true, expected_elaboration.status,
                expected_elaboration.detail, NULL);
        }
        expected_lowered = expected_elaboration.lowered;
    }

    if (declarations.count == 0u) {
        prime_regular_declaration_context_free(&declarations);
        return (PrimeRegularTermCheckingDecision){0};
    }

    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    if (!prime_regular_declaration_charge(
            &budget, recognition_budget.spent)) {
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, synthesize ? PRIME_RESOURCE_SYNTHESIS
                               : PRIME_RESOURCE_CHECKING,
            &budget);
        return prime_declared_term_decision(
            true, CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            prime_expr1(arena, "declaration-recognition-budget"), NULL);
    }

    PrimeRegularDeclarationOccurrenceResult instantiated_term =
        prime_regular_declaration_instantiate_occurrences_rec(
            arena, &declarations, lowered.pattern, &budget);
    if (instantiated_term.status !=
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !instantiated_term.pattern) {
        CettaPrimeRegularKernelStatus status = instantiated_term.status;
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, synthesize ? PRIME_RESOURCE_SYNTHESIS
                               : PRIME_RESOURCE_CHECKING,
            &budget);
        return prime_declared_term_decision(
            true, status,
            prime_expr1(
                arena, instantiated_term.reason
                    ? instantiated_term.reason
                    : "declaration-instantiation-failed"),
            NULL);
    }
    lowered.pattern = instantiated_term.pattern;
    if (!synthesize) {
        PrimeRegularDeclarationOccurrenceResult instantiated_expected =
            prime_regular_declaration_instantiate_occurrences_rec(
                arena, &declarations, expected_lowered.pattern, &budget);
        if (instantiated_expected.status !=
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
            !instantiated_expected.pattern) {
            CettaPrimeRegularKernelStatus status =
                instantiated_expected.status;
            prime_regular_declaration_context_free(&declarations);
            prime_account_regular_kernel(
                ledger, PRIME_RESOURCE_CHECKING, &budget);
            return prime_declared_term_decision(
                true, status,
                prime_expr1(
                    arena, instantiated_expected.reason
                        ? instantiated_expected.reason
                        : "declaration-expected-instantiation-failed"),
                NULL);
        }
        expected_lowered.pattern = instantiated_expected.pattern;
    }

    CettaPrimeRegularPatternEnvironmentV1 pattern_environment = {0};
    CettaPrimeRegularPatternElaborationV1 elaborated =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, pattern_environment, lowered.pattern, &budget);
    if (elaborated.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        CettaPrimeRegularKernelStatus status =
            elaborated.status == CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED
                ? CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
                : CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, synthesize ? PRIME_RESOURCE_SYNTHESIS
                               : PRIME_RESOURCE_CHECKING,
            &budget);
        return prime_declared_term_decision(
            true, status, prime_expr1(
                arena, status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
                    ? "declaration-pattern-budget"
                    : "declaration-pattern-invalid"),
            NULL);
    }

    Atom *expected_intrinsic = NULL;
    if (!synthesize) {
        CettaPrimeRegularPatternElaborationV1 elaborated_expected =
            cetta_prime_regular_pattern_elaborate_v1(
                arena, pattern_environment, expected_lowered.pattern,
                &budget);
        if (elaborated_expected.status !=
            CETTA_PRIME_REGULAR_PATTERN_OK) {
            CettaPrimeRegularKernelStatus status =
                elaborated_expected.status ==
                    CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED
                ? CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
                : CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
            prime_regular_declaration_context_free(&declarations);
            prime_account_regular_kernel(
                ledger, PRIME_RESOURCE_CHECKING, &budget);
            return prime_declared_term_decision(
                true, status, prime_expr1(
                    arena,
                    status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
                        ? "declaration-expected-pattern-budget"
                        : "declaration-expected-pattern-invalid"),
                NULL);
        }
        expected_intrinsic = elaborated_expected.term;
    }

    Atom *context = prime_regular_declaration_context_atom(
        arena, &declarations);

    CettaPrimeRegularKernelResult result;
    if (synthesize) {
        result =
            cetta_prime_regular_kernel_synth_intrinsic_instantiating_levels_v1(
                arena, context, elaborated.term,
                declarations.level_parameters,
                declarations.level_parameter_count, &budget);
    } else {
        result =
            cetta_prime_regular_kernel_check_intrinsic_instantiating_levels_v1(
                arena, context, elaborated.term, expected_intrinsic,
                declarations.level_parameters,
                declarations.level_parameter_count, &budget);
    }
    bool current = cetta_prime_typing_direct_authority_token_v1_is_current(
        &authority_token, space, UINT32_C(0x4445434c));
    Atom *quoted_type = synthesize &&
                        result.status ==
                            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? prime_regular_declaration_quote_intrinsic(
              arena, &declarations, result.type)
        : NULL;
    Atom *quoted_term = result.status ==
                            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? prime_regular_declaration_quote_intrinsic(
              arena, &declarations, elaborated.term)
        : NULL;
    Atom *detail = result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? (synthesize ? quoted_type : expected)
        : prime_regular_kernel_reason(
              arena, &result, synthesize
                  ? "declared-regular-synthesis"
                  : "declared-regular-checking");
    prime_regular_declaration_context_free(&declarations);
    prime_account_regular_kernel(
        ledger, synthesize ? PRIME_RESOURCE_SYNTHESIS
                           : PRIME_RESOURCE_CHECKING,
        &budget);
    if (!current)
        return prime_declared_term_decision(
            true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            prime_expr1(arena, "declaration-revision-changed"), NULL);
    if (synthesize &&
        result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
        !quoted_type)
        return prime_declared_term_decision(
            true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            prime_expr1(arena, "declaration-type-quotation-failed"), NULL);
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
        !quoted_term)
        return prime_declared_term_decision(
            true, CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            prime_expr1(arena, "declaration-term-quotation-failed"), NULL);
    return prime_declared_term_decision(
        true, result.status, detail, quoted_term);
}

static PrimeFormStatus prime_form_declared_regular_type(
    Space *space, Arena *arena, Atom *type, PrimeResourceLedger *ledger,
    Atom **detail, bool *owned) {
    if (owned) *owned = false;
    if (!space || !arena || !type || !ledger || !detail || !owned)
        return PRIME_FORM_UNDETERMINED;

    CettaNikDirectAuthorityTokenV1 authority_token;
    if (!cetta_prime_typing_direct_authority_token_v1(
            space, UINT32_C(0x4445464d), &authority_token))
        return PRIME_FORM_UNDETERMINED;

    CettaPrimeRegularKernelBudget recognition_budget;
    cetta_prime_regular_kernel_budget_init(
        &recognition_budget, true, UINT64_MAX);
    PrimeRegularDeclarationContext declarations = {0};
    PrimeRegularDeclaredElaboration lowered =
        prime_elaborate_declared_regular_term(
            space, arena, type, &declarations, &recognition_budget);
    if (lowered.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        declarations.count == 0u) {
        bool recognized = lowered.owned;
        CettaPrimeRegularKernelStatus status = lowered.status;
        Atom *failure_detail = lowered.detail;
        prime_regular_declaration_context_free(&declarations);
        if (!recognized) return PRIME_FORM_UNDETERMINED;
        *owned = true;
        *detail = failure_detail;
        if (status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return PRIME_FORM_INCOMPLETE;
        if (status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE)
            return PRIME_FORM_FAULT;
        return PRIME_FORM_UNDETERMINED;
    }

    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    if (!prime_regular_declaration_charge(
            &budget, recognition_budget.spent)) {
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_FORMATION, &budget);
        *owned = true;
        *detail = prime_expr1(
            arena, "declaration-formation-recognition-budget");
        return PRIME_FORM_INCOMPLETE;
    }

    PrimeRegularDeclarationOccurrenceResult instantiated =
        prime_regular_declaration_instantiate_occurrences_rec(
            arena, &declarations, lowered.lowered.pattern, &budget);
    if (instantiated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !instantiated.pattern) {
        CettaPrimeRegularKernelStatus status = instantiated.status;
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_FORMATION, &budget);
        *owned = true;
        *detail = prime_expr1(
            arena, instantiated.reason
                ? instantiated.reason
                : "declaration-formation-instantiation-failed");
        if (status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return PRIME_FORM_INCOMPLETE;
        return PRIME_FORM_FAULT;
    }
    lowered.lowered.pattern = instantiated.pattern;

    CettaPrimeRegularPatternEnvironmentV1 environment = {0};
    CettaPrimeRegularPatternElaborationV1 elaborated =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, environment, lowered.lowered.pattern, &budget);
    if (elaborated.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        bool exhausted = elaborated.status ==
            CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED;
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_FORMATION, &budget);
        *owned = true;
        *detail = prime_expr1(
            arena, exhausted ? "declaration-formation-pattern-budget"
                             : "declaration-formation-pattern-invalid");
        return exhausted ? PRIME_FORM_INCOMPLETE : PRIME_FORM_FAULT;
    }

    Atom *context = prime_regular_declaration_context_atom(
        arena, &declarations);
    CettaPrimeRegularKernelResult formed =
        cetta_prime_regular_kernel_form_intrinsic_instantiating_levels_v1(
            arena, context, elaborated.term,
            declarations.level_parameters,
            declarations.level_parameter_count, &budget);
    bool current = cetta_prime_typing_direct_authority_token_v1_is_current(
        &authority_token, space, UINT32_C(0x4445464d));
    prime_regular_declaration_context_free(&declarations);
    prime_account_regular_kernel(
        ledger, PRIME_RESOURCE_FORMATION, &budget);
    *owned = true;
    if (!current) {
        *detail = prime_expr1(arena, "declaration-formation-revision-changed");
        return PRIME_FORM_FAULT;
    }
    if (formed.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        *detail = type;
        return PRIME_FORM_ESTABLISHED;
    }
    *detail = prime_regular_kernel_reason(
        arena, &formed, "declared-regular-formation");
    if (formed.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
        return PRIME_FORM_REFUTED;
    if (formed.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
        return PRIME_FORM_INCOMPLETE;
    if (formed.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE)
        return PRIME_FORM_FAULT;
    return PRIME_FORM_UNDETERMINED;
}

static Atom *prime_synth_declared_regular(
    Space *space, Arena *arena, Atom *judgment, Atom *term,
    PrimeResourceLedger *ledger, bool *engine_fault_out,
    Atom **canonical_term_out) {
    PrimeRegularTermCheckingDecision decision =
        prime_resolve_declared_regular_term(
            space, arena, term, NULL, ledger, true);
    if (!decision.owned) return NULL;
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (canonical_term_out)
            *canonical_term_out = decision.canonical_term;
        return prime_established(
            arena, judgment,
            prime_expr2(
                arena, "PrimeRegularDeclaredSynthesis", decision.detail));
    }
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
        return prime_refuted(arena, judgment, decision.detail);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
        return prime_incomplete(arena, judgment, decision.detail);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE &&
        engine_fault_out)
        *engine_fault_out = true;
    return prime_undetermined(arena, judgment, decision.detail);
}

static PrimeRegularTermCheckingDecision
prime_resolve_regular_term_check(
    Space *space, Arena *arena, Atom *term, Atom *expected,
    PrimeResourceLedger *ledger) {
    if (!cetta_prime_regular_term_maybe_syntax_v1(term) ||
        !cetta_prime_regular_term_maybe_syntax_v1(expected))
        return (PrimeRegularTermCheckingDecision){0};

    PrimeAuthoredRegularElaboration expected_elaboration =
        prime_elaborate_authored_regular(
            arena, expected, ledger, PRIME_RESOURCE_CHECKING,
            CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_SYNTAX,
            CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_PATTERN,
            false, false, true);
    if (!expected_elaboration.owned)
        return (PrimeRegularTermCheckingDecision){0};
    if (expected_elaboration.status !=
        CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (PrimeRegularTermCheckingDecision){
            .owned = true,
            .status = expected_elaboration.status,
            .detail = expected_elaboration.detail,
        };

    PrimeAuthoredRegularElaboration term_elaboration =
        prime_elaborate_authored_regular(
            arena, term, ledger, PRIME_RESOURCE_CHECKING,
            CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX,
            CETTA_PRIME_REGULAR_TERM_PHASE_TERM_PATTERN,
            false, false, true);
    if (!term_elaboration.owned)
        return (PrimeRegularTermCheckingDecision){0};
    if (term_elaboration.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (PrimeRegularTermCheckingDecision){
            .owned = true,
            .status = term_elaboration.status,
            .detail = term_elaboration.detail,
        };

    CettaPrimeRegularKernelAdmittedCheckingDecisionV1 decision =
        prime_resolve_closed_regular_check(
            space, arena, term_elaboration.term,
            expected_elaboration.term, ledger);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED) {
        CettaPrimeRegularKernelResult result = {
            .status = decision.judgment_status,
            .reason = decision.reason,
        };
        return (PrimeRegularTermCheckingDecision){
            .owned = true,
            .status = decision.judgment_status,
            .detail = decision.judgment_status ==
                              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                ? expected
                : prime_regular_kernel_reason(
                      arena, &result, "regular-kernel-checking-refuted"),
            .canonical_term = decision.judgment_status ==
                                      CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                ? term_elaboration.term
                : NULL,
        };
    }
    if (decision.status ==
        CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED)
        return (PrimeRegularTermCheckingDecision){
            .owned = true,
            .status = CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .detail = prime_expr1(
                arena, decision.reason
                           ? decision.reason
                           : "regular-kernel-checking-incomplete"),
        };
    if (decision.status ==
        CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE)
        return (PrimeRegularTermCheckingDecision){
            .owned = true,
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .detail = prime_expr1(
                arena, decision.reason
                           ? decision.reason
                           : "regular-kernel-checking-engine-failure"),
        };
    return (PrimeRegularTermCheckingDecision){
        .owned = true,
        .status = CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
        .detail = prime_expr1(
            arena, "authored-checking-admission-declined"),
    };
}

static void prime_select_checking_route(
    CettaPrimeTypingRouteV1 *route_out,
    CettaPrimeTypingRouteV1 route,
    CettaRuntimeCounter counter) {
    if (route_out) *route_out = route;
    cetta_runtime_stats_inc(counter);
}

static Atom *prime_check_or_analyze(
    Space *space, Arena *a, Atom *judgment, Atom *term, Atom *expected,
    PrimeResourceLedger *ledger, bool require_exact_or_structural,
    bool expected_already_formed,
    CettaPrimeTypingRouteV1 *route_out,
    bool *engine_fault_out, Atom **canonical_term_out) {
    if (route_out) *route_out = CETTA_PRIME_TYPING_ROUTE_NONE;
    if (engine_fault_out) *engine_fault_out = false;
    if (canonical_term_out) *canonical_term_out = NULL;
    if (cetta_prime_regular_kernel_unwrap_scoped(term, NULL, NULL)) {
        CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
        CettaPrimeRegularKernelResult result = cetta_prime_regular_kernel_check(
            a, term, expected, &budget);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_CHECKING, &budget);
        if (result.status != CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
            result.status != CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED) {
            prime_select_checking_route(
                route_out,
                CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR,
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_SCOPED_REGULAR);
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            if (canonical_term_out) *canonical_term_out = term;
            return prime_established(
                a, judgment,
                prime_expr2(
                    a, require_exact_or_structural
                           ? "PrimeRegularChecked"
                           : "PrimeRegularAnalyzed",
                    expected));
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
            return prime_incomplete(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-checking-incomplete"));
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE) {
            if (engine_fault_out) *engine_fault_out = true;
            return prime_undetermined(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-checking-engine-failure"));
        }
        if (result.status != CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
            result.status != CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED) {
            return prime_refuted(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-checking-refuted"));
        }
    }
    /* Checking follows the same contextual-before-context-free order as
     * synthesis so both judgments classify one term identically. */
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        PrimeRegularTermCheckingDecision declared =
            prime_resolve_declared_regular_term(
                space, a, term, expected, ledger, false);
        if (declared.owned) {
            prime_select_checking_route(
                route_out,
                CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR,
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_DECLARED_REGULAR);
            if (declared.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
                if (canonical_term_out)
                    *canonical_term_out = declared.canonical_term;
                return prime_established(
                    a, judgment,
                    prime_expr2(
                        a, require_exact_or_structural
                               ? "PrimeRegularDeclaredChecked"
                               : "PrimeRegularDeclaredAnalyzed",
                        expected));
            }
            if (declared.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
                return prime_refuted(a, judgment, declared.detail);
            if (declared.status ==
                CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
                return prime_incomplete(a, judgment, declared.detail);
            if (declared.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE &&
                engine_fault_out)
                *engine_fault_out = true;
            return prime_undetermined(a, judgment, declared.detail);
        }
    }
    PrimeRegularTermCheckingDecision syntax = {0};
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE)
        syntax = prime_resolve_regular_term_check(
            space, a, term, expected, ledger);
    if (syntax.owned) {
        prime_select_checking_route(
            route_out,
            CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR,
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_AUTHORED_REGULAR);
        if (syntax.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            if (canonical_term_out)
                *canonical_term_out = syntax.canonical_term;
            return prime_established(
                a, judgment,
                prime_expr2(
                    a, require_exact_or_structural
                           ? "PrimeRegularChecked"
                           : "PrimeRegularAnalyzed",
                    expected));
        }
        if (syntax.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
            return prime_refuted(a, judgment, syntax.detail);
        if (syntax.status ==
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return prime_incomplete(a, judgment, syntax.detail);
        if (syntax.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE &&
            engine_fault_out)
            *engine_fault_out = true;
        return prime_undetermined(a, judgment, syntax.detail);
    }
    CettaPrimeRegularKernelAdmittedCheckingDecisionV1 native =
        prime_resolve_closed_regular_check(
            space, a, term, expected, ledger);
    if (native.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED) {
        prime_select_checking_route(
            route_out,
            CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR,
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_CLOSED_REGULAR);
        CettaPrimeRegularKernelResult result = {
            .status = CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .reason = native.reason,
        };
        return prime_incomplete(
            a, judgment,
            prime_regular_kernel_reason(
                a, &result, "regular-kernel-checking-incomplete"));
    }
    if (native.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE) {
        if (engine_fault_out) *engine_fault_out = true;
        prime_select_checking_route(
            route_out,
            CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR,
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_CLOSED_REGULAR);
        CettaPrimeRegularKernelResult result = {
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = native.reason,
        };
        return prime_undetermined(
            a, judgment,
            prime_regular_kernel_reason(
                a, &result, "regular-kernel-checking-engine-failure"));
    }
    if (native.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED) {
        prime_select_checking_route(
            route_out,
            CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR,
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_CLOSED_REGULAR);
        if (native.judgment_status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            if (canonical_term_out) *canonical_term_out = term;
            return prime_established(
                a, judgment,
                prime_expr2(
                    a, require_exact_or_structural
                           ? "PrimeRegularChecked"
                           : "PrimeRegularAnalyzed",
                    expected));
        }
        CettaPrimeRegularKernelResult result = {
            .status = native.judgment_status,
            .reason = native.reason,
        };
        return prime_refuted(
            a, judgment,
            prime_regular_kernel_reason(
                a, &result, "regular-kernel-checking-refuted"));
    }
    if (!expected_already_formed) {
        Atom *formation_detail = NULL;
        PrimeFormStatus formation = prime_form_type(
            space, a, expected, ledger, &formation_detail, NULL);
        if (formation != PRIME_FORM_ESTABLISHED) {
            prime_select_checking_route(
                route_out,
                CETTA_PRIME_TYPING_ROUTE_AMBIENT_FORMATION,
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_AMBIENT_FORMATION);
        }
        if (formation == PRIME_FORM_REFUTED)
            return prime_refuted(a, judgment,
                                 prime_expr2(a, "ill-formed-expected-type",
                                             formation_detail));
        if (formation == PRIME_FORM_UNDETERMINED)
            return prime_undetermined(
                a, judgment,
                prime_expr2(a, "expected-type-undetermined",
                            formation_detail));
        if (formation == PRIME_FORM_INCOMPLETE)
            return prime_incomplete(
                a, judgment,
                prime_expr2(a, "expected-type-incomplete",
                            formation_detail));
        if (formation == PRIME_FORM_FAULT) {
            if (engine_fault_out) *engine_fault_out = true;
            return prime_undetermined(
                a, judgment,
                prime_expr2(a, "expected-type-fault", formation_detail));
        }
    }

    CettaHeTypingEdge edge = CETTA_HE_EDGE_NONE;
    Atom *detail = NULL;
    uint64_t phase_before = prime_resource_phase_begin(ledger);
    if (route_out)
        *route_out = CETTA_PRIME_TYPING_ROUTE_LEGACY_HE;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CHECKING);
    CettaNikOutcomeV1 status = he_typing_check_term_outcome_budgeted(
        a, space, term, expected, &ledger->typing,
        require_exact_or_structural, &edge, &detail);
    prime_resource_phase_end(ledger, PRIME_RESOURCE_CHECKING, phase_before);
    Atom *edge_atom = prime_sym(a, he_typing_edge_name(edge));
    if (status == CETTA_NIK_OUTCOME_ESTABLISHED) {
        Atom *evidence = prime_expr3(
            a, require_exact_or_structural ? "CheckedTypingEvidence"
                                           : "ConsistencyEvidence",
            edge_atom, detail ? detail : expected);
        return prime_established(a, judgment, evidence);
    }
    if (status == CETTA_NIK_OUTCOME_REFUTED)
        return prime_refuted(a, judgment,
                             detail ? detail : prime_expr1(a, "type-mismatch"));
    if (status == CETTA_NIK_OUTCOME_INCOMPLETE)
        return prime_incomplete(
            a, judgment,
            detail ? detail : prime_expr1(a, "typing-resource-incomplete"));
    return prime_undetermined(
        a, judgment,
        detail ? detail : prime_expr1(a, "typing-boundary-undetermined"));
}

static Atom *prime_form_judgment(
    Space *space, Arena *arena, Atom *judgment, Atom *type,
    PrimeResourceLedger *ledger, CettaPrimeTypingRouteV1 *route_out,
    bool *engine_fault_out) {
    if (route_out) *route_out = CETTA_PRIME_TYPING_ROUTE_NONE;
    if (engine_fault_out) *engine_fault_out = false;

    Atom *detail = NULL;
    bool native_owned = false;
    PrimeFormStatus status = PRIME_FORM_UNDETERMINED;
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        status = prime_form_scoped_regular_type(
            arena, type, ledger, &detail, &native_owned);
        if (native_owned && route_out)
            *route_out = CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR;
        /* A type may mention value declarations even when its outer syntax
         * is an ordinary authored `id`/Pi/Sigma form. */
        if (!native_owned)
            status = prime_form_declared_regular_type(
                space, arena, type, ledger, &detail, &native_owned);
        if (native_owned && route_out &&
            *route_out == CETTA_PRIME_TYPING_ROUTE_NONE)
            *route_out = CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR;
        if (!native_owned)
            status = prime_form_regular_term_type(
                space, arena, type, ledger, &detail, &native_owned);
        if (native_owned && route_out &&
            *route_out == CETTA_PRIME_TYPING_ROUTE_NONE)
            *route_out = CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR;
        if (!native_owned &&
            cetta_prime_regular_kernel_term_maybe_syntax(type)) {
            status = prime_form_closed_regular_type(
                space, arena, type, ledger, &detail, &native_owned, false);
            if (native_owned && route_out)
                *route_out = CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR;
        }
    }
    if (native_owned) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_FORMATION_EXECUTION);
    } else {
        if (route_out)
            *route_out = CETTA_PRIME_TYPING_ROUTE_AMBIENT_FORMATION;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_LEGACY_FORMATION);
        status = prime_form_type(
            space, arena, type, ledger, &detail, NULL);
    }
    if (status == PRIME_FORM_ESTABLISHED)
        return prime_established(arena, judgment, detail);
    if (status == PRIME_FORM_REFUTED)
        return prime_refuted(arena, judgment, detail);
    if (status == PRIME_FORM_INCOMPLETE)
        return prime_incomplete(arena, judgment, detail);
    if (status == PRIME_FORM_FAULT && engine_fault_out)
        *engine_fault_out = true;
    return prime_undetermined(arena, judgment, detail);
}

bool cetta_prime_typing_authority_observation_v1_status(
    const CettaPrimeTypingAuthorityObservationV1 *observation,
    CettaNikStatusV1 *status_out) {
    return observation &&
           observation->result.kind == CETTA_NIK_RESULT_OUTCOME &&
           cetta_nik_outcome_v1_status(
               observation->result.value.outcome, status_out);
}

static bool prime_authority_result_from_verdict(
    Atom *verdict, bool engine_fault,
    CettaNikResultV1 *result_out) {
    if (result_out) *result_out = (CettaNikResultV1){0};
    if (!verdict || !result_out ||
        verdict->kind != ATOM_EXPR ||
        verdict->expr.len != 4u ||
        !is_symbol_named(verdict->expr.elems[0], "PrimeVerdict")) {
        return false;
    }
    Atom *status = verdict->expr.elems[1];
    if (is_symbol_named(status, "Established")) {
        if (engine_fault) return false;
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_ESTABLISHED);
    } else if (is_symbol_named(status, "Refuted")) {
        if (engine_fault) return false;
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_REFUTED);
    } else if (is_symbol_named(status, "Undetermined")) {
        *result_out = engine_fault
            ? cetta_nik_result_v1_engine_fault(
                  CETTA_NIK_ENGINE_FAULT_UNAVAILABLE)
            : cetta_nik_result_v1_outcome(
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT);
    } else if (is_symbol_named(status, "Incomplete")) {
        if (engine_fault) return false;
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_INCOMPLETE);
    } else {
        return false;
    }
    return cetta_nik_result_v1_is_valid(*result_out);
}

static CettaPrimeTypingResourceObservationV1
prime_resource_observation(const PrimeResourceLedger *ledger) {
    if (!ledger) return (CettaPrimeTypingResourceObservationV1){0};
    return (CettaPrimeTypingResourceObservationV1){
        .limited = ledger->typing.steps_limited,
        .initial = ledger->typing.steps_initial,
        .spent = ledger->typing.steps_spent,
        .remaining = ledger->typing.steps_remaining,
        .formation = ledger->phase_spent[PRIME_RESOURCE_FORMATION],
        .synthesis = ledger->phase_spent[PRIME_RESOURCE_SYNTHESIS],
        .normalization = ledger->phase_spent[PRIME_RESOURCE_NORMALIZATION],
        .checking = ledger->phase_spent[PRIME_RESOURCE_CHECKING],
        .refinement = ledger->phase_spent[PRIME_RESOURCE_REFINEMENT],
        .evaluation = ledger->phase_spent[PRIME_RESOURCE_EVALUATION],
    };
}

bool cetta_prime_typing_observe_checking_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingCheckingCandidateV1 *candidate,
    CettaPrimeTypingCheckingObservationV1 *observation_out) {
    if (observation_out)
        *observation_out = (CettaPrimeTypingCheckingObservationV1){0};
    if (!arena || !space || !candidate || !candidate->term ||
        !candidate->expected_type || !observation_out ||
        (candidate->steps_limited && candidate->steps == 0u)) {
        return false;
    }

    PrimeResourceLedger ledger;
    prime_resource_init(
        &ledger, candidate->steps_limited, candidate->steps);
    CettaPrimeTypingRouteV1 route = CETTA_PRIME_TYPING_ROUTE_NONE;
    bool engine_fault = false;
    Atom *canonical_term = NULL;

    Atom *judgment = prime_expr3(
        arena, "type:check", candidate->term, candidate->expected_type);
    Atom *verdict = prime_check_or_analyze(
        space, arena, judgment, candidate->term, candidate->expected_type,
        &ledger, true, false, &route, &engine_fault, &canonical_term);
    CettaNikResultV1 result;
    if (!prime_authority_result_from_verdict(
            verdict, engine_fault, &result) ||
        route == CETTA_PRIME_TYPING_ROUTE_NONE) {
        return false;
    }

    *observation_out = (CettaPrimeTypingCheckingObservationV1){
        .candidate = *candidate,
        .authority = {
            .result = result,
            .route = route,
            .payload = verdict->expr.elems[3],
            .canonical_term = canonical_term,
            .resources = prime_resource_observation(&ledger),
        },
    };
    return true;
}

bool cetta_prime_typing_observe_formation_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingFormationCandidateV1 *candidate,
    CettaPrimeTypingFormationObservationV1 *observation_out) {
    if (observation_out)
        *observation_out = (CettaPrimeTypingFormationObservationV1){0};
    if (!arena || !space || !candidate || !candidate->type ||
        !observation_out ||
        (candidate->steps_limited && candidate->steps == 0u))
        return false;

    PrimeResourceLedger ledger;
    prime_resource_init(&ledger, candidate->steps_limited, candidate->steps);
    CettaPrimeTypingRouteV1 route = CETTA_PRIME_TYPING_ROUTE_NONE;
    bool engine_fault = false;
    Atom *judgment = prime_expr2(arena, "type:formed", candidate->type);
    Atom *verdict = prime_form_judgment(
        space, arena, judgment, candidate->type, &ledger,
        &route, &engine_fault);
    CettaNikResultV1 result;
    if (!prime_authority_result_from_verdict(
            verdict, engine_fault, &result) ||
        route == CETTA_PRIME_TYPING_ROUTE_NONE)
        return false;

    *observation_out = (CettaPrimeTypingFormationObservationV1){
        .candidate = *candidate,
        .authority = {
            .result = result,
            .route = route,
            .payload = verdict->expr.elems[3],
            .resources = prime_resource_observation(&ledger),
        },
    };
    return true;
}

bool cetta_prime_typing_observe_synthesis_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingSynthesisCandidateV1 *candidate,
    CettaPrimeTypingSynthesisObservationV1 *observation_out) {
    if (observation_out)
        *observation_out = (CettaPrimeTypingSynthesisObservationV1){0};
    if (!arena || !space || !candidate || !candidate->term ||
        !observation_out ||
        (candidate->steps_limited && candidate->steps == 0u))
        return false;

    PrimeResourceLedger ledger;
    prime_resource_init(&ledger, candidate->steps_limited, candidate->steps);
    CettaPrimeTypingRouteV1 route = CETTA_PRIME_TYPING_ROUTE_NONE;
    bool engine_fault = false;
    Atom *canonical_term = NULL;
    Atom *judgment = prime_expr2(arena, "type:of", candidate->term);
    Atom *verdict = prime_synth(
        space, arena, judgment, candidate->term, &ledger,
        &route, &engine_fault, &canonical_term);
    CettaNikResultV1 result;
    if (!prime_authority_result_from_verdict(
            verdict, engine_fault, &result) ||
        route == CETTA_PRIME_TYPING_ROUTE_NONE)
        return false;

    *observation_out = (CettaPrimeTypingSynthesisObservationV1){
        .candidate = *candidate,
        .authority = {
            .result = result,
            .route = route,
            .payload = verdict->expr.elems[3],
            .canonical_term = canonical_term,
            .resources = prime_resource_observation(&ledger),
        },
    };
    return true;
}

bool cetta_prime_typing_observe_checking_bag_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingCheckingCandidateV1 *candidates, size_t count,
    CettaPrimeTypingCheckingBagV1 *bag_out) {
    if (bag_out) *bag_out = (CettaPrimeTypingCheckingBagV1){0};
    if (!arena || !space || !bag_out ||
        (count != 0u && !candidates) ||
        count > SIZE_MAX / sizeof(CettaPrimeTypingCheckingObservationV1)) {
        return false;
    }
    if (count == 0u) return true;

    CettaPrimeTypingCheckingObservationV1 *occurrences =
        arena_alloc(arena, count * sizeof(*occurrences));
    CettaPrimeTypingCheckingBagV1 bag = {
        .count = count,
        .occurrences = occurrences,
    };
    for (size_t index = 0u; index < count; index++) {
        if (!cetta_prime_typing_observe_checking_v1(
                arena, space, &candidates[index], &occurrences[index])) {
            return false;
        }
        if (occurrences[index].authority.result.kind ==
            CETTA_NIK_RESULT_ENGINE_FAULT) {
            bag.engine_fault_count++;
            continue;
        }
        CettaNikStatusV1 status;
        if (!cetta_prime_typing_authority_observation_v1_status(
                &occurrences[index].authority, &status)) return false;
        switch (status) {
        case CETTA_NIK_STATUS_ESTABLISHED:
            bag.established_count++;
            break;
        case CETTA_NIK_STATUS_REFUTED:
            bag.refuted_count++;
            break;
        case CETTA_NIK_STATUS_UNDETERMINED:
            bag.undetermined_count++;
            break;
        case CETTA_NIK_STATUS_INCOMPLETE:
            bag.incomplete_count++;
            break;
        }
    }
    *bag_out = bag;
    return true;
}

bool cetta_prime_typing_checking_bag_v1_is_decision_complete(
    const CettaPrimeTypingCheckingBagV1 *bag) {
    return bag && bag->engine_fault_count == 0u &&
           bag->established_count + bag->refuted_count == bag->count;
}

static bool prime_typing_checking_candidate_equal(
    const CettaPrimeTypingCheckingCandidateV1 *left,
    const CettaPrimeTypingCheckingCandidateV1 *right) {
    return left && right && left->term && right->term &&
           left->expected_type && right->expected_type &&
           atom_eq(left->term, right->term) &&
           atom_eq(left->expected_type, right->expected_type);
}

bool cetta_prime_typing_checking_candidate_bag_equal_v1(
    Arena *scratch,
    const CettaPrimeTypingCheckingCandidateV1 *left, size_t left_count,
    const CettaPrimeTypingCheckingCandidateV1 *right, size_t right_count) {
    if (left_count != right_count) return false;
    if (left_count == 0u) return true;
    if (!scratch || !left || !right || right_count > SIZE_MAX / sizeof(bool))
        return false;

    bool *matched = arena_alloc(scratch, right_count * sizeof(*matched));
    memset(matched, 0, right_count * sizeof(*matched));
    for (size_t left_index = 0u; left_index < left_count; left_index++) {
        bool found = false;
        for (size_t right_index = 0u; right_index < right_count;
             right_index++) {
            if (!matched[right_index] &&
                prime_typing_checking_candidate_equal(
                    &left[left_index], &right[right_index])) {
                matched[right_index] = true;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
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
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_CONVERSION_CERTIFICATE_CONSTRUCTION);
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

static __attribute__((noinline)) Atom *prime_convert_legacy_he(
    Space *space, Arena *a, Atom *judgment, Atom *left, Atom *right,
    PrimeResourceLedger *ledger) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CONVERSION);
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
    Atom *evidence = prime_conversion_certificate_atom(
        a, left, right, left_nf, right_nf, equal);
    return equal
        ? prime_established(a, judgment, evidence)
        : prime_refuted(a, judgment, evidence);
}

static Atom *prime_convert_closed_regular(
    Space *space, Arena *a, Atom *judgment, Atom *left, Atom *right,
    PrimeResourceLedger *ledger) {
    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    CettaPrimeRegularKernelAdmittedConversionDecisionV1 decision =
        cetta_prime_regular_kernel_resolve_closed_conversion_v1(
            a, space, left, right, &budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED) {
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
        CettaPrimeRegularKernelResult incomplete = {
            .status = CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .reason = decision.reason,
        };
        return prime_incomplete(
            a, judgment,
            prime_regular_kernel_reason(
                a, &incomplete, "regular-kernel-admission-incomplete"));
    }
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE) {
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
        CettaPrimeRegularKernelResult failure = {
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = decision.reason,
        };
        return prime_undetermined(
            a, judgment,
            prime_regular_kernel_reason(
                a, &failure, "regular-kernel-conversion-engine-failure"));
    }
    if (decision.status != CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED)
        return NULL;

    prime_account_regular_kernel(
        ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
    if (decision.equal) {
        return prime_established(
            a, judgment, prime_expr1(a, "PrimeBetaEtaEqual"));
    }
    return prime_refuted(
        a, judgment,
        prime_expr1(
            a, decision.reason
                   ? decision.reason : "regular-kernel-conversion-refuted"));
}

static Atom *prime_convert_authored_regular(
    Space *space, Arena *arena, Atom *judgment, Atom *left, Atom *right,
    PrimeResourceLedger *ledger) {
    PrimeAuthoredRegularElaboration left_elaboration =
        prime_elaborate_authored_regular(
            arena, left, ledger, PRIME_RESOURCE_NORMALIZATION,
            CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX,
            CETTA_PRIME_REGULAR_TERM_PHASE_TERM_PATTERN,
            true, true, false);
    if (!left_elaboration.owned ||
        left_elaboration.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (left_elaboration.status ==
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return prime_incomplete(
                arena, judgment, left_elaboration.detail);
        if (left_elaboration.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
            return prime_refuted(arena, judgment, left_elaboration.detail);
        return prime_undetermined(
            arena, judgment, left_elaboration.detail);
    }
    PrimeAuthoredRegularElaboration right_elaboration =
        prime_elaborate_authored_regular(
            arena, right, ledger, PRIME_RESOURCE_NORMALIZATION,
            CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX,
            CETTA_PRIME_REGULAR_TERM_PHASE_TERM_PATTERN,
            true, true, false);
    if (!right_elaboration.owned ||
        right_elaboration.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (right_elaboration.status ==
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return prime_incomplete(
                arena, judgment, right_elaboration.detail);
        if (right_elaboration.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
            return prime_refuted(arena, judgment, right_elaboration.detail);
        return prime_undetermined(
            arena, judgment, right_elaboration.detail);
    }
    Atom *admitted = prime_convert_closed_regular(
        space, arena, judgment, left_elaboration.term,
        right_elaboration.term, ledger);
    return admitted ? admitted
                    : prime_undetermined(
                          arena, judgment,
                          prime_expr1(
                          arena,
                          "authored-conversion-admission-declined"));
}

static Atom *prime_convert_declared_regular(
    Space *space, Arena *arena, Atom *judgment, Atom *left, Atom *right,
    PrimeResourceLedger *ledger) {
    if (!space || !arena || !judgment || !left || !right || !ledger)
        return NULL;

    CettaNikDirectAuthorityTokenV1 authority_token;
    if (!cetta_prime_typing_direct_authority_token_v1(
            space, UINT32_C(0x44454356), &authority_token))
        return NULL;

    CettaPrimeRegularKernelBudget recognition_budget;
    cetta_prime_regular_kernel_budget_init(
        &recognition_budget, true, UINT64_MAX);
    PrimeRegularDeclarationContext declarations = {0};
    PrimeRegularDeclaredElaboration left_elaboration =
        prime_elaborate_declared_regular_term(
            space, arena, left, &declarations, &recognition_budget);
    if (left_elaboration.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        prime_regular_declaration_context_free(&declarations);
        if (!left_elaboration.owned) return NULL;
        if (left_elaboration.status ==
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return prime_incomplete(
                arena, judgment, left_elaboration.detail);
        return prime_undetermined(
            arena, judgment, left_elaboration.detail);
    }

    PrimeRegularDeclaredElaboration right_elaboration =
        prime_elaborate_declared_regular_term(
            space, arena, right, &declarations, &recognition_budget);
    if (right_elaboration.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        prime_regular_declaration_context_free(&declarations);
        if (!right_elaboration.owned) return NULL;
        if (right_elaboration.status ==
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return prime_incomplete(
                arena, judgment, right_elaboration.detail);
        return prime_undetermined(
            arena, judgment, right_elaboration.detail);
    }
    if (declarations.count == 0u) {
        prime_regular_declaration_context_free(&declarations);
        return NULL;
    }

    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    if (!prime_regular_declaration_charge(
            &budget, recognition_budget.spent)) {
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
        return prime_incomplete(
            arena, judgment,
            prime_expr1(arena, "declaration-conversion-recognition-budget"));
    }


    PrimeRegularDeclarationOccurrenceResult instantiated_left =
        prime_regular_declaration_instantiate_occurrences_rec(
            arena, &declarations, left_elaboration.lowered.pattern,
            &budget);
    PrimeRegularDeclarationOccurrenceResult instantiated_right =
        instantiated_left.status ==
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
            ? prime_regular_declaration_instantiate_occurrences_rec(
                  arena, &declarations,
                  right_elaboration.lowered.pattern, &budget)
            : (PrimeRegularDeclarationOccurrenceResult){
                  .status = instantiated_left.status,
                  .reason = instantiated_left.reason,
              };
    if (instantiated_left.status !=
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !instantiated_left.pattern ||
        instantiated_right.status !=
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !instantiated_right.pattern) {
        CettaPrimeRegularKernelStatus status =
            instantiated_left.status !=
                    CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                ? instantiated_left.status : instantiated_right.status;
        const char *reason =
            instantiated_left.status !=
                    CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                ? instantiated_left.reason : instantiated_right.reason;
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
        Atom *detail = prime_expr1(
            arena, reason ? reason
                          : "declaration-conversion-instantiation-failed");
        return status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
            ? prime_incomplete(arena, judgment, detail)
            : prime_undetermined(arena, judgment, detail);
    }
    left_elaboration.lowered.pattern = instantiated_left.pattern;
    right_elaboration.lowered.pattern = instantiated_right.pattern;

    CettaPrimeRegularPatternEnvironmentV1 pattern_environment = {0};
    CettaPrimeRegularPatternElaborationV1 left_pattern =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, pattern_environment, left_elaboration.lowered.pattern,
            &budget);
    CettaPrimeRegularPatternElaborationV1 right_pattern =
        left_pattern.status == CETTA_PRIME_REGULAR_PATTERN_OK
            ? cetta_prime_regular_pattern_elaborate_v1(
                  arena, pattern_environment,
                  right_elaboration.lowered.pattern, &budget)
            : (CettaPrimeRegularPatternElaborationV1){0};
    if (left_pattern.status != CETTA_PRIME_REGULAR_PATTERN_OK ||
        right_pattern.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        bool exhausted =
            left_pattern.status ==
                CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED ||
            right_pattern.status ==
                CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED;
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
        Atom *detail = prime_expr1(
            arena, exhausted ? "declaration-conversion-pattern-budget"
                             : "declaration-conversion-pattern-invalid");
        return exhausted ? prime_incomplete(arena, judgment, detail)
                         : prime_undetermined(arena, judgment, detail);
    }

    Atom *context = prime_regular_declaration_context_atom(
        arena, &declarations);

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_DECLARED_REGULAR_CONVERSION_EXECUTION);
    CettaPrimeRegularKernelConversionDecision decision =
        cetta_prime_regular_kernel_decide_intrinsic_conversion_instantiating_levels_v1(
            arena, context, left_pattern.term, right_pattern.term,
            declarations.level_parameters,
            declarations.level_parameter_count, &budget);
    bool current = cetta_prime_typing_direct_authority_token_v1_is_current(
        &authority_token, space, UINT32_C(0x44454356));
    prime_regular_declaration_context_free(&declarations);
    prime_account_regular_kernel(
        ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
    if (!current)
        return prime_undetermined(
            arena, judgment,
            prime_expr1(arena, "declaration-conversion-revision-changed"));
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return prime_established(
            arena, judgment, prime_expr1(arena, "PrimeBetaEtaEqual"));

    CettaPrimeRegularKernelResult result = {
        .status = decision.status,
        .reason = decision.reason,
    };
    Atom *detail = prime_regular_kernel_reason(
        arena, &result, "declared-regular-conversion");
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
        return prime_refuted(arena, judgment, detail);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
        return prime_incomplete(arena, judgment, detail);
    return prime_undetermined(arena, judgment, detail);
}

static Atom *prime_convert(
    Space *space, Arena *a, Atom *judgment, Atom *left, Atom *right,
    PrimeResourceLedger *ledger) {
    bool left_scoped = cetta_prime_regular_kernel_unwrap_scoped(
        left, NULL, NULL);
    bool right_scoped = cetta_prime_regular_kernel_unwrap_scoped(
        right, NULL, NULL);
    if (left_scoped || right_scoped) {
        if (!left_scoped || !right_scoped) {
            return prime_undetermined(
                a, judgment,
                prime_expr1(a, "mixed-regular-presentation"));
        }
        CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
        CettaPrimeRegularKernelResult result = cetta_prime_regular_kernel_convert(
            a, left, right, &budget);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            return prime_established(
                a, judgment, prime_expr1(a, "PrimeBetaEtaEqual"));
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
            return prime_incomplete(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-conversion-incomplete"));
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE) {
            return prime_undetermined(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-conversion-engine-failure"));
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS ||
            result.status == CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED) {
            return prime_convert_legacy_he(
                space, a, judgment, left, right, ledger);
        }
        return prime_refuted(
            a, judgment,
            prime_regular_kernel_reason(
                a, &result, "regular-kernel-conversion-refuted"));
    }

    /* Both operands must share the declaration context before conversion. */
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        Atom *native = prime_convert_declared_regular(
            space, a, judgment, left, right, ledger);
        if (native) return native;
    }
    if (cetta_prime_regular_term_maybe_syntax_v1(left) &&
        cetta_prime_regular_term_maybe_syntax_v1(right) &&
        CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        return prime_convert_authored_regular(
            space, a, judgment, left, right, ledger);
    }

    if (left && right &&
        (left->kind == ATOM_SYMBOL || left->kind == ATOM_EXPR) &&
        (right->kind == ATOM_SYMBOL || right->kind == ATOM_EXPR) &&
        CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        Atom *native = prime_convert_closed_regular(
            space, a, judgment, left, right, ledger);
        if (native) return native;
    }
    return prime_convert_legacy_he(
        space, a, judgment, left, right, ledger);
}

static Atom *prime_refine(Space *space, Arena *a, Atom *judgment,
                          Atom *type, PrimeResourceLedger *ledger) {
    Atom *formation_detail = NULL;
    bool native_formation_owned = false;
    PrimeFormStatus formation = PRIME_FORM_UNDETERMINED;
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        formation = prime_form_declared_regular_type(
            space, a, type, ledger, &formation_detail,
            &native_formation_owned);
        if (!native_formation_owned)
            formation = prime_form_regular_term_type(
                space, a, type, ledger, &formation_detail,
                &native_formation_owned);
        if (!native_formation_owned &&
            cetta_prime_regular_kernel_term_maybe_syntax(type)) {
            formation = prime_form_closed_regular_type(
                space, a, type, ledger, &formation_detail,
                &native_formation_owned, false);
        }
    }
    if (native_formation_owned) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_FORMATION_EXECUTION);
    } else {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_LEGACY_FORMATION);
        formation = prime_form_type(
            space, a, type, ledger, &formation_detail, NULL);
    }
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
    if (formation == PRIME_FORM_FAULT)
        return prime_undetermined(a, judgment,
                                  prime_expr2(a, "refinement-type-fault",
                                              formation_detail));

    Atom *detail = NULL;
    uint64_t phase_before = prime_resource_phase_begin(ledger);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_REFINEMENT);
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

/* Preserve the old compact branch receipt while deriving it from the one
 * checking authority path.  Native evidence is named by its constructor;
 * legacy HE evidence keeps its checked edge name. */
static Atom *prime_branch_evidence_label(Arena *arena, Atom *evidence) {
    if (!evidence || evidence->kind != ATOM_EXPR ||
        evidence->expr.len == 0u ||
        evidence->expr.elems[0]->kind != ATOM_SYMBOL) {
        return prime_sym(arena, "NoEvidence");
    }
    if (is_symbol_named(evidence->expr.elems[0], "CheckedTypingEvidence") &&
        evidence->expr.len >= 2u &&
        evidence->expr.elems[1]->kind == ATOM_SYMBOL) {
        return evidence->expr.elems[1];
    }
    return evidence->expr.elems[0];
}

static Atom *prime_may_or_must(Space *space, Arena *a, Atom *judgment,
                               Atom *bag_atom, Atom *expected,
                               PrimeResourceLedger *ledger, bool must) {
    Atom *formation_detail = NULL;
    bool native_formation_owned = false;
    PrimeFormStatus formation = PRIME_FORM_UNDETERMINED;
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        formation = prime_form_declared_regular_type(
            space, a, expected, ledger, &formation_detail,
            &native_formation_owned);
        if (!native_formation_owned)
            formation = prime_form_regular_term_type(
                space, a, expected, ledger, &formation_detail,
                &native_formation_owned);
        if (!native_formation_owned &&
            cetta_prime_regular_kernel_term_maybe_syntax(expected)) {
            formation = prime_form_closed_regular_type(
                space, a, expected, ledger, &formation_detail,
                &native_formation_owned, true);
        }
    }
    if (!native_formation_owned) {
        formation = prime_form_type(
            space, a, expected, ledger, &formation_detail, NULL);
    }
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
    if (formation == PRIME_FORM_FAULT)
        return prime_undetermined(a, judgment,
                                  prime_expr2(a, "branch-type-fault",
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
        Atom *branch_judgment = prime_expr3(
            a, "type:check", payload, expected);
        CettaPrimeTypingRouteV1 branch_route =
            CETTA_PRIME_TYPING_ROUTE_NONE;
        bool branch_engine_fault = false;
        /* prepare_answer_bag demanded the producer exactly once.  Formation
         * was established above, so the ambient fallback must not demand it
         * again; each native route may still validate its own representation
         * under the branch's regular context. */
        Atom *branch_verdict = prime_check_or_analyze(
            space, a, branch_judgment, payload, expected, ledger,
            true, true, &branch_route, &branch_engine_fault, NULL);
        CettaNikResultV1 branch_result;
        if (!prime_authority_result_from_verdict(
                branch_verdict, branch_engine_fault, &branch_result)) {
            return prime_undetermined(
                a, judgment, prime_expr1(a, "branch-authority-invalid"));
        }
        Atom *detail = branch_verdict->expr.elems[3];
        Atom *evidence_label = prime_branch_evidence_label(a, detail);
        CettaNikOutcomeV1 status = branch_result.kind ==
                                          CETTA_NIK_RESULT_ENGINE_FAULT
            ? CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT
            : branch_result.value.outcome;
        if (status == CETTA_NIK_OUTCOME_ESTABLISHED) {
            Atom *branch_items[4] = {
                prime_sym(a, "BranchEvidence"), atom_int(a, i), payload,
                evidence_label};
            branch_evidence[evidence_count++] = atom_expr(a, branch_items, 4);
            if (!must) {
                Atom *may_items[5] = {
                    prime_sym(a, "MayWitness"), atom_int(a, i), payload,
                    expected, evidence_label};
                return prime_established(a, judgment,
                                         atom_expr(a, may_items, 5));
            }
            continue;
        }
        if (status == CETTA_NIK_OUTCOME_REFUTED) {
            if (must) {
                Atom *counter_items[4] = {
                    prime_sym(a, "MustCounterexample"), atom_int(a, i),
                    payload, detail ? detail : prime_expr1(a, "type-mismatch")};
                return prime_refuted(a, judgment,
                                     atom_expr(a, counter_items, 4));
            }
            continue;
        }
        if (status == CETTA_NIK_OUTCOME_INCOMPLETE) {
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
    if (ledger->typing.steps_limited) {
        limits.replay.max_nodes =
            ledger->typing.steps_remaining > (uint64_t)SIZE_MAX
                ? SIZE_MAX
                : (size_t)ledger->typing.steps_remaining;
    }
    CettaNikReceiptV1 receipt;
    char diagnostic[512] = {0};
    CettaLibraryContext *library = eval_current_library_context();
    CettaNikRuntimeV1 *runtime = library
        ? cetta_library_context_nik_runtime(
            library, diagnostic, sizeof(diagnostic))
        : NULL;
    CettaNikOutcome outcome = runtime
        ? cetta_nik_runtime_v1_check(
            runtime, atom_name_cstr(authority), claim, proof, limits, a,
            &receipt, diagnostic, sizeof(diagnostic))
        : cetta_nik_check_v1(
            atom_name_cstr(authority), claim, proof, limits, a, &receipt,
            diagnostic, sizeof(diagnostic));
    prime_account_nik_work(ledger, receipt.native_nodes);
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

    if (strcmp(name, "type:formed") == 0) {
        if (judgment->expr.len != 2)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "type:formed-arity"));
        return prime_form_judgment(
            space, a, judgment, judgment->expr.elems[1], ledger,
            NULL, NULL);
    }

    if (strcmp(name, "type:of") == 0) {
        if (judgment->expr.len != 2)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "type:of-arity"));
        return prime_synth(
            space, a, judgment, judgment->expr.elems[1], ledger,
            NULL, NULL, NULL);
    }

    if (strcmp(name, "nik:check") == 0) {
        if (judgment->expr.len != 4)
            return prime_refuted(
                a, judgment, prime_expr1(a, "nik:check-arity"));
        return prime_nik_check(
            a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], judgment->expr.elems[3], ledger);
    }

    if (strcmp(name, "type:check") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(
                a, judgment, prime_expr1(a, "type:check-arity"));
        return prime_check_or_analyze(
            space, a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], ledger, true, false, NULL, NULL, NULL);
    }

    if (strcmp(name, "type:analyze") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(
                a, judgment, prime_expr1(a, "type:analyze-arity"));
        return prime_check_or_analyze(
            space, a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], ledger, false, false, NULL, NULL, NULL);
    }

    if (strcmp(name, "type:eq") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "type:eq-arity"));
        return prime_convert(space, a, judgment, judgment->expr.elems[1],
                             judgment->expr.elems[2], ledger);
    }

    if (strcmp(name, "type:refine") == 0) {
        if (judgment->expr.len != 2)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "type:refine-arity"));
        return prime_refine(space, a, judgment, judgment->expr.elems[1],
                            ledger);
    }

    if (strcmp(name, "type:may") == 0 ||
        strcmp(name, "type:must") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(
                a, judgment,
                prime_expr1(a, strcmp(name, "type:may") == 0
                                   ? "type:may-arity"
                                   : "type:must-arity"));
        return prime_may_or_must(
            space, a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], ledger,
            strcmp(name, "type:must") == 0);
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

static bool prime_is_native_typing_judgment(Atom *judgment) {
    judgment = unquote_data(judgment);
    if (!judgment || judgment->kind != ATOM_EXPR ||
        judgment->expr.len == 0 ||
        judgment->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    const char *name = atom_name_cstr(judgment->expr.elems[0]);
    if (!name) return false;
    if ((strcmp(name, "type:formed") == 0 ||
         strcmp(name, "type:of") == 0 ||
         strcmp(name, "type:refine") == 0) && judgment->expr.len == 2) {
        return true;
    }
    return (strcmp(name, "type:check") == 0 ||
            strcmp(name, "type:analyze") == 0 ||
            strcmp(name, "type:eq") == 0 ||
            strcmp(name, "type:may") == 0 ||
            strcmp(name, "type:must") == 0) && judgment->expr.len == 3;
}

Atom *prime_semantics_judge_typing_direct(
    Arena *a, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps) {
    if (!a || !space || (steps_limited && steps == 0) ||
        !prime_is_native_typing_judgment(judgment)) {
        return NULL;
    }
    return prime_judge(a, space, judgment, steps_limited, steps);
}

Atom *prime_semantics_check_nik_direct(
    Arena *a, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps) {
    Atom *view = unquote_data(judgment);
    if (!a || !space || (steps_limited && steps == 0u) || !view ||
        view->kind != ATOM_EXPR || view->expr.len != 4u ||
        view->expr.elems[0]->kind != ATOM_SYMBOL ||
        strcmp(atom_name_cstr(view->expr.elems[0]), "nik:check") != 0) {
        return NULL;
    }
    return prime_judge(a, space, view, steps_limited, steps);
}

const CettaPrimeTypingDirectServiceV1
    cetta_prime_typing_direct_service_v1 = {
        .authority = &cetta_prime_typing_direct_authority_v1,
        .judge = prime_semantics_judge_typing_direct,
    };

bool cetta_prime_typing_direct_service_v1_is_valid(
    const CettaPrimeTypingDirectServiceV1 *service) {
    return service &&
           cetta_nik_direct_authority_v1_is_valid(service->authority) &&
           service->judge;
}

bool cetta_prime_typing_direct_authority_token_v1(
    const Space *space, uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token) {
    if (!space) {
        return cetta_nik_direct_authority_v1_token(
            &cetta_prime_typing_direct_authority_v1,
            policy_identity, NULL, token);
    }

    uint64_t epoch_before = space_global_mutation_epoch();
    SpaceReadToken read = space_read_token(space);
    uint64_t epoch_after = space_global_mutation_epoch();
    if (epoch_before != epoch_after ||
        !space_read_token_matches_live_space(read, space)) {
        if (token) *token = (CettaNikDirectAuthorityTokenV1){0};
        return false;
    }

    CettaNikDirectAuthorityTokenV1 mutable = {
        .words = {read.instance_id, read.revision, epoch_after},
        .length = 3u,
    };
    return cetta_nik_direct_authority_v1_token(
        &cetta_prime_typing_direct_authority_v1,
        policy_identity, &mutable, token);
}

bool cetta_prime_typing_direct_authority_token_v1_is_current(
    const CettaNikDirectAuthorityTokenV1 *token,
    const Space *space, uint32_t policy_identity) {
    CettaNikDirectAuthorityTokenV1 current;
    return token &&
           cetta_prime_typing_direct_authority_token_v1(
               space, policy_identity, &current) &&
           cetta_nik_direct_authority_token_v1_equal(token, &current);
}

static const char *const PRIME_OP_NAMES[] = {"prime-package"};

bool prime_semantics_is_op_id(SymbolId id) {
    return id != SYMBOL_ID_NONE &&
           id == g_builtin_syms.prime_package;
}

bool prime_semantics_is_op(const char *name) {
    if (!name) return false;
    for (size_t i = 0; i < sizeof PRIME_OP_NAMES / sizeof PRIME_OP_NAMES[0]; i++)
        if (strcmp(name, PRIME_OP_NAMES[i]) == 0) return true;
    return false;
}

bool prime_semantics_op_data_arg(const char *name, uint32_t arg_index) {
    (void)name;
    (void)arg_index;
    return false;
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

    return NULL;
}
