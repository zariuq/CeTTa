/* MeTTa-Prime v0: an executable semantic package over CeTTa's shared evaluator
 * and unified dependent type engine. Ordinary judgments run until completion;
 * callers may explicitly request a bounded, resource-reporting judgment. */

#include "prime_semantics.h"

#include <limits.h>
#include <math.h>
#include <pthread.h>
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
#include "prime_arith_oracle.h"
#include "prime_scoped_judgments.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "parser.h"

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

/* A refutation by the conversion algorithm names the theorem it rests on,
 * below Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId: terms of a
 * principal type that the algorithm does not relate are equal at no type
 * (`not_equal_of_unrelated`, TypedEquality/Normalization/Synthesis.lean:792,
 * for a package whose algorithm is complete; see regular_unrelated_refuted),
 * and terms whose synthesized types have no common upper bound are equal at
 * no type (`Synth.not_equal_types`, the same file:767).  The same reasons in
 * an undecided verdict name nothing. */
static Atom *prime_refutation_basis(Arena *a, Atom *reason) {
    if (!reason || reason->kind != ATOM_EXPR || reason->expr.len != 1u) return reason;
    if (atom_is_symbol(reason->expr.elems[0], "not-convertible"))
        return prime_expr2(
            a, "not-convertible",
            prime_sym(a, "Presentation.TypedEquality.Normalization.not_equal_of_unrelated"));
    if (atom_is_symbol(reason->expr.elems[0], "conversion-type-mismatch"))
        return prime_expr2(
            a, "conversion-type-mismatch",
            prime_sym(a, "Presentation.TypedEquality.Normalization.Synth.not_equal_types"));
    return reason;
}

static Atom *prime_refuted(Arena *a, Atom *judgment, Atom *reason) {
    return prime_verdict(a, "Refuted", judgment, prime_refutation_basis(a, reason));
}

static Atom *prime_undetermined(Arena *a, Atom *judgment, Atom *reason) {
    return prime_verdict(a, "Undetermined", judgment, reason);
}

static Atom *prime_incomplete(Arena *a, Atom *judgment, Atom *reason) {
    return prime_verdict(a, "Incomplete", judgment, reason);
}

static bool prime_verdict_is(Atom *verdict, const char *status);
static Atom *prime_implicit_verdict(Arena *a, Atom *judgment,
                                    const PrimeImplicitReport *report,
                                    Atom *elaborated, Atom *written);

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
    /* A judgment without a budget gives its level arithmetic, over all the
     * levels it reads, the default allowance of the level library. */
    CettaPrimeRegularKernelBudget level_allowance;
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
    cetta_prime_level_budget_allowance_v1(&ledger->level_allowance);
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

/* The kernel's budget of a judgment: its steps where it has a budget, and
 * otherwise no count of its own, with the level arithmetic within the
 * judgment's default allowance. */
static CettaPrimeRegularKernelBudget prime_regular_kernel_budget(
    PrimeResourceLedger *ledger) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(
        &budget, ledger->typing.steps_limited,
        ledger->typing.steps_limited ? ledger->typing.steps_remaining : 0u);
    if (!ledger->typing.steps_limited)
        budget.within = &ledger->level_allowance;
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
        prime_sym(a, "BuiltinConstantFragment"),
        prime_expr2(a, "ReplaySchema",
                    prime_sym(a, "PrimeConversionCertificateV1")),
        prime_sym(a, "UndeclaredHeadsUnresolved")};
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
           atom_string_equals_cstr(atom, value);
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
                         "BuiltinConstantFragment") ||
        !prime_symbol_field(rewrites->expr.elems[2]->expr.elems[2],
                            "ReplaySchema",
                            "PrimeConversionCertificateV1") ||
        !is_symbol_named(rewrites->expr.elems[2]->expr.elems[3],
                         "UndeclaredHeadsUnresolved")) {
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
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_UNDECIDED) {
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_SYNTHESIS, &budget);
        return prime_undetermined(
            a, judgment,
            prime_expr1(a, decision.reason ? decision.reason
                                           : "regular-kernel-synthesis-undecided"));
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


/* True when a level, the argument of `(u ...)`, is written over a level
 * variable: `$l`; `(+ level k)` over such a level with a numeral k of any
 * length; or `(max a b)` with such a level on either side. */
static bool prime_level_has_variable(const Atom *level) {
    if (!level) return false;
    if (level->kind == ATOM_VAR) return true;
    if (level->kind != ATOM_EXPR || level->expr.len != 3u) return false;
    if (is_symbol_named(level->expr.elems[0], "max"))
        return prime_level_has_variable(level->expr.elems[1]) ||
               prime_level_has_variable(level->expr.elems[2]);
    return is_symbol_named(level->expr.elems[0], "+") &&
           cetta_prime_regular_kernel_level_numeral_v1(
               level->expr.elems[2]) &&
           prime_level_has_variable(level->expr.elems[1]);
}

/* True when `type` writes a universe at a level over a level variable,
 * `(u $l)`. */
static bool prime_type_has_level_variable(const Atom *type) {
    if (!type || type->kind != ATOM_EXPR) return false;
    if (type->expr.len == 2u && is_symbol_named(type->expr.elems[0], "u") &&
        prime_level_has_variable(type->expr.elems[1]))
        return true;
    for (CettaExprIndex i = 0u; i < type->expr.len; i++)
        if (prime_type_has_level_variable(type->expr.elems[i])) return true;
    return false;
}

/* The head of `term`, when a type the space declares for it is a schema
 * over universe levels; NULL otherwise. */
static Atom *prime_level_schema_head(Space *space, Arena *a, Atom *term) {
    Atom *head = term && term->kind == ATOM_EXPR && term->expr.len > 0u
        ? term->expr.elems[0] : term;
    if (!space || !head || head->kind != ATOM_SYMBOL) return NULL;
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(space, a, head, &declared);
    bool schema = false;
    for (uint32_t i = 0u; i < count && !schema; i++)
        schema = prime_type_has_level_variable(declared[i]);
    free(declared);
    return schema ? head : NULL;
}

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
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_UNDECIDED) {
            return prime_undetermined(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-synthesis-undecided"));
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
    /* A match against a space that declares the atoms of its functor typed
     * is a query, and a query is a type: the type of its answers.  So is a
     * call to a function whose stored rules the space declares typed. */
    Atom *query = prime_scoped_typed_query_judge(
        a, space, judgment, term, ledger->typing.steps_limited,
        ledger->typing.steps_remaining);
    if (query) return query;
    /* A declaration over universe levels is a schema only the kernel reads.
     * When the kernel did not form it, the enumerated declared type is not
     * a type of the term: its level variables are unread, and a schema
     * formed at no level instantiates to no type. */
    Atom *schema_head = prime_level_schema_head(space, a, term);
    if (schema_head)
        return prime_undetermined(
            a, judgment, prime_expr2(a, "universe-schema-not-formed",
                                     schema_head));
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
    Atom **detail, bool *owned, Atom **canonical_term_out) {
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
    if (canonical_term_out) *canonical_term_out = type;
    return PRIME_FORM_ESTABLISHED;
}

static PrimeFormStatus prime_form_closed_regular_type(
    Space *space, Arena *a, Atom *expected,
    PrimeResourceLedger *ledger, Atom **detail, bool *owned,
    bool allow_top_sort, Atom **canonical_term_out) {
    if (owned) *owned = false;
    if (!space || !expected || !detail || !owned ||
        !cetta_prime_regular_kernel_term_maybe_syntax(expected) ||
        !CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        return PRIME_FORM_UNDETERMINED;
    }
    if (allow_top_sort && is_symbol_named(expected, "U1")) {
        *owned = true;
        *detail = prime_expr2(a, "PrimeRegularTypeFormation", expected);
        if (canonical_term_out) *canonical_term_out = expected;
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
    if (synthesis.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_UNDECIDED) {
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_FORMATION, &budget);
        *detail = prime_expr1(
            a, synthesis.reason ? synthesis.reason
                                : "regular-kernel-formation-undecided");
        return PRIME_FORM_UNDETERMINED;
    }
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
    if (canonical_term_out) *canonical_term_out = expected;
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
    Atom **detail, bool *owned, Atom **canonical_term_out) {
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
        space, arena, elaborated.term, ledger, detail, &intrinsic_owned, true,
        canonical_term_out);
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

/* A recognition pass that ended for lack of steps took the steps the
 * judgment had: they are charged to it. */
static void prime_account_exhausted_recognition(
    PrimeResourceLedger *ledger, PrimeResourcePhase phase,
    CettaPrimeRegularKernelStatus status,
    const CettaPrimeRegularKernelBudget *recognition) {
    if (status != CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) return;
    CettaPrimeRegularKernelBudget budget = prime_regular_kernel_budget(ledger);
    (void)prime_regular_declaration_charge(&budget, recognition->spent);
    prime_account_regular_kernel(ledger, phase, &budget);
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

/* A level written over a level variable, with each variable replaced by the
 * marker of its schema parameter: `$l` itself; `(+ level k)` over such a
 * level, whose numeral is kept as written; or `(max a b)`, whose side
 * without a variable is kept as written. */
static Atom *prime_regular_level_schema_level(
    Arena *arena, Atom *level,
    PrimeRegularLevelSchemaVariables *variables,
    CettaPrimeRegularKernelBudget *budget,
    PrimeRegularLevelSchemaStatus *status) {
    if (level->kind == ATOM_VAR) {
        uint64_t parameter = 0u;
        if (!prime_regular_level_schema_parameter(
                variables, level, &parameter)) {
            *status = PRIME_LEVEL_SCHEMA_RESOURCE;
            return NULL;
        }
        Atom *marker = cetta_prime_regular_level_parameter_marker_v1(
            arena, parameter);
        if (!marker) *status = PRIME_LEVEL_SCHEMA_RESOURCE;
        return marker;
    }
    if (!prime_regular_declaration_charge(budget, 1u)) {
        *status = PRIME_LEVEL_SCHEMA_BUDGET;
        return NULL;
    }
    Atom *sides[2] = {level->expr.elems[1], level->expr.elems[2]};
    for (size_t i = 0u; i < 2u; i++) {
        if (!prime_level_has_variable(sides[i])) continue;
        sides[i] = prime_regular_level_schema_level(
            arena, sides[i], variables, budget, status);
        if (!sides[i]) return NULL;
    }
    return atom_expr3(arena, level->expr.elems[0], sides[0], sides[1]);
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
        prime_level_has_variable(syntax->expr.elems[1])) {
        Atom *level = prime_regular_level_schema_level(
            arena, syntax->expr.elems[1], variables, budget, status);
        return level
            ? atom_expr2(arena, syntax->expr.elems[0], level) : NULL;
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
 * positions, `(u $level)` or a level over one such as `(u (+ $level 1))`.
 * Every other `$` occurrence remains in the ambient MeTTa typing discipline.
 * First-occurrence numbering makes duplicate declarations compare modulo
 * their authored variable identities. */
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
    /* A closed level constant has no parameter: it is not walked, however
     * many terms it has. */
    if (term->kind != ATOM_EXPR ||
        (term->expr.len == 4u &&
         atom_is_symbol(term->expr.elems[0], "LevelCantor")) ||
        (term->expr.len == 2u &&
         atom_is_symbol(term->expr.elems[0], "LevelAbove")))
        return term;
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
           atom_string_equals_cstr(name, text);
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
    /* A universe holds no declared constant: its level, which can have any
     * number of terms, is not walked. */
    if (pattern->kind != ATOM_EXPR ||
        (pattern->expr.len == 3u &&
         atom_is_symbol(pattern->expr.elems[0], "PApp") &&
         prime_regular_pattern_name_equals(pattern->expr.elems[1], "Sort")))
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
    /* A universe holds no constant and no index: its level, which can have
     * any number of terms, is not walked.  A sort above every written
     * universe is written by its name (`set`, `class`). */
    if (term->expr.len == 2u && atom_is_symbol(term->expr.elems[0], "Sort")) {
        Atom *named = cetta_prime_regular_kernel_quote_sort_above_v1(
            arena, term);
        return named ? named : term;
    }
    /* A constant is written bare, unless its name also spells a universe
     * written bare (`U0`, `U1`, `set`, `class`): it then keeps its
     * declaration form. */
    if (term->expr.len >= 2u &&
        atom_is_symbol(term->expr.elems[0], "DeclConst") &&
        term->expr.elems[1] &&
        term->expr.elems[1]->kind == ATOM_SYMBOL) {
        Atom *name = term->expr.elems[1];
        return term->expr.len == 2u && (atom_is_symbol(name, "U0") ||
                                        atom_is_symbol(name, "U1") ||
                                        atom_is_symbol(name, "u0") ||
                                        atom_is_symbol(name, "u1") ||
                                        cetta_prime_regular_kernel_sort_above_word_v1(
                                            name))
            ? term : name;
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

/* Language-owned declarations.  The identity eliminator is fixed-left-endpoint
 * elimination in Paulin-Mohring form with two universe parameters, the
 * carrier's level and the motive's level; it is present under every policy.
 * The univalence guest adds an abstract equivalence former and the univalence
 * axiom as declarations without computation; they exist only under that
 * policy, so that the cost of an axiom without a computation rule can be
 * observed rather than argued. */
typedef struct {
    const char *name;
    const char *type;
    int policy;   /* -1: every policy */
} PrimeLanguageDeclaration;

static const PrimeLanguageDeclaration PRIME_LANGUAGE_DECLARATIONS[] = {
    {"id:eliminate",
     "(-> (A : (u $carrier-level))"
     "    (x : A)"
     "    (P : (-> (y : A) (-> (id A x y) (u $motive-level))))"
     "    (d : (P x (refl x)))"
     "    (y : A)"
     "    (e : (id A x y))"
     "    (P y e))",
     -1},
    {"id:assumed-unique",
     "(-> (A : (u $level))"
     "    (a : A)"
     "    (b : A)"
     "    (p : (id A a b))"
     "    (q : (id A a b))"
     "    (id (id A a b) p q))",
     CETTA_PRIME_IDENTITY_UIP},
    {"equiv",
     "(-> (A : (u $level)) (-> (B : (u $level)) (u $level)))",
     CETTA_PRIME_IDENTITY_UNIVALENCE},
    {"ua",
     "(-> (A : (u $level))"
     "    (-> (B : (u $level))"
     "        (-> (equiv A B) (id (u $level) A B))))",
     CETTA_PRIME_IDENTITY_UNIVALENCE},
};

static Atom *prime_language_owned_declaration(Arena *arena, Atom *name) {
    for (size_t i = 0; i < sizeof PRIME_LANGUAGE_DECLARATIONS /
                           sizeof PRIME_LANGUAGE_DECLARATIONS[0]; i++) {
        const PrimeLanguageDeclaration *declaration =
            &PRIME_LANGUAGE_DECLARATIONS[i];
        if (!is_symbol_named(name, declaration->name)) continue;
        if (declaration->policy >= 0 &&
            cetta_prime_identity_policy() != declaration->policy)
            return NULL;
        Atom **atoms = NULL;
        int count = parse_metta_text(declaration->type, arena, &atoms);
        Atom *type = count == 1 && atoms ? atoms[0] : NULL;
        free(atoms);
        return type;
    }
    return NULL;
}

static bool prime_theorem_context_unavailable(
    Space *space, Arena *arena, Atom *name, Atom **reason_out);

static PrimeRegularDeclaredElaboration
prime_resolve_declared_regular_name(
    Space *space, Arena *arena, Atom *name,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget,
    const PrimeRegularDeclarationTrail *trail) {
    if (!space || !arena || !name || !declarations || !budget ||
        name->kind != ATOM_SYMBOL ||
        prime_scoped_judgment_reserved_name(name))
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
    Atom *builtin = prime_language_owned_declaration(arena, name);
    if (builtin) {
        /* A language-owned constant is declared by the language, not by the
         * space: its type is fixed so that the kernel's rules compute on a
         * constant of exactly this type. */
        free(declared_types);
        declared_types = NULL;
        declared_count = 0u;
        if (builtin) {
            declared_types = malloc(sizeof(Atom *));
            if (declared_types) {
                declared_types[0] = builtin;
                declared_count = 1u;
            }
        }
    }
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
    /* A published theorem is the judgment it was checked as, in the context
     * that judgment used.  While that context is unavailable, its statement
     * is not reused: the lookup abstains and names what is missing. */
    Atom *unavailable = NULL;
    if (!builtin &&
        prime_theorem_context_unavailable(space, arena, name, &unavailable)) {
        free(declared_types);
        return prime_declared_elaboration(
            true, CETTA_PRIME_REGULAR_KERNEL_UNDECIDED,
            (CettaPrimeRegularTermElaborationV1){0}, unavailable);
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
            /* A type that mentions a theorem whose context is unavailable
             * is not known to be formed: abstain with the same reason. */
            return dependencies.status ==
                       CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED ||
                   dependencies.status ==
                       CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE ||
                   dependencies.status ==
                       CETTA_PRIME_REGULAR_KERNEL_UNDECIDED
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
                       CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE ||
                   lowered.status ==
                       CETTA_PRIME_REGULAR_KERNEL_UNDECIDED
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

/* Type comparison may unfold an equation under a telescope binder. That binder
 * is not a global declaration and admitting it here does not add one. */
/* While a judgment is decided: the reason a kernel route declined because a
 * universe level could not be brought to normal form, or NULL.  Such a level
 * is a legal level whose value the kernel does not have: there was no memory
 * to compute or to hold it. */
static _Thread_local const char *g_prime_unread_level_reason;

static void prime_note_unread_level(const char *reason) {
    if (!g_prime_unread_level_reason) g_prime_unread_level_reason = reason;
}

/* A judgment starts with no level unread; the state of a judgment around it
 * is returned, to be handed back when this one ends. */
static const char *prime_unread_level_begin(void) {
    const char *outer = g_prime_unread_level_reason;
    g_prime_unread_level_reason = NULL;
    return outer;
}

/* Where a kernel route declined for a level it could not read, the routes
 * that follow answer as they did before such a level was level syntax.  What
 * they establish stands: reporting a declared type, for one, does not need
 * the level's value.  What they refute or leave open was judged without that
 * value, so the judgment is incomplete, for the reason the level was not
 * read. */
static Atom *prime_unread_level_end(
    Arena *a, Atom *verdict, const char *outer) {
    const char *reason = g_prime_unread_level_reason;
    g_prime_unread_level_reason = outer;
    if (!reason || !verdict || verdict->kind != ATOM_EXPR ||
        verdict->expr.len != 4u ||
        !is_symbol_named(verdict->expr.elems[0], "PrimeVerdict") ||
        (!is_symbol_named(verdict->expr.elems[1], "Refuted") &&
         !is_symbol_named(verdict->expr.elems[1], "Undetermined")))
        return verdict;
    return prime_incomplete(
        a, verdict->expr.elems[2], prime_expr1(a, reason));
}

static _Thread_local bool g_prime_admit_open_parameters;
static _Thread_local unsigned g_prime_open_parameter_budget;

/* A telescope parameter has to inhabit a formed universe. A bare symbol is
 * not a context domain, and a later constructor lookup would be rejected. */
static Atom *prime_open_parameter_type(Arena *arena) {
    Atom *level = atom_expr2(
        arena, atom_symbol(arena, "LevelConst"), atom_int(arena, 0));
    return level ? atom_expr2(arena, atom_symbol(arena, "Sort"), level) : NULL;
}

/* Extend one declaration context until `term` lowers to the regular Pattern
 * wire.  Declarations lower to named FVars and their schemas are installed
 * only after the acyclic declarations used by their types. */
/* How many arguments a declared type takes: the binders of its arrows,
 * authored `(-> A ... B)` or kernel `(Pi A B)`, however nested. */
static size_t prime_declared_arity(Atom *type) {
    size_t arity = 0u;
    for (;;) {
        if (type && type->kind == ATOM_EXPR && type->expr.len >= 3u &&
            atom_is_symbol(type->expr.elems[0], "->")) {
            arity += (size_t)type->expr.len - 2u;
            type = type->expr.elems[type->expr.len - 1u];
        } else if (type && type->kind == ATOM_EXPR && type->expr.len == 3u &&
                   atom_is_symbol(type->expr.elems[0], "Pi")) {
            arity++;
            type = type->expr.elems[2];
        } else {
            return arity;
        }
    }
}

/* The words of the term syntax a program may also name a constant with.
 * `quote` and `unquote`, the reader's `@` and `*`, are operators of the
 * language itself, like `let`, and are not among them. */
static bool prime_authored_form_word(Atom *atom) {
    static const char *const words[] = {
        "lam", "app", "fst", "snd", "pair", "refl", "id", "sigma", "u",
        "idx", "u0", "u1",
    };
    if (!atom || atom->kind != ATOM_SYMBOL) return false;
    for (size_t i = 0u; i < sizeof words / sizeof words[0]; i++)
        if (atom_is_symbol(atom, words[i])) return true;
    /* The names of the sorts above the written universes. */
    return cetta_prime_regular_kernel_sort_above_word_v1(atom);
}

static PrimeRegularDeclaredElaboration
prime_resolve_declared_regular_name(
    Space *space, Arena *arena, Atom *name,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget,
    const PrimeRegularDeclarationTrail *trail);

/* A name the space declares is that name even where it spells a word of the
 * term syntax (`fst`, `pair`, `lam`, `u0`, ...): declare it before the term
 * is read, so that reading applies it like any other constant instead of
 * taking it for the syntax. */
static void prime_declare_shadowing_names(
    Space *space, Arena *arena, Atom *term,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget,
    const PrimeRegularDeclarationTrail *trail) {
    if (!term) return;
    Atom *name = term->kind == ATOM_EXPR && term->expr.len > 0u
        ? term->expr.elems[0] : term;
    if (prime_authored_form_word(name) &&
        !prime_regular_declaration_context_contains(declarations, name)) {
        size_t argc = term->kind == ATOM_EXPR ? (size_t)term->expr.len - 1u : 0u;
        Atom **declared = NULL;
        uint32_t count = space_get_declared_types(space, arena, name, &declared);
        bool fits = false;
        for (uint32_t i = 0u; i < count && !fits; i++)
            fits = argc <= prime_declared_arity(declared[i]);
        free(declared);
        if (fits)
            (void)prime_resolve_declared_regular_name(
                space, arena, name, declarations, budget, trail);
    }
    if (term->kind != ATOM_EXPR) return;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        prime_declare_shadowing_names(
            space, arena, term->expr.elems[i], declarations, budget, trail);
}

static PrimeRegularDeclaredElaboration
prime_elaborate_declared_regular_term_with_trail(
    Space *space, Arena *arena, Atom *term,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget,
    const PrimeRegularDeclarationTrail *trail) {
    if (!space || !arena || !term || !declarations || !budget)
        return (PrimeRegularDeclaredElaboration){0};
    prime_declare_shadowing_names(
        space, arena, term, declarations, budget, trail);

    for (;;) {
        size_t *arities = arena_alloc(
            arena, sizeof(size_t) * (declarations->count ? declarations->count : 1u));
        for (size_t i = 0u; arities && i < declarations->count; i++)
            arities[i] = prime_declared_arity(declarations->types[i]);
        CettaPrimeRegularTermEnvironmentV1 environment = {
            .names = (const Atom *const *)declarations->source_names,
            .count = declarations->count,
            .arities = arities,
        };
        CettaPrimeRegularTermElaborationV1 lowered =
            cetta_prime_regular_term_to_pattern_in_environment_v1(
                arena, environment, term, budget);
        if (lowered.status == CETTA_PRIME_REGULAR_TERM_OK) {
            return prime_declared_elaboration(
                declarations->count > 0u,
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, lowered, NULL);
        }
        if (cetta_prime_regular_term_level_incomplete_v1(&lowered)) {
            /* A universe at a level the kernel cannot bring to normal form:
             * this route declines, as it does for a type outside the regular
             * syntax, so a declared type is still reported as written.  The
             * reason is kept for the judgments that need the level's
             * value. */
            prime_note_unread_level(lowered.reason);
            return (PrimeRegularDeclaredElaboration){0};
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

        if (g_prime_admit_open_parameters && !trail &&
            !prime_language_owned_declaration(arena, name)) {
            Atom **declared = NULL;
            uint32_t declared_count = space_get_declared_types(
                space, arena, name, &declared);
            free(declared);
            if (declared_count == 0u) {
                if (prime_regular_declaration_context_contains(
                        declarations, name) ||
                    g_prime_open_parameter_budget == 0u)
                    return (PrimeRegularDeclaredElaboration){0};
                g_prime_open_parameter_budget--;
                Atom *parameter_type = prime_open_parameter_type(arena);
                if (!parameter_type ||
                    !prime_regular_declaration_context_prepend(
                        arena, declarations, name, NULL, parameter_type, 0u))
                    return (PrimeRegularDeclaredElaboration){0};
                continue;
            }
        }

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

/* The kernel compares terms at their types, so a context must declare every
 * constant that computation can put into a term, not only the constants the
 * checked term mentions.  Close the declarations under the patterns and
 * right-hand sides of the space's rules at declared heads, until no new
 * declaration appears.  A constant whose declaration does not resolve is left
 * out; a comparison that needs its type then abstains. */
static void prime_regular_declare_rule_constants(
    Space *space, Arena *arena, Atom *term,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget) {
    if (!term || term->kind != ATOM_EXPR) return;
    if (term->expr.len >= 2u &&
        atom_is_symbol(term->expr.elems[0], "DeclConst") &&
        term->expr.elems[1] && term->expr.elems[1]->kind == ATOM_SYMBOL) {
        if (!prime_regular_declaration_context_contains(
                declarations, term->expr.elems[1]))
            (void)prime_resolve_declared_regular_name(
                space, arena, term->expr.elems[1], declarations, budget, NULL);
        return;
    }
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        prime_regular_declare_rule_constants(
            space, arena, term->expr.elems[i], declarations, budget);
}

static Atom *prime_regular_declaration_kernel_context(
    Space *space, Arena *arena, PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget) {
    Atom *rules = prime_semantics_kernel_rules(arena, space);
    size_t previous;
    do {
        previous = declarations->count;
        for (Atom *r = rules;
             r && r->kind == ATOM_EXPR && r->expr.len == 3u &&
             atom_is_symbol(r->expr.elems[0], "LCons");
             r = r->expr.elems[2]) {
            Atom *rule = r->expr.elems[1];
            if (!rule || rule->kind != ATOM_EXPR || rule->expr.len != 5u ||
                !atom_is_symbol(rule->expr.elems[0], "PrimeRule") ||
                !prime_regular_declaration_context_contains(
                    declarations, rule->expr.elems[1]))
                continue;
            prime_regular_declare_rule_constants(
                space, arena, rule->expr.elems[3], declarations, budget);
            prime_regular_declare_rule_constants(
                space, arena, rule->expr.elems[4], declarations, budget);
        }
    } while (declarations->count != previous);
    return prime_regular_declaration_context_atom(arena, declarations);
}

/* Intrinsic clients have already resolved lexical scope. Only global
 * occurrences need the same schema lookup and fresh level instantiation as
 * authored FVars; typed binders and explicit level arguments stay intact. */
static PrimeRegularDeclarationOccurrenceResult
prime_instantiate_declared_intrinsic_rec(
    Space *space, Arena *arena, Atom *term,
    PrimeRegularDeclarationContext *declarations,
    CettaPrimeRegularKernelBudget *budget) {
    if (!term || !prime_regular_declaration_charge(budget, 1u))
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
            "declaration-instantiation-budget");
    /* A universe holds no declared constant: its level, which can have any
     * number of terms, is not walked. */
    if (term->kind != ATOM_EXPR ||
        (term->expr.len == 2u && atom_is_symbol(term->expr.elems[0], "Sort")))
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, term, NULL);
    if (term->expr.len >= 2u &&
        atom_is_symbol(term->expr.elems[0], "DeclConst")) {
        Atom *name = term->expr.elems[1];
        PrimeRegularDeclaredElaboration resolved =
            prime_resolve_declared_regular_name(
                space, arena, name, declarations, budget, NULL);
        if (resolved.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
            !resolved.owned)
            return prime_regular_declaration_occurrence_result(
                resolved.owned ? resolved.status
                               : CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
                NULL, "unresolved-intrinsic-declaration");
        if (term->expr.len > 2u)
            return prime_regular_declaration_occurrence_result(
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, term, NULL);
        return prime_regular_declaration_instantiate_fvar(
            arena, declarations,
            atom_expr2(arena, atom_symbol(arena, "FVar"),
                       atom_string(arena, atom_name_cstr(name))), budget);
    }
    if (!cetta_expr_len_mul_fits_size(term->expr.len, sizeof(Atom *)))
        return prime_regular_declaration_occurrence_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "declaration-instance-term-resource");
    Atom **items = arena_alloc(arena, sizeof(*items) * (size_t)term->expr.len);
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        PrimeRegularDeclarationOccurrenceResult child =
            prime_instantiate_declared_intrinsic_rec(
                space, arena, term->expr.elems[i], declarations, budget);
        if (child.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return child;
        items[i] = child.pattern;
    }
    return prime_regular_declaration_occurrence_result(
        CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
        atom_expr(arena, items, term->expr.len), NULL);
}

CettaPrimeRegularKernelResult prime_semantics_check_declared_intrinsic_v1(
    Arena *arena, Space *space, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget) {
    CettaPrimeRegularKernelResult result = {
        .status = CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
        .reason = "intrinsic-declaration-authority-unavailable",
    };
    CettaNikDirectAuthorityTokenV1 token;
    if (!arena || !space || !term || !expected || !budget ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, UINT32_C(0x4445434c), &token))
        return result;
    Atom *outer_rules = cetta_prime_regular_kernel_rules_get();
    cetta_prime_regular_kernel_rules_set(prime_semantics_kernel_rules(arena, space));
    PrimeRegularDeclarationContext declarations = {0};
    PrimeRegularDeclarationOccurrenceResult type =
        prime_instantiate_declared_intrinsic_rec(
            space, arena, expected, &declarations, budget);
    PrimeRegularDeclarationOccurrenceResult body = type;
    if (type.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        body = prime_instantiate_declared_intrinsic_rec(
            space, arena, term, &declarations, budget);
    if (body.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        result = cetta_prime_regular_kernel_check_intrinsic_instantiating_levels_v1(
            arena, prime_regular_declaration_kernel_context(
                space, arena, &declarations, budget),
            body.pattern, type.pattern, declarations.level_parameters,
            declarations.level_parameter_count, budget);
    } else {
        result.status = body.status;
        result.reason = body.reason;
    }
    prime_regular_declaration_context_free(&declarations);
    cetta_prime_regular_kernel_rules_set(outer_rules);
    if (!cetta_prime_typing_direct_authority_token_v1_is_current(
            &token, space, UINT32_C(0x4445434c))) {
        result.status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
        result.reason = "declaration-revision-changed";
    }
    return result;
}

/* Resolve the exact acyclic declaration class whose types are already formed
 * by the sealed regular calculus.  Value-indexed evidence such as
 * `p : u0; h : Id u0 p p` is included; variables-as-types remain outside the
 * class because the regular context validator declines them.  Open `$`
 * schemes, conflicts, cycles, and malformed annotations remain ambient. */
/* A kernel-query export runs an ordinary typing judgment with capture on.
 * The scoped and declared routes then record the represented inputs they
 * would hand to the kernel and decline instead of deciding.  Ordinary
 * judgments never turn capture on. */
typedef struct {
    bool active;
    bool captured;
    const char *route;
    Atom *context;
    Atom *object_context;
    Atom *subject;
    Atom *object;
    uint64_t *levels;
    size_t level_count;
} PrimeKernelQueryCapture;

static PrimeKernelQueryCapture g_prime_kernel_query;

static bool prime_kernel_query_capturing(void) {
    return g_prime_kernel_query.active && !g_prime_kernel_query.captured;
}

static void prime_kernel_query_record(
    const char *route, Atom *context, Atom *object_context, Atom *subject,
    Atom *object, const uint64_t *levels, size_t level_count) {
    PrimeKernelQueryCapture *capture = &g_prime_kernel_query;
    capture->captured = true;
    capture->route = route;
    capture->context = context;
    capture->object_context = object_context;
    capture->subject = subject;
    capture->object = object;
    capture->levels = NULL;
    capture->level_count = 0u;
    if (level_count && levels) {
        capture->levels = malloc(sizeof(uint64_t) * level_count);
        if (capture->levels) {
            memcpy(capture->levels, levels, sizeof(uint64_t) * level_count);
            capture->level_count = level_count;
        }
    }
}

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
    /* The pass that finds out whether a term is one of declared constants
     * finds that out whatever the judgment has left: its own steps are not
     * limited, and are charged to the judgment once the term is such a
     * term.  The arithmetic of a level is work of the judgment whoever does
     * it, so it also counts against the steps the judgment has left: a
     * level whose value takes more of them is read no further. */
    CettaPrimeRegularKernelBudget judgment_steps =
        prime_regular_kernel_budget(ledger);
    recognition_budget.within = &judgment_steps;
    PrimeRegularDeclarationContext declarations = {0};
    PrimeRegularDeclaredElaboration declaration_elaboration =
        prime_elaborate_declared_regular_term(
            space, arena, term, &declarations, &recognition_budget);
    if (declaration_elaboration.status !=
        CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        prime_regular_declaration_context_free(&declarations);
        if (!declaration_elaboration.owned)
            return (PrimeRegularTermCheckingDecision){0};
        prime_account_exhausted_recognition(
            ledger, synthesize ? PRIME_RESOURCE_SYNTHESIS
                               : PRIME_RESOURCE_CHECKING,
            declaration_elaboration.status, &recognition_budget);
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
            prime_account_exhausted_recognition(
                ledger, PRIME_RESOURCE_CHECKING, expected_elaboration.status,
                &recognition_budget);
            return prime_declared_term_decision(
                true, expected_elaboration.status,
                expected_elaboration.detail, NULL);
        }
        expected_lowered = expected_elaboration.lowered;
    }

    if (declarations.count == 0u &&
        (synthesize || !prime_kernel_query_capturing())) {
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

    Atom *context = prime_regular_declaration_kernel_context(
        space, arena, &declarations, &budget);

    if (!synthesize && prime_kernel_query_capturing()) {
        prime_kernel_query_record(
            declarations.count > 0u ? "declared" : "closed", context, NULL,
            elaborated.term, expected_intrinsic,
            declarations.level_parameters, declarations.level_parameter_count);
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_CHECKING, &budget);
        return prime_declared_term_decision(
            true, CETTA_PRIME_REGULAR_KERNEL_UNDECIDED,
            prime_expr1(arena, "kernel-query-captured"), NULL);
    }

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
    Atom **detail, bool *owned, Atom **canonical_term_out) {
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
    /* The arithmetic of a level counts against the steps the judgment has
     * left, as in the recognition of a declared term above. */
    CettaPrimeRegularKernelBudget judgment_steps =
        prime_regular_kernel_budget(ledger);
    recognition_budget.within = &judgment_steps;
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
        prime_account_exhausted_recognition(
            ledger, PRIME_RESOURCE_FORMATION, status, &recognition_budget);
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

    Atom *context = prime_regular_declaration_kernel_context(
        space, arena, &declarations, &budget);
    CettaPrimeRegularKernelFormedSchemaV1 formed =
        cetta_prime_regular_kernel_form_intrinsic_level_schema_v1(
            arena, context, elaborated.term,
            declarations.level_parameters,
            declarations.level_parameter_count, &budget);
    bool current = cetta_prime_typing_direct_authority_token_v1_is_current(
        &authority_token, space, UINT32_C(0x4445464d));
    Atom *canonical = current &&
            formed.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? prime_regular_declaration_quote_intrinsic(
              arena, &declarations, formed.term)
        : NULL;
    prime_regular_declaration_context_free(&declarations);
    prime_account_regular_kernel(
        ledger, PRIME_RESOURCE_FORMATION, &budget);
    *owned = true;
    if (!current) {
        *detail = prime_expr1(arena, "declaration-formation-revision-changed");
        return PRIME_FORM_FAULT;
    }
    if (formed.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (!canonical) {
            *detail = prime_expr1(arena, "declaration-formation-quote-failed");
            return PRIME_FORM_FAULT;
        }
        *detail = type;
        if (canonical_term_out) *canonical_term_out = canonical;
        return PRIME_FORM_ESTABLISHED;
    }
    CettaPrimeRegularKernelResult failure = {
        .status = formed.status,
        .reason = formed.reason,
    };
    *detail = prime_regular_kernel_reason(
        arena, &failure, "declared-regular-formation");
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
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_UNDECIDED)
        return (PrimeRegularTermCheckingDecision){
            .owned = true,
            .status = CETTA_PRIME_REGULAR_KERNEL_UNDECIDED,
            .detail = prime_expr1(
                arena, decision.reason
                           ? decision.reason
                           : "regular-kernel-checking-undecided"),
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
        if (expected && prime_kernel_query_capturing()) {
            Atom *context = NULL, *subject = NULL;
            Atom *object_context = NULL, *object = expected;
            cetta_prime_regular_kernel_unwrap_scoped(term, &context, &subject);
            if (cetta_prime_regular_kernel_unwrap_scoped(
                    expected, &object_context, &object) &&
                atom_eq(object_context, context))
                object_context = NULL;
            prime_kernel_query_record(
                "scoped", context, object_context, subject, object, NULL, 0u);
            return prime_undetermined(
                a, judgment, prime_expr1(a, "kernel-query-captured"));
        }
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
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_UNDECIDED) {
            return prime_undetermined(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-checking-undecided"));
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
    if (native.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_UNDECIDED) {
        prime_select_checking_route(
            route_out,
            CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR,
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_CLOSED_REGULAR);
        return prime_undetermined(
            a, judgment,
            prime_expr1(a, native.reason ? native.reason
                                         : "regular-kernel-checking-undecided"));
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
    bool *engine_fault_out, Atom **canonical_term_out) {
    if (route_out) *route_out = CETTA_PRIME_TYPING_ROUTE_NONE;
    if (engine_fault_out) *engine_fault_out = false;
    if (canonical_term_out) *canonical_term_out = NULL;

    Atom *detail = NULL;
    bool native_owned = false;
    PrimeFormStatus status = PRIME_FORM_UNDETERMINED;
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        status = prime_form_scoped_regular_type(
            arena, type, ledger, &detail, &native_owned, canonical_term_out);
        if (native_owned && route_out)
            *route_out = CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR;
        /* A type may mention value declarations even when its outer syntax
         * is an ordinary authored `id`/Pi/Sigma form. */
        if (!native_owned)
            status = prime_form_declared_regular_type(
                space, arena, type, ledger, &detail, &native_owned,
                canonical_term_out);
        if (native_owned && route_out &&
            *route_out == CETTA_PRIME_TYPING_ROUTE_NONE)
            *route_out = CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR;
        if (!native_owned)
            status = prime_form_regular_term_type(
                space, arena, type, ledger, &detail, &native_owned,
                canonical_term_out);
        if (native_owned && route_out &&
            *route_out == CETTA_PRIME_TYPING_ROUTE_NONE)
            *route_out = CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR;
        if (!native_owned &&
            cetta_prime_regular_kernel_term_maybe_syntax(type)) {
            status = prime_form_closed_regular_type(
                space, arena, type, ledger, &detail, &native_owned, false,
                canonical_term_out);
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
        if (canonical_term_out) *canonical_term_out = NULL;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_LEGACY_FORMATION);
        status = prime_form_type(
            space, arena, type, ledger, &detail, NULL);
    }
    if (status == PRIME_FORM_ESTABLISHED)
        return prime_established(arena, judgment, detail);
    if (canonical_term_out) *canonical_term_out = NULL;
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

    /* Observations and ordinary judgments read the same admitted equations
     * and stored occurrences. A nested observation restores its caller's
     * computation context before publishing either success or failure. */
    Atom *outer_rules = cetta_prime_regular_kernel_rules_get();
    space = prime_scoped_stored_space(arena, space);
    cetta_prime_regular_kernel_rules_set(prime_semantics_kernel_rules(arena, space));
    PrimeResourceLedger ledger;
    prime_resource_init(
        &ledger, candidate->steps_limited, candidate->steps);
    CettaPrimeTypingRouteV1 route = CETTA_PRIME_TYPING_ROUTE_NONE;
    bool engine_fault = false;
    Atom *canonical_term = NULL;

    Atom *judgment = prime_expr3(
        arena, "type:check", candidate->term, candidate->expected_type);
    /* Constructors written without their parameters are read at the type
     * the term is checked at (prime_implicit_verdict). */
    PrimeImplicitReport implicit = {0};
    Atom *explicit_judgment = prime_scoped_implicit_judgment(arena, space, judgment, &implicit);
    const char *outer_unread_level = prime_unread_level_begin();
    Atom *verdict = prime_check_or_analyze(
        space, arena, explicit_judgment, explicit_judgment->expr.elems[1],
        explicit_judgment->expr.elems[2],
        &ledger, true, false, &route, &engine_fault, &canonical_term);
    verdict = prime_unread_level_end(arena, verdict, outer_unread_level);
    if ((explicit_judgment != judgment || implicit.unsolved) &&
        !prime_verdict_is(verdict, "Established")) {
        CettaPrimeTypingRouteV1 written_route = CETTA_PRIME_TYPING_ROUTE_NONE;
        bool written_fault = false;
        Atom *written_canonical = NULL;
        const char *outer = prime_unread_level_begin();
        Atom *written = prime_check_or_analyze(
            space, arena, judgment, candidate->term, candidate->expected_type,
            &ledger, true, false, &written_route, &written_fault, &written_canonical);
        written = prime_unread_level_end(arena, written, outer);
        Atom *combined = explicit_judgment != judgment
            ? prime_implicit_verdict(arena, judgment, &implicit, verdict, written)
            : prime_implicit_verdict(arena, judgment, &implicit, written, written);
        if (combined == written) {
            route = written_route;
            engine_fault = written_fault;
            canonical_term = written_canonical;
        } else if (combined != verdict) {
            canonical_term = NULL;
        }
        verdict = combined;
    }
    cetta_prime_regular_kernel_rules_set(outer_rules);
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

    Atom *outer_rules = cetta_prime_regular_kernel_rules_get();
    space = prime_scoped_stored_space(arena, space);
    cetta_prime_regular_kernel_rules_set(prime_semantics_kernel_rules(arena, space));
    PrimeResourceLedger ledger;
    prime_resource_init(&ledger, candidate->steps_limited, candidate->steps);
    CettaPrimeTypingRouteV1 route = CETTA_PRIME_TYPING_ROUTE_NONE;
    bool engine_fault = false;
    Atom *judgment = prime_expr2(arena, "type:formed", candidate->type);
    Atom *canonical_term = NULL;
    PrimeImplicitReport implicit = {0};
    Atom *explicit_judgment = prime_scoped_implicit_judgment(arena, space, judgment, &implicit);
    const char *outer_unread_level = prime_unread_level_begin();
    Atom *verdict = prime_form_judgment(
        space, arena, explicit_judgment, explicit_judgment->expr.elems[1], &ledger,
        &route, &engine_fault, &canonical_term);
    verdict = prime_unread_level_end(arena, verdict, outer_unread_level);
    if ((explicit_judgment != judgment || implicit.unsolved) &&
        !prime_verdict_is(verdict, "Established")) {
        CettaPrimeTypingRouteV1 written_route = CETTA_PRIME_TYPING_ROUTE_NONE;
        bool written_fault = false;
        Atom *written_canonical = NULL;
        const char *outer = prime_unread_level_begin();
        Atom *written = prime_form_judgment(
            space, arena, judgment, candidate->type, &ledger,
            &written_route, &written_fault, &written_canonical);
        written = prime_unread_level_end(arena, written, outer);
        Atom *combined = explicit_judgment != judgment
            ? prime_implicit_verdict(arena, judgment, &implicit, verdict, written)
            : prime_implicit_verdict(arena, judgment, &implicit, written, written);
        if (combined == written) {
            route = written_route;
            engine_fault = written_fault;
            canonical_term = written_canonical;
        } else if (combined != verdict) {
            canonical_term = NULL;
        }
        verdict = combined;
    }
    cetta_prime_regular_kernel_rules_set(outer_rules);
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
            .canonical_term = canonical_term,
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

    Atom *outer_rules = cetta_prime_regular_kernel_rules_get();
    space = prime_scoped_stored_space(arena, space);
    cetta_prime_regular_kernel_rules_set(prime_semantics_kernel_rules(arena, space));
    PrimeResourceLedger ledger;
    prime_resource_init(&ledger, candidate->steps_limited, candidate->steps);
    CettaPrimeTypingRouteV1 route = CETTA_PRIME_TYPING_ROUTE_NONE;
    bool engine_fault = false;
    Atom *canonical_term = NULL;
    Atom *judgment = prime_expr2(arena, "type:of", candidate->term);
    PrimeImplicitReport implicit = {0};
    Atom *explicit_judgment = prime_scoped_implicit_judgment(arena, space, judgment, &implicit);
    const char *outer_unread_level = prime_unread_level_begin();
    Atom *verdict = prime_synth(
        space, arena, explicit_judgment, explicit_judgment->expr.elems[1], &ledger,
        &route, &engine_fault, &canonical_term);
    verdict = prime_unread_level_end(arena, verdict, outer_unread_level);
    if ((explicit_judgment != judgment || implicit.unsolved) &&
        !prime_verdict_is(verdict, "Established")) {
        CettaPrimeTypingRouteV1 written_route = CETTA_PRIME_TYPING_ROUTE_NONE;
        bool written_fault = false;
        Atom *written_canonical = NULL;
        const char *outer = prime_unread_level_begin();
        Atom *written = prime_synth(
            space, arena, judgment, candidate->term, &ledger,
            &written_route, &written_fault, &written_canonical);
        written = prime_unread_level_end(arena, written, outer);
        Atom *combined = explicit_judgment != judgment
            ? prime_implicit_verdict(arena, judgment, &implicit, verdict, written)
            : prime_implicit_verdict(arena, judgment, &implicit, written, written);
        if (combined == written) {
            route = written_route;
            engine_fault = written_fault;
            canonical_term = written_canonical;
        } else if (combined != verdict) {
            canonical_term = NULL;
        }
        verdict = combined;
    }
    cetta_prime_regular_kernel_rules_set(outer_rules);
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

typedef enum {
    PRIME_BUILTIN_UNDECIDED = 0,
    PRIME_BUILTIN_EQUAL,
    PRIME_BUILTIN_DISTINCT,
} PrimeBuiltinRelation;

static bool prime_builtin_carrier(Atom *atom) {
    return is_symbol_named(atom, "Number") ||
           is_symbol_named(atom, "String") ||
           is_symbol_named(atom, "Bool");
}

/* The built-in signature: the carriers Number, String and Bool, and the
 * grounded literals of each.  Its constants are rigid: identical constants
 * are equal by reflexivity, and distinct ones are equal at no type
 * (`not_equal_of_distinct_rigid`).  NaN and the two zeros have no canonical
 * spelling, big integers and rationals are compared only for identity, and
 * whether literals of different kinds denote one constant is not
 * specified. */
static PrimeBuiltinRelation prime_builtin_constant_relation(Atom *left,
                                                            Atom *right) {
    if (prime_builtin_carrier(left) && prime_builtin_carrier(right))
        return atom_eq(left, right) ? PRIME_BUILTIN_EQUAL
                                    : PRIME_BUILTIN_DISTINCT;
    if (!left || !right || left->kind != ATOM_GROUNDED ||
        right->kind != ATOM_GROUNDED ||
        left->ground.gkind != right->ground.gkind)
        return PRIME_BUILTIN_UNDECIDED;
    switch (left->ground.gkind) {
    case GV_INT:
    case GV_BOOL:
    case GV_STRING:
        return atom_eq(left, right) ? PRIME_BUILTIN_EQUAL
                                    : PRIME_BUILTIN_DISTINCT;
    case GV_FLOAT: {
        double x = left->ground.fval;
        double y = right->ground.fval;
        if (isnan(x) || isnan(y) || (x == 0.0 && y == 0.0))
            return signbit(x) == signbit(y) && !isnan(x) && !isnan(y)
                ? PRIME_BUILTIN_EQUAL
                : PRIME_BUILTIN_UNDECIDED;
        return x == y ? PRIME_BUILTIN_EQUAL : PRIME_BUILTIN_DISTINCT;
    }
    case GV_BIGINT:
    case GV_RATIONAL:
        return atom_eq(left, right) ? PRIME_BUILTIN_EQUAL
                                    : PRIME_BUILTIN_UNDECIDED;
    default:
        return PRIME_BUILTIN_UNDECIDED;
    }
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
                    prime_sym(a, "BuiltinConstantFragment"))};
    return atom_expr(a, items, 5);
}

static bool prime_replay_conversion_certificate_checked(
    Atom *certificate, bool *equal_out) {
    if (
        !prime_schema_expr(certificate, "PrimeConversionCertificateV1", 5) ||
        !prime_schema_expr(certificate->expr.elems[1], "Original", 3) ||
        !prime_schema_expr(certificate->expr.elems[2], "NormalForms", 3) ||
        !prime_schema_expr(certificate->expr.elems[3], "Relation", 2) ||
        !prime_symbol_field(certificate->expr.elems[4], "Fragment",
                            "BuiltinConstantFragment")) {
        return false;
    }

    Atom *relation = certificate->expr.elems[3]->expr.elems[1];
    bool claims_equal = is_symbol_named(relation, "Equal");
    if (!claims_equal && !is_symbol_named(relation, "Distinct")) return false;

    Atom *left = certificate->expr.elems[1]->expr.elems[1];
    Atom *right = certificate->expr.elems[1]->expr.elems[2];
    if (!atom_eq(certificate->expr.elems[2]->expr.elems[1], left) ||
        !atom_eq(certificate->expr.elems[2]->expr.elems[2], right))
        return false;
    PrimeBuiltinRelation relation_found =
        prime_builtin_constant_relation(left, right);
    if (relation_found == PRIME_BUILTIN_UNDECIDED) return false;
    bool equal = relation_found == PRIME_BUILTIN_EQUAL;
    if (equal != claims_equal) return false;
    if (equal_out) *equal_out = equal;
    return true;
}

bool prime_semantics_replay_conversion_certificate(
    Arena *a, Space *space, Atom *certificate, bool *equal_out) {
    if (!a || !space) return false;
    return prime_replay_conversion_certificate_checked(certificate, equal_out);
}

/* Conversion that no native route owns is decided only between constants of
 * the built-in signature.  Any other operand has a head the declaration
 * context does not type (`const_not_typed`) or one that runs only under
 * ordinary equations, so its conversion is unresolved rather than compared
 * by spelling. */
static __attribute__((noinline)) Atom *prime_convert_builtin_constants(
    Arena *a, Atom *judgment, Atom *left, Atom *right) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CONVERSION);
    PrimeBuiltinRelation relation =
        prime_builtin_constant_relation(left, right);
    if (relation == PRIME_BUILTIN_UNDECIDED) {
        return prime_undetermined(
            a, judgment,
            prime_expr1(a, "conversion-outside-builtin-constants"));
    }
    bool equal = relation == PRIME_BUILTIN_EQUAL;
    Atom *evidence = prime_conversion_certificate_atom(
        a, left, right, left, right, equal);
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
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_UNDECIDED) {
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
        return prime_undetermined(
            a, judgment,
            prime_expr1(a, decision.reason ? decision.reason
                                           : "regular-kernel-conversion-undecided"));
    }
    if (decision.status != CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED)
        return NULL;

    prime_account_regular_kernel(
        ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
    /* Joined by steps, each an instance of an admitted equation: see
     * regular_conv_at for what that rests on, and what it does not. */
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
    /* The arithmetic of a level counts against the steps the judgment has
     * left, as in the recognition of a declared term above. */
    CettaPrimeRegularKernelBudget judgment_steps =
        prime_regular_kernel_budget(ledger);
    recognition_budget.within = &judgment_steps;
    PrimeRegularDeclarationContext declarations = {0};
    PrimeRegularDeclaredElaboration left_elaboration =
        prime_elaborate_declared_regular_term(
            space, arena, left, &declarations, &recognition_budget);
    if (left_elaboration.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        prime_regular_declaration_context_free(&declarations);
        if (!left_elaboration.owned) return NULL;
        prime_account_exhausted_recognition(
            ledger, PRIME_RESOURCE_NORMALIZATION, left_elaboration.status,
            &recognition_budget);
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
        prime_account_exhausted_recognition(
            ledger, PRIME_RESOURCE_NORMALIZATION, right_elaboration.status,
            &recognition_budget);
        if (right_elaboration.status ==
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return prime_incomplete(
                arena, judgment, right_elaboration.detail);
        return prime_undetermined(
            arena, judgment, right_elaboration.detail);
    }
    if (declarations.count == 0u && !prime_kernel_query_capturing()) {
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

    Atom *context = prime_regular_declaration_kernel_context(
        space, arena, &declarations, &budget);

    if (prime_kernel_query_capturing()) {
        prime_kernel_query_record(
            declarations.count > 0u ? "declared" : "closed", context, NULL,
            left_pattern.term, right_pattern.term,
            declarations.level_parameters, declarations.level_parameter_count);
        prime_regular_declaration_context_free(&declarations);
        prime_account_regular_kernel(
            ledger, PRIME_RESOURCE_NORMALIZATION, &budget);
        return prime_undetermined(
            arena, judgment, prime_expr1(arena, "kernel-query-captured"));
    }

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
        if (prime_kernel_query_capturing()) {
            Atom *context = NULL, *subject = NULL;
            Atom *object_context = NULL, *object = NULL;
            cetta_prime_regular_kernel_unwrap_scoped(left, &context, &subject);
            cetta_prime_regular_kernel_unwrap_scoped(
                right, &object_context, &object);
            prime_kernel_query_record(
                "scoped", context,
                atom_eq(context, object_context) ? NULL : object_context,
                subject, object, NULL, 0u);
            return prime_undetermined(
                a, judgment, prime_expr1(a, "kernel-query-captured"));
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
        /* Scoped operands the kernel declines are not compared by any other
         * route: the legacy route would only test the two scoped atoms for
         * identity, which refutes nothing. */
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS ||
            result.status == CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED) {
            return prime_undetermined(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-conversion-declined"));
        }
        if (result.status == CETTA_PRIME_REGULAR_KERNEL_UNDECIDED) {
            return prime_undetermined(
                a, judgment,
                prime_regular_kernel_reason(
                    a, &result, "regular-kernel-conversion-undecided"));
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
    return prime_convert_builtin_constants(a, judgment, left, right);
}

static Atom *prime_refine(Space *space, Arena *a, Atom *judgment,
                          Atom *type, PrimeResourceLedger *ledger) {
    Atom *formation_detail = NULL;
    bool native_formation_owned = false;
    PrimeFormStatus formation = PRIME_FORM_UNDETERMINED;
    if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {
        formation = prime_form_declared_regular_type(
            space, a, type, ledger, &formation_detail,
            &native_formation_owned, NULL);
        if (!native_formation_owned)
            formation = prime_form_regular_term_type(
                space, a, type, ledger, &formation_detail,
                &native_formation_owned, NULL);
        if (!native_formation_owned &&
            cetta_prime_regular_kernel_term_maybe_syntax(type)) {
            formation = prime_form_closed_regular_type(
                space, a, type, ledger, &formation_detail,
                &native_formation_owned, false, NULL);
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
    /* An incomplete evaluation becomes this judgment's own incomplete
     * answer; it is not also an incompleteness of the enclosing query. */
    outcome.completion_consumed = true;
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
            &native_formation_owned, NULL);
        if (!native_formation_owned)
            formation = prime_form_regular_term_type(
                space, a, expected, ledger, &formation_detail,
                &native_formation_owned, NULL);
        if (!native_formation_owned &&
            cetta_prime_regular_kernel_term_maybe_syntax(expected)) {
            formation = prime_form_closed_regular_type(
                space, a, expected, ledger, &formation_detail,
                &native_formation_owned, true, NULL);
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
                             PrimeResourceLedger *ledger,
                             Atom **canonical_term_out) {
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
            NULL, NULL, canonical_term_out);
    }

    if (strcmp(name, "type:of") == 0) {
        if (judgment->expr.len != 2)
            return prime_refuted(a, judgment,
                                 prime_expr1(a, "type:of-arity"));
        return prime_synth(
            space, a, judgment, judgment->expr.elems[1], ledger,
            NULL, NULL, canonical_term_out);
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
            judgment->expr.elems[2], ledger, true, false, NULL, NULL,
            canonical_term_out);
    }

    if (strcmp(name, "type:analyze") == 0) {
        if (judgment->expr.len != 3)
            return prime_refuted(
                a, judgment, prime_expr1(a, "type:analyze-arity"));
        return prime_check_or_analyze(
            space, a, judgment, judgment->expr.elems[1],
            judgment->expr.elems[2], ledger, false, false, NULL, NULL,
            canonical_term_out);
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

static bool prime_kernel_rule_atom(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 5u &&
           is_symbol_named(atom->expr.elems[0], "type:rule");
}

static Atom *prime_kernel_rule_wrap(Arena *a, Atom *atom, Atom *list) {
    Atom *rule_items[5] = {atom_symbol(a, "PrimeRule"), atom->expr.elems[1], atom->expr.elems[2],
                           atom->expr.elems[3], atom->expr.elems[4]};
    Atom *rule = atom_expr(a, rule_items, 5u);
    Atom *cons_items[3] = {atom_symbol(a, "LCons"), rule, list ? list : atom_symbol(a, "LNil")};
    return atom_expr(a, cons_items, 3u);
}

/* Only rules that admission published compute.  A rule written or imported
 * as a raw atom was never checked, so it confers nothing. */
static Atom *prime_kernel_rule_cons(Arena *a, Space *space, Atom *atom,
                                    Atom *list) {
    if (!prime_kernel_rule_atom(atom) ||
        !prime_scoped_judgment_admitted(a, space, atom))
        return list;
    return prime_kernel_rule_wrap(a, atom, list);
}

/* The admitted rules of an indexed space, remembered per thread for one
 * state of the space and of admission.  Every covered call asks for the
 * rules; finding them reads the space's index and asks admission about each
 * candidate, a digest per rule, which made a call cost a pass over all rules
 * with a hash each.  The rules found are the same while the space is the same
 * instance at the same revision (every addition or removal changes it) and
 * admission has published or withdrawn nothing (its epoch), so the rule atoms
 * are kept with those four keys, one array that a rebuild overwrites, and the
 * list is built again in the caller's arena, in the same order, on every
 * call: what is remembered is bounded by the number of rules, and the list
 * lives exactly as long as an uncached one.  The atoms are the space's own,
 * which live while the space keeps them, that is, at least until its
 * revision moves. */
typedef struct {
    const Space *space;
    uint64_t instance;
    uint64_t revision;
    uint64_t admission;
    Atom **rules;
    size_t count;
    size_t cap;
    bool valid;
} PrimeKernelRuleCache;

static _Thread_local PrimeKernelRuleCache *t_kernel_rule_cache;
static pthread_key_t g_kernel_rule_cache_key;
static pthread_once_t g_kernel_rule_cache_key_once = PTHREAD_ONCE_INIT;
static bool g_kernel_rule_cache_key_ready = false;

static void prime_kernel_rule_cache_destroy(void *raw) {
    PrimeKernelRuleCache *cache = raw;
    if (!cache) return;
    free(cache->rules);
    free(cache);
}

static void prime_kernel_rule_cache_make_key(void) {
    g_kernel_rule_cache_key_ready =
        pthread_key_create(&g_kernel_rule_cache_key, prime_kernel_rule_cache_destroy) == 0;
}

static PrimeKernelRuleCache *prime_kernel_rule_cache_get(void) {
    if (t_kernel_rule_cache) return t_kernel_rule_cache;
    pthread_once(&g_kernel_rule_cache_key_once, prime_kernel_rule_cache_make_key);
    if (!g_kernel_rule_cache_key_ready) return NULL;
    PrimeKernelRuleCache *cache = calloc(1u, sizeof *cache);
    if (!cache) return NULL;
    if (pthread_setspecific(g_kernel_rule_cache_key, cache) != 0) {
        prime_kernel_rule_cache_destroy(cache);
        return NULL;
    }
    t_kernel_rule_cache = cache;
    return cache;
}

static bool prime_kernel_rule_cache_hit(const PrimeKernelRuleCache *cache,
                                        const Space *space) {
    return cache && cache->valid && cache->space == space &&
           cache->instance == space_instance_id(space) &&
           cache->revision == space_revision(space) &&
           cache->admission == prime_scoped_judgment_admission_epoch();
}

static void prime_kernel_rule_cache_note(PrimeKernelRuleCache *cache, Atom *atom) {
    if (!cache || !cache->valid) return;
    if (cache->count == cache->cap) {
        size_t next = cache->cap ? cache->cap * 2u : 64u;
        Atom **grown = realloc(cache->rules, sizeof(Atom *) * next);
        if (!grown) {
            cache->valid = false;
            return;
        }
        cache->rules = grown;
        cache->cap = next;
    }
    cache->rules[cache->count++] = atom;
}

/* Physical candidate coordinates belong to the indexed store, not to an
 * overlay's logical view. Read overlays through their snapshot-aware accessor
 * so inherited rules, removed rows and local additions have the same meaning
 * during declaration admission and ordinary checking. Preserve the source
 * rule atoms rather than instantiating their internal pattern variables. */
Atom *prime_semantics_kernel_rules(Arena *a, Space *space) {
    if (!space) return NULL;
    Atom *list = NULL;
    if (space->overlay_base) {
        CettaCount count = space_length64(space);
        for (CettaIndex i = 0u; i < count; i++)
            list = prime_kernel_rule_cons(
                a, space, space_get_at64(space, i), list);
        return list;
    }
    PrimeKernelRuleCache *cache = prime_kernel_rule_cache_get();
    if (prime_kernel_rule_cache_hit(cache, space)) {
        for (size_t i = 0u; i < cache->count; i++)
            list = prime_kernel_rule_wrap(a, cache->rules[i], list);
        return list;
    }
    uint64_t admission = prime_scoped_judgment_admission_epoch();
    uint64_t revision = space_revision(space);
    if (cache) {
        cache->valid = true;
        cache->count = 0u;
    }
    Atom *items[5] = {atom_symbol(a, "type:rule"), atom_var(a, "h"), atom_var(a, "n"),
                      atom_var(a, "p"), atom_var(a, "r")};
    Atom *pattern = atom_expr(a, items, 5u);
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    for (CettaIndex i = 0u; i < count; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        Atom *extended = prime_kernel_rule_cons(a, space, atom, list);
        if (extended != list) prime_kernel_rule_cache_note(cache, atom);
        list = extended;
    }
    free(candidates);
    /* Kept only when nothing moved while the rules were read. */
    if (cache) {
        cache->valid = cache->valid && space_revision(space) == revision &&
                       prime_scoped_judgment_admission_epoch() == admission;
        cache->space = space;
        cache->instance = space_instance_id(space);
        cache->revision = revision;
        cache->admission = admission;
    }
    return list;
}

/* The written spelling of a kernel normal form. An unquoted binder or wire
 * constructor declines the whole term, so ordinary evaluation never prints
 * an internal spelling in place of the source call. */
static Atom *prime_quote_open(Arena *arena, Atom *term, uint64_t depth,
                              Atom *binder);

/* The arguments of an application spine `(App (App f a1) a2) ...`, as many
 * as it has: its length, then the arguments in the order of the spine from
 * the outermost (last) argument, and its head. */
static size_t prime_quote_spine_length(Atom *term) {
    size_t argc = 0u;
    for (Atom *cursor = term;
         cursor && cursor->kind == ATOM_EXPR && cursor->expr.len == 3u &&
         is_symbol_named(cursor->expr.elems[0], "App");
         cursor = cursor->expr.elems[1])
        argc++;
    return argc;
}

static Atom *prime_quote_spine_arguments(Atom *term, Atom **args, size_t argc) {
    Atom *cursor = term;
    for (size_t i = 0u; i < argc; i++) {
        args[i] = cursor->expr.elems[2];
        cursor = cursor->expr.elems[1];
    }
    return cursor;
}

static Atom *prime_quote_runtime_term(Arena *arena, Atom *term) {
    if (!arena || !term) return NULL;
    /* A symbol standing alone is a universe or a constant; only the
     * universes have another authored spelling.  A constant keeps its name,
     * whatever it spells. */
    if (term->kind == ATOM_SYMBOL)
        return is_symbol_named(term, "U0") || is_symbol_named(term, "U1")
            ? cetta_prime_regular_term_authored_symbol_v1(arena, term)
            : term;
    if (term->kind != ATOM_EXPR) return term;
    if (term->expr.len == 2u &&
        is_symbol_named(term->expr.elems[0], "DeclConst") &&
        term->expr.elems[1] && term->expr.elems[1]->kind == ATOM_SYMBOL)
        return term->expr.elems[1];
    if (term->expr.len == 3u && is_symbol_named(term->expr.elems[0], "App")) {
        size_t argc = prime_quote_spine_length(term);
        Atom **args = arena_alloc(arena, sizeof(Atom *) * argc);
        if (!args) return NULL;
        Atom *cursor = prime_quote_spine_arguments(term, args, argc);
        Atom *head = prime_quote_runtime_term(arena, cursor);
        if (!head) return NULL;
        Atom **items = arena_alloc(arena, sizeof(Atom *) * (argc + 1u));
        if (!items) return NULL;
        items[0] = head;
        for (size_t i = 0u; i < argc; i++) {
            Atom *arg = prime_quote_runtime_term(arena, args[argc - 1u - i]);
            if (!arg) return NULL;
            items[i + 1u] = arg;
        }
        return atom_expr(arena, items, (CettaExprLen)(argc + 1u));
    }
    if (term->expr.len == 3u && is_symbol_named(term->expr.elems[0], "Pair")) {
        Atom *first = prime_quote_runtime_term(arena, term->expr.elems[1]);
        Atom *second = prime_quote_runtime_term(arena, term->expr.elems[2]);
        if (!first || !second) return NULL;
        Atom *items[3] = {atom_symbol(arena, "pair"), first, second};
        return atom_expr(arena, items, 3u);
    }
    if (term->expr.len == 2u &&
        (is_symbol_named(term->expr.elems[0], "Fst") ||
         is_symbol_named(term->expr.elems[0], "Snd") ||
         is_symbol_named(term->expr.elems[0], "Refl"))) {
        const char *authored = is_symbol_named(term->expr.elems[0], "Fst") ? "fst"
                            : is_symbol_named(term->expr.elems[0], "Snd") ? "snd"
                            : "refl";
        Atom *arg = prime_quote_runtime_term(arena, term->expr.elems[1]);
        if (!arg) return NULL;
        Atom *items[2] = {atom_symbol(arena, authored), arg};
        return atom_expr(arena, items, 2u);
    }
    if (term->expr.len == 4u && is_symbol_named(term->expr.elems[0], "Id")) {
        Atom *carrier = prime_quote_runtime_term(arena, term->expr.elems[1]);
        Atom *left = prime_quote_runtime_term(arena, term->expr.elems[2]);
        Atom *right = prime_quote_runtime_term(arena, term->expr.elems[3]);
        if (!carrier || !left || !right) return NULL;
        Atom *items[4] = {atom_symbol(arena, "id"), carrier, left, right};
        return atom_expr(arena, items, 4u);
    }
    if (is_symbol_named(term->expr.elems[0], "Lam") ||
        is_symbol_named(term->expr.elems[0], "Pi") ||
        is_symbol_named(term->expr.elems[0], "Sigma"))
        return prime_quote_open(arena, term, 0u, NULL);
    Atom *quoted = cetta_prime_regular_term_quote_intrinsic_v1(arena, term);
    if (!quoted || quoted->kind != ATOM_EXPR || quoted->expr.len == 0u ||
        quoted->expr.elems[0]->kind != ATOM_SYMBOL)
        return quoted && quoted->kind != ATOM_EXPR ? quoted : NULL;
    if (is_symbol_named(quoted->expr.elems[0], "Lam") ||
        is_symbol_named(quoted->expr.elems[0], "Pi") ||
        is_symbol_named(quoted->expr.elems[0], "Sigma") ||
        is_symbol_named(quoted->expr.elems[0], "DeclConst") ||
        is_symbol_named(quoted->expr.elems[0], "App") ||
        is_symbol_named(quoted->expr.elems[0], "PVar") ||
        is_symbol_named(quoted->expr.elems[0], "FVar"))
        return NULL;
    return quoted;
}

/* A matcher variable is not a lexical binder, so a quoted lambda uses a
 * fresh symbol. Reusing one interned spelling would capture nested binders. */
static Atom *prime_quote_fresh_binder(Arena *arena) {
    char spelling[32];
    snprintf(spelling, sizeof spelling, "b%llu",
             (unsigned long long)fresh_var_id());
    return atom_symbol(arena, spelling);
}

static Atom *prime_quote_level(Arena *arena, Atom *term,
                               Atom **binders, uint64_t nbinders) {
    if (!term) return NULL;
    if (term->kind != ATOM_EXPR) return prime_quote_runtime_term(arena, term);
    if (term->expr.len == 2u && is_symbol_named(term->expr.elems[0], "idx") &&
        term->expr.elems[1] && term->expr.elems[1]->kind == ATOM_GROUNDED &&
        term->expr.elems[1]->ground.gkind == GV_INT &&
        term->expr.elems[1]->ground.ival >= 0) {
        uint64_t index = (uint64_t)term->expr.elems[1]->ground.ival;
        /* idx 0 is the nearest binder. */
        if (index < nbinders)
            return binders[nbinders - 1u - index];
        Atom *items[2] = {atom_symbol(arena, "idx"), term->expr.elems[1]};
        return atom_expr(arena, items, 2u);
    }
    if (term->expr.len == 2u && is_symbol_named(term->expr.elems[0], "DeclConst"))
        return prime_quote_runtime_term(arena, term);
    if (is_symbol_named(term->expr.elems[0], "Lam")) {
        Atom *fresh = prime_quote_fresh_binder(arena);
        Atom **stacked = arena_alloc(arena, sizeof(Atom *) * (size_t)(nbinders + 1u));
        if (!stacked) return NULL;
        for (uint64_t i = 0u; i < nbinders; i++)
            stacked[i] = binders[i];
        stacked[nbinders] = fresh;
        Atom *body_term = term->expr.len == 2u ? term->expr.elems[1]
                                               : term->expr.elems[2];
        Atom *body = prime_quote_level(arena, body_term, stacked, nbinders + 1u);
        if (!body) return NULL;
        Atom *items[3] = {atom_symbol(arena, "lam"), fresh, body};
        return atom_expr(arena, items, 3u);
    }
    if (term->expr.len == 2u &&
        (is_symbol_named(term->expr.elems[0], "Fst") ||
         is_symbol_named(term->expr.elems[0], "Snd") ||
         is_symbol_named(term->expr.elems[0], "Refl"))) {
        const char *authored = is_symbol_named(term->expr.elems[0], "Fst") ? "fst"
                            : is_symbol_named(term->expr.elems[0], "Snd") ? "snd"
                            : "refl";
        Atom *arg = prime_quote_level(arena, term->expr.elems[1], binders, nbinders);
        if (!arg) return NULL;
        Atom *items[2] = {atom_symbol(arena, authored), arg};
        return atom_expr(arena, items, 2u);
    }
    if (term->expr.len == 3u && is_symbol_named(term->expr.elems[0], "Pair")) {
        Atom *first = prime_quote_level(arena, term->expr.elems[1], binders, nbinders);
        Atom *second = prime_quote_level(arena, term->expr.elems[2], binders, nbinders);
        if (!first || !second) return NULL;
        Atom *items[3] = {atom_symbol(arena, "pair"), first, second};
        return atom_expr(arena, items, 3u);
    }
    if (term->expr.len == 4u && is_symbol_named(term->expr.elems[0], "Id")) {
        Atom *carrier = prime_quote_level(arena, term->expr.elems[1], binders, nbinders);
        Atom *left = prime_quote_level(arena, term->expr.elems[2], binders, nbinders);
        Atom *right = prime_quote_level(arena, term->expr.elems[3], binders, nbinders);
        if (!carrier || !left || !right) return NULL;
        Atom *items[4] = {atom_symbol(arena, "id"), carrier, left, right};
        return atom_expr(arena, items, 4u);
    }
    if (term->expr.len == 3u && is_symbol_named(term->expr.elems[0], "App")) {
        size_t argc = prime_quote_spine_length(term);
        Atom **args = arena_alloc(arena, sizeof(Atom *) * argc);
        if (!args) return NULL;
        Atom *cursor = prime_quote_spine_arguments(term, args, argc);
        Atom *head = prime_quote_level(arena, cursor, binders, nbinders);
        if (!head) return NULL;
        Atom **items = arena_alloc(arena, sizeof(Atom *) * (argc + 1u));
        if (!items) return NULL;
        items[0] = head;
        for (size_t i = 0u; i < argc; i++) {
            Atom *arg = prime_quote_level(
                arena, args[argc - 1u - i], binders, nbinders);
            if (!arg) return NULL;
            items[i + 1u] = arg;
        }
        return atom_expr(arena, items, (CettaExprLen)(argc + 1u));
    }
    return NULL;
}

static Atom *prime_quote_open(Arena *arena, Atom *term, uint64_t depth,
                              Atom *binder) {
    (void)depth;
    Atom *stacked[1];
    if (binder) {
        stacked[0] = binder;
        return prime_quote_level(arena, term, stacked, 1u);
    }
    return prime_quote_level(arena, term, NULL, 0u);
}

static Atom *prime_quote_shared_redex(Arena *arena, Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !is_symbol_named(term->expr.elems[0], "App"))
        return NULL;
    Atom *function = term->expr.elems[1];
    if (!function || function->kind != ATOM_EXPR ||
        !is_symbol_named(function->expr.elems[0], "Lam"))
        return NULL;
    Atom *body = function->expr.len == 2u ? function->expr.elems[1]
                                          : function->expr.elems[2];
    Atom *binder = atom_var_with_id(arena, "pack", fresh_var_id());
    Atom *argument = prime_quote_runtime_term(arena, term->expr.elems[2]);
    Atom *quoted_body = prime_quote_open(arena, body, 0u, binder);
    if (!argument || !quoted_body) return NULL;
    Atom *items[4] = {atom_symbol(arena, "let"), binder, argument, quoted_body};
    return atom_expr(arena, items, 4u);
}

static size_t prime_declared_arity(Atom *type);

/* Whether the space declares `name` at a type taking at least `argc`
 * arguments. */
static bool prime_program_declares(Space *space, Arena *arena, Atom *name,
                                   size_t argc) {
    if (!space || !name || name->kind != ATOM_SYMBOL) return false;
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(space, arena, name, &declared);
    bool fits = false;
    for (uint32_t i = 0u; i < count && !fits; i++)
        fits = argc <= prime_declared_arity(declared[i]);
    free(declared);
    return fits;
}

static Atom *prime_authored_projection(Arena *arena, Atom *call) {
    (void)arena;
    if (!call || call->kind != ATOM_EXPR || call->expr.len != 2u) return NULL;
    Atom *head = call->expr.elems[0];
    Atom *argument = call->expr.elems[1];
    if (!head || !argument || argument->kind != ATOM_EXPR ||
        argument->expr.len != 3u ||
        !is_symbol_named(argument->expr.elems[0], "pair"))
        return NULL;
    if (is_symbol_named(head, "fst")) return argument->expr.elems[1];
    if (is_symbol_named(head, "snd")) return argument->expr.elems[2];
    return NULL;
}

Atom *prime_semantics_project_pair(Atom *call) {
    return prime_authored_projection(NULL, call);
}

/* A lambda binder is a name key: a symbol names itself, `(quote K)` names K
 * (`cetta_prime_name_key_v1`), and a variable binder, which the evaluator
 * also admits, is itself.  The body refers to the bound value through the
 * bare name or the drop `(unquote n)` of the binder's name n.  A quotation is
 * sealed: `(quote x)` is the literal name x even under a binder `x`, so beta
 * never observes how its argument was written and respects the equality of
 * arguments. */

static bool prime_form(const Atom *atom, const char *head) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 2u &&
           is_symbol_named(atom->expr.elems[0], head);
}

/* The key a binder names, or NULL for a bare `_`, which binds nothing. */
static Atom *prime_binder_key(Atom *binder) {
    if (!binder) return NULL;
    binder = cetta_prime_name_normal_v1(binder);
    if (binder->kind == ATOM_VAR) return binder;
    if (prime_form(binder, "quote") && binder->expr.elems[1]->kind == ATOM_VAR)
        return binder->expr.elems[1];
    Atom *key = NULL;
    return cetta_prime_name_key_v1(binder, true, &key, NULL) ==
                   CETTA_PRIME_NAME_OK_V1
               ? key
               : NULL;
}

static bool prime_same_key(const Atom *left, const Atom *right) {
    if (!left || !right) return false;
    if (left->kind == ATOM_VAR || right->kind == ATOM_VAR)
        return left->kind == ATOM_VAR && right->kind == ATOM_VAR &&
               left->var_id == right->var_id;
    return atom_eq((Atom *)left, (Atom *)right);
}

/* Whether `term`, taken whole, refers to the value of the binder with key
 * `key`.  A bare `_` is a hole and refers to nothing. */
static bool prime_binder_reference(const Atom *key, Atom *term) {
    if (!key || !term) return false;
    if (term->kind == ATOM_VAR ||
        (term->kind == ATOM_SYMBOL && !is_symbol_named(term, "_")))
        return prime_same_key(key, term);
    if (!prime_form(term, "unquote")) return false;
    Atom *name = cetta_prime_name_normal_v1(term->expr.elems[1]);
    return prime_form(name, "quote") && prime_same_key(key, name->expr.elems[1]);
}

/* An authored lambda `(lam binders body)`, its binders read with the kernel's
 * grammar (`cetta_prime_lambda_binder_groups_v1`).  The evaluator binds the
 * kernel's names (symbols and explicitly quoted structural names) and
 * variables; a bare `_` binds nothing.  An elaborated lambda may carry a
 * fourth element, the list of its own names `($y ...)`
 * (prime_semantics_elaborate_form): the names its body quantifies, which
 * each application copies afresh.  Own names bind in the body as the
 * binders do. */
typedef struct {
    CettaPrimeLambdaBinderGroupV1 *groups;
    size_t count;
    bool listed;
    Atom *own;
} PrimeLambdaTelescope;

/* An own-name list, `(OWN RECORD $y ...)`: the reserved head is a
 * symbol no source text can spell (it holds a space), so an elaborated list
 * cannot be forged by writing a last element.  RECORD is the scope profile
 * the template was elaborated under (prime_scope_record) and the variables
 * after it, from PRIME_OWN_FIRST, are the template's own slots.  A written
 * last element of variables, `($y ...)`, names what the template owns, and
 * braces before the parameter, `{$t ...}`, what it shares (read by the
 * elaboration, prime_elab_lambda and prime_elab_iter).  The printer shows the
 * own list's names last, and nothing for an empty one
 * (atom_is_prime_own_list). */
#define PRIME_OWN_FIRST 2u

static bool prime_own_list(const Atom *own) {
    return atom_is_prime_own_list(own);
}

static bool prime_own_binds(const Atom *own, const Atom *key) {
    if (!own || !key || key->kind != ATOM_VAR) return false;
    for (CettaExprIndex i = PRIME_OWN_FIRST; i < own->expr.len; i++)
        if (prime_same_key(key, own->expr.elems[i])) return true;
    return false;
}

/* `list` with its element `index` replaced by `element`. */
static Atom *prime_list_with(Arena *arena, Atom *list, CettaExprIndex index,
                             Atom *element) {
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (size_t)list->expr.len);
    if (!items) return NULL;
    for (CettaExprIndex i = 0u; i < list->expr.len; i++)
        items[i] = list->expr.elems[i];
    items[index] = element;
    return atom_expr(arena, items, list->expr.len);
}

static bool prime_lambda_telescope(Arena *arena, Atom *term,
                                   PrimeLambdaTelescope *out) {
    if (!arena || !term || term->kind != ATOM_EXPR ||
        (term->expr.len != 3u && term->expr.len != 4u) ||
        !is_symbol_named(term->expr.elems[0], "lam"))
        return false;
    if (term->expr.len == 4u && !prime_own_list(term->expr.elems[3]))
        return false;
    PrimeLambdaTelescope telescope = {0};
    if (cetta_prime_lambda_binder_groups_v1(
            arena, term->expr.elems[1], &telescope.groups, &telescope.count,
            &telescope.listed) != CETTA_PRIME_LAMBDA_BINDERS_OK_V1)
        return false;
    for (size_t g = 0u; g < telescope.count; g++) {
        for (size_t i = 0u; i < telescope.groups[g].names_count; i++) {
            Atom *name = cetta_prime_lambda_binder_name_v1(
                &telescope.groups[g], i);
            if (!name) return false;
            if (name->kind != ATOM_VAR) {
                CettaPrimeNameStatusV1 status =
                    cetta_prime_name_key_v1(name, true, NULL, NULL);
                if (status != CETTA_PRIME_NAME_OK_V1 &&
                    status != CETTA_PRIME_NAME_ANONYMOUS_V1)
                    return false;
            }
        }
    }
    telescope.own = term->expr.len == 4u ? term->expr.elems[3] : NULL;
    *out = telescope;
    return true;
}

static bool prime_group_binds(const CettaPrimeLambdaBinderGroupV1 *group,
                              const Atom *key) {
    for (size_t i = 0u; i < group->names_count; i++) {
        Atom *name = cetta_prime_lambda_binder_name_v1(group, i);
        if (prime_same_key(key, prime_binder_key(name))) return true;
    }
    return false;
}

/* The templates of map-atom and foldl-atom (rule 3 of the binding
 * structure): `(map-atom L $e T)` and `(foldl-atom L init $acc $item O)`,
 * each with an optional own-name list last.  The list and the initial value
 * belong to the enclosing scope; the parameters and the own names bind in
 * the template body.  Prime only; HE keeps its own map-atom. */
typedef struct {
    CettaExprIndex first_param;
    CettaExprIndex param_count;
    CettaExprIndex body;
    CettaExprIndex own; /* 0: no own list */
} PrimeIterTemplate;

static bool prime_iter_template(const Atom *term, PrimeIterTemplate *out) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 4u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    SymbolId head = term->expr.elems[0]->sym_id;
    CettaExprLen len = term->expr.len;
    PrimeIterTemplate view;
    if (head == g_builtin_syms.map_atom && (len == 4u || len == 5u)) {
        view = (PrimeIterTemplate){2u, 1u, 3u, len == 5u ? 4u : 0u};
    } else if (head == g_builtin_syms.foldl_atom && (len == 6u || len == 7u)) {
        view = (PrimeIterTemplate){3u, 2u, 5u, len == 7u ? 6u : 0u};
    } else {
        return false;
    }
    for (CettaExprIndex p = 0u; p < view.param_count; p++) {
        Atom *param = term->expr.elems[view.first_param + p];
        if (!param || param->kind != ATOM_VAR) return false;
    }
    if (view.own && !prime_own_list(term->expr.elems[view.own]))
        return false;
    if (out) *out = view;
    return true;
}

static bool prime_iter_binds(const Atom *term, const PrimeIterTemplate *view,
                             const Atom *key) {
    for (CettaExprIndex p = 0u; p < view->param_count; p++)
        if (prime_same_key(key, term->expr.elems[view->first_param + p]))
            return true;
    return view->own && prime_own_binds(term->expr.elems[view->own], key);
}

/* `(new ($h ...) body)`: fresh private slots, one set per evaluation of the
 * form (prime_semantics_new_open); inside a lambda body that is once per
 * activation.  Its names are lexical binders of the body, as a lambda's
 * parameters are: owned by the `new`, hidden from the caller, never reached
 * by an outer substitution and never captured by spelling.  It is the
 * written form of a private hole under lexical-fresh, and the translation
 * from rule M writes it around a body with names that no pattern introduces
 * (TemplateScope's `Src.new`).  Any other shape is data. */
static bool prime_new_form(const Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL ||
        term->expr.elems[0]->sym_id != g_builtin_syms.prime_new)
        return false;
    const Atom *names = term->expr.elems[1];
    if (!names || names->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < names->expr.len; i++)
        if (!names->expr.elems[i] || names->expr.elems[i]->kind != ATOM_VAR)
            return false;
    return true;
}

static bool prime_new_binds(const Atom *term, const Atom *key) {
    const Atom *names = term->expr.elems[1];
    for (CettaExprIndex i = 0u; i < names->expr.len; i++)
        if (prime_same_key(key, names->expr.elems[i])) return true;
    return false;
}

/* Contextual code `(quote M (k1 ... kn))`: the code M under the binders
 * k1 ... kn (binder keys, the outermost first), the binding quote's shape:
 * the list binds its names in M.
 * Matching a quotation through one of its own binders hands the part over
 * this way (prime_semantics_contextual_match), never as an open term;
 * `lift let` fills its binders, and `*` opens it as the function of them.
 * It is sealed as a quotation is: no substitution enters it. */
bool prime_semantics_contextual_code(const Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL ||
        term->expr.elems[0]->sym_id != g_builtin_syms.quote)
        return false;
    const Atom *keys = term->expr.elems[2];
    if (!keys || keys->kind != ATOM_EXPR || keys->expr.len == 0u ||
        atom_is_prime_own_list(keys))
        return false;
    for (CettaExprIndex i = 0u; i < keys->expr.len; i++)
        if (!prime_binder_key(keys->expr.elems[i])) return false;
    return true;
}

/* Code of either shape: a quotation, or contextual code. */
static bool prime_code_like(const Atom *term) {
    return atom_is_quotation(term) || prime_semantics_contextual_code(term);
}

/* The code `code` (a quotation or contextual code) with `payload` in place
 * of its own, keeping its binders. */
static Atom *prime_code_with_payload(Arena *arena, Atom *code, Atom *payload) {
    if (!payload) return NULL;
    return code->expr.len == 3u
        ? atom_expr3(arena, code->expr.elems[0], payload, code->expr.elems[2])
        : atom_expr2(arena, code->expr.elems[0], payload);
}

/* The pattern positions of the pattern binders (rule 4): a name written
 * there, quoted or not, is a store-name occurrence that binds.  `(P B)`
 * pairs are the branches of case and switch and the bindings of let*.  The
 * table is the binder table of the elaboration (equation heads, case,
 * switch, let, let*, unify, match, chain, filter-atom). */
typedef enum {
    PRIME_CHILD_VALUE = 0,
    PRIME_CHILD_PATTERN,
    PRIME_CHILD_PATTERN_PAIRS,
} PrimeChildRole;

static PrimeChildRole prime_child_role(const Atom *term,
                                       CettaExprIndex index) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL ||
        index == 0u)
        return PRIME_CHILD_VALUE;
    SymbolId head = term->expr.elems[0]->sym_id;
    CettaExprLen len = term->expr.len;
    if (head == g_builtin_syms.equals && len == 3u)
        return index == 1u ? PRIME_CHILD_PATTERN : PRIME_CHILD_VALUE;
    if ((head == g_builtin_syms.case_text ||
         head == g_builtin_syms.switch_text ||
         head == g_builtin_syms.switch_minimal) &&
        (len == 3u || len == 4u))
        return index == 2u ? PRIME_CHILD_PATTERN_PAIRS : PRIME_CHILD_VALUE;
    if (head == g_builtin_syms.let && len == 4u)
        return index == 1u ? PRIME_CHILD_PATTERN : PRIME_CHILD_VALUE;
    if (head == g_builtin_syms.let_star && len == 3u)
        return index == 1u ? PRIME_CHILD_PATTERN_PAIRS : PRIME_CHILD_VALUE;
    if (head == g_builtin_syms.unify && len == 5u)
        return index == 1u || index == 2u ? PRIME_CHILD_PATTERN
                                          : PRIME_CHILD_VALUE;
    if ((head == g_builtin_syms.match || head == g_builtin_syms.chain ||
         head == g_builtin_syms.filter_atom) &&
        len == 4u)
        return index == 2u ? PRIME_CHILD_PATTERN : PRIME_CHILD_VALUE;
    return PRIME_CHILD_VALUE;
}

/* The slot a binder is filling (prime_semantics_subst_var).  Inside a
 * quotation the elaboration left only the holes in scope of their binder,
 * every other quoted mention being a variable of the quotation's own; so the
 * slot is filled there too, as a hole of quoted code: textually, since the
 * code's own binders are syntax and bind nothing at this level.  Any other
 * substitution, an alpha renaming included, stops at the seal. */
static __thread const Atom *g_prime_subst_quoted_hole = NULL;

static Atom *prime_bindings_formed_node(Arena *arena, Atom *node,
                                        bool in_code);

/* `value` for every occurrence of the variable `id` in quoted code.  An
 * expression whose head it fills with a symbol forms the construct it
 * names, in code (prime_bindings_formed_node): a binder formed so is
 * sealed, as every substitution's is. */
static Atom *prime_fill_quoted_hole(Arena *arena, Atom *term, VarId id,
                                    Atom *value) {
    if (!term) return NULL;
    if (term->kind == ATOM_VAR) return term->var_id == id ? value : term;
    if (term->kind != ATOM_EXPR || !atom_has_vars(term)) return term;
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        Atom *child = term->expr.elems[i];
        Atom *next = prime_fill_quoted_hole(arena, child, id, value);
        if (!next) return NULL;
        if (next != child && !items) {
            items = arena_alloc(arena, sizeof(Atom *) * (size_t)term->expr.len);
            if (!items) return NULL;
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = term->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    if (!items) return term;
    Atom *filled = atom_expr(arena, items, term->expr.len);
    if (filled && term->expr.elems[0]->kind == ATOM_VAR &&
        term->expr.elems[0]->var_id == id && items[0] &&
        items[0]->kind == ATOM_SYMBOL)
        filled = prime_bindings_formed_node(arena, filled, true);
    return filled;
}

/* What a hole holds where it is spliced into quoted code: its value, or,
 * for contextual code (a part taken out from under binders of its code),
 * the part's own syntax, which the code around the hole puts back under
 * binders of its own. */
static Atom *prime_spliced_syntax(Atom *value) {
    return prime_semantics_contextual_code(value) ? value->expr.elems[1]
                                                  : value;
}

static Atom *prime_value_into_code(Arena *arena, Atom *value);
static Atom *prime_formed_by_substitution(Arena *arena, Atom *term);

/* Whether `term` uses the binder with key `key` freely.  The written types of
 * a group are read before the group's own names are bound; a group that binds
 * the key hides it from the later groups and the body, and so do an own
 * name and a template parameter. */
static bool prime_binder_occurs_free_in(Arena *arena, const Atom *key,
                                        Atom *term, bool pattern);

static bool prime_binder_occurs_free_pairs(Arena *arena, const Atom *key,
                                           Atom *pairs) {
    if (!pairs || pairs->kind != ATOM_EXPR)
        return prime_binder_occurs_free_in(arena, key, pairs, false);
    for (CettaExprIndex i = 0u; i < pairs->expr.len; i++) {
        Atom *pair = pairs->expr.elems[i];
        if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
            if (prime_binder_occurs_free_in(arena, key, pair->expr.elems[0],
                                            true) ||
                prime_binder_occurs_free_in(arena, key, pair->expr.elems[1],
                                            false))
                return true;
        } else if (prime_binder_occurs_free_in(arena, key, pair, false)) {
            return true;
        }
    }
    return false;
}

static bool prime_binder_occurs_free_in(Arena *arena, const Atom *key,
                                        Atom *term, bool pattern) {
    if (!key || !term) return false;
    if (prime_binder_reference(key, term)) return true;
    if (term->kind != ATOM_EXPR) return false;
    /* A quotation in value position is sealed: nothing under it refers to a
     * binder.  So is contextual code. */
    if (!pattern && prime_code_like(term)) return false;
    /* In a pattern position a lambda or template form is a pattern that
     * takes such forms apart: its names are store-name occurrences. */
    PrimeLambdaTelescope telescope;
    if (!pattern && prime_lambda_telescope(arena, term, &telescope)) {
        for (size_t g = 0u; g < telescope.count; g++) {
            const CettaPrimeLambdaBinderGroupV1 *group = &telescope.groups[g];
            for (size_t t = 0u; group->typed && t < group->types_count; t++) {
                if (prime_binder_occurs_free_in(
                        arena, key,
                        group->syntax->expr.elems[group->types_start + t],
                        pattern))
                    return true;
            }
            if (prime_group_binds(group, key)) return false;
        }
        if (prime_own_binds(telescope.own, key)) return false;
        return prime_binder_occurs_free_in(arena, key, term->expr.elems[2],
                                           pattern);
    }
    PrimeIterTemplate iter;
    if (!pattern && prime_iter_template(term, &iter)) {
        for (CettaExprIndex i = 1u; i < iter.first_param; i++)
            if (prime_binder_occurs_free_in(arena, key, term->expr.elems[i],
                                            pattern))
                return true;
        if (prime_iter_binds(term, &iter, key)) return false;
        return prime_binder_occurs_free_in(arena, key,
                                           term->expr.elems[iter.body],
                                           pattern);
    }
    if (!pattern && prime_new_form(term))
        return !prime_new_binds(term, key) &&
               prime_binder_occurs_free_in(arena, key, term->expr.elems[2],
                                           pattern);
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                      : prime_child_role(term, i);
        bool occurs = role == PRIME_CHILD_PATTERN_PAIRS
            ? prime_binder_occurs_free_pairs(arena, key, term->expr.elems[i])
            : prime_binder_occurs_free_in(arena, key, term->expr.elems[i],
                                          role == PRIME_CHILD_PATTERN);
        if (occurs) return true;
    }
    return false;
}

static bool prime_binder_occurs_free(Arena *arena, const Atom *key,
                                     Atom *term) {
    return prime_binder_occurs_free_in(arena, key, term, false);
}

static Atom *prime_subst_in(Arena *arena, Atom *term, const Atom *key,
                            Atom *value, bool pattern);

/* A group with one element replaced. */
static Atom *prime_group_with(Arena *arena,
                              const CettaPrimeLambdaBinderGroupV1 *group,
                              size_t position, Atom *element) {
    if (!group->typed) return element;
    Atom *syntax = group->syntax;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (size_t)syntax->expr.len);
    if (!items) return NULL;
    for (CettaExprIndex i = 0u; i < syntax->expr.len; i++)
        items[i] = syntax->expr.elems[i];
    items[position] = element;
    return atom_expr(arena, items, syntax->expr.len);
}

/* Whether `key` occurs free in the scope of the binders of group `g`: the
 * written types of the later groups and the body, each until a later group
 * or the own list binds the key.  A binder whose scope does not use the key
 * is never reached by the substitution, so it is never renamed. */
static bool prime_key_free_after(Arena *arena,
                                 const CettaPrimeLambdaBinderGroupV1 *groups,
                                 size_t count, Atom **group_syntax, size_t g,
                                 Atom *body, Atom *own, const Atom *key,
                                 bool pattern) {
    for (size_t h = g + 1u; h < count; h++) {
        CettaPrimeLambdaBinderGroupV1 group = groups[h];
        group.syntax = group_syntax[h];
        for (size_t t = 0u; group.typed && t < group.types_count; t++)
            if (prime_binder_occurs_free_in(
                    arena, key, group.syntax->expr.elems[group.types_start + t],
                    pattern))
                return true;
        if (prime_group_binds(&group, key)) return false;
    }
    if (own && prime_own_binds(own, key)) return false;
    return prime_binder_occurs_free_in(arena, key, body, pattern);
}

/* Substitute `value` for the binder with key `key` in a telescope's written
 * types, its own names and its body.  `group_syntax` holds each group's
 * current syntax and is updated in place, as `own` is.  A binder or own
 * name that `value` uses freely is renamed in its scope before the
 * replacement reaches it, when the key occurs in that scope.  With
 * `keep_first_types` the first group's written types are not entered: they
 * belong to the context outside this telescope, as when beta splits a
 * group. */
static bool prime_subst_telescope_in(
    Arena *arena, const CettaPrimeLambdaBinderGroupV1 *groups, size_t count,
    Atom **group_syntax, Atom **body, Atom **own, const Atom *key,
    Atom *value, bool keep_first_types, bool pattern) {
    for (size_t g = 0u; g < count; g++) {
        CettaPrimeLambdaBinderGroupV1 group = groups[g];
        group.syntax = group_syntax[g];
        for (size_t t = 0u; group.typed && !(keep_first_types && g == 0u) &&
                            t < group.types_count; t++) {
            size_t position = group.types_start + t;
            Atom *type = group.syntax->expr.elems[position];
            Atom *next = prime_subst_in(arena, type, key, value, pattern);
            if (!next) return false;
            if (next == type) continue;
            group.syntax = prime_group_with(arena, &group, position, next);
            if (!group.syntax) return false;
            group_syntax[g] = group.syntax;
        }
        if (prime_group_binds(&group, key)) return true;
        for (size_t i = 0u; i < group.names_count; i++) {
            Atom *name = cetta_prime_lambda_binder_name_v1(&group, i);
            Atom *name_key = prime_binder_key(name);
            if (!name_key || !prime_binder_occurs_free(arena, name_key, value) ||
                !prime_key_free_after(arena, groups, count, group_syntax, g,
                                      *body, own ? *own : NULL, key, pattern))
                continue;
            /* The binder would capture a name the value uses. Rename it.
             * Alpha-equivalent binders must not change the value. */
            Atom *fresh = atom_var_with_id(arena, "binder", fresh_var_id());
            if (!fresh ||
                !prime_subst_telescope_in(arena, groups + g + 1u,
                                          count - g - 1u,
                                          group_syntax + g + 1u, body, own,
                                          name_key, fresh, false, pattern))
                return false;
            group.syntax = prime_group_with(
                arena, &group, group.typed ? group.names_start + i : 0u,
                fresh);
            if (!group.syntax) return false;
            group_syntax[g] = group.syntax;
        }
    }
    if (own && *own) {
        if (prime_own_binds(*own, key)) return true;
        bool key_in_body = prime_binder_occurs_free_in(arena, key, *body,
                                                       pattern);
        for (CettaExprIndex j = PRIME_OWN_FIRST;
             key_in_body && j < (*own)->expr.len; j++) {
            Atom *name = (*own)->expr.elems[j];
            if (!prime_binder_occurs_free(arena, name, value)) continue;
            Atom *fresh = atom_var_like(arena, name, fresh_var_id());
            Atom *renamed = fresh
                ? prime_subst_in(arena, *body, name, fresh, pattern) : NULL;
            Atom *list = renamed ? prime_list_with(arena, *own, j, fresh)
                                 : NULL;
            if (!list) return false;
            *body = renamed;
            *own = list;
        }
    }
    Atom *next = prime_subst_in(arena, *body, key, value, pattern);
    if (!next) return false;
    *body = next;
    return true;
}

static bool prime_subst_telescope(Arena *arena,
                                  const CettaPrimeLambdaBinderGroupV1 *groups,
                                  size_t count, Atom **group_syntax,
                                  Atom **body, Atom **own, const Atom *key,
                                  Atom *value, bool keep_first_types) {
    return prime_subst_telescope_in(arena, groups, count, group_syntax, body,
                                    own, key, value, keep_first_types,
                                    false);
}

/* An authored lambda from its parts, with its own list when it has one. */
static Atom *prime_lambda_rebuild(Arena *arena, Atom *head, bool listed,
                                  Atom **group_syntax, size_t count,
                                  Atom *body, Atom *own) {
    Atom *binders = listed ? atom_expr(arena, group_syntax, (CettaExprLen)count)
                           : group_syntax[0];
    if (!binders || !body) return NULL;
    if (own) {
        Atom *items[4] = {head, binders, body, own};
        return atom_expr(arena, items, 4u);
    }
    return atom_expr3(arena, head, binders, body);
}

/* Substitution in a map-atom or foldl-atom template: the list (and the
 * initial value) take it in the enclosing scope; the body takes it unless a
 * parameter or an own name binds the key, after renaming any of them the
 * value uses freely. */
static Atom *prime_subst_iter(Arena *arena, Atom *term,
                              const PrimeIterTemplate *view, const Atom *key,
                              Atom *value, bool pattern) {
    CettaExprLen len = term->expr.len;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (size_t)len);
    if (!items) return NULL;
    for (CettaExprIndex i = 0u; i < len; i++) items[i] = term->expr.elems[i];
    bool changed = false;
    for (CettaExprIndex i = 1u; i < view->first_param; i++) {
        items[i] = prime_subst_in(arena, term->expr.elems[i], key, value,
                                  pattern);
        if (!items[i]) return NULL;
        changed = changed || items[i] != term->expr.elems[i];
    }
    if (!prime_iter_binds(term, view, key)) {
        Atom *body = items[view->body];
        Atom *own = view->own ? items[view->own] : NULL;
        bool key_in_body = prime_binder_occurs_free_in(arena, key, body,
                                                       pattern);
        for (CettaExprIndex p = 0u; key_in_body && p < view->param_count;
             p++) {
            CettaExprIndex at = view->first_param + p;
            if (!prime_binder_occurs_free(arena, items[at], value)) continue;
            Atom *fresh = atom_var_like(arena, items[at], fresh_var_id());
            body = fresh ? prime_subst_in(arena, body, items[at], fresh,
                                          pattern)
                         : NULL;
            if (!body) return NULL;
            items[at] = fresh;
        }
        for (CettaExprIndex j = PRIME_OWN_FIRST;
             key_in_body && own && j < own->expr.len; j++) {
            Atom *name = own->expr.elems[j];
            if (!prime_binder_occurs_free(arena, name, value)) continue;
            Atom *fresh = atom_var_like(arena, name, fresh_var_id());
            body = fresh ? prime_subst_in(arena, body, name, fresh, pattern)
                         : NULL;
            own = body ? prime_list_with(arena, own, j, fresh) : NULL;
            if (!own) return NULL;
        }
        body = prime_subst_in(arena, body, key, value, pattern);
        if (!body) return NULL;
        items[view->body] = body;
        if (view->own) items[view->own] = own;
        for (CettaExprIndex i = view->first_param; i < len; i++)
            changed = changed || items[i] != term->expr.elems[i];
    }
    return changed ? atom_expr(arena, items, len) : term;
}

/* Substitution in `(new (names) body)`: the names bind in the body, so a
 * name of the same key stops it; a name that `value` uses freely is renamed
 * in its scope first, when the key occurs there. */
static Atom *prime_subst_new(Arena *arena, Atom *term, const Atom *key,
                             Atom *value) {
    if (prime_new_binds(term, key)) return term;
    Atom *names = term->expr.elems[1];
    Atom *body = term->expr.elems[2];
    bool key_in_body = prime_binder_occurs_free_in(arena, key, body, false);
    for (CettaExprIndex i = 0u; key_in_body && i < names->expr.len; i++) {
        Atom *name = names->expr.elems[i];
        if (!prime_binder_occurs_free(arena, name, value)) continue;
        Atom *fresh = atom_var_like(arena, name, fresh_var_id());
        body = fresh ? prime_subst_in(arena, body, name, fresh, false) : NULL;
        names = body ? prime_list_with(arena, names, i, fresh) : NULL;
        if (!names) return NULL;
    }
    body = prime_subst_in(arena, body, key, value, false);
    if (!body) return NULL;
    if (names == term->expr.elems[1] && body == term->expr.elems[2])
        return term;
    return atom_expr3(arena, term->expr.elems[0], names, body);
}

static Atom *prime_subst_pairs(Arena *arena, Atom *pairs, const Atom *key,
                               Atom *value) {
    if (!pairs || pairs->kind != ATOM_EXPR)
        return prime_subst_in(arena, pairs, key, value, false);
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < pairs->expr.len; i++) {
        Atom *pair = pairs->expr.elems[i];
        Atom *next;
        if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
            Atom *pattern = prime_subst_in(arena, pair->expr.elems[0], key,
                                           value, true);
            Atom *branch = prime_subst_in(arena, pair->expr.elems[1], key,
                                          value, false);
            if (!pattern || !branch) return NULL;
            next = pattern == pair->expr.elems[0] &&
                           branch == pair->expr.elems[1]
                ? pair : atom_expr2(arena, pattern, branch);
        } else {
            next = prime_subst_in(arena, pair, key, value, false);
        }
        if (!next) return NULL;
        if (next != pair && !items) {
            items = arena_alloc(arena,
                                sizeof(Atom *) * (size_t)pairs->expr.len);
            if (!items) return NULL;
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = pairs->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    return items ? atom_expr(arena, items, pairs->expr.len) : pairs;
}

/* Lambda's own substitution: `value` for the references to the binder
 * `key`.  It stops at a binder of the same key (a lambda binder, an own name
 * or a template parameter), renames a binder that would capture a name of
 * `value`, never enters a quotation in value position, and fills the
 * references in pattern positions, quoted or not (rule 4). */
static Atom *prime_subst_in(Arena *arena, Atom *term, const Atom *key,
                            Atom *value, bool pattern) {
    if (!term || !key) return NULL;
    if (prime_binder_reference(key, term)) return value;
    if (term->kind != ATOM_EXPR) return term;
    /* Substitution does not enter a quotation in value position
     * (atom_is_quotation, the seal every Prime binder shares), except that
     * a binder fills its own slot where it is a hole of quoted code. */
    if (!pattern && atom_is_quotation(term)) {
        const Atom *hole = g_prime_subst_quoted_hole;
        return hole && key->kind == ATOM_VAR && key->var_id == hole->var_id
            ? prime_fill_quoted_hole(arena, term, key->var_id,
                                     prime_spliced_syntax(value))
            : term;
    }
    if (!pattern && prime_semantics_contextual_code(term)) return term;
    /* In a pattern position a lambda or template form is a pattern: its
     * names are filled as store-name occurrences. */
    PrimeLambdaTelescope telescope;
    if (!pattern && prime_lambda_telescope(arena, term, &telescope)) {
        Atom **groups = arena_alloc(arena, sizeof(Atom *) * telescope.count);
        if (!groups) return NULL;
        for (size_t g = 0u; g < telescope.count; g++)
            groups[g] = telescope.groups[g].syntax;
        Atom *body = term->expr.elems[2];
        Atom *own = telescope.own;
        if (!prime_subst_telescope_in(arena, telescope.groups,
                                      telescope.count, groups, &body, &own,
                                      key, value, false, pattern))
            return NULL;
        bool changed = body != term->expr.elems[2] || own != telescope.own;
        for (size_t g = 0u; g < telescope.count; g++)
            changed = changed || groups[g] != telescope.groups[g].syntax;
        return changed
            ? prime_lambda_rebuild(arena, term->expr.elems[0],
                                   telescope.listed, groups, telescope.count,
                                   body, own)
            : term;
    }
    PrimeIterTemplate iter;
    if (!pattern && prime_iter_template(term, &iter))
        return prime_subst_iter(arena, term, &iter, key, value, pattern);
    if (!pattern && prime_new_form(term))
        return prime_subst_new(arena, term, key, value);
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        Atom *child = term->expr.elems[i];
        PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                      : prime_child_role(term, i);
        Atom *next = role == PRIME_CHILD_PATTERN_PAIRS
            ? prime_subst_pairs(arena, child, key, value)
            : prime_subst_in(arena, child, key, value,
                             role == PRIME_CHILD_PATTERN);
        if (!next) return NULL;
        if (next != child && !items) {
            items = arena_alloc(arena, sizeof(Atom *) * (size_t)term->expr.len);
            if (!items) return NULL;
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = term->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    if (!items) return term;
    Atom *rebuilt = atom_expr(arena, items, term->expr.len);
    /* A head the substitution filled with a symbol forms the construct the
     * symbol names: a lambda is elaborated, a binder sealed. */
    if (rebuilt && !pattern && items[0] != term->expr.elems[0] &&
        items[0] && items[0]->kind == ATOM_SYMBOL)
        rebuilt = prime_formed_by_substitution(arena, rebuilt);
    return rebuilt;
}

static Atom *prime_subst_binder(Arena *arena, Atom *term, const Atom *key,
                                Atom *value) {
    return prime_subst_in(arena, term, key, value, false);
}

Atom *prime_semantics_subst_var(Arena *arena, Atom *term, Atom *var,
                                Atom *value) {
    if (!arena || !term || !var || var->kind != ATOM_VAR || !value)
        return NULL;
    const Atom *saved = g_prime_subst_quoted_hole;
    g_prime_subst_quoted_hole = var;
    Atom *result = prime_subst_in(arena, term, var, value, false);
    g_prime_subst_quoted_hole = saved;
    return result;
}

/* A lambda with the binder or own name `key` renamed to a fresh variable
 * throughout its scope (alpha conversion).  The lambda itself when `key`
 * binds nothing in it. */
static Atom *prime_lambda_rename_binder(Arena *arena, Atom *term,
                                        const Atom *key) {
    PrimeLambdaTelescope telescope;
    if (!prime_lambda_telescope(arena, term, &telescope)) return NULL;
    Atom **groups = arena_alloc(arena, sizeof(Atom *) * telescope.count);
    if (!groups) return NULL;
    for (size_t g = 0u; g < telescope.count; g++)
        groups[g] = telescope.groups[g].syntax;
    Atom *body = term->expr.elems[2];
    Atom *own = telescope.own;
    for (size_t g = 0u; g < telescope.count; g++) {
        CettaPrimeLambdaBinderGroupV1 group = telescope.groups[g];
        group.syntax = groups[g];
        for (size_t i = 0u; i < group.names_count; i++) {
            Atom *name_key = prime_binder_key(
                cetta_prime_lambda_binder_name_v1(&group, i));
            if (!name_key || !prime_same_key(name_key, key)) continue;
            Atom *fresh = atom_var_with_id(arena, "binder", fresh_var_id());
            if (!fresh ||
                !prime_subst_telescope_in(arena, telescope.groups + g + 1u,
                                          telescope.count - g - 1u,
                                          groups + g + 1u, &body, &own,
                                          name_key, fresh, false, false))
                return NULL;
            groups[g] = prime_group_with(
                arena, &group, group.typed ? group.names_start + i : 0u,
                fresh);
            if (!groups[g]) return NULL;
            return prime_lambda_rebuild(arena, term->expr.elems[0],
                                        telescope.listed, groups,
                                        telescope.count, body, own);
        }
    }
    for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len; j++) {
        Atom *name = own->expr.elems[j];
        if (!prime_same_key(name, key)) continue;
        Atom *fresh = atom_var_like(arena, name, fresh_var_id());
        body = fresh ? prime_subst_in(arena, body, name, fresh, false) : NULL;
        own = body ? prime_list_with(arena, own, j, fresh) : NULL;
        if (!own) return NULL;
        return prime_lambda_rebuild(arena, term->expr.elems[0],
                                    telescope.listed, groups,
                                    telescope.count, body, own);
    }
    return term;
}

/* A map-atom or foldl-atom template with its parameter or own name `key`
 * renamed to a fresh variable throughout the body. */
static Atom *prime_iter_rename_binder(Arena *arena, Atom *term,
                                      const PrimeIterTemplate *view,
                                      const Atom *key) {
    CettaExprLen len = term->expr.len;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (size_t)len);
    if (!items) return NULL;
    for (CettaExprIndex i = 0u; i < len; i++) items[i] = term->expr.elems[i];
    for (CettaExprIndex p = 0u; p < view->param_count; p++) {
        CettaExprIndex at = view->first_param + p;
        if (!prime_same_key(items[at], key)) continue;
        Atom *fresh = atom_var_like(arena, items[at], fresh_var_id());
        items[view->body] = fresh ? prime_subst_in(arena, items[view->body],
                                                   items[at], fresh, false)
                                  : NULL;
        if (!items[view->body]) return NULL;
        items[at] = fresh;
        return atom_expr(arena, items, len);
    }
    Atom *own = view->own ? items[view->own] : NULL;
    for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len; j++) {
        Atom *name = own->expr.elems[j];
        if (!prime_same_key(name, key)) continue;
        Atom *fresh = atom_var_like(arena, name, fresh_var_id());
        items[view->body] = fresh ? prime_subst_in(arena, items[view->body],
                                                   name, fresh, false)
                                  : NULL;
        items[view->own] = items[view->body]
            ? prime_list_with(arena, own, j, fresh) : NULL;
        if (!items[view->own]) return NULL;
        return atom_expr(arena, items, len);
    }
    return term;
}

/* ── The environment action (rule 5 of the binding structure) ──────────
 *
 * Capture-avoiding substitution of the store over the binding structure,
 * built from lambda's own substitution above: it stops at a lambda binder,
 * an own name or a template parameter of the same variable, never enters a
 * quotation in value position while the session seals, fills the store
 * names in pattern positions (quoted ones included), and leaves a
 * template's own names to its activation.  A binder that an inserted value
 * would capture is renamed and the substitution of its scope repeated.
 * The action is idempotent and absorbs refinement: applying a store and
 * then a refinement of it is applying the refinement. */
#define PRIME_ENV_NESTING_LIMIT 100000u

typedef struct {
    VarId id;
    Atom *image;
} PrimeEnvMemoSlot;

typedef struct {
    const Atom *key;
    bool captured;
} PrimeEnvWatch;

typedef struct {
    Arena *arena;
    Bindings *store;
    bool seal;
    VarId *hidden;
    size_t hidden_len, hidden_cap, hidden_base;
    PrimeEnvWatch *watch;
    size_t watch_len, watch_cap, watch_base;
    PrimeEnvMemoSlot *memo;
    size_t memo_len, memo_cap;
    VarId *resolving;
    size_t resolving_len, resolving_cap;
    uint32_t nesting;
    bool failed;
} PrimeEnvSubst;

static bool prime_env_reserve(void **items, size_t *cap, size_t need,
                              size_t size) {
    if (need <= *cap) return true;
    size_t next = *cap ? *cap : 16u;
    while (next < need) {
        if (next > SIZE_MAX / 2u) return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / size) return false;
    void *grown = realloc(*items, next * size);
    if (!grown) return false;
    *items = grown;
    *cap = next;
    return true;
}

static size_t prime_env_memo_slot(VarId id, size_t cap) {
    uint64_t mixed = (uint64_t)id * UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)(mixed >> 17) & (cap - 1u);
}

static Atom *prime_env_memo_get(const PrimeEnvSubst *s, VarId id) {
    if (!s->memo_cap) return NULL;
    size_t at = prime_env_memo_slot(id, s->memo_cap);
    while (s->memo[at].id != VAR_ID_NONE) {
        if (s->memo[at].id == id) return s->memo[at].image;
        at = (at + 1u) & (s->memo_cap - 1u);
    }
    return NULL;
}

static bool prime_env_memo_put(PrimeEnvSubst *s, VarId id, Atom *image) {
    if (id == VAR_ID_NONE) return true;
    if ((s->memo_len + 1u) * 2u > s->memo_cap) {
        size_t cap = s->memo_cap ? s->memo_cap * 2u : 32u;
        PrimeEnvMemoSlot *slots = calloc(cap, sizeof(*slots));
        if (!slots) return false;
        for (size_t i = 0u; i < s->memo_cap; i++) {
            if (s->memo[i].id == VAR_ID_NONE) continue;
            size_t at = prime_env_memo_slot(s->memo[i].id, cap);
            while (slots[at].id != VAR_ID_NONE) at = (at + 1u) & (cap - 1u);
            slots[at] = s->memo[i];
        }
        free(s->memo);
        s->memo = slots;
        s->memo_cap = cap;
    }
    size_t at = prime_env_memo_slot(id, s->memo_cap);
    while (s->memo[at].id != VAR_ID_NONE && s->memo[at].id != id)
        at = (at + 1u) & (s->memo_cap - 1u);
    if (s->memo[at].id == VAR_ID_NONE) s->memo_len++;
    s->memo[at] = (PrimeEnvMemoSlot){.id = id, .image = image};
    return true;
}

static bool prime_env_hidden(const PrimeEnvSubst *s, VarId id) {
    for (size_t i = s->hidden_base; i < s->hidden_len; i++)
        if (s->hidden[i] == id) return true;
    return false;
}

static bool prime_env_bind(PrimeEnvSubst *s, const Atom *key) {
    if (!key) return true;
    if (key->kind == ATOM_VAR) {
        if (!prime_env_reserve((void **)&s->hidden, &s->hidden_cap,
                               s->hidden_len + 1u, sizeof(*s->hidden)))
            return false;
        s->hidden[s->hidden_len++] = key->var_id;
    }
    if (!prime_env_reserve((void **)&s->watch, &s->watch_cap,
                           s->watch_len + 1u, sizeof(*s->watch)))
        return false;
    s->watch[s->watch_len++] = (PrimeEnvWatch){.key = key, .captured = false};
    return true;
}

/* A value inserted under the binders of the current path: note each binder
 * whose key the value uses freely. */
static void prime_env_note_insert(PrimeEnvSubst *s, Atom *image) {
    for (size_t i = s->watch_base; i < s->watch_len; i++) {
        if (!s->watch[i].captured &&
            prime_binder_occurs_free(s->arena, s->watch[i].key, image))
            s->watch[i].captured = true;
    }
}

static Atom *prime_env_subst(PrimeEnvSubst *s, Atom *term, bool pattern);

/* The image of a store variable: its value with the store applied to it,
 * outside every binder of the current path. */
static Atom *prime_env_image(PrimeEnvSubst *s, Atom *var) {
    VarId id = var->var_id;
    Atom *memo = prime_env_memo_get(s, id);
    if (memo) return memo;
    for (size_t i = 0u; i < s->resolving_len; i++)
        if (s->resolving[i] == id) return var;
    BindingValue bound = bindings_lookup_value_id(s->store, id);
    if (!bound.skeleton) {
        if (!prime_env_memo_put(s, id, var)) s->failed = true;
        return var;
    }
    Atom *raw = binding_value_materialize(s->arena, bound);
    if (!raw ||
        !prime_env_reserve((void **)&s->resolving, &s->resolving_cap,
                           s->resolving_len + 1u, sizeof(*s->resolving))) {
        s->failed = true;
        return var;
    }
    s->resolving[s->resolving_len++] = id;
    size_t hidden_base = s->hidden_base, watch_base = s->watch_base;
    s->hidden_base = s->hidden_len;
    s->watch_base = s->watch_len;
    Atom *image = prime_env_subst(s, raw, false);
    s->hidden_base = hidden_base;
    s->watch_base = watch_base;
    s->resolving_len--;
    if (!image) {
        s->failed = true;
        return var;
    }
    if (!prime_env_memo_put(s, id, image)) s->failed = true;
    return image;
}

static Atom *prime_env_subst_pairs(PrimeEnvSubst *s, Atom *pairs) {
    if (!pairs || pairs->kind != ATOM_EXPR)
        return prime_env_subst(s, pairs, false);
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < pairs->expr.len; i++) {
        Atom *pair = pairs->expr.elems[i];
        Atom *next;
        if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
            Atom *pattern = prime_env_subst(s, pair->expr.elems[0], true);
            Atom *branch = prime_env_subst(s, pair->expr.elems[1], false);
            if (!pattern || !branch) return NULL;
            next = pattern == pair->expr.elems[0] &&
                           branch == pair->expr.elems[1]
                ? pair : atom_expr2(s->arena, pattern, branch);
        } else {
            next = prime_env_subst(s, pair, false);
        }
        if (!next) return NULL;
        if (next != pair && !items) {
            items = arena_alloc(s->arena,
                                sizeof(Atom *) * (size_t)pairs->expr.len);
            if (!items) return NULL;
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = pairs->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    return items ? atom_expr(s->arena, items, pairs->expr.len) : pairs;
}

/* The first binder of the current path above `mark` that an inserted value
 * captured, or NULL. */
static const Atom *prime_env_captured(const PrimeEnvSubst *s, size_t mark) {
    for (size_t i = mark; i < s->watch_len; i++)
        if (s->watch[i].captured) return s->watch[i].key;
    return NULL;
}

static Atom *prime_env_subst_lambda(PrimeEnvSubst *s, Atom *term,
                                    bool pattern) {
    for (unsigned attempt = 0u; attempt < 64u; attempt++) {
        PrimeLambdaTelescope telescope;
        if (!prime_lambda_telescope(s->arena, term, &telescope)) {
            s->failed = true;
            return NULL;
        }
        size_t hidden_mark = s->hidden_len, watch_mark = s->watch_len;
        Atom **groups = arena_alloc(s->arena,
                                    sizeof(Atom *) * telescope.count);
        if (!groups) {
            s->failed = true;
            return NULL;
        }
        bool changed = false;
        for (size_t g = 0u; g < telescope.count && !s->failed; g++) {
            CettaPrimeLambdaBinderGroupV1 group = telescope.groups[g];
            groups[g] = group.syntax;
            for (size_t t = 0u; group.typed && t < group.types_count; t++) {
                size_t position = group.types_start + t;
                Atom *type = group.syntax->expr.elems[position];
                Atom *next = prime_env_subst(s, type, pattern);
                if (!next || next == type) continue;
                group.syntax = prime_group_with(s->arena, &group, position,
                                                next);
                if (!group.syntax) {
                    s->failed = true;
                    break;
                }
                groups[g] = group.syntax;
                changed = true;
            }
            for (size_t i = 0u; i < group.names_count && !s->failed; i++) {
                Atom *key = prime_binder_key(
                    cetta_prime_lambda_binder_name_v1(&group, i));
                if (key && !prime_env_bind(s, key)) s->failed = true;
            }
        }
        for (CettaExprIndex j = PRIME_OWN_FIRST;
             telescope.own && j < telescope.own->expr.len && !s->failed; j++)
            if (!prime_env_bind(s, telescope.own->expr.elems[j]))
                s->failed = true;
        Atom *body = s->failed ? NULL
                               : prime_env_subst(s, term->expr.elems[2],
                                                 pattern);
        const Atom *captured = prime_env_captured(s, watch_mark);
        s->hidden_len = hidden_mark;
        s->watch_len = watch_mark;
        if (s->failed || !body) {
            s->failed = true;
            return NULL;
        }
        if (!captured) {
            changed = changed || body != term->expr.elems[2];
            return changed
                ? prime_lambda_rebuild(s->arena, term->expr.elems[0],
                                       telescope.listed, groups,
                                       telescope.count, body, telescope.own)
                : term;
        }
        term = prime_lambda_rename_binder(s->arena, term, captured);
        if (!term) {
            s->failed = true;
            return NULL;
        }
    }
    s->failed = true;
    return NULL;
}

static Atom *prime_env_subst_iter(PrimeEnvSubst *s, Atom *term,
                                  bool pattern) {
    for (unsigned attempt = 0u; attempt < 64u; attempt++) {
        PrimeIterTemplate view;
        if (!prime_iter_template(term, &view)) {
            s->failed = true;
            return NULL;
        }
        CettaExprLen len = term->expr.len;
        Atom **items = arena_alloc(s->arena, sizeof(Atom *) * (size_t)len);
        if (!items) {
            s->failed = true;
            return NULL;
        }
        bool changed = false;
        for (CettaExprIndex i = 0u; i < len; i++) items[i] = term->expr.elems[i];
        for (CettaExprIndex i = 1u; i < view.first_param && !s->failed; i++) {
            items[i] = prime_env_subst(s, term->expr.elems[i], pattern);
            if (!items[i]) s->failed = true;
            else changed = changed || items[i] != term->expr.elems[i];
        }
        size_t hidden_mark = s->hidden_len, watch_mark = s->watch_len;
        for (CettaExprIndex p = 0u; p < view.param_count && !s->failed; p++)
            if (!prime_env_bind(s, term->expr.elems[view.first_param + p]))
                s->failed = true;
        Atom *own = view.own ? term->expr.elems[view.own] : NULL;
        for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len && !s->failed; j++)
            if (!prime_env_bind(s, own->expr.elems[j])) s->failed = true;
        Atom *body = s->failed ? NULL
                               : prime_env_subst(s, term->expr.elems[view.body],
                                                 pattern);
        const Atom *captured = prime_env_captured(s, watch_mark);
        s->hidden_len = hidden_mark;
        s->watch_len = watch_mark;
        if (s->failed || !body) {
            s->failed = true;
            return NULL;
        }
        if (!captured) {
            items[view.body] = body;
            changed = changed || body != term->expr.elems[view.body];
            return changed ? atom_expr(s->arena, items, len) : term;
        }
        term = prime_iter_rename_binder(s->arena, term, &view, captured);
        if (!term) {
            s->failed = true;
            return NULL;
        }
    }
    s->failed = true;
    return NULL;
}

/* `(new (names) body)` with its name `key` renamed to a fresh variable
 * throughout its scope; the form itself when `key` names none of them. */
static Atom *prime_new_rename_binder(Arena *arena, Atom *term,
                                     const Atom *key) {
    Atom *names = term->expr.elems[1];
    for (CettaExprIndex j = 0u; j < names->expr.len; j++) {
        Atom *name = names->expr.elems[j];
        if (!prime_same_key(name, key)) continue;
        Atom *fresh = atom_var_like(arena, name, fresh_var_id());
        Atom *body = fresh ? prime_subst_in(arena, term->expr.elems[2], name,
                                            fresh, false)
                           : NULL;
        Atom *list = body ? prime_list_with(arena, names, j, fresh) : NULL;
        return list ? atom_expr3(arena, term->expr.elems[0], list, body)
                    : NULL;
    }
    return term;
}

/* The environment action on `(new (names) body)`: the names bind in the
 * body, as a lambda's parameters do. */
static Atom *prime_env_subst_new(PrimeEnvSubst *s, Atom *term) {
    for (unsigned attempt = 0u; attempt < 64u; attempt++) {
        Atom *names = term->expr.elems[1];
        size_t hidden_mark = s->hidden_len, watch_mark = s->watch_len;
        for (CettaExprIndex i = 0u; i < names->expr.len && !s->failed; i++)
            if (!prime_env_bind(s, names->expr.elems[i])) s->failed = true;
        Atom *body = s->failed ? NULL
                               : prime_env_subst(s, term->expr.elems[2], false);
        const Atom *captured = prime_env_captured(s, watch_mark);
        s->hidden_len = hidden_mark;
        s->watch_len = watch_mark;
        if (s->failed || !body) {
            s->failed = true;
            return NULL;
        }
        if (!captured) {
            if (body == term->expr.elems[2]) return term;
            Atom *rebuilt = atom_expr3(s->arena, term->expr.elems[0], names,
                                       body);
            if (!rebuilt) s->failed = true;
            return rebuilt;
        }
        term = prime_new_rename_binder(s->arena, term, captured);
        if (!term) {
            s->failed = true;
            return NULL;
        }
    }
    s->failed = true;
    return NULL;
}

static Atom *prime_env_subst(PrimeEnvSubst *s, Atom *term, bool pattern) {
    if (!term) {
        s->failed = true;
        return NULL;
    }
    if (s->failed || !atom_has_vars(term)) return term;
    if (term->kind == ATOM_VAR) {
        if (prime_env_hidden(s, term->var_id)) return term;
        Atom *image = prime_env_image(s, term);
        if (image != term) prime_env_note_insert(s, image);
        return image;
    }
    if (term->kind != ATOM_EXPR) return term;
    /* The drop *@$x refers to $x where it stands. */
    if (atom_is_drop_of_quoted_variable(term)) {
        Atom *var = term->expr.elems[1]->expr.elems[1];
        if (prime_env_hidden(s, var->var_id)) return term;
        Atom *image = prime_env_image(s, var);
        if (image == var) return term;
        prime_env_note_insert(s, image);
        return image;
    }
    /* A quotation's crossing names, (meta (quote X) {$t ...}), are holes the
     * environment fills; nothing else enters the quotation. */
    if (!pattern && s->seal && atom_is_prime_meta(term) &&
        atom_is_prime_braces(term->expr.elems[2]) &&
        atom_is_quotation(term->expr.elems[1])) {
        Atom *quote = term->expr.elems[1];
        Atom *braces = term->expr.elems[2];
        Atom *payload = quote->expr.elems[1];
        for (CettaExprIndex i = 1u; i < braces->expr.len && payload; i++) {
            Atom *var = braces->expr.elems[i];
            if (!var || var->kind != ATOM_VAR ||
                prime_env_hidden(s, var->var_id))
                continue;
            Atom *image = prime_env_image(s, var);
            if (image == var) continue;
            /* A value entering code keeps the binding structure it was
             * formed with (prime_value_into_code). */
            Atom *entering = prime_value_into_code(s->arena, image);
            payload = entering
                ? prime_fill_quoted_hole(s->arena, payload, var->var_id,
                                         entering)
                : NULL;
        }
        Atom *next_braces = prime_env_subst(s, braces, false);
        if (!payload || !next_braces) {
            s->failed = true;
            return NULL;
        }
        if (payload == quote->expr.elems[1] && next_braces == braces)
            return term;
        /* The quotation keeps its own list (prime_code_with_payload). */
        Atom *next_quote = prime_code_with_payload(s->arena, quote, payload);
        Atom *wrapped = next_quote
            ? atom_expr3(s->arena, term->expr.elems[0], next_quote,
                         next_braces)
            : NULL;
        if (!wrapped) s->failed = true;
        return wrapped;
    }
    if (!pattern && s->seal && prime_code_like(term)) return term;
    if (++s->nesting > PRIME_ENV_NESTING_LIMIT) {
        s->failed = true;
        return NULL;
    }
    Atom *result;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (!pattern && prime_lambda_telescope(s->arena, term, &telescope)) {
        result = prime_env_subst_lambda(s, term, pattern);
    } else if (!pattern && prime_iter_template(term, &iter)) {
        result = prime_env_subst_iter(s, term, pattern);
    } else if (!pattern && prime_new_form(term)) {
        result = prime_env_subst_new(s, term);
    } else {
        Atom **items = NULL;
        result = term;
        for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
            Atom *child = term->expr.elems[i];
            PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                          : prime_child_role(term, i);
            Atom *next = role == PRIME_CHILD_PATTERN_PAIRS
                ? prime_env_subst_pairs(s, child)
                : prime_env_subst(s, child, role == PRIME_CHILD_PATTERN);
            if (!next || s->failed) {
                s->failed = true;
                result = NULL;
                break;
            }
            if (next != child && !items) {
                items = arena_alloc(s->arena,
                                    sizeof(Atom *) * (size_t)term->expr.len);
                if (!items) {
                    s->failed = true;
                    result = NULL;
                    break;
                }
                for (CettaExprIndex j = 0u; j < i; j++)
                    items[j] = term->expr.elems[j];
            }
            if (items) items[i] = next;
        }
        if (result && items) {
            result = atom_expr(s->arena, items, term->expr.len);
            /* A head the store filled with a symbol forms the construct
             * the symbol names: a lambda is elaborated, a binder sealed. */
            if (result && !pattern && items[0] != term->expr.elems[0] &&
                items[0] && items[0]->kind == ATOM_SYMBOL) {
                result = prime_formed_by_substitution(s->arena, result);
                if (!result) s->failed = true;
            }
        }
    }
    s->nesting--;
    return result;
}

static Atom *prime_env_apply_mode(Arena *arena, const Bindings *store,
                                  Atom *term, bool pattern) {
    if (!arena || !term || !store || !atom_has_vars(term) ||
        !bindings_has_bound_values(store))
        return term;
    PrimeEnvSubst s = {
        .arena = arena,
        .store = (Bindings *)store,
        .seal = prime_quote_seal_active(),
    };
    Atom *result = prime_env_subst(&s, term, pattern);
    free(s.hidden);
    free(s.watch);
    free(s.memo);
    free(s.resolving);
    return s.failed ? NULL : result;
}

Atom *prime_semantics_env_apply(Arena *arena, const Bindings *store,
                                Atom *term) {
    return prime_env_apply_mode(arena, store, term, false);
}

Atom *prime_semantics_env_apply_pattern(Arena *arena, const Bindings *store,
                                        Atom *term) {
    return prime_env_apply_mode(arena, store, term, true);
}

/* A typed group without its first name: `(x y z : A)` becomes `(y z : A)`,
 * and `(x y : A B)` becomes `(y : B)`. */
static bool prime_group_drop_first(Arena *arena,
                                   const CettaPrimeLambdaBinderGroupV1 *group,
                                   CettaPrimeLambdaBinderGroupV1 *out) {
    Atom *syntax = group->syntax;
    bool shared = group->types_count == 1u;
    size_t names = group->names_count - 1u;
    size_t types = shared ? 1u : group->types_count - 1u;
    size_t length = names + 1u + types;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * length);
    if (!items) return false;
    for (size_t i = 0u; i < names; i++)
        items[i] = syntax->expr.elems[group->names_start + 1u + i];
    items[names] = syntax->expr.elems[group->types_start - 1u];
    for (size_t t = 0u; t < types; t++)
        items[names + 1u + t] =
            syntax->expr.elems[group->types_start + (shared ? 0u : 1u + t)];
    *out = (CettaPrimeLambdaBinderGroupV1){
        .syntax = atom_expr(arena, items, (CettaExprLen)length),
        .typed = true,
        .names_start = 0u,
        .names_count = names,
        .types_start = names + 1u,
        .types_count = types,
    };
    return out->syntax != NULL;
}

/* ── Activation of a template by its record (rule 3) ────────────────────
 *
 * A lambda application, and one use of a map-atom or foldl-atom template per
 * element, activate the template: its own slots, the variables of its own
 * list, are copied fresh, so two activations never share them; under the
 * snapshot readout every other name of the body still unbound at the call
 * is copied too (the environment action has already put the bound ones'
 * values in their place).  The parameters are never copied: the arguments
 * take their place.  The record decides, whatever the caller's profile. */
static bool prime_scope_record_read(const Atom *own,
                                    CettaPrimeScopeProfile *out);

typedef struct {
    Arena *arena;
    VarId *hidden;
    size_t hidden_len, hidden_cap;
    Atom **found;
    size_t found_len, found_cap;
    uint32_t nesting;
    bool failed;
} PrimeFreeNames;

static void prime_free_names_hide(PrimeFreeNames *f, const Atom *key) {
    if (!key || key->kind != ATOM_VAR) return;
    if (!prime_env_reserve((void **)&f->hidden, &f->hidden_cap,
                           f->hidden_len + 1u, sizeof(*f->hidden))) {
        f->failed = true;
        return;
    }
    f->hidden[f->hidden_len++] = key->var_id;
}

static void prime_free_names_add(PrimeFreeNames *f, Atom *var) {
    for (size_t i = 0u; i < f->hidden_len; i++)
        if (f->hidden[i] == var->var_id) return;
    for (size_t i = 0u; i < f->found_len; i++)
        if (f->found[i]->var_id == var->var_id) return;
    if (!prime_env_reserve((void **)&f->found, &f->found_cap,
                           f->found_len + 1u, sizeof(*f->found))) {
        f->failed = true;
        return;
    }
    f->found[f->found_len++] = var;
}

/* The free store names of `term`: binders and own names hidden in their
 * scopes, quoted code left out, pattern positions included. */
static void prime_free_names_walk(PrimeFreeNames *f, Atom *term,
                                  bool pattern) {
    if (f->failed || !term || !atom_has_vars(term)) return;
    if (term->kind == ATOM_VAR) {
        prime_free_names_add(f, term);
        return;
    }
    if (term->kind != ATOM_EXPR) return;
    if (atom_is_drop_of_quoted_variable(term)) {
        prime_free_names_add(f, term->expr.elems[1]->expr.elems[1]);
        return;
    }
    if (!pattern && prime_code_like(term)) return;
    if (++f->nesting > PRIME_ENV_NESTING_LIMIT) {
        f->failed = true;
        return;
    }
    size_t mark = f->hidden_len;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (!pattern && prime_lambda_telescope(f->arena, term, &telescope)) {
        for (size_t g = 0u; g < telescope.count; g++) {
            const CettaPrimeLambdaBinderGroupV1 *group = &telescope.groups[g];
            for (size_t t = 0u; group->typed && t < group->types_count; t++)
                prime_free_names_walk(
                    f, group->syntax->expr.elems[group->types_start + t],
                    false);
            for (size_t i = 0u; i < group->names_count; i++)
                prime_free_names_hide(
                    f, prime_binder_key(
                           cetta_prime_lambda_binder_name_v1(group, i)));
        }
        for (CettaExprIndex j = PRIME_OWN_FIRST;
             telescope.own && j < telescope.own->expr.len; j++)
            prime_free_names_hide(f, telescope.own->expr.elems[j]);
        prime_free_names_walk(f, term->expr.elems[2], false);
    } else if (!pattern && prime_iter_template(term, &iter)) {
        for (CettaExprIndex i = 1u; i < iter.first_param; i++)
            prime_free_names_walk(f, term->expr.elems[i], false);
        for (CettaExprIndex p = 0u; p < iter.param_count; p++)
            prime_free_names_hide(f, term->expr.elems[iter.first_param + p]);
        Atom *own = iter.own ? term->expr.elems[iter.own] : NULL;
        for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len; j++)
            prime_free_names_hide(f, own->expr.elems[j]);
        prime_free_names_walk(f, term->expr.elems[iter.body], false);
    } else if (!pattern && prime_new_form(term)) {
        Atom *names = term->expr.elems[1];
        for (CettaExprIndex j = 0u; j < names->expr.len; j++)
            prime_free_names_hide(f, names->expr.elems[j]);
        prime_free_names_walk(f, term->expr.elems[2], false);
    } else {
        for (CettaExprIndex i = 0u; i < term->expr.len && !f->failed; i++) {
            PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                          : prime_child_role(term, i);
            Atom *child = term->expr.elems[i];
            if (role == PRIME_CHILD_PATTERN_PAIRS && child &&
                child->kind == ATOM_EXPR) {
                for (CettaExprIndex k = 0u; k < child->expr.len; k++) {
                    Atom *pair = child->expr.elems[k];
                    if (pair && pair->kind == ATOM_EXPR &&
                        pair->expr.len == 2u) {
                        prime_free_names_walk(f, pair->expr.elems[0], true);
                        prime_free_names_walk(f, pair->expr.elems[1], false);
                    } else {
                        prime_free_names_walk(f, pair, false);
                    }
                }
            } else {
                prime_free_names_walk(f, child, role == PRIME_CHILD_PATTERN);
            }
        }
    }
    f->hidden_len = mark;
    f->nesting--;
}

/* `names` copied to fresh slots throughout `body`, and in `own` when they
 * are listed there.  A copy is an instance, as a clause variable renamed for
 * one call is: it carries a frame identity of its own, held by the arena
 * that holds the copy, so it prints with an instance suffix and two copies
 * that print alike are one slot.  Its base is fresh too, so renaming a term
 * for a later call never merges a copy with the name it was copied from.
 * Running out of frame identities ends the run as a resource failure: an
 * activation that could not copy its names must not leave the application
 * standing as if it were data. */
static Atom *prime_rename_everywhere(Arena *arena, Atom *term,
                                     Atom *const *from, Atom *const *to,
                                     size_t count, uint32_t nesting);

static bool prime_scope_copy(Arena *arena, CettaFrameIdentityScope *identities,
                             Atom **body, Atom **own,
                             Atom *const *names, size_t count,
                             bool store_names) {
    for (size_t i = 0u; i < count; i++) {
        CettaFrameIdentity instance =
            cetta_frame_identity_scope_fresh(identities);
        Atom *fresh = atom_var_like(arena, names[i],
                                    var_epoch_id(fresh_var_id(), instance));
        /* A store name the template owns is its slot alone, so it is
         * renamed wherever it occurs, quoted code included, which shares
         * it (an opening of code does the same, prime_code_opening).  A
         * binder's quoted mention stays literal: lambda's own substitution,
         * which never enters a quotation. */
        Atom *next = !fresh ? NULL
            : store_names
                ? prime_rename_everywhere(arena, *body, &names[i], &fresh, 1u,
                                          0u)
                : prime_subst_in(arena, *body, names[i], fresh, false);
        if (!next) return false;
        *body = next;
        for (CettaExprIndex j = PRIME_OWN_FIRST; *own && j < (*own)->expr.len;
             j++) {
            if ((*own)->expr.elems[j]->var_id != names[i]->var_id) continue;
            Atom *list = prime_list_with(arena, *own, j, fresh);
            if (!list) return false;
            *own = list;
        }
    }
    return true;
}

/* Activate a template whose body is `*body` and own list `*own`, with
 * parameters `params`: the copies replace the own slots in both. */
static bool prime_scope_activate(Arena *arena, Atom **body, Atom **own,
                                 Atom *const *params, size_t param_count) {
    if (!*own || !atom_is_prime_own_list(*own)) return true;
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    size_t count = (size_t)(*own)->expr.len - PRIME_OWN_FIRST;
    Atom **names = count ? arena_alloc(arena, sizeof(Atom *) * count) : NULL;
    if (count && !names) return false;
    for (size_t i = 0u; i < count; i++)
        names[i] = (*own)->expr.elems[PRIME_OWN_FIRST + i];
    if (!prime_scope_copy(arena, &identities, body, own, names, count, true))
        return false;
    CettaPrimeScopeProfile record;
    if (!prime_scope_record_read(*own, &record) ||
        record.readout != CETTA_PRIME_READOUT_SNAPSHOT)
        return true;
    PrimeFreeNames free_names = {.arena = arena};
    for (size_t p = 0u; p < param_count; p++)
        prime_free_names_hide(&free_names, params[p]);
    for (CettaExprIndex j = PRIME_OWN_FIRST; j < (*own)->expr.len; j++)
        prime_free_names_hide(&free_names, (*own)->expr.elems[j]);
    prime_free_names_walk(&free_names, *body, false);
    bool ok = !free_names.failed &&
              prime_scope_copy(arena, &identities, body, own,
                               free_names.found, free_names.found_len, true);
    free(free_names.found);
    free(free_names.hidden);
    return ok;
}

/* One evaluation of `(new (names) body)`: each name is copied to a fresh
 * slot throughout the body, an instance of its own as an activation's own
 * slot is, and the body is what runs.  NULL when the form is not a `new`
 * or a copy could not be made. */
Atom *prime_semantics_new_open(Arena *arena, Atom *term) {
    if (!arena || !prime_new_form(term)) return NULL;
    Atom *names = term->expr.elems[1];
    Atom *body = term->expr.elems[2];
    if (names->expr.len == 0u) return body;
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    Atom *own = NULL;
    if (!prime_scope_copy(arena, &identities, &body, &own, names->expr.elems,
                          (size_t)names->expr.len, false))
        return NULL;
    return body;
}

/* The variable binders of a lambda telescope. */
static size_t prime_lambda_params(Arena *arena,
                                  const PrimeLambdaTelescope *telescope,
                                  Atom ***out) {
    size_t total = 0u;
    for (size_t g = 0u; g < telescope->count; g++)
        total += telescope->groups[g].names_count;
    Atom **params = total ? arena_alloc(arena, sizeof(Atom *) * total) : NULL;
    size_t count = 0u;
    for (size_t g = 0u; params && g < telescope->count; g++)
        for (size_t i = 0u; i < telescope->groups[g].names_count; i++) {
            Atom *key = prime_binder_key(
                cetta_prime_lambda_binder_name_v1(&telescope->groups[g], i));
            if (key) params[count++] = key;
        }
    *out = params;
    return count;
}

/* ── map-atom and foldl-atom by their template records (Prime) ──────────
 *
 * One step of `(map-atom L $e T OWN)` or `(foldl-atom L init $a $b O OWN)`,
 * a literal template, in the order of the library definitions they replace
 * in Prime: map-atom maps the tail first, then the head, and conses;
 * foldl-atom folds from the left.  Each step is a `chain` evaluated where
 * the call stands, so a refinement of a captured name reaches the outside
 * slot.  Each element activates the template by its record.
 * A list that is not a plain expression is left to the library: the call is
 * re-entered without its own list.  HE keeps its own map-atom, which seals
 * the template. */
static Atom *prime_iter_with(Arena *arena, Atom *call, CettaExprIndex index,
                             Atom *element) {
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (size_t)call->expr.len);
    if (!items) return NULL;
    for (CettaExprIndex i = 0u; i < call->expr.len; i++)
        items[i] = call->expr.elems[i];
    items[index] = element;
    return atom_expr(arena, items, call->expr.len);
}

PrimeIterationStep prime_semantics_iteration_step(Arena *arena, Atom *call,
                                                  Atom *space, Atom **out) {
    *out = NULL;
    PrimeIterTemplate view;
    if (!arena || !call || !prime_iter_template(call, &view) || !view.own)
        return PRIME_ITERATION_DECLINE;
    bool fold = view.param_count == 2u;
    Atom *list = call->expr.elems[1];
    if (!list || list->kind != ATOM_EXPR || atom_is_list(list) ||
        atom_is_list_rest(list) || (fold && !space)) {
        /* The library reads it, without the record. */
        *out = atom_expr(arena, call->expr.elems, call->expr.len - 1u);
        return *out ? PRIME_ITERATION_REENTER : PRIME_ITERATION_DECLINE;
    }
    if (list->expr.len == 0u) {
        *out = fold ? call->expr.elems[2] : list;
        return fold ? PRIME_ITERATION_REENTER : PRIME_ITERATION_VALUE;
    }
    Atom *body = call->expr.elems[view.body];
    Atom *own = call->expr.elems[view.own];
    Atom *params[2] = {call->expr.elems[view.first_param],
                       fold ? call->expr.elems[view.first_param + 1u] : NULL};
    if (!prime_scope_activate(arena, &body, &own, params, view.param_count))
        return PRIME_ITERATION_DECLINE;
    Atom *head = list->expr.elems[0];
    Atom *values[2] = {fold ? call->expr.elems[2] : head,
                       fold ? head : NULL};
    for (CettaExprIndex p = 0u; p < view.param_count; p++) {
        body = prime_subst_in(arena, body, params[p], values[p], false);
        if (!body) return PRIME_ITERATION_DECLINE;
    }
    Atom *rest = atom_expr(arena, list->expr.elems + 1u, list->expr.len - 1u);
    Atom *recur = rest ? prime_iter_with(arena, call, 1u, rest) : NULL;
    if (!recur) return PRIME_ITERATION_DECLINE;
    Atom *chain = atom_symbol_id(arena, g_builtin_syms.chain);
    if (fold) {
        /* Each step is evaluated where the fold stands, as a map element
         * is: a step that refines a captured name refines the outside slot
         * (rule 3), which a separate `metta` evaluation would not keep. */
        (void)space;
        Atom *acc = atom_var_with_id(arena, "fold-step", fresh_var_id());
        Atom *next_call = acc ? prime_iter_with(arena, recur, 2u, acc) : NULL;
        Atom *items[4] = {chain, body, acc, next_call};
        *out = next_call ? atom_expr(arena, items, 4u) : NULL;
    } else {
        Atom *tail = atom_var_with_id(arena, "map-tail", fresh_var_id());
        Atom *mapped = atom_var_with_id(arena, "map-head", fresh_var_id());
        Atom *cons = tail && mapped
            ? atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.cons_atom),
                         mapped, tail)
            : NULL;
        Atom *inner_items[4] = {chain, body, mapped, cons};
        Atom *inner = cons ? atom_expr(arena, inner_items, 4u) : NULL;
        Atom *outer_items[4] = {chain, recur, tail, inner};
        *out = inner ? atom_expr(arena, outer_items, 4u) : NULL;
    }
    return *out ? PRIME_ITERATION_REENTER : PRIME_ITERATION_DECLINE;
}

/* One contraction at the root of the atom being evaluated: one-argument beta
 * of an authored lambda (nested binders of the same variable stay bound), a
 * projection of a pair, or identity elimination at reflexivity. Subterms are
 * never entered. An argument is contracted when the evaluator evaluates it,
 * so a term that reaches the root only as data, such as the tail of a list
 * or a quoted term, is not rewritten. */
Atom *prime_semantics_authored_head_step(Arena *arena, Atom *term) {
    if (!arena || !term || term->kind != ATOM_EXPR) return term;
    Atom *next = prime_semantics_beta(arena, term);
    if (!next) next = prime_semantics_project_pair(term);
    if (!next) next = prime_semantics_identity_iota(term);
    return next ? next : term;
}

bool prime_semantics_is_authored_lambda(Arena *arena, Atom *term) {
    PrimeLambdaTelescope telescope;
    return prime_lambda_telescope(arena, term, &telescope);
}

/* Beta for an authored lambda applied to one or more arguments: the first
 * binder takes the first argument, and the written domains of the binders
 * that remain are kept (annotated reduction, `ATm.Step.betaTyped`, whose
 * erasure is ordinary beta: `Step.erasure`, WrittenDomains.lean).  The
 * remaining arguments apply to the result. */
Atom *prime_semantics_beta(Arena *arena, Atom *call) {
    if (!arena || !call || call->kind != ATOM_EXPR || call->expr.len < 2u)
        return NULL;
    Atom *function = call->expr.elems[0];
    /* A lambda with its crossing set, (meta (lam ...) {...}), applies as the
     * lambda: its elaboration applied the set. */
    if (atom_is_prime_meta(function) &&
        prime_semantics_meta_core(arena, function))
        function = function->expr.elems[1];
    PrimeLambdaTelescope telescope;
    if (!prime_lambda_telescope(arena, function, &telescope) ||
        telescope.count == 0u)
        return NULL;
    Atom *argument = call->expr.elems[1];
    const CettaPrimeLambdaBinderGroupV1 *first = &telescope.groups[0];
    Atom *binder = cetta_prime_lambda_binder_name_v1(first, 0u);
    /* The siblings of the first name keep their group; their written types
     * are read outside the group and never see the first name. */
    CettaPrimeLambdaBinderGroupV1 siblings;
    bool has_siblings = first->names_count > 1u;
    if (has_siblings && !prime_group_drop_first(arena, first, &siblings))
        return NULL;
    size_t rest_count = telescope.count - 1u + (has_siblings ? 1u : 0u);
    CettaPrimeLambdaBinderGroupV1 *rest = rest_count
        ? arena_alloc(arena, sizeof(*rest) * rest_count) : NULL;
    Atom **rest_syntax = rest_count
        ? arena_alloc(arena, sizeof(Atom *) * rest_count) : NULL;
    if (rest_count && (!rest || !rest_syntax)) return NULL;
    size_t next = 0u;
    if (has_siblings) rest[next++] = siblings;
    for (size_t g = 1u; g < telescope.count; g++)
        rest[next++] = telescope.groups[g];
    for (size_t g = 0u; g < rest_count; g++)
        rest_syntax[g] = rest[g].syntax;
    Atom *body = function->expr.elems[2];
    Atom *own = telescope.own;
    /* Activation (rule 3): this application's own slots are fresh copies;
     * under the snapshot readout so are the body's names still unbound.  The
     * record in the own list decides, whatever the caller's profile.  A
     * lambda that remains after a partial application keeps the copies as
     * its own slots, so each later application copies them again. */
    if (own) {
        Atom **params = NULL;
        size_t param_count = prime_lambda_params(arena, &telescope, &params);
        if (!prime_scope_activate(arena, &body, &own, params, param_count))
            return NULL;
    }
    Atom *key = prime_binder_key(binder);
    if (key &&
        !prime_subst_telescope(arena, rest, rest_count, rest_syntax, &body,
                               &own, key, argument, has_siblings))
        return NULL;
    Atom *reduct = rest_count == 0u
        ? body
        : prime_lambda_rebuild(arena, function->expr.elems[0],
                               telescope.listed, rest_syntax, rest_count,
                               body, own);
    if (!reduct || call->expr.len == 2u) return reduct;
    Atom **items = arena_alloc(
        arena, sizeof(Atom *) * ((size_t)call->expr.len - 1u));
    if (!items) return NULL;
    items[0] = reduct;
    for (CettaExprIndex i = 2u; i < call->expr.len; i++)
        items[i - 1u] = call->expr.elems[i];
    return atom_expr(arena, items, call->expr.len - 1u);
}

/* The seal of quoted code (atom_is_quotation) while the session seals:
 * Prime unless its profile is quote-as-written.  It follows the session's
 * language and profile, as the active language does. */
static __thread bool g_prime_quote_seal = false;

void prime_quote_seal_set(bool active) {
    g_prime_quote_seal = active;
}

bool prime_quote_seal_active(void) {
    return g_prime_quote_seal;
}

/* ── Elaboration of authored forms: the binding structure ──────────────
 *
 * A form is elaborated once, when it is read, under the scope profile of its
 * document (below), and the result is stored in the term:
 *
 *   1. Lexical binders.  Each `$` parameter of a lambda and of a map-atom or
 *      foldl-atom template becomes a slot of the binder's own, a fresh
 *      variable, one per spelling in the form: no outer substitution or
 *      unification of a same-spelled store name can reach it.  A binder binds
 *      only in its own lambda, so two lambdas written alike stay alike, and
 *      their binders never connect.  A pattern binder written with a
 *      crossing set makes every other name of its patterns a fresh variable
 *      in the part of the form each pattern scopes over.
 *   2. Ownership (rule 3), by the profile's ownership option and the
 *      crossing sets written on templates: the names a template owns become
 *      its own slots, held last in it with the profile's record,
 *      `(OWN RECORD $y ...)`, hidden metadata no observer of the authored
 *      term sees; each activation copies them (prime_scope_activate).  A
 *      written quotation in value position is a scope as a template is,
 *      and so is a template written in code: its own names are held last
 *      in it, `(quote X (OWN RECORD $u ...))`, and each opening of the code
 *      copies them (prime_code_opening); a hole in the code is an
 *      occurrence where its pattern stands.  The crossing sets stay in the
 *      term as written: they are authored structure.  A template or
 *      quotation that already carries an own list keeps it: code is never
 *      elaborated twice, and a term formed while the program runs never
 *      decides ownership (prime_elab_formed).
 *   3. The seal (rule 4).  A role belongs to an occurrence.  A name in a
 *      pattern position, quoted or not, is a store-name occurrence.  A
 *      mention, inside a quotation in value position, of a variable that a
 *      pattern binds at a lower quote depth becomes a variable of the
 *      quotation's own, one per depth, so no binder's substitution reaches
 *      it; a hole, a variable a pattern binds inside a quotation, keeps its
 *      identity in the part of the form that pattern scopes over.  Every
 *      other quoted variable keeps its identity: the environment action
 *      never enters the quotation (prime_semantics_env_apply), and code that
 *      shares a variable with the outside, such as a rule program's template
 *      or the exclusion list of `sealed`, still shares it.  The drop *@$x
 *      refers to $x where it stands.
 *
 * Failure (memory, or nesting beyond the limit) fails the elaboration; the
 * reader then refuses the form instead of accepting it unsealed. */
#define PRIME_ELAB_NESTING_LIMIT 100000u

/* ── Scope profiles (the scope-policy spectrum) ────────────────────────
 *
 * A scope profile is an elaboration property of a document.  It decides,
 * when a form is read, which names each lambda or map-atom/foldl-atom
 * template owns, and the template records it in its own list.  Applying a
 * template reads its own list, whatever the caller's profile.
 *
 *   ownership  query-wide         templates own nothing (the control).
 *              mercury-implicit   a template owns the names written directly
 *                                 in its body (outside nested templates)
 *                                 that no enclosing scope writes directly or
 *                                 owns: disjoint templates each own a name,
 *                                 and a name written outside is captured.
 *              lexical-fresh      templates own nothing by themselves; a
 *                                 pattern of let, let*, case, switch,
 *                                 switch-minimal, match, chain or
 *                                 filter-atom makes a fresh slot for each
 *                                 store name it holds, in force in the part
 *                                 it scopes over (inner wins); unify and an
 *                                 equation head make none, they refer.  The
 *                                 slots made directly in a template's body
 *                                 are made at each of its calls.
 *              explicit-capture   a template owns every name its region
 *                                 uses: written directly in it, or shared
 *                                 with it by the crossing set of a template
 *                                 directly in it.
 *              lexical-inventory  variant (i): implicit introduction over the
 *                                 template's region, apart by default,
 *                                 connected by the crossing set.  Its lists
 *                                 are explicit capture's (TemplateScope's
 *                                 profLIi = profEC).
 *   lifetime   per-call           each activation copies the own slots.
 *              per-closure        a template's own slots move to the scope
 *                                 that creates it: an enclosing template
 *                                 copies them at its activation, once per
 *                                 closure, and at a query or clause root
 *                                 they are its variables.  The fresh slots
 *                                 of the pattern binders in a template stay
 *                                 with it: they are made at each of its
 *                                 calls.
 *   readout    reference          a captured name is its slot.
 *              snapshot           an activation also copies every name of
 *                                 the body still unbound at the call.
 *
 * Every option is a default, which a crossing set written on a construct
 * replaces, one convention for every scope-forming construct: braces
 * touching it, `(lam z body){$t}`, `(map-atom L $e T){$t}`,
 * `(let P V B){$t}`, `(quote X){$t}`, name what it shares with the scope
 * around, and it holds every other name as its own: a template owns every
 * other name its region uses, a pattern binder makes every other name of
 * its patterns fresh, a quotation seals every other name it holds
 * (prime_scope_compute, prime_lex_template, prime_elab_binders_crossing,
 * prime_elab_seal_apply).  `{}` shares nothing.
 *
 * A document chooses its profile with a declaration
 * `(scope:profile OWNERSHIP [LIFETIME] [READOUT])`.  As a bare top-level atom
 * it applies to the whole document; as a query, `!(scope:profile ...)`, it
 * applies from its position onward, as HE's `!(pragma! ...)` does.  A
 * document without one is elaborated under mercury-implicit per-call
 * reference (provisional). */
static const char *const prime_scope_ownership_names[] = {
    "query-wide", "mercury-implicit", "lexical-fresh", "explicit-capture",
    "lexical-inventory",
};
#define PRIME_SCOPE_OWNERSHIP_COUNT 5
static const char *const prime_scope_lifetime_names[] = {
    "per-call", "per-closure",
};
static const char *const prime_scope_readout_names[] = {
    "reference", "snapshot",
};

static const CettaPrimeScopeProfile prime_scope_builtin_default = {
    .ownership = CETTA_PRIME_OWNERSHIP_MERCURY_IMPLICIT,
    .lifetime = CETTA_PRIME_LIFETIME_PER_CALL,
    .readout = CETTA_PRIME_READOUT_REFERENCE,
};

static int prime_scope_name_index(const char *name, size_t len,
                                  const char *const *names, int count) {
    for (int i = 0; i < count; i++)
        if (strlen(names[i]) == len && memcmp(name, names[i], len) == 0)
            return i;
    return -1;
}

/* `ownership[/lifetime[/readout]]`, the form of a record and of the
 * experiment's default. */
static bool prime_scope_profile_parse(const char *text,
                                      CettaPrimeScopeProfile *out) {
    if (!text || !out) return false;
    CettaPrimeScopeProfile profile = prime_scope_builtin_default;
    const char *names[3] = {text, NULL, NULL};
    size_t lens[3] = {0u, 0u, 0u};
    size_t parts = 1u;
    for (const char *p = text;; p++) {
        if (*p == '/' || *p == '\0') {
            lens[parts - 1u] = (size_t)(p - names[parts - 1u]);
            if (*p == '\0') break;
            if (parts == 3u) return false;
            names[parts++] = p + 1;
        }
    }
    int ownership = prime_scope_name_index(names[0], lens[0],
                                           prime_scope_ownership_names,
                                           PRIME_SCOPE_OWNERSHIP_COUNT);
    if (ownership < 0) return false;
    profile.ownership = (uint8_t)ownership;
    if (parts > 1u) {
        int lifetime = prime_scope_name_index(names[1], lens[1],
                                              prime_scope_lifetime_names, 2);
        if (lifetime < 0) return false;
        profile.lifetime = (uint8_t)lifetime;
    }
    if (parts > 2u) {
        int readout = prime_scope_name_index(names[2], lens[2],
                                             prime_scope_readout_names, 2);
        if (readout < 0) return false;
        profile.readout = (uint8_t)readout;
    }
    *out = profile;
    return true;
}

/* The profile of a document that declares none.  The scope experiment (the
 * census and its output comparison) reads the documents of the corpus under
 * another default without copying them (main.c reads
 * CETTA_PRIME_SCOPE_DEFAULT=ownership[/lifetime/readout]).  It is an
 * elaboration default only: a declaration in a document wins, and every
 * template is applied by its own record. */
static __thread bool g_prime_scope_default_set = false;
static __thread CettaPrimeScopeProfile g_prime_scope_default;

bool prime_scope_profile_default_set(const char *text) {
    CettaPrimeScopeProfile profile;
    if (!text || !prime_scope_profile_parse(text, &profile)) return false;
    g_prime_scope_default = profile;
    g_prime_scope_default_set = true;
    return true;
}

CettaPrimeScopeProfile prime_scope_profile_default(void) {
    return g_prime_scope_default_set ? g_prime_scope_default
                                     : prime_scope_builtin_default;
}

/* The record symbol `ownership/lifetime/readout`. */
static Atom *prime_scope_record(Arena *arena, CettaPrimeScopeProfile profile) {
    char text[96];
    snprintf(text, sizeof text, "%s/%s/%s",
             prime_scope_ownership_names[profile.ownership],
             prime_scope_lifetime_names[profile.lifetime],
             prime_scope_readout_names[profile.readout]);
    return atom_symbol(arena, text);
}

static bool prime_scope_record_read(const Atom *own,
                                    CettaPrimeScopeProfile *out) {
    if (!atom_is_prime_own_list(own)) return false;
    const char *text = symbol_bytes(g_symbols, own->expr.elems[1]->sym_id);
    return text && prime_scope_profile_parse(text, out);
}

/* The axes of a declaration `(scope:profile ownership [lifetime]
 * [readout])`, in any order: false when a name is unknown, repeated, or the
 * ownership is missing. */
static bool prime_scope_axes(const char *const *names, size_t count,
                             CettaPrimeScopeProfile *out) {
    CettaPrimeScopeProfile profile = prime_scope_builtin_default;
    int seen[3] = {0, 0, 0};
    for (size_t i = 0u; i < count; i++) {
        const char *name = names[i];
        if (!name) return false;
        size_t len = strlen(name);
        int value;
        if (!seen[0] &&
            (value = prime_scope_name_index(name, len,
                                            prime_scope_ownership_names,
                                            PRIME_SCOPE_OWNERSHIP_COUNT)) >= 0) {
            profile.ownership = (uint8_t)value;
            seen[0] = 1;
        } else if (!seen[1] &&
                   (value = prime_scope_name_index(
                        name, len, prime_scope_lifetime_names, 2)) >= 0) {
            profile.lifetime = (uint8_t)value;
            seen[1] = 1;
        } else if (!seen[2] &&
                   (value = prime_scope_name_index(
                        name, len, prime_scope_readout_names, 2)) >= 0) {
            profile.readout = (uint8_t)value;
            seen[2] = 1;
        } else {
            return false;
        }
    }
    if (!seen[0]) return false;
    *out = profile;
    return true;
}

/* A declaration `(scope:profile ownership [lifetime] [readout])`.  False for
 * any other form.  A declaration with a name it does not know selects
 * nothing, as an unknown theory profile does; it stays in the document as
 * data. */
static bool prime_scope_declaration(TermUniverse *universe, AtomId id,
                                    CettaPrimeScopeProfile *out,
                                    bool *is_declaration) {
    *is_declaration = false;
    if (tu_kind(universe, id) != ATOM_EXPR) return false;
    CettaExprLen arity = tu_arity(universe, id);
    if (arity < 2u || arity > 4u) return false;
    AtomId head = tu_child(universe, id, 0u);
    if (tu_kind(universe, head) != ATOM_SYMBOL) return false;
    const char *head_name = symbol_bytes(g_symbols, tu_sym(universe, head));
    if (!head_name || strcmp(head_name, "scope:profile") != 0) return false;
    *is_declaration = true;
    const char *names[3] = {NULL, NULL, NULL};
    for (CettaExprIndex i = 1u; i < arity; i++) {
        AtomId child = tu_child(universe, id, i);
        if (tu_kind(universe, child) != ATOM_SYMBOL) return false;
        names[i - 1u] = symbol_bytes(g_symbols, tu_sym(universe, child));
    }
    return prime_scope_axes(names, (size_t)arity - 1u, out);
}

bool prime_scope_profile_from_atom(const Atom *decl,
                                   CettaPrimeScopeProfile *out) {
    if (!decl || decl->kind != ATOM_EXPR || decl->expr.len < 2u ||
        decl->expr.len > 4u || !is_symbol_named(decl->expr.elems[0],
                                                "scope:profile"))
        return false;
    const char *names[3] = {NULL, NULL, NULL};
    for (CettaExprIndex i = 1u; i < decl->expr.len; i++) {
        if (!decl->expr.elems[i] || decl->expr.elems[i]->kind != ATOM_SYMBOL)
            return false;
        names[i - 1u] = symbol_bytes(g_symbols, decl->expr.elems[i]->sym_id);
    }
    return prime_scope_axes(names, (size_t)decl->expr.len - 1u, out);
}

/* The profile in force while a document runs: the main document's own
 * (its whole-document declaration, or the default), then each query
 * declaration evaluated, from its position onward.  Code formed while the
 * program runs (parse) is elaborated under it. */
static __thread bool g_prime_scope_runtime_set = false;
static __thread CettaPrimeScopeProfile g_prime_scope_runtime;

void prime_scope_runtime_profile_set(CettaPrimeScopeProfile profile) {
    g_prime_scope_runtime = profile;
    g_prime_scope_runtime_set = true;
}

CettaPrimeScopeProfile prime_scope_runtime_profile(void) {
    return g_prime_scope_runtime_set ? g_prime_scope_runtime
                                     : prime_scope_profile_default();
}

/* ── Elaboration state ─────────────────────────────────────────────── */

typedef struct {
    VarId from;
    uint32_t level;
    Atom *to;
} PrimeElabTwin;

/* A hashed map from (level, variable) to an atom or a number: binder twins
 * (level 0), the slots a scope owns (level: the scope), the seal's twins
 * (level: the quote depth), least pattern depths. */
typedef struct {
    VarId id;
    uint32_t level;
    bool used;
    uint32_t number;
    Atom *atom;
} PrimeVarEntry;

typedef struct {
    PrimeVarEntry *items;
    size_t len, cap;
} PrimeVarMap;

static size_t prime_var_map_slot(VarId id, uint32_t level, size_t cap) {
    uint64_t mixed = (uint64_t)id * UINT64_C(0x9E3779B97F4A7C15) ^
                     ((uint64_t)level + 1u) * UINT64_C(0xC2B2AE3D27D4EB4F);
    mixed ^= mixed >> 31;
    return (size_t)mixed & (cap - 1u);
}

static PrimeVarEntry *prime_var_map_find(const PrimeVarMap *m, VarId id,
                                         uint32_t level) {
    if (!m->cap) return NULL;
    size_t at = prime_var_map_slot(id, level, m->cap);
    while (m->items[at].used) {
        if (m->items[at].id == id && m->items[at].level == level)
            return &m->items[at];
        at = (at + 1u) & (m->cap - 1u);
    }
    return NULL;
}

/* The entry for (level, id), made empty when absent; NULL on failure. */
static PrimeVarEntry *prime_var_map_get(PrimeVarMap *m, VarId id,
                                        uint32_t level, bool *made) {
    PrimeVarEntry *found = prime_var_map_find(m, id, level);
    if (made) *made = false;
    if (found) return found;
    if ((m->len + 1u) * 2u > m->cap) {
        size_t cap = m->cap ? m->cap * 2u : 64u;
        PrimeVarEntry *items = calloc(cap, sizeof(*items));
        if (!items) return NULL;
        for (size_t i = 0u; i < m->cap; i++) {
            if (!m->items[i].used) continue;
            size_t at = prime_var_map_slot(m->items[i].id, m->items[i].level,
                                           cap);
            while (items[at].used) at = (at + 1u) & (cap - 1u);
            items[at] = m->items[i];
        }
        free(m->items);
        m->items = items;
        m->cap = cap;
    }
    size_t at = prime_var_map_slot(id, level, m->cap);
    while (m->items[at].used) at = (at + 1u) & (m->cap - 1u);
    m->items[at] = (PrimeVarEntry){.id = id, .level = level, .used = true};
    m->len++;
    if (made) *made = true;
    return &m->items[at];
}

/* One template scope of a form (the root, a query or a clause, is scope
 * 0): the names written directly in it, the crossing set written on it, and
 * the names it owns.  Scopes are numbered in the order the walk meets them,
 * outer before inner. */
typedef struct {
    int32_t parent;
    Atom *shared;      /* the crossing set written on it, or NULL */
    VarId *direct;     /* the names written directly in it */
    size_t direct_len, direct_cap;
    VarId *passed;     /* the crossing sets of the templates directly in it */
    size_t passed_len, passed_cap;
    VarId *fresh;      /* the names its patterns make fresh (a crossing set
                        * on a pattern binder directly in it) */
    size_t fresh_len, fresh_cap;
    VarId *own;
    size_t own_len, own_cap;
} PrimeScopeNode;

/* An entry of the crossing names in force (lexical-fresh): a name a
 * crossing set shares with the scope around, or, shadowing it, a name a
 * construct inside owns or makes fresh. */
typedef struct {
    VarId id;
    bool in_force;
} PrimeLexForce;

typedef struct {
    Arena *arena;
    CettaPrimeScopeProfile profile;
    Atom *record;
    bool seal;
    /* 1: binder twins, one per spelled variable of the form, and the set of
     * the variables that are binders (twins, and the own names of templates
     * elaborated before) */
    PrimeVarMap binders;
    PrimeVarMap binder_ids;
    /* 2: the scopes, the template scopes the walk is in, the slots each
     * scope owns, and the own slots moved up under per-closure */
    PrimeScopeNode *scopes;
    size_t scopes_len, scopes_cap;
    uint32_t *stack;
    size_t stack_len, stack_cap;
    uint32_t next_scope;
    PrimeVarMap slots;
    Atom **hoist;
    size_t hoist_len, hoist_cap;
    /* 1: the names a crossing set on a pattern binder made fresh, and a
     * spelling of every name the collect walk meets */
    PrimeVarMap fresh;
    PrimeVarMap spellings;
    /* 2, lexical-fresh: the slots in force (innermost last), the slots made
     * in the template body being walked, and the crossing names in force
     * (no pattern inside makes a slot for them) */
    PrimeElabTwin *lex;
    size_t lex_len, lex_cap;
    Atom **frame;
    size_t frame_len, frame_cap;
    PrimeLexForce *lex_shared;
    size_t lex_shared_len, lex_shared_cap;
    /* lexical-fresh: the crossing set a wrapper puts on the pattern binder
     * resolved next */
    Atom *pending_crossing;
    /* 3: the crossing set of the quotation being sealed, when it is written
     * (every other name of the quotation is its own), and the depth of that
     * quotation's code */
    Atom *seal_crossing;
    uint32_t seal_crossing_depth;
    /* 3: least pattern depth per variable, and the seal twins per (variable,
     * depth) */
    PrimeVarMap depths;
    PrimeVarMap sealed;
    /* holes in scope: variables a pattern binds inside a quotation written
     * in pattern position, while a pass walks that binder's scope; and, for
     * step 2, the scope each was pushed in, where its pattern stands */
    VarId *holes;
    size_t holes_len, holes_cap;
    uint32_t *holes_scope;
    size_t holes_scope_cap;
    /* step 2: how many written quotations, scopes of their own, the walk is
     * inside, and the crossing set a wrapper puts on the quotation walked
     * next, (meta (quote X) {$t ...}) */
    uint32_t code_scopes;
    Atom *pending_quote_crossing;
    uint32_t nesting;
    /* how many quotations in value position enclose the current term */
    uint32_t code_depth;
    bool failed;
} PrimeElab;

static bool prime_elab_reserve(PrimeElab *e, void **items, size_t *cap,
                               size_t need, size_t size) {
    if (!prime_env_reserve(items, cap, need, size)) {
        e->failed = true;
        return false;
    }
    return true;
}

static bool prime_elab_enter(PrimeElab *e) {
    if (e->failed) return false;
    if (++e->nesting > PRIME_ELAB_NESTING_LIMIT) {
        e->failed = true;
        return false;
    }
    return true;
}

static void prime_elab_free(PrimeElab *e) {
    free(e->binders.items);
    free(e->binder_ids.items);
    for (size_t i = 0u; i < e->scopes_len; i++) {
        free(e->scopes[i].direct);
        free(e->scopes[i].passed);
        free(e->scopes[i].fresh);
        free(e->scopes[i].own);
    }
    free(e->scopes);
    free(e->stack);
    free(e->slots.items);
    free(e->hoist);
    free(e->fresh.items);
    free(e->spellings.items);
    free(e->lex);
    free(e->frame);
    free(e->lex_shared);
    free(e->depths.items);
    free(e->sealed.items);
    free(e->holes);
    free(e->holes_scope);
}

static bool prime_elab_is_binder_id(const PrimeElab *e, VarId id) {
    return prime_var_map_find(&e->binder_ids, id, 0u) != NULL;
}

static void prime_elab_note_binder(PrimeElab *e, VarId id) {
    if (!prime_var_map_get(&e->binder_ids, id, 0u, NULL)) e->failed = true;
}

/* The crossing set of a scope-forming construct: a braces node touching it,
 * C{$t ...}, which the reader makes the wrapper (meta C {$t ...}).  It
 * names what the construct shares with the scope around it, and decides
 * everything else the construct holds, under every profile (the profile's
 * default applies only where no set is written):
 *   - a lambda or a map-atom/foldl-atom template owns every other name its
 *     region uses: the names written directly in it and those the templates
 *     directly in it share with it;
 *   - a pattern binder (let, let*, case, switch, switch-minimal, match,
 *     chain, filter-atom) makes every other name of its patterns fresh, for
 *     the part of the form each pattern scopes over;
 *   - a quotation lets the set's names cross into it, as holes the
 *     environment fills; every other name in it is the quotation's own.
 * Under lexical-fresh the set's names are also in force inside the
 * construct: no pattern there makes a slot for them.  unify refines and
 * introduces nothing, so it takes no crossing set, and neither does an
 * equation.  The wrapper stays in the term: the authored braces are
 * structure.  On any other head the wrapper is ordinary data. */
typedef enum {
    PRIME_META_NONE = 0,
    PRIME_META_TEMPLATE,
    PRIME_META_BINDER,
    PRIME_META_QUOTE,
} PrimeMetaKind;

typedef struct {
    Atom *plain;       /* the template inside its wrapper */
    Atom *shared;      /* the crossing set's variables, or NULL */
    Atom *shared_node; /* the braces node as written, or NULL */
} PrimeElabLists;

/* Why the last form could not be elaborated, when the elaboration says
 * (prime_semantics_elaboration_error). */
static __thread char g_prime_elab_error[256];

const char *prime_semantics_elaboration_error(void) {
    return g_prime_elab_error[0] ? g_prime_elab_error : NULL;
}

static void prime_elab_refuse(PrimeElab *e, const char *what,
                              const Atom *atom) {
    e->failed = true;
    const char *name = atom && atom->kind == ATOM_SYMBOL
        ? symbol_bytes(g_symbols, atom->sym_id) : NULL;
    snprintf(g_prime_elab_error, sizeof g_prime_elab_error, "%s%s%s", what,
             name ? ": " : "", name ? name : "");
}

static bool prime_elab_list_has(const Atom *list, VarId id) {
    for (CettaExprIndex i = 0u; list && i < list->expr.len; i++)
        if (list->expr.elems[i]->var_id == id) return true;
    return false;
}

/* (meta C {S}): the wrapped term and the braces node. */
static bool prime_meta_braces(const Atom *term, Atom **inner, Atom **braces) {
    if (!atom_is_prime_meta(term) ||
        !atom_is_prime_braces(term->expr.elems[2]))
        return false;
    *inner = term->expr.elems[1];
    *braces = term->expr.elems[2];
    return true;
}

static bool prime_braces_all_vars(const Atom *braces) {
    for (CettaExprIndex i = 1u; i < braces->expr.len; i++)
        if (!braces->expr.elems[i] ||
            braces->expr.elems[i]->kind != ATOM_VAR)
            return false;
    return true;
}

/* How the core reads a crossing set on `inner`. */
static PrimeMetaKind prime_meta_kind(Arena *arena, Atom *inner) {
    if (!inner || inner->kind != ATOM_EXPR) return PRIME_META_NONE;
    if (atom_is_quotation(inner)) return PRIME_META_QUOTE;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (prime_lambda_telescope(arena, inner, &telescope) ||
        prime_iter_template(inner, &iter))
        return PRIME_META_TEMPLATE;
    if (inner->expr.len > 0u && inner->expr.elems[0] &&
        inner->expr.elems[0]->kind == ATOM_SYMBOL &&
        inner->expr.elems[0]->sym_id != g_builtin_syms.equals &&
        inner->expr.elems[0]->sym_id != g_builtin_syms.unify)
        for (CettaExprIndex i = 1u; i < inner->expr.len; i++)
            if (prime_child_role(inner, i) != PRIME_CHILD_VALUE)
                return PRIME_META_BINDER;
    return PRIME_META_NONE;
}

bool prime_semantics_meta_core(Arena *arena, Atom *term) {
    Atom *inner, *braces;
    return prime_meta_braces(term, &inner, &braces) &&
           prime_meta_kind(arena, inner) != PRIME_META_NONE;
}

/* The crossing set of a core construct: its variables as an expression.
 * A crossing set that names anything but variables refuses the form (in
 * code, which is syntax, nothing is read). */
static Atom *prime_elab_crossing(PrimeElab *e, Atom *braces) {
    if (!prime_braces_all_vars(braces)) {
        if (e->code_depth == 0u)
            prime_elab_refuse(e, "a crossing set names something that is "
                                 "not a variable", NULL);
        return NULL;
    }
    Atom *vars = atom_expr(e->arena, braces->expr.elems + 1u,
                           braces->expr.len - 1u);
    if (!vars) e->failed = true;
    return vars;
}

/* A lambda as the elaboration reads it: its telescope, and its crossing set
 * when it is wrapped, (meta (lam binders body) {$t ...}). */
static bool prime_elab_lambda(PrimeElab *e, Atom *term,
                              PrimeLambdaTelescope *telescope,
                              PrimeElabLists *lists) {
    *lists = (PrimeElabLists){.plain = term};
    if (prime_lambda_telescope(e->arena, term, telescope)) return true;
    Atom *inner, *braces;
    if (!prime_meta_braces(term, &inner, &braces) ||
        !prime_lambda_telescope(e->arena, inner, telescope))
        return false;
    lists->plain = inner;
    lists->shared_node = braces;
    lists->shared = prime_elab_crossing(e, braces);
    return !e->failed;
}

/* A map-atom or foldl-atom template as the elaboration reads it, with its
 * crossing set when it is wrapped. */
static bool prime_elab_iter(PrimeElab *e, Atom *term, PrimeIterTemplate *view,
                            PrimeElabLists *lists) {
    *lists = (PrimeElabLists){.plain = term};
    if (prime_iter_template(term, view)) return true;
    Atom *inner, *braces;
    if (!prime_meta_braces(term, &inner, &braces) ||
        !prime_iter_template(inner, view))
        return false;
    lists->plain = inner;
    lists->shared_node = braces;
    lists->shared = prime_elab_crossing(e, braces);
    return !e->failed;
}

/* Whether `term` is a lambda or a map-atom/foldl-atom template, in its
 * crossing set's wrapper or not, that carries its own list: one elaborated
 * before, whose binding structure is decided and kept. */
static bool prime_elaborated_template(Arena *arena, Atom *term) {
    Atom *inner = term, *braces = NULL;
    if (!prime_meta_braces(term, &inner, &braces)) inner = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (prime_lambda_telescope(arena, inner, &telescope))
        return telescope.own != NULL;
    return prime_iter_template(inner, &iter) && iter.own != 0u;
}

/* A template rebuilt with `own` (its own list, or NULL) last: `items` are
 * the plain template's. */
static Atom *prime_elab_iter_rebuild(PrimeElab *e, Atom **items,
                                     CettaExprLen plain_len, Atom *own) {
    Atom **out = arena_alloc(e->arena,
                             sizeof(Atom *) * ((size_t)plain_len + 1u));
    if (!out) {
        e->failed = true;
        return NULL;
    }
    for (CettaExprIndex i = 0u; i < plain_len; i++) out[i] = items[i];
    CettaExprLen len = plain_len;
    if (own) out[len++] = own;
    Atom *result = atom_expr(e->arena, out, len);
    if (!result) e->failed = true;
    return result;
}

/* A rebuilt template in its wrapper again, (meta template braces), when it
 * was written with a crossing set; `braces` is the braces node (resolved by
 * step 2, as written before). */
static Atom *prime_elab_rewrap(PrimeElab *e, Atom *template_term,
                               Atom *braces) {
    if (!template_term || !braces) return template_term;
    Atom *wrapped = atom_expr3(
        e->arena, atom_symbol_id(e->arena, g_builtin_syms.prime_meta),
        template_term, braces);
    if (!wrapped) e->failed = true;
    return wrapped;
}

/* A variable the elaboration splits from `var` (a binder's twin, an own
 * slot, a seal twin): a fresh identity in the instance `var` lives in.  At
 * read time no variable of a form is in an instance.  A form formed while
 * the program runs may hold a call's instance of its variables, and what
 * the formation splits from one stays in that instance, as the variables of
 * a written form are renamed for the call (atom.h, var_epoch_id). */
static Atom *prime_elab_split_var(Arena *arena, Atom *var) {
    return atom_var_like(arena, var,
                         var_epoch_id(fresh_var_id(),
                                      var_epoch_suffix(var->var_id)));
}

/* ── Step 1: lexical binders ───────────────────────────────────────── */

/* The slot of the lexical binders spelled `var` in this form: one per
 * spelling, made at the first binder. */
static Atom *prime_elab_binder_twin(PrimeElab *e, Atom *var) {
    if (prime_elab_is_binder_id(e, var->var_id)) return var;
    bool made = false;
    PrimeVarEntry *entry = prime_var_map_get(&e->binders, var->var_id, 0u,
                                             &made);
    if (!entry) {
        e->failed = true;
        return var;
    }
    if (!made) return entry->atom;
    Atom *twin = prime_elab_split_var(e->arena, var);
    if (!twin) {
        e->failed = true;
        return var;
    }
    entry->atom = twin;
    prime_elab_note_binder(e, twin->var_id);
    return twin;
}

static Atom *prime_elab_binders(PrimeElab *e, Atom *term, bool pattern);

/* Step 1 for one lambda, outer binders first: an inner binder of the same
 * variable then still shadows the outer one when the outer substitution
 * reaches it.  The written lists are kept for step 2. */
static Atom *prime_elab_binders_lambda(PrimeElab *e, Atom *authored,
                                       const PrimeLambdaTelescope *telescope,
                                       const PrimeElabLists *lists) {
    Atom *term = lists->plain;
    Atom **groups = arena_alloc(e->arena, sizeof(Atom *) * telescope->count);
    if (!groups) {
        e->failed = true;
        return authored;
    }
    for (size_t g = 0u; g < telescope->count; g++)
        groups[g] = telescope->groups[g].syntax;
    Atom *body = term->expr.elems[2];
    Atom *own = telescope->own;
    /* The names of an own list already present are slots of their own. */
    for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len; j++)
        prime_elab_note_binder(e, own->expr.elems[j]->var_id);
    for (size_t g = 0u; g < telescope->count && !e->failed; g++) {
        for (size_t i = 0u; i < telescope->groups[g].names_count; i++) {
            CettaPrimeLambdaBinderGroupV1 group = telescope->groups[g];
            group.syntax = groups[g];
            Atom *name = cetta_prime_lambda_binder_name_v1(&group, i);
            if (!name || name->kind != ATOM_VAR) continue;
            /* In code a binder keeps its variable: the seal gives code its
             * identities, and a binder there may be a hole that a pattern
             * around the quotation binds, rebuilding the code with it. */
            if (e->code_depth > 0) {
                prime_elab_note_binder(e, name->var_id);
                continue;
            }
            Atom *twin = prime_elab_binder_twin(e, name);
            if (e->failed || twin == name) continue;
            if (!prime_subst_telescope_in(
                    e->arena, telescope->groups + g + 1u,
                    telescope->count - g - 1u, groups + g + 1u, &body, &own,
                    name, twin, false, false)) {
                e->failed = true;
                break;
            }
            groups[g] = prime_group_with(
                e->arena, &group, group.typed ? group.names_start + i : 0u,
                twin);
            if (!groups[g]) {
                e->failed = true;
                break;
            }
        }
    }
    for (size_t g = 0u; g < telescope->count && !e->failed; g++) {
        CettaPrimeLambdaBinderGroupV1 group = telescope->groups[g];
        group.syntax = groups[g];
        for (size_t t = 0u; group.typed && t < group.types_count; t++) {
            size_t position = group.types_start + t;
            Atom *type = group.syntax->expr.elems[position];
            Atom *next = prime_elab_binders(e, type, false);
            if (next == type) continue;
            group.syntax = prime_group_with(e->arena, &group, position, next);
            if (!group.syntax) {
                e->failed = true;
                break;
            }
            groups[g] = group.syntax;
        }
    }
    if (e->failed) return authored;
    body = prime_elab_binders(e, body, false);
    if (e->failed) return authored;
    bool changed = body != term->expr.elems[2] || own != telescope->own;
    for (size_t g = 0u; g < telescope->count; g++)
        changed = changed || groups[g] != telescope->groups[g].syntax;
    if (!changed) return authored;
    Atom *rebuilt = prime_lambda_rebuild(e->arena, term->expr.elems[0],
                                         telescope->listed, groups,
                                         telescope->count, body, own);
    rebuilt = prime_elab_rewrap(e, rebuilt, lists->shared_node);
    if (!rebuilt) {
        e->failed = true;
        return authored;
    }
    return rebuilt;
}

static Atom *prime_elab_binders_iter(PrimeElab *e, Atom *authored,
                                     const PrimeIterTemplate *view,
                                     const PrimeElabLists *lists) {
    Atom *term = lists->plain;
    CettaExprLen len = term->expr.len;
    Atom **items = arena_alloc(e->arena, sizeof(Atom *) * (size_t)len);
    if (!items) {
        e->failed = true;
        return term;
    }
    for (CettaExprIndex i = 0u; i < len; i++) items[i] = term->expr.elems[i];
    Atom *body = items[view->body];
    if (view->own)
        for (CettaExprIndex j = PRIME_OWN_FIRST;
             j < items[view->own]->expr.len; j++)
            prime_elab_note_binder(e, items[view->own]->expr.elems[j]->var_id);
    for (CettaExprIndex p = 0u; p < view->param_count && !e->failed; p++) {
        CettaExprIndex at = view->first_param + p;
        if (e->code_depth > 0) {
            prime_elab_note_binder(e, items[at]->var_id);
            continue;
        }
        Atom *twin = prime_elab_binder_twin(e, items[at]);
        if (e->failed || twin == items[at]) continue;
        body = prime_subst_in(e->arena, body, items[at], twin, false);
        if (!body) e->failed = true;
        items[at] = twin;
    }
    if (e->failed) return authored;
    for (CettaExprIndex i = 1u; i < view->first_param && !e->failed; i++)
        items[i] = prime_elab_binders(e, items[i], false);
    items[view->body] = prime_elab_binders(e, body, false);
    if (e->failed) return authored;
    bool changed = false;
    for (CettaExprIndex i = 0u; i < len; i++)
        changed = changed || items[i] != term->expr.elems[i];
    if (!changed) return authored;
    Atom *rebuilt = prime_elab_rewrap(e, atom_expr(e->arena, items, len),
                                      lists->shared_node);
    return rebuilt ? rebuilt : authored;
}

/* Step 1 for `(new (names) body)`: each name is a lexical binder of the
 * body, a slot of the binder's own as a lambda parameter is (one per
 * spelling in the form), so no outer substitution or capture reaches it.
 * In code a binder keeps its variable, as a lambda's does. */
static Atom *prime_elab_binders_new(PrimeElab *e, Atom *term) {
    Atom *names = term->expr.elems[1];
    Atom *body = term->expr.elems[2];
    for (CettaExprIndex i = 0u; i < names->expr.len && !e->failed; i++) {
        Atom *name = names->expr.elems[i];
        if (e->code_depth > 0) {
            prime_elab_note_binder(e, name->var_id);
            continue;
        }
        Atom *twin = prime_elab_binder_twin(e, name);
        if (e->failed || twin == name) continue;
        body = prime_subst_in(e->arena, body, name, twin, false);
        names = body ? prime_list_with(e->arena, names, i, twin) : NULL;
        if (!names) e->failed = true;
    }
    if (e->failed) return term;
    body = prime_elab_binders(e, body, false);
    if (e->failed) return term;
    if (names == term->expr.elems[1] && body == term->expr.elems[2])
        return term;
    Atom *rebuilt = atom_expr3(e->arena, term->expr.elems[0], names, body);
    if (!rebuilt) e->failed = true;
    return rebuilt ? rebuilt : term;
}

static Atom *prime_elab_binders_pairs(PrimeElab *e, Atom *pairs) {
    if (!pairs || pairs->kind != ATOM_EXPR)
        return prime_elab_binders(e, pairs, false);
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < pairs->expr.len && !e->failed; i++) {
        Atom *pair = pairs->expr.elems[i];
        Atom *next;
        if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
            Atom *pattern = prime_elab_binders(e, pair->expr.elems[0], true);
            Atom *branch = prime_elab_binders(e, pair->expr.elems[1], false);
            next = pattern == pair->expr.elems[0] &&
                           branch == pair->expr.elems[1]
                ? pair : atom_expr2(e->arena, pattern, branch);
        } else {
            next = prime_elab_binders(e, pair, false);
        }
        if (!next) {
            e->failed = true;
            break;
        }
        if (next != pair && !items) {
            items = arena_alloc(e->arena,
                                sizeof(Atom *) * (size_t)pairs->expr.len);
            if (!items) {
                e->failed = true;
                break;
            }
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = pairs->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    return !e->failed && items ? atom_expr(e->arena, items, pairs->expr.len)
                               : pairs;
}

/* The names a pattern makes fresh under the crossing set `crossing`: its
 * store names outside the set, each once (a lambda's parameter is no store
 * name). */
static void prime_elab_fresh_names(PrimeElab *e, Atom *pattern, Atom *crossing,
                                   Atom ***names, size_t *count,
                                   size_t *cap) {
    if (e->failed || !pattern || !atom_has_vars(pattern)) return;
    if (pattern->kind == ATOM_VAR) {
        if (prime_elab_is_binder_id(e, pattern->var_id) ||
            prime_elab_list_has(crossing, pattern->var_id))
            return;
        for (size_t i = 0u; i < *count; i++)
            if ((*names)[i]->var_id == pattern->var_id) return;
        if (prime_elab_reserve(e, (void **)names, cap, *count + 1u,
                               sizeof(**names)))
            (*names)[(*count)++] = pattern;
        return;
    }
    if (pattern->kind != ATOM_EXPR || !prime_elab_enter(e)) return;
    for (CettaExprIndex i = 0u; i < pattern->expr.len && !e->failed; i++)
        prime_elab_fresh_names(e, pattern->expr.elems[i], crossing, names,
                               count, cap);
    e->nesting--;
}

/* Each name `pattern` makes fresh, renamed to a fresh variable of its
 * spelling in the `count` terms of `region`: the pattern, and the part of
 * the form it scopes over.  The fresh names are recorded (e->fresh): the
 * scope the binder stands in owns them. */
static void prime_elab_make_fresh(PrimeElab *e, Atom *pattern, Atom *crossing,
                                  Atom **region, size_t count) {
    Atom **names = NULL;
    size_t len = 0u, cap = 0u;
    prime_elab_fresh_names(e, pattern, crossing, &names, &len, &cap);
    for (size_t i = 0u; i < len && !e->failed; i++) {
        Atom *fresh = prime_elab_split_var(e->arena, names[i]);
        PrimeVarEntry *entry =
            fresh ? prime_var_map_get(&e->fresh, fresh->var_id, 0u, NULL)
                  : NULL;
        if (!entry) {
            e->failed = true;
            break;
        }
        entry->atom = fresh;
        for (size_t k = 0u; k < count && !e->failed; k++) {
            region[k] = prime_fill_quoted_hole(e->arena, region[k],
                                               names[i]->var_id, fresh);
            if (!region[k]) e->failed = true;
        }
    }
    free(names);
}

/* Step 1 for a pattern binder with a crossing set, (meta B {$t ...}): each
 * pattern makes its names outside the set fresh for the part of the form it
 * scopes over (a let's body, the template of match, chain and filter-atom,
 * a case or switch branch, what follows it in a let*).  The value a pattern
 * meets is read in the scope around it.  Then step 1 goes on inside. */
static Atom *prime_elab_binders_crossing(PrimeElab *e, Atom *term, Atom *inner,
                                         Atom *braces) {
    Atom *crossing = prime_elab_crossing(e, braces);
    if (e->failed) return term;
    SymbolId head = inner->expr.elems[0]->sym_id;
    CettaExprLen len = inner->expr.len;
    Atom **items = arena_alloc(e->arena, sizeof(Atom *) * (size_t)len);
    if (!items) {
        e->failed = true;
        return term;
    }
    for (CettaExprIndex i = 0u; i < len; i++) items[i] = inner->expr.elems[i];
    if ((head == g_builtin_syms.let || head == g_builtin_syms.match ||
         head == g_builtin_syms.chain || head == g_builtin_syms.filter_atom) &&
        len == 4u) {
        CettaExprIndex at = head == g_builtin_syms.let ? 1u : 2u;
        Atom *region[2] = {items[at], items[3]};
        prime_elab_make_fresh(e, items[at], crossing, region, 2u);
        items[at] = region[0];
        items[3] = region[1];
    } else if (head == g_builtin_syms.let_star && len == 3u &&
               items[1]->kind == ATOM_EXPR) {
        CettaExprLen n = items[1]->expr.len;
        Atom **rest = arena_alloc(e->arena,
                                  sizeof(Atom *) * ((size_t)n + 1u));
        Atom **part = arena_alloc(e->arena,
                                  sizeof(Atom *) * ((size_t)n + 1u));
        if (!rest || !part) {
            e->failed = true;
            return term;
        }
        for (CettaExprIndex j = 0u; j < n; j++)
            rest[j] = items[1]->expr.elems[j];
        rest[n] = items[2];
        for (CettaExprIndex i = 0u; i < n && !e->failed; i++) {
            Atom *pair = rest[i];
            if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u)
                continue;
            /* the pair's pattern, the pairs after it and the body */
            size_t count = 1u + (size_t)(n - i);
            part[0] = pair->expr.elems[0];
            for (size_t k = 1u; k < count; k++) part[k] = rest[i + k];
            prime_elab_make_fresh(e, part[0], crossing, part, count);
            if (e->failed) break;
            if (part[0] != pair->expr.elems[0]) {
                rest[i] = atom_expr2(e->arena, part[0], pair->expr.elems[1]);
                if (!rest[i]) e->failed = true;
            }
            for (size_t k = 1u; k < count; k++) rest[i + k] = part[k];
        }
        if (!e->failed) {
            items[1] = atom_expr(e->arena, rest, n);
            items[2] = rest[n];
            if (!items[1]) e->failed = true;
        }
    } else if ((head == g_builtin_syms.case_text ||
                head == g_builtin_syms.switch_text ||
                head == g_builtin_syms.switch_minimal) &&
               (len == 3u || len == 4u) && items[2]->kind == ATOM_EXPR) {
        Atom *branches = items[2];
        Atom **out = arena_alloc(
            e->arena, sizeof(Atom *) * ((size_t)branches->expr.len + 1u));
        if (!out) {
            e->failed = true;
            return term;
        }
        for (CettaExprIndex i = 0u; i < branches->expr.len && !e->failed;
             i++) {
            Atom *branch = branches->expr.elems[i];
            out[i] = branch;
            if (!branch || branch->kind != ATOM_EXPR || branch->expr.len != 2u)
                continue;
            Atom *region[2] = {branch->expr.elems[0], branch->expr.elems[1]};
            prime_elab_make_fresh(e, region[0], crossing, region, 2u);
            if (!e->failed && region[0] != branch->expr.elems[0]) {
                out[i] = atom_expr2(e->arena, region[0], region[1]);
                if (!out[i]) e->failed = true;
            }
        }
        if (!e->failed) {
            items[2] = atom_expr(e->arena, out, branches->expr.len);
            if (!items[2]) e->failed = true;
        }
    }
    if (e->failed) return term;
    Atom *renamed = atom_expr(e->arena, items, len);
    if (!renamed) {
        e->failed = true;
        return term;
    }
    Atom *next = prime_elab_binders(e, renamed, false);
    return e->failed ? term : prime_elab_rewrap(e, next, braces);
}

/* In a pattern position nothing is a lexical binder: a lambda or template
 * form written there is a pattern, and its names are store-name
 * occurrences (rule 4). */
static Atom *prime_elab_binders(PrimeElab *e, Atom *term, bool pattern) {
    if (e->failed || !term || term->kind != ATOM_EXPR || !atom_has_vars(term))
        return term;
    if (pattern) return term;
    if (!prime_elab_enter(e)) return term;
    Atom *result = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    PrimeElabLists lists;
    Atom *meta_inner, *meta_braces;
    if (prime_code_like(term)) {
        e->code_depth++;
        Atom *payload = prime_elab_binders(e, term->expr.elems[1], false);
        e->code_depth--;
        if (payload && payload != term->expr.elems[1])
            result = prime_code_with_payload(e->arena, term, payload);
    } else if (prime_elab_lambda(e, term, &telescope, &lists)) {
        result = prime_elab_binders_lambda(e, term, &telescope, &lists);
    } else if (!e->failed && prime_elab_iter(e, term, &iter, &lists)) {
        result = prime_elab_binders_iter(e, term, &iter, &lists);
    } else if (e->failed) {
        result = term;
    } else if (prime_new_form(term)) {
        result = prime_elab_binders_new(e, term);
    } else if (e->code_depth == 0u &&
               prime_meta_braces(term, &meta_inner, &meta_braces) &&
               prime_meta_kind(e->arena, meta_inner) == PRIME_META_BINDER) {
        result = prime_elab_binders_crossing(e, term, meta_inner, meta_braces);
    } else {
        Atom **items = NULL;
        for (CettaExprIndex i = 0u; i < term->expr.len && !e->failed; i++) {
            Atom *child = term->expr.elems[i];
            PrimeChildRole role = prime_child_role(term, i);
            Atom *next = role == PRIME_CHILD_PATTERN_PAIRS
                ? prime_elab_binders_pairs(e, child)
                : prime_elab_binders(e, child, role == PRIME_CHILD_PATTERN);
            if (next != child && !items) {
                items = arena_alloc(e->arena,
                                    sizeof(Atom *) * (size_t)term->expr.len);
                if (!items) {
                    e->failed = true;
                    break;
                }
                for (CettaExprIndex j = 0u; j < i; j++)
                    items[j] = term->expr.elems[j];
            }
            if (items) items[i] = next;
        }
        if (!e->failed && items)
            result = atom_expr(e->arena, items, term->expr.len);
    }
    e->nesting--;
    return result;
}

/* ── Holes and binder scopes, shared by steps 2 and 3 ──────────────── */

static uint32_t prime_scope_current(const PrimeElab *e);

/* One hole pushed, with the scope it is pushed in (step 2's current scope;
 * no scope for the other passes). */
static void prime_elab_push_hole(PrimeElab *e, VarId id) {
    if (prime_elab_reserve(e, (void **)&e->holes, &e->holes_cap,
                           e->holes_len + 1u, sizeof(*e->holes)) &&
        prime_elab_reserve(e, (void **)&e->holes_scope, &e->holes_scope_cap,
                           e->holes_len + 1u, sizeof(*e->holes_scope))) {
        e->holes_scope[e->holes_len] = prime_scope_current(e);
        e->holes[e->holes_len++] = id;
    }
}

/* The holes a pattern binds: its variables inside quotations written in
 * the pattern.  They are pushed while a pass walks the binder's scope. */
static void prime_elab_push_holes(PrimeElab *e, Atom *pattern, bool quoted) {
    if (e->failed || !pattern || !atom_has_vars(pattern)) return;
    if (pattern->kind == ATOM_VAR) {
        if (!quoted) return;
        prime_elab_push_hole(e, pattern->var_id);
        return;
    }
    if (pattern->kind != ATOM_EXPR) return;
    bool inner = quoted || atom_is_quotation(pattern);
    for (CettaExprIndex i = 0u; i < pattern->expr.len && !e->failed; i++)
        prime_elab_push_holes(e, pattern->expr.elems[i], inner);
}

/* A quotation's crossing set, (meta (quote X) {$t ...}): its names are holes
 * in scope inside the quotation, as a pattern's holes are.  Pushed while a
 * pass walks the quotation; the mark to pop to is returned. */
static size_t prime_elab_push_crossing_holes(PrimeElab *e, Atom *braces) {
    size_t mark = e->holes_len;
    for (CettaExprIndex i = 1u; i < braces->expr.len && !e->failed; i++)
        prime_elab_push_hole(e, braces->expr.elems[i]->var_id);
    return mark;
}

/* A quotation with a crossing set of variables: (meta (quote X) {...}). */
static bool prime_elab_meta_quote(const Atom *term, Atom **quote,
                                  Atom **braces) {
    Atom *inner;
    if (!prime_meta_braces(term, &inner, braces) ||
        !atom_is_quotation(inner) || !prime_braces_all_vars(*braces))
        return false;
    *quote = inner;
    return true;
}

static Atom *prime_elab_rebuild_meta(PrimeElab *e, Atom *term, Atom *inner,
                                     Atom *braces) {
    if (e->failed) return term;
    if (inner == term->expr.elems[1] && braces == term->expr.elems[2])
        return term;
    Atom *rebuilt = atom_expr3(e->arena, term->expr.elems[0], inner, braces);
    if (!rebuilt) e->failed = true;
    return rebuilt ? rebuilt : term;
}

static bool prime_elab_hole_in_scope(const PrimeElab *e, VarId id) {
    for (size_t i = e->holes_len; i > 0u; i--)
        if (e->holes[i - 1u] == id) return true;
    return false;
}

/* The scope the hole `id` in scope was pushed in (step 2). */
static bool prime_elab_hole_scope(const PrimeElab *e, VarId id,
                                  uint32_t *scope) {
    for (size_t i = e->holes_len; i > 0u; i--)
        if (e->holes[i - 1u] == id) {
            *scope = e->holes_scope[i - 1u];
            return true;
        }
    return false;
}

/* The patterns of a binding construct that scope over its child `index`: a
 * let's body, the template of match, chain and filter-atom, the branches of
 * unify, an equation's body.  case, switch and let* bind pair by pair. */
static size_t prime_elab_binder_cover(const Atom *term, CettaExprIndex index,
                                      CettaExprIndex patterns[2]) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL)
        return 0u;
    SymbolId head = term->expr.elems[0]->sym_id;
    CettaExprLen len = term->expr.len;
    if (head == g_builtin_syms.let && len == 4u && index == 3u) {
        patterns[0] = 1u;
        return 1u;
    }
    if ((head == g_builtin_syms.match || head == g_builtin_syms.chain ||
         head == g_builtin_syms.filter_atom) &&
        len == 4u && index == 3u) {
        patterns[0] = 2u;
        return 1u;
    }
    if (head == g_builtin_syms.unify && len == 5u && index == 3u) {
        patterns[0] = 1u;
        patterns[1] = 2u;
        return 2u;
    }
    if (head == g_builtin_syms.equals && len == 3u && index == 2u) {
        patterns[0] = 1u;
        return 1u;
    }
    return 0u;
}

static bool prime_elab_is_let_star(const Atom *term) {
    return term && term->kind == ATOM_EXPR && term->expr.len == 3u &&
           term->expr.elems[0] && term->expr.elems[0]->kind == ATOM_SYMBOL &&
           term->expr.elems[0]->sym_id == g_builtin_syms.let_star;
}

static Atom **prime_elab_items_for(PrimeElab *e, Atom *term, Atom **items,
                                   CettaExprIndex i, Atom *next) {
    if (items || next == term->expr.elems[i]) {
        if (items) items[i] = next;
        return items;
    }
    items = arena_alloc(e->arena, sizeof(Atom *) * (size_t)term->expr.len);
    if (!items) {
        e->failed = true;
        return NULL;
    }
    for (CettaExprIndex j = 0u; j < term->expr.len; j++)
        items[j] = term->expr.elems[j];
    items[i] = next;
    return items;
}

static Atom *prime_elab_rebuilt(PrimeElab *e, Atom *term, Atom **items) {
    if (e->failed || !items) return term;
    Atom *result = atom_expr(e->arena, items, term->expr.len);
    if (!result) e->failed = true;
    return result ? result : term;
}

/* ── Step 2: ownership ─────────────────────────────────────────────── */

/* The own list a template receives: its slots under the profile's record;
 * NULL when it needs none (no slots, the reference readout, a lambda). */
static Atom *prime_elab_own_list(PrimeElab *e, Atom *const *names,
                                 size_t count, bool always) {
    if (count == 0u && !always &&
        e->profile.readout != CETTA_PRIME_READOUT_SNAPSHOT)
        return NULL;
    Atom **items = arena_alloc(e->arena, sizeof(Atom *) * (count + 2u));
    if (!items) {
        e->failed = true;
        return NULL;
    }
    items[0] = atom_internal_tag(e->arena, CETTA_INTERNAL_TAG_PRIME_OWN);
    items[1] = e->record;
    for (size_t i = 0u; i < count; i++) items[i + 2u] = names[i];
    Atom *list = atom_expr(e->arena, items, (CettaExprLen)(count + 2u));
    if (!list) e->failed = true;
    return list;
}

static bool prime_scope_push(PrimeElab *e, uint32_t scope) {
    if (!prime_elab_reserve(e, (void **)&e->stack, &e->stack_cap,
                            e->stack_len + 1u, sizeof(*e->stack)))
        return false;
    e->stack[e->stack_len++] = scope;
    return true;
}

static uint32_t prime_scope_current(const PrimeElab *e) {
    return e->stack_len ? e->stack[e->stack_len - 1u] : 0u;
}

static uint32_t prime_scope_new(PrimeElab *e, uint32_t parent,
                                const PrimeElabLists *lists) {
    if (!prime_elab_reserve(e, (void **)&e->scopes, &e->scopes_cap,
                            e->scopes_len + 1u, sizeof(*e->scopes)))
        return 0u;
    e->scopes[e->scopes_len] = (PrimeScopeNode){
        .parent = (int32_t)parent,
        .shared = lists->shared,
    };
    return (uint32_t)e->scopes_len++;
}

static void prime_scope_note(PrimeElab *e, uint32_t scope, VarId id) {
    PrimeScopeNode *node = &e->scopes[scope];
    if (node->direct_len &&
        node->direct[node->direct_len - 1u] == id)
        return;
    if (prime_elab_reserve(e, (void **)&node->direct, &node->direct_cap,
                           node->direct_len + 1u, sizeof(*node->direct)))
        node->direct[node->direct_len++] = id;
}

static int prime_var_id_cmp(const void *left, const void *right) {
    VarId a = *(const VarId *)left, b = *(const VarId *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static size_t prime_var_ids_sort(VarId *ids, size_t len) {
    if (len < 2u) return len;
    qsort(ids, len, sizeof(*ids), prime_var_id_cmp);
    size_t out = 1u;
    for (size_t i = 1u; i < len; i++)
        if (ids[i] != ids[out - 1u]) ids[out++] = ids[i];
    return out;
}

static bool prime_var_ids_has(const VarId *ids, size_t len, VarId id) {
    size_t lo = 0u, hi = len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (ids[mid] < id) lo = mid + 1u;
        else if (ids[mid] > id) hi = mid;
        else return true;
    }
    return false;
}

static void prime_scope_add(PrimeElab *e, VarId **ids, size_t *len,
                            size_t *cap, VarId id) {
    if (prime_elab_reserve(e, (void **)ids, cap, *len + 1u, sizeof(**ids)))
        (*ids)[(*len)++] = id;
}

/* A spelling of the name `var`, kept for a slot made from its id alone. */
static void prime_scope_spell(PrimeElab *e, Atom *var) {
    bool made = false;
    PrimeVarEntry *entry = prime_var_map_get(&e->spellings, var->var_id, 0u,
                                             &made);
    if (!entry)
        e->failed = true;
    else if (made)
        entry->atom = var;
}

/* Whether rule M quantifies `id` around the template scope `scope`: a scope
 * enclosing it writes the name directly or owns it. */
static bool prime_scope_quantified_above(const PrimeElab *e, uint32_t scope,
                                         VarId id) {
    int32_t at = e->scopes[scope].parent;
    while (at >= 0) {
        const PrimeScopeNode *node = &e->scopes[at];
        if (prime_var_ids_has(node->direct, node->direct_len, id) ||
            prime_var_ids_has(node->own, node->own_len, id))
            return true;
        at = node->parent;
    }
    return false;
}

/* The own sets of the template scopes (every profile but lexical-fresh),
 * outer before inner.  A template uses the names written directly in it
 * and those its templates' crossing sets share with it.  With a crossing
 * set written, it owns every name it uses outside the set, under every
 * profile.  With none, the profile decides: explicit capture owns every
 * name it uses, rule M the names written directly in it that no enclosing
 * scope quantifies, query-wide nothing.  The names a crossing set on a
 * pattern binder made fresh are owned where the binder stands. */
static void prime_scope_compute(PrimeElab *e) {
    for (size_t s = 0u; s < e->scopes_len; s++) {
        PrimeScopeNode *node = &e->scopes[s];
        node->direct_len = prime_var_ids_sort(node->direct, node->direct_len);
        node->passed_len = prime_var_ids_sort(node->passed, node->passed_len);
        node->fresh_len = prime_var_ids_sort(node->fresh, node->fresh_len);
    }
    uint8_t ownership = e->profile.ownership;
    bool capture = ownership == CETTA_PRIME_OWNERSHIP_EXPLICIT_CAPTURE ||
                   ownership == CETTA_PRIME_OWNERSHIP_LEXICAL_INVENTORY;
    for (size_t s = 1u; s < e->scopes_len && !e->failed; s++) {
        PrimeScopeNode *node = &e->scopes[s];
        if (node->shared || capture) {
            for (size_t i = 0u; i < node->direct_len + node->passed_len; i++) {
                VarId id = i < node->direct_len
                    ? node->direct[i] : node->passed[i - node->direct_len];
                if (!prime_elab_list_has(node->shared, id))
                    prime_scope_add(e, &node->own, &node->own_len,
                                    &node->own_cap, id);
            }
        } else if (ownership != CETTA_PRIME_OWNERSHIP_QUERY_WIDE) {
            for (size_t i = 0u; i < node->direct_len; i++)
                if (!prime_scope_quantified_above(e, (uint32_t)s,
                                                  node->direct[i]))
                    prime_scope_add(e, &node->own, &node->own_len,
                                    &node->own_cap, node->direct[i]);
        }
        for (size_t i = 0u; i < node->fresh_len; i++)
            prime_scope_add(e, &node->own, &node->own_len, &node->own_cap,
                            node->fresh[i]);
        node->own_len = prime_var_ids_sort(node->own, node->own_len);
    }
}

/* The slot scope `scope` owns for the variable `var`. */
static Atom *prime_scope_slot(PrimeElab *e, uint32_t scope, Atom *var) {
    bool made = false;
    PrimeVarEntry *entry = prime_var_map_get(&e->slots, var->var_id, scope,
                                             &made);
    if (!entry) {
        e->failed = true;
        return var;
    }
    if (made) {
        entry->atom = prime_elab_split_var(e->arena, var);
        if (!entry->atom) {
            e->failed = true;
            return var;
        }
    }
    return entry->atom;
}

/* The slot an occurrence denotes: the slot of the innermost template on the
 * walk's path that owns the name, else the name itself. */
static Atom *prime_scope_resolve(PrimeElab *e, Atom *var) {
    if (prime_elab_is_binder_id(e, var->var_id)) return var;
    for (size_t k = e->stack_len; k > 0u; k--) {
        uint32_t scope = e->stack[k - 1u];
        const PrimeScopeNode *node = &e->scopes[scope];
        if (prime_var_ids_has(node->own, node->own_len, var->var_id))
            return prime_scope_slot(e, scope, var);
    }
    return var;
}

typedef enum {
    PRIME_SCOPE_COLLECT = 0,
    PRIME_SCOPE_APPLY,
} PrimeScopeMode;

/* The variables of a store-name occurrence of the current scope.  Inside a
 * written quotation, a hole (a name a pattern binds inside a quotation, or a
 * crossing name of the quotation) is an occurrence where its pattern stands,
 * not of the code's own scopes: the code takes its value from there. */
static Atom *prime_scope_var(PrimeElab *e, Atom *var, PrimeScopeMode mode) {
    if (prime_elab_is_binder_id(e, var->var_id)) return var;
    if (mode == PRIME_SCOPE_COLLECT) {
        uint32_t scope = prime_scope_current(e), hole_scope;
        if (e->code_scopes > 0u &&
            prime_elab_hole_scope(e, var->var_id, &hole_scope))
            scope = hole_scope;
        prime_scope_note(e, scope, var->var_id);
        prime_scope_spell(e, var);
        return var;
    }
    return prime_scope_resolve(e, var);
}

static Atom *prime_scope_walk(PrimeElab *e, Atom *term, bool pattern,
                              PrimeScopeMode mode);

/* A crossing set written on a template: in the collect walk, the names its
 * template shares with the scope around, which that scope uses (they are
 * no occurrence written there); in the apply walk, references to the scope
 * around, resolved there. */
static Atom *prime_scope_crossing(PrimeElab *e, Atom *braces,
                                  const PrimeElabLists *lists,
                                  PrimeScopeMode mode) {
    if (!braces) return NULL;
    if (mode == PRIME_SCOPE_APPLY)
        return prime_scope_walk(e, braces, false, mode);
    PrimeScopeNode *node = &e->scopes[prime_scope_current(e)];
    for (CettaExprIndex j = 0u; lists->shared && j < lists->shared->expr.len;
         j++) {
        prime_scope_add(e, &node->passed, &node->passed_len, &node->passed_cap,
                        lists->shared->expr.elems[j]->var_id);
        prime_scope_spell(e, lists->shared->expr.elems[j]);
    }
    return braces;
}

/* Whether the braces node `braces` holds the variable `id`. */
static bool prime_braces_has(const Atom *braces, VarId id) {
    for (CettaExprIndex i = 1u; braces && i < braces->expr.len; i++)
        if (braces->expr.elems[i] && braces->expr.elems[i]->kind == ATOM_VAR &&
            braces->expr.elems[i]->var_id == id)
            return true;
    return false;
}

/* The names a pattern binder's crossing set made fresh (step 1): the names
 * of its patterns step 1 renamed, outside its own set (a name in the set is
 * one an enclosing binder made fresh, shared here).  They are owned by the
 * scope the binder stands in. */
static void prime_scope_note_fresh(PrimeElab *e, Atom *pattern,
                                   const Atom *braces) {
    if (e->failed || !pattern || !atom_has_vars(pattern)) return;
    if (pattern->kind == ATOM_VAR) {
        if (prime_var_map_find(&e->fresh, pattern->var_id, 0u) &&
            !prime_braces_has(braces, pattern->var_id)) {
            PrimeScopeNode *node = &e->scopes[prime_scope_current(e)];
            prime_scope_add(e, &node->fresh, &node->fresh_len,
                            &node->fresh_cap, pattern->var_id);
        }
        return;
    }
    if (pattern->kind != ATOM_EXPR) return;
    for (CettaExprIndex i = 0u; i < pattern->expr.len && !e->failed; i++)
        prime_scope_note_fresh(e, pattern->expr.elems[i], braces);
}

static void prime_scope_note_binder_fresh(PrimeElab *e, Atom *binder,
                                          const Atom *braces) {
    for (CettaExprIndex i = 1u; i < binder->expr.len && !e->failed; i++) {
        PrimeChildRole role = prime_child_role(binder, i);
        Atom *child = binder->expr.elems[i];
        if (role == PRIME_CHILD_PATTERN) {
            prime_scope_note_fresh(e, child, braces);
        } else if (role == PRIME_CHILD_PATTERN_PAIRS &&
                   child->kind == ATOM_EXPR) {
            for (CettaExprIndex j = 0u; j < child->expr.len; j++) {
                Atom *pair = child->expr.elems[j];
                if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u)
                    prime_scope_note_fresh(e, pair->expr.elems[0], braces);
            }
        }
    }
}

/* Quoted code: only its holes in scope are occurrences, where the
 * quotation stands; every other quoted name is the code's. */
static Atom *prime_scope_code(PrimeElab *e, Atom *term, PrimeScopeMode mode) {
    if (e->failed || !term || !atom_has_vars(term)) return term;
    if (term->kind == ATOM_VAR)
        return prime_elab_hole_in_scope(e, term->var_id)
            ? prime_scope_var(e, term, mode) : term;
    if (term->kind != ATOM_EXPR || !prime_elab_enter(e)) return term;
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len && !e->failed; i++)
        items = prime_elab_items_for(
            e, term, items, i, prime_scope_code(e, term->expr.elems[i], mode));
    e->nesting--;
    return prime_elab_rebuilt(e, term, items);
}

static Atom *prime_scope_walk(PrimeElab *e, Atom *term, bool pattern,
                              PrimeScopeMode mode);

static Atom *prime_scope_template_list(PrimeElab *e, uint32_t scope,
                                       size_t hoist_mark, bool always);

/* A written quotation in value position is a scope, as a template is (stage
 * 5, item 3): the names written directly in its code that it owns, by the
 * same options and crossing sets as a template's, become its own slots, in
 * its own list `(quote X (OWN RECORD $u ...))`, hidden metadata; each
 * opening of the code (`*`, or the instance `lift let` makes) copies them.
 * A lambda or template written in the code is a scope inside it.  A hole
 * stays an occurrence where its pattern stands (prime_scope_var).  A
 * quotation that carries an own list was elaborated before. */
static Atom *prime_scope_quote(PrimeElab *e, Atom *term, PrimeScopeMode mode) {
    if (term->expr.len != 2u) return term;
    PrimeElabLists lists = {.plain = term,
                            .shared = e->pending_quote_crossing};
    e->pending_quote_crossing = NULL;
    uint32_t scope = mode == PRIME_SCOPE_COLLECT
        ? prime_scope_new(e, prime_scope_current(e), &lists)
        : ++e->next_scope;
    size_t hoist_mark = e->hoist_len;
    if (e->failed || !prime_scope_push(e, scope)) return term;
    e->code_scopes++;
    Atom *payload = prime_scope_walk(e, term->expr.elems[1], false, mode);
    e->code_scopes--;
    e->stack_len--;
    if (e->failed || mode == PRIME_SCOPE_COLLECT) return term;
    Atom *own = prime_scope_template_list(e, scope, hoist_mark, false);
    if (e->failed) return term;
    if (payload == term->expr.elems[1] && !own) return term;
    Atom *result = own
        ? atom_expr3(e->arena, term->expr.elems[0], payload, own)
        : atom_expr2(e->arena, term->expr.elems[0], payload);
    if (!result) e->failed = true;
    return result ? result : term;
}

/* The own list of the template whose scope is `scope`, after its body was
 * walked: its own slots, or under per-closure those of the templates in it,
 * which moved up to it (`hoist_mark`: where its nested templates' slots
 * begin in e->hoist), and the fresh slots of the pattern binders in it,
 * made at each of its calls.  Its other own slots then move up to its
 * creator. */
static Atom *prime_scope_template_list(PrimeElab *e, uint32_t scope,
                                       size_t hoist_mark, bool always) {
    const PrimeScopeNode *node = &e->scopes[scope];
    Atom **own = node->own_len
        ? arena_alloc(e->arena, sizeof(Atom *) * node->own_len) : NULL;
    if (node->own_len && !own) {
        e->failed = true;
        return NULL;
    }
    /* A name a nested template's crossing set passes up but nothing writes
     * has no slot, and nothing to copy.  The fresh slots come first. */
    size_t count = 0u, fresh = 0u;
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0u; i < node->own_len; i++) {
            bool is_fresh = prime_var_ids_has(node->fresh, node->fresh_len,
                                              node->own[i]);
            if (is_fresh != (pass == 0)) continue;
            PrimeVarEntry *entry = prime_var_map_find(&e->slots, node->own[i],
                                                      scope);
            if (!entry || !entry->atom) continue;
            own[count++] = entry->atom;
            if (is_fresh) fresh++;
        }
    Atom *list;
    if (e->profile.lifetime == CETTA_PRIME_LIFETIME_PER_CLOSURE) {
        for (size_t i = 0u; i < fresh && !e->failed; i++)
            if (prime_elab_reserve(e, (void **)&e->hoist, &e->hoist_cap,
                                   e->hoist_len + 1u, sizeof(*e->hoist)))
                e->hoist[e->hoist_len++] = own[i];
        list = e->failed ? NULL
                         : prime_elab_own_list(e, e->hoist + hoist_mark,
                                               e->hoist_len - hoist_mark,
                                               always);
        e->hoist_len = hoist_mark;
        for (size_t i = fresh; i < count && !e->failed; i++)
            if (prime_elab_reserve(e, (void **)&e->hoist, &e->hoist_cap,
                                   e->hoist_len + 1u, sizeof(*e->hoist)))
                e->hoist[e->hoist_len++] = own[i];
    } else {
        list = prime_elab_own_list(e, own, count, always);
    }
    return list;
}

static Atom *prime_scope_walk_pairs(PrimeElab *e, Atom *pairs,
                                    bool sequential, PrimeScopeMode mode) {
    if (!pairs || pairs->kind != ATOM_EXPR)
        return prime_scope_walk(e, pairs, false, mode);
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < pairs->expr.len && !e->failed; i++) {
        Atom *pair = pairs->expr.elems[i];
        Atom *next;
        if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
            Atom *pattern = prime_scope_walk(e, pair->expr.elems[0], true,
                                             mode);
            size_t holes_mark = e->holes_len;
            if (!sequential)
                prime_elab_push_holes(e, pair->expr.elems[0], false);
            Atom *branch = prime_scope_walk(e, pair->expr.elems[1], false,
                                            mode);
            if (sequential)
                prime_elab_push_holes(e, pair->expr.elems[0], false);
            else
                e->holes_len = holes_mark;
            next = pattern == pair->expr.elems[0] &&
                           branch == pair->expr.elems[1]
                ? pair : atom_expr2(e->arena, pattern, branch);
            if (!next) e->failed = true;
        } else {
            next = prime_scope_walk(e, pair, false, mode);
        }
        items = prime_elab_items_for(e, pairs, items, i, next);
    }
    return prime_elab_rebuilt(e, pairs, items);
}

static Atom *prime_scope_walk_children(PrimeElab *e, Atom *term, bool pattern,
                                       PrimeScopeMode mode) {
    size_t holes_mark = e->holes_len;
    bool sequential = !pattern && prime_elab_is_let_star(term);
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len && !e->failed; i++) {
        PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                      : prime_child_role(term, i);
        CettaExprIndex covers[2];
        size_t count = pattern ? 0u : prime_elab_binder_cover(term, i, covers);
        size_t child_holes = e->holes_len;
        for (size_t k = 0u; k < count; k++)
            prime_elab_push_holes(e, term->expr.elems[covers[k]], false);
        Atom *next = role == PRIME_CHILD_PATTERN_PAIRS
            ? prime_scope_walk_pairs(e, term->expr.elems[i], sequential, mode)
            : prime_scope_walk(e, term->expr.elems[i],
                               role == PRIME_CHILD_PATTERN, mode);
        items = prime_elab_items_for(e, term, items, i, next);
        if (!(sequential && role == PRIME_CHILD_PATTERN_PAIRS))
            e->holes_len = child_holes;
    }
    e->holes_len = holes_mark;
    return prime_elab_rebuilt(e, term, items);
}

/* Step 2 for every option but lexical-fresh.  The collect walk notes, in
 * each template scope, the names written directly in it; the apply walk,
 * which meets the scopes in the same order, rewrites each occurrence to its
 * owner's slot and gives each template its own list.  A template that
 * carries an own list was elaborated before, when it was formed (a value
 * that is a part of a term formed now): its binding structure is kept as it
 * is, so it is no scope, and its names are no occurrences of the scopes
 * around it; it captures what it captured. */
static Atom *prime_scope_walk(PrimeElab *e, Atom *term, bool pattern,
                              PrimeScopeMode mode) {
    if (e->failed || !term || !atom_has_vars(term)) return term;
    if (term->kind == ATOM_VAR) return prime_scope_var(e, term, mode);
    if (term->kind != ATOM_EXPR) return term;
    if (atom_is_drop_of_quoted_variable(term)) {
        Atom *var = term->expr.elems[1]->expr.elems[1];
        Atom *image = prime_scope_var(e, var, mode);
        if (image == var) return term;
        Atom *quoted = atom_expr2(e->arena, term->expr.elems[1]->expr.elems[0],
                                  image);
        Atom *drop = quoted ? atom_expr2(e->arena, term->expr.elems[0], quoted)
                            : NULL;
        if (!drop) e->failed = true;
        return drop ? drop : term;
    }
    if (!prime_elab_enter(e)) return term;
    Atom *result = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    PrimeElabLists lists;
    Atom *meta_quote, *meta_braces;
    Atom *meta_inner;
    if (!pattern && prime_elab_meta_quote(term, &meta_quote, &meta_braces)) {
        /* A quotation's crossing names are occurrences where it stands;
         * every other name it holds is its own. */
        Atom *braces = prime_scope_walk(e, meta_braces, false, mode);
        size_t mark = prime_elab_push_crossing_holes(e, meta_braces);
        Atom *crossing = atom_expr(e->arena, meta_braces->expr.elems + 1u,
                                   meta_braces->expr.len - 1u);
        if (!crossing) e->failed = true;
        e->pending_quote_crossing = crossing;
        Atom *quote = prime_scope_walk(e, meta_quote, false, mode);
        e->pending_quote_crossing = NULL;
        e->holes_len = mark;
        result = prime_elab_rebuild_meta(e, term, quote, braces);
    } else if (!pattern &&
               prime_meta_braces(term, &meta_inner, &meta_braces) &&
               prime_meta_kind(e->arena, meta_inner) == PRIME_META_BINDER) {
        /* A pattern binder's crossing set refers to the scope around and is
         * no occurrence written there; the names its patterns made fresh
         * are this scope's. */
        if (mode == PRIME_SCOPE_COLLECT) {
            prime_scope_note_binder_fresh(e, meta_inner, meta_braces);
            (void)prime_scope_walk(e, meta_inner, false, mode);
        } else {
            Atom *inner = prime_scope_walk(e, meta_inner, false, mode);
            Atom *braces = prime_scope_walk(e, meta_braces, false, mode);
            result = prime_elab_rebuild_meta(e, term, inner, braces);
        }
    } else if (!pattern && prime_elaborated_template(e->arena, term)) {
        result = term;
    } else if (!pattern && atom_is_quotation(term)) {
        result = prime_scope_quote(e, term, mode);
    } else if (!pattern && prime_code_like(term)) {
        /* Contextual code as written: its holes in scope are occurrences
         * where it stands. */
        Atom *payload = prime_scope_code(e, term->expr.elems[1], mode);
        if (payload != term->expr.elems[1]) {
            result = prime_code_with_payload(e->arena, term, payload);
            if (!result) {
                e->failed = true;
                result = term;
            }
        }
    } else if (!pattern && prime_elab_lambda(e, term, &telescope, &lists) &&
               !telescope.own) {
        Atom *plain = lists.plain;
        Atom **groups = arena_alloc(e->arena, sizeof(Atom *) * telescope.count);
        if (!groups) {
            e->failed = true;
            goto done;
        }
        for (size_t g = 0u; g < telescope.count && !e->failed; g++) {
            CettaPrimeLambdaBinderGroupV1 group = telescope.groups[g];
            groups[g] = group.syntax;
            for (size_t t = 0u; group.typed && t < group.types_count; t++) {
                size_t position = group.types_start + t;
                Atom *type = group.syntax->expr.elems[position];
                Atom *next = prime_scope_walk(e, type, false, mode);
                if (next == type) continue;
                group.syntax = prime_group_with(e->arena, &group, position,
                                                next);
                if (!group.syntax) {
                    e->failed = true;
                    break;
                }
                groups[g] = group.syntax;
            }
        }
        Atom *crossing = prime_scope_crossing(e, lists.shared_node, &lists,
                                              mode);
        uint32_t scope = mode == PRIME_SCOPE_COLLECT
            ? prime_scope_new(e, prime_scope_current(e), &lists)
            : ++e->next_scope;
        size_t hoist_mark = e->hoist_len;
        if (e->failed || !prime_scope_push(e, scope)) goto done;
        Atom *body = prime_scope_walk(e, plain->expr.elems[2], false, mode);
        e->stack_len--;
        if (e->failed || mode == PRIME_SCOPE_COLLECT) goto done;
        Atom *own = prime_scope_template_list(e, scope, hoist_mark, false);
        if (e->failed) goto done;
        result = prime_lambda_rebuild(e->arena, plain->expr.elems[0],
                                      telescope.listed, groups,
                                      telescope.count, body, own);
        if (!result) {
            e->failed = true;
            result = term;
        } else {
            result = prime_elab_rewrap(e, result, crossing);
        }
    } else if (!pattern && !e->failed &&
               prime_elab_iter(e, term, &iter, &lists) && !iter.own) {
        CettaExprLen plain = lists.plain->expr.len;
        Atom **items = arena_alloc(e->arena, sizeof(Atom *) * (size_t)plain);
        if (!items) {
            e->failed = true;
            goto done;
        }
        for (CettaExprIndex i = 0u; i < plain; i++)
            items[i] = lists.plain->expr.elems[i];
        for (CettaExprIndex i = 1u; i < iter.first_param; i++)
            items[i] = prime_scope_walk(e, items[i], false, mode);
        Atom *crossing = prime_scope_crossing(e, lists.shared_node, &lists,
                                              mode);
        uint32_t scope = mode == PRIME_SCOPE_COLLECT
            ? prime_scope_new(e, prime_scope_current(e), &lists)
            : ++e->next_scope;
        size_t hoist_mark = e->hoist_len;
        if (e->failed || !prime_scope_push(e, scope)) goto done;
        items[iter.body] = prime_scope_walk(e, items[iter.body], false, mode);
        e->stack_len--;
        if (e->failed || mode == PRIME_SCOPE_COLLECT) goto done;
        /* A literal template always carries its record: it marks the
         * template the evaluator activates by rule 3
         * (prime_semantics_iteration_step). */
        Atom *own = prime_scope_template_list(e, scope, hoist_mark, true);
        if (e->failed) goto done;
        Atom *rebuilt = prime_elab_iter_rebuild(e, items, plain, own);
        if (rebuilt) result = prime_elab_rewrap(e, rebuilt, crossing);
    } else {
        result = prime_scope_walk_children(e, term, pattern, mode);
    }
done:
    e->nesting--;
    return result;
}

/* ── Step 2, lexical-fresh: resolution ─────────────────────────────── */

static Atom *prime_lex_lookup(PrimeElab *e, Atom *var) {
    if (prime_elab_is_binder_id(e, var->var_id)) return var;
    for (size_t i = e->lex_len; i > 0u; i--)
        if (e->lex[i - 1u].from == var->var_id) return e->lex[i - 1u].to;
    return var;
}

/* Whether `id` is a crossing name in force: no pattern makes a slot for
 * it (the innermost entry decides). */
static bool prime_lex_is_shared(const PrimeElab *e, VarId id) {
    for (size_t i = e->lex_shared_len; i > 0u; i--)
        if (e->lex_shared[i - 1u].id == id)
            return e->lex_shared[i - 1u].in_force;
    return false;
}

/* Puts the names of the expression `names` in force, or out of force over
 * an entry around; the caller pops to the returned mark. */
static size_t prime_lex_force(PrimeElab *e, const Atom *names,
                              bool in_force) {
    size_t mark = e->lex_shared_len;
    for (CettaExprIndex j = 0u; names && j < names->expr.len && !e->failed;
         j++)
        if (prime_elab_reserve(e, (void **)&e->lex_shared, &e->lex_shared_cap,
                               e->lex_shared_len + 1u,
                               sizeof(*e->lex_shared)))
            e->lex_shared[e->lex_shared_len++] = (PrimeLexForce){
                .id = names->expr.elems[j]->var_id, .in_force = in_force};
    return mark;
}

static void prime_lex_force_id(PrimeElab *e, VarId id, bool in_force) {
    if (prime_elab_reserve(e, (void **)&e->lex_shared, &e->lex_shared_cap,
                           e->lex_shared_len + 1u, sizeof(*e->lex_shared)))
        e->lex_shared[e->lex_shared_len++] =
            (PrimeLexForce){.id = id, .in_force = in_force};
}

/* The store names a pattern holds, each once.  In a written quotation's
 * code a hole keeps its identity: no pattern there makes a slot for it. */
static void prime_lex_pattern_names(PrimeElab *e, Atom *pattern, Atom ***names,
                                    size_t *count, size_t *cap) {
    if (e->failed || !pattern || !atom_has_vars(pattern)) return;
    if (pattern->kind == ATOM_VAR) {
        if (prime_elab_is_binder_id(e, pattern->var_id) ||
            prime_lex_is_shared(e, pattern->var_id) ||
            (e->code_scopes > 0u &&
             prime_elab_hole_in_scope(e, pattern->var_id)))
            return;
        for (size_t i = 0u; i < *count; i++)
            if ((*names)[i]->var_id == pattern->var_id) return;
        if (prime_elab_reserve(e, (void **)names, cap, *count + 1u,
                               sizeof(**names)))
            (*names)[(*count)++] = pattern;
        return;
    }
    if (pattern->kind != ATOM_EXPR || !prime_elab_enter(e)) return;
    for (CettaExprIndex i = 0u; i < pattern->expr.len && !e->failed; i++)
        prime_lex_pattern_names(e, pattern->expr.elems[i], names, count, cap);
    e->nesting--;
}

static Atom *prime_lex_resolve(PrimeElab *e, Atom *term, bool pattern);

/* A pattern that makes slots: a fresh slot for each store name it holds, in
 * force from now (the caller pops it), recorded in the current template's
 * frame; the pattern is written with its slots. */
static Atom *prime_lex_introduce(PrimeElab *e, Atom *pattern) {
    Atom **names = NULL;
    size_t count = 0u, cap = 0u;
    prime_lex_pattern_names(e, pattern, &names, &count, &cap);
    for (size_t i = 0u; i < count && !e->failed; i++) {
        Atom *slot = prime_elab_split_var(e->arena, names[i]);
        if (!slot ||
            !prime_elab_reserve(e, (void **)&e->lex, &e->lex_cap,
                                e->lex_len + 1u, sizeof(*e->lex)) ||
            !prime_elab_reserve(e, (void **)&e->frame, &e->frame_cap,
                                e->frame_len + 1u, sizeof(*e->frame))) {
            e->failed = true;
            break;
        }
        e->lex[e->lex_len++] =
            (PrimeElabTwin){.from = names[i]->var_id, .to = slot};
        e->frame[e->frame_len++] = slot;
    }
    free(names);
    return e->failed ? pattern : prime_lex_resolve(e, pattern, true);
}


static Atom *prime_lex_code(PrimeElab *e, Atom *term) {
    if (e->failed || !term || !atom_has_vars(term)) return term;
    if (term->kind == ATOM_VAR)
        return prime_elab_hole_in_scope(e, term->var_id)
            ? prime_lex_lookup(e, term) : term;
    if (term->kind != ATOM_EXPR || !prime_elab_enter(e)) return term;
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len && !e->failed; i++)
        items = prime_elab_items_for(e, term, items, i,
                                     prime_lex_code(e, term->expr.elems[i]));
    e->nesting--;
    return prime_elab_rebuilt(e, term, items);
}

/* A template body in a frame of its own.  The slots its patterns make
 * directly in it are made at each of its calls, under either lifetime.  With
 * a crossing set written, it also owns every name it uses outside the set
 * (the collect walk's scope node: the names written directly in it and
 * those its templates' crossing sets share with it): a slot for each, in
 * force in its body, which per-closure moves up to its creator; the set's
 * names are in force inside, and its own names are not. */
static Atom *prime_lex_template(PrimeElab *e, Atom *body,
                                const PrimeElabLists *lists,
                                Atom **own_out, bool always) {
    uint32_t scope = ++e->next_scope;
    if (scope >= e->scopes_len) {
        e->failed = true;
        return body;
    }
    size_t frame_mark = e->frame_len, lex_mark = e->lex_len;
    size_t shared_mark = e->lex_shared_len, hoist_mark = e->hoist_len;
    Atom *shared = lists ? lists->shared : NULL;
    Atom **owned = NULL;
    size_t owned_len = 0u, owned_cap = 0u;
    if (shared) {
        (void)prime_lex_force(e, shared, true);
        const PrimeScopeNode *node = &e->scopes[scope];
        for (size_t i = 0u; i < node->direct_len + node->passed_len &&
                            !e->failed; i++) {
            VarId id = i < node->direct_len
                ? node->direct[i] : node->passed[i - node->direct_len];
            if (prime_elab_list_has(shared, id)) continue;
            bool seen = false;
            for (size_t k = 0u; k < owned_len && !seen; k++)
                seen = e->lex[lex_mark + k].from == id;
            if (seen) continue;
            PrimeVarEntry *spelled = prime_var_map_find(&e->spellings, id, 0u);
            Atom *slot = spelled && spelled->atom
                ? prime_elab_split_var(e->arena, spelled->atom)
                : NULL;
            if (!slot ||
                !prime_elab_reserve(e, (void **)&owned, &owned_cap,
                                    owned_len + 1u, sizeof(*owned)) ||
                !prime_elab_reserve(e, (void **)&e->lex, &e->lex_cap,
                                    e->lex_len + 1u, sizeof(*e->lex))) {
                e->failed = true;
                break;
            }
            e->lex[e->lex_len++] = (PrimeElabTwin){.from = id, .to = slot};
            owned[owned_len++] = slot;
            prime_lex_force_id(e, id, false);
        }
    }
    Atom *resolved = e->failed ? body : prime_lex_resolve(e, body, false);
    size_t count = e->frame_len - frame_mark;
    if (!e->failed && e->profile.lifetime == CETTA_PRIME_LIFETIME_PER_CLOSURE) {
        for (size_t i = 0u; i < count && !e->failed; i++)
            if (prime_elab_reserve(e, (void **)&e->hoist, &e->hoist_cap,
                                   e->hoist_len + 1u, sizeof(*e->hoist)))
                e->hoist[e->hoist_len++] = e->frame[frame_mark + i];
        *own_out = e->failed ? NULL
                             : prime_elab_own_list(e, e->hoist + hoist_mark,
                                                   e->hoist_len - hoist_mark,
                                                   always);
        e->hoist_len = hoist_mark;
        for (size_t i = 0u; i < owned_len && !e->failed; i++)
            if (prime_elab_reserve(e, (void **)&e->hoist, &e->hoist_cap,
                                   e->hoist_len + 1u, sizeof(*e->hoist)))
                e->hoist[e->hoist_len++] = owned[i];
    } else if (!e->failed) {
        for (size_t i = 0u; i < count && !e->failed; i++)
            if (prime_elab_reserve(e, (void **)&owned, &owned_cap,
                                   owned_len + 1u, sizeof(*owned)))
                owned[owned_len++] = e->frame[frame_mark + i];
        *own_out = e->failed ? NULL
                             : prime_elab_own_list(e, owned, owned_len,
                                                   always);
    }
    free(owned);
    e->frame_len = frame_mark;
    e->lex_len = lex_mark;
    e->lex_shared_len = shared_mark;
    return resolved;
}

/* A written quotation under lexical-fresh, a scope as a template is (stage
 * 5, item 3): the slots its code's patterns make directly in it (and, with
 * a crossing set, every name it uses outside the set) are its own, in its
 * own list, and each opening copies them.  Its holes keep their identity. */
static Atom *prime_lex_quote(PrimeElab *e, Atom *term) {
    if (term->expr.len != 2u) return term;
    PrimeElabLists lists = {.plain = term,
                            .shared = e->pending_quote_crossing};
    e->pending_quote_crossing = NULL;
    Atom *own = NULL;
    e->code_scopes++;
    Atom *payload = prime_lex_template(e, term->expr.elems[1], &lists, &own,
                                       false);
    e->code_scopes--;
    if (e->failed) return term;
    if (payload == term->expr.elems[1] && !own) return term;
    Atom *result = own
        ? atom_expr3(e->arena, term->expr.elems[0], payload, own)
        : atom_expr2(e->arena, term->expr.elems[0], payload);
    if (!result) e->failed = true;
    return result ? result : term;
}

/* Every other form: names refer to the slots in force; a binder's holes
 * are in scope in the part its patterns scope over. */
static Atom *prime_lex_resolve_children(PrimeElab *e, Atom *term,
                                        bool pattern) {
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len && !e->failed; i++) {
        CettaExprIndex covers[2];
        size_t count = pattern ? 0u : prime_elab_binder_cover(term, i, covers);
        size_t holes_mark = e->holes_len;
        for (size_t k = 0u; k < count; k++)
            prime_elab_push_holes(e, term->expr.elems[covers[k]], false);
        PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                      : prime_child_role(term, i);
        items = prime_elab_items_for(
            e, term, items, i,
            prime_lex_resolve(e, term->expr.elems[i],
                              role != PRIME_CHILD_VALUE));
        e->holes_len = holes_mark;
    }
    return prime_elab_rebuilt(e, term, items);
}

static Atom *prime_lex_resolve(PrimeElab *e, Atom *term, bool pattern) {
    if (e->failed || !term || !atom_has_vars(term)) return term;
    if (term->kind == ATOM_VAR) return prime_lex_lookup(e, term);
    if (term->kind != ATOM_EXPR) return term;
    if (atom_is_drop_of_quoted_variable(term)) {
        Atom *var = term->expr.elems[1]->expr.elems[1];
        Atom *image = prime_lex_lookup(e, var);
        if (image == var) return term;
        Atom *quoted = atom_expr2(e->arena, term->expr.elems[1]->expr.elems[0],
                                  image);
        Atom *drop = quoted ? atom_expr2(e->arena, term->expr.elems[0], quoted)
                            : NULL;
        if (!drop) e->failed = true;
        return drop ? drop : term;
    }
    if (!prime_elab_enter(e)) return term;
    Atom *result = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    PrimeElabLists lists;
    SymbolId head = term->expr.len > 0u && term->expr.elems[0] &&
                    term->expr.elems[0]->kind == ATOM_SYMBOL
        ? term->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    CettaExprLen len = term->expr.len;
    size_t lex_mark = e->lex_len;
    size_t holes_mark = e->holes_len;
    /* A crossing set a wrapper puts on this pattern binder: its names are
     * in force in each pattern and the part of the form it scopes over, so
     * no pattern there makes a slot for them.  Step 1 made the pattern's
     * other names fresh. */
    Atom *crossing = e->pending_crossing;
    e->pending_crossing = NULL;
    Atom *meta_inner, *meta_braces;
    if (!pattern && prime_meta_braces(term, &meta_inner, &meta_braces) &&
        prime_braces_all_vars(meta_braces) &&
        prime_meta_kind(e->arena, meta_inner) == PRIME_META_QUOTE) {
        Atom *braces = prime_lex_resolve(e, meta_braces, false);
        size_t mark = prime_elab_push_crossing_holes(e, meta_braces);
        Atom *crossing = atom_expr(e->arena, meta_braces->expr.elems + 1u,
                                   meta_braces->expr.len - 1u);
        if (!crossing) e->failed = true;
        e->pending_quote_crossing = crossing;
        Atom *quote = prime_lex_resolve(e, meta_inner, false);
        e->pending_quote_crossing = NULL;
        e->holes_len = mark;
        result = prime_elab_rebuild_meta(e, term, quote, braces);
    } else if (!pattern &&
               prime_meta_braces(term, &meta_inner, &meta_braces) &&
               prime_meta_kind(e->arena, meta_inner) == PRIME_META_BINDER) {
        Atom *braces = prime_lex_resolve(e, meta_braces, false);
        e->pending_crossing = prime_elab_crossing(e, meta_braces);
        Atom *inner = prime_lex_resolve(e, meta_inner, false);
        e->pending_crossing = NULL;
        result = prime_elab_rebuild_meta(e, term, inner, braces);
    } else if (pattern) {
        result = prime_lex_resolve_children(e, term, true);
    } else if (atom_is_quotation(term)) {
        result = prime_lex_quote(e, term);
    } else if (prime_code_like(term)) {
        /* Contextual code as written: its holes in scope only. */
        Atom *payload = prime_lex_code(e, term->expr.elems[1]);
        if (payload != term->expr.elems[1]) {
            result = prime_code_with_payload(e->arena, term, payload);
            if (!result) {
                e->failed = true;
                result = term;
            }
        }
    } else if (prime_elab_lambda(e, term, &telescope, &lists) &&
               !telescope.own) {
        Atom *plain = lists.plain;
        Atom **groups = arena_alloc(e->arena, sizeof(Atom *) * telescope.count);
        if (!groups) {
            e->failed = true;
            goto done;
        }
        for (size_t g = 0u; g < telescope.count && !e->failed; g++) {
            CettaPrimeLambdaBinderGroupV1 group = telescope.groups[g];
            groups[g] = group.syntax;
            for (size_t t = 0u; group.typed && t < group.types_count; t++) {
                size_t position = group.types_start + t;
                Atom *type = group.syntax->expr.elems[position];
                Atom *next = prime_lex_resolve(e, type, false);
                if (next == type) continue;
                group.syntax = prime_group_with(e->arena, &group, position,
                                                next);
                if (!group.syntax) {
                    e->failed = true;
                    break;
                }
                groups[g] = group.syntax;
            }
        }
        Atom *crossing_node = lists.shared_node
            ? prime_lex_resolve(e, lists.shared_node, false) : NULL;
        Atom *own = NULL;
        Atom *body = prime_lex_template(e, plain->expr.elems[2], &lists, &own,
                                        false);
        if (e->failed) goto done;
        result = prime_lambda_rebuild(e->arena, plain->expr.elems[0],
                                      telescope.listed, groups,
                                      telescope.count, body, own);
        if (!result) {
            e->failed = true;
            result = term;
        } else {
            result = prime_elab_rewrap(e, result, crossing_node);
        }
    } else if (!e->failed && prime_elab_iter(e, term, &iter, &lists) &&
               !iter.own) {
        CettaExprLen plain = lists.plain->expr.len;
        Atom **items = arena_alloc(e->arena, sizeof(Atom *) * (size_t)plain);
        if (!items) {
            e->failed = true;
            goto done;
        }
        for (CettaExprIndex i = 0u; i < plain; i++)
            items[i] = lists.plain->expr.elems[i];
        for (CettaExprIndex i = 1u; i < iter.first_param; i++)
            items[i] = prime_lex_resolve(e, items[i], false);
        Atom *crossing_node = lists.shared_node
            ? prime_lex_resolve(e, lists.shared_node, false) : NULL;
        Atom *own = NULL;
        items[iter.body] = prime_lex_template(e, items[iter.body], &lists,
                                              &own, true);
        if (e->failed) goto done;
        Atom *rebuilt = prime_elab_rewrap(
            e, prime_elab_iter_rebuild(e, items, plain, own), crossing_node);
        if (rebuilt) result = rebuilt;
    } else if (!e->failed && prime_elaborated_template(e->arena, term)) {
        /* A template elaborated before, when it was formed, keeps its
         * binding structure as it is: none of its patterns makes a slot
         * again, and it captures what it captured. */
        result = term;
    } else if ((head == g_builtin_syms.let && len == 4u) ||
               ((head == g_builtin_syms.match ||
                 head == g_builtin_syms.chain ||
                 head == g_builtin_syms.filter_atom) && len == 4u)) {
        /* (let P V B), (match S P T), (chain E $v B), (filter-atom L $v P):
         * the source is read in the scope around; the pattern makes slots
         * for the part it scopes over. */
        CettaExprIndex pattern_at = head == g_builtin_syms.let ? 1u : 2u;
        CettaExprIndex source_at = head == g_builtin_syms.let ? 2u : 1u;
        Atom *items[4] = {term->expr.elems[0], NULL, NULL, NULL};
        items[source_at] = prime_lex_resolve(e, term->expr.elems[source_at],
                                             false);
        size_t force_mark = prime_lex_force(e, crossing, true);
        items[pattern_at] = prime_lex_introduce(e, term->expr.elems[pattern_at]);
        prime_elab_push_holes(e, term->expr.elems[pattern_at], false);
        items[3] = prime_lex_resolve(e, term->expr.elems[3], false);
        e->lex_shared_len = force_mark;
        if (!e->failed) {
            result = atom_expr(e->arena, items, 4u);
            if (!result) {
                e->failed = true;
                result = term;
            }
        }
    } else if (prime_elab_is_let_star(term) &&
               term->expr.elems[1]->kind == ATOM_EXPR) {
        Atom *bindings = term->expr.elems[1];
        Atom **pairs = arena_alloc(
            e->arena, sizeof(Atom *) * ((size_t)bindings->expr.len + 1u));
        if (!pairs) {
            e->failed = true;
            goto done;
        }
        /* The first source is read around the let*, what follows it with
         * the crossing names in force. */
        size_t force_mark = e->lex_shared_len;
        for (CettaExprIndex i = 0u; i < bindings->expr.len && !e->failed; i++) {
            Atom *pair = bindings->expr.elems[i];
            if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
                Atom *source = prime_lex_resolve(e, pair->expr.elems[1], false);
                if (i == 0u) (void)prime_lex_force(e, crossing, true);
                Atom *bound = prime_lex_introduce(e, pair->expr.elems[0]);
                prime_elab_push_holes(e, pair->expr.elems[0], false);
                pairs[i] = atom_expr2(e->arena, bound, source);
            } else {
                pairs[i] = prime_lex_resolve(e, pair, false);
                if (i == 0u) (void)prime_lex_force(e, crossing, true);
            }
        }
        Atom *body = prime_lex_resolve(e, term->expr.elems[2], false);
        e->lex_shared_len = force_mark;
        Atom *list = e->failed ? NULL
                               : atom_expr(e->arena, pairs, bindings->expr.len);
        if (list) result = atom_expr3(e->arena, term->expr.elems[0], list, body);
        if (!e->failed && !result) e->failed = true;
        if (!result) result = term;
    } else if ((head == g_builtin_syms.case_text ||
                head == g_builtin_syms.switch_text ||
                head == g_builtin_syms.switch_minimal) &&
               (len == 3u || len == 4u) &&
               term->expr.elems[2]->kind == ATOM_EXPR) {
        Atom *branches = term->expr.elems[2];
        Atom **resolved = arena_alloc(
            e->arena, sizeof(Atom *) * ((size_t)branches->expr.len + 1u));
        Atom **items = arena_alloc(e->arena, sizeof(Atom *) * (size_t)len);
        if (!resolved || !items) {
            e->failed = true;
            goto done;
        }
        items[0] = term->expr.elems[0];
        items[1] = prime_lex_resolve(e, term->expr.elems[1], false);
        for (CettaExprIndex i = 0u; i < branches->expr.len && !e->failed; i++) {
            Atom *branch = branches->expr.elems[i];
            if (branch && branch->kind == ATOM_EXPR && branch->expr.len == 2u) {
                size_t mark = e->lex_len, branch_holes = e->holes_len;
                size_t force_mark = prime_lex_force(e, crossing, true);
                Atom *bound = prime_lex_introduce(e, branch->expr.elems[0]);
                prime_elab_push_holes(e, branch->expr.elems[0], false);
                Atom *body = prime_lex_resolve(e, branch->expr.elems[1], false);
                e->lex_len = mark;
                e->holes_len = branch_holes;
                e->lex_shared_len = force_mark;
                resolved[i] = atom_expr2(e->arena, bound, body);
            } else {
                resolved[i] = prime_lex_resolve(e, branch, false);
            }
        }
        items[2] = atom_expr(e->arena, resolved, branches->expr.len);
        if (len == 4u) items[3] = prime_lex_resolve(e, term->expr.elems[3],
                                                    false);
        if (!e->failed) {
            result = atom_expr(e->arena, items, len);
            if (!result) {
                e->failed = true;
                result = term;
            }
        }
    } else {
        /* unify, an equation, and every other form: names refer to the
         * slots in force; an equation head's names are the clause's. */
        result = prime_lex_resolve_children(e, term, false);
    }
done:
    e->lex_len = lex_mark;
    e->holes_len = holes_mark;
    e->nesting--;
    return result;
}

/* ── Step 3: the seal ──────────────────────────────────────────────── */

/* The least binding depth of each variable: the quote depth of a pattern
 * that binds it, or of any mention inside code, since code may bind it with
 * its own binders when it runs (a let built by `lift app` from quoted
 * pieces, say).  Then each mention inside a quotation in value position, at
 * a depth above that, becomes the quotation's own variable for that depth,
 * unless it is a hole in scope.  A variable that only stands as a value
 * outside code keeps its identity under a quotation: the environment action
 * never enters the quotation, and code that shares the variable still
 * shares it. */
static void prime_elab_note_depth(PrimeElab *e, VarId id, uint32_t depth) {
    bool made = false;
    PrimeVarEntry *entry = prime_var_map_get(&e->depths, id, 0u, &made);
    if (!entry) {
        e->failed = true;
        return;
    }
    if (made || depth < entry->number) entry->number = depth;
}

static uint32_t prime_elab_least_depth(const PrimeElab *e, VarId id) {
    const PrimeVarEntry *entry = prime_var_map_find(&e->depths, id, 0u);
    return entry ? entry->number : UINT32_MAX;
}

/* `(lift let K A C)` with the code C written in place: the key K names
 * slots inside that code, so its variables are read at the code's depth,
 * not where K is written.  When C is not written there (a name holding
 * code, a computation of code, held syntax), K is read where it stands: a
 * key the program computed, such as a binder a match took out, by
 * identity, never a spelling of a slot in code written elsewhere. */
static bool prime_elab_lift_let(const Atom *term) {
    return term && term->kind == ATOM_EXPR && term->expr.len == 5u &&
           is_symbol_named(term->expr.elems[0], "lift") &&
           term->expr.elems[1] && term->expr.elems[1]->kind == ATOM_SYMBOL &&
           term->expr.elems[1]->sym_id == g_builtin_syms.let &&
           prime_code_like(term->expr.elems[4]);
}

static void prime_elab_seal_collect(PrimeElab *e, Atom *term, uint32_t depth,
                                    bool pattern);

static void prime_elab_seal_collect_pairs(PrimeElab *e, Atom *pairs,
                                          uint32_t depth) {
    if (!pairs || pairs->kind != ATOM_EXPR) {
        prime_elab_seal_collect(e, pairs, depth, false);
        return;
    }
    for (CettaExprIndex i = 0u; i < pairs->expr.len && !e->failed; i++) {
        Atom *pair = pairs->expr.elems[i];
        if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
            prime_elab_seal_collect(e, pair->expr.elems[0], depth, true);
            prime_elab_seal_collect(e, pair->expr.elems[1], depth, false);
        } else {
            prime_elab_seal_collect(e, pair, depth, false);
        }
    }
}

static void prime_elab_seal_collect(PrimeElab *e, Atom *term, uint32_t depth,
                                    bool pattern) {
    if (e->failed || !term || !atom_has_vars(term)) return;
    if (term->kind == ATOM_VAR) {
        if (pattern || depth > 0u)
            prime_elab_note_depth(e, term->var_id, depth);
        return;
    }
    if (term->kind != ATOM_EXPR) return;
    if (!pattern && atom_is_drop_of_quoted_variable(term)) {
        if (depth > 0u)
            prime_elab_note_depth(e, term->expr.elems[1]->expr.elems[1]->var_id,
                                  depth);
        return;
    }
    if (!prime_elab_enter(e)) return;
    if (!pattern && prime_code_like(term)) {
        prime_elab_seal_collect(e, term->expr.elems[1], depth + 1u, false);
    } else if (!pattern && prime_elab_lift_let(term)) {
        prime_elab_seal_collect(e, term->expr.elems[2], depth + 1u, false);
        for (CettaExprIndex i = 3u; i < term->expr.len && !e->failed; i++)
            prime_elab_seal_collect(e, term->expr.elems[i], depth, false);
    } else {
        for (CettaExprIndex i = 0u; i < term->expr.len && !e->failed; i++) {
            PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                          : prime_child_role(term, i);
            if (role == PRIME_CHILD_PATTERN_PAIRS)
                prime_elab_seal_collect_pairs(e, term->expr.elems[i], depth);
            else
                prime_elab_seal_collect(e, term->expr.elems[i], depth,
                                        role == PRIME_CHILD_PATTERN);
        }
    }
    e->nesting--;
}

static Atom *prime_elab_sealed_twin(PrimeElab *e, Atom *var, uint32_t depth) {
    bool made = false;
    PrimeVarEntry *entry = prime_var_map_get(&e->sealed, var->var_id, depth,
                                             &made);
    if (!entry) {
        e->failed = true;
        return var;
    }
    if (made) {
        entry->atom = prime_elab_split_var(e->arena, var);
        if (!entry->atom) {
            e->failed = true;
            return var;
        }
    }
    return entry->atom;
}

/* The binder forms whose patterns bind holes for a body: the indices of
 * the patterns and of the scope each pattern covers.  -1 when none. */
static Atom *prime_elab_seal_apply_scoped(PrimeElab *e, Atom *term,
                                          uint32_t depth, bool *handled);

static Atom *prime_elab_seal_apply(PrimeElab *e, Atom *term, uint32_t depth,
                                   bool pattern);

static Atom *prime_elab_seal_apply_pairs(PrimeElab *e, Atom *pairs,
                                         uint32_t depth) {
    if (!pairs || pairs->kind != ATOM_EXPR)
        return prime_elab_seal_apply(e, pairs, depth, false);
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < pairs->expr.len && !e->failed; i++) {
        Atom *pair = pairs->expr.elems[i];
        Atom *next;
        if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
            Atom *pattern = prime_elab_seal_apply(e, pair->expr.elems[0],
                                                  depth, true);
            Atom *branch = prime_elab_seal_apply(e, pair->expr.elems[1],
                                                 depth, false);
            next = pattern == pair->expr.elems[0] &&
                           branch == pair->expr.elems[1]
                ? pair : atom_expr2(e->arena, pattern, branch);
        } else {
            next = prime_elab_seal_apply(e, pair, depth, false);
        }
        if (!next) {
            e->failed = true;
            break;
        }
        if (next != pair && !items) {
            items = arena_alloc(e->arena,
                                sizeof(Atom *) * (size_t)pairs->expr.len);
            if (!items) {
                e->failed = true;
                break;
            }
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = pairs->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    return !e->failed && items ? atom_expr(e->arena, items, pairs->expr.len)
                               : pairs;
}

static Atom *prime_elab_seal_apply(PrimeElab *e, Atom *term, uint32_t depth,
                                   bool pattern) {
    if (e->failed || !term || !atom_has_vars(term)) return term;
    if (term->kind == ATOM_VAR) {
        if (depth == 0u) return term;
        /* In the code of a quotation with a crossing set written, the set's
         * names cross in and every other name is the quotation's own. */
        if (e->seal_crossing && depth == e->seal_crossing_depth)
            return prime_elab_list_has(e->seal_crossing, term->var_id)
                ? term : prime_elab_sealed_twin(e, term, depth);
        if (prime_elab_least_depth(e, term->var_id) >= depth ||
            prime_elab_hole_in_scope(e, term->var_id))
            return term;
        return prime_elab_sealed_twin(e, term, depth);
    }
    if (term->kind != ATOM_EXPR) return term;
    if (atom_is_drop_of_quoted_variable(term)) {
        Atom *var = term->expr.elems[1]->expr.elems[1];
        Atom *image = prime_elab_seal_apply(e, var, depth, pattern);
        if (image == var) return term;
        Atom *quoted = atom_expr2(e->arena, term->expr.elems[1]->expr.elems[0],
                                  image);
        return quoted ? atom_expr2(e->arena, term->expr.elems[0], quoted)
                      : NULL;
    }
    if (!prime_elab_enter(e)) return term;
    Atom *result = term;
    bool scoped_handled = false;
    Atom *meta_quote, *meta_braces;
    if (!pattern && prime_elab_meta_quote(term, &meta_quote, &meta_braces)) {
        /* A quotation's crossing names are holes in scope: never sealed.
         * Outside code the set is complete: every other name the quotation
         * holds at its own depth is its own (code inside code keeps the
         * rule of its depth). */
        Atom *braces = prime_elab_seal_apply(e, meta_braces, depth, false);
        size_t mark = prime_elab_push_crossing_holes(e, meta_braces);
        Atom *outer_crossing = e->seal_crossing;
        uint32_t outer_depth = e->seal_crossing_depth;
        if (depth == 0u) {
            e->seal_crossing = atom_expr(e->arena, meta_braces->expr.elems + 1u,
                                         meta_braces->expr.len - 1u);
            e->seal_crossing_depth = 1u;
            if (!e->seal_crossing) e->failed = true;
        }
        Atom *quote = prime_elab_seal_apply(e, meta_quote, depth, false);
        e->seal_crossing = outer_crossing;
        e->seal_crossing_depth = outer_depth;
        e->holes_len = mark;
        if (!braces || !quote)
            e->failed = true;
        else
            result = prime_elab_rebuild_meta(e, term, quote, braces);
    } else if (!pattern && prime_code_like(term)) {
        Atom *payload = prime_elab_seal_apply(e, term->expr.elems[1],
                                              depth + 1u, false);
        if (payload && payload != term->expr.elems[1])
            result = prime_code_with_payload(e->arena, term, payload);
    } else if (!pattern &&
               (result = prime_elab_seal_apply_scoped(e, term, depth,
                                                      &scoped_handled),
                scoped_handled)) {
        /* a binder with holes: walked with its holes in scope */
    } else if (!pattern && prime_elab_lift_let(term)) {
        Atom *items[5];
        bool changed = false;
        for (CettaExprIndex i = 0u; i < 5u; i++) {
            Atom *child = term->expr.elems[i];
            items[i] = i == 2u ? prime_elab_seal_apply(e, child, depth + 1u,
                                                       false)
                       : i >= 3u ? prime_elab_seal_apply(e, child, depth,
                                                         false)
                                 : child;
            if (!items[i]) {
                e->failed = true;
                break;
            }
            changed = changed || items[i] != child;
        }
        if (!e->failed && changed) result = atom_expr(e->arena, items, 5u);
    } else {
        Atom **items = NULL;
        for (CettaExprIndex i = 0u; i < term->expr.len && !e->failed; i++) {
            Atom *child = term->expr.elems[i];
            PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                          : prime_child_role(term, i);
            Atom *next = role == PRIME_CHILD_PATTERN_PAIRS
                ? prime_elab_seal_apply_pairs(e, child, depth)
                : prime_elab_seal_apply(e, child, depth,
                                        role == PRIME_CHILD_PATTERN);
            if (!next) {
                e->failed = true;
                break;
            }
            if (next != child && !items) {
                items = arena_alloc(e->arena,
                                    sizeof(Atom *) * (size_t)term->expr.len);
                if (!items) {
                    e->failed = true;
                    break;
                }
                for (CettaExprIndex j = 0u; j < i; j++)
                    items[j] = term->expr.elems[j];
            }
            if (items) items[i] = next;
        }
        if (!e->failed && items)
            result = atom_expr(e->arena, items, term->expr.len);
    }
    e->nesting--;
    return result;
}

/* A binder whose patterns hold quotations: each pattern binds its holes for
 * the part of the form it scopes over (let: the body; let*: what follows;
 * case and switch: the branch; unify: the then branch; match: the
 * template; an equation: its body).  *handled is false for other forms. */
static Atom *prime_elab_seal_apply_scoped(PrimeElab *e, Atom *term,
                                          uint32_t depth, bool *handled) {
    *handled = false;
    if (term->kind != ATOM_EXPR || term->expr.len == 0u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL)
        return term;
    SymbolId head = term->expr.elems[0]->sym_id;
    CettaExprLen len = term->expr.len;
    /* pattern indices and the index each pattern scopes over */
    CettaExprIndex patterns[2] = {0u, 0u};
    size_t pattern_count = 0u;
    CettaExprIndex scope_at = 0u;
    if (head == g_builtin_syms.let && len == 4u) {
        patterns[pattern_count++] = 1u;
        scope_at = 3u;
    } else if (head == g_builtin_syms.match && len == 4u) {
        patterns[pattern_count++] = 2u;
        scope_at = 3u;
    } else if (head == g_builtin_syms.unify && len == 5u) {
        patterns[pattern_count++] = 1u;
        patterns[pattern_count++] = 2u;
        scope_at = 3u;
    } else if (head == g_builtin_syms.equals && len == 3u) {
        patterns[pattern_count++] = 1u;
        scope_at = 2u;
    } else if (!((head == g_builtin_syms.case_text ||
                  head == g_builtin_syms.switch_text ||
                  head == g_builtin_syms.switch_minimal) &&
                 (len == 3u || len == 4u)) &&
               !(head == g_builtin_syms.let_star && len == 3u)) {
        return term;
    }
    *handled = true;
    Atom **items = arena_alloc(e->arena, sizeof(Atom *) * (size_t)len);
    if (!items) {
        e->failed = true;
        return term;
    }
    for (CettaExprIndex i = 0u; i < len; i++) items[i] = term->expr.elems[i];
    size_t mark = e->holes_len;
    if (pattern_count > 0u) {
        for (CettaExprIndex i = 1u; i < len && !e->failed; i++) {
            bool is_pattern = i == patterns[0] ||
                              (pattern_count > 1u && i == patterns[1]);
            if (i == scope_at) {
                for (size_t k = 0u; k < pattern_count; k++)
                    prime_elab_push_holes(e, term->expr.elems[patterns[k]],
                                          false);
                items[i] = prime_elab_seal_apply(e, items[i], depth, false);
                e->holes_len = mark;
            } else {
                items[i] = prime_elab_seal_apply(e, items[i], depth,
                                                 is_pattern);
            }
        }
    } else if (head == g_builtin_syms.let_star) {
        Atom *bindings = term->expr.elems[1];
        Atom **pairs = bindings->kind == ATOM_EXPR
            ? arena_alloc(e->arena,
                          sizeof(Atom *) * ((size_t)bindings->expr.len + 1u))
            : NULL;
        if (bindings->kind == ATOM_EXPR && !pairs) {
            e->failed = true;
            return term;
        }
        for (CettaExprIndex i = 0u; bindings->kind == ATOM_EXPR &&
                                    i < bindings->expr.len && !e->failed; i++) {
            Atom *pair = bindings->expr.elems[i];
            if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
                Atom *source = prime_elab_seal_apply(e, pair->expr.elems[1],
                                                     depth, false);
                Atom *pattern = prime_elab_seal_apply(e, pair->expr.elems[0],
                                                      depth, true);
                prime_elab_push_holes(e, pair->expr.elems[0], false);
                pairs[i] = atom_expr2(e->arena, pattern, source);
            } else {
                pairs[i] = prime_elab_seal_apply(e, pair, depth, false);
            }
        }
        if (bindings->kind == ATOM_EXPR)
            items[1] = atom_expr(e->arena, pairs, bindings->expr.len);
        else
            items[1] = prime_elab_seal_apply(e, bindings, depth, false);
        items[2] = prime_elab_seal_apply(e, items[2], depth, false);
        e->holes_len = mark;
    } else {
        /* case, switch, switch-minimal: (head scrutinee ((P B) ...) [rest]) */
        items[1] = prime_elab_seal_apply(e, items[1], depth, false);
        Atom *branches = term->expr.elems[2];
        if (branches->kind == ATOM_EXPR) {
            Atom **out = arena_alloc(
                e->arena, sizeof(Atom *) * ((size_t)branches->expr.len + 1u));
            if (!out) {
                e->failed = true;
                return term;
            }
            for (CettaExprIndex i = 0u; i < branches->expr.len && !e->failed;
                 i++) {
                Atom *branch = branches->expr.elems[i];
                if (branch && branch->kind == ATOM_EXPR &&
                    branch->expr.len == 2u) {
                    Atom *pattern = prime_elab_seal_apply(
                        e, branch->expr.elems[0], depth, true);
                    size_t branch_mark = e->holes_len;
                    prime_elab_push_holes(e, branch->expr.elems[0], false);
                    Atom *body = prime_elab_seal_apply(
                        e, branch->expr.elems[1], depth, false);
                    e->holes_len = branch_mark;
                    out[i] = atom_expr2(e->arena, pattern, body);
                } else {
                    out[i] = prime_elab_seal_apply(e, branch, depth, false);
                }
            }
            items[2] = atom_expr(e->arena, out, branches->expr.len);
        } else {
            items[2] = prime_elab_seal_apply(e, branches, depth, true);
        }
        for (CettaExprIndex i = 3u; i < len; i++)
            items[i] = prime_elab_seal_apply(e, items[i], depth, false);
    }
    if (e->failed) return term;
    bool changed = false;
    for (CettaExprIndex i = 0u; i < len; i++)
        changed = changed || items[i] != term->expr.elems[i];
    return changed ? atom_expr(e->arena, items, len) : term;
}


/* The binding structure of a whole authored form under `profile`: steps 1
 * to 3 above.  NULL when the form cannot be elaborated. */
static Atom *prime_elab_run(Arena *arena, Atom *form,
                            CettaPrimeScopeProfile profile) {
    if (!arena || !form) return NULL;
    if (!atom_has_vars(form)) return form;
    PrimeElab e = {
        .arena = arena,
        .profile = profile,
        .seal = prime_quote_seal_active(),
    };
    g_prime_elab_error[0] = '\0';
    e.record = prime_scope_record(arena, profile);
    if (!e.record) return NULL;
    Atom *result = prime_elab_binders(&e, form, false);
    /* The seal before ownership: a quoted mention keeps the identity the
     * seal gives it, and ownership then rewrites occurrences only. */
    if (!e.failed && e.seal) {
        prime_elab_seal_collect(&e, result, 0u, false);
        if (!e.failed)
            result = prime_elab_seal_apply(&e, result, 0u, false);
    }
    e.holes_len = 0u;
    /* The collect walk: the template scopes, the names written directly in
     * each, the crossing sets each shares with the scope around. */
    if (!e.failed &&
        prime_elab_reserve(&e, (void **)&e.scopes, &e.scopes_cap, 1u,
                           sizeof(*e.scopes))) {
        e.scopes[0] = (PrimeScopeNode){.parent = -1};
        e.scopes_len = 1u;
        (void)prime_scope_walk(&e, result, false, PRIME_SCOPE_COLLECT);
        e.holes_len = 0u;
    }
    if (!e.failed &&
        profile.ownership == CETTA_PRIME_OWNERSHIP_LEXICAL_FRESH) {
        e.next_scope = 0u;
        result = prime_lex_resolve(&e, result, false);
        if (!e.failed && (size_t)e.next_scope + 1u != e.scopes_len)
            e.failed = true;
    } else if (!e.failed) {
        if (!e.failed) prime_scope_compute(&e);
        if (!e.failed) {
            e.holes_len = 0u;
            e.next_scope = 0u;
            e.hoist_len = 0u;
            result = prime_scope_walk(&e, result, false, PRIME_SCOPE_APPLY);
            if (!e.failed && (size_t)e.next_scope + 1u != e.scopes_len)
                e.failed = true;
        }
    }
    bool failed = e.failed;
    prime_elab_free(&e);
    return failed ? NULL : result;
}

/* A form formed while the program runs, from text (parse), is elaborated as
 * the reader elaborates a form, under the profile in force
 * (prime_scope_runtime_profile). */
Atom *prime_semantics_elaborate_form(Arena *arena, Atom *form) {
    return prime_elab_run(arena, form, prime_scope_runtime_profile());
}

/* The seal of a term formed at run time (step 3 alone): a mention inside a
 * quotation in value position, of a variable a pattern of the term binds at
 * a lower quote depth, becomes the quotation's own variable, exactly as in
 * a form the reader elaborated.  NULL on failure. */
static Atom *prime_seal_formed(Arena *arena, Atom *term) {
    if (!arena || !term || !atom_has_vars(term) ||
        !prime_quote_seal_active())
        return term;
    PrimeElab e = {.arena = arena, .seal = true};
    prime_elab_seal_collect(&e, term, 0u, false);
    Atom *result = e.failed ? NULL : prime_elab_seal_apply(&e, term, 0u, false);
    bool failed = e.failed;
    prime_elab_free(&e);
    return failed ? NULL : result;
}

/* ── Terms formed while the program runs ──────────────────────────────
 *
 * Names carry identities, never spellings, and a name's identity is fixed
 * where the code is written.  A term formed while the program runs, by
 * cons-atom or union-atom, by a substitution that puts `lam` or a binder at
 * the head of an expression, by `lift`, or by opening code with `*`, gets
 * the binding structure a written form gets except ownership
 * (prime_elab_formed): its lambdas' parameters, a `new`'s names and a
 * template's parameters become binders with identities of their own, and a
 * binder formed so is sealed as a written one is.  Which scope a store name
 * belongs to was decided where it was written, and forming a term never
 * decides it again (stage 5, item 5):
 *   - a name written outside a quotation belongs to the scope it was written
 *     in, so a lambda formed from parts captures it, whether it is bound
 *     before the lambda is formed or after;
 *   - a name a written lambda or template owns stays its own: the template
 *     carries its own list, and is never elaborated again;
 *   - a name written only inside a written quotation is the quotation's own
 *     (or a template's inside it), recorded in the quotation, and each
 *     opening copies it (prime_semantics_open_code, prime_semantics_lift_let).
 * A formed lambda or template carries its record, an own list with no
 * slots, and so does a value's lambda that enters a formed term or code
 * holding a store name free (prime_value_into_code).  `parse` reads text,
 * which is code written there: it elaborates the form it reads in full
 * (prime_semantics_elaborate_form). */

/* The profile record a value's template is given when it carries no own
 * list: the profile in force, with the reference readout, since a template
 * elaborated under the snapshot readout always carries its record
 * (prime_elab_own_list). */
static Atom *prime_value_record(Arena *arena) {
    CettaPrimeScopeProfile profile = prime_scope_runtime_profile();
    profile.readout = CETTA_PRIME_READOUT_REFERENCE;
    return prime_scope_record(arena, profile);
}

typedef struct {
    Arena *arena;
    Atom *record;
    VarId *hidden;
    size_t hidden_len, hidden_cap;
    uint32_t nesting;
    bool failed;
} PrimeValueMarks;

static void prime_value_marks_hide(PrimeValueMarks *m, const Atom *key) {
    if (!key || key->kind != ATOM_VAR) return;
    if (!prime_env_reserve((void **)&m->hidden, &m->hidden_cap,
                           m->hidden_len + 1u, sizeof(*m->hidden))) {
        m->failed = true;
        return;
    }
    m->hidden[m->hidden_len++] = key->var_id;
}

/* Whether the lambda `term`, read under the binders hidden in `m`, holds a
 * store name free: a name neither it nor an enclosing binder binds. */
static bool prime_value_holds_store_name(PrimeValueMarks *m, Atom *term) {
    PrimeFreeNames f = {.arena = m->arena};
    for (size_t i = 0u; i < m->hidden_len && !f.failed; i++) {
        if (!prime_env_reserve((void **)&f.hidden, &f.hidden_cap,
                               f.hidden_len + 1u, sizeof(*f.hidden))) {
            f.failed = true;
            break;
        }
        f.hidden[f.hidden_len++] = m->hidden[i];
    }
    if (!f.failed) prime_free_names_walk(&f, term, false);
    bool holds = f.failed || f.found_len > 0u;
    if (f.failed) m->failed = true;
    free(f.found);
    free(f.hidden);
    return holds;
}

static Atom *prime_value_marks_walk(PrimeValueMarks *m, Atom *term,
                                    bool mark_here);

/* The lambda `inner` (a value), with its record when it carries none and
 * holds a store name; lambdas inside it first. */
static Atom *prime_value_marks_lambda(PrimeValueMarks *m, Atom *inner,
                                      const PrimeLambdaTelescope *telescope,
                                      bool mark_here) {
    size_t mark = m->hidden_len;
    for (size_t g = 0u; g < telescope->count; g++)
        for (size_t i = 0u; i < telescope->groups[g].names_count; i++)
            prime_value_marks_hide(m, prime_binder_key(
                cetta_prime_lambda_binder_name_v1(&telescope->groups[g], i)));
    for (CettaExprIndex j = PRIME_OWN_FIRST;
         telescope->own && j < telescope->own->expr.len; j++)
        prime_value_marks_hide(m, telescope->own->expr.elems[j]);
    Atom *body = prime_value_marks_walk(m, inner->expr.elems[2], true);
    m->hidden_len = mark;
    if (m->failed || !body) return inner;
    Atom *own = telescope->own;
    if (mark_here && !own) {
        Atom *probe = body == inner->expr.elems[2]
            ? inner : atom_expr3(m->arena, inner->expr.elems[0],
                                 inner->expr.elems[1], body);
        if (probe && prime_value_holds_store_name(m, probe)) {
            Atom *items[2] = {
                atom_internal_tag(m->arena, CETTA_INTERNAL_TAG_PRIME_OWN),
                m->record};
            own = atom_expr(m->arena, items, 2u);
            if (!own) m->failed = true;
        }
    }
    if (m->failed) return inner;
    if (body == inner->expr.elems[2] && own == telescope->own) return inner;
    Atom *rebuilt = own
        ? atom_expr(m->arena, (Atom *[]){inner->expr.elems[0],
                                         inner->expr.elems[1], body, own}, 4u)
        : atom_expr3(m->arena, inner->expr.elems[0], inner->expr.elems[1],
                     body);
    if (!rebuilt) m->failed = true;
    return rebuilt ? rebuilt : inner;
}

/* The value `term` with each lambda in it that carries no own list and holds
 * a store name given its record (with `mark_here`, `term` itself too).
 * Code is left as it is, and so are pattern positions: a lambda written
 * there is a pattern. */
static Atom *prime_value_marks_walk(PrimeValueMarks *m, Atom *term,
                                    bool mark_here) {
    if (m->failed || !term || term->kind != ATOM_EXPR || !atom_has_vars(term) ||
        prime_code_like(term))
        return term;
    if (++m->nesting > PRIME_ELAB_NESTING_LIMIT) {
        m->failed = true;
        return term;
    }
    Atom *result = term;
    Atom *inner = term, *braces = NULL;
    if (!prime_meta_braces(term, &inner, &braces)) inner = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (prime_lambda_telescope(m->arena, inner, &telescope)) {
        Atom *marked = prime_value_marks_lambda(m, inner, &telescope,
                                                mark_here);
        if (marked != inner)
            result = braces ? atom_expr3(m->arena, term->expr.elems[0], marked,
                                         braces)
                            : marked;
    } else if (inner == term && prime_iter_template(term, &iter)) {
        Atom **items = arena_alloc(m->arena,
                                   sizeof(Atom *) * (size_t)term->expr.len);
        if (!items) {
            m->failed = true;
        } else {
            bool changed = false;
            for (CettaExprIndex i = 0u; i < term->expr.len; i++)
                items[i] = term->expr.elems[i];
            for (CettaExprIndex i = 1u; i < iter.first_param; i++) {
                items[i] = prime_value_marks_walk(m, items[i], true);
                changed = changed || items[i] != term->expr.elems[i];
            }
            size_t mark = m->hidden_len;
            for (CettaExprIndex p = 0u; p < iter.param_count; p++)
                prime_value_marks_hide(m, items[iter.first_param + p]);
            Atom *own = iter.own ? items[iter.own] : NULL;
            for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len;
                 j++)
                prime_value_marks_hide(m, own->expr.elems[j]);
            items[iter.body] = prime_value_marks_walk(m, items[iter.body],
                                                      true);
            changed = changed || items[iter.body] != term->expr.elems[iter.body];
            m->hidden_len = mark;
            if (!m->failed && changed) {
                result = atom_expr(m->arena, items, term->expr.len);
                if (!result) {
                    m->failed = true;
                    result = term;
                }
            }
        }
    } else if (prime_new_form(term)) {
        size_t mark = m->hidden_len;
        Atom *names = term->expr.elems[1];
        for (CettaExprIndex j = 0u; j < names->expr.len; j++)
            prime_value_marks_hide(m, names->expr.elems[j]);
        Atom *body = prime_value_marks_walk(m, term->expr.elems[2], true);
        m->hidden_len = mark;
        if (!m->failed && body != term->expr.elems[2]) {
            result = atom_expr3(m->arena, term->expr.elems[0], names, body);
            if (!result) {
                m->failed = true;
                result = term;
            }
        }
    } else {
        Atom **items = NULL;
        for (CettaExprIndex i = 0u; i < term->expr.len && !m->failed; i++) {
            Atom *child = term->expr.elems[i];
            PrimeChildRole role = prime_child_role(term, i);
            Atom *next = child;
            if (role == PRIME_CHILD_VALUE) {
                next = prime_value_marks_walk(m, child, true);
            } else if (role == PRIME_CHILD_PATTERN_PAIRS &&
                       child->kind == ATOM_EXPR) {
                Atom **pairs = NULL;
                for (CettaExprIndex k = 0u; k < child->expr.len && !m->failed;
                     k++) {
                    Atom *pair = child->expr.elems[k];
                    Atom *next_pair = pair;
                    if (pair && pair->kind == ATOM_EXPR &&
                        pair->expr.len == 2u) {
                        Atom *branch = prime_value_marks_walk(
                            m, pair->expr.elems[1], true);
                        if (branch != pair->expr.elems[1])
                            next_pair = atom_expr2(m->arena,
                                                   pair->expr.elems[0], branch);
                    }
                    if (!next_pair) {
                        m->failed = true;
                        break;
                    }
                    if (next_pair != pair && !pairs) {
                        pairs = arena_alloc(
                            m->arena, sizeof(Atom *) * (size_t)child->expr.len);
                        if (!pairs) {
                            m->failed = true;
                            break;
                        }
                        for (CettaExprIndex j = 0u; j < k; j++)
                            pairs[j] = child->expr.elems[j];
                    }
                    if (pairs) pairs[k] = next_pair;
                }
                if (pairs && !m->failed)
                    next = atom_expr(m->arena, pairs, child->expr.len);
            }
            if (!next) {
                m->failed = true;
                break;
            }
            if (next != child && !items) {
                items = arena_alloc(m->arena,
                                    sizeof(Atom *) * (size_t)term->expr.len);
                if (!items) {
                    m->failed = true;
                    break;
                }
                for (CettaExprIndex j = 0u; j < i; j++)
                    items[j] = term->expr.elems[j];
            }
            if (items) items[i] = next;
        }
        if (!m->failed && items) {
            result = atom_expr(m->arena, items, term->expr.len);
            if (!result) {
                m->failed = true;
                result = term;
            }
        }
    }
    m->nesting--;
    return m->failed ? term : result;
}

/* `value` with its lambdas' records (prime_value_marks_walk), the root
 * included when `mark_root`.  NULL on failure. */
static Atom *prime_value_marked(Arena *arena, Atom *value, bool mark_root) {
    if (!arena || !value || value->kind != ATOM_EXPR || !atom_has_vars(value))
        return value;
    PrimeValueMarks m = {.arena = arena, .record = prime_value_record(arena)};
    if (!m.record) return NULL;
    Atom *result = prime_value_marks_walk(&m, value, mark_root);
    free(m.hidden);
    return m.failed ? NULL : result;
}

/* A value entering code, as a hole's or a crossing name's filling, or as the
 * code of `lift`: its lambdas keep the binding structure they were formed
 * with.  Contextual code (a part taken out from under binders) enters as
 * its own syntax (prime_spliced_syntax). */
static Atom *prime_value_into_code(Arena *arena, Atom *value) {
    if (prime_semantics_contextual_code(value)) return value->expr.elems[1];
    return prime_value_marked(arena, value, true);
}

/* Whether `term` is a lambda or a map-atom/foldl-atom template not yet
 * elaborated, in its crossing set's wrapper or not, or a `new`. */
static bool prime_formed_template(Arena *arena, Atom *term) {
    if (prime_new_form(term)) return true;
    Atom *inner = term, *braces = NULL;
    if (!prime_meta_braces(term, &inner, &braces)) inner = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (prime_lambda_telescope(arena, inner, &telescope))
        return telescope.own == NULL;
    return prime_iter_template(inner, &iter) && iter.own == 0u;
}

static bool prime_formed_binder(const Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 2u) return false;
    for (CettaExprIndex i = 1u; i < term->expr.len; i++)
        if (prime_child_role(term, i) != PRIME_CHILD_VALUE) return true;
    return false;
}

/* Whether the formation pass can change the term `term`, formed at run
 * time: it has variables and holds a lambda, a template, a `new`, a
 * quotation, a drop or a crossing set (prime_elab_formed). */
static bool prime_formed_relevant(Atom *term) {
    if (!term || term->kind != ATOM_EXPR || !atom_has_vars(term)) return false;
    Atom **stack = NULL;
    size_t len = 0u, cap = 0u;
    bool relevant = false;
    if (!prime_env_reserve((void **)&stack, &cap, 1u, sizeof(*stack)))
        return true;
    stack[len++] = term;
    while (len > 0u && !relevant) {
        Atom *at = stack[--len];
        if (!at || at->kind != ATOM_EXPR || !atom_has_vars(at)) continue;
        SymbolId head = at->expr.len > 0u && at->expr.elems[0] &&
                        at->expr.elems[0]->kind == ATOM_SYMBOL
            ? at->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
        CettaExprLen arity = at->expr.len;
        if ((head == g_builtin_syms.quote && (arity == 2u || arity == 3u)) ||
            (head == g_builtin_syms.unquote && arity == 2u) ||
            (head == g_builtin_syms.prime_lam && arity >= 3u) ||
            (head == g_builtin_syms.map_atom && arity >= 4u) ||
            (head == g_builtin_syms.foldl_atom && arity >= 6u) ||
            (head == g_builtin_syms.prime_meta && arity == 3u) ||
            (head == g_builtin_syms.prime_new && arity == 3u)) {
            relevant = true;
            break;
        }
        if (!prime_env_reserve((void **)&stack, &cap, len + arity,
                               sizeof(*stack))) {
            relevant = true;
            break;
        }
        for (CettaExprIndex i = 0u; i < arity; i++)
            stack[len++] = at->expr.elems[i];
    }
    free(stack);
    return relevant;
}

/* The formed root `term`, a lambda or template (in its crossing set's
 * wrapper or not) that carries no own list, with its record: an own list
 * with no slots, under the profile the pass runs under.  It owns nothing,
 * and no later pass decides otherwise. */
static Atom *prime_formed_root_record(PrimeElab *e, Atom *term) {
    Atom *inner = term, *braces = NULL;
    if (!prime_meta_braces(term, &inner, &braces)) inner = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    Atom *rebuilt = NULL;
    if (prime_lambda_telescope(e->arena, inner, &telescope)) {
        if (telescope.own) return term;
        Atom *own = prime_elab_own_list(e, NULL, 0u, true);
        if (!own) return term;
        Atom *items[4] = {inner->expr.elems[0], inner->expr.elems[1],
                          inner->expr.elems[2], own};
        rebuilt = atom_expr(e->arena, items, 4u);
        if (!rebuilt) e->failed = true;
    } else if (prime_iter_template(inner, &iter)) {
        if (iter.own) return term;
        Atom *own = prime_elab_own_list(e, NULL, 0u, true);
        if (!own) return term;
        rebuilt = prime_elab_iter_rebuild(e, inner->expr.elems,
                                          inner->expr.len, own);
    } else {
        return term;
    }
    if (e->failed || !rebuilt) return term;
    return braces ? prime_elab_rewrap(e, rebuilt, braces) : rebuilt;
}

/* The binding structure of a term formed at run time (the section above):
 * steps 1 and 3 of a form's elaboration under `profile`, the binders and the
 * seal, and the root's record.  Ownership, step 2, is never decided here.
 * NULL when the term cannot be formed. */
static Atom *prime_elab_formed(Arena *arena, Atom *form,
                               CettaPrimeScopeProfile profile) {
    PrimeElab e = {
        .arena = arena,
        .profile = profile,
        .seal = prime_quote_seal_active(),
    };
    g_prime_elab_error[0] = '\0';
    e.record = prime_scope_record(arena, profile);
    if (!e.record) return NULL;
    Atom *result = prime_elab_binders(&e, form, false);
    if (!e.failed && e.seal) {
        prime_elab_seal_collect(&e, result, 0u, false);
        if (!e.failed)
            result = prime_elab_seal_apply(&e, result, 0u, false);
    }
    if (!e.failed) result = prime_formed_root_record(&e, result);
    bool failed = e.failed;
    prime_elab_free(&e);
    return failed ? NULL : result;
}

/* The term `term`, formed at run time, given its binding structure
 * (prime_elab_formed) under the profile in force.  With `from_parts`,
 * `term`'s root was formed now from parts formed before (a construction or
 * a substitution): the parts' lambdas keep their records first.  The
 * thread's quoted-hole filling (prime_semantics_subst_var) is not the
 * pass's: it is set aside.  NULL when the term cannot be formed. */
static Atom *prime_form_elaborate(Arena *arena, Atom *term, bool from_parts) {
    if (!prime_formed_relevant(term)) return term;
    Atom *marked = from_parts ? prime_value_marked(arena, term, false) : term;
    if (!marked) return NULL;
    const Atom *saved_hole = g_prime_subst_quoted_hole;
    g_prime_subst_quoted_hole = NULL;
    Atom *result = prime_elab_formed(arena, marked,
                                     prime_scope_runtime_profile());
    g_prime_subst_quoted_hole = saved_hole;
    return result;
}

/* A term built at run time from parts (cons-atom, union-atom).  Its root is
 * the one node formed now:
 *   - a lambda or template (or `new`) is formed (prime_elab_formed): its
 *     binders get identities of their own, and it owns none of its parts'
 *     names; its parts' lambdas are kept as they were formed;
 *   - a binder (let, case, match, ...) is sealed as the reader seals one:
 *     only the new node can relate a pattern to a quoted mention that no
 *     part relates, and the parts were sealed when they were formed;
 *   - a quotation is code made of a value, whose lambdas keep the binding
 *     structure they were formed with (prime_value_into_code); a store
 *     variable it holds stays shared, whether it is bound before or after
 *     the quotation is built;
 *   - any other term is returned as it is.
 * NULL on failure. */
Atom *prime_semantics_form_built(Arena *arena, Atom *built) {
    if (!arena || !built || built->kind != ATOM_EXPR || built->expr.len < 2u ||
        !atom_has_vars(built))
        return built;
    if (atom_is_quotation(built)) {
        Atom *payload = prime_value_marked(arena, built->expr.elems[1], true);
        return payload == built->expr.elems[1]
            ? built : prime_code_with_payload(arena, built, payload);
    }
    if (prime_formed_template(arena, built))
        return prime_form_elaborate(arena, built, true);
    return prime_formed_binder(built) ? prime_seal_formed(arena, built)
                                      : built;
}

/* A term whose head a substitution has just filled with the symbol at its
 * head: a lambda or template formed so is formed (prime_elab_formed), a
 * binder formed so is sealed as a written one (in code too).  In code a
 * lambda stays syntax: it is formed when the code is opened. */
static __thread uint32_t g_prime_forming_in_code = 0u;

static Atom *prime_formed_by_substitution(Arena *arena, Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 2u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL ||
        !atom_has_vars(term))
        return term;
    if (g_prime_forming_in_code == 0u && prime_formed_template(arena, term))
        return prime_form_elaborate(arena, term, true);
    return prime_formed_binder(term) ? prime_seal_formed(arena, term) : term;
}

/* `term` with each variable `from[i]` replaced by `to[i]` wherever it
 * occurs, quotations and lists included.  NULL on failure. */
static Atom *prime_rename_everywhere(Arena *arena, Atom *term,
                                     Atom *const *from, Atom *const *to,
                                     size_t count, uint32_t nesting) {
    if (!term) return NULL;
    if (term->kind == ATOM_VAR) {
        for (size_t i = 0u; i < count; i++)
            if (term->var_id == from[i]->var_id) return to[i];
        return term;
    }
    if (term->kind != ATOM_EXPR || !atom_has_vars(term)) return term;
    if (nesting > PRIME_ELAB_NESTING_LIMIT) return NULL;
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        Atom *child = term->expr.elems[i];
        Atom *next = prime_rename_everywhere(arena, child, from, to, count,
                                             nesting + 1u);
        if (!next) return NULL;
        if (next != child && !items) {
            items = arena_alloc(arena, sizeof(Atom *) * (size_t)term->expr.len);
            if (!items) return NULL;
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = term->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    return items ? atom_expr(arena, items, term->expr.len) : term;
}

/* An opening of code (stage 5, item 3) is an activation of the written
 * quotation: the code's payload with the names it owns copied to fresh
 * slots, an instance of their own, as an activation copies a template's own
 * slots.  An own name is a slot of this quotation alone, so it is renamed
 * wherever it occurs in the code: a nested quotation holds it only as a
 * hole or a crossing name, which take their value from this code.  An own
 * name a match has bound is a name no longer (prime_bindings_own_list).
 * Code built at run time owns nothing, and its payload is itself.  NULL on
 * failure.
 *
 * The snapshot readout copies at a template's activation every other name
 * of its body still unbound, after the environment action has put the bound
 * ones' values in place (prime_scope_activate).  The environment action
 * leaves quoted code sealed, so at an opening a name the code shares with
 * the scope around may be bound with its value not yet in place; the
 * opening copies the own names alone, under every readout. */
static Atom *prime_code_opening(Arena *arena, Atom *code) {
    Atom *payload = code->expr.elems[1];
    if (!atom_is_quotation(code) || code->expr.len != 3u) return payload;
    Atom *own = code->expr.elems[2];
    PrimeFreeNames names = {.arena = arena};
    for (CettaExprIndex j = PRIME_OWN_FIRST; j < own->expr.len; j++) {
        Atom *name = own->expr.elems[j];
        if (name && name->kind == ATOM_VAR) prime_free_names_add(&names, name);
    }
    Atom *result = names.failed ? NULL : payload;
    Atom **copies = names.found_len
        ? arena_alloc(arena, sizeof(Atom *) * names.found_len) : NULL;
    if (names.found_len && !copies) result = NULL;
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    for (size_t i = 0u; result && i < names.found_len; i++) {
        CettaFrameIdentity instance =
            cetta_frame_identity_scope_fresh(&identities);
        copies[i] = atom_var_like(arena, names.found[i],
                                  var_epoch_id(fresh_var_id(), instance));
        if (!copies[i]) result = NULL;
    }
    if (result && names.found_len)
        result = prime_rename_everywhere(arena, payload, names.found, copies,
                                         names.found_len, 0u);
    free(names.found);
    free(names.hidden);
    return result;
}

/* `*` on code: an opening (prime_code_opening), and the term it gives
 * formed now (prime_elab_formed): the lambdas written in the code keep the
 * ownership decided where they were written, and a value's lambda inside it
 * keeps its record.  Contextual code `(quote M (k ...))` opens as the
 * function of its binders, `(lam (k ...) M)`.  NULL when `code` is no code
 * value or the opened term cannot be formed. */
Atom *prime_semantics_open_code(Arena *arena, Atom *code) {
    if (!arena || !prime_code_like(code)) return NULL;
    Atom *payload = prime_code_opening(arena, code);
    if (!payload) return NULL;
    if (prime_semantics_contextual_code(code)) {
        Atom *keys = code->expr.elems[2];
        Atom *binders = keys->expr.len == 1u ? keys->expr.elems[0] : keys;
        payload = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.prime_lam),
                             binders, payload);
        if (!payload) return NULL;
    }
    return prime_form_elaborate(arena, payload, false);
}

/* ── Prime's binding structure in the generic substitution ─────────────
 *
 * The application of bindings (match.c, every bindings_apply variant) is the
 * one substitution every route shares: the evaluator instantiating an
 * equation's body or a branch, the relational machine materializing a goal,
 * a space query's template.  In Prime it calls these three operations while
 * it rebuilds a term (BindingsStructureHooks):
 *   - an expression whose head was a variable and is now a symbol was formed
 *     by the substitution (prime_formed_by_substitution): a binder formed so
 *     is sealed, in code too; a lambda formed so in a value gets its binder
 *     identities, and in code stays code until the code is opened;
 *   - a variable inside code reads as code: bound to contextual code (a part
 *     taken out from under binders of its code), it is the part's syntax,
 *     which the code around it puts back under binders of its own
 *     (prime_spliced_syntax).  Outside code it is the contextual code;
 *   - a scope's own list keeps only names: a match that takes a template or
 *     code apart may bind a name the scope owns, and that name's value then
 *     stands in its place, so no activation or opening copies it
 *     (prime_bindings_own_list). */
static Atom *prime_bindings_formed_node(Arena *arena, Atom *node, bool in_code) {
    if (in_code) g_prime_forming_in_code++;
    Atom *formed = prime_formed_by_substitution(arena, node);
    if (in_code) g_prime_forming_in_code--;
    return formed;
}

static Atom *prime_bindings_code_reading(Atom *image) {
    return prime_spliced_syntax(image);
}

/* An own list `(OWN RECORD x ...)` after a substitution: the items that are
 * still names, each once.  `(let $c @(Pair $u k) (unify $c @(Pair 1 k) ...))`
 * binds the code's own `$u` to 1: the code is now `(Pair 1 k)`, and it owns
 * nothing; two own names bound to one variable leave that variable once. */
static Atom *prime_bindings_own_list(Arena *arena, Atom *list) {
    if (!list || list->kind != ATOM_EXPR || list->expr.len < PRIME_OWN_FIRST)
        return list;
    CettaExprLen len = list->expr.len;
    bool clean = true;
    for (CettaExprIndex j = PRIME_OWN_FIRST; clean && j < len; j++) {
        Atom *item = list->expr.elems[j];
        if (!item || item->kind != ATOM_VAR) {
            clean = false;
            break;
        }
        for (CettaExprIndex k = PRIME_OWN_FIRST; k < j; k++)
            if (list->expr.elems[k]->var_id == item->var_id) {
                clean = false;
                break;
            }
    }
    if (clean) return list;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (size_t)len);
    if (!items) return NULL;
    CettaExprLen kept = 0u;
    for (CettaExprIndex j = 0u; j < PRIME_OWN_FIRST; j++)
        items[kept++] = list->expr.elems[j];
    for (CettaExprIndex j = PRIME_OWN_FIRST; j < len; j++) {
        Atom *item = list->expr.elems[j];
        if (!item || item->kind != ATOM_VAR) continue;
        bool seen = false;
        for (CettaExprIndex k = PRIME_OWN_FIRST; k < kept && !seen; k++)
            seen = items[k]->var_id == item->var_id;
        if (!seen) items[kept++] = item;
    }
    return atom_expr(arena, items, kept);
}

/* ── Taking code apart with its binders ──────────────────────────────
 *
 * A quoted pattern meets quoted code binder by binder.  A binder position
 * of the pattern matches the code's binder by identity, never by spelling:
 * both sides are read with each binder of their code, and its references,
 * as the binder's level (prime_semantics_code_canonical), so `(lam w w)`
 * matches `(lam z z)`, and `(lam w z)` does not, its `z` being free where the
 * code's is bound.  A pattern variable at a binder position takes the
 * code's binder itself.
 *
 * Each hole (a variable the pattern holds inside the quotation) then takes
 * the part of the code it met (prime_semantics_contextual_match).  A part
 * under binders of the code (the parameters of a lambda, of a
 * map-atom/foldl-atom template, or the names of a `new`) that it mentions
 * is no open term: it is handed over as contextual code, `(quote M (k ...))`,
 * with those binders, the outermost first.  It is read where the variable
 * stands: a hole of the pattern refers to the pattern's binders, a variable
 * of the value to the value's, so `(quote (lam z $hole))` meeting
 * `(quote (lam w w))` gives `$hole` the code `(quote z (z))` whichever side
 * it stands on.  `lift let` fills them (prime_semantics_lift_let) and `*`
 * opens it as their function.  Where the continuation splices the hole into
 * quoted code again, it splices the part's syntax: the code around it puts
 * back the binders of those names.  A binder can never escape its code: a
 * match that would bind any other variable to one of the code's binders
 * fails. */

typedef struct {
    Atom **items;
    size_t len, cap;
} PrimeAtomList;

static bool prime_atom_list_push(PrimeAtomList *list, Atom *item) {
    if (!prime_env_reserve((void **)&list->items, &list->cap, list->len + 1u,
                           sizeof(*list->items)))
        return false;
    list->items[list->len++] = item;
    return true;
}

/* The binder of level `level` in quoted code, (CODE_BINDER level). */
static Atom *prime_code_binder_atom(Arena *arena, uint32_t level) {
    Atom *tag = atom_internal_tag(arena, CETTA_INTERNAL_TAG_PRIME_CODE_BINDER);
    Atom *number = atom_int(arena, (int64_t)level);
    return tag && number ? atom_expr2(arena, tag, number) : NULL;
}

static bool prime_code_binder_is(const Atom *term) {
    return term && term->kind == ATOM_EXPR && term->expr.len == 2u &&
           atom_is_internal_tag(term->expr.elems[0],
                                CETTA_INTERNAL_TAG_PRIME_CODE_BINDER);
}

static bool prime_code_binder_mentioned(const Atom *term, uint32_t nesting) {
    if (!term || term->kind != ATOM_EXPR || nesting > PRIME_ELAB_NESTING_LIMIT)
        return false;
    if (prime_code_binder_is(term)) return true;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        if (prime_code_binder_mentioned(term->expr.elems[i], nesting + 1u))
            return true;
    return false;
}

typedef struct {
    Arena *arena;
    bool pattern_side;
    PrimeAtomList keys;   /* the binders in scope in the code, by level */
    uint32_t nesting;
    bool failed;
} PrimeCodeCanon;

static Atom *prime_code_canon(PrimeCodeCanon *c, Atom *term, bool in_code);

/* The binder `name` of quoted code made its level's binder: pushed in
 * scope, and its canonical atom returned.  On the pattern's side a
 * variable at a binder position stays a variable: it takes the code's
 * binder when matched. */
static Atom *prime_code_canon_bind(PrimeCodeCanon *c, Atom *name) {
    Atom *key = prime_binder_key(name);
    if (!key) return name;
    if (c->pattern_side && key->kind == ATOM_VAR) return name;
    Atom *atom = prime_code_binder_atom(c->arena, (uint32_t)c->keys.len);
    if (!atom || !prime_atom_list_push(&c->keys, key)) {
        c->failed = true;
        return name;
    }
    return atom;
}

/* A reference of quoted code to a binder in scope: its level's atom. */
static Atom *prime_code_canon_reference(PrimeCodeCanon *c, Atom *term) {
    for (size_t k = c->keys.len; k > 0u; k--) {
        if (!prime_binder_reference(c->keys.items[k - 1u], term)) continue;
        Atom *atom = prime_code_binder_atom(c->arena, (uint32_t)(k - 1u));
        if (!atom) c->failed = true;
        return atom ? atom : term;
    }
    return NULL;
}

static Atom *prime_code_canon_lambda(PrimeCodeCanon *c, Atom *term,
                                     const PrimeLambdaTelescope *telescope) {
    size_t mark = c->keys.len;
    Atom **groups = arena_alloc(c->arena, sizeof(Atom *) * telescope->count);
    if (!groups) {
        c->failed = true;
        return term;
    }
    for (size_t g = 0u; g < telescope->count && !c->failed; g++) {
        CettaPrimeLambdaBinderGroupV1 group = telescope->groups[g];
        groups[g] = group.syntax;
        for (size_t t = 0u; group.typed && t < group.types_count; t++) {
            size_t position = group.types_start + t;
            Atom *type = group.syntax->expr.elems[position];
            Atom *next = prime_code_canon(c, type, true);
            if (next == type) continue;
            group.syntax = prime_group_with(c->arena, &group, position, next);
            if (!group.syntax) {
                c->failed = true;
                break;
            }
            groups[g] = group.syntax;
        }
        for (size_t i = 0u; i < group.names_count && !c->failed; i++) {
            Atom *name = cetta_prime_lambda_binder_name_v1(&group, i);
            Atom *canonical = name ? prime_code_canon_bind(c, name) : NULL;
            if (!canonical || canonical == name) continue;
            group.syntax = prime_group_with(
                c->arena, &group, group.typed ? group.names_start + i : 0u,
                canonical);
            if (!group.syntax) {
                c->failed = true;
                break;
            }
            groups[g] = group.syntax;
        }
    }
    Atom *body = c->failed ? term->expr.elems[2]
                           : prime_code_canon(c, term->expr.elems[2], true);
    c->keys.len = mark;
    if (c->failed) return term;
    /* The authored lambda: an own list is hidden metadata. */
    Atom *binders = telescope->listed
        ? atom_expr(c->arena, groups, (CettaExprLen)telescope->count)
        : groups[0];
    Atom *rebuilt = binders ? atom_expr3(c->arena, term->expr.elems[0],
                                         binders, body)
                            : NULL;
    if (!rebuilt) c->failed = true;
    return rebuilt ? rebuilt : term;
}

static Atom *prime_code_canon(PrimeCodeCanon *c, Atom *term, bool in_code) {
    if (c->failed || !term) return term;
    if (in_code && (term->kind == ATOM_VAR || term->kind == ATOM_SYMBOL ||
                    atom_is_drop_of_quoted_variable(term) ||
                    prime_form(term, "unquote"))) {
        Atom *reference = prime_code_canon_reference(c, term);
        if (reference) return reference;
    }
    if (term->kind != ATOM_EXPR) return term;
    if (++c->nesting > PRIME_ELAB_NESTING_LIMIT) {
        c->failed = true;
        return term;
    }
    Atom *result = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (prime_code_like(term)) {
        /* Each quotation's code has binders of its own: no reference
         * crosses a quotation, which seals what it holds. */
        PrimeAtomList outer = c->keys;
        c->keys = (PrimeAtomList){0};
        Atom *payload = prime_code_canon(c, term->expr.elems[1], true);
        free(c->keys.items);
        c->keys = outer;
        if (!c->failed && payload != term->expr.elems[1])
            result = prime_code_with_payload(c->arena, term, payload);
    } else if (in_code && prime_lambda_telescope(c->arena, term, &telescope)) {
        result = prime_code_canon_lambda(c, term, &telescope);
    } else if (in_code && prime_new_form(term)) {
        size_t mark = c->keys.len;
        Atom *names = term->expr.elems[1];
        Atom **items = arena_alloc(
            c->arena, sizeof(Atom *) * ((size_t)names->expr.len + 1u));
        if (!items) {
            c->failed = true;
        } else {
            for (CettaExprIndex j = 0u; j < names->expr.len; j++)
                items[j] = prime_code_canon_bind(c, names->expr.elems[j]);
            Atom *canon_names = atom_expr(c->arena, items, names->expr.len);
            Atom *body = prime_code_canon(c, term->expr.elems[2], true);
            result = canon_names && !c->failed
                ? atom_expr3(c->arena, term->expr.elems[0], canon_names, body)
                : term;
            if (!result) c->failed = true;
        }
        c->keys.len = mark;
    } else if (in_code && prime_iter_template(term, &iter)) {
        CettaExprLen len = atom_authored_len(term);
        Atom **items = arena_alloc(c->arena, sizeof(Atom *) * (size_t)len);
        if (!items) {
            c->failed = true;
        } else {
            for (CettaExprIndex i = 0u; i < len; i++) items[i] = term->expr.elems[i];
            for (CettaExprIndex i = 1u; i < iter.first_param; i++)
                items[i] = prime_code_canon(c, items[i], true);
            size_t mark = c->keys.len;
            for (CettaExprIndex p = 0u; p < iter.param_count; p++)
                items[iter.first_param + p] = prime_code_canon_bind(
                    c, items[iter.first_param + p]);
            items[iter.body] = prime_code_canon(c, items[iter.body], true);
            c->keys.len = mark;
            result = c->failed ? term : atom_expr(c->arena, items, len);
            if (!result) {
                c->failed = true;
                result = term;
            }
        }
    } else {
        Atom **items = NULL;
        CettaExprLen len = in_code ? atom_authored_len(term) : term->expr.len;
        for (CettaExprIndex i = 0u; i < len && !c->failed; i++) {
            Atom *child = term->expr.elems[i];
            Atom *next = prime_code_canon(c, child, in_code);
            if ((next != child || len != term->expr.len) && !items) {
                items = arena_alloc(c->arena, sizeof(Atom *) * (size_t)len);
                if (!items) {
                    c->failed = true;
                    break;
                }
                for (CettaExprIndex j = 0u; j < i; j++)
                    items[j] = term->expr.elems[j];
            }
            if (items) items[i] = next;
        }
        if (!c->failed && items) {
            result = atom_expr(c->arena, items, len);
            if (!result) {
                c->failed = true;
                result = term;
            }
        }
    }
    c->nesting--;
    return c->failed ? term : result;
}

/* Whether the pattern `term` holds a quotation. */
static bool prime_pattern_quotes(const Atom *term, uint32_t nesting) {
    if (!term || term->kind != ATOM_EXPR || nesting > PRIME_ELAB_NESTING_LIMIT)
        return false;
    if (prime_code_like(term)) return true;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        if (prime_pattern_quotes(term->expr.elems[i], nesting + 1u))
            return true;
    return false;
}

bool prime_semantics_code_pattern(const Atom *pattern) {
    return prime_pattern_quotes(pattern, 0u);
}

/* Whether the pattern `term` holds code it takes apart: a quotation whose
 * code is an expression.  A quotation of a bare variable takes code whole,
 * and matches binder by binder exactly as it matches as written. */
static bool prime_pattern_takes_code_apart(const Atom *term,
                                           uint32_t nesting) {
    if (!term || term->kind != ATOM_EXPR || nesting > PRIME_ELAB_NESTING_LIMIT)
        return false;
    if (prime_code_like(term))
        return term->expr.elems[1] && term->expr.elems[1]->kind == ATOM_EXPR;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        if (prime_pattern_takes_code_apart(term->expr.elems[i], nesting + 1u))
            return true;
    return false;
}

bool prime_semantics_pattern_takes_code_apart(const Atom *pattern) {
    return prime_pattern_takes_code_apart(pattern, 0u);
}

bool prime_semantics_binder_takes_code_apart(const Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 2u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL ||
        term->expr.elems[0]->sym_id == g_builtin_syms.equals)
        return false;
    for (CettaExprIndex i = 1u; i < term->expr.len; i++) {
        PrimeChildRole role = prime_child_role(term, i);
        Atom *child = term->expr.elems[i];
        if (role == PRIME_CHILD_PATTERN &&
            prime_pattern_takes_code_apart(child, 0u))
            return true;
        if (role == PRIME_CHILD_PATTERN_PAIRS && child &&
            child->kind == ATOM_EXPR)
            for (CettaExprIndex k = 0u; k < child->expr.len; k++) {
                Atom *pair = child->expr.elems[k];
                if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u &&
                    prime_pattern_takes_code_apart(pair->expr.elems[0], 0u))
                    return true;
            }
    }
    return false;
}

Atom *prime_semantics_code_canonical(Arena *arena, Atom *term,
                                     bool pattern_side) {
    if (!arena || !term || term->kind != ATOM_EXPR) return term;
    PrimeCodeCanon c = {.arena = arena, .pattern_side = pattern_side};
    Atom *result = prime_code_canon(&c, term, false);
    free(c.keys.items);
    return c.failed ? NULL : result;
}

typedef struct {
    Arena *arena;
    Bindings *bindings;
    /* binder keys of the code in scope, outermost first: the value's, and
     * the pattern's at the same levels (a pattern variable where the
     * pattern's binder is one) */
    PrimeAtomList binders;
    PrimeAtomList pattern_binders;
    PrimeAtomList holes;     /* the holes given contextual code, and parts */
    PrimeAtomList parts;
    PrimeAtomList seen;      /* every variable rebound or kept */
    uint32_t nesting;
    bool failed;
} PrimeContextualMatch;

static bool prime_contextual_member(const PrimeAtomList *list,
                                    const Atom *var) {
    for (size_t i = 0u; i < list->len; i++)
        if (list->items[i]->var_id == var->var_id) return true;
    return false;
}

/* Whether `term` mentions the variable `id` anywhere. */
static bool prime_contextual_mentions(const Atom *term, VarId id) {
    if (!term) return false;
    if (term->kind == ATOM_VAR) return term->var_id == id;
    if (term->kind != ATOM_EXPR || !atom_has_vars(term)) return false;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        if (prime_contextual_mentions(term->expr.elems[i], id)) return true;
    return false;
}

/* Rebinds `var` to `value` unless it is bound to it already; the first
 * occurrence decides.  A variable that is not bound keeps its state. */
static bool prime_contextual_rebind(PrimeContextualMatch *c, Atom *var,
                                    Atom *value) {
    if (prime_contextual_member(&c->seen, var)) return false;
    if (!prime_atom_list_push(&c->seen, var)) {
        c->failed = true;
        return false;
    }
    BindingValue bound = bindings_lookup_value_id(c->bindings, var->var_id);
    if (!bound.skeleton || bound.skeleton == value ||
        atom_eq(bound.skeleton, value))
        return false;
    /* A value that holds the variable itself cannot be its binding. */
    if (prime_contextual_mentions(value, var->var_id)) return false;
    if (!bindings_rewrite_value_id(c->bindings, var->var_id,
                                   binding_value_from_atom(value))) {
        c->failed = true;
        return false;
    }
    return true;
}

/* The part `part`, met in code under the binders `binders` (the outermost
 * first), as a variable takes it: contextual code over the binders it
 * mentions, the outermost first, or the part itself when it mentions none.
 * *keyed is whether it mentions any.  NULL on failure. */
static Atom *prime_contextual_part(PrimeContextualMatch *c,
                                   const PrimeAtomList *binders, Atom *part,
                                   bool in_code, bool *keyed) {
    *keyed = false;
    if (!in_code || binders->len == 0u) return part;
    Atom **keys = arena_alloc(c->arena, sizeof(Atom *) * binders->len);
    if (!keys) return NULL;
    size_t count = 0u;
    for (size_t i = 0u; i < binders->len; i++) {
        Atom *key = binders->items[i];
        bool repeated = false;
        for (size_t j = 0u; j < count && !repeated; j++)
            repeated = prime_same_key(keys[j], key);
        if (repeated) continue;
        /* A binder of the same key nearer the part shadows this one. */
        bool shadowed = false;
        for (size_t j = i + 1u; j < binders->len && !shadowed; j++)
            shadowed = prime_same_key(binders->items[j], key);
        if (!shadowed && prime_binder_occurs_free(c->arena, key, part))
            keys[count++] = key;
    }
    if (count == 0u) return part;
    *keyed = true;
    Atom *list = atom_expr(c->arena, keys, (CettaExprLen)count);
    return list
        ? atom_expr3(c->arena, atom_symbol_id(c->arena, g_builtin_syms.quote),
                     part, list)
        : NULL;
}

/* `part`, taken from under the binders `from` of one side of a code match,
 * read under the binders `to` of the other side at the same levels (both
 * the outermost first): its references to each binder of `from` become
 * references to the binder of `to` at that level, all at once (through a
 * fresh variable each, so no reference is renamed twice).  A level whose
 * `to` binder is a pattern variable (`to_symbols_only`), which takes the
 * code's binder itself, keeps the `from` binder; so does every level when
 * the two sides do not have the same binders.  *side receives the binders in
 * scope where the part is read.  NULL on failure. */
static Atom *prime_contextual_rename(PrimeContextualMatch *c, Atom *part,
                                     const PrimeAtomList *from,
                                     const PrimeAtomList *to,
                                     bool to_symbols_only,
                                     PrimeAtomList *side) {
    side->len = 0u;
    for (size_t i = 0u; i < from->len; i++)
        if (!prime_atom_list_push(side, from->items[i])) return NULL;
    if (from->len != to->len) return part;
    size_t count = from->len;
    Atom **temps = count ? arena_alloc(c->arena, sizeof(Atom *) * count) : NULL;
    if (count && !temps) return NULL;
    Atom *value = part;
    /* The innermost binder of a key is the one the part refers to. */
    for (size_t k = count; k > 0u && value; k--) {
        size_t i = k - 1u;
        temps[i] = NULL;
        Atom *source = from->items[i], *target = to->items[i];
        if (!source || !target || prime_same_key(source, target) ||
            (to_symbols_only && target->kind == ATOM_VAR))
            continue;
        bool shadowed = false;
        for (size_t j = i + 1u; j < count && !shadowed; j++)
            shadowed = prime_same_key(from->items[j], source);
        if (shadowed || !prime_binder_occurs_free(c->arena, source, value))
            continue;
        temps[i] = atom_var_with_id(c->arena, "binder", fresh_var_id());
        value = temps[i] ? prime_subst_binder(c->arena, value, source, temps[i])
                         : NULL;
    }
    for (size_t i = 0u; i < count && value; i++) {
        if (!temps[i]) continue;
        value = prime_subst_binder(c->arena, value, temps[i], to->items[i]);
        side->items[i] = to->items[i];
    }
    return value;
}

/* The pattern variable `hole` met the part `part` of the value: it takes the
 * part, or, in code, contextual code when the part mentions binders of the
 * code in scope.  Read where the hole stands, in the pattern's code: its
 * references to the value's binders are references to the pattern's binders
 * at the same levels, so a binder of the value never enters the pattern's
 * code by its bare name.  A pattern variable at a binder position takes the
 * value's binder itself. */
static void prime_contextual_hole(PrimeContextualMatch *c, Atom *hole,
                                  Atom *part, bool in_code) {
    if (prime_contextual_member(&c->seen, hole)) return;
    PrimeAtomList side = {0};
    Atom *read = in_code
        ? prime_contextual_rename(c, part, &c->binders, &c->pattern_binders,
                                  true, &side)
        : part;
    bool keyed = false;
    Atom *value = read ? prime_contextual_part(c, &side, read, in_code, &keyed)
                       : NULL;
    free(side.items);
    if (!value) {
        c->failed = true;
        return;
    }
    if (prime_contextual_rebind(c, hole, value) && keyed &&
        (!prime_atom_list_push(&c->holes, hole) ||
         !prime_atom_list_push(&c->parts, read)))
        c->failed = true;
}

/* A variable of the value met the part `part` of the pattern: it takes the
 * part, with the pattern's binders in scope (its references to them, and a
 * pattern variable standing at a binder position) read as the value's
 * binders at the same levels.  The hole rule holds in this direction too: a
 * part under binders of the code that it mentions is contextual code over
 * them, so a binder never leaves its code by a bare name; read inside the
 * code, where the variable stands, it is the part's syntax again
 * (prime_bindings_code_reading). */
static void prime_contextual_value_var(PrimeContextualMatch *c, Atom *var,
                                       Atom *part, bool in_code) {
    if (prime_contextual_member(&c->seen, var)) return;
    PrimeAtomList side = {0};
    Atom *value = in_code
        ? prime_contextual_rename(c, part, &c->pattern_binders, &c->binders,
                                  false, &side)
        : part;
    bool keyed = false;
    value = value ? prime_contextual_part(c, &c->binders, value, in_code,
                                          &keyed)
                  : NULL;
    free(side.items);
    if (!value) {
        c->failed = true;
        return;
    }
    (void)prime_contextual_rebind(c, var, value);
}

/* The binder keys of the code construct `term` for its child `index`: a
 * lambda's parameters for its body, a template's parameters for its body,
 * a `new`'s names for its body.  On the pattern's side a binder that is a
 * pattern variable is that variable: it takes the code's binder at its
 * level.  The number pushed. */
static size_t prime_contextual_push(PrimeContextualMatch *c,
                                    PrimeAtomList *list, Atom *term,
                                    CettaExprIndex index) {
    size_t before = list->len;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (index == 2u && prime_lambda_telescope(c->arena, term, &telescope)) {
        for (size_t g = 0u; g < telescope.count && !c->failed; g++)
            for (size_t n = 0u; n < telescope.groups[g].names_count; n++) {
                Atom *key = prime_binder_key(
                    cetta_prime_lambda_binder_name_v1(&telescope.groups[g], n));
                if (!key) continue;
                if (!prime_atom_list_push(list, key)) c->failed = true;
            }
    } else if (prime_iter_template(term, &iter) && index == iter.body) {
        for (CettaExprIndex p = 0u; p < iter.param_count && !c->failed; p++)
            if (!prime_atom_list_push(list,
                                      term->expr.elems[iter.first_param + p]))
                c->failed = true;
    } else if (index == 2u && prime_new_form(term)) {
        Atom *names = term->expr.elems[1];
        for (CettaExprIndex j = 0u; j < names->expr.len && !c->failed; j++)
            if (!prime_atom_list_push(list, names->expr.elems[j]))
                c->failed = true;
    }
    return list->len - before;
}

static void prime_contextual_walk(PrimeContextualMatch *c, Atom *pattern,
                                  Atom *value, bool in_code) {
    if (c->failed || !pattern || !value) return;
    if (pattern->kind == ATOM_VAR) {
        prime_contextual_hole(c, pattern, value, in_code);
        return;
    }
    if (value->kind == ATOM_VAR) {
        prime_contextual_value_var(c, value, pattern, in_code);
        return;
    }
    if (pattern->kind != ATOM_EXPR || value->kind != ATOM_EXPR) return;
    if (++c->nesting > PRIME_ELAB_NESTING_LIMIT) {
        c->failed = true;
        return;
    }
    bool quotation = prime_code_like(pattern) && prime_code_like(value);
    bool code = in_code || quotation;
    /* A quotation's code has binders of its own. */
    PrimeAtomList outer = c->binders, outer_pattern = c->pattern_binders;
    if (quotation) {
        c->binders = (PrimeAtomList){0};
        c->pattern_binders = (PrimeAtomList){0};
    }
    CettaExprLen len = atom_authored_len(value);
    if (atom_authored_len(pattern) == len) {
        for (CettaExprIndex i = 0u; i < len && !c->failed; i++) {
            size_t pushed = 0u, pattern_pushed = 0u;
            if (code && !quotation) {
                pushed = prime_contextual_push(c, &c->binders, value, i);
                pattern_pushed = prime_contextual_push(
                    c, &c->pattern_binders, pattern, i);
            }
            prime_contextual_walk(c, pattern->expr.elems[i],
                                  value->expr.elems[i], code);
            c->binders.len -= pushed;
            c->pattern_binders.len -= pattern_pushed;
        }
    }
    if (quotation) {
        free(c->binders.items);
        free(c->pattern_binders.items);
        c->binders = outer;
        c->pattern_binders = outer_pattern;
    }
    c->nesting--;
}

/* `term` with each quoted mention of a hole given contextual code replaced
 * by its part's syntax (prime_spliced_syntax): the continuation's quoted
 * code puts the part back under binders of its own. */
static Atom *prime_contextual_splice(PrimeContextualMatch *c, Atom *term,
                                     uint32_t depth) {
    if (c->failed || !term || !atom_has_vars(term)) return term;
    if (term->kind == ATOM_VAR) {
        if (depth == 0u) return term;
        for (size_t i = 0u; i < c->holes.len; i++)
            if (c->holes.items[i]->var_id == term->var_id)
                return c->parts.items[i];
        return term;
    }
    if (term->kind != ATOM_EXPR) return term;
    if (++c->nesting > PRIME_ELAB_NESTING_LIMIT) {
        c->failed = true;
        return term;
    }
    uint32_t inner = depth + (prime_code_like(term) ? 1u : 0u);
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len && !c->failed; i++) {
        Atom *child = term->expr.elems[i];
        Atom *next = prime_contextual_splice(c, child, inner);
        if (next != child && !items) {
            items = arena_alloc(c->arena,
                                sizeof(Atom *) * (size_t)term->expr.len);
            if (!items) {
                c->failed = true;
                break;
            }
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = term->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    c->nesting--;
    Atom *result = !c->failed && items
        ? atom_expr(c->arena, items, term->expr.len) : term;
    if (!result) c->failed = true;
    return result ? result : term;
}

/* Whether a binding mentions a binder of the code: one escaped the code. */
static bool prime_contextual_escaped(const Bindings *bindings) {
    BindingsIterator iterator = {.bindings = bindings};
    Binding binding;
    while (bindings_iterator_next(&iterator, &binding))
        if (prime_code_binder_mentioned(binding.value.skeleton, 0u))
            return true;
    return false;
}

bool prime_semantics_contextual_match(Arena *arena, Atom *pattern,
                                      Atom *value, Bindings *bindings,
                                      Atom **branch, bool *matched) {
    if (matched) *matched = true;
    if (!arena || !pattern || !value || !bindings) return false;
    if (!prime_pattern_quotes(pattern, 0u)) return true;
    PrimeContextualMatch c = {.arena = arena, .bindings = bindings};
    prime_contextual_walk(&c, pattern, value, false);
    if (!c.failed && matched && prime_contextual_escaped(bindings))
        *matched = false;
    if (!c.failed && branch && *branch && c.holes.len > 0u) {
        c.nesting = 0u;
        *branch = prime_contextual_splice(&c, *branch, 0u);
    }
    bool ok = !c.failed;
    free(c.binders.items);
    free(c.pattern_binders.items);
    free(c.holes.items);
    free(c.parts.items);
    free(c.seen.items);
    return ok;
}

/* `value` matched against the pattern `pattern`, which takes code apart,
 * binder by binder (stage 5, item 2): both in the forms of
 * prime_semantics_code_canonical, matched into `bindings`, then each hole
 * given the part it met, contextual code under binders of the code, and the
 * match refused when a binder would escape its code
 * (prime_semantics_contextual_match).  The same operation as `let`,
 * `unify`, `case` and `switch` take code apart by: an equation's head and a
 * space query use it after their candidates are selected.  *matched is
 * false when they do not match, and `bindings` is then unchanged.  False on
 * failure. */
bool prime_semantics_code_unify(Arena *arena, Atom *pattern, Atom *value,
                                Bindings *bindings, bool *matched) {
    *matched = false;
    if (!arena || !pattern || !value || !bindings) return false;
    Atom *pattern_form = prime_semantics_code_canonical(arena, pattern, true);
    Atom *value_form = prime_semantics_code_canonical(arena, value, false);
    if (!pattern_form || !value_form) return false;
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, bindings)) return false;
    if (!match_atoms_builder(value_form, pattern_form, &builder, arena)) {
        bindings_builder_free(&builder);
        return true;
    }
    Bindings taken;
    bindings_builder_take(&builder, &taken);
    bool kept = true;
    if (!prime_semantics_contextual_match(arena, pattern, value, &taken, NULL,
                                          &kept)) {
        bindings_free(&taken);
        return false;
    }
    if (!kept) {
        bindings_free(&taken);
        return true;
    }
    bindings_replace(bindings, &taken);
    *matched = true;
    return true;
}

static bool prime_bindings_code_pattern(const Atom *pattern) {
    return prime_pattern_takes_code_apart(pattern, 0u);
}

/* Whether the code `term` holds a binder: a lambda, a map-atom or
 * foldl-atom template, or a `new`. */
static bool prime_code_holds_binder(const Atom *term, uint32_t nesting) {
    if (!term || term->kind != ATOM_EXPR || nesting > PRIME_ELAB_NESTING_LIMIT)
        return false;
    if (term->expr.len > 0u && term->expr.elems[0] &&
        term->expr.elems[0]->kind == ATOM_SYMBOL) {
        SymbolId head = term->expr.elems[0]->sym_id;
        if (head == g_builtin_syms.prime_lam ||
            head == g_builtin_syms.map_atom ||
            head == g_builtin_syms.foldl_atom ||
            head == g_builtin_syms.prime_new)
            return true;
    }
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        if (prime_code_holds_binder(term->expr.elems[i], nesting + 1u))
            return true;
    return false;
}

/* `term` as an index reads it (BindingsStructureHooks.code_selection): code
 * that holds a binder is a fresh variable, since its binders' spellings
 * decide nothing (stage 5, item 2); candidates are then matched binder by
 * binder. */
static Atom *prime_code_selection_rec(Arena *arena, Atom *term,
                                      uint32_t nesting) {
    if (!term || term->kind != ATOM_EXPR) return term;
    if (nesting > PRIME_ELAB_NESTING_LIMIT) return NULL;
    if (prime_code_like(term))
        return prime_code_holds_binder(term->expr.elems[1], 0u)
            ? atom_var_with_id(arena, "code", fresh_var_id()) : term;
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        Atom *child = term->expr.elems[i];
        Atom *next = prime_code_selection_rec(arena, child, nesting + 1u);
        if (!next) return NULL;
        if (next != child && !items) {
            items = arena_alloc(arena, sizeof(Atom *) * (size_t)term->expr.len);
            if (!items) return NULL;
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = term->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    return items ? atom_expr(arena, items, term->expr.len) : term;
}

static Atom *prime_bindings_code_selection(Arena *arena, Atom *term) {
    return prime_code_selection_rec(arena, term, 0u);
}

static const BindingsStructureHooks prime_binding_structure_hooks = {
    .formed_node = prime_bindings_formed_node,
    .code_reading = prime_bindings_code_reading,
    .own_list = prime_bindings_own_list,
    .code_pattern = prime_bindings_code_pattern,
    .code_match = prime_semantics_code_unify,
    .code_selection = prime_bindings_code_selection,
};

const BindingsStructureHooks *prime_semantics_binding_hooks(void) {
    return &prime_binding_structure_hooks;
}

/* ── Equality of terms with binders (Prime `==`) ─────────────────────
 *
 * Bound names are compared by position, never by spelling, as a quoted
 * pattern meets quoted code: each binder (a lambda's or a template's
 * parameter, an own slot of an elaborated template, a `new`'s name, a name
 * in the list of contextual code) and its references are read as the
 * binder's number, in the order the binders are
 * met; an own slot is numbered where it first occurs.  A name a lambda
 * captures is not bound by it: it stays as the runtime has it, the value
 * the environment gave it or the variable itself.  No reference crosses a
 * quotation, which seals what it holds.  A pattern is no binder: a lambda
 * form in a pattern position is compared as written. */

typedef struct {
    Atom *key;
    Atom *atom;      /* the binder's number, NULL while not met (own slots) */
} PrimeEqBinder;

typedef struct {
    Arena *arena;
    PrimeEqBinder *binders;
    size_t len, cap;
    uint32_t next;
    uint32_t nesting;
    bool failed;
} PrimeEqCanon;

static bool prime_eq_push(PrimeEqCanon *c, Atom *key, bool numbered) {
    if (!key) return true;
    if (!prime_env_reserve((void **)&c->binders, &c->cap, c->len + 1u,
                           sizeof(*c->binders))) {
        c->failed = true;
        return false;
    }
    Atom *atom = numbered ? prime_code_binder_atom(c->arena, c->next++) : NULL;
    if (numbered && !atom) {
        c->failed = true;
        return false;
    }
    c->binders[c->len++] = (PrimeEqBinder){.key = key, .atom = atom};
    return true;
}

static Atom *prime_eq_reference(PrimeEqCanon *c, Atom *term) {
    for (size_t k = c->len; k > 0u; k--) {
        PrimeEqBinder *binder = &c->binders[k - 1u];
        if (!prime_binder_reference(binder->key, term)) continue;
        if (!binder->atom) {
            binder->atom = prime_code_binder_atom(c->arena, c->next++);
            if (!binder->atom) c->failed = true;
        }
        return binder->atom;
    }
    return NULL;
}

static Atom *prime_eq_canon(PrimeEqCanon *c, Atom *term, bool pattern);

static Atom *prime_eq_canon_lambda(PrimeEqCanon *c, Atom *inner,
                                   const PrimeLambdaTelescope *telescope) {
    size_t mark = c->len;
    Atom **groups = arena_alloc(c->arena, sizeof(Atom *) * telescope->count);
    if (!groups) {
        c->failed = true;
        return inner;
    }
    for (size_t g = 0u; g < telescope->count && !c->failed; g++) {
        CettaPrimeLambdaBinderGroupV1 group = telescope->groups[g];
        groups[g] = group.syntax;
        for (size_t t = 0u; group.typed && t < group.types_count; t++) {
            size_t position = group.types_start + t;
            Atom *next = prime_eq_canon(
                c, group.syntax->expr.elems[position], false);
            if (next == group.syntax->expr.elems[position]) continue;
            group.syntax = prime_group_with(c->arena, &group, position, next);
            if (!group.syntax) {
                c->failed = true;
                break;
            }
            groups[g] = group.syntax;
        }
        for (size_t i = 0u; i < group.names_count && !c->failed; i++) {
            Atom *name = cetta_prime_lambda_binder_name_v1(&group, i);
            Atom *key = name ? prime_binder_key(name) : NULL;
            if (!key || !prime_eq_push(c, key, true)) continue;
            group.syntax = prime_group_with(
                c->arena, &group, group.typed ? group.names_start + i : 0u,
                c->binders[c->len - 1u].atom);
            if (!group.syntax) {
                c->failed = true;
                break;
            }
            groups[g] = group.syntax;
        }
    }
    for (CettaExprIndex j = PRIME_OWN_FIRST;
         telescope->own && j < telescope->own->expr.len && !c->failed; j++)
        (void)prime_eq_push(c, telescope->own->expr.elems[j], false);
    Atom *body = c->failed ? inner->expr.elems[2]
                           : prime_eq_canon(c, inner->expr.elems[2], false);
    c->len = mark;
    if (c->failed) return inner;
    Atom *binders = telescope->listed
        ? atom_expr(c->arena, groups, (CettaExprLen)telescope->count)
        : groups[0];
    Atom *rebuilt = binders ? atom_expr3(c->arena, inner->expr.elems[0],
                                         binders, body)
                            : NULL;
    if (!rebuilt) c->failed = true;
    return rebuilt ? rebuilt : inner;
}

static Atom *prime_eq_canon(PrimeEqCanon *c, Atom *term, bool pattern) {
    if (c->failed || !term) return term;
    /* A variable bound around is its binder's number in a pattern too: the
     * pattern refines it there. */
    if (term->kind == ATOM_VAR ||
        (!pattern && (term->kind == ATOM_SYMBOL ||
                      prime_form(term, "unquote")))) {
        Atom *reference = prime_eq_reference(c, term);
        if (reference) return reference;
    }
    if (term->kind != ATOM_EXPR) return term;
    if (++c->nesting > PRIME_ELAB_NESTING_LIMIT) {
        c->failed = true;
        return term;
    }
    Atom *result = term;
    Atom *inner = term, *braces = NULL;
    if (pattern || !prime_meta_braces(term, &inner, &braces)) inner = term;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (!pattern && prime_code_like(term)) {
        /* No reference crosses a quotation.  Contextual code binds the names
         * of its list in its code, outermost first, as a lambda binds its
         * parameters. */
        size_t mark = c->len;
        PrimeEqBinder *outer = c->binders;
        size_t outer_cap = c->cap;
        c->binders = NULL;
        c->len = c->cap = 0u;
        Atom *keys = NULL;
        /* A written quotation's own names (stage 5, item 3) are bound by
         * it, as a template's own slots are: numbered where they first
         * occur. */
        if (atom_is_quotation(term) && term->expr.len == 3u) {
            Atom *own = term->expr.elems[2];
            for (CettaExprIndex j = PRIME_OWN_FIRST;
                 j < own->expr.len && !c->failed; j++)
                (void)prime_eq_push(c, own->expr.elems[j], false);
        }
        if (prime_semantics_contextual_code(term)) {
            Atom *list = term->expr.elems[2];
            Atom **items =
                arena_alloc(c->arena, sizeof(Atom *) * (size_t)list->expr.len);
            if (!items) c->failed = true;
            for (CettaExprIndex j = 0u;
                 items && !c->failed && j < list->expr.len; j++) {
                Atom *key = prime_binder_key(list->expr.elems[j]);
                items[j] = key && prime_eq_push(c, key, true)
                    ? c->binders[c->len - 1u].atom : list->expr.elems[j];
            }
            if (items && !c->failed) {
                keys = atom_expr(c->arena, items, list->expr.len);
                if (!keys) c->failed = true;
            }
        }
        Atom *payload = c->failed
            ? term->expr.elems[1]
            : prime_eq_canon(c, term->expr.elems[1], false);
        free(c->binders);
        c->binders = outer;
        c->cap = outer_cap;
        c->len = mark;
        if (!c->failed && keys)
            result = atom_expr3(c->arena, term->expr.elems[0], payload, keys);
        else if (!c->failed && payload != term->expr.elems[1])
            result = prime_code_with_payload(c->arena, term, payload);
        if (!result) {
            c->failed = true;
            result = term;
        }
    } else if (!pattern &&
               prime_lambda_telescope(c->arena, inner, &telescope)) {
        Atom *canon = prime_eq_canon_lambda(c, inner, &telescope);
        Atom *canon_braces = braces ? prime_eq_canon(c, braces, false) : NULL;
        result = braces ? atom_expr3(c->arena, term->expr.elems[0], canon,
                                     canon_braces)
                        : canon;
    } else if (!pattern && inner == term && prime_iter_template(term, &iter)) {
        CettaExprLen len = atom_authored_len(term);
        Atom **items = arena_alloc(c->arena, sizeof(Atom *) * (size_t)len);
        if (!items) {
            c->failed = true;
        } else {
            for (CettaExprIndex i = 0u; i < len; i++) items[i] = term->expr.elems[i];
            for (CettaExprIndex i = 1u; i < iter.first_param; i++)
                items[i] = prime_eq_canon(c, items[i], false);
            size_t mark = c->len;
            for (CettaExprIndex p = 0u; p < iter.param_count; p++) {
                Atom *param = items[iter.first_param + p];
                if (prime_eq_push(c, param, true))
                    items[iter.first_param + p] = c->binders[c->len - 1u].atom;
            }
            Atom *own = iter.own ? term->expr.elems[iter.own] : NULL;
            for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len;
                 j++)
                (void)prime_eq_push(c, own->expr.elems[j], false);
            items[iter.body] = prime_eq_canon(c, items[iter.body], false);
            c->len = mark;
            result = c->failed ? term : atom_expr(c->arena, items, len);
            if (!result) {
                c->failed = true;
                result = term;
            }
        }
    } else if (!pattern && prime_new_form(term)) {
        size_t mark = c->len;
        Atom *names = term->expr.elems[1];
        Atom **items = arena_alloc(
            c->arena, sizeof(Atom *) * ((size_t)names->expr.len + 1u));
        if (!items) {
            c->failed = true;
        } else {
            for (CettaExprIndex j = 0u; j < names->expr.len; j++)
                items[j] = prime_eq_push(c, names->expr.elems[j], true)
                    ? c->binders[c->len - 1u].atom : names->expr.elems[j];
            Atom *list = atom_expr(c->arena, items, names->expr.len);
            Atom *body = prime_eq_canon(c, term->expr.elems[2], false);
            result = list && !c->failed
                ? atom_expr3(c->arena, term->expr.elems[0], list, body) : term;
            if (!result) {
                c->failed = true;
                result = term;
            }
        }
        c->len = mark;
    } else {
        CettaExprLen len = atom_authored_len(term);
        Atom **items = NULL;
        for (CettaExprIndex i = 0u; i < len && !c->failed; i++) {
            Atom *child = term->expr.elems[i];
            PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                          : prime_child_role(term, i);
            Atom *next;
            if (role == PRIME_CHILD_PATTERN_PAIRS && child->kind == ATOM_EXPR) {
                Atom **pairs = arena_alloc(
                    c->arena, sizeof(Atom *) * ((size_t)child->expr.len + 1u));
                if (!pairs) {
                    c->failed = true;
                    break;
                }
                for (CettaExprIndex k = 0u; k < child->expr.len; k++) {
                    Atom *pair = child->expr.elems[k];
                    pairs[k] = pair && pair->kind == ATOM_EXPR &&
                                       pair->expr.len == 2u
                        ? atom_expr2(c->arena,
                                     prime_eq_canon(c, pair->expr.elems[0], true),
                                     prime_eq_canon(c, pair->expr.elems[1],
                                                    false))
                        : prime_eq_canon(c, pair, false);
                }
                next = atom_expr(c->arena, pairs, child->expr.len);
            } else {
                next = prime_eq_canon(c, child, role == PRIME_CHILD_PATTERN);
            }
            if (!next) {
                c->failed = true;
                break;
            }
            if ((next != child || len != term->expr.len) && !items) {
                items = arena_alloc(c->arena, sizeof(Atom *) * (size_t)len);
                if (!items) {
                    c->failed = true;
                    break;
                }
                for (CettaExprIndex j = 0u; j < i; j++)
                    items[j] = term->expr.elems[j];
            }
            if (items) items[i] = next;
        }
        if (!c->failed && items) {
            result = atom_expr(c->arena, items, len);
            if (!result) {
                c->failed = true;
                result = term;
            }
        }
    }
    c->nesting--;
    return c->failed ? term : result;
}

/* Whether `term` holds a construct that binds names. */
static bool prime_eq_binds(const Atom *term, uint32_t nesting) {
    if (!term || term->kind != ATOM_EXPR || nesting > PRIME_ELAB_NESTING_LIMIT)
        return false;
    if (prime_semantics_contextual_code(term)) return true;
    /* A written quotation's own names (stage 5, item 3). */
    if (atom_is_quotation(term) && term->expr.len == 3u) return true;
    if (term->expr.len > 0u && term->expr.elems[0] &&
        term->expr.elems[0]->kind == ATOM_SYMBOL) {
        SymbolId head = term->expr.elems[0]->sym_id;
        if (head == g_builtin_syms.prime_lam ||
            head == g_builtin_syms.prime_new ||
            head == g_builtin_syms.map_atom ||
            head == g_builtin_syms.foldl_atom)
            return true;
    }
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        if (prime_eq_binds(term->expr.elems[i], nesting + 1u)) return true;
    return false;
}

bool prime_semantics_binders_eq(Arena *arena, Atom *left, Atom *right) {
    if (!arena || !left || !right || left->kind != ATOM_EXPR ||
        right->kind != ATOM_EXPR ||
        (!prime_eq_binds(left, 0u) && !prime_eq_binds(right, 0u)))
        return false;
    PrimeEqCanon a = {.arena = arena}, b = {.arena = arena};
    Atom *left_form = prime_eq_canon(&a, left, false);
    Atom *right_form = prime_eq_canon(&b, right, false);
    bool failed = a.failed || b.failed;
    free(a.binders);
    free(b.binders);
    return !failed && atom_prime_authored_eq(left_form, right_form);
}

/* ── The scope census (CETTA_PRIME_SCOPE_CENSUS) ─────────────────────────
 *
 * A measurement, not a profile.  While the main document is read, each of
 * its forms is also elaborated under every ownership option (per call, by
 * reference) and the elaborated forms are counted:
 *   - lambdas and map-atom/foldl-atom templates, outside quoted code;
 *   - those with own names, and the own names;
 *   - those with captured names (free store names of the template's body,
 *     which it shares with the scope around it), and the captured names;
 *   - let and let* patterns, and those that refine a name occurring outside
 *     them: one of their names also occurs outside the pattern, its source
 *     and its scope.  Lexical-fresh would give such a pattern a slot of its
 *     own instead.
 * One line per option goes to standard output after the document is read,
 * and the program is not run (main.c). */
typedef struct {
    uint64_t forms, lambdas, iterations, with_own, own_names, with_captured,
        captured_names, lets, refining_lets, refining_names;
} PrimeScopeCensus;

static __thread bool g_prime_scope_reading_document = false;
static __thread bool g_prime_scope_census = false;

void prime_scope_document_reading(bool reading) {
    g_prime_scope_reading_document = reading;
}

void prime_scope_census_set(bool on) {
    g_prime_scope_census = on;
}

static bool prime_scope_census_requested(void) {
    return g_prime_scope_census;
}

typedef struct {
    VarId *ids;
    size_t len, cap;
    bool failed;
} PrimeCensusIds;

/* The variables standing in a term, own lists left out. */
static void prime_census_ids(PrimeCensusIds *out, const Atom *term,
                             uint32_t nesting) {
    if (out->failed || !term) return;
    if (term->kind == ATOM_VAR) {
        if (!prime_env_reserve((void **)&out->ids, &out->cap, out->len + 1u,
                               sizeof(*out->ids))) {
            out->failed = true;
            return;
        }
        out->ids[out->len++] = term->var_id;
        return;
    }
    if (term->kind != ATOM_EXPR || !atom_has_vars(term) ||
        atom_is_prime_own_list(term))
        return;
    if (nesting > PRIME_ENV_NESTING_LIMIT) {
        out->failed = true;
        return;
    }
    for (CettaExprIndex i = 0u; i < term->expr.len && !out->failed; i++)
        prime_census_ids(out, term->expr.elems[i], nesting + 1u);
}

static size_t prime_census_count(const PrimeCensusIds *sorted, VarId id) {
    size_t lo = 0u, hi = sorted->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (sorted->ids[mid] < id) lo = mid + 1u;
        else hi = mid;
    }
    size_t count = 0u;
    while (lo < sorted->len && sorted->ids[lo] == id) {
        count++;
        lo++;
    }
    return count;
}

static void prime_census_sort(PrimeCensusIds *ids) {
    if (ids->len > 1u)
        qsort(ids->ids, ids->len, sizeof(*ids->ids), prime_var_id_cmp);
}

/* One let pattern, the terms inside the let (pattern, source, scope), and
 * the whole form's variables. */
static void prime_census_let(PrimeScopeCensus *c, const PrimeCensusIds *all,
                             Atom *const *inside, size_t inside_count) {
    c->lets++;
    PrimeCensusIds names = {0}, in = {0};
    prime_census_ids(&names, inside[0], 0u);
    for (size_t i = 0u; i < inside_count; i++)
        prime_census_ids(&in, inside[i], 0u);
    if (!names.failed && !in.failed) {
        prime_census_sort(&names);
        prime_census_sort(&in);
        uint64_t refining = 0u;
        for (size_t i = 0u; i < names.len; i++) {
            if (i > 0u && names.ids[i] == names.ids[i - 1u]) continue;
            if (prime_census_count(all, names.ids[i]) >
                prime_census_count(&in, names.ids[i]))
                refining++;
        }
        if (refining > 0u) {
            c->refining_lets++;
            c->refining_names += refining;
        }
    }
    free(names.ids);
    free(in.ids);
}

/* The free store names of a template's body: neither binders (its
 * parameters, the binders and own names of templates in it) nor its own
 * names; quoted code left out. */
typedef struct {
    VarId *hidden;
    size_t hidden_len, hidden_cap;
    VarId *found;
    size_t found_len, found_cap;
    bool failed;
} PrimeCensusFree;

static void prime_census_hide(PrimeCensusFree *f, const Atom *key) {
    if (!key || key->kind != ATOM_VAR) return;
    if (!prime_env_reserve((void **)&f->hidden, &f->hidden_cap,
                           f->hidden_len + 1u, sizeof(*f->hidden))) {
        f->failed = true;
        return;
    }
    f->hidden[f->hidden_len++] = key->var_id;
}

static void prime_census_free_walk(Arena *arena, PrimeCensusFree *f,
                                   Atom *term, uint32_t nesting) {
    if (f->failed || !term || !atom_has_vars(term)) return;
    if (term->kind == ATOM_VAR) {
        for (size_t i = 0u; i < f->hidden_len; i++)
            if (f->hidden[i] == term->var_id) return;
        for (size_t i = 0u; i < f->found_len; i++)
            if (f->found[i] == term->var_id) return;
        if (!prime_env_reserve((void **)&f->found, &f->found_cap,
                               f->found_len + 1u, sizeof(*f->found))) {
            f->failed = true;
            return;
        }
        f->found[f->found_len++] = term->var_id;
        return;
    }
    if (term->kind != ATOM_EXPR || prime_code_like(term) ||
        atom_is_prime_own_list(term))
        return;
    if (nesting > PRIME_ENV_NESTING_LIMIT) {
        f->failed = true;
        return;
    }
    size_t mark = f->hidden_len;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (prime_lambda_telescope(arena, term, &telescope)) {
        for (size_t g = 0u; g < telescope.count; g++)
            for (size_t i = 0u; i < telescope.groups[g].names_count; i++)
                prime_census_hide(f, prime_binder_key(
                    cetta_prime_lambda_binder_name_v1(&telescope.groups[g], i)));
        for (CettaExprIndex j = PRIME_OWN_FIRST;
             telescope.own && j < telescope.own->expr.len; j++)
            prime_census_hide(f, telescope.own->expr.elems[j]);
        prime_census_free_walk(arena, f, term->expr.elems[2], nesting + 1u);
    } else if (prime_iter_template(term, &iter)) {
        for (CettaExprIndex i = 1u; i < iter.first_param; i++)
            prime_census_free_walk(arena, f, term->expr.elems[i],
                                   nesting + 1u);
        for (CettaExprIndex p = 0u; p < iter.param_count; p++)
            prime_census_hide(f, term->expr.elems[iter.first_param + p]);
        Atom *own = iter.own ? term->expr.elems[iter.own] : NULL;
        for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len; j++)
            prime_census_hide(f, own->expr.elems[j]);
        prime_census_free_walk(arena, f, term->expr.elems[iter.body],
                               nesting + 1u);
    } else if (prime_new_form(term)) {
        Atom *names = term->expr.elems[1];
        for (CettaExprIndex j = 0u; j < names->expr.len; j++)
            prime_census_hide(f, names->expr.elems[j]);
        prime_census_free_walk(arena, f, term->expr.elems[2], nesting + 1u);
    } else {
        for (CettaExprIndex i = 0u; i < term->expr.len; i++)
            prime_census_free_walk(arena, f, term->expr.elems[i],
                                   nesting + 1u);
    }
    f->hidden_len = mark;
}

/* An iteration template: the free names of its body, its parameters and own
 * names hidden.  Its list and initial value belong to the scope around it. */
static void prime_census_template(Arena *arena, PrimeScopeCensus *c,
                                  Atom *template_term,
                                  const PrimeIterTemplate *iter,
                                  const Atom *own) {
    size_t own_count = own && own->expr.len > PRIME_OWN_FIRST
        ? (size_t)own->expr.len - PRIME_OWN_FIRST : 0u;
    PrimeCensusFree f = {0};
    for (CettaExprIndex p = 0u; p < iter->param_count; p++)
        prime_census_hide(&f, template_term->expr.elems[iter->first_param + p]);
    for (CettaExprIndex j = PRIME_OWN_FIRST; own && j < own->expr.len; j++)
        prime_census_hide(&f, own->expr.elems[j]);
    prime_census_free_walk(arena, &f, template_term->expr.elems[iter->body],
                           0u);
    size_t captured = f.failed ? 0u : f.found_len;
    free(f.hidden);
    free(f.found);
    c->own_names += own_count;
    c->with_own += own_count > 0u;
    c->captured_names += captured;
    c->with_captured += captured > 0u;
}

static void prime_census_walk(Arena *arena, Atom *term, bool pattern,
                              const PrimeCensusIds *all, PrimeScopeCensus *c,
                              uint32_t nesting) {
    if (!term || term->kind != ATOM_EXPR || nesting > PRIME_ENV_NESTING_LIMIT)
        return;
    if (!pattern && prime_code_like(term)) return;
    PrimeLambdaTelescope telescope;
    PrimeIterTemplate iter;
    if (!pattern && prime_lambda_telescope(arena, term, &telescope)) {
        c->lambdas++;
        Atom *plain = telescope.own
            ? atom_expr3(arena, term->expr.elems[0], term->expr.elems[1],
                         term->expr.elems[2])
            : term;
        if (plain) {
            /* free names of the body: the binders hidden, then the own
             * names */
            PrimeCensusFree f = {0};
            for (size_t g = 0u; g < telescope.count; g++)
                for (size_t i = 0u; i < telescope.groups[g].names_count; i++)
                    prime_census_hide(&f, prime_binder_key(
                        cetta_prime_lambda_binder_name_v1(&telescope.groups[g],
                                                          i)));
            for (CettaExprIndex j = PRIME_OWN_FIRST;
                 telescope.own && j < telescope.own->expr.len; j++)
                prime_census_hide(&f, telescope.own->expr.elems[j]);
            prime_census_free_walk(arena, &f, term->expr.elems[2], 0u);
            size_t own_count = telescope.own &&
                                       telescope.own->expr.len > PRIME_OWN_FIRST
                ? (size_t)telescope.own->expr.len - PRIME_OWN_FIRST : 0u;
            size_t captured = f.failed ? 0u : f.found_len;
            c->own_names += own_count;
            c->with_own += own_count > 0u;
            c->captured_names += captured;
            c->with_captured += captured > 0u;
            free(f.hidden);
            free(f.found);
        }
        for (size_t g = 0u; g < telescope.count; g++) {
            const CettaPrimeLambdaBinderGroupV1 *group = &telescope.groups[g];
            for (size_t t = 0u; group->typed && t < group->types_count; t++)
                prime_census_walk(
                    arena, group->syntax->expr.elems[group->types_start + t],
                    false, all, c, nesting + 1u);
        }
        prime_census_walk(arena, term->expr.elems[2], false, all, c,
                          nesting + 1u);
        return;
    }
    if (!pattern && prime_iter_template(term, &iter)) {
        c->iterations++;
        Atom *own = iter.own ? term->expr.elems[iter.own] : NULL;
        prime_census_template(arena, c, term, &iter, own);
        for (CettaExprIndex i = 1u; i < iter.first_param; i++)
            prime_census_walk(arena, term->expr.elems[i], false, all, c,
                              nesting + 1u);
        prime_census_walk(arena, term->expr.elems[iter.body], false, all, c,
                          nesting + 1u);
        return;
    }
    SymbolId head = term->expr.len > 0u && term->expr.elems[0] &&
                    term->expr.elems[0]->kind == ATOM_SYMBOL
        ? term->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    if (!pattern && head == g_builtin_syms.let && term->expr.len == 4u) {
        Atom *inside[3] = {term->expr.elems[1], term->expr.elems[2],
                           term->expr.elems[3]};
        prime_census_let(c, all, inside, 3u);
    } else if (!pattern && prime_elab_is_let_star(term) &&
               term->expr.elems[1]->kind == ATOM_EXPR) {
        Atom *bindings = term->expr.elems[1];
        Atom **inside = malloc(sizeof(Atom *) *
                               ((size_t)bindings->expr.len + 2u));
        for (CettaExprIndex k = 0u; inside && k < bindings->expr.len; k++) {
            Atom *pair = bindings->expr.elems[k];
            if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u)
                continue;
            size_t count = 0u;
            inside[count++] = pair->expr.elems[0];
            inside[count++] = pair->expr.elems[1];
            for (CettaExprIndex j = k + 1u; j < bindings->expr.len; j++)
                inside[count++] = bindings->expr.elems[j];
            inside[count++] = term->expr.elems[2];
            prime_census_let(c, all, inside, count);
        }
        free(inside);
    }
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        PrimeChildRole role = pattern ? PRIME_CHILD_PATTERN
                                      : prime_child_role(term, i);
        Atom *child = term->expr.elems[i];
        if (role == PRIME_CHILD_PATTERN_PAIRS && child &&
            child->kind == ATOM_EXPR) {
            for (CettaExprIndex k = 0u; k < child->expr.len; k++) {
                Atom *pair = child->expr.elems[k];
                if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
                    prime_census_walk(arena, pair->expr.elems[0], true, all,
                                      c, nesting + 1u);
                    prime_census_walk(arena, pair->expr.elems[1], false, all,
                                      c, nesting + 1u);
                } else {
                    prime_census_walk(arena, pair, false, all, c,
                                      nesting + 1u);
                }
            }
        } else {
            prime_census_walk(arena, child, role == PRIME_CHILD_PATTERN, all,
                              c, nesting + 1u);
        }
    }
}

/* One read form, elaborated under every ownership option and counted. */
static bool prime_scope_census_form(TermUniverse *universe, AtomId id,
                                    PrimeScopeCensus *census) {
    if (tu_kind(universe, id) != ATOM_EXPR) return true;
    Arena arena;
    arena_init(&arena);
    Atom *form = term_universe_copy_atom(universe, &arena, id);
    bool ok = form != NULL;
    for (int o = 0; ok && o < PRIME_SCOPE_OWNERSHIP_COUNT; o++) {
        CettaPrimeScopeProfile profile = {
            .ownership = (uint8_t)o,
            .lifetime = CETTA_PRIME_LIFETIME_PER_CALL,
            .readout = CETTA_PRIME_READOUT_REFERENCE,
        };
        Atom *elaborated = prime_elab_run(&arena, form, profile);
        if (!elaborated) {
            ok = false;
            break;
        }
        PrimeCensusIds all = {0};
        prime_census_ids(&all, elaborated, 0u);
        if (all.failed) {
            free(all.ids);
            ok = false;
            break;
        }
        prime_census_sort(&all);
        census[o].forms++;
        prime_census_walk(&arena, elaborated, false, &all, &census[o], 0u);
        free(all.ids);
    }
    arena_free(&arena);
    return ok;
}

static void prime_scope_census_print(const PrimeScopeCensus *census) {
    for (int o = 0; o < PRIME_SCOPE_OWNERSHIP_COUNT; o++) {
        const PrimeScopeCensus *c = &census[o];
        printf("census\t%s\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu"
               "\t%llu\t%llu\t%llu\n",
               prime_scope_ownership_names[o],
               (unsigned long long)c->forms, (unsigned long long)c->lambdas,
               (unsigned long long)c->iterations,
               (unsigned long long)c->with_own,
               (unsigned long long)c->own_names,
               (unsigned long long)c->with_captured,
               (unsigned long long)c->captured_names,
               (unsigned long long)c->lets,
               (unsigned long long)c->refining_lets,
               (unsigned long long)c->refining_names);
    }
    fflush(stdout);
}

/* Whether elaborating the read form `id` can change it: it has variables
 * and a lambda, an iteration template, a `new`, a quotation, a drop or a
 * meta-argument (a crossing set on a pattern binder makes names fresh);
 * under lexical-fresh, any form with variables (its patterns make slots).
 * -1 when the scan could not finish. */
static int prime_elab_ids_relevant(TermUniverse *universe, AtomId root,
                                   CettaPrimeScopeProfile profile) {
    if (tu_kind(universe, root) != ATOM_EXPR || !tu_has_vars(universe, root))
        return 0;
    if (profile.ownership == CETTA_PRIME_OWNERSHIP_LEXICAL_FRESH) return 1;
    AtomId *stack = NULL;
    size_t len = 0u, cap = 0u;
    int result = 0;
    if (!prime_env_reserve((void **)&stack, &cap, 1u, sizeof(*stack)))
        return -1;
    stack[len++] = root;
    while (len > 0u && result == 0) {
        AtomId id = stack[--len];
        if (tu_kind(universe, id) != ATOM_EXPR || !tu_has_vars(universe, id))
            continue;
        SymbolId head = tu_head_sym(universe, id);
        CettaExprLen arity = tu_arity(universe, id);
        if ((head == g_builtin_syms.quote && arity == 2u) ||
            (head == g_builtin_syms.unquote && arity == 2u) ||
            (head == g_builtin_syms.prime_lam && arity >= 3u) ||
            (head == g_builtin_syms.map_atom && arity >= 4u) ||
            (head == g_builtin_syms.foldl_atom && arity >= 6u) ||
            (head == g_builtin_syms.prime_meta && arity == 3u) ||
            (head == g_builtin_syms.prime_new && arity == 3u)) {
            result = 1;
            break;
        }
        if (!prime_env_reserve((void **)&stack, &cap, len + arity,
                               sizeof(*stack))) {
            result = -1;
            break;
        }
        for (CettaExprIndex i = 0u; i < arity; i++)
            stack[len++] = tu_child(universe, id, i);
    }
    free(stack);
    return result;
}

static bool prime_scope_profile_equal(CettaPrimeScopeProfile a,
                                      CettaPrimeScopeProfile b) {
    return a.ownership == b.ownership && a.lifetime == b.lifetime &&
           a.readout == b.readout;
}

/* A document's forms in order.  A declaration `(scope:profile ...)` written
 * as a bare top-level atom chooses the profile of the whole document; two
 * that disagree refuse the document.  Written as a query,
 * `!(scope:profile ...)`, it chooses the profile from its position onward,
 * as HE's `!(pragma! ...)` does; the query itself, when it runs, sets the
 * profile code formed at run time is elaborated under.  A declaration stays
 * in the document. */
bool prime_semantics_elaborate_read_forms(TermUniverse *universe,
                                          AtomId *forms, size_t count) {
    if (!universe || (count && !forms)) return false;
    g_prime_elab_error[0] = '\0';
    CettaPrimeScopeProfile profile = prime_scope_profile_default();
    bool document_declared = false;
    for (size_t i = 0u; i < count; i++) {
        bool after_bang = i > 0u &&
            tu_kind(universe, forms[i - 1u]) == ATOM_SYMBOL &&
            tu_sym(universe, forms[i - 1u]) == g_builtin_syms.bang;
        bool is_declaration = false;
        CettaPrimeScopeProfile declared;
        if (after_bang ||
            !prime_scope_declaration(universe, forms[i], &declared,
                                     &is_declaration))
            continue;
        if (document_declared &&
            !prime_scope_profile_equal(profile, declared)) {
            snprintf(g_prime_elab_error, sizeof g_prime_elab_error,
                     "two scope:profile declarations of the whole document "
                     "disagree");
            return false;
        }
        profile = declared;
        document_declared = true;
    }
    if (g_prime_scope_reading_document)
        prime_scope_runtime_profile_set(profile);
    Arena scratch;
    arena_init(&scratch);
    bool ok = true;
    bool counting = prime_scope_census_requested() &&
                    g_prime_scope_reading_document;
    PrimeScopeCensus census[PRIME_SCOPE_OWNERSHIP_COUNT];
    memset(census, 0, sizeof census);
    for (size_t i = 0u; i < count && ok; i++) {
        bool after_bang = i > 0u &&
            tu_kind(universe, forms[i - 1u]) == ATOM_SYMBOL &&
            tu_sym(universe, forms[i - 1u]) == g_builtin_syms.bang;
        bool is_declaration = false;
        CettaPrimeScopeProfile declared;
        bool valid = prime_scope_declaration(universe, forms[i], &declared,
                                             &is_declaration);
        if (is_declaration) {
            /* A query declaration applies from here on; its own form is
             * data to elaborate (it holds no variables). */
            if (after_bang && valid) profile = declared;
            continue;
        }
        if (counting && !prime_scope_census_form(universe, forms[i], census))
            ok = false;
        int relevant = prime_elab_ids_relevant(universe, forms[i], profile);
        if (relevant < 0) {
            ok = false;
            break;
        }
        if (relevant == 0) continue;
        ArenaMark mark = arena_mark(&scratch);
        Atom *form = term_universe_copy_atom(universe, &scratch, forms[i]);
        Atom *elaborated = form ? prime_elab_run(&scratch, form, profile)
                                : NULL;
        if (!elaborated) {
            ok = false;
            break;
        }
        if (elaborated != form) {
            AtomId id = term_universe_store_atom_id(universe, &scratch,
                                                    elaborated);
            if (id == CETTA_ATOM_ID_NONE) {
                ok = false;
                break;
            }
            forms[i] = id;
        }
        arena_reset(&scratch, mark);
    }
    arena_free(&scratch);
    if (ok && counting) prime_scope_census_print(census);
    return ok;
}

/* `lift`: named syntax operations on code values.  A quotation is sealed,
 * so code is built from code only by an operation that is named, acts on
 * code values, and seals what it builds.  It runs nothing.
 *
 *   (lift let K @A @C) is the code C with A in the slot K.  K is a binder
 *     key, read as a lambda binder's (prime_binder_key), and its slots are
 *     the references a lambda binder with that key binds: the bare key or
 *     the drop *@K.  The operation is lambda's own substitution
 *     (prime_subst_binder): an inner binder of the same key shadows the
 *     slot, a binder that would capture a name A uses is renamed, and a
 *     quotation nested in C is sealed.  A tuple of keys with a tuple of
 *     codes, (lift let (K1 .. Kn) (@A1 .. @An) @C), fills the slots at once:
 *     each slot is first renamed to a fresh one, then filled.  C may be
 *     contextual code, (quote M (k ...)), a part taken out from under
 *     binders of its code: a slot K that is one of its binders is filled,
 *     and the binders left stay with the result (plain code when none).
 *   (lift app @F @A1 .. @An) is the application code @(F A1 .. An).
 *
 * NULL when an argument is not a code value or a key is not a binder key;
 * the call then stays as written. */

static Atom *prime_code_payload(Atom *value) {
    return atom_is_quotation(value) ? value->expr.elems[1] : NULL;
}

static Atom *prime_code_value(Arena *arena, Atom *payload) {
    return payload
        ? atom_expr2(arena, atom_symbol_id(arena, g_builtin_syms.quote),
                     payload)
        : NULL;
}

static bool prime_lift_anonymous_key(Atom *key) {
    return key && key->kind == ATOM_SYMBOL && is_symbol_named(key, "_");
}

/* The result of `lift let` on `code`: the payload `body`, under the
 * binders of `code`'s list (contextual code) that no slot filled; plain
 * code when none remains. */
static Atom *prime_lift_result(Arena *arena, Atom *code, Atom *body,
                               Atom *const *filled, size_t filled_count) {
    if (!body) return NULL;
    if (!prime_semantics_contextual_code(code))
        return prime_code_value(arena, body);
    Atom *list = code->expr.elems[2];
    Atom **rest = arena_alloc(arena, sizeof(Atom *) * (size_t)list->expr.len);
    if (!rest) return NULL;
    size_t count = 0u;
    for (CettaExprIndex i = 0u; i < list->expr.len; i++) {
        Atom *key = prime_binder_key(list->expr.elems[i]);
        bool gone = false;
        for (size_t j = 0u; j < filled_count && !gone; j++)
            gone = filled[j] && prime_same_key(filled[j], key);
        if (!gone) rest[count++] = list->expr.elems[i];
    }
    if (count == 0u) return prime_code_value(arena, body);
    Atom *remaining = atom_expr(arena, rest, (CettaExprLen)count);
    return remaining
        ? atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.quote), body,
                     remaining)
        : NULL;
}

static Atom *prime_lift_let_in_code(Arena *arena, Atom *keys, Atom *codes,
                                    Atom *code) {
    if (!arena || !keys || !codes || !prime_code_like(code)) return NULL;
    if (prime_lift_anonymous_key(keys))
        return prime_code_payload(codes) ? code : NULL;
    /* The instance `lift let` makes is an opening of the code: the names
     * a written quotation owns are copied for it (prime_code_opening). */
    Atom *body = prime_code_opening(arena, code);
    if (!body) return NULL;
    Atom *key = prime_binder_key(keys);
    if (key) {
        Atom *value = prime_code_payload(codes);
        return value
            ? prime_lift_result(arena, code,
                                prime_subst_binder(arena, body, key, value),
                                &key, 1u)
            : NULL;
    }
    if (keys->kind != ATOM_EXPR || keys->expr.len == 0u ||
        codes->kind != ATOM_EXPR || codes->expr.len != keys->expr.len)
        return NULL;
    size_t count = keys->expr.len;
    Atom **slots = arena_alloc(arena, sizeof(Atom *) * count);
    Atom **values = arena_alloc(arena, sizeof(Atom *) * count);
    Atom **fresh = arena_alloc(arena, sizeof(Atom *) * count);
    if (!slots || !values || !fresh) return NULL;
    for (size_t i = 0u; i < count; i++) {
        Atom *element = keys->expr.elems[i];
        values[i] = prime_code_payload(codes->expr.elems[i]);
        if (!values[i]) return NULL;
        slots[i] = prime_lift_anonymous_key(element)
            ? NULL : prime_binder_key(element);
        if (!slots[i] && !prime_lift_anonymous_key(element)) return NULL;
        /* Each slot is named once, as a pattern variable is in `let`. */
        for (size_t j = 0u; slots[i] && j < i; j++)
            if (slots[j] && prime_same_key(slots[i], slots[j])) return NULL;
    }
    for (size_t i = 0u; i < count; i++) {
        if (!slots[i]) continue;
        fresh[i] = atom_var_with_id(arena, "slot", fresh_var_id());
        body = fresh[i]
            ? prime_subst_binder(arena, body, slots[i], fresh[i]) : NULL;
        if (!body) return NULL;
    }
    for (size_t i = 0u; i < count; i++) {
        if (!slots[i]) continue;
        body = prime_subst_binder(arena, body, fresh[i], values[i]);
        if (!body) return NULL;
    }
    return prime_lift_result(arena, code, body, slots, count);
}

/* `lift let` acts on code: a binder its substitution forms there is
 * sealed, and a lambda stays syntax until the code is opened. */
Atom *prime_semantics_lift_let(Arena *arena, Atom *keys, Atom *codes,
                               Atom *code) {
    g_prime_forming_in_code++;
    Atom *result = prime_lift_let_in_code(arena, keys, codes, code);
    g_prime_forming_in_code--;
    return result;
}

Atom *prime_semantics_lift_app(Arena *arena, Atom *const *codes,
                               size_t count) {
    if (!arena || !codes || count == 0u)
        return NULL;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * count);
    if (!items) return NULL;
    for (size_t i = 0u; i < count; i++) {
        items[i] = prime_code_payload(codes[i]);
        if (!items[i]) return NULL;
    }
    /* The application code is sealed as the reader seals code: a binder
     * formed here (@let applied to a pattern and a body) seals the quoted
     * mentions of its pattern's names in the body. */
    return prime_seal_formed(
        arena, prime_code_value(arena,
                                atom_expr(arena, items, (CettaExprLen)count)));
}

/* Identity elimination on reflexivity returns the method. The six arguments
 * are carrier, point, motive, method, target, and path, which is the order
 * stored for `id:eliminate`. */
Atom *prime_semantics_identity_iota(Atom *call) {
    if (!call || call->kind != ATOM_EXPR || call->expr.len != 7u ||
        !call->expr.elems[0] ||
        !is_symbol_named(call->expr.elems[0], "id:eliminate"))
        return NULL;
    Atom *point = call->expr.elems[2];
    Atom *method = call->expr.elems[4];
    Atom *target = call->expr.elems[5];
    Atom *path = call->expr.elems[6];
    if (!point || !method || !target || !path ||
        path->kind != ATOM_EXPR || path->expr.len != 2u ||
        !is_symbol_named(path->expr.elems[0], "refl") ||
        !atom_eq(path->expr.elems[1], point) ||
        !atom_eq(target, point))
        return NULL;
    return method;
}

/* True when the kernel term `term`, under `depth` binders, has an index no
 * binder of its own binds.  Such a term has no authored reading. */
static bool prime_kernel_term_has_free_index(const Atom *term, uint64_t depth) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u) return false;
    if (term->expr.len == 2u && is_symbol_named(term->expr.elems[0], "idx")) {
        const Atom *index = term->expr.elems[1];
        return !index || index->kind != ATOM_GROUNDED ||
               index->ground.gkind != GV_INT || index->ground.ival < 0 ||
               (uint64_t)index->ground.ival >= depth;
    }
    if (is_symbol_named(term->expr.elems[0], "DeclConst")) return false;
    for (CettaExprIndex i = 1u; i < term->expr.len; i++) {
        bool under =
            (term->expr.len == 2u && i == 1u &&
             is_symbol_named(term->expr.elems[0], "Lam")) ||
            (term->expr.len == 3u && i == 2u &&
             (is_symbol_named(term->expr.elems[0], "Lam") ||
              is_symbol_named(term->expr.elems[0], "Pi") ||
              is_symbol_named(term->expr.elems[0], "Sigma")));
        if (prime_kernel_term_has_free_index(term->expr.elems[i],
                                             under ? depth + 1u : depth))
            return true;
    }
    return false;
}

/* A reduct is returned only in the evaluated call's own vocabulary: every
 * constant it names is one the call's elaboration resolved (the call's
 * constants, and the open parameters it admitted), one the space declares,
 * or one the language owns, under that name and with no universe argument
 * written.  A rule's internal lowering, such as a quantifier instance
 * specialized for the proof checker or an explicit universe argument, is not
 * a term the program can read back, so the call is left as it is. */
static bool prime_reduct_in_vocabulary(
    Space *space, Arena *arena,
    const PrimeRegularDeclarationContext *declarations, Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u) return true;
    if (is_symbol_named(term->expr.elems[0], "DeclConst")) {
        Atom *name = term->expr.len == 2u ? term->expr.elems[1] : NULL;
        if (!name || name->kind != ATOM_SYMBOL) return false;
        if (prime_regular_declaration_context_contains(declarations, name) ||
            prime_language_owned_declaration(arena, name))
            return true;
        Atom **declared = NULL;
        uint32_t count = space_get_declared_types(space, arena, name, &declared);
        free(declared);
        return count > 0u;
    }
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        if (!prime_reduct_in_vocabulary(space, arena, declarations,
                                        term->expr.elems[i]))
            return false;
    return true;
}

static bool prime_head_has_type_rule(Arena *arena, Space *space, Atom *head) {
    Atom *items[5] = {
        atom_symbol(arena, "type:rule"), head, atom_var(arena, "n"),
        atom_var(arena, "p"), atom_var(arena, "r")};
    Atom *pattern = atom_expr(arena, items, 5u);
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    bool admitted = false;
    for (CettaIndex i = 0u; i < count && !admitted; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        admitted = prime_kernel_rule_atom(atom) &&
                   atom_eq(atom->expr.elems[1], head) &&
                   prime_scoped_judgment_admitted(arena, space, atom);
    }
    free(candidates);
    return admitted;
}

/* The equation by which a definition admitted through its scrutinee-first
 * form passes its arguments to that form, `name x1 ... xn = form xp1 ... xpn`:
 * the one rule of `name`, whose patterns are its variables and whose right
 * side applies `form` to a permutation of them, where the admitted record of
 * `name` names `form` as its scrutinee-first form.  An equation of that shape
 * that a program wrote itself has no such record and is not this one. */
static Atom *prime_scrutinee_first_passing_rule(
    Arena *arena, Space *space, Atom *rules, Atom *head, Atom **form_out) {
    Atom *found = NULL;
    for (Atom *r = rules; r && r->kind == ATOM_EXPR && r->expr.len == 3u &&
             is_symbol_named(r->expr.elems[0], "LCons");
         r = r->expr.elems[2]) {
        Atom *rule = r->expr.elems[1];
        if (!rule || rule->kind != ATOM_EXPR || rule->expr.len != 5u ||
            !atom_eq(rule->expr.elems[1], head))
            continue;
        if (found) return NULL;
        found = rule;
    }
    if (!found) return NULL;
    Atom *arity = found->expr.elems[2];
    Atom *pats = found->expr.elems[3];
    if (arity->kind != ATOM_GROUNDED || arity->ground.gkind != GV_INT ||
        arity->ground.ival < 1 || pats->kind != ATOM_EXPR ||
        pats->expr.len != (CettaExprLen)arity->ground.ival)
        return NULL;
    size_t n = (size_t)arity->ground.ival;
    for (size_t i = 0u; i < n; i++) {
        Atom *pat = pats->expr.elems[i];
        if (pat->kind != ATOM_EXPR || pat->expr.len != 2u ||
            !is_symbol_named(pat->expr.elems[0], "PVar") ||
            pat->expr.elems[1]->kind != ATOM_GROUNDED ||
            pat->expr.elems[1]->ground.ival != (int64_t)i)
            return NULL;
    }
    Atom *cursor = found->expr.elems[4];
    bool *seen = arena_alloc(arena, sizeof(bool) * n);
    if (!seen) return NULL;
    memset(seen, 0, sizeof(bool) * n);
    for (size_t i = 0u; i < n; i++) {
        if (cursor->kind != ATOM_EXPR || cursor->expr.len != 3u ||
            !is_symbol_named(cursor->expr.elems[0], "App"))
            return NULL;
        Atom *arg = cursor->expr.elems[2];
        if (arg->kind != ATOM_EXPR || arg->expr.len != 2u ||
            !is_symbol_named(arg->expr.elems[0], "PVar") ||
            arg->expr.elems[1]->kind != ATOM_GROUNDED ||
            arg->expr.elems[1]->ground.ival < 0 ||
            (size_t)arg->expr.elems[1]->ground.ival >= n ||
            seen[arg->expr.elems[1]->ground.ival])
            return NULL;
        seen[arg->expr.elems[1]->ground.ival] = true;
        cursor = cursor->expr.elems[1];
    }
    if (cursor->kind != ATOM_EXPR || cursor->expr.len < 2u ||
        !is_symbol_named(cursor->expr.elems[0], "DeclConst"))
        return NULL;
    Atom *form = cursor->expr.elems[1];
    if (!form || form->kind != ATOM_SYMBOL || atom_eq(form, head))
        return NULL;
    Atom *items[8] = {
        atom_symbol(arena, "set:known"), head, atom_var(arena, "p"),
        atom_symbol(arena, "definition"), atom_var(arena, "f"),
        atom_var(arena, "d"), atom_var(arena, "r"), atom_var(arena, "s")};
    Atom *pattern = atom_expr(arena, items, 8u);
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    bool named = false;
    for (CettaIndex i = 0u; i < count && !named; i++) {
        Atom *record = space_match_candidate_at64(space, candidates[i]);
        if (!record || record->kind != ATOM_EXPR || record->expr.len != 8u ||
            !atom_eq(record->expr.elems[1], head) ||
            !is_symbol_named(record->expr.elems[3], "definition"))
            continue;
        Atom *evidence = record->expr.elems[4];
        Atom *termination = evidence && evidence->kind == ATOM_EXPR &&
                evidence->expr.len >= 3u &&
                is_symbol_named(evidence->expr.elems[0], "definition-evidence")
            ? evidence->expr.elems[2] : NULL;
        named = termination && termination->kind == ATOM_EXPR &&
                termination->expr.len == 3u &&
                is_symbol_named(termination->expr.elems[0], "scrutinee-first") &&
                atom_eq(termination->expr.elems[1], form) &&
                prime_scoped_judgment_admitted(arena, space, record);
    }
    free(candidates);
    if (!named) return NULL;
    *form_out = form;
    return found;
}

/* Every full application of the scrutinee-first `form` in `term`, read back
 * as the authored call it is equal to by the passing equation `rule`:
 * `form b1 ... bn` is `name a1 ... an` with a(p_j) = b_j when the equation's
 * right side passes argument p_j in position j.  `call_head` is the call's
 * own constant, levels included. */
static Atom *prime_read_back_scrutinee_first(
    Arena *arena, Atom *term, Atom *form, Atom *rule, Atom *call_head) {
    if (!term || term->kind != ATOM_EXPR) return term;
    size_t n = (size_t)rule->expr.elems[2]->ground.ival;
    size_t argc = 0u;
    Atom *cursor = term;
    while (cursor->kind == ATOM_EXPR && cursor->expr.len == 3u &&
           is_symbol_named(cursor->expr.elems[0], "App")) {
        argc++;
        cursor = cursor->expr.elems[1];
    }
    if (argc == n && cursor->kind == ATOM_EXPR && cursor->expr.len >= 2u &&
        is_symbol_named(cursor->expr.elems[0], "DeclConst") &&
        atom_eq(cursor->expr.elems[1], form)) {
        Atom **authored = arena_alloc(arena, sizeof(Atom *) * n);
        if (!authored) return NULL;
        Atom *spine = term;
        Atom *passing = rule->expr.elems[4];
        for (size_t j = n; j > 0u; j--) {
            Atom *argument = prime_read_back_scrutinee_first(
                arena, spine->expr.elems[2], form, rule, call_head);
            if (!argument) return NULL;
            authored[passing->expr.elems[2]->expr.elems[1]->ground.ival] = argument;
            spine = spine->expr.elems[1];
            passing = passing->expr.elems[1];
        }
        Atom *read = call_head;
        for (size_t i = 0u; i < n; i++)
            read = atom_expr3(arena, atom_symbol(arena, "App"), read, authored[i]);
        return read;
    }
    Atom **children = arena_alloc(arena, sizeof(Atom *) * term->expr.len);
    if (!children) return NULL;
    bool changed = false;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        children[i] = prime_read_back_scrutinee_first(
            arena, term->expr.elems[i], form, rule, call_head);
        if (!children[i]) return NULL;
        changed = changed || children[i] != term->expr.elems[i];
    }
    return changed ? atom_expr(arena, children, term->expr.len) : term;
}

/* The rule firings of the last covered call this thread reduced. */
static __thread uint64_t g_prime_covered_call_firings = 1u;

static Atom *prime_written_sort_kernel(Arena *arena, Atom *sort) {
    bool universe = sort && sort->kind == ATOM_EXPR && sort->expr.len == 2u &&
                    is_symbol_named(sort->expr.elems[0], "u");
    if (!universe && !cetta_prime_regular_kernel_sort_above_spelling_v1(arena, sort, NULL))
        return NULL;
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, 4096u);
    CettaPrimeRegularTermElaborationV1 lowered =
        cetta_prime_regular_term_to_pattern_v1(arena, sort, &budget);
    if (lowered.status != CETTA_PRIME_REGULAR_TERM_OK) return NULL;
    CettaPrimeRegularPatternEnvironmentV1 closed = {0};
    CettaPrimeRegularPatternElaborationV1 elaborated =
        cetta_prime_regular_pattern_elaborate_v1(arena, closed, lowered.pattern, &budget);
    return elaborated.status == CETTA_PRIME_REGULAR_PATTERN_OK ? elaborated.term : NULL;
}

bool prime_semantics_written_sort_within(Arena *arena, Atom *lower, Atom *upper) {
    Atom *low = prime_written_sort_kernel(arena, lower);
    Atom *high = low ? prime_written_sort_kernel(arena, upper) : NULL;
    return high && cetta_prime_regular_kernel_sort_within_v1(arena, low, high);
}

uint64_t prime_semantics_covered_call_firings(void) {
    return g_prime_covered_call_firings;
}

static Atom *prime_reduce_covered_call(
    Arena *arena, Space *space, Atom *call, int fuel, bool admit_open) {
    if (!arena || !space || !call || fuel == 0 ||
        call->kind != ATOM_EXPR || call->expr.len < 2u)
        return NULL;
    /* A projection of a pair, unless `fst`, `snd` or `pair` there is the
     * program's own function or constructor. */
    Atom *projected = prime_authored_projection(arena, call);
    if (projected &&
        !prime_program_declares(space, arena, call->expr.elems[0], 1u) &&
        !prime_program_declares(space, arena,
                                call->expr.elems[1]->expr.elems[0], 2u))
        return projected;
    Atom *head = call->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL) return NULL;
    if (!prime_head_has_type_rule(arena, space, head))
        return NULL;
    /* An admitted oracle declaration of this operation computes the call
     * natively when its arguments are closed numerals of their admitted
     * readings: one step, to the numeral the declared equations specify
     * (prime_arith_oracle.h).  Without that declaration, or at any other
     * argument, the call steps by its equations as written. */
    if (!admit_open) {
        Atom *native = prime_arith_oracle_answer(arena, space, call);
        if (native) {
            g_prime_covered_call_firings = 1u;
            return native;
        }
    }

    /* A negative evaluator fuel is unlimited. One covered call still has a
     * finite kernel budget. Running that budget out leaves the call in place. */
    uint64_t steps = fuel < 0 || (uint64_t)fuel > 100000u
        ? 100000u : (uint64_t)fuel;
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, steps);
    PrimeRegularDeclarationContext declarations = {0};
    bool saved_open = g_prime_admit_open_parameters;
    unsigned saved_budget = g_prime_open_parameter_budget;
    g_prime_admit_open_parameters = admit_open;
    if (admit_open)
        g_prime_open_parameter_budget = 8u;
    PrimeRegularDeclaredElaboration elaborated =
        prime_elaborate_declared_regular_term(
            space, arena, call, &declarations, &budget);
    g_prime_admit_open_parameters = saved_open;
    g_prime_open_parameter_budget = saved_budget;
    if (elaborated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        declarations.count == 0u || !elaborated.lowered.pattern) {
        prime_regular_declaration_context_free(&declarations);
        return NULL;
    }
    PrimeRegularDeclarationOccurrenceResult instantiated =
        prime_regular_declaration_instantiate_occurrences_rec(
            arena, &declarations, elaborated.lowered.pattern, &budget);
    if (instantiated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !instantiated.pattern) {
        prime_regular_declaration_context_free(&declarations);
        return NULL;
    }
    CettaPrimeRegularPatternEnvironmentV1 pattern_environment = {0};
    CettaPrimeRegularPatternElaborationV1 intrinsic =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, pattern_environment, instantiated.pattern, &budget);
    if (intrinsic.status != CETTA_PRIME_REGULAR_PATTERN_OK || !intrinsic.term) {
        prime_regular_declaration_context_free(&declarations);
        return NULL;
    }
    /* The rule list is read only while this step computes.  The kernel
     * walks it, and the scrutinee-first form reads its passing rule from it;
     * what they return holds no cell of the list, since a rule's head,
     * arity, patterns and right side are the space's own atoms and every
     * term they build is built in `arena`.  So the list lives in a scratch
     * arena of its own, freed as soon as the kernel no longer has it, and a
     * long evaluation does not keep one list per covered call. */
    Arena rule_arena;
    arena_init_detached(&rule_arena);
    Atom *rules = prime_semantics_kernel_rules(&rule_arena, space);
    cetta_prime_regular_kernel_rules_set(rules);
    g_prime_covered_call_firings = 1u;
    /* A definition admitted on a set solution unfolds only at closed
     * arguments, and its right side may hold a partial application (a
     * stream's tail): a call of it is computed in the kernel to its normal
     * form at once, every rule firing on the way counted (`cost:firings`). */
    if (!admit_open && cetta_prime_regular_kernel_rule_guarded_v1(head)) {
        uint64_t firings = 0u;
        Atom *normal = cetta_prime_regular_kernel_rule_normal_form_v1(
            arena, intrinsic.term, &budget, &firings);
        cetta_prime_regular_kernel_rules_set(NULL);
        arena_free(&rule_arena);
        g_prime_covered_call_firings = firings;
        Atom *reached = normal && firings > 0u ? normal : NULL;
        bool readable = reached &&
            !prime_kernel_term_has_free_index(reached, 0u) &&
            prime_reduct_in_vocabulary(space, arena, &declarations, reached);
        prime_regular_declaration_context_free(&declarations);
        if (!readable) return NULL;
        Atom *shared = prime_quote_shared_redex(arena, reached);
        Atom *quoted = shared ? shared : prime_quote_runtime_term(arena, reached);
        return quoted && !atom_eq(quoted, call) ? quoted : NULL;
    }
    /* An argument the rule inspects that is a call waiting for an
     * observation, such as the iteration of a stream under its head or its
     * tail, unfolds in the step: that unfolding is a firing of its equation,
     * counted with the call's own (`cost:firings`). */
    uint64_t firings = 0u;
    Atom *contractum = cetta_prime_regular_kernel_rule_contractum_counted_v1(
        arena, intrinsic.term, &budget, &firings);
    if (firings > 1u) g_prime_covered_call_firings = firings;
    /* A definition admitted through its scrutinee-first form runs as it was
     * written: the equation passing its arguments to that form is applied in
     * the same step as the rule it hands the call to, and the result is read
     * back as authored calls, so one step is one authored equation.  When no
     * rule of the form applies, no authored equation does either, and the
     * call stays as written.  Comparison of types keeps the plain steps. */
    Atom *form = NULL;
    Atom *passing = contractum && !admit_open
        ? prime_scrutinee_first_passing_rule(arena, space, rules, head, &form)
        : NULL;
    if (passing) {
        Atom *call_head = intrinsic.term;
        while (call_head->kind == ATOM_EXPR && call_head->expr.len == 3u &&
               is_symbol_named(call_head->expr.elems[0], "App"))
            call_head = call_head->expr.elems[1];
        Atom *handed = cetta_prime_regular_kernel_rule_contractum_v1(
            arena, contractum, &budget);
        contractum = handed
            ? prime_read_back_scrutinee_first(arena, handed, form, passing, call_head)
            : NULL;
    }
    cetta_prime_regular_kernel_rules_set(NULL);
    arena_free(&rule_arena);
    /* The kernel term is quoted form by form: a form with no authored
     * reading, such as a pattern slot or an index no binder binds, makes the
     * quotation fail and the call stays as written.  The authored reading is
     * not inspected afterwards for kernel spellings: a constructor the
     * program declared may be spelled like a kernel form. */
    bool readable = contractum &&
        !prime_kernel_term_has_free_index(contractum, 0u) &&
        prime_reduct_in_vocabulary(space, arena, &declarations, contractum);
    prime_regular_declaration_context_free(&declarations);
    if (!readable) return NULL;
    Atom *shared = prime_quote_shared_redex(arena, contractum);
    Atom *quoted = shared ? shared : prime_quote_runtime_term(arena, contractum);
    if (!quoted || atom_eq(quoted, call))
        return NULL;
    return quoted;
}

Atom *prime_semantics_reduce_covered_call(
    Arena *arena, Space *space, Atom *call, int fuel) {
    return prime_reduce_covered_call(arena, space, call, fuel, false);
}

/* Same one-equation step, but a symbol with no declaration is a parameter of
 * the type being compared. Ordinary execution does not use this entry. */
Atom *prime_semantics_reduce_open_covered_call(
    Arena *arena, Space *space, Atom *call, int fuel) {
    return prime_reduce_covered_call(arena, space, call, fuel, true);
}

/* The one step of a constant defined with no arguments, `set:define` of `c`
 * with `(= c v)`: the right side of its admitted rule of arity zero, in the
 * authored spelling.  The kernel unfolds the same rule when it compares
 * terms, so running the constant agrees with its typing.  NULL when no
 * admitted rule of arity zero names `name`, or its right side has no
 * authored spelling. */
Atom *prime_semantics_unfold_covered_constant(Arena *arena, Space *space,
                                              Atom *name) {
    if (!arena || !space || !name || name->kind != ATOM_SYMBOL)
        return NULL;
    Atom *items[5] = {
        atom_symbol(arena, "type:rule"), name, atom_var(arena, "n"),
        atom_var(arena, "p"), atom_var(arena, "r")};
    Atom *pattern = atom_expr(arena, items, 5u);
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    Atom *right = NULL;
    for (CettaIndex i = 0u; i < count && !right; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        if (!prime_kernel_rule_atom(atom) || !atom_eq(atom->expr.elems[1], name))
            continue;
        Atom *arity = atom->expr.elems[2];
        if (arity->kind != ATOM_GROUNDED || arity->ground.gkind != GV_INT ||
            arity->ground.ival != 0)
            continue;
        if (prime_scoped_judgment_admitted(arena, space, atom))
            right = atom->expr.elems[4];
    }
    free(candidates);
    if (!right || prime_kernel_term_has_free_index(right, 0u)) return NULL;
    Atom *shared = prime_quote_shared_redex(arena, right);
    Atom *quoted = shared ? shared : prime_quote_runtime_term(arena, right);
    return quoted && !atom_eq(quoted, name) ? quoted : NULL;
}

/* The normal form of a closed call by the space's admitted rules alone, in
 * the authored spelling, computed in the kernel with at most `steps` steps.
 * No oracle takes part: the kernel computes only by rules.  NULL when the
 * head has no admitted rules, the call does not elaborate, the steps run
 * out (`*exhausted` is then set) or the normal form has no authored
 * spelling. */
Atom *prime_semantics_rules_normal_form(Arena *arena, Space *space,
                                        Atom *call, uint64_t steps,
                                        bool *exhausted) {
    if (exhausted) *exhausted = false;
    if (!arena || !space || !call || call->kind != ATOM_EXPR ||
        call->expr.len < 2u)
        return NULL;
    Atom *head = call->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL ||
        !prime_head_has_type_rule(arena, space, head))
        return NULL;
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, 100000u);
    PrimeRegularDeclarationContext declarations = {0};
    bool saved_open = g_prime_admit_open_parameters;
    g_prime_admit_open_parameters = false;
    PrimeRegularDeclaredElaboration elaborated =
        prime_elaborate_declared_regular_term(
            space, arena, call, &declarations, &budget);
    g_prime_admit_open_parameters = saved_open;
    if (elaborated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        declarations.count == 0u || !elaborated.lowered.pattern) {
        prime_regular_declaration_context_free(&declarations);
        return NULL;
    }
    PrimeRegularDeclarationOccurrenceResult instantiated =
        prime_regular_declaration_instantiate_occurrences_rec(
            arena, &declarations, elaborated.lowered.pattern, &budget);
    if (instantiated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !instantiated.pattern) {
        prime_regular_declaration_context_free(&declarations);
        return NULL;
    }
    CettaPrimeRegularPatternEnvironmentV1 pattern_environment = {0};
    CettaPrimeRegularPatternElaborationV1 intrinsic =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, pattern_environment, instantiated.pattern, &budget);
    if (intrinsic.status != CETTA_PRIME_REGULAR_PATTERN_OK || !intrinsic.term) {
        prime_regular_declaration_context_free(&declarations);
        return NULL;
    }
    Arena rule_arena;
    arena_init_detached(&rule_arena);
    cetta_prime_regular_kernel_rules_set(
        prime_semantics_kernel_rules(&rule_arena, space));
    CettaPrimeRegularKernelBudget normalizing;
    cetta_prime_regular_kernel_budget_init(&normalizing, true, steps);
    uint64_t firings = 0u;
    Atom *normal = cetta_prime_regular_kernel_rule_normal_form_v1(
        arena, intrinsic.term, &normalizing, &firings);
    cetta_prime_regular_kernel_rules_set(NULL);
    arena_free(&rule_arena);
    if (!normal) {
        if (exhausted) *exhausted = normalizing.remaining == 0u;
        prime_regular_declaration_context_free(&declarations);
        return NULL;
    }
    bool readable = !prime_kernel_term_has_free_index(normal, 0u) &&
        prime_reduct_in_vocabulary(space, arena, &declarations, normal);
    prime_regular_declaration_context_free(&declarations);
    if (!readable) return NULL;
    Atom *shared = prime_quote_shared_redex(arena, normal);
    return shared ? shared : prime_quote_runtime_term(arena, normal);
}

/* A refutation by the conversion algorithm assumes the algorithm complete
 * for the package.  Lean proves that for the bare tower and the object
 * package; where the comparison meets other declarations, the reason says
 * which kinds (prime_scoped_judgment_completeness_assumed), and a refutation
 * inside the proven packages prints no qualifier.  A refutation because the
 * two sides' types are apart rests on the same assumption: the algorithm
 * found the types unrelated.  Where Lean refutes the equation itself, with no
 * assumption about the algorithm, the reason names that theorem instead
 * (prime_scoped_judgment_refutation_without_completeness: the empty list
 * against a non-empty one). */
static Atom *prime_refutation_assumptions(Arena *a, Space *space, Atom *judgment,
                                          Atom *verdict) {
    if (!verdict || verdict->kind != ATOM_EXPR || verdict->expr.len != 4u ||
        !is_symbol_named(verdict->expr.elems[0], "PrimeVerdict") ||
        !is_symbol_named(verdict->expr.elems[1], "Refuted"))
        return verdict;
    Atom *reason = verdict->expr.elems[3];
    if (!reason || reason->kind != ATOM_EXPR || reason->expr.len != 2u)
        return verdict;
    bool unrelated = is_symbol_named(reason->expr.elems[0], "not-convertible");
    bool types_apart =
        is_symbol_named(reason->expr.elems[0], "conversion-type-mismatch");
    if (!unrelated && !types_apart)
        return verdict;
    Atom *operands = unquote_data(judgment);
    if (!operands || operands->kind != ATOM_EXPR || operands->expr.len != 3u)
        return verdict;
    Atom *assumed = prime_scoped_judgment_completeness_assumed(
        a, space, operands->expr.elems[1], operands->expr.elems[2]);
    if (!assumed) return verdict;
    const char *theorem = unrelated
        ? prime_scoped_judgment_refutation_without_completeness(
              a, space, operands->expr.elems[1], operands->expr.elems[2])
        : NULL;
    if (theorem)
        return prime_verdict(a, "Refuted", verdict->expr.elems[2],
                             prime_expr2(a, "not-convertible", prime_sym(a, theorem)));
    return prime_verdict(
        a, "Refuted", verdict->expr.elems[2],
        prime_expr3(a, unrelated ? "not-convertible" : "conversion-type-mismatch",
                    reason->expr.elems[1], assumed));
}

/* The status word of a verdict, or NULL. */
static const char *prime_verdict_status_name(Atom *verdict) {
    if (!verdict || verdict->kind != ATOM_EXPR || verdict->expr.len != 4u ||
        !is_symbol_named(verdict->expr.elems[0], "PrimeVerdict") ||
        verdict->expr.elems[1]->kind != ATOM_SYMBOL)
        return NULL;
    return atom_name_cstr(verdict->expr.elems[1]);
}

static bool prime_verdict_is(Atom *verdict, const char *status) {
    const char *name = prime_verdict_status_name(verdict);
    return name && strcmp(name, status) == 0;
}

/* A judgment whose terms write constructors of a datatype with parameters
 * without them is decided on its elaborated form, where they are written
 * (prime_scoped_implicit_judgment).  What that form establishes stands.
 * Otherwise the judgment as written is decided too, since a use read as
 * implicit may also read as the explicit constant applied to some of its
 * parameters; what it establishes stands.  A refutation stands when both
 * are refuted and every parameter was determined.  Anything else is
 * undecided: the parameters chosen, or left open, are not the only ones. */
static Atom *prime_implicit_verdict(Arena *a, Atom *judgment,
                                    const PrimeImplicitReport *report,
                                    Atom *elaborated, Atom *written) {
    if (prime_verdict_is(elaborated, "Established")) return elaborated;
    if (prime_verdict_is(written, "Established")) return written;
    if (prime_verdict_is(elaborated, "Refuted") && prime_verdict_is(written, "Refuted") &&
        !report->unforced && !report->unsolved)
        return elaborated;
    if (prime_verdict_is(elaborated, "Incomplete")) return elaborated;
    if (prime_verdict_is(written, "Incomplete")) return written;
    Atom *evidence = elaborated && elaborated->kind == ATOM_EXPR && elaborated->expr.len == 4u
        ? elaborated->expr.elems[3] : NULL;
    return prime_undetermined(
        a, judgment,
        prime_expr3(a, "implicit-parameters",
                    prime_sym(a, report->unsolved ? "not-determined" : "chosen"),
                    evidence ? evidence : prime_sym(a, "no-verdict")));
}

static Atom *prime_judge_accounted(
    Arena *a, Space *space, Atom *judgment, bool steps_limited,
    uint64_t steps, CettaPrimeTypingResourceObservationV1 *resources_out,
    Atom **canonical_term_out) {
    if (canonical_term_out) *canonical_term_out = NULL;
    /* A space that declares stored atoms typed is read with what they
     * give: the occurrences, their type and their argument maps. */
    space = prime_scoped_stored_space(a, space);
    PrimeImplicitReport implicit = {0};
    Atom *explicit_judgment = prime_scoped_implicit_judgment(a, space, judgment, &implicit);
    PrimeResourceLedger ledger;
    prime_resource_init(&ledger, steps_limited, steps);
    cetta_prime_regular_kernel_rules_set(prime_semantics_kernel_rules(a, space));
    const char *outer_unread_level = prime_unread_level_begin();
    Atom *verdict = prime_judge_raw(
        a, space, explicit_judgment, &ledger, canonical_term_out);
    verdict = prime_unread_level_end(a, verdict, outer_unread_level);
    Atom *written = NULL;
    Atom *written_canonical = NULL;
    bool implicit_uses = explicit_judgment != judgment || implicit.unsolved;
    if (implicit_uses && !prime_verdict_is(verdict, "Established")) {
        const char *outer = prime_unread_level_begin();
        written = prime_judge_raw(
            a, space, judgment, &ledger, canonical_term_out ? &written_canonical : NULL);
        written = prime_unread_level_end(a, written, outer);
    }
    cetta_prime_regular_kernel_rules_set(NULL);
    verdict = prime_refutation_assumptions(a, space, explicit_judgment, verdict);
    if (written) {
        written = prime_refutation_assumptions(a, space, judgment, written);
        Atom *combined = explicit_judgment != judgment
            ? prime_implicit_verdict(a, judgment, &implicit, verdict, written)
            : prime_implicit_verdict(a, judgment, &implicit, written, written);
        if (combined == written && canonical_term_out)
            *canonical_term_out = written_canonical;
        verdict = combined;
    }
    if (resources_out) *resources_out = prime_resource_observation(&ledger);
    return steps_limited ? prime_attach_ledger(a, verdict, &ledger) : verdict;
}

static Atom *prime_judge(Arena *a, Space *space, Atom *judgment,
                         bool steps_limited, uint64_t steps) {
    return prime_judge_accounted(
        a, space, judgment, steps_limited, steps, NULL, NULL);
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
    return prime_semantics_judge_typing_accounted(
        a, space, judgment, steps_limited, steps, NULL, NULL);
}

Atom *prime_semantics_judge_typing_accounted(
    Arena *a, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypingResourceObservationV1 *resources_out,
    Atom **canonical_term_out) {
    if (resources_out)
        *resources_out = (CettaPrimeTypingResourceObservationV1){0};
    if (canonical_term_out) *canonical_term_out = NULL;
    if (!a || !space || (steps_limited && steps == 0) ||
        !prime_is_native_typing_judgment(judgment)) {
        return NULL;
    }
    return prime_judge_accounted(
        a, space, judgment, steps_limited, steps, resources_out,
        canonical_term_out);
}

static Atom *prime_kernel_query_type(
    Arena *a, const PrimeKernelQueryCapture *capture, Atom *context,
    Atom *term) {
    if (!context || !term)
        return prime_expr1(a, "Unsynthesized");
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    CettaPrimeRegularKernelResult result =
        cetta_prime_regular_kernel_synth_intrinsic_instantiating_levels_v1(
            a, context, term, capture->levels, capture->level_count, &budget);
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED && result.type)
        return result.type;
    return prime_expr2(
        a, "Unsynthesized",
        prime_sym(a, result.reason ? result.reason : "synthesis-declined"));
}

/* The represented query a `type:eq` or `type:check` judgment poses to the
 * kernel: context, level parameters, admitted rules, the two terms and
 * their synthesized types.  The judgment runs with capture on, so the
 * kernel decides nothing; no equality or checking answer is returned. */
Atom *prime_semantics_kernel_query(
    Arena *a, Space *space, Atom *judgment, bool steps_limited,
    uint64_t steps) {
    if (!a || !space || !judgment) return NULL;
    free(g_prime_kernel_query.levels);
    g_prime_kernel_query = (PrimeKernelQueryCapture){.active = true};
    (void)prime_semantics_judge_typing_direct(
        a, space, judgment, steps_limited, steps);
    PrimeKernelQueryCapture capture = g_prime_kernel_query;
    g_prime_kernel_query = (PrimeKernelQueryCapture){0};
    if (!capture.captured) {
        return atom_expr3(
            a, prime_sym(a, "PrimeKernelQueryV1"),
            prime_expr2(a, "Judgment", judgment),
            prime_expr2(a, "Route", prime_sym(a, "outside")));
    }
    Atom *rules = prime_semantics_kernel_rules(a, space);
    cetta_prime_regular_kernel_rules_set(rules);
    Atom *subject_type = prime_kernel_query_type(
        a, &capture, capture.context, capture.subject);
    Atom *object_type = prime_kernel_query_type(
        a, &capture,
        capture.object_context ? capture.object_context : capture.context,
        capture.object);
    cetta_prime_regular_kernel_rules_set(NULL);
    Atom **levels = arena_alloc(
        a, sizeof(Atom *) * (capture.level_count + 1u));
    levels[0] = prime_sym(a, "LevelParameters");
    for (size_t i = 0u; i < capture.level_count; i++)
        levels[i + 1u] = atom_int(a, (int64_t)capture.levels[i]);
    free(capture.levels);
    Atom *items[11];
    size_t count = 0u;
    items[count++] = prime_sym(a, "PrimeKernelQueryV1");
    items[count++] = prime_expr2(a, "Judgment", judgment);
    items[count++] = prime_expr2(a, "Route", prime_sym(a, capture.route));
    items[count++] = prime_expr2(a, "Context", capture.context);
    if (capture.object_context)
        items[count++] = prime_expr2(
            a, "ObjectContext", capture.object_context);
    items[count++] = atom_expr(
        a, levels, (CettaExprLen)(capture.level_count + 1u));
    items[count++] = prime_expr2(
        a, "Rules", rules ? rules : prime_sym(a, "LNil"));
    items[count++] = prime_expr2(a, "Subject", capture.subject);
    items[count++] = prime_expr2(a, "SubjectType", subject_type);
    items[count++] = prime_expr2(a, "Object", capture.object);
    items[count++] = prime_expr2(a, "ObjectType", object_type);
    return atom_expr(a, items, (CettaExprLen)count);
}

/* ------------------------------------------------------------------------ */
/* Published theorems are hypothetical judgments.                            */
/*                                                                            */
/* `type:theorem` and `type:prove` publish a declaration only after a native  */
/* checking judgment succeeds, and that judgment was made in a context: the   */
/* space declarations the declared route consulted (exactly its class) and    */
/* the admitted rules at those heads.  The published theorem is the judgment  */
/* G |- P, and its record keeps G, one context per accepted proof.            */
/*                                                                            */
/* Withdrawing a declaration of G changes neither the published statement nor */
/* its record, and it does not turn the statement into an implication.  While */
/* no recorded context is available, the kernel does not resolve the          */
/* theorem's name: a typing judgment that would reuse it abstains and names   */
/* what is missing.  Once a recorded context is available again, reuse        */
/* resumes.  A published theorem inside a context needs its own context in    */
/* turn.  Discharging a hypothesis into an implication is a proof a program   */
/* writes and the kernel checks, where the profile's logic has that rule;     */
/* withdrawal never performs it.                                              */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint64_t space;          /* the publishing space, by instance */
    Atom *name;
    Atom *type;              /* the statement as published */
    Atom **contexts;         /* (PrimeTheoremContextV1 dependency ...) */
    size_t context_count;
    size_t context_capacity;
} PrimeTheoremRecord;

static struct {
    bool ready;
    Arena arena;             /* owns every recorded atom for the process */
    PrimeTheoremRecord *records;
    size_t count;
    size_t capacity;
} g_prime_theorems;

static uint64_t prime_theorem_space_key(const Space *space) {
    while (space && space->overlay_base) space = space->overlay_base;
    return space ? space_instance_id(space) : 0u;
}

static bool prime_theorem_names_contain(Atom **names, size_t count,
                                        Atom *name) {
    for (size_t i = 0u; i < count; i++)
        if (atom_eq(names[i], name)) return true;
    return false;
}

/* The dependency record of one checking judgment: each space declaration of
 * the declared route's class, as the declarations `(: name type)` the space
 * holds for it, and the admitted rules at those heads, in kernel spelling.
 * A judgment that the declared route did not decide consulted no space
 * declaration, and its record is empty.  NULL when the class of a
 * declared-route judgment cannot be read back. */
Atom *prime_semantics_theorem_context(
    Arena *a, Space *space, Atom *judgment, bool declared_route) {
    if (!a || !space || !judgment) return NULL;
    Atom *head = prime_sym(a, "PrimeTheoremContextV1");
    if (!declared_route) return atom_expr(a, &head, 1u);
    free(g_prime_kernel_query.levels);
    g_prime_kernel_query = (PrimeKernelQueryCapture){.active = true};
    (void)prime_semantics_judge_typing_direct(a, space, judgment, false, 0u);
    PrimeKernelQueryCapture capture = g_prime_kernel_query;
    g_prime_kernel_query = (PrimeKernelQueryCapture){0};
    free(capture.levels);
    if (!capture.captured || !capture.route || !capture.context ||
        strcmp(capture.route, "declared") != 0)
        return NULL;

    size_t name_count = 0u, name_capacity = 8u;
    Atom **names = cetta_malloc(sizeof(Atom *) * name_capacity);
    for (Atom *cursor = capture.context;
         cursor && cursor->kind == ATOM_EXPR && cursor->expr.len == 4u &&
         atom_is_symbol(cursor->expr.elems[0], "PrimeCtxDecl");
         cursor = cursor->expr.elems[3]) {
        Atom *constant = cursor->expr.elems[1];
        if (!constant || constant->kind != ATOM_EXPR ||
            constant->expr.len < 2u ||
            !atom_is_symbol(constant->expr.elems[0], "DeclConst"))
            continue;
        Atom *name = constant->expr.elems[1];
        if (!name || name->kind != ATOM_SYMBOL ||
            prime_language_owned_declaration(a, name) ||
            prime_theorem_names_contain(names, name_count, name))
            continue;
        if (name_count == name_capacity) {
            name_capacity *= 2u;
            names = cetta_realloc(names, sizeof(Atom *) * name_capacity);
        }
        names[name_count++] = name;
    }

    size_t count = 1u, capacity = 16u;
    Atom **items = cetta_malloc(sizeof(Atom *) * capacity);
    items[0] = head;
    for (size_t i = 0u; i < name_count; i++) {
        Atom **types = NULL;
        uint32_t type_count = space_get_declared_types(
            space, a, names[i], &types);
        for (uint32_t t = 0u; t < type_count; t++) {
            if (count == capacity) {
                capacity *= 2u;
                items = cetta_realloc(items, sizeof(Atom *) * capacity);
            }
            items[count++] = atom_expr3(
                a, atom_symbol_id(a, g_builtin_syms.colon), names[i],
                types[t]);
        }
        free(types);
    }
    for (Atom *rules = prime_semantics_kernel_rules(a, space);
         rules && rules->kind == ATOM_EXPR && rules->expr.len == 3u &&
         atom_is_symbol(rules->expr.elems[0], "LCons");
         rules = rules->expr.elems[2]) {
        Atom *rule = rules->expr.elems[1];
        if (!rule || rule->kind != ATOM_EXPR || rule->expr.len != 5u ||
            !prime_theorem_names_contain(names, name_count,
                                         rule->expr.elems[1]))
            continue;
        if (count == capacity) {
            capacity *= 2u;
            items = cetta_realloc(items, sizeof(Atom *) * capacity);
        }
        items[count++] = rule;
    }
    Atom *context = atom_expr(a, items, (CettaExprLen)count);
    free(items);
    free(names);
    return context;
}

static PrimeTheoremRecord *prime_theorem_record_slot(
    uint64_t space, Atom *name, Atom *type) {
    for (size_t i = 0u; i < g_prime_theorems.count; i++) {
        PrimeTheoremRecord *record = &g_prime_theorems.records[i];
        if (record->space == space && atom_eq(record->name, name) &&
            atom_alpha_eq(record->type, type))
            return record;
    }
    if (g_prime_theorems.count == g_prime_theorems.capacity) {
        g_prime_theorems.capacity = g_prime_theorems.capacity
            ? g_prime_theorems.capacity * 2u : 16u;
        g_prime_theorems.records = cetta_realloc(
            g_prime_theorems.records,
            sizeof(PrimeTheoremRecord) * g_prime_theorems.capacity);
    }
    PrimeTheoremRecord *record =
        &g_prime_theorems.records[g_prime_theorems.count++];
    *record = (PrimeTheoremRecord){
        .space = space,
        .name = atom_deep_copy(&g_prime_theorems.arena, name),
        .type = atom_deep_copy(&g_prime_theorems.arena, type),
    };
    return record;
}

static void prime_theorem_record_add_context(PrimeTheoremRecord *record,
                                             Atom *context) {
    for (size_t i = 0u; i < record->context_count; i++)
        if (atom_alpha_eq(record->contexts[i], context)) return;
    if (record->context_count == record->context_capacity) {
        record->context_capacity = record->context_capacity
            ? record->context_capacity * 2u : 2u;
        record->contexts = cetta_realloc(
            record->contexts, sizeof(Atom *) * record->context_capacity);
    }
    record->contexts[record->context_count++] =
        atom_deep_copy(&g_prime_theorems.arena, context);
}

static void prime_theorem_ledger_ready(void) {
    if (g_prime_theorems.ready) return;
    arena_init_detached(&g_prime_theorems.arena);
    g_prime_theorems.ready = true;
    /* An import's working copy and the space that takes over its contents
     * hold the same theorems, so they hold the same records. */
    prime_scoped_judgment_follow_space_contents();
}

void prime_semantics_theorem_record(
    Space *space, Atom *name, Atom *type,
    Atom *const *contexts, size_t context_count) {
    if (!space || !name || !type || !contexts) return;
    prime_theorem_ledger_ready();
    PrimeTheoremRecord *record = prime_theorem_record_slot(
        prime_theorem_space_key(space), name, type);
    for (size_t i = 0u; i < context_count; i++)
        if (contexts[i]) prime_theorem_record_add_context(record, contexts[i]);
}

void prime_semantics_theorem_ledger_follow(uint64_t from, uint64_t to) {
    if (from == to || !g_prime_theorems.ready) return;
    size_t count = g_prime_theorems.count;
    for (size_t i = 0u; i < count; i++) {
        if (g_prime_theorems.records[i].space != from) continue;
        /* The slot may move the records; read this one by index. */
        Atom *name = g_prime_theorems.records[i].name;
        Atom *type = g_prime_theorems.records[i].type;
        PrimeTheoremRecord *target = prime_theorem_record_slot(to, name, type);
        PrimeTheoremRecord *source = &g_prime_theorems.records[i];
        for (size_t c = 0u; c < source->context_count; c++)
            prime_theorem_record_add_context(target, source->contexts[c]);
    }
}

/* The record whose statement the space now declares for `name`. */
static const PrimeTheoremRecord *prime_theorem_record_in_force(
    Space *space, Arena *arena, Atom *name, uint64_t key) {
    Atom **types = NULL;
    uint32_t type_count = 0u;
    bool looked_up = false;
    const PrimeTheoremRecord *found = NULL;
    for (size_t i = 0u; i < g_prime_theorems.count && !found; i++) {
        const PrimeTheoremRecord *record = &g_prime_theorems.records[i];
        if (record->space != key || !atom_eq(record->name, name)) continue;
        if (!looked_up) {
            type_count = space_get_declared_types(space, arena, name, &types);
            looked_up = true;
        }
        for (uint32_t t = 0u; t < type_count && !found; t++)
            if (atom_alpha_eq(types[t], record->type)) found = record;
    }
    free(types);
    return found;
}

typedef struct PrimeTheoremVisit {
    const PrimeTheoremRecord *record;
    const struct PrimeTheoremVisit *outer;
} PrimeTheoremVisit;

typedef struct {
    Space *space;
    Arena *arena;
    uint64_t key;
    bool rules_read;
    Atom *rules;
} PrimeTheoremAvailability;

static bool prime_theorem_available(
    PrimeTheoremAvailability *view, const PrimeTheoremRecord *record,
    const PrimeTheoremVisit *outer, Atom **missing_out);

static bool prime_theorem_dependency_available(
    PrimeTheoremAvailability *view, Atom *dependency,
    const PrimeTheoremVisit *visit) {
    if (dependency && dependency->kind == ATOM_EXPR &&
        dependency->expr.len == 3u &&
        atom_is_symbol_id(dependency->expr.elems[0], g_builtin_syms.colon)) {
        Atom *name = dependency->expr.elems[1];
        Atom **types = NULL;
        uint32_t type_count = space_get_declared_types(
            view->space, view->arena, name, &types);
        bool declared = false;
        for (uint32_t t = 0u; t < type_count && !declared; t++)
            declared = atom_alpha_eq(types[t], dependency->expr.elems[2]);
        free(types);
        if (!declared) return false;
        const PrimeTheoremRecord *inner = prime_theorem_record_in_force(
            view->space, view->arena, name, view->key);
        return !inner || prime_theorem_available(view, inner, visit, NULL);
    }
    if (dependency && dependency->kind == ATOM_EXPR &&
        dependency->expr.len == 5u &&
        atom_is_symbol(dependency->expr.elems[0], "PrimeRule")) {
        if (!view->rules_read) {
            view->rules = prime_semantics_kernel_rules(view->arena, view->space);
            view->rules_read = true;
        }
        for (Atom *rules = view->rules;
             rules && rules->kind == ATOM_EXPR && rules->expr.len == 3u &&
             atom_is_symbol(rules->expr.elems[0], "LCons");
             rules = rules->expr.elems[2])
            if (atom_alpha_eq(rules->expr.elems[1], dependency)) return true;
        return false;
    }
    return false;
}

static bool prime_theorem_available(
    PrimeTheoremAvailability *view, const PrimeTheoremRecord *record,
    const PrimeTheoremVisit *outer, Atom **missing_out) {
    /* A justification is well founded: a theorem is not its own support. */
    size_t depth = 0u;
    for (const PrimeTheoremVisit *v = outer; v; v = v->outer, depth++)
        if (v->record == record || depth >= 64u) return false;
    PrimeTheoremVisit visit = {.record = record, .outer = outer};
    for (size_t c = 0u; c < record->context_count; c++) {
        Atom *context = record->contexts[c];
        bool available = true;
        for (CettaExprIndex i = 1u; i < context->expr.len && available; i++)
            available = prime_theorem_dependency_available(
                view, context->expr.elems[i], &visit);
        if (available) return true;
    }
    if (missing_out) {
        /* What the recorded contexts lack, by name, each name once. */
        size_t total = 0u;
        for (size_t c = 0u; c < record->context_count; c++)
            total += (size_t)record->contexts[c]->expr.len;
        size_t count = 0u;
        Atom **missing = arena_alloc(
            view->arena, sizeof(Atom *) * (total ? total : 1u));
        for (size_t c = 0u; c < record->context_count; c++) {
            Atom *context = record->contexts[c];
            for (CettaExprIndex i = 1u; i < context->expr.len; i++) {
                Atom *dependency = context->expr.elems[i];
                if (prime_theorem_dependency_available(
                        view, dependency, &visit))
                    continue;
                Atom *name = dependency->expr.elems[1];
                if (!prime_theorem_names_contain(missing, count, name))
                    missing[count++] = name;
            }
        }
        *missing_out = atom_expr(view->arena, missing, (CettaExprLen)count);
    }
    return false;
}

static bool prime_theorem_context_unavailable(
    Space *space, Arena *arena, Atom *name, Atom **reason_out) {
    if (!g_prime_theorems.count || !space || !arena || !name) return false;
    uint64_t key = prime_theorem_space_key(space);
    const PrimeTheoremRecord *record =
        prime_theorem_record_in_force(space, arena, name, key);
    if (!record) return false;
    PrimeTheoremAvailability view = {
        .space = space, .arena = arena, .key = key,
    };
    Atom *missing = NULL;
    if (prime_theorem_available(&view, record, NULL, &missing)) return false;
    if (reason_out)
        *reason_out = prime_expr3(
            arena, "theorem-context-unavailable", name,
            missing ? missing : atom_unit(arena));
    return true;
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
