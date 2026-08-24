#include "parser.h"
#include "prime_rule_machine_ingress.h"
#include "prime_typed_flow_boundary.h"
#include "rule_machine.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool expr_named(Atom *atom, const char *name, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
           atom_is_symbol(atom->expr.elems[0], name);
}

static void print_case_failure(Atom *name, const char *reason) {
    fputs("FAIL: Hopper source case ", stderr);
    if (name && name->kind == ATOM_SYMBOL) {
        const char *bytes = symbol_bytes(g_symbols, name->sym_id);
        size_t length = symbol_len(g_symbols, name->sym_id);
        fwrite(bytes, 1u, length, stderr);
    } else {
        fputs("<malformed-name>", stderr);
    }
    fprintf(stderr, ": %s\n", reason);
}

static bool symbol_contains(Atom *atom, const char *needle) {
    if (!atom || atom->kind != ATOM_SYMBOL || !needle) return false;
    const char *bytes = symbol_bytes(g_symbols, atom->sym_id);
    size_t length = symbol_len(g_symbols, atom->sym_id);
    size_t needle_length = strlen(needle);
    if (!bytes || needle_length > length) return false;
    for (size_t offset = 0u; offset + needle_length <= length; offset++)
        if (memcmp(bytes + offset, needle, needle_length) == 0) return true;
    return false;
}

static bool native_typing_route(CettaPrimeTypingRouteV1 route) {
    return route == CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR;
}

static void add_file(Arena *arena, Space *space, const char *path) {
    Atom **forms = NULL;
    int count = parse_metta_file(path, arena, &forms);
    CHECK(count >= 0, "Hopper typed support file parses");
    for (int index = 0; index < count; index++)
        space_add(space, forms[index]);
    free(forms);
}

static CettaPrimeTypedValueV1 *import_native_goal(
    Arena *arena, Space *space, Atom *goal) {
    CettaPrimeTypingSynthesisObservationV1 observation;
    CettaPrimeTypedValueV1 *value = NULL;
    bool observed = goal && cetta_prime_typed_value_import_term_v1(
        arena, space, goal, false, 0u, &observation, &value);
    return observed &&
            observation.authority.result.kind == CETTA_NIK_RESULT_OUTCOME &&
            observation.authority.result.value.outcome ==
                CETTA_NIK_OUTCOME_ESTABLISHED &&
            native_typing_route(observation.authority.route) && value &&
            cetta_prime_typed_value_v1_is_current(value, space)
        ? value
        : NULL;
}

static Atom *compile_rule_machine_definition(
    Arena *arena, const char *path, const char *definition_name) {
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
    free(forms);
    return artifact;
}

static Atom *run_rule_machine_artifact(
    Arena *arena, Atom *artifact, Atom *quoted_goal,
    int64_t depth, int64_t max_states, int64_t max_occurrences) {
    Atom *run_head = atom_symbol(arena, "compile:run");
    Atom *run_arguments[] = {
        artifact,
        atom_int(arena, depth),
        atom_int(arena, max_states),
        atom_int(arena, max_occurrences),
        quoted_goal,
    };
    return artifact && quoted_goal
        ? cetta_rule_machine_dispatch(
              arena, run_head, run_arguments,
              sizeof(run_arguments) / sizeof(run_arguments[0]))
        : NULL;
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

static Atom *erase_term(
    Arena *arena, const TermUniverse *universe,
    const CettaPrimeTypedValueV1 *value) {
    Atom *term = NULL;
    return value && cetta_prime_typed_value_v1_erase(
                        value, universe, arena, &term, NULL)
        ? term
        : NULL;
}

static bool proper_list_length(Atom *list, size_t *length_out) {
    if (length_out) *length_out = 0u;
    if (!list || !length_out) return false;
    size_t length = 0u;
    Atom *cursor = list;
    while (expr_named(cursor, "list:cons", 4u)) {
        length++;
        cursor = cursor->expr.elems[3];
    }
    if (!expr_named(cursor, "list:nil", 2u)) return false;
    *length_out = length;
    return true;
}

static bool hopper_nat_literal(Atom *atom, size_t *value_out) {
    if (value_out) *value_out = 0u;
    if (!atom || atom->kind != ATOM_SYMBOL || !value_out) return false;
    static const char prefix[] = "hopper:nat:n";
    const char *bytes = symbol_bytes(g_symbols, atom->sym_id);
    size_t length = symbol_len(g_symbols, atom->sym_id);
    size_t prefix_length = sizeof(prefix) - 1u;
    if (!bytes || length <= prefix_length ||
        memcmp(bytes, prefix, prefix_length) != 0) {
        return false;
    }
    size_t value = 0u;
    for (size_t index = prefix_length; index < length; index++) {
        if (bytes[index] < '0' || bytes[index] > '9') return false;
        value = value * 10u + (size_t)(bytes[index] - '0');
    }
    *value_out = value;
    return true;
}

int main(void) {
    Arena arena;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;
    Atom **source_forms = NULL;

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

    add_file(&arena, &space, "lib/ilp/prime_native_list_types.metta");
    add_file(
        &arena, &space,
        "lib/ilp/prime_relational_combinators_types.metta");
    add_file(
        &arena, &space,
        "lib/ilp/hopper_table1_first_order_types.metta");

    Atom *artifact = compile_rule_machine_definition(
        &arena, "lib/ilp/hopper_table1_first_order_rules.metta",
        "hopper:table1:first-order:package");
    CHECK(artifact != NULL,
          "the source-grounded Hopper first-order presentation compiles once");

    int source_form_count = parse_metta_file(
        "examples/prime/hopper_table1_first_order_ground_truth.metta",
        &arena, &source_forms);
    CHECK(source_form_count >= 0,
          "the generated source-grounded Hopper fixture parses");

    size_t selected_cases = 0u;
    size_t positive_cases = 0u;
    size_t negative_cases = 0u;
    size_t native_proofs = 0u;
    size_t fold_cons_nodes = 0u;
    size_t iteration_step_nodes = 0u;

    for (int form_index = 0;
         artifact && form_index < source_form_count; form_index++) {
        Atom *payload = NULL;
        if (!parser_syn_exec_payload(source_forms[form_index], &payload)) {
            if (atom_is_symbol(source_forms[form_index], "!") &&
                form_index + 1 < source_form_count) {
                payload = source_forms[++form_index];
            }
        }
        if (!expr_named(payload, "hopper:classify", 3u)) {
            continue;
        }
        Atom *case_name = payload->expr.elems[1];
        Atom *quoted_goal = payload->expr.elems[2];
        if (!case_name || case_name->kind != ATOM_SYMBOL ||
            !expr_named(quoted_goal, "quote", 2u)) {
            continue;
        }
        Atom *goal = quoted_goal->expr.elems[1];
        bool is_length = goal && goal->kind == ATOM_EXPR &&
            goal->expr.len == 3u &&
            atom_is_symbol(goal->expr.elems[0], "hopper:length:f");
        bool is_reverse = goal && goal->kind == ATOM_EXPR &&
            goal->expr.len == 3u &&
            atom_is_symbol(goal->expr.elems[0], "hopper:reverse:f");
        bool is_dropk = goal && goal->kind == ATOM_EXPR &&
            goal->expr.len == 4u &&
            atom_is_symbol(goal->expr.elems[0], "hopper:dropk:f");
        if (!is_length && !is_reverse && !is_dropk) continue;

        selected_cases++;
        bool expected_positive = symbol_contains(case_name, ":pos-");
        bool expected_negative = symbol_contains(case_name, ":neg-");
        positive_cases += expected_positive ? 1u : 0u;
        negative_cases += expected_negative ? 1u : 0u;
        if (expected_positive == expected_negative) {
            print_case_failure(case_name, "source polarity is not explicit");
            failures++;
            continue;
        }

        CettaPrimeTypedValueV1 *expected_type =
            import_native_goal(&arena, &space, goal);
        if (!expected_type) {
            print_case_failure(
                case_name, "goal did not enter the native typed boundary");
            failures++;
            continue;
        }
        Atom *run_result = run_rule_machine_artifact(
            &arena, artifact, quoted_goal, 512, 10000000, 4096);
        CettaPrimeRuleMachineIngressResultV1 ingress;
        bool imported = run_result &&
            cetta_prime_rule_machine_import_run_v1(
                &arena, &space, run_result, expected_type,
                false, 0u, &ingress);
        if (!imported ||
            ingress.completion !=
                CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1) {
            print_case_failure(
                case_name, "producer result did not complete typed ingress");
            failures++;
            continue;
        }

        if (expected_negative) {
            if (ingress.occurrence_count != 0u || ingress.occurrences) {
                print_case_failure(
                    case_name,
                    "a source-negative case acquired a proof occurrence");
                failures++;
            }
            continue;
        }

        size_t source_length = 0u;
        size_t iteration_count = 0u;
        CettaPrimeTypedDerivationViewV1 derivation = {0};
        CettaPrimeRuleMachineTypedOccurrenceV1 *occurrence =
            ingress.occurrence_count == 1u && ingress.occurrences
                ? &ingress.occurrences[0]
                : NULL;
        Atom *proof = occurrence
            ? erase_term(&arena, &universe, occurrence->value)
            : NULL;
        Atom *source = is_dropk
            ? goal->expr.elems[2]
            : goal->expr.elems[1];
        bool has_source_length = proper_list_length(source, &source_length);
        bool recursive_shape_ok = is_dropk
            ? hopper_nat_literal(goal->expr.elems[1], &iteration_count) &&
              atom_symbol_occurrences(proof, "rel:iterate:step") ==
                  iteration_count &&
              atom_symbol_occurrences(proof, "rel:iterate:zero") == 1u &&
              atom_symbol_occurrences(proof, "hopper:dropk:proof") == 1u
            : atom_symbol_occurrences(proof, "rel:fold:cons") ==
                  source_length &&
              atom_symbol_occurrences(proof, "rel:fold:nil") == 1u &&
              atom_symbol_occurrences(
                  proof,
                  is_length ? "hopper:length:proof" :
                              "hopper:reverse:proof") == 1u;
        bool case_ok = occurrence &&
            occurrence->mode ==
                CETTA_PRIME_RULE_MACHINE_INGRESS_NATIVE_CONSTRUCTION_V1 &&
            occurrence->value &&
            cetta_prime_typed_value_v1_is_current(
                occurrence->value, &space) &&
            has_source_length &&
            proof &&
            recursive_shape_ok &&
            cetta_prime_typed_value_v1_derivation(
                occurrence->value, &derivation) &&
            derivation_rule_count(
                &arena, &universe, &derivation,
                "typed:boundary-check") == 0u &&
            derivation_rule_count(
                &arena, &universe, &derivation,
                "typed:rule-machine-ingress") == 1u;
        if (!case_ok) {
            print_case_failure(
                case_name,
                "proof did not preserve the exact native recursive construction");
            failures++;
            continue;
        }
        native_proofs++;
        if (is_dropk) {
            iteration_step_nodes += iteration_count;
        } else {
            fold_cons_nodes += source_length;
        }
    }

    CHECK(selected_cases == 21u && positive_cases == 11u &&
              negative_cases == 10u,
          "all 21 source-pinned Hopper length/reverse/dropK cases are selected with exact polarity");
    CHECK(native_proofs == positive_cases,
          "every source-positive Hopper recursive case uses native indexed construction");
    CHECK(fold_cons_nodes != 0u,
          "the native source suite exercises nonempty recursive fold paths");
    CHECK(iteration_step_nodes != 0u,
          "the native source suite exercises nonempty generic iteration paths");

    free(source_forms);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);

    if (failures != 0u) {
        fprintf(stderr,
                "PrimeHopperRecursiveNativeSummary cases=%zu positive=%zu "
                "negative=%zu native=%zu checks=%u failures=%u\n",
                selected_cases, positive_cases, negative_cases,
                native_proofs, checks, failures);
        return 1;
    }
    printf(
        "(PrimeHopperRecursiveNativeSummary cases=%zu positive=%zu "
        "negative=%zu native=%zu fold-cons=%zu iterate-step=%zu "
        "checks=%u failures=0)\n",
        selected_cases, positive_cases, negative_cases,
        native_proofs, fold_cons_nodes, iteration_step_nodes, checks);
    return 0;
}
