#include "parser.h"
#include "prime_regular_kernel.h"
#include "prime_scoped_judgments.h"
#include "prime_semantics.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned failures;
static unsigned checks;

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static void expect_judgment_status(Arena *arena, Space *space, Atom *query,
                                   const char *label, bool limited,
                                   uint64_t steps, const char *status) {
    checks++;
    Atom *verdict = query ? prime_scoped_judgment_judge(
        arena, space, query, limited, steps) : NULL;
    bool matches = verdict && verdict->kind == ATOM_EXPR &&
        verdict->expr.len == 4u &&
        atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
        atom_is_symbol(verdict->expr.elems[1], status);
    if (!matches) {
        fprintf(stderr, "FAIL: expected %s (limited=%d, steps=%llu): %s\n",
                status, limited, (unsigned long long)steps, label);
        if (verdict) atom_print(verdict, stderr);
        fputc('\n', stderr);
        failures++;
    }
}

static void expect_status(Arena *arena, Space *space, const char *text,
                          bool limited, uint64_t steps, const char *status) {
    expect_judgment_status(
        arena, space, parse_one(arena, text), text, limited, steps, status);
}

static void check_request_cache_lifetime(Arena *persistent, Space *space) {
    space_add(space, parse_one(persistent, "(: cacheFunction (-> Field set))"));
    Arena request;
    arena_init_detached(&request);
    ArenaMark start = arena_mark(&request);
    VarInternTable *saved_variables = g_var_intern;
    g_var_intern = NULL;
    const uint64_t revision = space_revision(space);
    expect_status(&request, space,
        "(set:proves (pf:fix x (pf:all-elim (pf:known PowerI) (cacheFunction x)))"
        " (all Field (lam x (In (cacheFunction x) (Power (cacheFunction x))))))",
        false, 0u, "Established");
    arena_reset(&request, start);
    /* Reuse the source arena, so a borrowed cached Atom is visibly wrong
     * even when resetting the arena does not return its pages to malloc. */
    for (unsigned i = 0u; i < 4096u; i++) (void)atom_int(&request, (int64_t)i);
    expect_status(&request, space,
        "(set:define cacheApply (-> (-> Field Field) Field Field)"
        " (= (cacheApply $f $x) ($f $x)))",
        false, 0u, "Established");
    checks++;
    if (space_revision(space) != revision) {
        failures++;
        fprintf(stderr, "FAIL: cache lifetime checking changed the authored theory\n");
    }
    g_var_intern = saved_variables;
    arena_free(&request);
}

static void check_retained_formation(Arena *arena, Space *space) {
    Atom *type = parse_one(arena, "(-> (A : (u 0)) (x : A) A)");
    Atom *query = atom_expr(arena, (Atom *[]){atom_symbol(arena, "type:formed"), type}, 2u);
    Atom *canonical = NULL;
    CettaPrimeTypingResourceObservationV1 resources;
    const uint64_t allowance = 1u << 20u;
    Atom *verdict = prime_semantics_judge_typing_accounted(
        arena, space, query, true, allowance, &resources, &canonical);
    Atom *expected = parse_one(arena,
        "(Pi (Sort (LevelConst 0)) (Pi (idx 0) (idx 1)))");
    checks++;
    if (!verdict || !atom_is_symbol(verdict->expr.elems[1], "Established") ||
        !canonical || !atom_eq(canonical, expected) ||
        resources.initial != allowance || resources.spent == 0u ||
        resources.remaining != allowance - resources.spent) {
        failures++;
        fprintf(stderr, "FAIL: accounted formation lost its checked telescope or resource receipt\n");
    }
    const char *obstructions[] = {
        "(type:formed (-> MissingType MissingType))",
        "(type:formed (u -1))",
    };
    const char *statuses[] = {"Undetermined", "Refuted"};
    for (size_t index = 0u; index < 2u; index++) {
        canonical = expected;
        verdict = prime_semantics_judge_typing_accounted(
            arena, space, parse_one(arena, obstructions[index]), true, allowance,
            &resources, &canonical);
        checks++;
        if (!verdict || !atom_is_symbol(verdict->expr.elems[1], statuses[index]) || canonical) {
            failures++;
            fprintf(stderr, "FAIL: %s formation retained a fabricated checked type\n", statuses[index]);
        }
    }
    canonical = expected;
    verdict = prime_semantics_judge_typing_accounted(
        arena, space, query, true, 1u, &resources, &canonical);
    checks++;
    if (!verdict || !atom_is_symbol(verdict->expr.elems[1], "Incomplete") ||
        canonical || resources.spent != 1u || resources.remaining != 0u) {
        failures++;
        fprintf(stderr, "FAIL: exhausted formation exported an unchecked telescope\n");
    }
    canonical = expected;
    verdict = prime_semantics_judge_typing_accounted(
        arena, space, query, true, 0u, &resources, &canonical);
    checks++;
    if (verdict || canonical || resources.spent != 0u) {
        failures++;
        fprintf(stderr, "FAIL: zero allowance replayed or retained formation\n");
    }
    CettaPrimeTypingCheckingCandidateV1 candidate = {
        .term = parse_one(arena, "(lam A (lam x x))"),
        .expected_type = type,
    };
    CettaPrimeTypingCheckingObservationV1 observation;
    checks++;
    if (!cetta_prime_typing_observe_checking_v1(arena, space, &candidate, &observation) ||
        observation.authority.result.value.outcome != CETTA_NIK_OUTCOME_ESTABLISHED ||
        !observation.authority.canonical_term ||
        !atom_eq(observation.authority.canonical_term, parse_one(arena, "(Lam (Lam (idx 0)))"))) {
        failures++;
        fprintf(stderr, "FAIL: dependent identity checking did not retain the actual lambda telescope\n");
    }
    candidate.term = parse_one(arena, "(lam A (lam x A))");
    checks++;
    if (!cetta_prime_typing_observe_checking_v1(arena, space, &candidate, &observation) ||
        observation.authority.result.value.outcome != CETTA_NIK_OUTCOME_REFUTED ||
        observation.authority.canonical_term) {
        failures++;
        fprintf(stderr, "FAIL: dependent identity confused its type parameter with its value\n");
    }
}

static bool judgment_is_established(Arena *arena, Space *space, Atom *query,
                                    uint64_t steps) {
    Atom *verdict = prime_scoped_judgment_judge(
        arena, space, query, true, steps);
    return verdict && verdict->kind == ATOM_EXPR && verdict->expr.len == 4u &&
        atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
        atom_is_symbol(verdict->expr.elems[1], "Established");
}

static uint64_t minimum_judgment_steps(Arena *arena, Space *space,
                                       Atom *query) {
    uint64_t upper = 1u;
    while (upper <= (1u << 20u) &&
           !judgment_is_established(arena, space, query, upper))
        upper *= 2u;
    if (upper > (1u << 20u)) return 0u;
    uint64_t lower = 0u;
    while (lower + 1u < upper) {
        uint64_t middle = lower + (upper - lower) / 2u;
        if (judgment_is_established(arena, space, query, middle))
            upper = middle;
        else
            lower = middle;
    }
    return upper;
}

static bool kernel_synthesis_is_established(
    Arena *arena, Atom *context, Atom *rules, Atom *term, uint64_t steps) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, steps);
    cetta_prime_regular_kernel_rules_set(rules);
    CettaPrimeRegularKernelResult result =
        cetta_prime_regular_kernel_synth_intrinsic_v1(
            arena, context, term, &budget);
    cetta_prime_regular_kernel_rules_set(NULL);
    return result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static uint64_t minimum_kernel_synthesis_steps(
    Arena *arena, Atom *context, Atom *rules, Atom *term) {
    uint64_t upper = 1u;
    while (upper <= (1u << 20u) &&
           !kernel_synthesis_is_established(
               arena, context, rules, term, upper))
        upper *= 2u;
    if (upper > (1u << 20u)) return 0u;
    uint64_t lower = 0u;
    while (lower + 1u < upper) {
        uint64_t middle = lower + (upper - lower) / 2u;
        if (kernel_synthesis_is_established(
                arena, context, rules, term, middle))
            upper = middle;
        else
            lower = middle;
    }
    return upper;
}

/* Measure the actual formation checks selected by the generated declaration
 * inventory. The largest individual allowance must not admit their batch;
 * the batch needs their sum, with no refreshed allowance between checks. */
static void check_inductive_shared_allowance(Arena *arena, Space *space) {
    Atom *query = parse_one(arena,
        "(set:inductive BudgetTree (u 0)"
        " (: BudgetLeaf BudgetTree) (: BudgetNode (-> Field BudgetTree)))");
    Atom *verdict = prime_scoped_judgment_judge(
        arena, space, query, false, 0u);
    Atom *published = verdict && verdict->kind == ATOM_EXPR &&
            verdict->expr.len == 4u
        ? verdict->expr.elems[3] : NULL;
    if (!published || published->kind != ATOM_EXPR ||
        !atom_is_symbol(published->expr.elems[0], "SetPublish")) {
        checks++;
        failures++;
        fprintf(stderr, "FAIL: no generated BudgetTree inventory\n");
        return;
    }
    Space view;
    space_init_overlay(&view, space);
    Atom *signature_verdict = prime_scoped_judgment_judge(
        arena, space, parse_one(arena, "(set:signature)"), false, 0u);
    Atom *signature = signature_verdict->expr.elems[3]->expr.elems[1];
    for (CettaExprIndex i = 0u; i < signature->expr.len; i++)
        space_add(&view, signature->expr.elems[i]);
    Atom *principle = NULL;
    uint64_t total = 0u, largest = 0u;
    unsigned stages = 0u;
    for (CettaExprIndex i = 1u; i <= published->expr.len; i++) {
        Atom *entry = i < published->expr.len ? published->expr.elems[i] : NULL;
        Atom *check = NULL;
        bool add_after = false;
        if (!entry) {
            if (principle)
                check = atom_expr(arena, (Atom *[]){
                    atom_symbol(arena, "type:check"), principle,
                    atom_symbol(arena, "prop")}, 3u);
        } else if (entry->kind == ATOM_EXPR && entry->expr.len == 8u &&
                   atom_is_symbol(entry->expr.elems[0], "set:known") &&
                   atom_is_symbol(entry->expr.elems[1], "BudgetTree-ind")) {
            principle = entry->expr.elems[2];
        } else if (entry->kind == ATOM_EXPR && entry->expr.len == 3u &&
                   atom_is_symbol(entry->expr.elems[0], ":")) {
            bool family = atom_is_symbol(entry->expr.elems[1], "BudgetTree");
            bool recursor = atom_is_symbol(entry->expr.elems[1], "BudgetTree-rec");
            check = family || recursor
                ? atom_expr(arena, (Atom *[]){atom_symbol(arena, "type:formed"),
                                            entry->expr.elems[2]}, 2u)
                : atom_expr(arena, (Atom *[]){atom_symbol(arena, "type:check"),
                                            entry->expr.elems[2],
                                            query->expr.elems[2]}, 3u);
            add_after = !recursor;
        }
        if (!check) continue;
        CettaPrimeTypingResourceObservationV1 resources;
        const uint64_t allowance = 1u << 20u;
        Atom *checked = prime_semantics_judge_typing_accounted(
            arena, &view, check, true, allowance, &resources, NULL);
        stages++;
        checks++;
        if (!checked || !atom_is_symbol(checked->expr.elems[1], "Established") ||
            !resources.limited || resources.initial != allowance ||
            resources.spent == 0u || resources.spent > allowance ||
            resources.remaining != allowance - resources.spent) {
            failures++;
            fprintf(stderr, "FAIL: nonconservative datatype formation account\n");
        }
        total += resources.spent;
        if (resources.spent > largest) largest = resources.spent;
        if (add_after) space_add(&view, entry);
    }
    space_free(&view);
    uint64_t required = minimum_judgment_steps(arena, space, query);
    checks++;
    if (stages != 5u || required != total || total <= largest) {
        failures++;
        fprintf(stderr,
                "FAIL: datatype batch account (stages=%u sum=%llu max=%llu required=%llu)\n",
                stages, (unsigned long long)total, (unsigned long long)largest,
                (unsigned long long)required);
    }
    expect_judgment_status(arena, space, query, "datatype zero allowance",
                           true, 0u, "Incomplete");
    expect_judgment_status(arena, space, query, "datatype one allowance",
                           true, 1u, "Incomplete");
    expect_judgment_status(arena, space, query, "datatype individual maximum",
                           true, largest, "Incomplete");
    expect_judgment_status(arena, space, query, "datatype just below total",
                           true, total - 1u, "Incomplete");
    expect_judgment_status(arena, space, query, "datatype total allowance",
                           true, total, "Established");
    expect_judgment_status(arena, space, query, "datatype larger allowance",
                           true, total + 1u, "Established");
}

static void expect_rule_inventory(Arena *arena, Space *space,
                                  const char *expected, const char *label) {
    checks++;
    Atom *actual = prime_semantics_kernel_rules(arena, space);
    Atom *wanted = parse_one(arena, expected);
    if (!actual || !wanted || !atom_eq(actual, wanted)) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", label);
        if (actual) atom_print(actual, stderr);
        fputc('\n', stderr);
    }
}

static void check_overlay_rule_inventory(Arena *arena, TermUniverse *universe) {
    Space base, view, nested;
    space_init_with_universe(&base, universe);
    Atom *inherited = parse_one(arena,
        "(type:rule inherited 0 () (DeclConst carrier))");
    space_add(&base, inherited);
    space_add(&base, parse_one(arena, "(: irrelevant (u 0))"));
    space_init_overlay(&view, &base);
    space_add(&base, parse_one(arena,
        "(type:rule late 0 () (DeclConst invisible))"));
    space_add(&view, parse_one(arena,
        "(type:rule local 1 ((PVar 0)) (PVar 0))"));
    expect_rule_inventory(arena, &view,
        "(LCons (PrimeRule local 1 ((PVar 0)) (PVar 0))"
        " (LCons (PrimeRule inherited 0 () (DeclConst carrier)) LNil))",
        "overlay includes inherited rules, excludes later base additions, preserves pattern code");
    space_init_overlay(&nested, &view);
    space_add(&nested, parse_one(arena,
        "(type:rule deepest 0 () (DeclConst local))"));
    expect_rule_inventory(arena, &nested,
        "(LCons (PrimeRule deepest 0 () (DeclConst local))"
        " (LCons (PrimeRule local 1 ((PVar 0)) (PVar 0))"
        " (LCons (PrimeRule inherited 0 () (DeclConst carrier)) LNil)))",
        "nested overlays collect the complete logical rule view");
    space_remove(&nested, inherited);
    expect_rule_inventory(arena, &nested,
        "(LCons (PrimeRule deepest 0 () (DeclConst local))"
        " (LCons (PrimeRule local 1 ((PVar 0)) (PVar 0)) LNil))",
        "nested overlay removal hides an inherited rule");
    expect_rule_inventory(arena, &base,
        "(LCons (PrimeRule late 0 () (DeclConst invisible))"
        " (LCons (PrimeRule inherited 0 () (DeclConst carrier)) LNil))",
        "overlay removal does not mutate the indexed base inventory");
    space_free(&nested);
    space_free(&view);
    space_free(&base);
}

int main(void) {
    Arena arena;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;
    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;

    check_overlay_rule_inventory(&arena, &universe);

    const char *valid =
        "(set:proves (pf:all-elim (pf:known PowerI) Empty)"
        " (In Empty (Power Empty)))";
    const char *wrong_target =
        "(set:proves (pf:all-elim (pf:known PowerI) Empty)"
        " (In Empty Empty))";

    /* Exhaustion of the conversion authority is not a new decision route. */
    expect_status(&arena, &space, valid, true, 0u, "Incomplete");
    expect_status(&arena, &space, valid, true, 64u, "Incomplete");
    expect_status(&arena, &space, wrong_target, true, 64u, "Incomplete");
    expect_status(&arena, &space, valid, false, 0u, "Established");
    expect_status(&arena, &space, wrong_target, false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:proves (pf:known missing) Falsum)", false, 0u, "Undetermined");
    /* A bad submitted proof of a true proposition is rejected, not accepted
     * merely because another proof of that proposition exists. */
    expect_status(&arena, &space,
        "(set:proves (pf:hyp 0) (In Empty (Power Empty)))",
        false, 0u, "Refuted");

    /* Dependent use rechecks the exact package before applying the consumer.
     * Exhaustion remains incomplete; with an unbounded budget the same
     * package and consumer establish a genuine dependent application. */
    Atom *proof_query = parse_one(&arena, "(set:native-proof PowerI)");
    Atom *proof_verdict = prime_scoped_judgment_judge(
        &arena, &space, proof_query, false, 0u);
    Atom *proof_evidence = proof_verdict && proof_verdict->kind == ATOM_EXPR &&
            proof_verdict->expr.len == 4u
        ? proof_verdict->expr.elems[3] : NULL;
    Atom *package = proof_evidence && proof_evidence->kind == ATOM_EXPR &&
            proof_evidence->expr.len == 2u &&
            atom_is_symbol(proof_evidence->expr.elems[0], "PrimeScopedValue")
        ? proof_evidence->expr.elems[1] : NULL;
    Atom *proof_type = package && package->kind == ATOM_EXPR &&
            package->expr.len == 10u &&
            atom_is_symbol(package->expr.elems[0], "SetNativeProofV1")
        ? package->expr.elems[5] : NULL;
    Atom *consumer = proof_type
        ? atom_expr(
            &arena,
            (Atom *[]){atom_symbol(&arena, "Lam"), proof_type,
                       parse_one(&arena, "(Refl (idx 0))")},
            3u)
        : NULL;
    Atom *use = package && consumer
        ? atom_expr(
            &arena,
            (Atom *[]){atom_symbol(&arena, "set:native-use"), package,
                       consumer},
            3u)
        : NULL;
    expect_judgment_status(&arena, &space, use, "dependent native use",
                           true, 0u, "Incomplete");
    expect_judgment_status(&arena, &space, use, "dependent native use",
                           false, 0u, "Established");

    /* One public judgment has one regular-kernel allowance.  In particular,
     * native-proof revalidation and consumer synthesis cannot each restart
     * the caller's budget. */
    Atom *context = package ? package->expr.elems[6] : NULL;
    Atom *rules = package ? package->expr.elems[7] : NULL;
    Atom *application = consumer && package
        ? atom_expr(
            &arena,
            (Atom *[]){atom_symbol(&arena, "App"), consumer,
                       package->expr.elems[4]},
            3u)
        : NULL;
    uint64_t proof_steps = proof_query
        ? minimum_judgment_steps(&arena, &space, proof_query) : 0u;
    uint64_t consumer_steps = context && rules && application
        ? minimum_kernel_synthesis_steps(
            &arena, context, rules, application) : 0u;
    uint64_t use_steps = use
        ? minimum_judgment_steps(&arena, &space, use) : 0u;
    checks++;
    if (proof_steps == 0u || consumer_steps == 0u || use_steps == 0u ||
        use_steps < proof_steps + consumer_steps) {
        fprintf(stderr,
                "FAIL: native-use refreshed its budget "
                "(proof=%llu consumer=%llu use=%llu)\n",
                (unsigned long long)proof_steps,
                (unsigned long long)consumer_steps,
                (unsigned long long)use_steps);
        failures++;
    }

    /* Constructor shape and positivity are not field-type formation. The
     * existing authority's abstention must not be relabeled as refutation. */
    expect_status(&arena, &space,
        "(set:inductive GhostTree (u 0) (: GhostNode (-> MissingType GhostTree)))",
        false, 0u, "Undetermined");
    expect_status(&arena, &space,
        "(set:inductive ValueTree (u 0) (: ValueNode (-> Empty ValueTree)))",
        false, 0u, "Undetermined");
    expect_status(&arena, &space,
        "(set:inductive BadLevel (u -1) (: BadLevelCtor BadLevel))",
        false, 0u, "Refuted");
    space_add(&space, parse_one(&arena, "(: Large (u 1))"));
    space_add(&space, parse_one(&arena, "(: Field (u 0))"));
    expect_status(&arena, &space,
        "(set:inductive SmallTree (u 0) (: SmallNode (-> Large SmallTree)))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:inductive LargeTree (u 1) (: LargeNode (-> Large LargeTree)))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:inductive P (u 0) (: MkP P))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:inductive NameTree (u 0) (: v1 (-> Field NameTree)))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:inductive ConflictTree (u 0) (: ConflictTree-rec ConflictTree))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:inductive WideTree (u 0)"
        " (: WideNode (-> Field Field Field Field Field Field Field Field Field WideTree)))",
        false, 0u, "Established");

    check_inductive_shared_allowance(&arena, &space);
    check_retained_formation(&arena, &space);
    check_request_cache_lifetime(&arena, &space);

    /* The definition elaborator's type-shaped syntax does not earn formation.
     * Bounded formation and both native endpoint checks share one allowance;
     * every refusal leaves the temporary declaration out of the theory. */
    expect_status(&arena, &space,
        "(set:define missingId (-> MissingType MissingType) (= (missingId $x) $x))",
        false, 0u, "Undetermined");
    expect_status(&arena, &space,
        "(set:define valueDomain (-> Empty Field) (= (valueDomain $x) $x))",
        false, 0u, "Undetermined");
    expect_status(&arena, &space,
        "(set:define badDefinition (-> (u -1) Field) (= (badDefinition $x) $x))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:define typeId (-> (u 0) (u 0)) (= (typeId $x) $x))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:define dependentId (-> (A : (u 0)) (x : A) A) (= (dependentId $A $x) $x))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:define wrongDependent (-> (A : (u 0)) (x : A) A) (= (wrongDependent $A $x) $A))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:define danglingDependent (-> (A : (u 0)) (x : A) A) (= (danglingDependent $A $x) $missing))",
        false, 0u, "Undetermined");
    expect_status(&arena, &space,
        "(set:define reflexiveEvidence (-> (A : (u 0)) (x : A) (id A x x))"
        " (= (reflexiveEvidence $A $x) (refl $x)))",
        false, 0u, "Established");
    Atom *dependent_query = parse_one(&arena,
        "(set:define budgetDependent (-> (A : (u 0)) (x : A) A) (= (budgetDependent $A $x) $x))");
    uint64_t dependent_revision = space_revision(&space);
    uint64_t dependent_steps = minimum_judgment_steps(&arena, &space, dependent_query);
    checks++;
    if (dependent_steps < 2u) {
        failures++;
        fprintf(stderr, "FAIL: no finite dependent clause checking allowance\n");
    } else {
        expect_judgment_status(&arena, &space, dependent_query,
            "dependent definition zero allowance", true, 0u, "Incomplete");
        expect_judgment_status(&arena, &space, dependent_query,
            "dependent definition just below allowance", true, dependent_steps - 1u, "Incomplete");
        expect_judgment_status(&arena, &space, dependent_query,
            "dependent definition exact allowance", true, dependent_steps, "Established");
        expect_judgment_status(&arena, &space, dependent_query,
            "dependent definition increased allowance", true, dependent_steps + 7u, "Established");
    }
    checks++;
    if (space_revision(&space) != dependent_revision) {
        failures++;
        fprintf(stderr, "FAIL: dependent clause checking published before admission\n");
    }
    Atom *definition_query = parse_one(&arena,
        "(set:define budgetId (-> Field Field) (= (budgetId $x) $x))");
    uint64_t definition_revision = space_revision(&space);
    uint64_t definition_steps = minimum_judgment_steps(&arena, &space, definition_query);
    checks++;
    if (definition_steps < 2u) {
        failures++;
        fprintf(stderr, "FAIL: no finite definition checking allowance\n");
    } else {
        expect_judgment_status(&arena, &space, definition_query,
            "definition zero allowance", true, 0u, "Incomplete");
        expect_judgment_status(&arena, &space, definition_query,
            "definition just below allowance", true, definition_steps - 1u, "Incomplete");
        expect_judgment_status(&arena, &space, definition_query,
            "definition exact allowance", true, definition_steps, "Established");
        expect_judgment_status(&arena, &space, definition_query,
            "definition increased allowance", true, definition_steps + 7u, "Established");
    }
    checks++;
    if (space_revision(&space) != definition_revision) {
        failures++;
        fprintf(stderr, "FAIL: definition checking mutated the authored theory\n");
    }

    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);

    if (failures) return 1;
    printf("Scoped conversion and declaration outcomes: %u checks passed\n", checks);
    return 0;
}
