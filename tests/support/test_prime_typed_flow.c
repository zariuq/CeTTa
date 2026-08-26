#include "parser.h"
#include "eval.h"
#include "prime_native_calculus.h"
#include "prime_rule_machine_ingress.h"
#include "prime_typed_finite_relation.h"
#include "prime_typed_flow_boundary.h"
#include "prime_typed_hyp.h"
#include "prime_typed_iteration.h"
#include "prime_typed_list.h"
#include "prime_typed_list_relations.h"
#include "prime_typed_list_relator.h"
#include "prime_typed_relation.h"
#include "rule_machine.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "term_universe.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                             \
    do {                                                                    \
        checks++;                                                           \
        if (!(condition)) {                                                 \
            fprintf(stderr, "FAIL: %s\n", (label));                       \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static Atom *run_rule_machine_definition(
    Arena *arena, const char *path, const char *definition_name,
    const char *goal_text, int64_t depth,
    int64_t max_states, int64_t max_occurrences) {
    Atom **forms = NULL;
    int count = parse_metta_file(path, arena, &forms);
    Atom *definition = count == 1 && forms ? forms[0] : NULL;
    Atom *compiled_call = definition && definition->kind == ATOM_EXPR &&
            definition->expr.len == 3u &&
            atom_is_symbol(definition->expr.elems[0], "=") &&
            definition->expr.elems[1]->kind == ATOM_EXPR &&
            definition->expr.elems[1]->expr.len == 1u &&
            atom_is_symbol(
                definition->expr.elems[1]->expr.elems[0], definition_name)
        ? definition->expr.elems[2]
        : NULL;
    Atom *artifact = compiled_call && compiled_call->kind == ATOM_EXPR &&
            compiled_call->expr.len == 3u &&
            atom_is_symbol(
                compiled_call->expr.elems[0], "compile:rule-package")
        ? cetta_rule_machine_dispatch(
              arena, compiled_call->expr.elems[0],
              &compiled_call->expr.elems[1], 2u)
        : NULL;
    Atom *goal = parse_one(arena, goal_text);
    Atom *run_head = atom_symbol(arena, "compile:run");
    Atom *run_args[] = {
        artifact,
        atom_int(arena, depth),
        atom_int(arena, max_states),
        atom_int(arena, max_occurrences),
        goal,
    };
    Atom *result = artifact && goal
        ? cetta_rule_machine_dispatch(arena, run_head, run_args, 5u)
        : NULL;
    free(forms);
    return result;
}

static Atom *run_rule_machine_applied_definition(
    Arena *arena, const char *path, const char *definition_call_text,
    const char *goal_text, int64_t depth,
    int64_t max_states, int64_t max_occurrences) {
    Atom **forms = NULL;
    int count = parse_metta_file(path, arena, &forms);
    Atom *definition_call = parse_one(arena, definition_call_text);
    Atom *compiled_call = NULL;
    for (int index = 0; definition_call && index < count; index++) {
        Atom *form = forms[index];
        if (!form || form->kind != ATOM_EXPR || form->expr.len != 3u ||
            !atom_is_symbol(form->expr.elems[0], "=") ||
            !atom_eq(form->expr.elems[1], definition_call)) {
            continue;
        }
        if (compiled_call) {
            compiled_call = NULL;
            break;
        }
        compiled_call = form->expr.elems[2];
    }
    Atom *artifact = compiled_call && compiled_call->kind == ATOM_EXPR &&
            compiled_call->expr.len == 3u &&
            atom_is_symbol(
                compiled_call->expr.elems[0], "compile:rule-package")
        ? cetta_rule_machine_dispatch(
              arena, compiled_call->expr.elems[0],
              &compiled_call->expr.elems[1], 2u)
        : NULL;
    Atom *goal = parse_one(arena, goal_text);
    Atom *run_head = atom_symbol(arena, "compile:run");
    Atom *run_args[] = {
        artifact,
        atom_int(arena, depth),
        atom_int(arena, max_states),
        atom_int(arena, max_occurrences),
        goal,
    };
    Atom *result = artifact && goal
        ? cetta_rule_machine_dispatch(arena, run_head, run_args, 5u)
        : NULL;
    free(forms);
    return result;
}

static void add_form(Arena *arena, Space *space, const char *text) {
    Atom *form = parse_one(arena, text);
    CHECK(form != NULL, "typed-flow declaration parses");
    if (form) space_add(space, form);
}

static void add_file(Arena *arena, Space *space, const char *path) {
    Atom **forms = NULL;
    int count = parse_metta_file(path, arena, &forms);
    CHECK(count >= 0, "typed-flow support file parses");
    for (int index = 0; index < count; index++)
        space_add(space, forms[index]);
    free(forms);
}

static bool native_typing_route(CettaPrimeTypingRouteV1 route) {
    return route == CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR;
}

static CettaPrimeTypedValueV1 *import_native(
    Arena *arena, Space *space, const char *text) {
    Atom *term = parse_one(arena, text);
    CettaPrimeTypingSynthesisObservationV1 observation;
    CettaPrimeTypedValueV1 *value = NULL;
    bool observed = term && cetta_prime_typed_value_import_term_v1(
        arena, space, term, false, 0u, &observation, &value);
    CHECK(observed, "term reaches the check-once typed boundary");
    bool native_value = observed &&
        observation.authority.result.kind == CETTA_NIK_RESULT_OUTCOME &&
        observation.authority.result.value.outcome ==
            CETTA_NIK_OUTCOME_ESTABLISHED &&
        native_typing_route(observation.authority.route) &&
        value && cetta_prime_typed_value_v1_is_current(value, space);
    CHECK(native_value,
          "term enters as a current native Prime typed value");
    if (!native_value && observed) {
        fprintf(
            stderr,
            "  typed import %s: result-kind=%d outcome=%d route=%d value=%s\n",
            text, (int)observation.authority.result.kind,
            observation.authority.result.kind == CETTA_NIK_RESULT_OUTCOME
                ? (int)observation.authority.result.value.outcome
                : -1,
            (int)observation.authority.route,
            value ? "yes" : "no");
        if (observation.authority.payload) {
            fputs("  payload=", stderr);
            atom_print(observation.authority.payload, stderr);
            fputc('\n', stderr);
        }
    }
    return value;
}

static CettaPrimeTypedValueV1 *import_checked(
    Arena *arena, Space *space, const char *text,
    const CettaPrimeTypedValueV1 *expected_type) {
    Atom *term = parse_one(arena, text);
    CettaPrimeTypingCheckingObservationV1 observation;
    CettaPrimeTypedValueV1 *value = NULL;
    bool observed = term && cetta_prime_typed_value_import_checked_term_v1(
        arena, space, term, expected_type, false, 0u,
        &observation, &value);
    CHECK(observed, "term reaches the check-once expected-type boundary");
    bool native_value = observed &&
        observation.authority.result.kind == CETTA_NIK_RESULT_OUTCOME &&
        observation.authority.result.value.outcome ==
            CETTA_NIK_OUTCOME_ESTABLISHED &&
        native_typing_route(observation.authority.route) &&
        value && cetta_prime_typed_value_v1_is_current(value, space);
    CHECK(native_value,
          "checked term enters as a current native Prime typed value");
    if (!native_value && observed) {
        fprintf(
            stderr,
            "  typed check %s: result-kind=%d outcome=%d route=%d value=%s\n",
            text, (int)observation.authority.result.kind,
            observation.authority.result.kind == CETTA_NIK_RESULT_OUTCOME
                ? (int)observation.authority.result.value.outcome
                : -1,
            (int)observation.authority.route,
            value ? "yes" : "no");
        if (observation.authority.payload) {
            fputs("  payload=", stderr);
            atom_print(observation.authority.payload, stderr);
            fputc('\n', stderr);
        }
    }
    return value;
}

static Atom *erase_term(
    Arena *arena, const TermUniverse *universe,
    const CettaPrimeTypedValueV1 *value) {
    Atom *term = NULL;
    return cetta_prime_typed_value_v1_erase(
               value, universe, arena, &term, NULL)
        ? term
        : NULL;
}

static Atom *erase_type(
    Arena *arena, const TermUniverse *universe,
    const CettaPrimeTypedValueV1 *value) {
    Atom *type = NULL;
    return cetta_prime_typed_value_v1_erase(
               value, universe, arena, NULL, &type)
        ? type
        : NULL;
}

static void print_typed_value_failure(
    Arena *arena, const TermUniverse *universe, const char *label,
    const CettaPrimeTypedValueV1 *value) {
    if (value) return;
    fprintf(stderr, "  missing typed value: %s\n", label);
    (void)arena;
    (void)universe;
}

static Atom *intrinsic_application(
    Arena *arena, const char *head,
    Atom *const *arguments, size_t argument_count) {
    Atom *application = atom_symbol(arena, head);
    for (size_t index = 0u; index < argument_count; index++) {
        if (!arguments[index]) return NULL;
        application = atom_expr3(
            arena, atom_symbol(arena, "App"),
            application, arguments[index]);
    }
    return application;
}

static Atom *intrinsic_apply_term(
    Arena *arena, Atom *function,
    Atom *const *arguments, size_t argument_count) {
    Atom *application = function;
    for (size_t index = 0u; application && index < argument_count; index++) {
        if (!arguments[index]) return NULL;
        application = atom_expr3(
            arena, atom_symbol(arena, "App"),
            application, arguments[index]);
    }
    return application;
}

static CettaPrimeTypedValueV1 *typed_apply_many(
    Arena *arena, Space *space,
    CettaPrimeTypedValueV1 *function,
    CettaPrimeTypedValueV1 *const *arguments,
    size_t argument_count) {
    CettaPrimeTypedValueV1 *result = function;
    for (size_t index = 0u; result && index < argument_count; index++)
        result = cetta_prime_typed_value_apply_v1(
            arena, space, result, arguments[index]);
    return result;
}

static const CettaPrimeTypedDerivationNodeV1 *find_derivation_node(
    const CettaPrimeTypedDerivationViewV1 *derivation,
    uint64_t occurrence_identity) {
    if (!derivation) return NULL;
    for (size_t index = 0u; index < derivation->node_count; index++)
        if (derivation->nodes[index].occurrence_identity ==
            occurrence_identity) {
            return &derivation->nodes[index];
        }
    return NULL;
}

static bool derivation_has_witness(
    const CettaPrimeTypedDerivationViewV1 *derivation, AtomId witness) {
    if (!derivation) return false;
    for (size_t index = 0u; index < derivation->witness_count; index++)
        if (derivation->witness_ids[index] == witness) return true;
    return false;
}

static size_t derivation_rule_count(
    Arena *arena, const TermUniverse *universe,
    const CettaPrimeTypedDerivationViewV1 *derivation,
    const char *rule_name) {
    if (!arena || !universe || !derivation || !rule_name) return 0u;
    Atom *rule = atom_symbol(arena, rule_name);
    size_t count = 0u;
    for (size_t index = 0u; index < derivation->node_count; index++)
        if (term_universe_atom_id_eq(
                universe, derivation->nodes[index].rule_id, rule)) {
            count++;
        }
    return count;
}

static size_t atom_symbol_occurrences(Atom *atom, const char *name) {
    if (!atom || !name) return 0u;
    if (atom->kind == ATOM_SYMBOL)
        return atom_is_symbol(atom, name) ? 1u : 0u;
    if (atom->kind != ATOM_EXPR) return 0u;
    size_t count = 0u;
    for (CettaExprIndex index = 0u; index < atom->expr.len; index++)
        count += atom_symbol_occurrences(atom->expr.elems[index], name);
    return count;
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

    add_form(&arena, &space, "(: alice u0)");
    add_form(&arena, &space, "(: bob u0)");
    add_form(&arena, &space, "(: carol u0)");
    add_form(&arena, &space, "(: dana u0)");
    add_form(&arena, &space, "(: eve u0)");
    add_form(&arena, &space, "(: edge (-> u0 (-> u0 u1)))");
    add_form(&arena, &space, "(: alice-bob-a (app (app edge alice) bob))");
    add_form(&arena, &space, "(: alice-bob-b (app (app edge alice) bob))");
    add_form(&arena, &space, "(: bob-carol (app (app edge bob) carol))");
    add_form(&arena, &space, "(: dana-carol (app (app edge dana) carol))");
    add_form(&arena, &space, "(: sort-code (u 0))");
    add_form(&arena, &space, "(: person-sort sort-code)");
    add_form(&arena, &space, "(: number-sort sort-code)");
    add_form(
        &arena, &space,
        "(: primitive-vocabulary "
        "  (-> sort-code (-> sort-code (u 1))))");
    add_form(
        &arena, &space,
        "(: alternate-primitive-vocabulary "
        "  (-> sort-code (-> sort-code (u 1))))");
    add_form(
        &arena, &space,
        "(: mother-symbol "
        "  (primitive-vocabulary person-sort person-sort))");
    add_form(
        &arena, &space,
        "(: father-symbol "
        "  (primitive-vocabulary person-sort person-sort))");
    add_form(
        &arena, &space,
        "(: successor-symbol "
        "  (primitive-vocabulary number-sort number-sort))");
    add_form(
        &arena, &space,
        "(: alternate-father-symbol "
        "  (alternate-primitive-vocabulary person-sort person-sort))");
    add_form(
        &arena, &space, "(: mother (-> u0 (-> u0 u1)))");
    add_form(
        &arena, &space, "(: father (-> u0 (-> u0 u1)))");
    add_form(
        &arena, &space,
        "(: silent-relation (-> u0 (-> u0 u1)))");
    add_form(
        &arena, &space,
        "(: mother-alice-bob (mother alice bob))");
    add_form(
        &arena, &space,
        "(: mother-alice-eve (mother alice eve))");
    add_form(
        &arena, &space,
        "(: father-bob-carol (father bob carol))");
    add_form(
        &arena, &space,
        "(: father-eve-carol (father eve carol))");
    add_form(
        &arena, &space,
        "(: sort-relation "
        "  (-> sort-code (-> sort-code (u 0))))");
    add_form(
        &arena, &space,
        "(: sort-next (-> sort-code sort-code))");
    add_form(
        &arena, &space,
        "(: person-number-proof-a "
        "  (sort-relation person-sort number-sort))");
    add_form(
        &arena, &space,
        "(: person-number-proof-b "
        "  (sort-relation person-sort number-sort))");
    add_form(
        &arena, &space,
        "(: person-person-proof "
        "  (sort-relation person-sort person-sort))");
    add_file(
        &arena, &space, "lib/ilp/prime_native_hyp_types.metta");
    add_file(
        &arena, &space, "lib/ilp/prime_native_list_types.metta");
    add_file(
        &arena, &space, "lib/ilp/prime_relational_combinators_types.metta");
    add_form(
        &arena, &space,
        "(: sort-predicate "
        "  (-> (value : sort-code) (u 0)))");
    add_form(
        &arena, &space,
        "(: alternate-sort-predicate "
        "  (-> (value : sort-code) (u 0)))");
    add_form(
        &arena, &space,
        "(: person-predicate-proof-a "
        "  (sort-predicate person-sort))");
    add_form(
        &arena, &space,
        "(: person-predicate-proof-b "
        "  (sort-predicate person-sort))");
    add_form(
        &arena, &space,
        "(: alternate-person-predicate-proof "
        "  (alternate-sort-predicate person-sort))");
    add_form(
        &arena, &space,
        "(: sort-fold-step "
        "  (-> (before : sort-code) "
        "      (head : sort-code) "
        "      (after : sort-code) "
        "      (u 0)))");
    add_form(
        &arena, &space,
        "(: sort-fold-step-proof "
        "  (sort-fold-step person-sort person-sort number-sort))");
    add_form(
        &arena, &space,
        "(: rel:all:nil-lookalike "
        "  (-> (element : (u $element-level)) "
        "      (predicate : "
        "        (-> (value : element) (u $evidence-level))) "
        "      (rel:all element predicate (list:nil element))))");
    add_form(
        &arena, &space,
        "(: rel:fold:nil-lookalike "
        "  (-> (element : (u $element-level)) "
        "      (accumulator : (u $accumulator-level)) "
        "      (step : "
        "        (-> (before : accumulator) (head : element) "
        "            (after : accumulator) (u $evidence-level))) "
        "      (before : accumulator) "
        "      (rel:fold element accumulator step before "
        "        (list:nil element) before)))");
    add_form(
        &arena, &space,
        "(: rel:iterate:zero-lookalike "
        "  (-> (value : (u $value-level)) "
        "      (counter : (u $counter-level)) "
        "      (step : "
        "        (-> (source : value) (target : value) "
        "            (u $evidence-level))) "
        "      (predecessor : "
        "        (-> (later : counter) (earlier : counter) "
        "            (u $evidence-level))) "
        "      (zero : counter) "
        "      (source : value) "
        "      (rel:iterate value counter step predecessor zero "
        "        source zero source)))");
    add_form(
        &arena, &space,
        "(: list-result-type "
        "  (-> (xs : (list sort-code)) (u 0)))");
    add_form(
        &arena, &space,
        "(: list-result-nil "
        "  (list-result-type (list:nil sort-code)))");
    add_form(
        &arena, &space,
        "(: list-result-cons "
        "  (-> (head : sort-code) "
        "      (tail : (list sort-code)) "
        "      (induction : (list-result-type tail)) "
        "      (list-result-type "
        "        (list:cons sort-code head tail))))");
    add_form(
        &arena, &space,
        "(: list:eliminate-lookalike "
        "  (-> (element : (u $element-level)) "
        "      (motive : "
        "        (-> (xs : (list element)) "
        "            (u $motive-level))) "
        "      (nil-case : (motive (list:nil element))) "
        "      (cons-case : "
        "        (-> (head : element) "
        "            (tail : (list element)) "
        "            (induction : (motive tail)) "
        "            (motive (list:cons element head tail)))) "
        "      (xs : (list element)) "
        "      (motive xs)))");
    add_file(
        &arena, &space,
        "lib/ilp/prime_native_list_relator_types.metta");
    add_form(
        &arena, &space,
        "(: map-rel:nil-lookalike "
        "  (-> (source : (u $source-level)) "
        "      (target : (u $target-level)) "
        "      (relation : "
        "        (-> source (-> target (u $evidence-level)))) "
        "      (map-rel source target relation "
        "        (list:nil source) (list:nil target))))");
    add_form(
        &arena, &space,
        "(: map-rel-result-type "
        "  (-> (source-list : (list sort-code)) "
        "      (target-list : (list sort-code)) "
        "      (evidence : "
        "        (map-rel sort-code sort-code sort-relation "
        "          source-list target-list)) "
        "      (u 0)))");
    add_form(
        &arena, &space,
        "(: map-rel-result-nil "
        "  (map-rel-result-type "
        "    (list:nil sort-code) (list:nil sort-code) "
        "    (map-rel:nil sort-code sort-code sort-relation)))");
    add_form(
        &arena, &space,
        "(: map-rel-result-cons "
        "  (-> (source-head : sort-code) "
        "      (target-head : sort-code) "
        "      (source-tail : (list sort-code)) "
        "      (target-tail : (list sort-code)) "
        "      (head-evidence : "
        "        (sort-relation source-head target-head)) "
        "      (tail-evidence : "
        "        (map-rel sort-code sort-code sort-relation "
        "          source-tail target-tail)) "
        "      (induction : "
        "        (map-rel-result-type "
        "          source-tail target-tail tail-evidence)) "
        "      (map-rel-result-type "
        "        (list:cons sort-code source-head source-tail) "
        "        (list:cons sort-code target-head target-tail) "
        "        (map-rel:cons sort-code sort-code sort-relation "
        "          source-head target-head source-tail target-tail "
        "          head-evidence tail-evidence))))");
    add_form(
        &arena, &space,
        "(: map-rel:eliminate-lookalike "
        "  (-> (source : (u $source-level)) "
        "      (target : (u $target-level)) "
        "      (relation : "
        "        (-> (source-value : source) "
        "            (target-value : target) "
        "            (u $evidence-level))) "
        "      (motive : "
        "        (-> (source-list : (list source)) "
        "            (target-list : (list target)) "
        "            (evidence : "
        "              (map-rel source target relation "
        "                source-list target-list)) "
        "            (u $motive-level))) "
        "      (nil-case : "
        "        (motive (list:nil source) (list:nil target) "
        "          (map-rel:nil source target relation))) "
        "      (cons-case : "
        "        (-> (source-head : source) "
        "            (target-head : target) "
        "            (source-tail : (list source)) "
        "            (target-tail : (list target)) "
        "            (head-evidence : "
        "              (relation source-head target-head)) "
        "            (tail-evidence : "
        "              (map-rel source target relation "
        "                source-tail target-tail)) "
        "            (induction : "
        "              (motive source-tail target-tail tail-evidence)) "
        "            (motive "
        "              (list:cons source source-head source-tail) "
        "              (list:cons target target-head target-tail) "
        "              (map-rel:cons source target relation "
        "                source-head target-head source-tail target-tail "
        "                head-evidence tail-evidence)))) "
        "      (source-list : (list source)) "
        "      (target-list : (list target)) "
        "      (evidence : "
        "        (map-rel source target relation source-list target-list)) "
        "      (motive source-list target-list evidence)))");
    add_file(
        &arena, &space, "lib/ilp/prime_native_hyp.metta");
    /* This deliberately structured test head is indexed through Space's
       wildcard equation cursor.  Its RHS only consumes `hyp:run`; it is not
       another definition of that operation and must not invalidate the
       exact authored profile. */
    add_form(
        &arena, &space,
        "(= ((test:structured-observer (quote $program)) $input) "
        "   (let (hyp:edge $target $proof) "
        "        (hyp:run (quote $program) $input) "
        "     (rel:edge $target $proof)))");
    add_form(
        &arena, &space,
        "(= (hyp:primitive-declaration $bias $primitives) "
        "   (match $bias "
        "     (: $symbol ($primitives $source-sort $target-sort)) "
        "     (hyp:declaration $source-sort $target-sort $symbol)))");
    add_form(
        &arena, &space,
        "(= (hyp:chain-candidate-typed "
        "      $bias $sorts $primitives $source-sort $target-sort) "
        "   (let (hyp:declaration "
        "          $source-sort $middle-sort $earlier-symbol) "
        "        (hyp:primitive-declaration $bias $primitives) "
        "     (let (hyp:declaration "
        "            $middle-sort $target-sort $later-symbol) "
        "          (hyp:primitive-declaration $bias $primitives) "
        "       (quote "
        "         (hyp:chain $sorts $primitives "
        "           $source-sort $middle-sort $target-sort "
        "           (hyp:primitive $sorts $primitives "
        "             $source-sort $middle-sort $earlier-symbol) "
        "           (hyp:primitive $sorts $primitives "
        "             $middle-sort $target-sort $later-symbol))))))");
    add_file(
        &arena, &space, "lib/ilp/prime_native_list_relator.metta");
    add_form(
        &arena, &space,
        "(= (hyp:carrier person-sort) u0)");
    add_form(
        &arena, &space,
        "(= (hyp:carrier number-sort) sort-code)");
    add_form(
        &arena, &space,
        "(= (hyp:meaning person-sort person-sort mother-symbol) mother)");
    add_form(
        &arena, &space,
        "(= (hyp:meaning person-sort person-sort father-symbol) father)");
    add_form(
        &arena, &space,
        "(= (mother alice) "
        "   (rel:edge bob mother-alice-bob))");
    add_form(
        &arena, &space,
        "(= (mother alice) "
        "   (rel:edge eve mother-alice-eve))");
    add_form(
        &arena, &space,
        "(= (father bob) "
        "   (rel:edge carol father-bob-carol))");
    add_form(
        &arena, &space,
        "(= (father eve) "
        "   (rel:edge carol father-eve-carol))");
    add_form(
        &arena, &space,
        "(= (sort-step person-sort) "
        "   (rel:edge number-sort person-number-proof-a))");
    add_form(
        &arena, &space,
        "(= (sort-step person-sort) "
        "   (rel:edge number-sort person-number-proof-b))");

    CettaPrimeTypedValueV1 *node = import_native(
        &arena, &space, "U0");
    CettaPrimeTypedValueV1 *evidence_universe = import_native(
        &arena, &space, "U1");
    CettaPrimeTypedValueV1 *edge = import_native(
        &arena, &space, "edge");
    CettaPrimeTypedValueV1 *alice = import_native(
        &arena, &space, "alice");
    CettaPrimeTypedValueV1 *bob = import_native(
        &arena, &space, "bob");
    CettaPrimeTypedValueV1 *carol = import_native(
        &arena, &space, "carol");
    CettaPrimeTypedValueV1 *dana = import_native(
        &arena, &space, "dana");
    CettaPrimeTypedValueV1 *alice_bob_a = import_native(
        &arena, &space, "alice-bob-a");
    CettaPrimeTypedValueV1 *alice_bob_b = import_native(
        &arena, &space, "alice-bob-b");
    CettaPrimeTypedValueV1 *bob_carol = import_native(
        &arena, &space, "bob-carol");
    CettaPrimeTypedValueV1 *dana_carol = import_native(
        &arena, &space, "dana-carol");
    CettaPrimeTypedValueV1 *mother_alice_bob = import_native(
        &arena, &space, "mother-alice-bob");
    CettaPrimeTypedValueV1 *mother_alice_eve = import_native(
        &arena, &space, "mother-alice-eve");
    CettaPrimeTypedValueV1 *father_bob_carol = import_native(
        &arena, &space, "father-bob-carol");
    CettaPrimeTypedValueV1 *father_eve_carol = import_native(
        &arena, &space, "father-eve-carol");
    (void)mother_alice_bob;
    (void)mother_alice_eve;
    (void)father_bob_carol;
    (void)father_eve_carol;
    CettaPrimeTypedValueV1 *sort_code = import_native(
        &arena, &space, "sort-code");
    CettaPrimeTypedValueV1 *tower_u0 = import_native(
        &arena, &space, "(u 0)");
    CettaPrimeTypedValueV1 *person_sort = import_native(
        &arena, &space, "person-sort");
    CettaPrimeTypedValueV1 *number_sort = import_native(
        &arena, &space, "number-sort");
    CettaPrimeTypedValueV1 *primitive_vocabulary = import_native(
        &arena, &space, "primitive-vocabulary");
    CettaPrimeTypedValueV1 *mother_symbol = import_native(
        &arena, &space, "mother-symbol");
    CettaPrimeTypedValueV1 *father_symbol = import_native(
        &arena, &space, "father-symbol");
    CettaPrimeTypedValueV1 *successor_symbol = import_native(
        &arena, &space, "successor-symbol");
    CettaPrimeTypedValueV1 *alternate_father_symbol = import_native(
        &arena, &space, "alternate-father-symbol");
    CettaPrimeTypedValueV1 *sort_relation = import_native(
        &arena, &space, "sort-relation");
    CettaPrimeTypedValueV1 *sort_next = import_native(
        &arena, &space, "sort-next");
    CettaPrimeTypedValueV1 *person_number_proof_a = import_native(
        &arena, &space, "person-number-proof-a");
    CettaPrimeTypedValueV1 *person_number_proof_b = import_native(
        &arena, &space, "person-number-proof-b");
    CettaPrimeTypedValueV1 *person_person_proof = import_native(
        &arena, &space, "person-person-proof");
    CettaPrimeTypedValueV1 *authored_primitive_rule = import_native(
        &arena, &space,
        "(hyp:primitive sort-code primitive-vocabulary)");
    CettaPrimeTypedValueV1 *authored_chain_rule = import_native(
        &arena, &space,
        "(hyp:chain sort-code primitive-vocabulary)");
    CettaPrimeTypedValueV1 *alternate_primitive_rule = import_native(
        &arena, &space,
        "(hyp:primitive sort-code alternate-primitive-vocabulary)");
    CettaPrimeTypedValueV1 *list_nil_rule = import_native(
        &arena, &space, "list:nil");
    CettaPrimeTypedValueV1 *list_family = import_native(
        &arena, &space, "list");
    CettaPrimeTypedValueV1 *list_cons_rule = import_native(
        &arena, &space, "list:cons");
    CettaPrimeTypedValueV1 *list_eliminate_rule = import_native(
        &arena, &space, "list:eliminate");
    CettaPrimeTypedValueV1 *list_eliminate_lookalike = import_native(
        &arena, &space, "list:eliminate-lookalike");
    CettaPrimeTypedValueV1 *list_result_type = import_native(
        &arena, &space, "list-result-type");
    CettaPrimeTypedValueV1 *list_result_nil = import_native(
        &arena, &space, "list-result-nil");
    CettaPrimeTypedValueV1 *list_result_cons = import_native(
        &arena, &space, "list-result-cons");
    CettaPrimeTypedValueV1 *native_map_type = import_native(
        &arena, &space,
        "(-> (source : (u 0)) "
        "    (target : (u 0)) "
        "    (function : (-> source target)) "
        "    (xs : (list source)) "
        "    (list target))");
    CettaPrimeTypedValueV1 *native_map_program = import_checked(
        &arena, &space,
        "(lam source "
        "  (lam target "
        "    (lam function "
        "      (lam xs "
        "        (list:eliminate source "
        "          (lam ignored (list target)) "
        "          (list:nil target) "
        "          (lam head "
        "            (lam tail "
        "              (lam induction "
        "                (list:cons target "
        "                  (function head) induction)))) "
        "          xs)))))",
        native_map_type);
    CettaPrimeTypedValueV1 *native_map_lookalike = import_checked(
        &arena, &space,
        "(lam source "
        "  (lam target "
        "    (lam function "
        "      (lam xs (list:nil target)))))",
        native_map_type);
    CettaPrimeTypedValueV1 *map_rel_nil_rule = import_native(
        &arena, &space, "map-rel:nil");
    CettaPrimeTypedValueV1 *map_rel_family = import_native(
        &arena, &space, "map-rel");
    CettaPrimeTypedValueV1 *map_rel_cons_rule = import_native(
        &arena, &space, "map-rel:cons");
    CettaPrimeTypedValueV1 *map_rel_eliminate_rule = import_native(
        &arena, &space, "map-rel:eliminate");
    CettaPrimeTypedValueV1 *map_rel_nil_lookalike = import_native(
        &arena, &space, "map-rel:nil-lookalike");
    CettaPrimeTypedValueV1 *map_rel_eliminate_lookalike = import_native(
        &arena, &space, "map-rel:eliminate-lookalike");
    CettaPrimeTypedValueV1 *map_rel_result_type = import_native(
        &arena, &space, "map-rel-result-type");
    CettaPrimeTypedValueV1 *map_rel_result_nil = import_native(
        &arena, &space, "map-rel-result-nil");
    CettaPrimeTypedValueV1 *map_rel_result_cons = import_native(
        &arena, &space, "map-rel-result-cons");
    CettaPrimeTypedValueV1 *all_nil_rule = import_native(
        &arena, &space, "rel:all:nil");
    CettaPrimeTypedValueV1 *all_cons_rule = import_native(
        &arena, &space, "rel:all:cons");
    CettaPrimeTypedValueV1 *all_nil_lookalike = import_native(
        &arena, &space, "rel:all:nil-lookalike");
    CettaPrimeTypedValueV1 *sort_predicate = import_native(
        &arena, &space, "sort-predicate");
    CettaPrimeTypedValueV1 *alternate_sort_predicate = import_native(
        &arena, &space, "alternate-sort-predicate");
    CettaPrimeTypedValueV1 *person_predicate_proof_a = import_native(
        &arena, &space, "person-predicate-proof-a");
    CettaPrimeTypedValueV1 *person_predicate_proof_b = import_native(
        &arena, &space, "person-predicate-proof-b");
    CettaPrimeTypedValueV1 *fold_nil_rule = import_native(
        &arena, &space, "rel:fold:nil");
    CettaPrimeTypedValueV1 *fold_cons_rule = import_native(
        &arena, &space, "rel:fold:cons");
    CettaPrimeTypedValueV1 *fold_nil_lookalike = import_native(
        &arena, &space, "rel:fold:nil-lookalike");
    CettaPrimeTypedValueV1 *sort_fold_step = import_native(
        &arena, &space, "sort-fold-step");
    CettaPrimeTypedValueV1 *sort_fold_step_proof = import_native(
        &arena, &space, "sort-fold-step-proof");
    CettaPrimeTypedValueV1 *iterate_zero_rule = import_native(
        &arena, &space, "rel:iterate:zero");
    CettaPrimeTypedValueV1 *iterate_step_rule = import_native(
        &arena, &space, "rel:iterate:step");
    CettaPrimeTypedValueV1 *iterate_zero_lookalike = import_native(
        &arena, &space, "rel:iterate:zero-lookalike");
    CettaPrimeTypedValueV1 *boundary_empty_sort_map_rel = import_native(
        &arena, &space,
        "(map-rel:nil sort-code sort-code sort-relation)");
    CettaPrimeTypedValueV1 *graph_relation_type =
        cetta_prime_typed_rel_type_v1(
            &arena, &space, sort_code, sort_code, tower_u0);
    CettaPrimeTypedValueV1 *graph_relation = import_checked(
        &arena, &space,
        "(lam source "
        "  (lam target "
        "    (id sort-code (sort-next source) target)))",
        graph_relation_type);
    CettaPrimeTypedValueV1 *graph_relation_lookalike = import_checked(
        &arena, &space,
        "(lam source "
        "  (lam target "
        "    (id sort-code source target)))",
        graph_relation_type);
    CettaPrimeTypedValueV1 *mapped_person_head =
        cetta_prime_typed_value_apply_v1(
            &arena, &space, sort_next, person_sort);
    CettaPrimeTypedValueV1 *mapped_number_head =
        cetta_prime_typed_value_apply_v1(
            &arena, &space, sort_next, number_sort);
    CettaPrimeTypedValueV1 *graph_person_arguments[] = {
        person_sort, mapped_person_head,
    };
    CettaPrimeTypedValueV1 *graph_number_arguments[] = {
        number_sort, mapped_number_head,
    };
    CettaPrimeTypedValueV1 *graph_person_fibre = typed_apply_many(
        &arena, &space, graph_relation, graph_person_arguments,
        sizeof(graph_person_arguments) / sizeof(graph_person_arguments[0]));
    CettaPrimeTypedValueV1 *graph_number_fibre = typed_apply_many(
        &arena, &space, graph_relation, graph_number_arguments,
        sizeof(graph_number_arguments) / sizeof(graph_number_arguments[0]));
    CettaPrimeTypedValueV1 *graph_lookalike_fibre = typed_apply_many(
        &arena, &space, graph_relation_lookalike, graph_person_arguments,
        sizeof(graph_person_arguments) / sizeof(graph_person_arguments[0]));
    CettaPrimeTypedValueV1 *mapped_person_refl =
        cetta_prime_typed_value_refl_v1(
            &arena, &space, mapped_person_head);
    CettaPrimeTypedValueV1 *mapped_number_refl =
        cetta_prime_typed_value_refl_v1(
            &arena, &space, mapped_number_head);

#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    CettaPrimeTypedValueV1 *edge_relation_type =
        cetta_prime_typed_rel_type_v1(
            &arena, &space, node, node, evidence_universe);
    CettaPrimeTypedValueV1 *grandparent_chain_type =
        cetta_prime_typed_chain_type_v1(
            &arena, &space, node, edge, edge, alice, carol);
    CettaPrimeTypedValueV1 *grandparent_chain_a =
        cetta_prime_typed_chain_v1(
            &arena, &space, grandparent_chain_type,
            bob, alice_bob_a, bob_carol);
    CettaPrimeTypedValueV1 *grandparent_chain_b =
        cetta_prime_typed_chain_v1(
            &arena, &space, grandparent_chain_type,
            bob, alice_bob_b, bob_carol);
    CettaPrimeTypedValueV1 *grandparent_relation_result_type =
        cetta_prime_typed_relation_chain_result_type_v1(
            &arena, &space, node, node, node, edge, edge);
    CettaPrimeTypedValueV1 *grandparent_relation =
        cetta_prime_typed_relation_chain_v1(
            &arena, &space, grandparent_relation_result_type,
            node, edge, edge);
    CettaPrimeTypedValueV1 *grandparent_relation_at_alice =
        cetta_prime_typed_value_apply_v1(
            &arena, &space, grandparent_relation, alice);
    CettaPrimeTypedValueV1 *grandparent_relation_fibre =
        cetta_prime_typed_value_apply_v1(
            &arena, &space, grandparent_relation_at_alice, carol);
    CettaPrimeTypedValueV1 *grandparent_relation_evidence =
        cetta_prime_typed_value_convert_beta_v1(
            &arena, &space, grandparent_chain_a,
            grandparent_relation_fibre);
    CettaPrimeTypedValueV1 *edge_alice_answer_type =
        cetta_prime_typed_relation_answer_type_v1(
            &arena, &space, edge, alice, node);
    CettaPrimeTypedValueV1 *edge_alice_bob_answer_a =
        cetta_prime_typed_relation_answer_v1(
            &arena, &space, edge, alice, node,
            bob, alice_bob_a);
    CettaPrimeTypedValueV1 *edge_alice_bob_answer_b =
        cetta_prime_typed_relation_answer_v1(
            &arena, &space, edge, alice, node,
            bob, alice_bob_b);
    CettaPrimeTypedValueV1 *edge_alice_bob_answer_a_again =
        cetta_prime_typed_relation_answer_v1(
            &arena, &space, edge, alice, node,
            bob, alice_bob_a);
    CettaPrimeTypedValueV1 *grandparent_answer_type =
        cetta_prime_typed_relation_answer_type_v1(
            &arena, &space, grandparent_relation, alice, node);
    CettaPrimeTypedValueV1 *grandparent_answer =
        cetta_prime_typed_relation_answer_v1(
            &arena, &space, grandparent_relation, alice, node,
            carol, grandparent_relation_evidence);
    CettaPrimeTypedValueV1 *grandparent_answer_nil_exact =
        cetta_prime_typed_value_apply_v1(
            &arena, &space, list_nil_rule, grandparent_answer_type);
    CettaPrimeTypedValueV1 *grandparent_answer_nil_converting =
        cetta_prime_typed_value_apply_converting_v1(
            &arena, &space, list_nil_rule, grandparent_answer_type);
    CettaPrimeTypedValueV1 *ill_typed_nil_converting =
        cetta_prime_typed_value_apply_converting_v1(
            &arena, &space, list_nil_rule, alice);
    CettaPrimeTypedValueV1 *grandparent_answer_list_nil =
        cetta_prime_typed_list_nil_v1(
            &arena, &space, list_nil_rule, grandparent_answer_type);
    CettaPrimeTypedValueV1 *grandparent_answer_list_one =
        cetta_prime_typed_list_cons_v1(
            &arena, &space, list_cons_rule, grandparent_answer_type,
            grandparent_answer, grandparent_answer_list_nil);
    CettaPrimeTypedFiniteRelationOccurrenceInputV1 edge_occurrences[] = {
        {alice, bob, alice_bob_a},
        {alice, bob, alice_bob_b},
        {bob, carol, bob_carol},
        {dana, carol, dana_carol},
    };
    CettaPrimeTypedFiniteRelationV1 *edge_provider = NULL;
    CettaPrimeTypedFiniteRelationBuildV1 edge_provider_built =
        cetta_prime_typed_finite_relation_create_v1(
            &arena, &space, node, node, edge,
            edge_occurrences,
            sizeof(edge_occurrences) / sizeof(edge_occurrences[0]),
            &edge_provider);
    CettaPrimeTypedFiniteRelationOccurrenceViewV1 edge_occurrence_one = {0};
    CHECK(edge_provider_built ==
              CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 &&
              edge_provider &&
              cetta_prime_typed_finite_relation_is_current_v1(
                  edge_provider, &space) &&
              cetta_prime_typed_finite_relation_occurrence_count_v1(
                  edge_provider) == 4u &&
              cetta_prime_typed_finite_relation_occurrence_v1(
                  edge_provider, 1u, &edge_occurrence_one) &&
              atom_eq(
                  erase_term(
                      &arena, &universe, edge_occurrence_one.source),
                  erase_term(&arena, &universe, alice)) &&
              atom_eq(
                  erase_term(
                      &arena, &universe, edge_occurrence_one.target),
                  erase_term(&arena, &universe, bob)) &&
              atom_eq(
                  erase_term(
                      &arena, &universe, edge_occurrence_one.evidence),
                  erase_term(&arena, &universe, alice_bob_b)),
          "a generic finite relation provider retains the exact authored typed occurrence bag independently of hyp");
    CettaPrimeTypedFiniteRelationOccurrenceInputV1
        ill_fibred_edge_occurrence = {alice, bob, bob_carol};
    CettaPrimeTypedFiniteRelationV1 *ill_fibred_edge_provider =
        edge_provider;
    CHECK(cetta_prime_typed_finite_relation_create_v1(
              &arena, &space, node, node, edge,
              &ill_fibred_edge_occurrence, 1u,
              &ill_fibred_edge_provider) ==
              CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1 &&
              ill_fibred_edge_provider == NULL,
          "a finite relation provider declines evidence from the wrong dependent fibre");
    CettaPrimeTypedFiniteRelationV1 *edge_chain_provider = NULL;
    CettaPrimeTypedFiniteRelationChainOriginV1 *edge_chain_origins = NULL;
    CettaPrimeTypedFiniteRelationBuildV1 edge_chain_built =
        cetta_prime_typed_finite_relation_chain_v1(
            &arena, &space, edge_provider, edge_provider,
            &edge_chain_provider, &edge_chain_origins);
    CettaPrimeTypedFiniteRelationOccurrenceViewV1 edge_chain_first = {0};
    CettaPrimeTypedFiniteRelationOccurrenceViewV1 edge_chain_second = {0};
    CHECK(edge_chain_built ==
              CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 &&
              edge_chain_provider && edge_chain_origins &&
              cetta_prime_typed_finite_relation_occurrence_count_v1(
                  edge_chain_provider) == 2u &&
              edge_chain_origins[0].earlier_index == 0u &&
              edge_chain_origins[0].later_index == 2u &&
              edge_chain_origins[1].earlier_index == 1u &&
              edge_chain_origins[1].later_index == 2u &&
              cetta_prime_typed_finite_relation_occurrence_v1(
                  edge_chain_provider, 0u, &edge_chain_first) &&
              cetta_prime_typed_finite_relation_occurrence_v1(
                  edge_chain_provider, 1u, &edge_chain_second) &&
              atom_eq(
                  erase_term(
                      &arena, &universe, edge_chain_first.source),
                  erase_term(&arena, &universe, alice)) &&
              atom_eq(
                  erase_term(
                      &arena, &universe, edge_chain_first.target),
                  erase_term(&arena, &universe, carol)) &&
              !atom_eq(
                  erase_term(
                      &arena, &universe, edge_chain_first.evidence),
                  erase_term(
                      &arena, &universe, edge_chain_second.evidence)),
          "generic finite relation composition preserves shared-middle origins and distinct proof evidence for equal endpoints");
    CettaPrimeTypedFiniteRelationSearchV1 edge_chain_search = {0};
    CettaPrimeTypedFiniteRelationBuildV1 edge_chain_searched =
        cetta_prime_typed_finite_relation_search_v1(
            &arena, &space, edge_chain_provider, alice,
            list_nil_rule, list_cons_rule, &edge_chain_search);
    CettaPrimeTypedValueMetadataV1 edge_chain_search_metadata = {0};
    CHECK(edge_chain_searched ==
              CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 &&
              edge_chain_search.answer_count == 2u &&
              edge_chain_search.occurrence_indices &&
              edge_chain_search.occurrence_indices[0] == 0u &&
              edge_chain_search.occurrence_indices[1] == 1u &&
              edge_chain_search.receipt &&
              cetta_prime_typed_value_v1_metadata(
                  edge_chain_search.receipt,
                  &edge_chain_search_metadata) &&
              term_universe_atom_id_eq(
                  &universe, edge_chain_search_metadata.rule_id,
                  atom_symbol(&arena, "rel:finite-search")),
          "generic finite relation search materializes the complete ordered dependent source fibre");
    Arena retained_provider_arena;
    arena_init(&retained_provider_arena);
    arena_set_runtime_kind(
        &retained_provider_arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    CettaPrimeTypedFiniteRelationV1 *retained_edge_provider =
        cetta_prime_typed_finite_relation_retain_v1(
            &retained_provider_arena, &space, edge_provider);
    CettaPrimeTypedFiniteRelationOccurrenceViewV1
        retained_edge_occurrence_one = {0};
    CettaPrimeTypedValueMetadataV1 edge_occurrence_evidence_metadata = {0};
    CettaPrimeTypedValueMetadataV1
        retained_edge_occurrence_evidence_metadata = {0};
    CHECK(retained_edge_provider &&
              retained_edge_provider != edge_provider &&
              cetta_prime_typed_finite_relation_occurrence_v1(
                  retained_edge_provider, 1u,
                  &retained_edge_occurrence_one) &&
              cetta_prime_typed_value_v1_metadata(
                  edge_occurrence_one.evidence,
                  &edge_occurrence_evidence_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  retained_edge_occurrence_one.evidence,
                  &retained_edge_occurrence_evidence_metadata) &&
              retained_edge_occurrence_evidence_metadata.
                      occurrence_identity ==
                  edge_occurrence_evidence_metadata.occurrence_identity,
          "retaining a finite relation provider transfers ownership without replaying its proof occurrences");
    arena_free(&retained_provider_arena);
    CettaPrimeTypedValueV1 *wrong_target_answer_type =
        cetta_prime_typed_relation_answer_type_v1(
            &arena, &space, edge, alice, sort_code);
    CettaPrimeTypedValueV1 *wrong_source_answer =
        cetta_prime_typed_relation_answer_v1(
            &arena, &space, edge, dana, node,
            bob, alice_bob_a);
    CettaPrimeTypedValueV1 *wrong_evidence_answer =
        cetta_prime_typed_relation_answer_v1(
            &arena, &space, edge, alice, node,
            bob, bob_carol);
    CettaPrimeTypedValueV1 *small_relation_type =
        cetta_prime_typed_rel_type_v1(
            &arena, &space, node, node, tower_u0);
    CettaPrimeTypedValueV1 *wrong_evidence_relation_chain =
        cetta_prime_typed_relation_chain_v1(
            &arena, &space, small_relation_type, node, edge, edge);
    CettaPrimeTypedValueV1 *wrong_middle_relation_chain =
        cetta_prime_typed_relation_chain_v1(
            &arena, &space, edge_relation_type,
            sort_code, edge, edge);
    CettaPrimeTypedValueV1 *wrong_endpoint_relation_chain =
        cetta_prime_typed_relation_chain_v1(
            &arena, &space, graph_relation_type, node, edge, edge);
    CettaPrimeTypedValueV1 *wrong_middle_relation_result_type =
        cetta_prime_typed_relation_chain_result_type_v1(
            &arena, &space, node, sort_code, node, edge, edge);
    CettaPrimeTypedValueV1 *wrong_endpoint_relation_result_type =
        cetta_prime_typed_relation_chain_result_type_v1(
            &arena, &space, sort_code, node, node, edge, edge);
    CettaPrimeTypedValueV1 *mother_hyp_a =
        cetta_prime_typed_hyp_primitive_v1(
            &arena, &space, authored_primitive_rule,
            person_sort, person_sort, mother_symbol);
    CettaPrimeTypedValueV1 *mother_hyp_duplicate =
        cetta_prime_typed_hyp_primitive_v1(
            &arena, &space, authored_primitive_rule,
            person_sort, person_sort, mother_symbol);
    CettaPrimeTypedValueV1 *mother_hyp_b =
        cetta_prime_typed_hyp_primitive_v1(
            &arena, &space, authored_primitive_rule,
            person_sort, person_sort, mother_symbol);
    CettaPrimeTypedValueV1 *father_hyp =
        cetta_prime_typed_hyp_primitive_v1(
            &arena, &space, authored_primitive_rule,
            person_sort, person_sort, father_symbol);
    CettaPrimeTypedValueV1 *successor_hyp =
        cetta_prime_typed_hyp_primitive_v1(
            &arena, &space, authored_primitive_rule,
            number_sort, number_sort, successor_symbol);
    CettaPrimeTypedValueV1 *alternate_father_hyp =
        cetta_prime_typed_hyp_primitive_v1(
            &arena, &space, alternate_primitive_rule,
            person_sort, person_sort, alternate_father_symbol);
    CettaPrimeTypedValueV1 *grandparent_hyp_a =
        cetta_prime_typed_hyp_chain_v1(
            &arena, &space, authored_chain_rule,
            person_sort, person_sort, person_sort,
            mother_hyp_a, father_hyp);
    CettaPrimeTypedValueV1 *grandparent_hyp_b =
        cetta_prime_typed_hyp_chain_v1(
            &arena, &space, authored_chain_rule,
            person_sort, person_sort, person_sort,
            mother_hyp_b, father_hyp);
    CettaPrimeTypedValueV1 *authored_mother_arguments[] = {
        person_sort, person_sort, mother_symbol,
    };
    CettaPrimeTypedValueV1 *authored_father_arguments[] = {
        person_sort, person_sort, father_symbol,
    };
    CettaPrimeTypedValueV1 *authored_mother = typed_apply_many(
        &arena, &space, authored_primitive_rule,
        authored_mother_arguments,
        sizeof(authored_mother_arguments) /
            sizeof(authored_mother_arguments[0]));
    CettaPrimeTypedValueV1 *authored_mother_duplicate = typed_apply_many(
        &arena, &space, authored_primitive_rule,
        authored_mother_arguments,
        sizeof(authored_mother_arguments) /
            sizeof(authored_mother_arguments[0]));
    CettaPrimeTypedValueV1 *authored_father = typed_apply_many(
        &arena, &space, authored_primitive_rule,
        authored_father_arguments,
        sizeof(authored_father_arguments) /
            sizeof(authored_father_arguments[0]));
    CettaPrimeTypedValueV1 *authored_grandparent_arguments[] = {
        person_sort, person_sort, person_sort,
        authored_mother, authored_father,
    };
    CettaPrimeTypedValueV1 *authored_grandparent = typed_apply_many(
        &arena, &space, authored_chain_rule,
        authored_grandparent_arguments,
        sizeof(authored_grandparent_arguments) /
            sizeof(authored_grandparent_arguments[0]));
    CettaPrimeTypedValueV1 *empty_sort_list =
        cetta_prime_typed_list_nil_v1(
            &arena, &space, list_nil_rule, sort_code);
    CettaPrimeTypedValueV1 *number_sort_list =
        cetta_prime_typed_list_cons_v1(
            &arena, &space, list_cons_rule, sort_code,
            number_sort, empty_sort_list);
    CettaPrimeTypedValueV1 *person_number_sort_list =
        cetta_prime_typed_list_cons_v1(
            &arena, &space, list_cons_rule, sort_code,
            person_sort, number_sort_list);
    CettaPrimeTypedValueV1 *person_sort_list =
        cetta_prime_typed_list_cons_v1(
            &arena, &space, list_cons_rule, sort_code,
            person_sort, empty_sort_list);
    CettaPrimeTypedValueV1 *person_person_sort_list =
        cetta_prime_typed_list_cons_v1(
            &arena, &space, list_cons_rule, sort_code,
            person_sort, person_sort_list);
    CettaPrimeTypedValueV1 *number_number_sort_list =
        cetta_prime_typed_list_cons_v1(
            &arena, &space, list_cons_rule, sort_code,
            number_sort, number_sort_list);
    CettaPrimeTypedValueV1 *empty_sort_all =
        cetta_prime_typed_list_all_nil_v1(
            &arena, &space, all_nil_rule,
            sort_code, sort_predicate);
    CettaPrimeTypedValueV1 *empty_alternate_sort_all =
        cetta_prime_typed_list_all_nil_v1(
            &arena, &space, all_nil_rule,
            sort_code, alternate_sort_predicate);
    CettaPrimeTypedValueV1 *person_sort_all_a =
        cetta_prime_typed_list_all_cons_v1(
            &arena, &space, all_cons_rule,
            sort_code, sort_predicate, person_sort, empty_sort_list,
            person_predicate_proof_a, empty_sort_all);
    CettaPrimeTypedValueV1 *person_sort_all_b =
        cetta_prime_typed_list_all_cons_v1(
            &arena, &space, all_cons_rule,
            sort_code, sort_predicate, person_sort, empty_sort_list,
            person_predicate_proof_b, empty_sort_all);
    CettaPrimeTypedValueV1 *wrong_head_sort_all =
        cetta_prime_typed_list_all_cons_v1(
            &arena, &space, all_cons_rule,
            sort_code, sort_predicate, person_sort, empty_sort_list,
            person_number_proof_a, empty_sort_all);
    CettaPrimeTypedValueV1 *wrong_tail_sort_all =
        cetta_prime_typed_list_all_cons_v1(
            &arena, &space, all_cons_rule,
            sort_code, sort_predicate, person_sort, empty_sort_list,
            person_predicate_proof_a, empty_alternate_sort_all);
    CettaPrimeTypedValueV1 *lookalike_empty_sort_all =
        cetta_prime_typed_list_all_nil_v1(
            &arena, &space, all_nil_lookalike,
            sort_code, sort_predicate);
    CettaPrimeTypedValueV1 *number_sort_fold_nil =
        cetta_prime_typed_list_fold_nil_v1(
            &arena, &space, fold_nil_rule,
            sort_code, sort_code, sort_fold_step, number_sort);
    CettaPrimeTypedValueV1 *person_sort_fold_nil =
        cetta_prime_typed_list_fold_nil_v1(
            &arena, &space, fold_nil_rule,
            sort_code, sort_code, sort_fold_step, person_sort);
    CettaPrimeTypedValueV1 *person_sort_fold =
        cetta_prime_typed_list_fold_cons_v1(
            &arena, &space, fold_cons_rule,
            sort_code, sort_code, sort_fold_step,
            person_sort, person_sort, empty_sort_list,
            number_sort, number_sort,
            sort_fold_step_proof, number_sort_fold_nil);
    CettaPrimeTypedValueV1 *wrong_step_sort_fold =
        cetta_prime_typed_list_fold_cons_v1(
            &arena, &space, fold_cons_rule,
            sort_code, sort_code, sort_fold_step,
            person_sort, person_sort, empty_sort_list,
            number_sort, number_sort,
            person_number_proof_a, number_sort_fold_nil);
    CettaPrimeTypedValueV1 *wrong_tail_sort_fold =
        cetta_prime_typed_list_fold_cons_v1(
            &arena, &space, fold_cons_rule,
            sort_code, sort_code, sort_fold_step,
            person_sort, person_sort, empty_sort_list,
            number_sort, number_sort,
            sort_fold_step_proof, person_sort_fold_nil);
    CettaPrimeTypedValueV1 *lookalike_number_sort_fold_nil =
        cetta_prime_typed_list_fold_nil_v1(
            &arena, &space, fold_nil_lookalike,
            sort_code, sort_code, sort_fold_step, number_sort);
    CettaPrimeTypedValueV1 *number_sort_iteration_zero =
        cetta_prime_typed_iteration_zero_v1(
            &arena, &space, iterate_zero_rule,
            sort_code, sort_code, sort_relation, sort_relation,
            number_sort, number_sort);
    CettaPrimeTypedValueV1 *person_number_iteration =
        cetta_prime_typed_iteration_step_v1(
            &arena, &space, iterate_step_rule,
            sort_code, sort_code, sort_relation, sort_relation,
            number_sort, person_sort, number_sort,
            person_sort, number_sort, number_sort,
            person_number_proof_a, person_number_proof_b,
            number_sort_iteration_zero);
    CettaPrimeTypedValueV1 *wrong_step_iteration =
        cetta_prime_typed_iteration_step_v1(
            &arena, &space, iterate_step_rule,
            sort_code, sort_code, sort_relation, sort_relation,
            number_sort, person_sort, number_sort,
            person_sort, number_sort, number_sort,
            person_number_proof_a, person_person_proof,
            number_sort_iteration_zero);
    CettaPrimeTypedValueV1 *wrong_recursive_iteration =
        cetta_prime_typed_iteration_step_v1(
            &arena, &space, iterate_step_rule,
            sort_code, sort_code, sort_relation, sort_relation,
            number_sort, person_sort, number_sort,
            person_sort, number_sort, number_sort,
            person_number_proof_a, person_number_proof_b,
            cetta_prime_typed_iteration_zero_v1(
                &arena, &space, iterate_zero_rule,
                sort_code, sort_code, sort_relation, sort_relation,
                number_sort, person_sort));
    CettaPrimeTypedValueV1 *lookalike_iteration_zero =
        cetta_prime_typed_iteration_zero_v1(
            &arena, &space, iterate_zero_lookalike,
            sort_code, sort_code, sort_relation, sort_relation,
            number_sort, number_sort);
    CettaPrimeTypedValueV1 *empty_sort_list_map =
        cetta_prime_typed_list_map_v1(
            &arena, &space, native_map_program,
            sort_code, sort_code, sort_next, empty_sort_list);
    CettaPrimeTypedValueV1 *person_number_sort_list_map =
        cetta_prime_typed_list_map_v1(
            &arena, &space, native_map_program,
            sort_code, sort_code, sort_next, person_number_sort_list);
    CettaPrimeTypedValueV1 *person_number_sort_list_map_again =
        cetta_prime_typed_list_map_v1(
            &arena, &space, native_map_program,
            sort_code, sort_code, sort_next, person_number_sort_list);
    CettaPrimeTypedValueV1 *lookalike_sort_list_map =
        cetta_prime_typed_list_map_v1(
            &arena, &space, native_map_lookalike,
            sort_code, sort_code, sort_next, person_number_sort_list);
    CettaPrimeTypedValueV1 *wrong_source_sort_list_map =
        cetta_prime_typed_list_map_v1(
            &arena, &space, native_map_program,
            person_sort, sort_code, sort_next, person_number_sort_list);
    CettaPrimeTypedValueV1 *wrong_function_sort_list_map =
        cetta_prime_typed_list_map_v1(
            &arena, &space, native_map_program,
            sort_code, sort_code, sort_relation,
            person_number_sort_list);
    CettaPrimeTypedValueV1 *graph_person_evidence =
        cetta_prime_typed_value_convert_beta_v1(
            &arena, &space, mapped_person_refl, graph_person_fibre);
    CettaPrimeTypedValueV1 *graph_number_evidence =
        cetta_prime_typed_value_convert_beta_v1(
            &arena, &space, mapped_number_refl, graph_number_fibre);
    CettaPrimeTypedValueV1 *graph_lookalike_evidence =
        cetta_prime_typed_value_convert_beta_v1(
            &arena, &space, mapped_person_refl, graph_lookalike_fibre);
    CettaPrimeTypedValueV1 *graph_empty_map_rel =
        cetta_prime_typed_list_map_rel_nil_v1(
            &arena, &space, map_rel_nil_rule,
            sort_code, sort_code, graph_relation);
    CettaPrimeTypedValueV1 *mapped_number_sort_list =
        cetta_prime_typed_list_cons_v1(
            &arena, &space, list_cons_rule, sort_code,
            mapped_number_head, empty_sort_list);
    CettaPrimeTypedValueV1 *graph_number_map_rel =
        cetta_prime_typed_list_map_rel_cons_v1(
            &arena, &space, map_rel_cons_rule,
            sort_code, sort_code, graph_relation,
            number_sort, mapped_number_head,
            empty_sort_list, empty_sort_list,
            graph_number_evidence, graph_empty_map_rel);
    CettaPrimeTypedValueV1 *graph_target_list =
        cetta_prime_typed_list_cons_v1(
            &arena, &space, list_cons_rule, sort_code,
            mapped_person_head, mapped_number_sort_list);
    CettaPrimeTypedValueV1 *graph_person_number_map_rel =
        cetta_prime_typed_list_map_rel_cons_v1(
            &arena, &space, map_rel_cons_rule,
            sort_code, sort_code, graph_relation,
            person_sort, mapped_person_head,
            number_sort_list, mapped_number_sort_list,
            graph_person_evidence, graph_number_map_rel);
    CettaPrimeTypedValueV1 *empty_list_elimination =
        cetta_prime_typed_list_eliminate_v1(
            &arena, &space, list_eliminate_rule,
            sort_code, list_result_type, list_result_nil,
            list_result_cons, empty_sort_list);
    CettaPrimeTypedValueV1 *person_number_list_elimination =
        cetta_prime_typed_list_eliminate_v1(
            &arena, &space, list_eliminate_rule,
            sort_code, list_result_type, list_result_nil,
            list_result_cons, person_number_sort_list);
    CettaPrimeTypedValueV1 *person_number_list_elimination_again =
        cetta_prime_typed_list_eliminate_v1(
            &arena, &space, list_eliminate_rule,
            sort_code, list_result_type, list_result_nil,
            list_result_cons, person_number_sort_list);
    CettaPrimeTypedValueV1 *lookalike_list_elimination =
        cetta_prime_typed_list_eliminate_v1(
            &arena, &space, list_eliminate_lookalike,
            sort_code, list_result_type, list_result_nil,
            list_result_cons, person_number_sort_list);
    CettaPrimeTypedValueV1 *ill_typed_list_elimination =
        cetta_prime_typed_list_eliminate_v1(
            &arena, &space, list_eliminate_rule,
            sort_code, list_result_type, person_sort,
            list_result_cons, person_number_sort_list);
    CettaPrimeTypedValueV1 *non_list_elimination =
        cetta_prime_typed_list_eliminate_v1(
            &arena, &space, list_eliminate_rule,
            sort_code, list_result_type, list_result_nil,
            list_result_cons, grandparent_hyp_a);
    CettaPrimeTypedValueV1 *empty_sort_map_rel =
        cetta_prime_typed_list_map_rel_nil_v1(
            &arena, &space, map_rel_nil_rule,
            sort_code, sort_code, sort_relation);
    CettaPrimeTypedValueV1 *person_number_map_rel_a =
        cetta_prime_typed_list_map_rel_cons_v1(
            &arena, &space, map_rel_cons_rule,
            sort_code, sort_code, sort_relation,
            person_sort, number_sort,
            empty_sort_list, empty_sort_list,
            person_number_proof_a, empty_sort_map_rel);
    CettaPrimeTypedValueV1 *person_number_map_rel_b =
        cetta_prime_typed_list_map_rel_cons_v1(
            &arena, &space, map_rel_cons_rule,
            sort_code, sort_code, sort_relation,
            person_sort, number_sort,
            empty_sort_list, empty_sort_list,
            person_number_proof_b, empty_sort_map_rel);
    CettaPrimeTypedFiniteRelationOccurrenceInputV1 sort_step_occurrences[] = {
        {person_sort, number_sort, person_number_proof_a},
        {person_sort, number_sort, person_number_proof_b},
    };
    CettaPrimeTypedFiniteRelationV1 *sort_step_provider = NULL;
    CettaPrimeTypedFiniteRelationBuildV1 sort_step_provider_built =
        cetta_prime_typed_finite_relation_create_v1(
            &arena, &space, sort_code, sort_code, sort_relation,
            sort_step_occurrences,
            sizeof(sort_step_occurrences) /
                sizeof(sort_step_occurrences[0]),
            &sort_step_provider);
    CettaPrimeTypedListMapRelFiniteV1 finite_list_lift = {0};
    CettaPrimeTypedFiniteRelationBuildV1 finite_list_lift_built =
        sort_step_provider_built ==
                CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1
        ? cetta_prime_typed_list_map_rel_finite_v1(
              &arena, &space, sort_step_provider,
              person_person_sort_list, list_family,
              list_nil_rule, list_cons_rule, map_rel_family,
              map_rel_nil_rule, map_rel_cons_rule, &finite_list_lift)
        : CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    CettaPrimeTypedListMapRelFiniteV1 empty_list_lift = {0};
    CettaPrimeTypedFiniteRelationBuildV1 empty_list_lift_built =
        sort_step_provider
        ? cetta_prime_typed_list_map_rel_finite_v1(
              &arena, &space, sort_step_provider,
              empty_sort_list, list_family,
              list_nil_rule, list_cons_rule, map_rel_family,
              map_rel_nil_rule, map_rel_cons_rule, &empty_list_lift)
        : CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    CettaPrimeTypedListMapRelFiniteV1 empty_fibre_list_lift = {0};
    CettaPrimeTypedFiniteRelationBuildV1 empty_fibre_list_lift_built =
        sort_step_provider
        ? cetta_prime_typed_list_map_rel_finite_v1(
              &arena, &space, sort_step_provider,
              number_sort_list, list_family,
              list_nil_rule, list_cons_rule, map_rel_family,
              map_rel_nil_rule, map_rel_cons_rule,
              &empty_fibre_list_lift)
        : CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    CettaPrimeTypedListMapRelFiniteV1 non_list_lift = {0};
    CettaPrimeTypedFiniteRelationBuildV1 non_list_lift_built =
        sort_step_provider
        ? cetta_prime_typed_list_map_rel_finite_v1(
              &arena, &space, sort_step_provider,
              person_sort, list_family,
              list_nil_rule, list_cons_rule, map_rel_family,
              map_rel_nil_rule, map_rel_cons_rule, &non_list_lift)
        : CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    CettaPrimeTypedValueV1 *ill_indexed_map_rel =
        cetta_prime_typed_list_map_rel_cons_v1(
            &arena, &space, map_rel_cons_rule,
            sort_code, sort_code, sort_relation,
            person_sort, number_sort,
            empty_sort_list, empty_sort_list,
            person_person_proof, empty_sort_map_rel);
    CettaPrimeTypedValueV1 *empty_map_rel_elimination =
        cetta_prime_typed_list_map_rel_eliminate_v1(
            &arena, &space, map_rel_eliminate_rule,
            sort_code, sort_code, sort_relation,
            map_rel_result_type, map_rel_result_nil,
            map_rel_result_cons, empty_sort_list, empty_sort_list,
            empty_sort_map_rel);
    CettaPrimeTypedValueV1 *map_rel_elimination_a =
        cetta_prime_typed_list_map_rel_eliminate_v1(
            &arena, &space, map_rel_eliminate_rule,
            sort_code, sort_code, sort_relation,
            map_rel_result_type, map_rel_result_nil,
            map_rel_result_cons, person_sort_list, number_sort_list,
            person_number_map_rel_a);
    CettaPrimeTypedValueV1 *map_rel_elimination_b =
        cetta_prime_typed_list_map_rel_eliminate_v1(
            &arena, &space, map_rel_eliminate_rule,
            sort_code, sort_code, sort_relation,
            map_rel_result_type, map_rel_result_nil,
            map_rel_result_cons, person_sort_list, number_sort_list,
            person_number_map_rel_b);
    CettaPrimeTypedValueV1 *map_rel_elimination_a_again =
        cetta_prime_typed_list_map_rel_eliminate_v1(
            &arena, &space, map_rel_eliminate_rule,
            sort_code, sort_code, sort_relation,
            map_rel_result_type, map_rel_result_nil,
            map_rel_result_cons, person_sort_list, number_sort_list,
            person_number_map_rel_a);
    CettaPrimeTypedValueV1 *lookalike_map_rel_elimination =
        cetta_prime_typed_list_map_rel_eliminate_v1(
            &arena, &space, map_rel_eliminate_lookalike,
            sort_code, sort_code, sort_relation,
            map_rel_result_type, map_rel_result_nil,
            map_rel_result_cons, person_sort_list, number_sort_list,
            person_number_map_rel_a);
    CettaPrimeTypedValueV1 *ill_typed_map_rel_elimination =
        cetta_prime_typed_list_map_rel_eliminate_v1(
            &arena, &space, map_rel_eliminate_rule,
            sort_code, sort_code, sort_relation,
            map_rel_result_type, person_sort,
            map_rel_result_cons, person_sort_list, number_sort_list,
            person_number_map_rel_a);
    CettaPrimeTypedValueV1 *mismatched_map_rel_elimination =
        cetta_prime_typed_list_map_rel_eliminate_v1(
            &arena, &space, map_rel_eliminate_rule,
            sort_code, sort_code, sort_relation,
            map_rel_result_type, map_rel_result_nil,
            map_rel_result_cons, person_number_sort_list,
            number_sort_list, person_number_map_rel_a);
    CettaPrimeTypedValueV1 *non_map_rel_elimination =
        cetta_prime_typed_list_map_rel_eliminate_v1(
            &arena, &space, map_rel_eliminate_rule,
            sort_code, sort_code, sort_relation,
            map_rel_result_type, map_rel_result_nil,
            map_rel_result_cons, person_sort_list, number_sort_list,
            grandparent_hyp_a);
    CHECK(ill_indexed_map_rel == NULL,
          "map-rel construction rejects head evidence at the wrong target index");
    CHECK(cetta_prime_typed_list_map_rel_nil_v1(
              &arena, &space, map_rel_nil_lookalike,
              sort_code, sort_code, sort_relation) == NULL,
          "a same-result function cannot masquerade as the authored map-rel constructor");
    if (!edge_relation_type || !grandparent_chain_type ||
        !grandparent_chain_a || !grandparent_chain_b ||
        !grandparent_relation || !grandparent_relation_at_alice ||
        !grandparent_relation_fibre || !grandparent_relation_evidence ||
        !edge_alice_answer_type || !edge_alice_bob_answer_a ||
        !edge_alice_bob_answer_b || !edge_alice_bob_answer_a_again ||
        !grandparent_answer_type ||
        !grandparent_answer || grandparent_answer_nil_exact ||
        !grandparent_answer_nil_converting || ill_typed_nil_converting ||
        !grandparent_answer_list_nil ||
        !grandparent_answer_list_one || wrong_target_answer_type ||
        wrong_source_answer || wrong_evidence_answer ||
        !small_relation_type || wrong_evidence_relation_chain ||
        wrong_middle_relation_chain || wrong_endpoint_relation_chain ||
        !mother_hyp_a ||
        !father_hyp || !grandparent_hyp_a || !list_nil_rule ||
        !list_cons_rule || !list_eliminate_rule ||
        !list_eliminate_lookalike || !list_result_type ||
        !list_result_nil || !list_result_cons || !native_map_type ||
        !native_map_program || !native_map_lookalike || !sort_next ||
        !tower_u0 || !graph_relation_type || !graph_relation ||
        !graph_relation_lookalike || !mapped_person_head ||
        !mapped_number_head || !graph_person_fibre ||
        !graph_number_fibre || !graph_lookalike_fibre ||
        !mapped_person_refl || !mapped_number_refl ||
        !empty_sort_list ||
        !number_sort_list || !person_number_sort_list ||
        !person_sort_list || !person_person_sort_list ||
        !number_number_sort_list || !all_nil_rule || !all_cons_rule ||
        !all_nil_lookalike || !sort_predicate ||
        !alternate_sort_predicate || !person_predicate_proof_a ||
        !person_predicate_proof_b || !empty_sort_all ||
        !empty_alternate_sort_all || !person_sort_all_a ||
        !person_sort_all_b || wrong_head_sort_all ||
        wrong_tail_sort_all || lookalike_empty_sort_all ||
        !fold_nil_rule || !fold_cons_rule || !fold_nil_lookalike ||
        !sort_fold_step || !sort_fold_step_proof ||
        !number_sort_fold_nil || !person_sort_fold_nil ||
        !person_sort_fold || wrong_step_sort_fold ||
        wrong_tail_sort_fold || lookalike_number_sort_fold_nil ||
        !iterate_zero_rule || !iterate_step_rule ||
        !iterate_zero_lookalike || !number_sort_iteration_zero ||
        !person_number_iteration || wrong_step_iteration ||
        wrong_recursive_iteration || lookalike_iteration_zero ||
        !empty_sort_list_map ||
        !person_number_sort_list_map ||
        !person_number_sort_list_map_again || lookalike_sort_list_map ||
        wrong_source_sort_list_map || wrong_function_sort_list_map ||
        !graph_person_evidence || !graph_number_evidence ||
        graph_lookalike_evidence || !graph_empty_map_rel ||
        !mapped_number_sort_list || !graph_number_map_rel ||
        !graph_target_list || !graph_person_number_map_rel ||
        !empty_list_elimination ||
        !person_number_list_elimination ||
        !person_number_list_elimination_again ||
        lookalike_list_elimination || ill_typed_list_elimination ||
        non_list_elimination || !map_rel_nil_rule || !map_rel_cons_rule ||
        !map_rel_eliminate_rule || !map_rel_eliminate_lookalike ||
        !map_rel_result_type || !map_rel_result_nil ||
        !map_rel_result_cons || !empty_sort_map_rel ||
        !boundary_empty_sort_map_rel || !person_number_map_rel_a ||
        !person_number_map_rel_b || !empty_map_rel_elimination ||
        !map_rel_elimination_a || !map_rel_elimination_b ||
        !map_rel_elimination_a_again || lookalike_map_rel_elimination ||
        ill_typed_map_rel_elimination || mismatched_map_rel_elimination ||
        non_map_rel_elimination) {
        print_typed_value_failure(
            &arena, &universe, "edge rel type", edge_relation_type);
        print_typed_value_failure(
            &arena, &universe, "grandparent chain type",
            grandparent_chain_type);
        print_typed_value_failure(
            &arena, &universe, "grandparent chain a",
            grandparent_chain_a);
        print_typed_value_failure(
            &arena, &universe, "grandparent chain b",
            grandparent_chain_b);
        print_typed_value_failure(
            &arena, &universe, "grandparent relation",
            grandparent_relation);
        print_typed_value_failure(
            &arena, &universe, "grandparent relation at alice",
            grandparent_relation_at_alice);
        print_typed_value_failure(
            &arena, &universe, "grandparent relation fibre",
            grandparent_relation_fibre);
        print_typed_value_failure(
            &arena, &universe, "grandparent relation evidence",
            grandparent_relation_evidence);
        print_typed_value_failure(
            &arena, &universe, "small relation type",
            small_relation_type);
        print_typed_value_failure(
            &arena, &universe, "mother hypothesis", mother_hyp_a);
        print_typed_value_failure(
            &arena, &universe, "father hypothesis", father_hyp);
        print_typed_value_failure(
            &arena, &universe, "grandparent hypothesis",
            grandparent_hyp_a);
        print_typed_value_failure(
            &arena, &universe, "list nil rule", list_nil_rule);
        print_typed_value_failure(
            &arena, &universe, "list cons rule", list_cons_rule);
        print_typed_value_failure(
            &arena, &universe, "list eliminator rule",
            list_eliminate_rule);
        print_typed_value_failure(
            &arena, &universe, "list eliminator lookalike",
            list_eliminate_lookalike);
        print_typed_value_failure(
            &arena, &universe, "list result motive", list_result_type);
        print_typed_value_failure(
            &arena, &universe, "list nil case", list_result_nil);
        print_typed_value_failure(
            &arena, &universe, "list cons case", list_result_cons);
        print_typed_value_failure(
            &arena, &universe, "native map type", native_map_type);
        print_typed_value_failure(
            &arena, &universe, "native map program", native_map_program);
        print_typed_value_failure(
            &arena, &universe, "native map lookalike",
            native_map_lookalike);
        print_typed_value_failure(
            &arena, &universe, "graph relation type",
            graph_relation_type);
        print_typed_value_failure(
            &arena, &universe, "graph relation", graph_relation);
        print_typed_value_failure(
            &arena, &universe, "graph person fibre",
            graph_person_fibre);
        print_typed_value_failure(
            &arena, &universe, "graph person evidence",
            graph_person_evidence);
        print_typed_value_failure(
            &arena, &universe, "graph target List", graph_target_list);
        print_typed_value_failure(
            &arena, &universe, "graph-agreement evidence",
            graph_person_number_map_rel);
        print_typed_value_failure(
            &arena, &universe, "native empty map", empty_sort_list_map);
        print_typed_value_failure(
            &arena, &universe, "native person-number map",
            person_number_sort_list_map);
        print_typed_value_failure(
            &arena, &universe, "empty sort list", empty_sort_list);
        print_typed_value_failure(
            &arena, &universe, "number-sort list", number_sort_list);
        print_typed_value_failure(
            &arena, &universe, "person-number sort list",
            person_number_sort_list);
        print_typed_value_failure(
            &arena, &universe, "person sort list", person_sort_list);
        print_typed_value_failure(
            &arena, &universe, "empty List elimination",
            empty_list_elimination);
        print_typed_value_failure(
            &arena, &universe, "person-number List elimination",
            person_number_list_elimination);
        print_typed_value_failure(
            &arena, &universe, "repeated person-number List elimination",
            person_number_list_elimination_again);
        print_typed_value_failure(
            &arena, &universe, "map-rel nil rule", map_rel_nil_rule);
        print_typed_value_failure(
            &arena, &universe, "map-rel cons rule", map_rel_cons_rule);
        print_typed_value_failure(
            &arena, &universe, "map-rel eliminator rule",
            map_rel_eliminate_rule);
        print_typed_value_failure(
            &arena, &universe, "map-rel eliminator lookalike",
            map_rel_eliminate_lookalike);
        print_typed_value_failure(
            &arena, &universe, "map-rel result motive",
            map_rel_result_type);
        print_typed_value_failure(
            &arena, &universe, "map-rel nil case",
            map_rel_result_nil);
        print_typed_value_failure(
            &arena, &universe, "map-rel cons case",
            map_rel_result_cons);
        print_typed_value_failure(
            &arena, &universe, "empty map-rel evidence",
            empty_sort_map_rel);
        print_typed_value_failure(
            &arena, &universe, "person-number map-rel evidence a",
            person_number_map_rel_a);
        print_typed_value_failure(
            &arena, &universe, "person-number map-rel evidence b",
            person_number_map_rel_b);
        print_typed_value_failure(
            &arena, &universe, "empty map-rel elimination",
            empty_map_rel_elimination);
        print_typed_value_failure(
            &arena, &universe, "map-rel elimination a",
            map_rel_elimination_a);
        print_typed_value_failure(
            &arena, &universe, "map-rel elimination b",
            map_rel_elimination_b);
        print_typed_value_failure(
            &arena, &universe, "repeated map-rel elimination",
            map_rel_elimination_a_again);
    }
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats construction_stats;
    cetta_runtime_stats_snapshot(&construction_stats);
    cetta_runtime_stats_disable();
    CHECK(construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ATTEMPT] ==
                  0u &&
              construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_CHECK] ==
                  0u &&
              construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ATTEMPT] ==
                  0u &&
              construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_CHECK] ==
                  0u &&
              construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ATTEMPT] ==
                  0u &&
              construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_CHECK] ==
                  0u,
          "intrinsic typed construction performs no post-hoc judgment demand");
#endif

    CHECK(mother_hyp_a && mother_hyp_duplicate && mother_hyp_b &&
              father_hyp && successor_hyp && alternate_father_hyp &&
              grandparent_hyp_a && grandparent_hyp_b && edge_relation_type &&
              grandparent_relation_result_type &&
              grandparent_chain_type && grandparent_chain_a &&
              grandparent_chain_b && grandparent_relation &&
              grandparent_relation_at_alice &&
              grandparent_relation_fibre &&
              grandparent_relation_evidence && authored_mother &&
              authored_mother_duplicate && authored_father &&
              authored_grandparent,
          "intrinsic hyp and ordinary rel/chain rules construct typed values");

    CettaPrimeTypedValueMetadataV1 mother_hyp_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 mother_duplicate_metadata = {0};
    CettaPrimeTypedValueMetadataV1 mother_hyp_b_metadata = {0};
    CettaPrimeTypedValueMetadataV1 father_hyp_metadata = {0};
    CettaPrimeTypedValueMetadataV1 grandparent_hyp_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 grandparent_hyp_b_metadata = {0};
    CettaPrimeTypedValueMetadataV1 edge_relation_type_metadata = {0};
    CettaPrimeTypedValueMetadataV1
        grandparent_relation_result_type_metadata = {0};
    CettaPrimeTypedValueMetadataV1 grandparent_chain_type_metadata = {0};
    CettaPrimeTypedValueMetadataV1 grandparent_chain_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 grandparent_chain_b_metadata = {0};
    CettaPrimeTypedValueMetadataV1 alice_bob_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 bob_carol_metadata = {0};
    CettaPrimeTypedValueMetadataV1 node_metadata = {0};
    CettaPrimeTypedValueMetadataV1 evidence_universe_metadata = {0};
    CettaPrimeTypedValueMetadataV1 edge_metadata = {0};
    CettaPrimeTypedValueMetadataV1 alice_metadata = {0};
    CettaPrimeTypedValueMetadataV1 bob_metadata = {0};
    CettaPrimeTypedValueMetadataV1 carol_metadata = {0};
    CettaPrimeTypedValueMetadataV1 sort_code_metadata = {0};
    CettaPrimeTypedValueMetadataV1 primitive_vocabulary_metadata = {0};
    CettaPrimeTypedValueMetadataV1 person_sort_metadata = {0};
    CHECK(mother_hyp_a && mother_hyp_duplicate && mother_hyp_b &&
              father_hyp && grandparent_hyp_a && grandparent_hyp_b &&
              cetta_prime_typed_value_v1_metadata(
                  mother_hyp_a, &mother_hyp_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  mother_hyp_duplicate, &mother_duplicate_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  mother_hyp_b, &mother_hyp_b_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  father_hyp, &father_hyp_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  grandparent_hyp_a, &grandparent_hyp_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  grandparent_hyp_b, &grandparent_hyp_b_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  edge_relation_type, &edge_relation_type_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  grandparent_relation_result_type,
                  &grandparent_relation_result_type_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  grandparent_chain_type,
                  &grandparent_chain_type_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  grandparent_chain_a,
                  &grandparent_chain_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  grandparent_chain_b,
                  &grandparent_chain_b_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  alice_bob_a, &alice_bob_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  bob_carol, &bob_carol_metadata) &&
              cetta_prime_typed_value_v1_metadata(node, &node_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  evidence_universe, &evidence_universe_metadata) &&
              cetta_prime_typed_value_v1_metadata(edge, &edge_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  alice, &alice_metadata) &&
              cetta_prime_typed_value_v1_metadata(bob, &bob_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  carol, &carol_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  sort_code, &sort_code_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  primitive_vocabulary,
                  &primitive_vocabulary_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort, &person_sort_metadata) &&
              mother_hyp_a_metadata.construction ==
                  CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1 &&
              grandparent_hyp_a_metadata.construction ==
                  CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1 &&
              mother_hyp_a_metadata.term_id ==
                  mother_duplicate_metadata.term_id &&
              mother_hyp_a_metadata.type_id ==
                  mother_duplicate_metadata.type_id &&
              mother_hyp_a_metadata.occurrence_identity !=
                  mother_duplicate_metadata.occurrence_identity &&
              grandparent_hyp_a_metadata.type_id ==
                  grandparent_hyp_b_metadata.type_id &&
              grandparent_hyp_a_metadata.occurrence_identity !=
                  grandparent_hyp_b_metadata.occurrence_identity &&
              edge_relation_type_metadata.term_id == edge_metadata.type_id &&
              grandparent_chain_a_metadata.type_id ==
                  grandparent_chain_type_metadata.term_id &&
              grandparent_chain_a_metadata.type_id ==
                  grandparent_chain_b_metadata.type_id &&
              grandparent_chain_a_metadata.occurrence_identity !=
                  grandparent_chain_b_metadata.occurrence_identity,
          "erased equality never collapses distinct proof-program occurrences");

    Atom *node_term = erase_term(&arena, &universe, node);
    Atom *edge_term = erase_term(&arena, &universe, edge);
    Atom *alice_term = erase_term(&arena, &universe, alice);
    Atom *bob_term = erase_term(&arena, &universe, bob);
    Atom *carol_term = erase_term(&arena, &universe, carol);
    Atom *alice_bob_a_term = erase_term(
        &arena, &universe, alice_bob_a);
    Atom *alice_bob_b_term = erase_term(
        &arena, &universe, alice_bob_b);
    Atom *bob_carol_term = erase_term(
        &arena, &universe, bob_carol);
    Atom *evidence_universe_term = erase_term(
        &arena, &universe, evidence_universe);
    Atom *edge_relation_type_term = erase_term(
        &arena, &universe, edge_relation_type);
    Atom *grandparent_relation_result_type_term = erase_term(
        &arena, &universe, grandparent_relation_result_type);
    Atom *grandparent_chain_type_term = erase_term(
        &arena, &universe, grandparent_chain_type);
    Atom *grandparent_chain_a_term = erase_term(
        &arena, &universe, grandparent_chain_a);
    Atom *grandparent_relation_term = erase_term(
        &arena, &universe, grandparent_relation);
    Atom *grandparent_relation_type = erase_type(
        &arena, &universe, grandparent_relation);
    Atom *grandparent_relation_fibre_term = erase_term(
        &arena, &universe, grandparent_relation_fibre);
    Atom *grandparent_relation_evidence_term = erase_term(
        &arena, &universe, grandparent_relation_evidence);
    Atom *grandparent_relation_evidence_type = erase_type(
        &arena, &universe, grandparent_relation_evidence);
    Atom *edge_alice_answer_type_term = erase_term(
        &arena, &universe, edge_alice_answer_type);
    Atom *edge_alice_bob_answer_a_term = erase_term(
        &arena, &universe, edge_alice_bob_answer_a);
    Atom *edge_alice_bob_answer_a_type = erase_type(
        &arena, &universe, edge_alice_bob_answer_a);
    Atom *edge_alice_bob_answer_b_term = erase_term(
        &arena, &universe, edge_alice_bob_answer_b);
    Atom *edge_alice_bob_answer_a_again_term = erase_term(
        &arena, &universe, edge_alice_bob_answer_a_again);
    Atom *grandparent_answer_type_term = erase_term(
        &arena, &universe, grandparent_answer_type);
    Atom *grandparent_answer_term = erase_term(
        &arena, &universe, grandparent_answer);
    Atom *grandparent_answer_carried_type = erase_type(
        &arena, &universe, grandparent_answer);
    Atom *sort_code_term = erase_term(&arena, &universe, sort_code);
    Atom *primitive_vocabulary_term = erase_term(
        &arena, &universe, primitive_vocabulary);
    Atom *person_sort_term = erase_term(&arena, &universe, person_sort);
    Atom *number_sort_term = erase_term(&arena, &universe, number_sort);
    Atom *sort_next_term = erase_term(&arena, &universe, sort_next);
    Atom *mother_symbol_term = erase_term(
        &arena, &universe, mother_symbol);
    Atom *mother_hyp_a_term = erase_term(
        &arena, &universe, mother_hyp_a);
    Atom *grandparent_hyp_a_term = erase_term(
        &arena, &universe, grandparent_hyp_a);
    Atom *grandparent_hyp_a_type = erase_type(
        &arena, &universe, grandparent_hyp_a);
    Atom *authored_mother_term = erase_term(
        &arena, &universe, authored_mother);
    Atom *authored_mother_duplicate_term = erase_term(
        &arena, &universe, authored_mother_duplicate);
    Atom *authored_father_term = erase_term(
        &arena, &universe, authored_father);
    Atom *authored_grandparent_term = erase_term(
        &arena, &universe, authored_grandparent);
    Atom *authored_grandparent_type = erase_type(
        &arena, &universe, authored_grandparent);
    Atom *authored_primitive_rule_term = erase_term(
        &arena, &universe, authored_primitive_rule);
    Atom *authored_chain_rule_term = erase_term(
        &arena, &universe, authored_chain_rule);
    Atom *type_arguments[] = {
        sort_code_term, primitive_vocabulary_term,
        person_sort_term, person_sort_term,
    };
    Atom *expected_type = intrinsic_application(
        &arena, "hyp", type_arguments,
        sizeof(type_arguments) / sizeof(type_arguments[0]));
    Atom *expected_authored_primitive_arguments[] = {
        person_sort_term, person_sort_term, mother_symbol_term,
    };
    Atom *expected_authored_primitive = intrinsic_apply_term(
        &arena, authored_primitive_rule_term,
        expected_authored_primitive_arguments,
        sizeof(expected_authored_primitive_arguments) /
            sizeof(expected_authored_primitive_arguments[0]));
    Atom *expected_authored_chain_arguments[] = {
        person_sort_term, person_sort_term, person_sort_term,
        authored_mother_term, authored_father_term,
    };
    Atom *expected_authored_chain = intrinsic_apply_term(
        &arena, authored_chain_rule_term,
        expected_authored_chain_arguments,
        sizeof(expected_authored_chain_arguments) /
            sizeof(expected_authored_chain_arguments[0]));
    CHECK(mother_hyp_a_term && grandparent_hyp_a_term &&
              grandparent_hyp_a_type &&
              atom_eq(
                  mother_hyp_a_term, expected_authored_primitive) &&
              atom_eq(
                  grandparent_hyp_a_term, expected_authored_chain) &&
              atom_eq(grandparent_hyp_a_type, expected_type),
          "hyp adapters apply authored lowercase Prime declarations and expose their exact indices");
    bool authored_programs_match =
        authored_mother_term && authored_mother_duplicate_term &&
        authored_grandparent_term && authored_grandparent_type &&
        atom_eq(authored_mother_term, expected_authored_primitive) &&
        atom_eq(
            authored_mother_duplicate_term,
            expected_authored_primitive) &&
        atom_eq(authored_grandparent_term, expected_authored_chain) &&
        atom_eq(authored_grandparent_type, expected_type);
    CHECK(authored_programs_match,
          "authored polymorphic hyp declarations construct the same intrinsic Prime programs by Pi elimination");

    CettaPrimeTypedValueMetadataV1 authored_mother_metadata = {0};
    CettaPrimeTypedValueMetadataV1 authored_duplicate_metadata = {0};
    CettaPrimeTypedDerivationViewV1 authored_grandparent_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_metadata(
              authored_mother, &authored_mother_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  authored_mother_duplicate,
                  &authored_duplicate_metadata) &&
              authored_mother_metadata.term_id ==
                  authored_duplicate_metadata.term_id &&
              authored_mother_metadata.type_id ==
                  authored_duplicate_metadata.type_id &&
              authored_mother_metadata.occurrence_identity !=
                  authored_duplicate_metadata.occurrence_identity &&
              cetta_prime_typed_value_v1_derivation(
                  authored_grandparent,
                  &authored_grandparent_derivation),
          "authored application preserves distinct proof-program occurrences");
    const CettaPrimeTypedDerivationNodeV1 *authored_grandparent_root =
        find_derivation_node(
            &authored_grandparent_derivation,
            authored_grandparent_derivation.root_occurrence_identity);
    CHECK(authored_grandparent_root &&
              authored_grandparent_root->premise_count == 2u &&
              term_universe_atom_id_eq(
                  &universe, authored_grandparent_root->rule_id,
                  atom_symbol(&arena, "app")),
          "authored hyp construction is ordinary dependent application, not a special checker opcode");

    Atom *empty_sort_list_term = erase_term(
        &arena, &universe, empty_sort_list);
    Atom *number_sort_list_term = erase_term(
        &arena, &universe, number_sort_list);
    Atom *person_sort_list_term = erase_term(
        &arena, &universe, person_sort_list);
    Atom *person_number_sort_list_term = erase_term(
        &arena, &universe, person_number_sort_list);
    Atom *person_number_sort_list_type = erase_type(
        &arena, &universe, person_number_sort_list);
    Atom *nil_arguments[] = {sort_code_term};
    Atom *expected_nil = intrinsic_application(
        &arena, "list:nil", nil_arguments,
        sizeof(nil_arguments) / sizeof(nil_arguments[0]));
    Atom *number_cons_arguments[] = {
        sort_code_term, number_sort_term, expected_nil,
    };
    Atom *expected_number_list = intrinsic_application(
        &arena, "list:cons", number_cons_arguments,
        sizeof(number_cons_arguments) / sizeof(number_cons_arguments[0]));
    Atom *person_cons_arguments[] = {
        sort_code_term, person_sort_term, expected_number_list,
    };
    Atom *expected_person_number_list = intrinsic_application(
        &arena, "list:cons", person_cons_arguments,
        sizeof(person_cons_arguments) / sizeof(person_cons_arguments[0]));
    Atom *list_type_arguments[] = {sort_code_term};
    Atom *expected_sort_list_type = intrinsic_application(
        &arena, "list", list_type_arguments,
        sizeof(list_type_arguments) / sizeof(list_type_arguments[0]));
    CHECK(empty_sort_list_term && number_sort_list_term &&
              person_number_sort_list_term &&
              person_number_sort_list_type &&
              atom_eq(empty_sort_list_term, expected_nil) &&
              atom_eq(number_sort_list_term, expected_number_list) &&
              atom_eq(
                  person_number_sort_list_term,
                  expected_person_number_list) &&
              atom_eq(
                  person_number_sort_list_type, expected_sort_list_type),
          "native List constructors erase to the exact lowercase intrinsic spine");

    CettaPrimeTypedIndexedViewV1 list_indexed = {0};
    Atom *list_runtime = NULL;
    Atom *expected_list_runtime = parse_one(
        &arena, "(person-sort number-sort)");
    CHECK(cetta_prime_typed_value_v1_indexed_view(
              person_number_sort_list, &list_indexed) &&
              list_indexed.parameter_count == 1u &&
              list_indexed.index_count == 0u &&
              list_indexed.parameter_ids[0] == sort_code_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, list_indexed.family_head_id,
                  atom_symbol(&arena, "list")) &&
              cetta_prime_typed_list_runtime_representation_v1(
                  &arena, &space, person_number_sort_list, &list_runtime) &&
              atom_eq(list_runtime, expected_list_runtime),
          "zero-index List uses the generic family view and the flat expression realization");

    Atom *empty_runtime = NULL;
    CHECK(cetta_prime_typed_list_runtime_representation_v1(
              &arena, &space, empty_sort_list, &empty_runtime) &&
              atom_eq(empty_runtime, atom_unit(&arena)),
          "native List nil realizes as the empty expression");

    Atom *not_a_list_runtime = NULL;
    CHECK(cetta_prime_typed_list_cons_v1(
              &arena, &space, list_cons_rule, sort_code,
              alice, empty_sort_list) == NULL &&
              cetta_prime_typed_list_cons_v1(
                  &arena, &space, list_cons_rule, sort_code,
                  person_sort, grandparent_hyp_a) == NULL &&
              !cetta_prime_typed_list_runtime_representation_v1(
                  &arena, &space, alice, &not_a_list_runtime),
          "List construction rejects a wrong head, wrong family tail, and non-List realization input");

    Atom *sort_predicate_term = erase_term(
        &arena, &universe, sort_predicate);
    Atom *person_predicate_proof_a_term = erase_term(
        &arena, &universe, person_predicate_proof_a);
    Atom *empty_sort_all_term = erase_term(
        &arena, &universe, empty_sort_all);
    Atom *person_sort_all_a_term = erase_term(
        &arena, &universe, person_sort_all_a);
    Atom *person_sort_all_a_type = erase_type(
        &arena, &universe, person_sort_all_a);
    Atom *all_nil_arguments[] = {
        sort_code_term, sort_predicate_term,
    };
    Atom *expected_empty_sort_all = intrinsic_application(
        &arena, "rel:all:nil", all_nil_arguments,
        sizeof(all_nil_arguments) / sizeof(all_nil_arguments[0]));
    Atom *all_cons_arguments[] = {
        sort_code_term, sort_predicate_term,
        person_sort_term, empty_sort_list_term,
        person_predicate_proof_a_term, expected_empty_sort_all,
    };
    Atom *expected_person_sort_all = intrinsic_application(
        &arena, "rel:all:cons", all_cons_arguments,
        sizeof(all_cons_arguments) / sizeof(all_cons_arguments[0]));
    Atom *all_type_arguments[] = {
        sort_code_term, sort_predicate_term, person_sort_list_term,
    };
    Atom *expected_person_sort_all_type = intrinsic_application(
        &arena, "rel:all", all_type_arguments,
        sizeof(all_type_arguments) / sizeof(all_type_arguments[0]));
    CettaPrimeTypedValueMetadataV1 all_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 all_b_metadata = {0};
    CettaPrimeTypedValueMetadataV1 all_head_metadata = {0};
    CettaPrimeTypedValueMetadataV1 all_tail_metadata = {0};
    CettaPrimeTypedValueMetadataV1 all_list_metadata = {0};
    CettaPrimeTypedValueMetadataV1 sort_predicate_metadata = {0};
    CettaPrimeTypedIndexedViewV1 all_indexed = {0};
    CettaPrimeTypedDerivationViewV1 all_derivation = {0};
    CHECK(person_sort_all_a_term && person_sort_all_a_type &&
              expected_person_sort_all && expected_person_sort_all_type &&
              atom_eq(empty_sort_all_term, expected_empty_sort_all) &&
              atom_eq(person_sort_all_a_term, expected_person_sort_all) &&
              atom_eq(
                  person_sort_all_a_type, expected_person_sort_all_type) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort_all_a, &all_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort_all_b, &all_b_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_predicate_proof_a, &all_head_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  empty_sort_all, &all_tail_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort_list, &all_list_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  sort_predicate, &sort_predicate_metadata) &&
              all_a_metadata.type_id == all_b_metadata.type_id &&
              all_a_metadata.term_id != all_b_metadata.term_id &&
              all_a_metadata.occurrence_identity !=
                  all_b_metadata.occurrence_identity &&
              cetta_prime_typed_value_v1_indexed_view(
                  person_sort_all_a, &all_indexed) &&
              all_indexed.parameter_count == 2u &&
              all_indexed.index_count == 1u &&
              all_indexed.parameter_ids[0] == sort_code_metadata.term_id &&
              all_indexed.parameter_ids[1] ==
                  sort_predicate_metadata.term_id &&
              all_indexed.index_ids[0] == all_list_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, all_indexed.family_head_id,
                  atom_symbol(&arena, "rel:all")) &&
              cetta_prime_typed_value_v1_derivation(
                  person_sort_all_a, &all_derivation) &&
              find_derivation_node(
                  &all_derivation,
                  all_head_metadata.occurrence_identity) &&
              find_derivation_node(
                  &all_derivation,
                  all_tail_metadata.occurrence_identity),
          "rel:all constructs the exact indexed proof and retains head and recursive evidence occurrences");
    CHECK(wrong_head_sort_all == NULL && wrong_tail_sort_all == NULL &&
              lookalike_empty_sort_all == NULL,
          "rel:all construction rejects a wrong head fibre, wrong recursive fibre, and same-result lookalike");

    Atom *sort_fold_step_term = erase_term(
        &arena, &universe, sort_fold_step);
    Atom *sort_fold_step_proof_term = erase_term(
        &arena, &universe, sort_fold_step_proof);
    Atom *number_sort_fold_nil_term = erase_term(
        &arena, &universe, number_sort_fold_nil);
    Atom *person_sort_fold_term = erase_term(
        &arena, &universe, person_sort_fold);
    Atom *person_sort_fold_type = erase_type(
        &arena, &universe, person_sort_fold);
    Atom *fold_nil_arguments[] = {
        sort_code_term, sort_code_term,
        sort_fold_step_term, number_sort_term,
    };
    Atom *expected_number_sort_fold_nil = intrinsic_application(
        &arena, "rel:fold:nil", fold_nil_arguments,
        sizeof(fold_nil_arguments) / sizeof(fold_nil_arguments[0]));
    Atom *fold_cons_arguments[] = {
        sort_code_term, sort_code_term, sort_fold_step_term,
        person_sort_term, person_sort_term, empty_sort_list_term,
        number_sort_term, number_sort_term,
        sort_fold_step_proof_term, expected_number_sort_fold_nil,
    };
    Atom *expected_person_sort_fold = intrinsic_application(
        &arena, "rel:fold:cons", fold_cons_arguments,
        sizeof(fold_cons_arguments) / sizeof(fold_cons_arguments[0]));
    Atom *fold_type_arguments[] = {
        sort_code_term, sort_code_term, sort_fold_step_term,
        person_sort_term, person_sort_list_term, number_sort_term,
    };
    Atom *expected_person_sort_fold_type = intrinsic_application(
        &arena, "rel:fold", fold_type_arguments,
        sizeof(fold_type_arguments) / sizeof(fold_type_arguments[0]));
    CettaPrimeTypedValueMetadataV1 fold_step_metadata = {0};
    CettaPrimeTypedValueMetadataV1 fold_tail_metadata = {0};
    CettaPrimeTypedValueMetadataV1 fold_list_metadata = {0};
    CettaPrimeTypedValueMetadataV1 fold_before_metadata = {0};
    CettaPrimeTypedValueMetadataV1 fold_after_metadata = {0};
    CettaPrimeTypedValueMetadataV1 fold_step_relation_metadata = {0};
    CettaPrimeTypedIndexedViewV1 fold_indexed = {0};
    CettaPrimeTypedDerivationViewV1 fold_derivation = {0};
    CHECK(number_sort_fold_nil_term && person_sort_fold_term &&
              person_sort_fold_type && expected_number_sort_fold_nil &&
              expected_person_sort_fold && expected_person_sort_fold_type &&
              atom_eq(
                  number_sort_fold_nil_term,
                  expected_number_sort_fold_nil) &&
              atom_eq(person_sort_fold_term, expected_person_sort_fold) &&
              atom_eq(
                  person_sort_fold_type, expected_person_sort_fold_type) &&
              cetta_prime_typed_value_v1_metadata(
                  sort_fold_step_proof, &fold_step_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  number_sort_fold_nil, &fold_tail_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort_list, &fold_list_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort, &fold_before_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  number_sort, &fold_after_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  sort_fold_step, &fold_step_relation_metadata) &&
              cetta_prime_typed_value_v1_indexed_view(
                  person_sort_fold, &fold_indexed) &&
              fold_indexed.parameter_count == 3u &&
              fold_indexed.index_count == 3u &&
              fold_indexed.parameter_ids[0] == sort_code_metadata.term_id &&
              fold_indexed.parameter_ids[1] == sort_code_metadata.term_id &&
              fold_indexed.parameter_ids[2] ==
                  fold_step_relation_metadata.term_id &&
              fold_indexed.index_ids[0] == fold_before_metadata.term_id &&
              fold_indexed.index_ids[1] == fold_list_metadata.term_id &&
              fold_indexed.index_ids[2] == fold_after_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, fold_indexed.family_head_id,
                  atom_symbol(&arena, "rel:fold")) &&
              cetta_prime_typed_value_v1_derivation(
                  person_sort_fold, &fold_derivation) &&
              find_derivation_node(
                  &fold_derivation,
                  fold_step_metadata.occurrence_identity) &&
              find_derivation_node(
                  &fold_derivation,
                  fold_tail_metadata.occurrence_identity),
          "rel:fold constructs the exact indexed path and retains its intermediate and both proof premises");
    CHECK(wrong_step_sort_fold == NULL && wrong_tail_sort_fold == NULL &&
              lookalike_number_sort_fold_nil == NULL,
          "rel:fold construction rejects a wrong step fibre, wrong accumulator continuation, and same-result lookalike");

    Atom *iteration_relation_term = erase_term(
        &arena, &universe, sort_relation);
    Atom *iteration_predecessor_proof_term = erase_term(
        &arena, &universe, person_number_proof_a);
    Atom *iteration_step_proof_term = erase_term(
        &arena, &universe, person_number_proof_b);
    Atom *number_sort_iteration_zero_term = erase_term(
        &arena, &universe, number_sort_iteration_zero);
    Atom *person_number_iteration_term = erase_term(
        &arena, &universe, person_number_iteration);
    Atom *person_number_iteration_type = erase_type(
        &arena, &universe, person_number_iteration);
    Atom *iteration_zero_arguments[] = {
        sort_code_term, sort_code_term,
        iteration_relation_term, iteration_relation_term,
        number_sort_term, number_sort_term,
    };
    Atom *expected_number_sort_iteration_zero = intrinsic_application(
        &arena, "rel:iterate:zero", iteration_zero_arguments,
        sizeof(iteration_zero_arguments) /
            sizeof(iteration_zero_arguments[0]));
    Atom *iteration_step_arguments[] = {
        sort_code_term, sort_code_term,
        iteration_relation_term, iteration_relation_term,
        number_sort_term, person_sort_term, number_sort_term,
        person_sort_term, number_sort_term, number_sort_term,
        iteration_predecessor_proof_term, iteration_step_proof_term,
        expected_number_sort_iteration_zero,
    };
    Atom *expected_person_number_iteration = intrinsic_application(
        &arena, "rel:iterate:step", iteration_step_arguments,
        sizeof(iteration_step_arguments) /
            sizeof(iteration_step_arguments[0]));
    Atom *iteration_type_arguments[] = {
        sort_code_term, sort_code_term,
        iteration_relation_term, iteration_relation_term,
        number_sort_term, person_sort_term, person_sort_term,
        number_sort_term,
    };
    Atom *expected_person_number_iteration_type = intrinsic_application(
        &arena, "rel:iterate", iteration_type_arguments,
        sizeof(iteration_type_arguments) /
            sizeof(iteration_type_arguments[0]));
    CettaPrimeTypedValueMetadataV1 iteration_predecessor_metadata = {0};
    CettaPrimeTypedValueMetadataV1 iteration_step_metadata = {0};
    CettaPrimeTypedValueMetadataV1 iteration_recursive_metadata = {0};
    CettaPrimeTypedValueMetadataV1 iteration_relation_metadata = {0};
    CettaPrimeTypedValueMetadataV1 iteration_zero_metadata = {0};
    CettaPrimeTypedValueMetadataV1 iteration_source_metadata = {0};
    CettaPrimeTypedValueMetadataV1 iteration_count_metadata = {0};
    CettaPrimeTypedValueMetadataV1 iteration_target_metadata = {0};
    CettaPrimeTypedIndexedViewV1 iteration_indexed = {0};
    CettaPrimeTypedDerivationViewV1 iteration_derivation = {0};
    CHECK(number_sort_iteration_zero_term &&
              person_number_iteration_term &&
              person_number_iteration_type &&
              expected_number_sort_iteration_zero &&
              expected_person_number_iteration &&
              expected_person_number_iteration_type &&
              atom_eq(
                  number_sort_iteration_zero_term,
                  expected_number_sort_iteration_zero) &&
              atom_eq(
                  person_number_iteration_term,
                  expected_person_number_iteration) &&
              atom_eq(
                  person_number_iteration_type,
                  expected_person_number_iteration_type) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_proof_a,
                  &iteration_predecessor_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_proof_b, &iteration_step_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  number_sort_iteration_zero,
                  &iteration_recursive_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  sort_relation, &iteration_relation_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  number_sort, &iteration_zero_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort, &iteration_source_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort, &iteration_count_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  number_sort, &iteration_target_metadata) &&
              cetta_prime_typed_value_v1_indexed_view(
                  person_number_iteration, &iteration_indexed) &&
              iteration_indexed.parameter_count == 5u &&
              iteration_indexed.index_count == 3u &&
              iteration_indexed.parameter_ids[0] ==
                  sort_code_metadata.term_id &&
              iteration_indexed.parameter_ids[1] ==
                  sort_code_metadata.term_id &&
              iteration_indexed.parameter_ids[2] ==
                  iteration_relation_metadata.term_id &&
              iteration_indexed.parameter_ids[3] ==
                  iteration_relation_metadata.term_id &&
              iteration_indexed.parameter_ids[4] ==
                  iteration_zero_metadata.term_id &&
              iteration_indexed.index_ids[0] ==
                  iteration_source_metadata.term_id &&
              iteration_indexed.index_ids[1] ==
                  iteration_count_metadata.term_id &&
              iteration_indexed.index_ids[2] ==
                  iteration_target_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, iteration_indexed.family_head_id,
                  atom_symbol(&arena, "rel:iterate")) &&
              cetta_prime_typed_value_v1_derivation(
                  person_number_iteration, &iteration_derivation) &&
              find_derivation_node(
                  &iteration_derivation,
                  iteration_predecessor_metadata.occurrence_identity) &&
              find_derivation_node(
                  &iteration_derivation,
                  iteration_step_metadata.occurrence_identity) &&
              find_derivation_node(
                  &iteration_derivation,
                  iteration_recursive_metadata.occurrence_identity),
          "rel:iterate constructs the exact indexed path and retains predecessor, step, and recursive evidence");
    CHECK(wrong_step_iteration == NULL &&
              wrong_recursive_iteration == NULL &&
              lookalike_iteration_zero == NULL,
          "rel:iterate construction rejects a wrong step fibre, wrong recursive fibre, and same-result lookalike");

    Atom *map_indices[6] = {0};
    for (size_t index = 0u; index < 6u; index++) {
        map_indices[index] = atom_expr2(
            &arena, atom_symbol(&arena, "idx"),
            atom_int(&arena, (int64_t)index));
    }
    Atom *map_motive_list_arguments[] = {map_indices[3]};
    Atom *map_motive = atom_expr2(
        &arena, atom_symbol(&arena, "Lam"),
        intrinsic_application(
            &arena, "list", map_motive_list_arguments,
            sizeof(map_motive_list_arguments) /
                sizeof(map_motive_list_arguments[0])));
    Atom *map_nil_arguments[] = {map_indices[2]};
    Atom *map_nil = intrinsic_application(
        &arena, "list:nil", map_nil_arguments,
        sizeof(map_nil_arguments) / sizeof(map_nil_arguments[0]));
    Atom *map_head_application = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        map_indices[4], map_indices[2]);
    Atom *map_cons_arguments[] = {
        map_indices[5], map_head_application, map_indices[0],
    };
    Atom *map_cons_case = intrinsic_application(
        &arena, "list:cons", map_cons_arguments,
        sizeof(map_cons_arguments) / sizeof(map_cons_arguments[0]));
    for (size_t binder = 0u; binder < 3u; binder++)
        map_cons_case = atom_expr2(
            &arena, atom_symbol(&arena, "Lam"), map_cons_case);
    Atom *map_eliminate_arguments[] = {
        map_indices[3], map_motive, map_nil, map_cons_case, map_indices[0],
    };
    Atom *expected_native_map_program = intrinsic_application(
        &arena, "list:eliminate", map_eliminate_arguments,
        sizeof(map_eliminate_arguments) /
            sizeof(map_eliminate_arguments[0]));
    for (size_t binder = 0u; binder < 4u; binder++)
        expected_native_map_program = atom_expr2(
            &arena, atom_symbol(&arena, "Lam"),
            expected_native_map_program);

    Atom *native_map_program_term = erase_term(
        &arena, &universe, native_map_program);
    Atom *native_map_lookalike_term = erase_term(
        &arena, &universe, native_map_lookalike);
    Atom *mapped_empty_term = erase_term(
        &arena, &universe, empty_sort_list_map);
    Atom *mapped_person_number_term = erase_term(
        &arena, &universe, person_number_sort_list_map);
    Atom *mapped_person_number_type = erase_type(
        &arena, &universe, person_number_sort_list_map);
    Atom *mapped_person = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        sort_next_term, person_sort_term);
    Atom *mapped_number = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        sort_next_term, number_sort_term);
    Atom *mapped_number_cons_arguments[] = {
        sort_code_term, mapped_number, expected_nil,
    };
    Atom *expected_mapped_number_list = intrinsic_application(
        &arena, "list:cons", mapped_number_cons_arguments,
        sizeof(mapped_number_cons_arguments) /
            sizeof(mapped_number_cons_arguments[0]));
    Atom *mapped_person_cons_arguments[] = {
        sort_code_term, mapped_person, expected_mapped_number_list,
    };
    Atom *expected_mapped_person_number_list = intrinsic_application(
        &arena, "list:cons", mapped_person_cons_arguments,
        sizeof(mapped_person_cons_arguments) /
            sizeof(mapped_person_cons_arguments[0]));
    CHECK(native_map_program_term && native_map_lookalike_term &&
              expected_native_map_program && mapped_empty_term &&
              mapped_person_number_term && mapped_person_number_type &&
              atom_eq(
                  native_map_program_term, expected_native_map_program) &&
              !atom_eq(
                  native_map_lookalike_term, expected_native_map_program) &&
              atom_eq(mapped_empty_term, expected_nil) &&
              atom_eq(
                  mapped_person_number_term,
                  expected_mapped_person_number_list) &&
              atom_eq(
                  mapped_person_number_type, expected_sort_list_type),
          "the check-once boundary retains the actual Prime map term and its fused native realization computes exactly");

    Atom *mapped_runtime = NULL;
    Atom *mapped_runtime_items[] = {mapped_person, mapped_number};
    Atom *expected_mapped_runtime = atom_expr(
        &arena, mapped_runtime_items,
        (CettaExprLen)(sizeof(mapped_runtime_items) /
                       sizeof(mapped_runtime_items[0])));
    CettaPrimeTypedIndexedViewV1 mapped_indexed = {0};
    CettaPrimeTypedValueMetadataV1 mapped_metadata = {0};
    CettaPrimeTypedValueMetadataV1 mapped_again_metadata = {0};
    CettaPrimeTypedDerivationViewV1 mapped_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_indexed_view(
              person_number_sort_list_map, &mapped_indexed) &&
              mapped_indexed.parameter_count == 1u &&
              mapped_indexed.index_count == 0u &&
              mapped_indexed.parameter_ids[0] ==
                  sort_code_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, mapped_indexed.family_head_id,
                  atom_symbol(&arena, "list")) &&
              cetta_prime_typed_list_runtime_representation_v1(
                  &arena, &space, person_number_sort_list_map,
                  &mapped_runtime) &&
              atom_eq(mapped_runtime, expected_mapped_runtime) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_sort_list_map, &mapped_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_sort_list_map_again,
                  &mapped_again_metadata) &&
              mapped_metadata.term_id == mapped_again_metadata.term_id &&
              mapped_metadata.type_id == mapped_again_metadata.type_id &&
              mapped_metadata.occurrence_identity !=
                  mapped_again_metadata.occurrence_identity &&
              cetta_prime_typed_value_v1_derivation(
                  person_number_sort_list_map, &mapped_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &mapped_derivation,
                  "list:map-fold") == 1u &&
              derivation_rule_count(
                  &arena, &universe, &mapped_derivation,
                  "list:map-fusion") == 1u,
          "native map preserves the List index, flat representation, typed receipt, and occurrence identity");

    Atom *live_map_application = parse_one(
        &arena,
        "((lam source "
        "  (lam target "
        "    (lam function "
        "      (lam xs "
        "        (list:eliminate source "
        "          (lam ignored (list target)) "
        "          (list:nil target) "
        "          (lam head "
        "            (lam tail "
        "              (lam induction "
        "                (list:cons target "
        "                  (function head) induction)))) "
        "          xs))))) "
        " sort-code sort-code sort-next "
        " (list:cons sort-code person-sort "
        "   (list:cons sort-code number-sort (list:nil sort-code))))");
    Atom *live_map_expected = parse_one(
        &arena,
        "((sort-next person-sort) (sort-next number-sort))");
    CettaPrimeNativeExecutionV1 live_map =
        cetta_prime_native_calculus_try_v1(
            &arena, &space, live_map_application);
    CettaPrimeTypedDerivationViewV1 live_map_derivation = {0};
    CHECK(live_map_application && live_map_expected &&
              live_map.kind == CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              live_map.value && atom_eq(live_map.value, live_map_expected) &&
              live_map.typed_value &&
              cetta_prime_typed_value_v1_is_current(
                  live_map.typed_value, &space) &&
              cetta_prime_typed_value_v1_derivation(
                  live_map.typed_value, &live_map_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &live_map_derivation,
                  "list:map-fold") == 1u &&
              derivation_rule_count(
                  &arena, &universe, &live_map_derivation,
                  "list:map-fusion") == 1u,
          "ordinary lowercase Prime map crosses one raw boundary and receives a current proof-carrying native realization");

    Atom *live_map_lookalike_application = parse_one(
        &arena,
        "((lam source "
        "  (lam target "
        "    (lam function "
        "      (lam xs (list:nil target))))) "
        " sort-code sort-code sort-next "
        " (list:cons sort-code person-sort "
        "   (list:cons sort-code number-sort (list:nil sort-code))))");
    Atom *live_map_wrong_argument_application = parse_one(
        &arena,
        "((lam source "
        "  (lam target "
        "    (lam function "
        "      (lam xs "
        "        (list:eliminate source "
        "          (lam ignored (list target)) "
        "          (list:nil target) "
        "          (lam head "
        "            (lam tail "
        "              (lam induction "
        "                (list:cons target "
        "                  (function head) induction)))) "
        "          xs))))) "
        " sort-code sort-code sort-next person-sort)");
    CettaPrimeNativeExecutionV1 live_map_lookalike =
        cetta_prime_native_calculus_try_v1(
            &arena, &space, live_map_lookalike_application);
    CettaPrimeNativeExecutionV1 live_map_wrong_argument =
        cetta_prime_native_calculus_try_v1(
            &arena, &space, live_map_wrong_argument_application);
    CHECK(live_map_lookalike_application &&
              live_map_wrong_argument_application &&
              live_map_lookalike.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !live_map_lookalike.value &&
              !live_map_lookalike.typed_value &&
              live_map_wrong_argument.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !live_map_wrong_argument.value &&
              !live_map_wrong_argument.typed_value,
          "same-typed lookalikes and non-List inputs remain ordinary execution rather than acquiring native authority");

    CettaPrimeTypedValueMetadataV1 source_list_metadata = {0};
    CettaPrimeTypedValueMetadataV1 graph_relation_metadata = {0};
    CettaPrimeTypedValueMetadataV1 graph_target_metadata = {0};
    CettaPrimeTypedValueMetadataV1 mapped_person_refl_metadata = {0};
    CettaPrimeTypedValueMetadataV1 graph_person_fibre_metadata = {0};
    CettaPrimeTypedValueMetadataV1 graph_person_metadata = {0};
    CettaPrimeTypedValueMetadataV1 graph_number_metadata = {0};
    CettaPrimeTypedValueMetadataV1 graph_nil_metadata = {0};
    CettaPrimeTypedIndexedViewV1 graph_agreement_indexed = {0};
    CettaPrimeTypedDerivationViewV1 graph_person_derivation = {0};
    CettaPrimeTypedDerivationViewV1 graph_agreement_derivation = {0};
    Atom *graph_relation_term = erase_term(
        &arena, &universe, graph_relation);
    Atom *graph_target_term = erase_term(
        &arena, &universe, graph_target_list);
    Atom *graph_agreement_term = erase_term(
        &arena, &universe, graph_person_number_map_rel);
    Atom *graph_agreement_type = erase_type(
        &arena, &universe, graph_person_number_map_rel);
    Atom *graph_person_fibre_term = erase_term(
        &arena, &universe, graph_person_fibre);
    Atom *graph_person_refl_type = erase_type(
        &arena, &universe, mapped_person_refl);
    Atom *graph_person_evidence_type = erase_type(
        &arena, &universe, graph_person_evidence);
    Atom *graph_number_fibre_term = erase_term(
        &arena, &universe, graph_number_fibre);
    Atom *graph_number_evidence_type = erase_type(
        &arena, &universe, graph_number_evidence);
    Atom *expected_graph_agreement_type_arguments[] = {
        sort_code_term, sort_code_term, graph_relation_term,
        person_number_sort_list_term, graph_target_term,
    };
    Atom *expected_graph_agreement_type = intrinsic_application(
        &arena, "map-rel", expected_graph_agreement_type_arguments,
        sizeof(expected_graph_agreement_type_arguments) /
            sizeof(expected_graph_agreement_type_arguments[0]));
    CHECK(graph_relation_term && graph_target_term &&
              graph_agreement_term && graph_agreement_type &&
              graph_person_fibre_term && graph_person_refl_type &&
              graph_person_evidence_type && graph_number_fibre_term &&
              graph_number_evidence_type &&
              expected_graph_agreement_type &&
              atom_eq(graph_target_term, mapped_person_number_term) &&
              atom_eq(
                  graph_person_evidence_type,
                  graph_person_fibre_term) &&
              atom_eq(
                  graph_number_evidence_type,
                  graph_number_fibre_term) &&
              !atom_eq(
                  graph_person_refl_type,
                  graph_person_fibre_term) &&
              atom_eq(
                  graph_agreement_type,
                  expected_graph_agreement_type) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_sort_list, &source_list_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  graph_relation, &graph_relation_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  graph_target_list, &graph_target_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  mapped_person_refl,
                  &mapped_person_refl_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  graph_person_fibre,
                  &graph_person_fibre_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  graph_person_evidence, &graph_person_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  graph_number_evidence, &graph_number_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  graph_empty_map_rel, &graph_nil_metadata) &&
              cetta_prime_typed_value_v1_indexed_view(
                  graph_person_number_map_rel,
                  &graph_agreement_indexed) &&
              graph_agreement_indexed.parameter_count == 3u &&
              graph_agreement_indexed.index_count == 2u &&
              graph_agreement_indexed.parameter_ids[0] ==
                  sort_code_metadata.term_id &&
              graph_agreement_indexed.parameter_ids[1] ==
                  sort_code_metadata.term_id &&
              graph_agreement_indexed.parameter_ids[2] ==
                  graph_relation_metadata.term_id &&
              graph_agreement_indexed.index_ids[0] ==
                  source_list_metadata.term_id &&
              graph_agreement_indexed.index_ids[1] ==
                  graph_target_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, graph_agreement_indexed.family_head_id,
                  atom_symbol(&arena, "map-rel")) &&
              cetta_prime_typed_value_v1_derivation(
                  graph_person_evidence,
                  &graph_person_derivation) &&
              cetta_prime_typed_value_v1_derivation(
                  graph_person_number_map_rel,
                  &graph_agreement_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &graph_agreement_derivation,
                  "conv:beta") == 2u &&
              find_derivation_node(
                  &graph_agreement_derivation,
                  graph_person_metadata.occurrence_identity) &&
              find_derivation_node(
                  &graph_agreement_derivation,
                  graph_number_metadata.occurrence_identity) &&
              find_derivation_node(
                  &graph_agreement_derivation,
                  graph_nil_metadata.occurrence_identity),
          "ordinary graph evidence and map-rel prove agreement with the eliminator-defined native map without a graph opcode");
    const CettaPrimeTypedDerivationNodeV1 *graph_beta_root =
        find_derivation_node(
            &graph_person_derivation,
            graph_person_derivation.root_occurrence_identity);
    CHECK(graph_beta_root && graph_beta_root->premise_count == 2u &&
              graph_beta_root->witness_count == 4u &&
              graph_beta_root->premise_offset <=
                  graph_person_derivation.premise_occurrence_count &&
              graph_beta_root->premise_count <=
                  graph_person_derivation.premise_occurrence_count -
                      graph_beta_root->premise_offset &&
              graph_beta_root->witness_offset <=
                  graph_person_derivation.witness_count &&
              graph_beta_root->witness_count <=
                  graph_person_derivation.witness_count -
                      graph_beta_root->witness_offset &&
              graph_person_derivation.premise_occurrences[
                  graph_beta_root->premise_offset] ==
                  mapped_person_refl_metadata.occurrence_identity &&
              graph_person_derivation.premise_occurrences[
                  graph_beta_root->premise_offset + 1u] ==
                  graph_person_fibre_metadata.occurrence_identity &&
              graph_person_derivation.witness_ids[
                  graph_beta_root->witness_offset] ==
                  mapped_person_refl_metadata.type_id &&
              graph_person_derivation.witness_ids[
                  graph_beta_root->witness_offset + 1u] ==
                  graph_person_fibre_metadata.term_id &&
              graph_person_derivation.witness_ids[
                  graph_beta_root->witness_offset + 2u] ==
                  graph_person_derivation.witness_ids[
                      graph_beta_root->witness_offset + 3u] &&
              graph_person_derivation.witness_ids[
                  graph_beta_root->witness_offset] !=
                  graph_person_derivation.witness_ids[
                      graph_beta_root->witness_offset + 1u] &&
              term_universe_atom_id_eq(
                  &universe, graph_beta_root->rule_id,
                  atom_symbol(&arena, "conv:beta")),
          "beta transport retains both typed premises, both unreduced fibres, and their common normal form");
    CHECK(graph_lookalike_evidence == NULL,
          "beta transport rejects a same-typed relation whose fibre is not the function graph");
    CHECK(lookalike_sort_list_map == NULL &&
              wrong_source_sort_list_map == NULL &&
              wrong_function_sort_list_map == NULL,
          "native map rejects a same-type non-map program, a mismatched List index, and a wrong function type");

    Atom *list_result_nil_term = erase_term(
        &arena, &universe, list_result_nil);
    Atom *list_result_cons_term = erase_term(
        &arena, &universe, list_result_cons);
    Atom *empty_elimination_term = erase_term(
        &arena, &universe, empty_list_elimination);
    Atom *empty_elimination_type = erase_type(
        &arena, &universe, empty_list_elimination);
    Atom *person_number_elimination_term = erase_term(
        &arena, &universe, person_number_list_elimination);
    Atom *person_number_elimination_type = erase_type(
        &arena, &universe, person_number_list_elimination);
    Atom *inner_fold_arguments[] = {
        number_sort_term, empty_sort_list_term, list_result_nil_term,
    };
    Atom *expected_inner_fold = intrinsic_apply_term(
        &arena, list_result_cons_term, inner_fold_arguments,
        sizeof(inner_fold_arguments) / sizeof(inner_fold_arguments[0]));
    Atom *outer_fold_arguments[] = {
        person_sort_term, number_sort_list_term, expected_inner_fold,
    };
    Atom *expected_person_number_fold = intrinsic_apply_term(
        &arena, list_result_cons_term, outer_fold_arguments,
        sizeof(outer_fold_arguments) / sizeof(outer_fold_arguments[0]));
    Atom *empty_result_type_arguments[] = {empty_sort_list_term};
    Atom *expected_empty_result_type = intrinsic_application(
        &arena, "list-result-type", empty_result_type_arguments,
        sizeof(empty_result_type_arguments) /
            sizeof(empty_result_type_arguments[0]));
    Atom *person_number_result_type_arguments[] = {
        person_number_sort_list_term,
    };
    Atom *expected_person_number_result_type = intrinsic_application(
        &arena, "list-result-type", person_number_result_type_arguments,
        sizeof(person_number_result_type_arguments) /
            sizeof(person_number_result_type_arguments[0]));
    CHECK(list_result_nil_term && list_result_cons_term &&
              empty_elimination_term && empty_elimination_type &&
              person_number_elimination_term &&
              person_number_elimination_type && expected_inner_fold &&
              expected_person_number_fold && expected_empty_result_type &&
              expected_person_number_result_type &&
              atom_eq(empty_elimination_term, list_result_nil_term) &&
              atom_eq(empty_elimination_type, expected_empty_result_type) &&
              atom_eq(
                  person_number_elimination_term,
                  expected_person_number_fold) &&
              atom_eq(
                  person_number_elimination_type,
                  expected_person_number_result_type),
          "native List elimination performs the authored dependent iota fold exactly");

    CettaPrimeTypedValueMetadataV1 list_elimination_metadata = {0};
    CettaPrimeTypedValueMetadataV1 repeated_elimination_metadata = {0};
    CettaPrimeTypedDerivationViewV1 list_elimination_derivation = {0};
    AtomId number_sort_id = term_universe_store_atom_id(
        &universe, &arena, number_sort_term);
    CHECK(cetta_prime_typed_value_v1_metadata(
              person_number_list_elimination,
              &list_elimination_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_list_elimination_again,
                  &repeated_elimination_metadata) &&
              list_elimination_metadata.term_id ==
                  repeated_elimination_metadata.term_id &&
              list_elimination_metadata.type_id ==
                  repeated_elimination_metadata.type_id &&
              list_elimination_metadata.occurrence_identity !=
                  repeated_elimination_metadata.occurrence_identity &&
              cetta_prime_typed_value_v1_derivation(
                  person_number_list_elimination,
                  &list_elimination_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &list_elimination_derivation,
                  "list:fold") == 1u &&
              derivation_rule_count(
                  &arena, &universe, &list_elimination_derivation,
                  "list:iota-fold") == 1u &&
              derivation_has_witness(
                  &list_elimination_derivation,
                  person_sort_metadata.term_id) &&
              derivation_has_witness(
                  &list_elimination_derivation,
                  number_sort_id),
          "fused List elimination retains typed computation evidence and distinct proof occurrences");
    CHECK(lookalike_list_elimination == NULL &&
              ill_typed_list_elimination == NULL &&
              non_list_elimination == NULL,
          "native List elimination rejects a same-type lookalike, a wrong case, and a non-List input");

    CettaPrimeTypedValueMetadataV1 number_sort_metadata = {0};
    CettaPrimeTypedValueMetadataV1 sort_relation_metadata = {0};
    CettaPrimeTypedValueMetadataV1 person_number_proof_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 person_number_proof_b_metadata = {0};
    CettaPrimeTypedValueMetadataV1 empty_sort_list_metadata = {0};
    CettaPrimeTypedValueMetadataV1 number_sort_list_metadata = {0};
    CettaPrimeTypedValueMetadataV1 person_sort_list_metadata = {0};
    CettaPrimeTypedValueMetadataV1 empty_map_rel_metadata = {0};
    CettaPrimeTypedValueMetadataV1 map_rel_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 map_rel_b_metadata = {0};
    CHECK(cetta_prime_typed_value_v1_metadata(
              number_sort, &number_sort_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  sort_relation, &sort_relation_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_proof_a,
                  &person_number_proof_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_proof_b,
                  &person_number_proof_b_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  empty_sort_list, &empty_sort_list_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  number_sort_list, &number_sort_list_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_sort_list, &person_sort_list_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  empty_sort_map_rel, &empty_map_rel_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_map_rel_a, &map_rel_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  person_number_map_rel_b, &map_rel_b_metadata),
          "List relator values expose their native construction metadata");

    Atom *sort_relation_term = erase_term(
        &arena, &universe, sort_relation);
    Atom *person_number_proof_a_term = erase_term(
        &arena, &universe, person_number_proof_a);
    Atom *person_number_proof_b_term = erase_term(
        &arena, &universe, person_number_proof_b);
    Atom *empty_map_rel_term = erase_term(
        &arena, &universe, empty_sort_map_rel);
    Atom *map_rel_a_term = erase_term(
        &arena, &universe, person_number_map_rel_a);
    Atom *map_rel_a_type = erase_type(
        &arena, &universe, person_number_map_rel_a);
    Atom *map_rel_b_term = erase_term(
        &arena, &universe, person_number_map_rel_b);
    Atom *map_rel_nil_arguments[] = {
        sort_code_term, sort_code_term, sort_relation_term,
    };
    Atom *expected_map_rel_nil = intrinsic_application(
        &arena, "map-rel:nil", map_rel_nil_arguments,
        sizeof(map_rel_nil_arguments) / sizeof(map_rel_nil_arguments[0]));
    Atom *map_rel_cons_a_arguments[] = {
        sort_code_term, sort_code_term, sort_relation_term,
        person_sort_term, number_sort_term,
        empty_sort_list_term, empty_sort_list_term,
        person_number_proof_a_term, expected_map_rel_nil,
    };
    Atom *expected_map_rel_a = intrinsic_application(
        &arena, "map-rel:cons", map_rel_cons_a_arguments,
        sizeof(map_rel_cons_a_arguments) /
            sizeof(map_rel_cons_a_arguments[0]));
    Atom *map_rel_cons_b_arguments[] = {
        sort_code_term, sort_code_term, sort_relation_term,
        person_sort_term, number_sort_term,
        empty_sort_list_term, empty_sort_list_term,
        person_number_proof_b_term, expected_map_rel_nil,
    };
    Atom *expected_map_rel_b = intrinsic_application(
        &arena, "map-rel:cons", map_rel_cons_b_arguments,
        sizeof(map_rel_cons_b_arguments) /
            sizeof(map_rel_cons_b_arguments[0]));
    Atom *map_rel_type_arguments[] = {
        sort_code_term, sort_code_term, sort_relation_term,
        person_sort_list_term, number_sort_list_term,
    };
    Atom *expected_map_rel_type = intrinsic_application(
        &arena, "map-rel", map_rel_type_arguments,
        sizeof(map_rel_type_arguments) / sizeof(map_rel_type_arguments[0]));
    CHECK(empty_map_rel_term && map_rel_a_term && map_rel_a_type &&
              map_rel_b_term && expected_map_rel_nil &&
              expected_map_rel_a && expected_map_rel_b &&
              expected_map_rel_type &&
              atom_eq(empty_map_rel_term, expected_map_rel_nil) &&
              atom_eq(map_rel_a_term, expected_map_rel_a) &&
              atom_eq(map_rel_b_term, expected_map_rel_b) &&
              atom_eq(map_rel_a_type, expected_map_rel_type) &&
              map_rel_a_metadata.type_id == map_rel_b_metadata.type_id &&
              map_rel_a_metadata.term_id != map_rel_b_metadata.term_id &&
              map_rel_a_metadata.occurrence_identity !=
                  map_rel_b_metadata.occurrence_identity,
          "map-rel retains endpoint indices and distinct proof evidence without collapsing equal endpoints");

    bool finite_lift_targets_exact =
        finite_list_lift_built ==
            CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 &&
        finite_list_lift.source_length == 2u &&
        finite_list_lift.search.answer_count == 4u &&
        finite_list_lift.target_lists && finite_list_lift.evidences &&
        finite_list_lift.base_occurrence_indices &&
        finite_list_lift.search.receipt;
    CettaPrimeTypedValueMetadataV1 finite_lift_evidence_metadata[4] = {0};
    Atom *number_number_sort_list_term = erase_term(
        &arena, &universe, number_number_sort_list);
    for (size_t index = 0u; finite_lift_targets_exact && index < 4u;
         index++) {
        Atom *target_list_term = erase_term(
            &arena, &universe, finite_list_lift.target_lists[index]);
        finite_lift_targets_exact =
            target_list_term && number_number_sort_list_term &&
            atom_eq(target_list_term, number_number_sort_list_term) &&
            cetta_prime_typed_value_v1_metadata(
                finite_list_lift.evidences[index],
                &finite_lift_evidence_metadata[index]);
    }
    bool finite_lift_occurrences_distinct = finite_lift_targets_exact;
    for (size_t left = 0u;
         finite_lift_occurrences_distinct && left < 4u; left++) {
        for (size_t right = left + 1u; right < 4u; right++) {
            if (finite_lift_evidence_metadata[left].occurrence_identity ==
                finite_lift_evidence_metadata[right].occurrence_identity) {
                finite_lift_occurrences_distinct = false;
            }
        }
    }
    CHECK(finite_lift_targets_exact && finite_lift_occurrences_distinct,
          "finite List lifting retains four distinct proof occurrences over one equal endpoint List");
    Atom *finite_lift_mixed_evidence =
        finite_list_lift.evidences
        ? erase_term(&arena, &universe, finite_list_lift.evidences[1])
        : NULL;
    CHECK(finite_list_lift.base_occurrence_indices &&
              finite_list_lift.base_occurrence_indices[0] == 0u &&
              finite_list_lift.base_occurrence_indices[1] == 0u &&
              finite_list_lift.base_occurrence_indices[2] == 0u &&
              finite_list_lift.base_occurrence_indices[3] == 1u &&
              finite_list_lift.base_occurrence_indices[4] == 1u &&
              finite_list_lift.base_occurrence_indices[5] == 0u &&
              finite_list_lift.base_occurrence_indices[6] == 1u &&
              finite_list_lift.base_occurrence_indices[7] == 1u &&
              finite_lift_mixed_evidence &&
              atom_symbol_occurrences(
                  finite_lift_mixed_evidence,
                  "person-number-proof-a") == 1u &&
              atom_symbol_occurrences(
                  finite_lift_mixed_evidence,
                  "person-number-proof-b") == 1u,
          "finite List lifting preserves lexicographic base origins and the complete elementwise proof term");
    CHECK(empty_list_lift_built ==
              CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 &&
              empty_list_lift.source_length == 0u &&
              empty_list_lift.search.answer_count == 1u &&
              empty_list_lift.target_lists &&
              atom_eq(
                  erase_term(
                      &arena, &universe,
                      empty_list_lift.target_lists[0]),
                  empty_sort_list_term) &&
              empty_list_lift.evidences &&
              atom_eq(
                  erase_term(
                      &arena, &universe,
                      empty_list_lift.evidences[0]),
                  empty_map_rel_term),
          "finite List lifting maps the empty List through the ordinary map-rel nil constructor");
    CHECK(empty_fibre_list_lift_built ==
              CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 &&
              empty_fibre_list_lift.source_length == 1u &&
              empty_fibre_list_lift.search.answer_count == 0u &&
              empty_fibre_list_lift.search.receipt,
          "a covered empty element fibre produces an exact empty lifted fibre rather than an abstention or refutation");
    CHECK(non_list_lift_built ==
              CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1 &&
              !non_list_lift.search.receipt,
          "finite List lifting declines a value outside the native List representation without changing its meaning");

    Atom *map_rel_result_nil_term = erase_term(
        &arena, &universe, map_rel_result_nil);
    Atom *map_rel_result_cons_term = erase_term(
        &arena, &universe, map_rel_result_cons);
    Atom *empty_map_rel_elimination_term = erase_term(
        &arena, &universe, empty_map_rel_elimination);
    Atom *map_rel_elimination_a_term = erase_term(
        &arena, &universe, map_rel_elimination_a);
    Atom *map_rel_elimination_a_type = erase_type(
        &arena, &universe, map_rel_elimination_a);
    Atom *map_rel_elimination_b_term = erase_term(
        &arena, &universe, map_rel_elimination_b);
    Atom *map_rel_result_a_arguments[] = {
        person_sort_term, number_sort_term,
        empty_sort_list_term, empty_sort_list_term,
        person_number_proof_a_term, empty_map_rel_term,
        map_rel_result_nil_term,
    };
    Atom *expected_map_rel_result_a = intrinsic_apply_term(
        &arena, map_rel_result_cons_term,
        map_rel_result_a_arguments,
        sizeof(map_rel_result_a_arguments) /
            sizeof(map_rel_result_a_arguments[0]));
    Atom *map_rel_result_b_arguments[] = {
        person_sort_term, number_sort_term,
        empty_sort_list_term, empty_sort_list_term,
        person_number_proof_b_term, empty_map_rel_term,
        map_rel_result_nil_term,
    };
    Atom *expected_map_rel_result_b = intrinsic_apply_term(
        &arena, map_rel_result_cons_term,
        map_rel_result_b_arguments,
        sizeof(map_rel_result_b_arguments) /
            sizeof(map_rel_result_b_arguments[0]));
    Atom *map_rel_result_type_a_arguments[] = {
        person_sort_list_term, number_sort_list_term, map_rel_a_term,
    };
    Atom *expected_map_rel_result_type_a = intrinsic_application(
        &arena, "map-rel-result-type",
        map_rel_result_type_a_arguments,
        sizeof(map_rel_result_type_a_arguments) /
            sizeof(map_rel_result_type_a_arguments[0]));
    CHECK(map_rel_result_nil_term && map_rel_result_cons_term &&
              empty_map_rel_elimination_term &&
              map_rel_elimination_a_term && map_rel_elimination_a_type &&
              map_rel_elimination_b_term && expected_map_rel_result_a &&
              expected_map_rel_result_b &&
              expected_map_rel_result_type_a &&
              atom_eq(
                  empty_map_rel_elimination_term,
                  map_rel_result_nil_term) &&
              atom_eq(
                  map_rel_elimination_a_term,
                  expected_map_rel_result_a) &&
              atom_eq(
                  map_rel_elimination_b_term,
                  expected_map_rel_result_b) &&
              atom_eq(
                  map_rel_elimination_a_type,
                  expected_map_rel_result_type_a) &&
              !atom_eq(
                  map_rel_elimination_a_term,
                  map_rel_elimination_b_term),
          "native map-rel elimination preserves head evidence while fusing its dependent iota fold");

    CettaPrimeTypedValueMetadataV1 map_rel_elimination_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 map_rel_elimination_b_metadata = {0};
    CettaPrimeTypedValueMetadataV1 map_rel_elimination_repeat_metadata = {0};
    CettaPrimeTypedDerivationViewV1 map_rel_elimination_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_metadata(
              map_rel_elimination_a,
              &map_rel_elimination_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  map_rel_elimination_b,
                  &map_rel_elimination_b_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  map_rel_elimination_a_again,
                  &map_rel_elimination_repeat_metadata) &&
              map_rel_elimination_a_metadata.term_id ==
                  map_rel_elimination_repeat_metadata.term_id &&
              map_rel_elimination_a_metadata.type_id ==
                  map_rel_elimination_repeat_metadata.type_id &&
              map_rel_elimination_a_metadata.occurrence_identity !=
                  map_rel_elimination_repeat_metadata.occurrence_identity &&
              map_rel_elimination_a_metadata.term_id !=
                  map_rel_elimination_b_metadata.term_id &&
              cetta_prime_typed_value_v1_derivation(
                  map_rel_elimination_a,
                  &map_rel_elimination_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &map_rel_elimination_derivation,
                  "map-rel:fold") == 1u &&
              derivation_rule_count(
                  &arena, &universe, &map_rel_elimination_derivation,
                  "map-rel:iota-fold") == 1u &&
              derivation_has_witness(
                  &map_rel_elimination_derivation,
                  person_number_proof_a_metadata.term_id) &&
              !derivation_has_witness(
                  &map_rel_elimination_derivation,
                  person_number_proof_b_metadata.term_id),
          "map-rel native computation retains exact proof occurrence data without quotienting equal endpoints");
    CHECK(lookalike_map_rel_elimination == NULL &&
              ill_typed_map_rel_elimination == NULL &&
              mismatched_map_rel_elimination == NULL &&
              non_map_rel_elimination == NULL,
          "native map-rel elimination rejects a same-type lookalike, wrong case, mismatched indices, and wrong family");

    CettaPrimeTypedIndexedViewV1 map_rel_indexed = {0};
    CettaPrimeTypedDerivationViewV1 map_rel_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_indexed_view(
              person_number_map_rel_a, &map_rel_indexed) &&
              map_rel_indexed.parameter_count == 3u &&
              map_rel_indexed.index_count == 2u &&
              map_rel_indexed.parameter_ids[0] ==
                  sort_code_metadata.term_id &&
              map_rel_indexed.parameter_ids[1] ==
                  sort_code_metadata.term_id &&
              map_rel_indexed.parameter_ids[2] ==
                  sort_relation_metadata.term_id &&
              map_rel_indexed.index_ids[0] ==
                  person_sort_list_metadata.term_id &&
              map_rel_indexed.index_ids[1] ==
                  number_sort_list_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, map_rel_indexed.family_head_id,
                  atom_symbol(&arena, "map-rel")) &&
              cetta_prime_typed_value_v1_derivation(
                  person_number_map_rel_a, &map_rel_derivation) &&
              find_derivation_node(
                  &map_rel_derivation,
                  person_number_proof_a_metadata.occurrence_identity) &&
              find_derivation_node(
                  &map_rel_derivation,
                  empty_map_rel_metadata.occurrence_identity),
          "map-rel exposes one generic indexed-family view and retains both recursive proof premises");

    Atom *expected_relation_type = atom_expr3(
        &arena, atom_symbol(&arena, "Pi"), node_term,
        atom_expr3(
            &arena, atom_symbol(&arena, "Pi"), node_term,
            evidence_universe_term));
    Atom *index_zero = atom_expr2(
        &arena, atom_symbol(&arena, "idx"), atom_int(&arena, 0));
    Atom *index_one = atom_expr2(
        &arena, atom_symbol(&arena, "idx"), atom_int(&arena, 1));
    Atom *index_two = atom_expr2(
        &arena, atom_symbol(&arena, "idx"), atom_int(&arena, 2));
    Atom *edge_at_alice_under_target = atom_expr3(
        &arena, atom_symbol(&arena, "App"), edge_term, alice_term);
    Atom *edge_answer_evidence_type = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        edge_at_alice_under_target, index_zero);
    Atom *expected_edge_answer_type = atom_expr3(
        &arena, atom_symbol(&arena, "Sigma"),
        node_term, edge_answer_evidence_type);
    Atom *expected_edge_answer_a = atom_expr3(
        &arena, atom_symbol(&arena, "Pair"),
        bob_term, alice_bob_a_term);
    Atom *expected_edge_answer_b = atom_expr3(
        &arena, atom_symbol(&arena, "Pair"),
        bob_term, alice_bob_b_term);
    Atom *grandparent_at_alice_under_target = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        grandparent_relation_term, alice_term);
    Atom *grandparent_answer_evidence_type = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        grandparent_at_alice_under_target, index_zero);
    Atom *expected_grandparent_answer_type = atom_expr3(
        &arena, atom_symbol(&arena, "Sigma"),
        node_term, grandparent_answer_evidence_type);
    Atom *expected_grandparent_answer = atom_expr3(
        &arena, atom_symbol(&arena, "Pair"),
        carol_term, grandparent_relation_evidence_term);
    CHECK(edge_alice_answer_type_term &&
              edge_alice_bob_answer_a_term &&
              edge_alice_bob_answer_a_type &&
              edge_alice_bob_answer_b_term &&
              edge_alice_bob_answer_a_again_term &&
              grandparent_answer_type_term && grandparent_answer_term &&
              grandparent_answer_carried_type &&
              expected_edge_answer_type && expected_edge_answer_a &&
              expected_edge_answer_b &&
              expected_grandparent_answer_type &&
              expected_grandparent_answer &&
              atom_eq(
                  edge_alice_answer_type_term,
                  expected_edge_answer_type) &&
              atom_eq(
                  edge_alice_bob_answer_a_type,
                  expected_edge_answer_type) &&
              atom_eq(
                  edge_alice_bob_answer_a_term,
                  expected_edge_answer_a) &&
              atom_eq(
                  edge_alice_bob_answer_b_term,
                  expected_edge_answer_b) &&
              atom_eq(
                  edge_alice_bob_answer_a_again_term,
                  expected_edge_answer_a) &&
              atom_eq(
                  grandparent_answer_type_term,
                  expected_grandparent_answer_type) &&
              atom_eq(
                  grandparent_answer_carried_type,
                  expected_grandparent_answer_type) &&
              atom_eq(
                  grandparent_answer_term,
                  expected_grandparent_answer),
          "proof-relevant relation answers are dependent target/evidence occurrences");

    CettaPrimeTypedDerivationViewV1 converting_nil_derivation = {0};
    CHECK(grandparent_answer_nil_exact == NULL &&
              grandparent_answer_nil_converting &&
              ill_typed_nil_converting == NULL &&
              cetta_prime_typed_value_v1_derivation(
                  grandparent_answer_nil_converting,
                  &converting_nil_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &converting_nil_derivation,
                  "conv:judgmental") == 1u,
          "explicit judgmental conversion bridges legacy U1 to sort zero while unequal application remains unconstructible");

    CettaPrimeTypedValueMetadataV1 edge_answer_a_metadata = {0};
    CettaPrimeTypedValueMetadataV1 edge_answer_b_metadata = {0};
    CettaPrimeTypedValueMetadataV1 edge_answer_repeat_metadata = {0};
    CettaPrimeTypedDerivationViewV1 edge_answer_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_metadata(
              edge_alice_bob_answer_a, &edge_answer_a_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  edge_alice_bob_answer_b, &edge_answer_b_metadata) &&
              cetta_prime_typed_value_v1_metadata(
                  edge_alice_bob_answer_a_again,
                  &edge_answer_repeat_metadata) &&
              edge_answer_a_metadata.type_id ==
                  edge_answer_b_metadata.type_id &&
              edge_answer_a_metadata.term_id !=
                  edge_answer_b_metadata.term_id &&
              edge_answer_a_metadata.term_id ==
                  edge_answer_repeat_metadata.term_id &&
              edge_answer_a_metadata.occurrence_identity !=
                  edge_answer_b_metadata.occurrence_identity &&
              edge_answer_a_metadata.occurrence_identity !=
                  edge_answer_repeat_metadata.occurrence_identity &&
              cetta_prime_typed_value_v1_derivation(
                  edge_alice_bob_answer_a, &edge_answer_derivation) &&
              derivation_has_witness(
                  &edge_answer_derivation, bob_metadata.term_id) &&
              find_derivation_node(
                  &edge_answer_derivation,
                  alice_bob_a_metadata.occurrence_identity),
          "answer occurrences preserve equal endpoints, distinct derivations, and repeated occurrence identity");

    Atom *relation_earlier_at_source = atom_expr3(
        &arena, atom_symbol(&arena, "App"), edge_term, index_two);
    Atom *relation_earlier_evidence = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        relation_earlier_at_source, index_zero);
    Atom *relation_later_at_middle = atom_expr3(
        &arena, atom_symbol(&arena, "App"), edge_term, index_one);
    Atom *relation_later_evidence = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        relation_later_at_middle, index_two);
    Atom *expected_chain_relation_fibre = atom_expr3(
        &arena, atom_symbol(&arena, "Sigma"), node_term,
        atom_expr3(
            &arena, atom_symbol(&arena, "Sigma"),
            relation_earlier_evidence, relation_later_evidence));
    Atom *expected_chain_relation = atom_expr2(
        &arena, atom_symbol(&arena, "Lam"),
        atom_expr2(
            &arena, atom_symbol(&arena, "Lam"),
            expected_chain_relation_fibre));
    Atom *earlier_at_source = atom_expr3(
        &arena, atom_symbol(&arena, "App"), edge_term, alice_term);
    Atom *earlier_evidence_type = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        earlier_at_source, index_zero);
    Atom *later_at_middle = atom_expr3(
        &arena, atom_symbol(&arena, "App"), edge_term, index_one);
    Atom *later_evidence_type = atom_expr3(
        &arena, atom_symbol(&arena, "App"),
        later_at_middle, carol_term);
    Atom *expected_chain_type = atom_expr3(
        &arena, atom_symbol(&arena, "Sigma"), node_term,
        atom_expr3(
            &arena, atom_symbol(&arena, "Sigma"),
            earlier_evidence_type, later_evidence_type));
    Atom *expected_chain_term = atom_expr3(
        &arena, atom_symbol(&arena, "Pair"), bob_term,
        atom_expr3(
            &arena, atom_symbol(&arena, "Pair"),
            alice_bob_a_term, bob_carol_term));
    CHECK(edge_relation_type_term && grandparent_relation_result_type_term &&
              grandparent_chain_type_term &&
              grandparent_chain_a_term && grandparent_relation_term &&
              grandparent_relation_type &&
              grandparent_relation_fibre_term &&
              grandparent_relation_evidence_term &&
              grandparent_relation_evidence_type &&
              atom_eq(edge_relation_type_term, expected_relation_type) &&
              atom_eq(
                  grandparent_relation_result_type_term,
                  expected_relation_type) &&
              atom_eq(grandparent_relation_term,
                  expected_chain_relation) &&
              atom_eq(grandparent_relation_type,
                  expected_relation_type) &&
              atom_eq(grandparent_chain_type_term, expected_chain_type) &&
              atom_eq(grandparent_chain_a_term, expected_chain_term) &&
              atom_eq(grandparent_relation_evidence_term,
                  expected_chain_term) &&
              atom_eq(grandparent_relation_evidence_type,
                  grandparent_relation_fibre_term),
          "native rel and chain erase to ordinary Pi, lambda, Sigma, and Pair terms");

    CettaPrimeTypedDerivationViewV1 relation_evidence_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_derivation(
              grandparent_relation_evidence,
              &relation_evidence_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &relation_evidence_derivation,
                  "conv:beta") == 1u &&
              find_derivation_node(
                  &relation_evidence_derivation,
                  grandparent_chain_a_metadata.occurrence_identity),
          "applying native relational chain exposes exactly the witness-retaining Sigma fibre by intrinsic beta conversion");

    CettaPrimeTypedDerivationViewV1 relation_result_type_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_derivation(
              grandparent_relation_result_type,
              &relation_result_type_derivation),
          "relational composition derives its result relation type");
    const CettaPrimeTypedDerivationNodeV1 *relation_result_type_root =
        find_derivation_node(
            &relation_result_type_derivation,
            relation_result_type_derivation.root_occurrence_identity);
    CHECK(relation_result_type_root &&
              relation_result_type_root->premise_count == 5u &&
              term_universe_atom_id_eq(
                  &universe, relation_result_type_root->rule_id,
                  atom_symbol(&arena, "rel:chain-type")),
          "relation result formation derives the cumulative evidence level from its carriers and both premise relations");

    CettaPrimeTypedDerivationViewV1 relation_chain_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_derivation(
              grandparent_relation, &relation_chain_derivation),
          "native relational composition exposes its derivation");
    const CettaPrimeTypedDerivationNodeV1 *relation_chain_root =
        find_derivation_node(
            &relation_chain_derivation,
            relation_chain_derivation.root_occurrence_identity);
    CHECK(relation_chain_root && relation_chain_root->premise_count == 4u &&
              relation_chain_root->premise_offset <=
                  relation_chain_derivation.premise_occurrence_count &&
              relation_chain_root->premise_count <=
                  relation_chain_derivation.premise_occurrence_count -
                      relation_chain_root->premise_offset &&
              relation_chain_derivation.premise_occurrences[
                  relation_chain_root->premise_offset] ==
                  grandparent_relation_result_type_metadata.
                      occurrence_identity &&
              relation_chain_derivation.premise_occurrences[
                  relation_chain_root->premise_offset + 1u] ==
                  node_metadata.occurrence_identity &&
              relation_chain_derivation.premise_occurrences[
                  relation_chain_root->premise_offset + 2u] ==
                  edge_metadata.occurrence_identity &&
              relation_chain_derivation.premise_occurrences[
                  relation_chain_root->premise_offset + 3u] ==
                  edge_metadata.occurrence_identity &&
              term_universe_atom_id_eq(
                  &universe, relation_chain_root->rule_id,
                  atom_symbol(&arena, "rel:chain")),
          "relational composition retains its result type, middle carrier, and both relation occurrences");

    CHECK(wrong_evidence_relation_chain == NULL &&
              wrong_middle_relation_chain == NULL &&
              wrong_endpoint_relation_chain == NULL &&
              wrong_middle_relation_result_type == NULL &&
              wrong_endpoint_relation_result_type == NULL,
          "relational composition formation and introduction decline on a too-small evidence universe, wrong middle carrier, or wrong endpoints");

    CettaPrimeTypedDerivationViewV1 chain_type_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_derivation(
              grandparent_chain_type, &chain_type_derivation),
          "native chain formation exposes its derivation");
    const CettaPrimeTypedDerivationNodeV1 *native_chain_type_root =
        find_derivation_node(
            &chain_type_derivation,
            chain_type_derivation.root_occurrence_identity);
    CHECK(native_chain_type_root &&
              native_chain_type_root->premise_count == 5u &&
              native_chain_type_root->premise_offset <=
                  chain_type_derivation.premise_occurrence_count &&
              native_chain_type_root->premise_count <=
                  chain_type_derivation.premise_occurrence_count -
                      native_chain_type_root->premise_offset &&
              chain_type_derivation.premise_occurrences[
                  native_chain_type_root->premise_offset] ==
                  node_metadata.occurrence_identity &&
              chain_type_derivation.premise_occurrences[
                  native_chain_type_root->premise_offset + 1u] ==
                  edge_metadata.occurrence_identity &&
              chain_type_derivation.premise_occurrences[
                  native_chain_type_root->premise_offset + 2u] ==
                  edge_metadata.occurrence_identity &&
              chain_type_derivation.premise_occurrences[
                  native_chain_type_root->premise_offset + 3u] ==
                  alice_metadata.occurrence_identity &&
              chain_type_derivation.premise_occurrences[
                  native_chain_type_root->premise_offset + 4u] ==
                  carol_metadata.occurrence_identity &&
              term_universe_atom_id_eq(
                  &universe, native_chain_type_root->rule_id,
                  atom_symbol(&arena, "chain:type")),
          "chain formation reads the relations' carried types without duplicate type premises");

    CettaPrimeTypedDerivationViewV1 chain_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_derivation(
              grandparent_chain_a, &chain_derivation),
          "native chain exposes its occurrence-retaining derivation");
    const CettaPrimeTypedDerivationNodeV1 *native_chain_root =
        find_derivation_node(
            &chain_derivation,
            chain_derivation.root_occurrence_identity);
    CHECK(native_chain_root && native_chain_root->premise_count == 4u &&
              native_chain_root->witness_count == 1u &&
              native_chain_root->premise_offset <=
                  chain_derivation.premise_occurrence_count &&
              native_chain_root->premise_count <=
                  chain_derivation.premise_occurrence_count -
                      native_chain_root->premise_offset &&
              native_chain_root->witness_offset <
                  chain_derivation.witness_count &&
              chain_derivation.premise_occurrences[
                  native_chain_root->premise_offset] ==
                  grandparent_chain_type_metadata.occurrence_identity &&
              chain_derivation.premise_occurrences[
                  native_chain_root->premise_offset + 1u] ==
                  bob_metadata.occurrence_identity &&
              chain_derivation.premise_occurrences[
                  native_chain_root->premise_offset + 2u] ==
                  alice_bob_a_metadata.occurrence_identity &&
              chain_derivation.premise_occurrences[
                  native_chain_root->premise_offset + 3u] ==
                  bob_carol_metadata.occurrence_identity &&
              chain_derivation.witness_ids[
                  native_chain_root->witness_offset] == bob_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, native_chain_root->rule_id,
                  atom_symbol(&arena, "chain")),
          "native chain retains its type, middle, and both evidence occurrences");

    CHECK(cetta_prime_typed_rel_type_v1(
              &arena, &space, node, node, alice) == NULL,
          "a term that is not a universe cannot masquerade as rel evidence");
    CHECK(wrong_target_answer_type == NULL &&
              wrong_source_answer == NULL &&
              wrong_evidence_answer == NULL,
          "dependent answer construction rejects a disconnected target carrier, source fibre, or evidence fibre");
    CettaPrimeTypedRelationViewV1 edge_relation_view = {0};
    CettaPrimeTypedRelationViewV1 not_relation_view = {0};
    CHECK(cetta_prime_typed_relation_v1_view(
              &arena, &space, edge, &edge_relation_view) &&
              edge_relation_view.source_type_id == node_metadata.term_id &&
              edge_relation_view.target_type_id == node_metadata.term_id &&
              edge_relation_view.evidence_universe_id ==
                  evidence_universe_metadata.term_id &&
              !cetta_prime_typed_relation_v1_view(
                  &arena, &space, alice, &not_relation_view),
          "a typed relation exposes its carried fibres while a non-relation does not");
    CHECK(cetta_prime_typed_hyp_primitive_v1(
              &arena, &space, alice,
              person_sort, person_sort, mother_symbol) == NULL,
          "hyp formation requires a carried authored constructor judgment");
    CHECK(cetta_prime_typed_value_apply_v1(
              &arena, &space, authored_primitive_rule, node) == NULL,
          "dependent application cannot consume an argument with the wrong carried type");
    CHECK(cetta_prime_typed_chain_type_v1(
              &arena, &space, evidence_universe,
              edge, edge, alice, carol) == NULL,
          "chain formation rejects a middle type outside the supplied rel fibres");
    CHECK(cetta_prime_typed_chain_v1(
              &arena, &space, grandparent_chain_type,
              dana, alice_bob_a, bob_carol) == NULL &&
              cetta_prime_typed_chain_v1(
                  &arena, &space, grandparent_chain_type,
                  bob, alice_bob_a, dana_carol) == NULL,
          "chain introduction rejects mismatched left and right evidence fibres");

    CettaPrimeTypedIndexedViewV1 grandparent_hyp_indexed = {0};
    CettaPrimeTypedDerivationViewV1 grandparent_hyp_derivation = {0};
    CHECK(cetta_prime_typed_value_v1_indexed_view(
              grandparent_hyp_a, &grandparent_hyp_indexed) &&
              grandparent_hyp_indexed.parameter_count == 2u &&
              grandparent_hyp_indexed.index_count == 2u &&
              grandparent_hyp_indexed.parameter_ids[0] ==
                  sort_code_metadata.term_id &&
              grandparent_hyp_indexed.parameter_ids[1] ==
                  primitive_vocabulary_metadata.term_id &&
              grandparent_hyp_indexed.index_ids[0] ==
                  person_sort_metadata.term_id &&
              grandparent_hyp_indexed.index_ids[1] ==
                  person_sort_metadata.term_id &&
              term_universe_atom_id_eq(
                  &universe, grandparent_hyp_indexed.family_head_id,
                  atom_symbol(&arena, "hyp")) &&
              cetta_prime_typed_value_v1_derivation(
                  grandparent_hyp_a, &grandparent_hyp_derivation),
          "chained hyp exposes its generic indexed-family and derivation views");

    const CettaPrimeTypedDerivationNodeV1 *chain_root =
        find_derivation_node(
            &grandparent_hyp_derivation,
            grandparent_hyp_derivation.root_occurrence_identity);
    CHECK(chain_root && chain_root->premise_count == 2u &&
              find_derivation_node(
                  &grandparent_hyp_derivation,
                  mother_hyp_a_metadata.occurrence_identity) &&
              find_derivation_node(
                  &grandparent_hyp_derivation,
                  father_hyp_metadata.occurrence_identity) &&
              find_derivation_node(
                  &grandparent_hyp_derivation,
                  person_sort_metadata.occurrence_identity) &&
              derivation_has_witness(
                  &grandparent_hyp_derivation,
                  person_sort_metadata.term_id) &&
              term_universe_atom_id_eq(
                  &universe, chain_root->rule_id,
                  atom_symbol(&arena, "app")),
          "authored hyp chain retains its shared sort and both premise derivations through ordinary application");

    Arena execution_arena;
    arena_init(&execution_arena);
    arena_set_runtime_kind(
        &execution_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
    Atom *executable_hyp = NULL;
    Atom *execution_input = parse_one(&execution_arena, "alice");
    bool erased_for_execution =
        cetta_prime_typed_value_v1_erase(
            grandparent_hyp_a, &universe, &execution_arena,
            &executable_hyp, NULL);
    Atom *quoted_executable_hyp = erased_for_execution
        ? atom_expr2(
              &execution_arena,
              atom_symbol(&execution_arena, "quote"), executable_hyp)
        : NULL;
    Atom *execution_query = quoted_executable_hyp && execution_input
        ? atom_expr3(
              &execution_arena,
              atom_symbol(&execution_arena, "hyp:run"),
              quoted_executable_hyp, execution_input)
        : NULL;
    Atom *expected_bob_proof = parse_one(
        &execution_arena,
        "(hyp:edge carol "
        "  (hyp:chain-proof bob "
        "    (hyp:primitive-proof mother-symbol mother-alice-bob) "
        "    (hyp:primitive-proof father-symbol father-bob-carol)))");
    Atom *expected_eve_proof = parse_one(
        &execution_arena,
        "(hyp:edge carol "
        "  (hyp:chain-proof eve "
        "    (hyp:primitive-proof mother-symbol mother-alice-eve) "
        "    (hyp:primitive-proof father-symbol father-eve-carol)))");
    Atom *expected_bob_evidence = parse_one(
        &execution_arena,
        "(Pair bob (Pair mother-alice-bob father-bob-carol))");
    Atom *expected_eve_evidence = parse_one(
        &execution_arena,
        "(Pair eve (Pair mother-alice-eve father-eve-carol))");
    Atom *expected_native_hyp_plan = parse_one(
        &execution_arena,
        "(superpose ("
        "  (hyp:edge carol "
        "    (hyp:chain-proof bob "
        "      (hyp:primitive-proof mother-symbol mother-alice-bob) "
        "      (hyp:primitive-proof father-symbol father-bob-carol))) "
        "  (hyp:edge carol "
        "    (hyp:chain-proof eve "
        "      (hyp:primitive-proof mother-symbol mother-alice-eve) "
        "      (hyp:primitive-proof father-symbol father-eve-carol)))))");
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    CettaPrimeNativeExecutionV1 native_hyp_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, execution_query);
    CettaPrimeTypedValueMetadataV1 native_hyp_execution_metadata = {0};
    CettaPrimeTypedIndexedViewV1 native_hyp_execution_indexed = {0};
    CHECK(native_hyp_execution.kind ==
              CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              native_hyp_execution.value &&
              native_hyp_execution.typed_value &&
              cetta_prime_typed_value_v1_metadata(
                  native_hyp_execution.typed_value,
                  &native_hyp_execution_metadata) &&
              native_hyp_execution_metadata.construction ==
                  CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1 &&
              term_universe_atom_id_eq(
                  &universe, native_hyp_execution_metadata.rule_id,
                  atom_symbol(&execution_arena, "rel:finite-search")) &&
              cetta_prime_typed_value_v1_indexed_view(
                  native_hyp_execution.typed_value,
                  &native_hyp_execution_indexed) &&
              native_hyp_execution_indexed.parameter_count == 1u &&
              native_hyp_execution_indexed.index_count == 0u &&
              term_universe_atom_id_eq(
                  &universe, native_hyp_execution_indexed.family_head_id,
                  atom_symbol(&execution_arena, "list")) &&
              cetta_prime_typed_value_v1_is_current(
                  native_hyp_execution.typed_value, &space) &&
              expected_native_hyp_plan &&
              atom_eq(
                  native_hyp_execution.value,
                  expected_native_hyp_plan) &&
              atom_symbol_occurrences(
                  native_hyp_execution.value, "hyp:run") == 0u &&
              atom_symbol_occurrences(
                  native_hyp_execution.value,
                  "hyp:run-primitive") == 0u &&
              atom_symbol_occurrences(
                  native_hyp_execution.value,
                  "hyp:chain-proof") == 2u,
          "the finite hyp provider materializes the exact ordered dependent answer bag as a current native List judgment");
    CettaPrimeNativeExecutionV1 native_hyp_execution_again =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, execution_query);
    CettaPrimeTypedValueMetadataV1
        native_hyp_execution_again_metadata = {0};
    CettaPrimeTypedDerivationViewV1 native_hyp_execution_derivation = {0};
    CettaPrimeTypedDerivationViewV1
        native_hyp_execution_again_derivation = {0};
    CHECK(native_hyp_execution_again.kind ==
              CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              native_hyp_execution_again.value &&
              native_hyp_execution_again.typed_value &&
              native_hyp_execution_again.typed_value !=
                  native_hyp_execution.typed_value &&
              atom_alpha_eq(
                  native_hyp_execution_again.value,
                  native_hyp_execution.value) &&
              cetta_prime_typed_value_v1_is_current(
                  native_hyp_execution_again.typed_value, &space) &&
              cetta_prime_typed_value_v1_metadata(
                  native_hyp_execution_again.typed_value,
                  &native_hyp_execution_again_metadata) &&
              native_hyp_execution_again_metadata.occurrence_identity !=
                  native_hyp_execution_metadata.occurrence_identity &&
              native_hyp_execution_again_metadata.term_id ==
                  native_hyp_execution_metadata.term_id &&
              native_hyp_execution_again_metadata.type_id ==
                  native_hyp_execution_metadata.type_id &&
              cetta_prime_typed_value_v1_derivation(
                  native_hyp_execution.typed_value,
                  &native_hyp_execution_derivation) &&
              cetta_prime_typed_value_v1_derivation(
                  native_hyp_execution_again.typed_value,
                  &native_hyp_execution_again_derivation) &&
              native_hyp_execution_again_derivation.
                      root_occurrence_identity !=
                  native_hyp_execution_derivation.
                      root_occurrence_identity &&
              native_hyp_execution_again_derivation.node_count ==
                  native_hyp_execution_derivation.node_count &&
              native_hyp_execution_again_derivation.
                      premise_occurrence_count ==
                  native_hyp_execution_derivation.
                      premise_occurrence_count &&
              native_hyp_execution_again_derivation.witness_count ==
                  native_hyp_execution_derivation.witness_count,
          "a current finite provider is reused while each query materializes a fresh proof-occurrence bag");
    const CettaPrimeTypedDerivationNodeV1 *native_search_root =
        find_derivation_node(
            &native_hyp_execution_derivation,
            native_hyp_execution_derivation.root_occurrence_identity);
    CHECK(derivation_rule_count(
              &execution_arena, &universe,
              &native_hyp_execution_derivation,
              "rel:finite-search") == 1u &&
              derivation_rule_count(
                  &execution_arena, &universe,
                  &native_hyp_execution_derivation,
                  "rel:answer") == 2u &&
              derivation_rule_count(
                  &execution_arena, &universe,
                  &native_hyp_execution_derivation, "rel:chain") == 1u &&
              derivation_rule_count(
                  &execution_arena, &universe,
                  &native_hyp_execution_derivation,
                  "rel:chain-type") == 1u &&
              derivation_rule_count(
                  &execution_arena, &universe,
                  &native_hyp_execution_derivation,
                  "conv:beta") == 2u &&
              native_search_root &&
              native_search_root->premise_count == 5u &&
              native_search_root->witness_count == 2u &&
              native_search_root->witness_offset <=
                  native_hyp_execution_derivation.witness_count &&
              native_search_root->witness_count <=
                  native_hyp_execution_derivation.witness_count -
                      native_search_root->witness_offset &&
              expected_bob_proof && expected_eve_proof &&
              expected_bob_evidence && expected_eve_evidence &&
              term_universe_atom_id_eq(
                  &universe,
                  native_hyp_execution_derivation.witness_ids[
                      native_search_root->witness_offset],
                  expected_bob_evidence) &&
              term_universe_atom_id_eq(
                  &universe,
                  native_hyp_execution_derivation.witness_ids[
                      native_search_root->witness_offset + 1u],
                  expected_eve_evidence),
          "the finite-search receipt retains both exact semantic evidences, their dependent answer occurrences, and the beta bridge to relational Chain");
    Atom *semantic_fallback_query = parse_one(
        &execution_arena,
        "(hyp:run (quote "
        "  (hyp:primitive sort-code alternate-primitive-vocabulary "
        "    person-sort person-sort alternate-father-symbol)) alice)");
    CettaPrimeNativeExecutionV1 semantic_fallback =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, semantic_fallback_query);
    CettaPrimeTypedValueMetadataV1 semantic_fallback_metadata = {0};
    CHECK(semantic_fallback_query &&
              semantic_fallback.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              semantic_fallback.value && semantic_fallback.typed_value &&
              atom_symbol_occurrences(
                  semantic_fallback.value, "hyp:run-primitive") == 1u &&
              cetta_prime_typed_value_v1_metadata(
                  semantic_fallback.typed_value,
                  &semantic_fallback_metadata) &&
              term_universe_atom_id_eq(
                  &universe, semantic_fallback_metadata.rule_id,
                  atom_symbol(&execution_arena, "app")),
          "missing authored meaning declines only the stronger denotation tier while structural native execution remains available");
    Atom *mismatched_hyp_execution_query = parse_one(
        &execution_arena,
        "(hyp:run (quote "
        "  (hyp:chain sort-code primitive-vocabulary "
        "    person-sort person-sort person-sort "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort mother-symbol) "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      number-sort number-sort successor-symbol))) alice)");
    CettaPrimeNativeExecutionV1 mismatched_hyp_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, mismatched_hyp_execution_query);
    CHECK(mismatched_hyp_execution_query &&
              mismatched_hyp_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !mismatched_hyp_execution.value &&
              !mismatched_hyp_execution.typed_value,
          "hyp construction rejects mismatched shared indices without minting a composite judgment");
    Atom *non_hyp_execution_query = parse_one(
        &execution_arena,
        "(hyp:run (quote person-sort) alice)");
    CettaPrimeNativeExecutionV1 non_hyp_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, non_hyp_execution_query);
    CHECK(non_hyp_execution_query &&
              non_hyp_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !non_hyp_execution.value &&
              !non_hyp_execution.typed_value,
          "a non-hyp quoted value retains ordinary relational fallback");
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats native_hyp_stats;
    cetta_runtime_stats_snapshot(&native_hyp_stats);
    cetta_runtime_stats_disable();
    bool native_hyp_stats_match = native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_CANDIDATE] == 5u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_REALIZED] == 3u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_MAP_REALIZED] == 0u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_DECLINED] == 2u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_FAULT] == 0u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_ADMISSION_CACHE_HIT] ==
                  1u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_ADMISSION_CACHE_MISS] ==
                  4u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_DENOTATION_ADMITTED] ==
                  1u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_DENOTATION_FALLBACK] ==
                  1u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_PROVIDER_ADMITTED] ==
                  1u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_PROVIDER_FALLBACK] ==
                  1u &&
              native_hyp_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_SEARCH_REALIZED] ==
                  2u;
    if (!native_hyp_stats_match) {
        fprintf(
            stderr,
            "  hyp finite provider admitted=%" PRIu64
            " fallback=%" PRIu64 " search=%" PRIu64 "\n",
            native_hyp_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_PROVIDER_ADMITTED],
            native_hyp_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_PROVIDER_FALLBACK],
            native_hyp_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_SEARCH_REALIZED]);
    }
    CHECK(native_hyp_stats_match,
          "the native-calculus receipt distinguishes semantic admission, structural fallback, cache reuse, and construction");
#endif
    Atom *candidate_query = parse_one(
        &execution_arena,
        "(hyp:chain-candidate-typed "
        "  &self sort-code primitive-vocabulary "
        "  person-sort person-sort)");
    Atom *expected_candidate_plan = parse_one(
        &execution_arena,
        "(superpose ("
        "  (quote (hyp:chain sort-code primitive-vocabulary "
        "    person-sort person-sort person-sort "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort mother-symbol) "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort mother-symbol))) "
        "  (quote (hyp:chain sort-code primitive-vocabulary "
        "    person-sort person-sort person-sort "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort mother-symbol) "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort father-symbol))) "
        "  (quote (hyp:chain sort-code primitive-vocabulary "
        "    person-sort person-sort person-sort "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort father-symbol) "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort mother-symbol))) "
        "  (quote (hyp:chain sort-code primitive-vocabulary "
        "    person-sort person-sort person-sort "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort father-symbol) "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort father-symbol)))))");
    const char *expected_candidate_sources[] = {
        "(hyp:chain sort-code primitive-vocabulary "
        "  person-sort person-sort person-sort "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort mother-symbol) "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort mother-symbol))",
        "(hyp:chain sort-code primitive-vocabulary "
        "  person-sort person-sort person-sort "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort mother-symbol) "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort father-symbol))",
        "(hyp:chain sort-code primitive-vocabulary "
        "  person-sort person-sort person-sort "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort father-symbol) "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort mother-symbol))",
        "(hyp:chain sort-code primitive-vocabulary "
        "  person-sort person-sort person-sort "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort father-symbol) "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort father-symbol))",
    };
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    CettaPrimeNativeExecutionV1 candidate_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, candidate_query);
    CettaPrimeTypedValueMetadataV1 candidate_metadata = {0};
    CettaPrimeTypedIndexedViewV1 candidate_indexed = {0};
    CettaPrimeTypedDerivationViewV1 candidate_derivation = {0};
    const CettaPrimeTypedDerivationNodeV1 *candidate_root = NULL;
    AtomId candidate_element_type_id = grandparent_hyp_a_metadata.type_id;
    AtomId expected_candidate_ids[4] = {0};
    bool expected_candidates_stored = true;
    for (size_t index = 0u;
         index < sizeof(expected_candidate_sources) /
                     sizeof(expected_candidate_sources[0]);
         index++) {
        Atom *expected_candidate = parse_one(
            &execution_arena, expected_candidate_sources[index]);
        expected_candidate_ids[index] = expected_candidate
            ? term_universe_store_atom_id(
                  &universe, &execution_arena, expected_candidate)
            : CETTA_ATOM_ID_NONE;
        expected_candidates_stored = expected_candidates_stored &&
            expected_candidate_ids[index] != CETTA_ATOM_ID_NONE;
    }
    bool candidate_receipt_observed =
        candidate_execution.typed_value &&
        cetta_prime_typed_value_v1_metadata(
            candidate_execution.typed_value, &candidate_metadata) &&
        cetta_prime_typed_value_v1_indexed_view(
            candidate_execution.typed_value, &candidate_indexed) &&
        cetta_prime_typed_value_v1_derivation(
            candidate_execution.typed_value, &candidate_derivation);
    if (candidate_receipt_observed)
        candidate_root = find_derivation_node(
            &candidate_derivation,
            candidate_derivation.root_occurrence_identity);
    bool candidate_result_matches =
        candidate_query && expected_candidate_plan &&
        candidate_execution.kind ==
            CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
        candidate_execution.value &&
        atom_eq(candidate_execution.value, expected_candidate_plan) &&
        candidate_receipt_observed &&
        candidate_metadata.construction ==
            CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1 &&
        term_universe_atom_id_eq(
            &universe, candidate_metadata.rule_id,
            atom_symbol(
                &execution_arena, "hyp:chain-candidate-typed")) &&
        cetta_prime_typed_value_v1_is_current(
            candidate_execution.typed_value, &space) &&
        candidate_indexed.parameter_count == 1u &&
        candidate_indexed.index_count == 0u &&
        candidate_indexed.parameter_ids[0] == candidate_element_type_id &&
        term_universe_atom_id_eq(
            &universe, candidate_indexed.family_head_id,
            atom_symbol(&execution_arena, "list"));
    if (!candidate_result_matches) {
        fprintf(
            stderr,
            "  candidate kind=%d value=%d receipt=%d current=%d "
            "construction=%d parameters=%zu indices=%zu "
            "element=%" PRIu64 " observed-element=%" PRIu64 "\n",
            (int)candidate_execution.kind,
            candidate_execution.value != NULL,
            candidate_receipt_observed,
            candidate_execution.typed_value &&
                cetta_prime_typed_value_v1_is_current(
                    candidate_execution.typed_value, &space),
            candidate_receipt_observed
                ? (int)candidate_metadata.construction : -1,
            candidate_receipt_observed
                ? candidate_indexed.parameter_count : 0u,
            candidate_receipt_observed
                ? candidate_indexed.index_count : 0u,
            candidate_element_type_id,
            candidate_receipt_observed &&
                    candidate_indexed.parameter_count != 0u
                ? candidate_indexed.parameter_ids[0]
                : CETTA_ATOM_ID_NONE);
        if (candidate_execution.value) {
            fputs("  candidate value=", stderr);
            atom_print(candidate_execution.value, stderr);
            fputc('\n', stderr);
        }
        if (expected_candidate_plan) {
            fputs("  expected value=", stderr);
            atom_print(expected_candidate_plan, stderr);
            fputc('\n', stderr);
        }
    }
    CHECK(candidate_result_matches,
          "native candidate construction returns the exact ordered bag with its current List-of-hyp judgment");
    bool candidate_root_shape = candidate_root &&
        candidate_root->premise_count == 5u &&
        candidate_root->witness_count == 4u &&
        candidate_root->premise_offset <=
            candidate_derivation.premise_occurrence_count &&
        candidate_root->premise_count <=
            candidate_derivation.premise_occurrence_count -
                candidate_root->premise_offset &&
        candidate_root->witness_offset <=
            candidate_derivation.witness_count &&
        candidate_root->witness_count <=
            candidate_derivation.witness_count -
                candidate_root->witness_offset;
    bool candidate_evidence_exact =
        candidate_root_shape && expected_candidates_stored;
    for (size_t index = 0u; candidate_evidence_exact && index < 4u;
         index++) {
        candidate_evidence_exact =
            candidate_derivation.witness_ids[
                candidate_root->witness_offset + index] ==
            expected_candidate_ids[index];
        for (size_t later = index + 1u;
             candidate_evidence_exact && later < 4u; later++) {
            candidate_evidence_exact =
                candidate_derivation.premise_occurrences[
                    candidate_root->premise_offset + index + 1u] !=
                candidate_derivation.premise_occurrences[
                    candidate_root->premise_offset + later + 1u];
        }
    }
    CHECK(candidate_evidence_exact,
          "the admitted operation receipt retains all four candidate occurrences and their exact authored programs");
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats candidate_stats;
    cetta_runtime_stats_snapshot(&candidate_stats);
    cetta_runtime_stats_disable();
    CHECK(candidate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_CANDIDATE] == 1u &&
              candidate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_CANDIDATE_BAG_REALIZED] ==
                  1u &&
              candidate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_DECLINED] == 0u &&
              candidate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_FAULT] == 0u &&
              candidate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_INTERIOR_CHECK] ==
                  0u &&
              candidate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_INTERIOR_CHECK] ==
                  0u,
          "candidate construction is one native realization with no interior synthesis or checking");
#endif
    EvalOutcome execution;
    eval_outcome_init(&execution);
    if (execution_query)
        metta_eval_outcome(
            &space, &execution_arena, NULL, execution_query, -1,
            &execution);
    bool execution_matches =
        erased_for_execution && execution_query &&
        expected_bob_proof && expected_eve_proof &&
        execution.completion == CETTA_EVAL_COMPLETE &&
        eval_outcome_fault_count(&execution) == 0u &&
        execution.results.len == 2u &&
        atom_eq(execution.results.items[0], expected_bob_proof) &&
        atom_eq(execution.results.items[1], expected_eve_proof);
    if (!execution_matches) {
        fprintf(
            stderr,
            "  hyp execution completion=%d results=%" PRIu64
            " faults=%" PRIu64 "\n",
            (int)execution.completion, execution.results.len,
            eval_outcome_fault_count(&execution));
        for (CettaCount index = 0u; index < execution.results.len; index++) {
            fputs("  result=", stderr);
            atom_print(execution.results.items[index], stderr);
            fputc('\n', stderr);
        }
    }
    CHECK(execution_matches,
          "typed hyp erasure executes as an ordinary proof-relevant relation with its ordered occurrence bag intact");
    eval_outcome_free(&execution);

    Atom *map_rel_query = parse_one(
        &execution_arena,
        "(map-rel:run sort-step (person-sort))");
    Atom *expected_map_rel_run_a = parse_one(
        &execution_arena,
        "(map-rel:edge (number-sort) "
        "  (map-rel:cons-proof person-sort number-sort "
        "    person-number-proof-a (map-rel:nil-proof)))");
    Atom *expected_map_rel_run_b = parse_one(
        &execution_arena,
        "(map-rel:edge (number-sort) "
        "  (map-rel:cons-proof person-sort number-sort "
        "    person-number-proof-b (map-rel:nil-proof)))");
    CettaPrimeNativeExecutionV1 native_map_rel_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, map_rel_query);
    CHECK(map_rel_query &&
              native_map_rel_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !native_map_rel_execution.value &&
              !native_map_rel_execution.typed_value,
          "an untyped authored relation remains outside native coverage while ordinary relational execution stays available");
    EvalOutcome map_rel_execution;
    eval_outcome_init(&map_rel_execution);
    if (map_rel_query)
        metta_eval_outcome(
            &space, &execution_arena, NULL, map_rel_query, -1,
            &map_rel_execution);
    bool map_rel_execution_matches =
        map_rel_query && expected_map_rel_run_a &&
        expected_map_rel_run_b &&
        map_rel_execution.completion == CETTA_EVAL_COMPLETE &&
        eval_outcome_fault_count(&map_rel_execution) == 0u &&
        map_rel_execution.results.len == 2u &&
        atom_eq(
            map_rel_execution.results.items[0],
            expected_map_rel_run_a) &&
        atom_eq(
            map_rel_execution.results.items[1],
            expected_map_rel_run_b);
    if (!map_rel_execution_matches) {
        fprintf(
            stderr,
            "  map-rel execution completion=%d results=%" PRIu64
            " faults=%" PRIu64 "\n",
            (int)map_rel_execution.completion,
            map_rel_execution.results.len,
            eval_outcome_fault_count(&map_rel_execution));
        for (CettaCount index = 0u;
             index < map_rel_execution.results.len; index++) {
            fputs("  result=", stderr);
            atom_print(map_rel_execution.results.items[index], stderr);
            fputc('\n', stderr);
        }
    }
    CHECK(map_rel_execution_matches,
          "ordinary map-rel execution preserves branching evidence and ordered answer multiplicity");
    eval_outcome_free(&map_rel_execution);

    /* The public `u0` carrier has legacy sort spelling `U1`, while fresh
       universe-polymorphic declarations elaborate to `Sort (LevelConst 0)`.
       Native lifting must cross that proved conversion seam explicitly. */
    Atom *legacy_map_rel_query = parse_one(
        &execution_arena, "(map-rel:run mother (alice))");
    Atom *expected_legacy_map_rel_plan = parse_one(
        &execution_arena,
        "(superpose ("
        "  (map-rel:edge (bob) "
        "    (map-rel:cons-proof alice bob mother-alice-bob "
        "      (map-rel:nil-proof))) "
        "  (map-rel:edge (eve) "
        "    (map-rel:cons-proof alice eve mother-alice-eve "
        "      (map-rel:nil-proof)))))");
    CettaPrimeNativeExecutionV1 legacy_map_rel_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, legacy_map_rel_query);
    CettaPrimeTypedDerivationViewV1 legacy_map_rel_derivation = {0};
    CHECK(legacy_map_rel_query && expected_legacy_map_rel_plan &&
              legacy_map_rel_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              legacy_map_rel_execution.value &&
              legacy_map_rel_execution.typed_value &&
              atom_eq(
                  legacy_map_rel_execution.value,
                  expected_legacy_map_rel_plan) &&
              cetta_prime_typed_value_v1_derivation(
                  legacy_map_rel_execution.typed_value,
                  &legacy_map_rel_derivation) &&
              derivation_rule_count(
                  &execution_arena, &universe,
                  &legacy_map_rel_derivation,
                  "conv:judgmental") > 0u,
          "native map-rel records the canonical U1-to-tower conversion instead of requiring syntactic universe identity");

    Atom *empty_map_rel_query = parse_one(
        &execution_arena, "(map-rel:run mother ())");
    Atom *expected_empty_map_rel_plan = parse_one(
        &execution_arena,
        "(superpose ((map-rel:edge () (map-rel:nil-proof))))");
    CettaPrimeNativeExecutionV1 empty_map_rel_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, empty_map_rel_query);
    CHECK(empty_map_rel_query && expected_empty_map_rel_plan &&
              empty_map_rel_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              empty_map_rel_execution.value &&
              empty_map_rel_execution.typed_value &&
              atom_eq(
                  empty_map_rel_execution.value,
                  expected_empty_map_rel_plan),
          "native map-rel realizes the empty List as one proof-relevant nil occurrence");

    Atom *empty_fibre_map_rel_query = parse_one(
        &execution_arena, "(map-rel:run mother (carol))");
    Atom *expected_empty_fibre_map_rel_plan = parse_one(
        &execution_arena, "(superpose ())");
    CettaPrimeNativeExecutionV1 empty_fibre_map_rel_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, empty_fibre_map_rel_query);
    CHECK(empty_fibre_map_rel_query && expected_empty_fibre_map_rel_plan &&
              empty_fibre_map_rel_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              empty_fibre_map_rel_execution.value &&
              empty_fibre_map_rel_execution.typed_value &&
              atom_eq(
                  empty_fibre_map_rel_execution.value,
                  expected_empty_fibre_map_rel_plan),
          "a covered empty element fibre remains an exact empty native result rather than becoming abstention or refutation");

    Atom *unauthored_empty_map_rel_query = parse_one(
        &execution_arena,
        "(map-rel:run silent-relation (alice))");
    CettaPrimeNativeExecutionV1 unauthored_empty_map_rel_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space,
            unauthored_empty_map_rel_query);
    EvalOutcome unauthored_empty_map_rel_outcome;
    eval_outcome_init(&unauthored_empty_map_rel_outcome);
    if (unauthored_empty_map_rel_query)
        metta_eval_outcome(
            &space, &execution_arena, NULL,
            unauthored_empty_map_rel_query, -1,
            &unauthored_empty_map_rel_outcome);
    CHECK(unauthored_empty_map_rel_query &&
              unauthored_empty_map_rel_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !unauthored_empty_map_rel_execution.value &&
              !unauthored_empty_map_rel_execution.typed_value &&
              unauthored_empty_map_rel_outcome.completion ==
                  CETTA_EVAL_COMPLETE &&
              eval_outcome_fault_count(
                  &unauthored_empty_map_rel_outcome) == 0u &&
              unauthored_empty_map_rel_outcome.results.len == 0u,
          "absence of authored relation occurrences does not become a native closed-world empty verdict");
    eval_outcome_free(&unauthored_empty_map_rel_outcome);

    Atom *map_rel_mismatch_query = parse_one(
        &execution_arena,
        "(map-rel:check sort-step (person-sort) ())");
    EvalOutcome map_rel_mismatch;
    eval_outcome_init(&map_rel_mismatch);
    if (map_rel_mismatch_query)
        metta_eval_outcome(
            &space, &execution_arena, NULL, map_rel_mismatch_query, -1,
            &map_rel_mismatch);
    CHECK(map_rel_mismatch_query &&
              map_rel_mismatch.completion == CETTA_EVAL_COMPLETE &&
              eval_outcome_fault_count(&map_rel_mismatch) == 0u &&
              map_rel_mismatch.results.len == 0u,
          "map-rel endpoint checking fails relationally on a shape mismatch without minting a type verdict");
    eval_outcome_free(&map_rel_mismatch);

    CHECK(cetta_prime_typed_hyp_chain_v1(
              &arena, &space, authored_chain_rule,
              person_sort, person_sort, person_sort,
              mother_hyp_a, successor_hyp) == NULL,
          "hyp chain cannot be constructed across a mismatched middle index");
    CHECK(cetta_prime_typed_hyp_chain_v1(
              &arena, &space, authored_chain_rule,
              person_sort, person_sort, person_sort,
              mother_hyp_a, alternate_father_hyp) == NULL,
          "hyp chain cannot be constructed across different primitive-vocabulary parameters");
    CettaPrimeTypedValueV1 *refl_person_sort =
        cetta_prime_typed_value_refl_v1(
            &arena, &space, person_sort);
    CHECK(refl_person_sort && cetta_prime_typed_hyp_chain_v1(
              &arena, &space, authored_chain_rule,
              person_sort, person_sort, person_sort,
              refl_person_sort, father_hyp) == NULL,
          "a different indexed family cannot masquerade as hyp");

    Atom *foreign_bias_candidate_query = parse_one(
        &execution_arena,
        "(hyp:chain-candidate-typed "
        "  unrelated-bias sort-code primitive-vocabulary "
        "  person-sort person-sort)");
    CettaPrimeNativeExecutionV1 foreign_bias_candidate =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, foreign_bias_candidate_query);
    CHECK(foreign_bias_candidate_query &&
              foreign_bias_candidate.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !foreign_bias_candidate.value &&
              !foreign_bias_candidate.typed_value,
          "a candidate query over an unowned bias remains an ordinary relational operation");

    /* Repeated authored declarations are repeated proof opportunities.  The
       native construction must preserve their ordered bag even when several
       occurrences erase to the same program. */
    Space duplicate_declaration_space;
    space_init_overlay(&duplicate_declaration_space, &space);
    add_form(
        &execution_arena, &duplicate_declaration_space,
        "(: mother-symbol "
        "  (primitive-vocabulary person-sort person-sort))");
    CettaPrimeNativeExecutionV1 duplicate_candidates =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &duplicate_declaration_space,
            candidate_query);
    CettaPrimeTypedDerivationViewV1 duplicate_derivation = {0};
    const CettaPrimeTypedDerivationNodeV1 *duplicate_root = NULL;
    Atom *duplicate_bag = duplicate_candidates.value &&
            duplicate_candidates.value->kind == ATOM_EXPR &&
            duplicate_candidates.value->expr.len == 2u &&
            atom_is_symbol(
                duplicate_candidates.value->expr.elems[0], "superpose")
        ? duplicate_candidates.value->expr.elems[1]
        : NULL;
    bool duplicate_receipt = duplicate_candidates.typed_value &&
        cetta_prime_typed_value_v1_derivation(
            duplicate_candidates.typed_value, &duplicate_derivation);
    if (duplicate_receipt)
        duplicate_root = find_derivation_node(
            &duplicate_derivation,
            duplicate_derivation.root_occurrence_identity);
    const size_t duplicate_expected_order[] = {
        0u, 1u, 0u, 2u, 3u, 2u, 0u, 1u, 0u,
    };
    bool duplicate_order_exact =
        duplicate_candidates.kind ==
            CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
        duplicate_bag && duplicate_bag->kind == ATOM_EXPR &&
        duplicate_bag->expr.len == 9u && duplicate_receipt &&
        duplicate_root && duplicate_root->premise_count == 10u &&
        duplicate_root->witness_count == 9u &&
        duplicate_root->premise_offset <=
            duplicate_derivation.premise_occurrence_count &&
        duplicate_root->premise_count <=
            duplicate_derivation.premise_occurrence_count -
                duplicate_root->premise_offset &&
        duplicate_root->witness_offset <=
            duplicate_derivation.witness_count &&
        duplicate_root->witness_count <=
            duplicate_derivation.witness_count -
                duplicate_root->witness_offset;
    for (size_t index = 0u;
         duplicate_order_exact &&
             index < sizeof(duplicate_expected_order) /
                         sizeof(duplicate_expected_order[0]);
         index++) {
        Atom *quoted = duplicate_bag->expr.elems[index];
        size_t expected_index = duplicate_expected_order[index];
        duplicate_order_exact = quoted && quoted->kind == ATOM_EXPR &&
            quoted->expr.len == 2u &&
            atom_is_symbol(quoted->expr.elems[0], "quote") &&
            term_universe_atom_id_eq(
                &universe, expected_candidate_ids[expected_index],
                quoted->expr.elems[1]) &&
            duplicate_derivation.witness_ids[
                duplicate_root->witness_offset + index] ==
                expected_candidate_ids[expected_index];
        for (size_t later = index + 1u;
             duplicate_order_exact && later < 9u; later++) {
            duplicate_order_exact =
                duplicate_derivation.premise_occurrences[
                    duplicate_root->premise_offset + index + 1u] !=
                duplicate_derivation.premise_occurrences[
                    duplicate_root->premise_offset + later + 1u];
        }
    }
    CHECK(duplicate_order_exact,
          "native candidate construction preserves duplicate authored occurrences, their order, and distinct proof identities");
    space_free(&duplicate_declaration_space);

    /* An open declaration may denote additional candidates after future
       instantiation.  Native closed-fragment construction therefore declines
       the whole call and leaves the authored relation to execute. */
    Space open_declaration_space;
    space_init_overlay(&open_declaration_space, &space);
    add_form(
        &execution_arena, &open_declaration_space,
        "(: open-symbol "
        "  (primitive-vocabulary $open-source person-sort))");
    CettaPrimeNativeExecutionV1 open_declaration_candidates =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &open_declaration_space,
            candidate_query);
    EvalOutcome open_declaration_fallback;
    eval_outcome_init(&open_declaration_fallback);
    metta_eval_outcome(
        &open_declaration_space, &execution_arena, NULL,
        candidate_query, -1, &open_declaration_fallback);
    CHECK(open_declaration_candidates.kind ==
              CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !open_declaration_candidates.value &&
              !open_declaration_candidates.typed_value &&
              open_declaration_fallback.completion ==
                  CETTA_EVAL_COMPLETE &&
              eval_outcome_fault_count(&open_declaration_fallback) == 0u &&
              open_declaration_fallback.results.len >= 4u,
          "an open candidate declaration causes principled native abstention while raw relational execution remains live");
    eval_outcome_free(&open_declaration_fallback);
    space_free(&open_declaration_space);

    /* Extending the authored operation invalidates the exact native
       realization.  No compatibility profile or parser metadata can retain
       authority for a changed relation. */
    Space candidate_drift_space;
    space_init_overlay(&candidate_drift_space, &space);
    add_form(
        &execution_arena, &candidate_drift_space,
        "(= (hyp:chain-candidate-typed "
        "      $bias $sorts $primitives $source-sort $target-sort) "
        "   (superpose ()))");
    CettaPrimeNativeExecutionV1 drifted_candidates =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &candidate_drift_space,
            candidate_query);
    CHECK(drifted_candidates.kind ==
              CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !drifted_candidates.value &&
              !drifted_candidates.typed_value,
          "an extended candidate presentation declines the former native realization instead of inheriting its authority");
    space_free(&candidate_drift_space);

    Atom *stale_candidate_term = NULL;
    Atom *stale_candidate_type = NULL;
    CHECK(candidate_execution.typed_value &&
              !cetta_prime_typed_value_v1_is_current(
                  candidate_execution.typed_value, &space) &&
              cetta_prime_typed_value_v1_erase(
                  candidate_execution.typed_value, &universe, &arena,
                  &stale_candidate_term, &stale_candidate_type) &&
              stale_candidate_term && stale_candidate_type,
          "a revised environment makes the candidate receipt stale while preserving exact erasure for relational fallback");

    /* A subsequent authored semantic assignment is genuine ambiguity, not a
       typing obstruction.  It advances the shared universe epoch, invalidates
       earlier admissions, and leaves the structural native realization in
       charge while the stronger denotation tier abstains. */
    Space ambiguous_semantics_space;
    space_init_overlay(&ambiguous_semantics_space, &space);
    add_form(
        &execution_arena, &ambiguous_semantics_space,
        "(= (hyp:meaning person-sort person-sort mother-symbol) father)");
    Atom *ambiguous_semantics_query = parse_one(
        &execution_arena,
        "(hyp:run (quote "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort mother-symbol)) alice)");
    CettaPrimeNativeExecutionV1 ambiguous_semantics_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &ambiguous_semantics_space,
            ambiguous_semantics_query);
    CettaPrimeTypedValueMetadataV1
        ambiguous_semantics_execution_metadata = {0};
    CHECK(ambiguous_semantics_query &&
              ambiguous_semantics_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              ambiguous_semantics_execution.value &&
              ambiguous_semantics_execution.typed_value &&
              atom_symbol_occurrences(
                  ambiguous_semantics_execution.value,
                  "hyp:run-primitive") == 1u &&
              cetta_prime_typed_value_v1_metadata(
                  ambiguous_semantics_execution.typed_value,
                  &ambiguous_semantics_execution_metadata) &&
              term_universe_atom_id_eq(
                  &universe,
                  ambiguous_semantics_execution_metadata.rule_id,
                  atom_symbol(&execution_arena, "app")) &&
              cetta_prime_typed_value_v1_is_current(
                  ambiguous_semantics_execution.typed_value,
                  &ambiguous_semantics_space),
          "ambiguous authored meaning abstains from denotation without suppressing structural native execution");
    space_free(&ambiguous_semantics_space);

    /* Returning to the exact base presentation must not revive the cache
       entry invalidated by the ambiguity mutation. */
    Atom *renewed_native_hyp_query = parse_one(
        &execution_arena,
        "(hyp:run (quote "
        "  (hyp:chain sort-code primitive-vocabulary "
        "    person-sort person-sort person-sort "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort mother-symbol) "
        "    (hyp:primitive sort-code primitive-vocabulary "
        "      person-sort person-sort father-symbol))) alice)");
    CettaPrimeNativeExecutionV1 renewed_native_hyp_execution =
        cetta_prime_native_calculus_try_v1(
            &execution_arena, &space, renewed_native_hyp_query);
    CettaPrimeTypedValueMetadataV1
        renewed_native_hyp_execution_metadata = {0};
    bool renewed_native_hyp_metadata =
        renewed_native_hyp_execution.typed_value &&
        cetta_prime_typed_value_v1_metadata(
            renewed_native_hyp_execution.typed_value,
            &renewed_native_hyp_execution_metadata);
    bool renewed_native_hyp_matches =
        renewed_native_hyp_execution.kind ==
              CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
        renewed_native_hyp_execution.value &&
        renewed_native_hyp_execution.typed_value &&
        expected_native_hyp_plan &&
        atom_eq(
            renewed_native_hyp_execution.value,
            expected_native_hyp_plan) &&
        atom_symbol_occurrences(
            renewed_native_hyp_execution.value, "hyp:run") == 0u &&
        atom_symbol_occurrences(
            renewed_native_hyp_execution.value,
            "hyp:run-primitive") == 0u &&
        atom_symbol_occurrences(
            renewed_native_hyp_execution.value,
            "hyp:chain-proof") == 2u &&
        cetta_prime_typed_value_v1_is_current(
            renewed_native_hyp_execution.typed_value, &space) &&
        renewed_native_hyp_metadata &&
        term_universe_atom_id_eq(
            &universe, renewed_native_hyp_execution_metadata.rule_id,
            atom_symbol(&execution_arena, "rel:finite-search")) &&
        renewed_native_hyp_execution_metadata.occurrence_identity !=
            native_hyp_execution_metadata.occurrence_identity;
    if (!renewed_native_hyp_matches) {
        fprintf(
            stderr,
            "  renewed hyp kind=%d value=%d typed=%d shape=%d current=%d "
            "metadata=%d old-occ=%" PRIu64 " new-occ=%" PRIu64 "\n",
            (int)renewed_native_hyp_execution.kind,
            renewed_native_hyp_execution.value != NULL,
            renewed_native_hyp_execution.typed_value != NULL,
            renewed_native_hyp_execution.value &&
                atom_symbol_occurrences(
                    renewed_native_hyp_execution.value,
                    "hyp:run") == 0u &&
                atom_symbol_occurrences(
                    renewed_native_hyp_execution.value,
                    "hyp:run-primitive") == 0u,
            renewed_native_hyp_execution.typed_value &&
                cetta_prime_typed_value_v1_is_current(
                    renewed_native_hyp_execution.typed_value, &space),
            renewed_native_hyp_metadata,
            native_hyp_execution_metadata.occurrence_identity,
            renewed_native_hyp_execution_metadata.occurrence_identity);
    }
    CHECK(renewed_native_hyp_matches,
          "an authority-environment revision cannot revive a stale hyp admission and instead reconstructs the exact current finite search");
    arena_free(&execution_arena);
    Atom *stale_term = NULL;
    Atom *stale_type = NULL;
    Atom *stale_map_rel_term = NULL;
    Atom *stale_mapped_list_term = NULL;
    Atom *stale_graph_agreement_term = NULL;
    Atom *stale_graph_target_term = NULL;
    CHECK(!cetta_prime_typed_value_v1_is_current(
              grandparent_hyp_a, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  grandparent_chain_a, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  grandparent_relation_result_type, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  grandparent_relation, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  grandparent_relation_fibre, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  grandparent_relation_evidence, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  person_number_map_rel_a, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  person_number_list_elimination, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  map_rel_elimination_a, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  person_number_sort_list_map, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  graph_person_evidence, &space) &&
              !cetta_prime_typed_value_v1_is_current(
                  graph_person_number_map_rel, &space) &&
              cetta_prime_typed_chain_v1(
                  &arena, &space, grandparent_chain_type,
                  bob, alice_bob_a, bob_carol) == NULL &&
              cetta_prime_typed_relation_chain_v1(
                  &arena, &space, edge_relation_type,
                  node, edge, edge) == NULL &&
              cetta_prime_typed_relation_chain_result_type_v1(
                  &arena, &space, node, node, node,
                  edge, edge) == NULL &&
              cetta_prime_typed_hyp_chain_v1(
                  &arena, &space, authored_chain_rule,
                  person_sort, person_sort, person_sort,
                  mother_hyp_a, father_hyp) == NULL &&
              cetta_prime_typed_value_apply_v1(
                  &arena, &space,
                  authored_primitive_rule, person_sort) == NULL &&
              cetta_prime_typed_value_apply_converting_v1(
                  &arena, &space,
                  list_nil_rule, grandparent_answer_type) == NULL &&
              !cetta_prime_typed_list_runtime_representation_v1(
                  &arena, &space, person_number_sort_list,
                  &not_a_list_runtime) &&
              cetta_prime_typed_list_map_rel_cons_v1(
                  &arena, &space, map_rel_cons_rule,
                  sort_code, sort_code, sort_relation,
                  person_sort, number_sort,
                  empty_sort_list, empty_sort_list,
                  person_number_proof_a,
                  empty_sort_map_rel) == NULL &&
              cetta_prime_typed_list_eliminate_v1(
                  &arena, &space, list_eliminate_rule,
                  sort_code, list_result_type, list_result_nil,
                  list_result_cons, person_number_sort_list) == NULL &&
              cetta_prime_typed_list_map_v1(
                  &arena, &space, native_map_program,
                  sort_code, sort_code, sort_next,
                  person_number_sort_list) == NULL &&
              cetta_prime_typed_value_convert_beta_v1(
                  &arena, &space,
                  mapped_person_refl, graph_person_fibre) == NULL &&
              cetta_prime_typed_list_map_rel_eliminate_v1(
                  &arena, &space, map_rel_eliminate_rule,
                  sort_code, sort_code, sort_relation,
                  map_rel_result_type, map_rel_result_nil,
                  map_rel_result_cons, person_sort_list,
                  number_sort_list, person_number_map_rel_a) == NULL &&
              cetta_prime_typed_value_v1_erase(
                  grandparent_hyp_a, &universe, &arena,
                  &stale_term, &stale_type) &&
              cetta_prime_typed_value_v1_erase(
                  person_number_map_rel_a, &universe, &arena,
                  &stale_map_rel_term, NULL) &&
              cetta_prime_typed_value_v1_erase(
                  person_number_sort_list_map, &universe, &arena,
                  &stale_mapped_list_term, NULL) &&
              cetta_prime_typed_value_v1_erase(
                  graph_person_number_map_rel, &universe, &arena,
                  &stale_graph_agreement_term, NULL) &&
              cetta_prime_typed_value_v1_erase(
                  graph_target_list, &universe, &arena,
                  &stale_graph_target_term, NULL) &&
              atom_eq(stale_term, expected_authored_chain) &&
              atom_eq(stale_type, expected_type) &&
              atom_eq(stale_map_rel_term, expected_map_rel_a) &&
              atom_eq(
                  stale_mapped_list_term,
                  expected_mapped_person_number_list) &&
              atom_eq(
                  stale_graph_agreement_term,
                  graph_agreement_term) &&
              atom_eq(
                  stale_graph_target_term,
                  expected_mapped_person_number_list),
          "stale admission blocks construction while exact erasure remains available");

    /* An overlay mutation advances the shared TermUniverse epoch, so exercise
       presentation drift only after all earlier admission-currentness checks. */
    Space extended_hyp_space;
    space_init_overlay(&extended_hyp_space, &space);
    Atom *extended_hyp_equation = parse_one(
        &arena,
        "(= (hyp:run (quote $program) $input) "
        "   (hyp:edge extension extension-proof))");
    Atom *extended_hyp_query = parse_one(
        &arena,
        "(hyp:run (quote "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort mother-symbol)) alice)");
    bool extended_hyp_added = extended_hyp_equation &&
        space_admit_atom(
            &extended_hyp_space, &arena,
            extended_hyp_equation);
    CettaPrimeNativeExecutionV1 extended_hyp_execution =
        cetta_prime_native_calculus_try_v1(
            &arena, &extended_hyp_space, extended_hyp_query);
    CHECK(extended_hyp_added && extended_hyp_query &&
              extended_hyp_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !extended_hyp_execution.value &&
              !extended_hyp_execution.typed_value,
          "an extended hyp operational presentation retains every authored branch by declining to relational execution");
    Atom *extended_hyp_expected_bob = parse_one(
        &arena,
        "(hyp:edge bob "
        "  (hyp:primitive-proof mother-symbol mother-alice-bob))");
    Atom *extended_hyp_expected_eve = parse_one(
        &arena,
        "(hyp:edge eve "
        "  (hyp:primitive-proof mother-symbol mother-alice-eve))");
    Atom *extended_hyp_expected_authored = parse_one(
        &arena, "(hyp:edge extension extension-proof)");
    EvalOutcome extended_hyp_outcome;
    eval_outcome_init(&extended_hyp_outcome);
    if (extended_hyp_query)
        metta_eval_outcome(
            &extended_hyp_space, &arena, NULL,
            extended_hyp_query, -1, &extended_hyp_outcome);
    CHECK(extended_hyp_expected_bob && extended_hyp_expected_eve &&
              extended_hyp_expected_authored &&
              extended_hyp_outcome.completion == CETTA_EVAL_COMPLETE &&
              eval_outcome_fault_count(&extended_hyp_outcome) == 0u &&
              extended_hyp_outcome.results.len == 3u &&
              atom_eq(
                  extended_hyp_outcome.results.items[0],
                  extended_hyp_expected_bob) &&
              atom_eq(
                  extended_hyp_outcome.results.items[1],
                  extended_hyp_expected_eve) &&
              atom_eq(
                  extended_hyp_outcome.results.items[2],
                  extended_hyp_expected_authored),
          "relational fallback preserves the original ordered occurrence bag and the newly authored branch");
    eval_outcome_free(&extended_hyp_outcome);
    space_free(&extended_hyp_space);

    /* A relation with an open rule remains meaningful to ordinary relational
       execution, but it is not a complete finite fact provider.  Declining
       that stronger realization must preserve every authored occurrence. */
    Space open_relation_space;
    space_init_overlay(&open_relation_space, &space);
    add_form(
        &arena, &open_relation_space,
        "(= (mother $source) "
        "   (rel:edge carol open-mother-proof))");
    Atom *open_relation_query = parse_one(
        &arena,
        "(hyp:run (quote "
        "  (hyp:primitive sort-code primitive-vocabulary "
        "    person-sort person-sort mother-symbol)) alice)");
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    CettaPrimeNativeExecutionV1 open_relation_execution =
        cetta_prime_native_calculus_try_v1(
            &arena, &open_relation_space, open_relation_query);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats open_relation_stats;
    cetta_runtime_stats_snapshot(&open_relation_stats);
    cetta_runtime_stats_disable();
#endif
    CettaPrimeTypedValueMetadataV1 open_relation_metadata = {0};
    CHECK(open_relation_query &&
              open_relation_execution.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_REALIZED &&
              open_relation_execution.value &&
              open_relation_execution.typed_value &&
              atom_symbol_occurrences(
                  open_relation_execution.value,
                  "hyp:run-primitive") == 1u &&
              cetta_prime_typed_value_v1_metadata(
                  open_relation_execution.typed_value,
                  &open_relation_metadata) &&
              term_universe_atom_id_eq(
                  &universe, open_relation_metadata.rule_id,
                  atom_symbol(&arena, "hyp:denote"))
#if CETTA_BUILD_WITH_RUNTIME_STATS
              && open_relation_stats.counters[
                  CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_PROVIDER_ADMITTED] ==
                  0u &&
              open_relation_stats.counters[
                  CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_PROVIDER_FALLBACK] ==
                  1u &&
              open_relation_stats.counters[
                  CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_SEARCH_REALIZED] ==
                  0u
#endif
          ,
          "an open relation declines only the finite provider while retaining the structural typed operation");
    Atom *open_relation_expected_bob = parse_one(
        &arena,
        "(hyp:edge bob "
        "  (hyp:primitive-proof mother-symbol mother-alice-bob))");
    Atom *open_relation_expected_eve = parse_one(
        &arena,
        "(hyp:edge eve "
        "  (hyp:primitive-proof mother-symbol mother-alice-eve))");
    Atom *open_relation_expected_authored = parse_one(
        &arena,
        "(hyp:edge carol "
        "  (hyp:primitive-proof mother-symbol open-mother-proof))");
    EvalOutcome open_relation_outcome;
    eval_outcome_init(&open_relation_outcome);
    if (open_relation_query)
        metta_eval_outcome(
            &open_relation_space, &arena, NULL,
            open_relation_query, -1, &open_relation_outcome);
    CHECK(open_relation_expected_bob && open_relation_expected_eve &&
              open_relation_expected_authored &&
              open_relation_outcome.completion == CETTA_EVAL_COMPLETE &&
              eval_outcome_fault_count(&open_relation_outcome) == 0u &&
              open_relation_outcome.results.len == 3u &&
              atom_eq(
                  open_relation_outcome.results.items[0],
                  open_relation_expected_bob) &&
              atom_eq(
                  open_relation_outcome.results.items[1],
                  open_relation_expected_eve) &&
              atom_eq(
                  open_relation_outcome.results.items[2],
                  open_relation_expected_authored),
          "relational execution preserves the full ordered bag when the finite provider abstains");
    eval_outcome_free(&open_relation_outcome);

    Atom *open_map_rel_query = parse_one(
        &arena, "(map-rel:run mother (alice))");
    Atom *open_map_rel_expected_bob = parse_one(
        &arena,
        "(map-rel:edge (bob) "
        "  (map-rel:cons-proof alice bob mother-alice-bob "
        "    (map-rel:nil-proof)))");
    Atom *open_map_rel_expected_eve = parse_one(
        &arena,
        "(map-rel:edge (eve) "
        "  (map-rel:cons-proof alice eve mother-alice-eve "
        "    (map-rel:nil-proof)))");
    Atom *open_map_rel_expected_authored = parse_one(
        &arena,
        "(map-rel:edge (carol) "
        "  (map-rel:cons-proof alice carol open-mother-proof "
        "    (map-rel:nil-proof)))");
    CettaPrimeNativeExecutionV1 open_map_rel_native =
        cetta_prime_native_calculus_try_v1(
            &arena, &open_relation_space, open_map_rel_query);
    EvalOutcome open_map_rel_outcome;
    eval_outcome_init(&open_map_rel_outcome);
    if (open_map_rel_query)
        metta_eval_outcome(
            &open_relation_space, &arena, NULL,
            open_map_rel_query, -1, &open_map_rel_outcome);
    CHECK(open_map_rel_query && open_map_rel_expected_bob &&
              open_map_rel_expected_eve &&
              open_map_rel_expected_authored &&
              open_map_rel_native.kind ==
                  CETTA_PRIME_NATIVE_EXECUTION_DECLINED &&
              !open_map_rel_native.value &&
              !open_map_rel_native.typed_value &&
              open_map_rel_outcome.completion == CETTA_EVAL_COMPLETE &&
              eval_outcome_fault_count(&open_map_rel_outcome) == 0u &&
              open_map_rel_outcome.results.len == 3u &&
              atom_eq(
                  open_map_rel_outcome.results.items[0],
                  open_map_rel_expected_bob) &&
              atom_eq(
                  open_map_rel_outcome.results.items[1],
                  open_map_rel_expected_eve) &&
              atom_eq(
                  open_map_rel_outcome.results.items[2],
                  open_map_rel_expected_authored),
          "open map-rel coverage declines natively while authored execution preserves every old and new occurrence");
    eval_outcome_free(&open_map_rel_outcome);
    space_free(&open_relation_space);

    /* The external producer supplies syntax, but Prime reconstructs these
     * indexed proofs from typed leaves and native constructors.  The opaque
     * whole-proof checker remains only the fallback for other families. */
    CettaPrimeTypedValueV1 *all_ingress_goal = import_native(
        &arena, &space,
        "(rel:all sort-code sort-predicate "
        "  (list:cons sort-code person-sort (list:nil sort-code)))");
    Atom *all_native_run = parse_one(
        &arena,
        "(compile-result proof-occurrence-bag "
        "  (occurrences "
        "    (occurrence (quote "
        "      (rel:all:cons sort-code sort-predicate "
        "        person-sort (list:nil sort-code) "
        "        person-predicate-proof-a "
        "        (rel:all:nil sort-code sort-predicate))))) "
        "  (run-metrics 1 1 1 1 1) rel-all-native-revision)");
    CettaPrimeRuleMachineIngressResultV1 all_native_ingress;
    bool all_native_ingress_observed =
        all_ingress_goal && all_native_run &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, all_native_run, all_ingress_goal,
            false, 0u, &all_native_ingress);
    CettaPrimeTypedDerivationViewV1 all_native_derivation = {0};
    CettaPrimeTypedIndexedViewV1 all_native_indexed = {0};
    Atom *all_native_term = all_native_ingress_observed &&
            all_native_ingress.occurrence_count == 1u &&
            all_native_ingress.occurrences[0].value
        ? erase_term(
              &arena, &universe,
              all_native_ingress.occurrences[0].value)
        : NULL;
    Atom *all_native_type = all_native_ingress_observed &&
            all_native_ingress.occurrence_count == 1u &&
            all_native_ingress.occurrences[0].value
        ? erase_type(
              &arena, &universe,
              all_native_ingress.occurrences[0].value)
        : NULL;
    Atom *all_ingress_goal_term = all_ingress_goal
        ? erase_term(&arena, &universe, all_ingress_goal)
        : NULL;
    CHECK(all_native_ingress_observed &&
              all_native_ingress.completion ==
                  CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 &&
              all_native_ingress.occurrence_count == 1u &&
              all_native_ingress.occurrences &&
              all_native_ingress.occurrences[0].mode ==
                  CETTA_PRIME_RULE_MACHINE_INGRESS_NATIVE_CONSTRUCTION_V1 &&
              all_native_ingress.occurrences[0].value &&
              all_native_term && all_native_type &&
              atom_eq(all_native_term, person_sort_all_a_term) &&
              atom_eq(all_native_type, all_ingress_goal_term) &&
              cetta_prime_typed_value_v1_indexed_view(
                  all_native_ingress.occurrences[0].value,
                  &all_native_indexed) &&
              term_universe_atom_id_eq(
                  &universe, all_native_indexed.family_head_id,
                  atom_symbol(&arena, "rel:all")) &&
              cetta_prime_typed_value_v1_derivation(
                  all_native_ingress.occurrences[0].value,
                  &all_native_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &all_native_derivation,
                  "typed:boundary-check") == 0u &&
              derivation_rule_count(
                  &arena, &universe, &all_native_derivation,
                  "typed:rule-machine-ingress") == 1u &&
              derivation_rule_count(
                  &arena, &universe, &all_native_derivation,
                  "app") >= 6u,
          "RuleMachine rel:all proof data is reconstructed natively rather than replay-checked as one blob");

    CettaPrimeTypedValueV1 *fold_ingress_goal = import_native(
        &arena, &space,
        "(rel:fold sort-code sort-code sort-fold-step "
        "  person-sort "
        "  (list:cons sort-code person-sort (list:nil sort-code)) "
        "  number-sort)");
    Atom *fold_native_run = parse_one(
        &arena,
        "(compile-result proof-occurrence-bag "
        "  (occurrences "
        "    (occurrence (quote "
        "      (rel:fold:cons sort-code sort-code sort-fold-step "
        "        person-sort person-sort (list:nil sort-code) "
        "        number-sort number-sort sort-fold-step-proof "
        "        (rel:fold:nil sort-code sort-code sort-fold-step "
        "          number-sort))))) "
        "  (run-metrics 1 1 1 1 1) rel-fold-native-revision)");
    CettaPrimeRuleMachineIngressResultV1 fold_native_ingress;
    bool fold_native_ingress_observed =
        fold_ingress_goal && fold_native_run &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, fold_native_run, fold_ingress_goal,
            false, 0u, &fold_native_ingress);
    CettaPrimeTypedDerivationViewV1 fold_native_derivation = {0};
    CettaPrimeTypedIndexedViewV1 fold_native_indexed = {0};
    Atom *fold_native_term = fold_native_ingress_observed &&
            fold_native_ingress.occurrence_count == 1u &&
            fold_native_ingress.occurrences[0].value
        ? erase_term(
              &arena, &universe,
              fold_native_ingress.occurrences[0].value)
        : NULL;
    Atom *fold_native_type = fold_native_ingress_observed &&
            fold_native_ingress.occurrence_count == 1u &&
            fold_native_ingress.occurrences[0].value
        ? erase_type(
              &arena, &universe,
              fold_native_ingress.occurrences[0].value)
        : NULL;
    Atom *fold_ingress_goal_term = fold_ingress_goal
        ? erase_term(&arena, &universe, fold_ingress_goal)
        : NULL;
    CHECK(fold_native_ingress_observed &&
              fold_native_ingress.completion ==
                  CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 &&
              fold_native_ingress.occurrence_count == 1u &&
              fold_native_ingress.occurrences &&
              fold_native_ingress.occurrences[0].mode ==
                  CETTA_PRIME_RULE_MACHINE_INGRESS_NATIVE_CONSTRUCTION_V1 &&
              fold_native_ingress.occurrences[0].value &&
              fold_native_term && fold_native_type &&
              atom_eq(fold_native_term, person_sort_fold_term) &&
              atom_eq(fold_native_type, fold_ingress_goal_term) &&
              cetta_prime_typed_value_v1_indexed_view(
                  fold_native_ingress.occurrences[0].value,
                  &fold_native_indexed) &&
              term_universe_atom_id_eq(
                  &universe, fold_native_indexed.family_head_id,
                  atom_symbol(&arena, "rel:fold")) &&
              cetta_prime_typed_value_v1_derivation(
                  fold_native_ingress.occurrences[0].value,
                  &fold_native_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &fold_native_derivation,
                  "typed:boundary-check") == 0u &&
              derivation_rule_count(
                  &arena, &universe, &fold_native_derivation,
                  "typed:rule-machine-ingress") == 1u &&
              derivation_rule_count(
                  &arena, &universe, &fold_native_derivation,
                  "app") >= 10u,
          "RuleMachine rel:fold proof data is reconstructed natively with its intermediate accumulator and evidence path");

    Atom *wrong_all_native_run = parse_one(
        &arena,
        "(compile-result proof-occurrence-bag "
        "  (occurrences "
        "    (occurrence (quote "
        "      (rel:all:cons sort-code sort-predicate "
        "        person-sort (list:nil sort-code) "
        "        person-number-proof-a "
        "        (rel:all:nil sort-code sort-predicate))))) "
        "  (run-metrics 1 1 1 1 1) rel-all-wrong-revision)");
    CettaPrimeRuleMachineIngressResultV1 wrong_all_native_ingress;
    bool wrong_all_native_observed = all_ingress_goal &&
        wrong_all_native_run &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, wrong_all_native_run, all_ingress_goal,
            false, 0u, &wrong_all_native_ingress);
    CHECK(wrong_all_native_observed &&
              wrong_all_native_ingress.occurrence_count == 1u &&
              wrong_all_native_ingress.occurrences &&
              wrong_all_native_ingress.occurrences[0].mode ==
                  CETTA_PRIME_RULE_MACHINE_INGRESS_NONE_V1 &&
              !wrong_all_native_ingress.occurrences[0].value &&
              wrong_all_native_ingress.occurrences[0]
                      .checking.authority.result.kind ==
                  CETTA_NIK_RESULT_OUTCOME &&
              wrong_all_native_ingress.occurrences[0]
                      .checking.authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_REFUTED,
          "an ill-indexed rel:all proof receives neither native construction nor a false Established fallback");

    add_file(
        &arena, &space,
        "lib/ilp/hopper_table1_first_order_types.metta");
    const char *hopper_all_even_goal_text =
        "(hopper:all-even:f "
        "  (list:cons hopper:nat hopper:nat:n2 "
        "    (list:cons hopper:nat hopper:nat:n4 "
        "      (list:cons hopper:nat hopper:nat:n6 "
        "        (list:cons hopper:nat hopper:nat:n8 "
        "          (list:cons hopper:nat hopper:nat:n10 "
        "            (list:nil hopper:nat)))))))";
    CettaPrimeTypedValueV1 *hopper_all_even_goal = import_native(
        &arena, &space, hopper_all_even_goal_text);
    Atom *hopper_all_even_run = run_rule_machine_definition(
        &arena,
        "lib/ilp/hopper_table1_first_order_rules.metta",
        "hopper:table1:first-order:package",
        "(quote "
        "  (hopper:all-even:f "
        "    (list:cons hopper:nat hopper:nat:n2 "
        "      (list:cons hopper:nat hopper:nat:n4 "
        "        (list:cons hopper:nat hopper:nat:n6 "
        "          (list:cons hopper:nat hopper:nat:n8 "
        "            (list:cons hopper:nat hopper:nat:n10 "
        "              (list:nil hopper:nat))))))))",
        512, 10000000, 4096);
    CettaPrimeRuleMachineIngressResultV1 hopper_all_even_ingress;
    bool hopper_all_even_observed = hopper_all_even_goal &&
        hopper_all_even_run &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, hopper_all_even_run,
            hopper_all_even_goal, false, 0u,
            &hopper_all_even_ingress);
    Atom *hopper_all_even_proof = hopper_all_even_observed &&
            hopper_all_even_ingress.occurrence_count == 1u &&
            hopper_all_even_ingress.occurrences[0].value
        ? erase_term(
              &arena, &universe,
              hopper_all_even_ingress.occurrences[0].value)
        : NULL;
    CettaPrimeTypedDerivationViewV1 hopper_all_even_derivation = {0};
    CHECK(hopper_all_even_observed &&
              hopper_all_even_ingress.completion ==
                  CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 &&
              hopper_all_even_ingress.occurrence_count == 1u &&
              hopper_all_even_ingress.occurrences &&
              hopper_all_even_ingress.occurrences[0].mode ==
                  CETTA_PRIME_RULE_MACHINE_INGRESS_NATIVE_CONSTRUCTION_V1 &&
              hopper_all_even_ingress.occurrences[0].value &&
              hopper_all_even_proof &&
              atom_symbol_occurrences(
                  hopper_all_even_proof, "hopper:all-even:proof") == 1u &&
              atom_symbol_occurrences(
                  hopper_all_even_proof, "rel:all:cons") == 5u &&
              atom_symbol_occurrences(
                  hopper_all_even_proof, "rel:all:nil") == 1u &&
              cetta_prime_typed_value_v1_derivation(
                  hopper_all_even_ingress.occurrences[0].value,
                  &hopper_all_even_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &hopper_all_even_derivation,
                  "typed:boundary-check") == 0u &&
              derivation_rule_count(
                  &arena, &universe, &hopper_all_even_derivation,
                  "typed:rule-machine-ingress") == 1u,
          "the source-grounded Hopper all-even proof is reconstructed through Prime's native rel:all calculus");

    const char *hopper_all_odd_goal_text =
        "(hopper:all-even:f "
        "  (list:cons hopper:nat hopper:nat:n2 "
        "    (list:cons hopper:nat hopper:nat:n4 "
        "      (list:cons hopper:nat hopper:nat:n3 "
        "        (list:cons hopper:nat hopper:nat:n8 "
        "          (list:cons hopper:nat hopper:nat:n10 "
        "            (list:nil hopper:nat)))))))";
    CettaPrimeTypedValueV1 *hopper_all_odd_goal = import_native(
        &arena, &space, hopper_all_odd_goal_text);
    Atom *hopper_all_odd_run = run_rule_machine_definition(
        &arena,
        "lib/ilp/hopper_table1_first_order_rules.metta",
        "hopper:table1:first-order:package",
        "(quote "
        "  (hopper:all-even:f "
        "    (list:cons hopper:nat hopper:nat:n2 "
        "      (list:cons hopper:nat hopper:nat:n4 "
        "        (list:cons hopper:nat hopper:nat:n3 "
        "          (list:cons hopper:nat hopper:nat:n8 "
        "            (list:cons hopper:nat hopper:nat:n10 "
        "              (list:nil hopper:nat))))))))",
        512, 10000000, 4096);
    CettaPrimeRuleMachineIngressResultV1 hopper_all_odd_ingress;
    bool hopper_all_odd_observed = hopper_all_odd_goal &&
        hopper_all_odd_run &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, hopper_all_odd_run,
            hopper_all_odd_goal, false, 0u,
            &hopper_all_odd_ingress);
    CHECK(hopper_all_odd_observed &&
              hopper_all_odd_ingress.completion ==
                  CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 &&
              hopper_all_odd_ingress.occurrence_count == 0u &&
              !hopper_all_odd_ingress.occurrences,
          "the source-grounded Hopper all-even negative completes with an empty proof bag, not a fabricated refutation");

    const char *hopper_length_goal_text =
        "(hopper:length:f "
        "  (list:cons hopper:atom hopper:atom:n3 "
        "    (list:cons hopper:atom hopper:atom:n3 "
        "      (list:cons hopper:atom hopper:atom:n1 "
        "        (list:nil hopper:atom)))) "
        "  hopper:nat:n3)";
    CettaPrimeTypedValueV1 *hopper_length_goal = import_native(
        &arena, &space, hopper_length_goal_text);
    Atom *hopper_length_run = run_rule_machine_definition(
        &arena,
        "lib/ilp/hopper_table1_first_order_rules.metta",
        "hopper:table1:first-order:package",
        "(quote "
        "  (hopper:length:f "
        "    (list:cons hopper:atom hopper:atom:n3 "
        "      (list:cons hopper:atom hopper:atom:n3 "
        "        (list:cons hopper:atom hopper:atom:n1 "
        "          (list:nil hopper:atom)))) "
        "    hopper:nat:n3))",
        512, 10000000, 4096);
    CettaPrimeRuleMachineIngressResultV1 hopper_length_ingress;
    bool hopper_length_observed = hopper_length_goal &&
        hopper_length_run &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, hopper_length_run,
            hopper_length_goal, false, 0u,
            &hopper_length_ingress);
    Atom *hopper_length_proof = hopper_length_observed &&
            hopper_length_ingress.occurrence_count == 1u &&
            hopper_length_ingress.occurrences[0].value
        ? erase_term(
              &arena, &universe,
              hopper_length_ingress.occurrences[0].value)
        : NULL;
    CettaPrimeTypedDerivationViewV1 hopper_length_derivation = {0};
    CHECK(hopper_length_observed &&
              hopper_length_ingress.completion ==
                  CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 &&
              hopper_length_ingress.occurrence_count == 1u &&
              hopper_length_ingress.occurrences &&
              hopper_length_ingress.occurrences[0].mode ==
                  CETTA_PRIME_RULE_MACHINE_INGRESS_NATIVE_CONSTRUCTION_V1 &&
              hopper_length_ingress.occurrences[0].value &&
              hopper_length_proof &&
              atom_symbol_occurrences(
                  hopper_length_proof, "hopper:length:proof") == 1u &&
              atom_symbol_occurrences(
                  hopper_length_proof, "rel:fold:cons") == 3u &&
              atom_symbol_occurrences(
                  hopper_length_proof, "rel:fold:nil") == 1u &&
              cetta_prime_typed_value_v1_derivation(
                  hopper_length_ingress.occurrences[0].value,
                  &hopper_length_derivation) &&
              derivation_rule_count(
                  &arena, &universe, &hopper_length_derivation,
                  "typed:boundary-check") == 0u &&
              derivation_rule_count(
                  &arena, &universe, &hopper_length_derivation,
                  "typed:rule-machine-ingress") == 1u,
          "the source-grounded Hopper length proof is reconstructed through Prime's native rel:fold calculus");

    /* A generic relational producer may construct a complete proof term as
     * data.  It crosses Prime's existing check-once boundary without granting
     * the RuleMachine, its parser, or its revision token typing authority. */
    add_file(
        &arena, &space,
        "lib/ilp/popper_synthesis_length_types.metta");
    add_file(
        &arena, &space,
        "lib/ilp/popper_synthesis_length_rules.metta");
    CettaPrimeTypedValueV1 *popper_length_goal = import_native(
        &arena, &space,
        "(target:length "
        "  (list:cons element e3 "
        "    (list:cons element e3 "
        "      (list:cons element e1 (list:nil element)))) "
        "  n3)");
    Atom *popper_run_result = run_rule_machine_definition(
        &arena,
        "lib/ilp/popper_synthesis_length_rules.metta",
        "popper:length:package",
        "(quote "
        "  (target:length "
        "    (list:cons element e3 "
        "      (list:cons element e3 "
        "        (list:cons element e1 (list:nil element)))) "
        "    n3))",
        24, 1000000, 100);
    CHECK(
        popper_run_result && popper_run_result->kind == ATOM_EXPR &&
            popper_run_result->expr.len == 5u &&
            atom_is_symbol(
                popper_run_result->expr.elems[0], "compile-result"),
        "generic RuleMachine produces one complete recursive Popper proof occurrence");

    CettaPrimeRuleMachineIngressResultV1 popper_ingress;
    bool popper_ingress_observed =
        popper_length_goal && popper_run_result &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, popper_run_result,
            popper_length_goal, false, 0u, &popper_ingress);
    if (!popper_ingress_observed) {
        fputs("  Popper ingress failed structurally; run-result=", stderr);
        if (popper_run_result)
            atom_print(popper_run_result, stderr);
        else
            fputs("<missing>", stderr);
        fputc('\n', stderr);
    } else if (popper_ingress.occurrence_count != 1u ||
               !popper_ingress.occurrences ||
               !popper_ingress.occurrences[0].value) {
        fprintf(
            stderr,
            "  Popper ingress completion=%d occurrences=%zu value=%s",
            (int)popper_ingress.completion,
            popper_ingress.occurrence_count,
            popper_ingress.occurrences &&
                    popper_ingress.occurrences[0].value
                ? "yes" : "no");
        if (popper_ingress.occurrences) {
            fprintf(
                stderr, " result-kind=%d outcome=%d route=%d",
                (int)popper_ingress.occurrences[0]
                    .checking.authority.result.kind,
                popper_ingress.occurrences[0]
                        .checking.authority.result.kind ==
                        CETTA_NIK_RESULT_OUTCOME
                    ? (int)popper_ingress.occurrences[0]
                          .checking.authority.result.value.outcome
                    : -1,
                (int)popper_ingress.occurrences[0]
                    .checking.authority.route);
            if (popper_ingress.occurrences[0].checking.authority.payload) {
                fputs(" payload=", stderr);
                atom_print(
                    popper_ingress.occurrences[0]
                        .checking.authority.payload,
                    stderr);
            }
            if (popper_ingress.occurrences[0].elaborated_term) {
                fputs(" term=", stderr);
                atom_print(
                    popper_ingress.occurrences[0].elaborated_term,
                    stderr);
            }
        }
        fputc('\n', stderr);
    }
    CHECK(
        popper_ingress_observed &&
            popper_ingress.completion ==
                CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 &&
            popper_ingress.occurrence_count == 1u &&
            popper_ingress.occurrences &&
            popper_ingress.occurrences[0].value &&
            popper_ingress.occurrences[0].checking.authority.result.kind ==
                CETTA_NIK_RESULT_OUTCOME &&
            popper_ingress.occurrences[0].checking.authority.result.value.outcome ==
                CETTA_NIK_OUTCOME_ESTABLISHED,
        "complete recursive proof data enters Prime through the existing Established boundary");

    Atom *expected_popper_proof = parse_one(
        &arena,
        "(length-proof:step "
        "  (list:cons element e3 "
        "    (list:cons element e3 "
        "      (list:cons element e1 (list:nil element)))) "
        "  (list:cons element e3 "
        "    (list:cons element e1 (list:nil element))) "
        "  n2 n3 "
        "  (length-proof:tail e3 "
        "    (list:cons element e3 "
        "      (list:cons element e1 (list:nil element)))) "
        "  (length-proof:step "
        "    (list:cons element e3 "
        "      (list:cons element e1 (list:nil element))) "
        "    (list:cons element e1 (list:nil element)) "
        "    n1 n2 "
        "    (length-proof:tail e3 "
        "      (list:cons element e1 (list:nil element))) "
        "    (length-proof:step "
        "      (list:cons element e1 (list:nil element)) "
        "      (list:nil element) n0 n1 "
        "      (length-proof:tail e1 (list:nil element)) "
        "      (length-proof:base "
        "        (list:nil element) n0 "
        "        length-proof:zero length-proof:empty) "
        "      length-proof:succ-0-1) "
        "    length-proof:succ-1-2) "
        "  length-proof:succ-2-3)");
    Atom *imported_popper_proof = popper_ingress_observed &&
            popper_ingress.occurrences[0].value
        ? erase_term(
              &arena, &universe,
              popper_ingress.occurrences[0].value)
        : NULL;
    Atom *imported_popper_goal = popper_ingress_observed &&
            popper_ingress.occurrences[0].value
        ? erase_type(
              &arena, &universe,
              popper_ingress.occurrences[0].value)
        : NULL;
    Atom *expected_popper_goal = parse_one(
        &arena,
        "(target:length "
        "  (list:cons element e3 "
        "    (list:cons element e3 "
        "      (list:cons element e1 (list:nil element)))) "
        "  n3)");
    Atom *expected_popper_goal_intrinsic = popper_length_goal
        ? erase_term(&arena, &universe, popper_length_goal)
        : NULL;
    CHECK(
        expected_popper_proof &&
            popper_ingress_observed &&
            popper_ingress.occurrences[0].elaborated_term &&
            atom_eq(
                popper_ingress.occurrences[0].elaborated_term,
                expected_popper_proof) &&
            imported_popper_proof &&
            popper_ingress.occurrences[0].checking.authority.canonical_term &&
            atom_eq(
                imported_popper_proof,
                popper_ingress.occurrences[0]
                    .checking.authority.canonical_term) &&
            expected_popper_goal && imported_popper_goal &&
            expected_popper_goal_intrinsic &&
            atom_eq(imported_popper_goal, expected_popper_goal_intrinsic),
        "staging elaborates the occurrence into the exact ordinary Prime constructor proof and index");

    CettaPrimeTypedDerivationViewV1 popper_derivation = {0};
    bool popper_derivation_ok = popper_ingress_observed &&
        popper_ingress.occurrences[0].value &&
        cetta_prime_typed_value_v1_derivation(
            popper_ingress.occurrences[0].value,
            &popper_derivation);
    AtomId popper_revision_id = popper_ingress_observed
        ? term_universe_store_atom_id(
              &universe, &arena, popper_ingress.producer_revision)
        : CETTA_ATOM_ID_NONE;
    AtomId popper_metrics_id = popper_ingress_observed
        ? term_universe_store_atom_id(
              &universe, &arena, popper_ingress.metrics)
        : CETTA_ATOM_ID_NONE;
    AtomId popper_encoded_proof_id = popper_ingress_observed
        ? term_universe_store_atom_id(
              &universe, &arena,
              popper_ingress.occurrences[0].encoded_proof)
        : CETTA_ATOM_ID_NONE;
    CHECK(
        popper_derivation_ok &&
            derivation_rule_count(
                &arena, &universe, &popper_derivation,
                "typed:boundary-check") == 1u &&
            derivation_rule_count(
                &arena, &universe, &popper_derivation,
                "typed:rule-machine-ingress") == 1u &&
            derivation_has_witness(
                &popper_derivation, popper_revision_id) &&
            derivation_has_witness(
                &popper_derivation, popper_metrics_id) &&
            derivation_has_witness(
                &popper_derivation, popper_encoded_proof_id),
        "typed ingress retains the producer revision, run metrics, encoded proof, and checked boundary receipt");

    /* Equal endpoint types do not collapse occurrence identity or authored
     * order at the raw boundary. */
    CettaPrimeTypedValueV1 *alice_bob_goal = import_native(
        &arena, &space, "(app (app edge alice) bob)");
    Atom *two_occurrences = parse_one(
        &arena,
        "(compile-result proof-occurrence-bag "
        "  (occurrences "
        "    (occurrence (quote alice-bob-a)) "
        "    (occurrence (quote alice-bob-b))) "
        "  (run-metrics 2 2 2 2 1) two-proof-revision)");
    CettaPrimeRuleMachineIngressResultV1 two_ingress;
    bool two_ingress_observed = alice_bob_goal && two_occurrences &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, two_occurrences, alice_bob_goal,
            false, 0u, &two_ingress);
    CettaPrimeTypedValueMetadataV1 first_occurrence_metadata = {0};
    CettaPrimeTypedValueMetadataV1 second_occurrence_metadata = {0};
    Atom *first_occurrence_term = two_ingress_observed &&
            two_ingress.occurrence_count == 2u &&
            two_ingress.occurrences[0].value
        ? erase_term(
              &arena, &universe, two_ingress.occurrences[0].value)
        : NULL;
    Atom *second_occurrence_term = two_ingress_observed &&
            two_ingress.occurrence_count == 2u &&
            two_ingress.occurrences[1].value
        ? erase_term(
              &arena, &universe, two_ingress.occurrences[1].value)
        : NULL;
    CHECK(
        two_ingress_observed && two_ingress.occurrence_count == 2u &&
            two_ingress.occurrences[0].value &&
            two_ingress.occurrences[1].value &&
            first_occurrence_term &&
            atom_is_symbol(first_occurrence_term, "alice-bob-a") &&
            second_occurrence_term &&
            atom_is_symbol(second_occurrence_term, "alice-bob-b") &&
            cetta_prime_typed_value_v1_metadata(
                two_ingress.occurrences[0].value,
                &first_occurrence_metadata) &&
            cetta_prime_typed_value_v1_metadata(
                two_ingress.occurrences[1].value,
                &second_occurrence_metadata) &&
            first_occurrence_metadata.occurrence_identity !=
                second_occurrence_metadata.occurrence_identity,
        "typed occurrence ingress preserves ordered multiplicity and distinct proof identities");

    Atom *incomplete_occurrence_bag = parse_one(
        &arena,
        "(compile-incomplete state-limit proof-occurrence-bag "
        "  (occurrences (occurrence (quote alice-bob-a))) "
        "  (run-metrics 1 1 1 1 1) incomplete-revision)");
    CettaPrimeRuleMachineIngressResultV1 incomplete_ingress;
    bool incomplete_ingress_observed =
        incomplete_occurrence_bag && alice_bob_goal &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, incomplete_occurrence_bag,
            alice_bob_goal, false, 0u, &incomplete_ingress);
    CHECK(
        incomplete_ingress_observed &&
            incomplete_ingress.completion ==
                CETTA_PRIME_RULE_MACHINE_RUN_INCOMPLETE_V1 &&
            incomplete_ingress.incomplete_reason &&
            atom_is_symbol(
                incomplete_ingress.incomplete_reason, "state-limit") &&
            incomplete_ingress.occurrence_count == 1u &&
            incomplete_ingress.occurrences &&
            incomplete_ingress.occurrences[0].encoded_proof &&
            !incomplete_ingress.occurrences[0].elaborated_term &&
            !incomplete_ingress.occurrences[0].value,
        "an incomplete producer run retains its frontier but publishes no typed result");

    Atom *mismatched_occurrence = parse_one(
        &arena,
        "(compile-result proof-occurrence-bag "
        "  (occurrences (occurrence (quote bob-carol))) "
        "  (run-metrics 1 1 1 1 1) mismatch-revision)");
    CettaPrimeRuleMachineIngressResultV1 mismatch_ingress;
    bool mismatch_ingress_observed = mismatched_occurrence && alice_bob_goal &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, mismatched_occurrence, alice_bob_goal,
            false, 0u, &mismatch_ingress);
    CHECK(
        mismatch_ingress_observed &&
            mismatch_ingress.occurrence_count == 1u &&
            !mismatch_ingress.occurrences[0].value &&
            mismatch_ingress.occurrences[0].checking.authority.result.kind ==
                CETTA_NIK_RESULT_OUTCOME &&
            mismatch_ingress.occurrences[0].checking.authority.result.value.outcome ==
                CETTA_NIK_OUTCOME_REFUTED,
        "a checked type mismatch remains Refuted rather than a producer fault or admitted value");

    Atom *malformed_staging = parse_one(
        &arena,
        "(compile-result proof-occurrence-bag "
        "  (occurrences "
        "    (occurrence (quote (unquote alice-bob-a)))) "
        "  (run-metrics 1 1 1 1 1) malformed-staging-revision)");
    CettaPrimeRuleMachineIngressResultV1 malformed_ingress;
    CHECK(
        malformed_staging && alice_bob_goal &&
            !cetta_prime_rule_machine_import_run_v1(
                &arena, &space, malformed_staging, alice_bob_goal,
                false, 0u, &malformed_ingress),
        "only explicit unquote-of-quote staging redexes cross the boundary");

    /* A second corpus task exercises two mutually nested structural
     * recursions: reverse calls append-last, and both emit ordinary Prime
     * constructor proofs.  This keeps the ingress generic rather than
     * accidentally specialized to synthesis-length's proof shape. */
    add_file(
        &arena, &space,
        "lib/ilp/popper_native_list_relations.metta");
    add_file(
        &arena, &space,
        "lib/ilp/popper_synthesis_reverse_types.metta");
    add_file(
        &arena, &space,
        "lib/ilp/popper_synthesis_reverse_rules.metta");
    const char *popper_reverse_goal_text =
        "(reverse:target "
        "  (list:cons reverse:element reverse:a "
        "    (list:cons reverse:element reverse:b "
        "      (list:cons reverse:element reverse:c "
        "        (list:nil reverse:element)))) "
        "  (list:cons reverse:element reverse:c "
        "    (list:cons reverse:element reverse:b "
        "      (list:cons reverse:element reverse:a "
        "        (list:nil reverse:element)))))";
    CettaPrimeTypedValueV1 *popper_reverse_goal = import_native(
        &arena, &space, popper_reverse_goal_text);
    Atom *popper_reverse_run = run_rule_machine_definition(
        &arena,
        "lib/ilp/popper_synthesis_reverse_rules.metta",
        "popper:reverse:package",
        "(quote "
        "  (reverse:target "
        "    (list:cons reverse:element reverse:a "
        "      (list:cons reverse:element reverse:b "
        "        (list:cons reverse:element reverse:c "
        "          (list:nil reverse:element)))) "
        "    (list:cons reverse:element reverse:c "
        "      (list:cons reverse:element reverse:b "
        "        (list:cons reverse:element reverse:a "
        "          (list:nil reverse:element))))))",
        64, 1000000, 100);
    CettaPrimeRuleMachineIngressResultV1 popper_reverse_ingress;
    bool popper_reverse_observed =
        popper_reverse_goal && popper_reverse_run &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, popper_reverse_run,
            popper_reverse_goal, false, 0u,
            &popper_reverse_ingress);
    Atom *popper_reverse_term =
        popper_reverse_observed &&
            popper_reverse_ingress.occurrence_count == 1u &&
            popper_reverse_ingress.occurrences &&
            popper_reverse_ingress.occurrences[0].value
        ? erase_term(
              &arena, &universe,
              popper_reverse_ingress.occurrences[0].value)
        : NULL;
    Atom *popper_reverse_type =
        popper_reverse_observed &&
            popper_reverse_ingress.occurrence_count == 1u &&
            popper_reverse_ingress.occurrences &&
            popper_reverse_ingress.occurrences[0].value
        ? erase_type(
              &arena, &universe,
              popper_reverse_ingress.occurrences[0].value)
        : NULL;
    Atom *expected_popper_reverse_type = popper_reverse_goal
        ? erase_term(&arena, &universe, popper_reverse_goal)
        : NULL;
    if (!popper_reverse_observed ||
        popper_reverse_ingress.occurrence_count != 1u ||
        !popper_reverse_ingress.occurrences ||
        !popper_reverse_ingress.occurrences[0].value ||
        !popper_reverse_type || !expected_popper_reverse_type ||
        !atom_eq(popper_reverse_type, expected_popper_reverse_type)) {
        fprintf(
            stderr,
            "  reverse ingress observed=%s completion=%d occurrences=%zu "
            "value=%s result-kind=%d outcome=%d route=%d\n",
            popper_reverse_observed ? "yes" : "no",
            popper_reverse_observed
                ? (int)popper_reverse_ingress.completion
                : -1,
            popper_reverse_observed
                ? popper_reverse_ingress.occurrence_count
                : 0u,
            popper_reverse_observed &&
                    popper_reverse_ingress.occurrences &&
                    popper_reverse_ingress.occurrences[0].value
                ? "yes" : "no",
            popper_reverse_observed &&
                    popper_reverse_ingress.occurrences
                ? (int)popper_reverse_ingress.occurrences[0]
                      .checking.authority.result.kind
                : -1,
            popper_reverse_observed &&
                    popper_reverse_ingress.occurrences &&
                    popper_reverse_ingress.occurrences[0]
                            .checking.authority.result.kind ==
                        CETTA_NIK_RESULT_OUTCOME
                ? (int)popper_reverse_ingress.occurrences[0]
                      .checking.authority.result.value.outcome
                : -1,
            popper_reverse_observed &&
                    popper_reverse_ingress.occurrences
                ? (int)popper_reverse_ingress.occurrences[0]
                      .checking.authority.route
                : -1);
        if (popper_reverse_run) {
            fputs("  reverse run=", stderr);
            atom_print(popper_reverse_run, stderr);
            fputc('\n', stderr);
        }
        if (popper_reverse_term) {
            fputs("  reverse term=", stderr);
            atom_print(popper_reverse_term, stderr);
            fputc('\n', stderr);
        }
        if (popper_reverse_type) {
            fputs("  reverse type=", stderr);
            atom_print(popper_reverse_type, stderr);
            fputc('\n', stderr);
        }
        if (expected_popper_reverse_type) {
            fputs("  expected reverse type=", stderr);
            atom_print(expected_popper_reverse_type, stderr);
            fputc('\n', stderr);
        }
    }
    CHECK(
        popper_reverse_observed &&
            popper_reverse_ingress.completion ==
                CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 &&
            popper_reverse_ingress.occurrence_count == 1u &&
            popper_reverse_ingress.occurrences &&
            popper_reverse_ingress.occurrences[0].value &&
            popper_reverse_ingress.occurrences[0]
                    .checking.authority.result.kind ==
                CETTA_NIK_RESULT_OUTCOME &&
            popper_reverse_ingress.occurrences[0]
                    .checking.authority.result.value.outcome ==
                CETTA_NIK_OUTCOME_ESTABLISHED &&
            popper_reverse_term && popper_reverse_type &&
            expected_popper_reverse_type &&
            atom_eq(popper_reverse_type, expected_popper_reverse_type),
        "a complete synthesis-reverse occurrence enters at its exact indexed Prime goal");
    CHECK(
        atom_symbol_occurrences(
            popper_reverse_term, "reverse-proof:step") == 3u &&
            atom_symbol_occurrences(
                popper_reverse_term, "reverse-proof:base") == 1u &&
            atom_symbol_occurrences(
                popper_reverse_term,
                "popper:list:proof:append-last-step") == 3u &&
            atom_symbol_occurrences(
                popper_reverse_term,
                "popper:list:proof:append-last-empty") == 3u &&
            atom_symbol_occurrences(
                popper_reverse_term, "unquote") == 0u &&
            atom_symbol_occurrences(
                popper_reverse_term, "quote") == 0u,
        "typed synthesis-reverse ingress retains both recursive proof trees after explicit staging");

    /* A complete finite-control GDL state contributes two proof occurrences
     * for one indexed target: an authored action establishes p, while p also
     * persists from the source state.  Both cross the same generic boundary;
     * neither the RuleMachine nor this game receives a typing privilege. */
    add_file(
        &arena, &space,
        "lib/ilp/iggp_finite_view_types.metta");
    add_file(
        &arena, &space,
        "lib/ilp/iggp_untwisty_corridor_types.metta");
    const char *corridor_goal_text =
        "(corridor:next corridor:next:train:state-1 corridor:p)";
    CettaPrimeTypedValueV1 *corridor_goal = import_native(
        &arena, &space, corridor_goal_text);
    Atom *corridor_run = run_rule_machine_applied_definition(
        &arena,
        "lib/ilp/iggp_untwisty_corridor_rules.metta",
        "(corridor:package corridor:next:train:state-1)",
        "(quote "
        "  (corridor:next corridor:next:train:state-1 corridor:p))",
        32, 2000000, 256);
    CettaPrimeRuleMachineIngressResultV1 corridor_ingress;
    bool corridor_ingress_observed =
        corridor_goal && corridor_run &&
        cetta_prime_rule_machine_import_run_v1(
            &arena, &space, corridor_run, corridor_goal,
            false, 0u, &corridor_ingress);
    Atom *corridor_terms[2] = {NULL, NULL};
    Atom *corridor_types[2] = {NULL, NULL};
    CettaPrimeTypedValueMetadataV1 corridor_metadata[2] = {{0}, {0}};
    bool corridor_values_current = corridor_ingress_observed &&
        corridor_ingress.completion ==
            CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 &&
        corridor_ingress.occurrence_count == 2u &&
        corridor_ingress.occurrences;
    for (size_t index = 0u; corridor_values_current && index < 2u; index++) {
        CettaPrimeRuleMachineTypedOccurrenceV1 *occurrence =
            &corridor_ingress.occurrences[index];
        corridor_values_current = occurrence->value &&
            occurrence->checking.authority.result.kind ==
                CETTA_NIK_RESULT_OUTCOME &&
            occurrence->checking.authority.result.value.outcome ==
                CETTA_NIK_OUTCOME_ESTABLISHED &&
            native_typing_route(occurrence->checking.authority.route) &&
            cetta_prime_typed_value_v1_is_current(
                occurrence->value, &space) &&
            cetta_prime_typed_value_v1_metadata(
                occurrence->value, &corridor_metadata[index]);
        if (!corridor_values_current) break;
        corridor_terms[index] = erase_term(
            &arena, &universe, occurrence->value);
        corridor_types[index] = erase_type(
            &arena, &universe, occurrence->value);
        corridor_values_current = corridor_terms[index] &&
            corridor_types[index];
    }
    Atom *corridor_goal_intrinsic = corridor_goal
        ? erase_term(&arena, &universe, corridor_goal)
        : NULL;
    CHECK(
        corridor_values_current && corridor_goal_intrinsic &&
            atom_eq(corridor_types[0], corridor_goal_intrinsic) &&
            atom_eq(corridor_types[1], corridor_goal_intrinsic),
        "both complete IGGP proof occurrences enter at the exact indexed Prime goal");
    CHECK(
        corridor_values_current &&
            atom_symbol_occurrences(
                corridor_terms[0],
                "corridor:proof:next-p-from-action") == 1u &&
            atom_symbol_occurrences(
                corridor_terms[0], "corridor:proof:does") == 1u &&
            atom_symbol_occurrences(
                corridor_terms[0], "corridor:g") >= 1u &&
            atom_symbol_occurrences(
                corridor_terms[1],
                "corridor:proof:next-p-persist") == 1u &&
            atom_symbol_occurrences(
                corridor_terms[1],
                "corridor:proof:true-proposition") == 1u &&
            atom_symbol_occurrences(corridor_terms[0], "quote") == 0u &&
            atom_symbol_occurrences(corridor_terms[0], "unquote") == 0u &&
            atom_symbol_occurrences(corridor_terms[1], "quote") == 0u &&
            atom_symbol_occurrences(corridor_terms[1], "unquote") == 0u,
        "typed IGGP ingress preserves authored action-before-persistence order and both proof trees");
    CHECK(
        corridor_values_current &&
            corridor_metadata[0].occurrence_identity !=
                corridor_metadata[1].occurrence_identity,
        "equal IGGP endpoint judgments retain distinct typed occurrence identities");

    space_add(&space, atom_symbol(&arena, "popper-ingress-revision-change"));
    CettaPrimeRuleMachineIngressResultV1 stale_ingress;
    CHECK(
        popper_run_result && popper_length_goal &&
            !cetta_prime_rule_machine_import_run_v1(
                &arena, &space, popper_run_result,
                popper_length_goal, false, 0u, &stale_ingress),
        "a revision change makes the expected typed goal stale before ingress");
    CHECK(
        corridor_values_current &&
            !cetta_prime_typed_value_v1_is_current(
                corridor_ingress.occurrences[0].value, &space) &&
            !cetta_prime_typed_value_v1_is_current(
                corridor_ingress.occurrences[1].value, &space),
        "one space revision makes every imported IGGP proof occurrence stale together");

    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);

    if (failures != 0u) {
        fprintf(stderr,
                "PrimeTypedFlowSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(PrimeTypedFlowSummary checks=%u failures=0)\n", checks);
    return 0;
}
