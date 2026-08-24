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

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
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
    CHECK(count >= 0, "Hopper branching support file parses");
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

static bool native_occurrence(
    Arena *arena, Space *space, const TermUniverse *universe,
    CettaPrimeRuleMachineTypedOccurrenceV1 *occurrence,
    Atom **proof_out, uint64_t *identity_out) {
    if (proof_out) *proof_out = NULL;
    if (identity_out) *identity_out = 0u;
    if (!arena || !space || !universe || !occurrence || !proof_out ||
        !identity_out ||
        occurrence->mode !=
            CETTA_PRIME_RULE_MACHINE_INGRESS_NATIVE_CONSTRUCTION_V1 ||
        !occurrence->value ||
        !cetta_prime_typed_value_v1_is_current(occurrence->value, space)) {
        return false;
    }
    CettaPrimeTypedDerivationViewV1 derivation = {0};
    CettaPrimeTypedValueMetadataV1 metadata = {0};
    Atom *proof = erase_term(arena, universe, occurrence->value);
    if (!proof ||
        !cetta_prime_typed_value_v1_derivation(
            occurrence->value, &derivation) ||
        !cetta_prime_typed_value_v1_metadata(
            occurrence->value, &metadata) ||
        derivation_rule_count(
            arena, universe, &derivation, "typed:boundary-check") != 0u ||
        derivation_rule_count(
            arena, universe, &derivation,
            "typed:rule-machine-ingress") != 1u) {
        return false;
    }
    *proof_out = proof;
    *identity_out = metadata.occurrence_identity;
    return true;
}

static size_t list_target_positions(
    Atom *list, Atom *target, size_t *positions, size_t capacity,
    bool *proper_out) {
    if (proper_out) *proper_out = false;
    if (!list || !target || !positions || capacity == 0u || !proper_out)
        return 0u;
    size_t position = 0u;
    size_t count = 0u;
    Atom *cursor = list;
    while (expr_named(cursor, "list:cons", 4u)) {
        if (atom_eq(cursor->expr.elems[2], target)) {
            if (count >= capacity) return 0u;
            positions[count++] = position;
        }
        position++;
        cursor = cursor->expr.elems[3];
    }
    *proper_out = expr_named(cursor, "list:nil", 2u);
    return count;
}

static bool find_duplicate_shape(
    Atom *list, Atom *answer,
    size_t *case_cons_out, size_t *member_there_out) {
    if (case_cons_out) *case_cons_out = 0u;
    if (member_there_out) *member_there_out = 0u;
    if (!list || !answer || !case_cons_out || !member_there_out)
        return false;
    size_t outer = 0u;
    for (Atom *cursor = list;
         expr_named(cursor, "list:cons", 4u);
         cursor = cursor->expr.elems[3], outer++) {
        if (!atom_eq(cursor->expr.elems[2], answer)) continue;
        size_t inner = 0u;
        for (Atom *tail = cursor->expr.elems[3];
             expr_named(tail, "list:cons", 4u);
             tail = tail->expr.elems[3], inner++) {
            if (atom_eq(tail->expr.elems[2], answer)) {
                *case_cons_out = outer + 1u;
                *member_there_out = inner;
                return true;
            }
        }
    }
    return false;
}

static bool malformed_occurrence_refuted(
    Arena *arena, Space *space,
    const char *goal_text, const char *run_text) {
    Atom *goal = parse_one(arena, goal_text);
    Atom *run = parse_one(arena, run_text);
    CettaPrimeTypedValueV1 *expected = import_native_goal(
        arena, space, goal);
    CettaPrimeRuleMachineIngressResultV1 ingress;
    bool observed = expected && run &&
        cetta_prime_rule_machine_import_run_v1(
            arena, space, run, expected, false, 0u, &ingress);
    return observed && ingress.occurrence_count == 1u &&
        ingress.occurrences &&
        ingress.occurrences[0].mode ==
            CETTA_PRIME_RULE_MACHINE_INGRESS_NONE_V1 &&
        !ingress.occurrences[0].value &&
        ingress.occurrences[0].checking.authority.result.kind ==
            CETTA_NIK_RESULT_OUTCOME &&
        ingress.occurrences[0].checking.authority.result.value.outcome ==
            CETTA_NIK_OUTCOME_REFUTED;
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

    size_t source_cases = 0u;
    size_t member_cases = 0u;
    size_t finddup_cases = 0u;
    size_t positive_cases = 0u;
    size_t negative_cases = 0u;
    size_t native_source_proofs = 0u;
    size_t any_there_nodes = 0u;
    size_t case_cons_nodes = 0u;
    size_t member_there_nodes = 0u;
    size_t canary_occurrences = 0u;
    size_t canary_native = 0u;
    bool canary_seen = false;

    for (int form_index = 0;
         artifact && form_index < source_form_count; form_index++) {
        Atom *payload = NULL;
        if (!parser_syn_exec_payload(source_forms[form_index], &payload)) {
            if (atom_is_symbol(source_forms[form_index], "!") &&
                form_index + 1 < source_form_count) {
                payload = source_forms[++form_index];
            }
        }
        if (!expr_named(payload, "hopper:classify", 3u)) continue;
        Atom *case_name = payload->expr.elems[1];
        Atom *quoted_goal = payload->expr.elems[2];
        if (!case_name || case_name->kind != ATOM_SYMBOL ||
            !expr_named(quoted_goal, "quote", 2u)) {
            continue;
        }
        Atom *goal = quoted_goal->expr.elems[1];
        bool is_member = expr_named(goal, "hopper:member:f", 3u);
        bool is_finddup = expr_named(goal, "hopper:find-dup:f", 3u);
        if (!is_member && !is_finddup) continue;

        bool is_canary = symbol_contains(case_name, "multiplicity-canary");
        bool expected_positive = symbol_contains(case_name, ":pos-");
        bool expected_negative = symbol_contains(case_name, ":neg-");
        if (!is_canary) {
            source_cases++;
            member_cases += is_member ? 1u : 0u;
            finddup_cases += is_finddup ? 1u : 0u;
            positive_cases += expected_positive ? 1u : 0u;
            negative_cases += expected_negative ? 1u : 0u;
            if (expected_positive == expected_negative) {
                print_case_failure(
                    case_name, "source polarity is not explicit");
                failures++;
                continue;
            }
        } else {
            canary_seen = true;
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

        if (is_member) {
            size_t positions[32] = {0};
            bool proper_list = false;
            size_t expected_occurrences = list_target_positions(
                goal->expr.elems[1], goal->expr.elems[2],
                positions, sizeof(positions) / sizeof(positions[0]),
                &proper_list);
            if (!proper_list || ingress.occurrence_count != expected_occurrences ||
                (expected_occurrences != 0u && !ingress.occurrences)) {
                print_case_failure(
                    case_name,
                    "member proof-bag multiplicity does not match list occurrences");
                failures++;
                continue;
            }
            uint64_t identities[32] = {0};
            bool case_ok = true;
            for (size_t index = 0u; index < ingress.occurrence_count; index++) {
                Atom *proof = NULL;
                if (!native_occurrence(
                        &arena, &space, &universe,
                        &ingress.occurrences[index],
                        &proof, &identities[index]) ||
                    atom_symbol_occurrences(
                        proof, "hopper:member:proof") != 1u ||
                    atom_symbol_occurrences(proof, "rel:any:here") != 1u ||
                    atom_symbol_occurrences(proof, "rel:any:there") !=
                        positions[index]) {
                    case_ok = false;
                    break;
                }
                for (size_t earlier = 0u; earlier < index; earlier++)
                    if (identities[earlier] == identities[index])
                        case_ok = false;
                any_there_nodes += positions[index];
            }
            if (!case_ok) {
                print_case_failure(
                    case_name,
                    "ordered member paths were not preserved by native rel:any construction");
                failures++;
                continue;
            }
            if (is_canary) {
                canary_occurrences = ingress.occurrence_count;
                canary_native = ingress.occurrence_count;
            } else {
                native_source_proofs += ingress.occurrence_count;
            }
            continue;
        }

        size_t expected_case_cons = 0u;
        size_t expected_member_there = 0u;
        bool has_duplicate = find_duplicate_shape(
            goal->expr.elems[1], goal->expr.elems[2],
            &expected_case_cons, &expected_member_there);
        if (!has_duplicate || ingress.occurrence_count != 1u ||
            !ingress.occurrences) {
            print_case_failure(
                case_name,
                "find-dup proof bag does not match the authored duplicate witness");
            failures++;
            continue;
        }
        Atom *proof = NULL;
        uint64_t identity = 0u;
        bool case_ok = native_occurrence(
                &arena, &space, &universe, &ingress.occurrences[0],
                &proof, &identity) && identity != 0u &&
            atom_symbol_occurrences(proof, "hopper:find-dup:proof") == 1u &&
            atom_symbol_occurrences(proof, "rel:case-list:cons") ==
                expected_case_cons &&
            atom_symbol_occurrences(proof, "rel:case-list:nil") == 0u &&
            atom_symbol_occurrences(proof, "rel:list:member-here") == 1u &&
            atom_symbol_occurrences(proof, "rel:list:member-there") ==
                expected_member_there;
        if (!case_ok) {
            print_case_failure(
                case_name,
                "find-dup did not retain its exact native case/member proof tree");
            failures++;
            continue;
        }
        native_source_proofs++;
        case_cons_nodes += expected_case_cons;
        member_there_nodes += expected_member_there;
    }

    CHECK(source_cases == 12u && member_cases == 7u && finddup_cases == 5u &&
              positive_cases == 7u && negative_cases == 5u,
          "all source-pinned Hopper member/find-dup cases are selected with exact polarity");
    CHECK(native_source_proofs == positive_cases,
          "every source-positive member/find-dup case uses native indexed-family construction");
    CHECK(canary_seen && canary_occurrences == 3u && canary_native == 3u,
          "the repeated-member canary retains three ordered native proof occurrences");
    CHECK(any_there_nodes != 0u && case_cons_nodes != 0u &&
              member_there_nodes != 0u,
          "the source suite exercises recursive any, case-list, and member paths");

    CHECK(
        malformed_occurrence_refuted(
            &arena, &space,
            "(rel:list:member hopper:atom "
            "  (list:cons hopper:atom hopper:atom:n7 "
            "    (list:nil hopper:atom)) hopper:atom:n7)",
            "(compile-result proof-occurrence-bag "
            "  (occurrences (occurrence (quote "
            "    (rel:list:member-there hopper:atom hopper:atom:n7 "
            "      (list:nil hopper:atom) hopper:atom:n7 "
            "      hopper:nat:proof:zero)))) "
            "  (run-metrics 1 1 1 1 1) malformed-member-revision)"),
        "ill-indexed member evidence receives no native value and is Refuted by the real fallback checker");
    CHECK(
        malformed_occurrence_refuted(
            &arena, &space,
            "(rel:any hopper:atom hopper:atom hopper:atom-list:head "
            "  (list:cons hopper:atom hopper:atom:n7 "
            "    (list:nil hopper:atom)) hopper:atom:n7)",
            "(compile-result proof-occurrence-bag "
            "  (occurrences (occurrence (quote "
            "    (rel:any:here hopper:atom hopper:atom "
            "      hopper:atom-list:head "
            "      (list:cons hopper:atom hopper:atom:n7 "
            "        (list:nil hopper:atom)) hopper:atom:n7 "
            "      hopper:nat:proof:zero)))) "
            "  (run-metrics 1 1 1 1 1) malformed-any-revision)"),
        "ill-indexed any evidence receives no native value and is Refuted by the real fallback checker");
    CHECK(
        malformed_occurrence_refuted(
            &arena, &space,
            "(rel:case-list hopper:atom hopper:atom "
            "  hopper:find-dup:nil-case hopper:find-dup:cons-case "
            "  (list:cons hopper:atom hopper:atom:n7 "
            "    (list:nil hopper:atom)) hopper:atom:n7)",
            "(compile-result proof-occurrence-bag "
            "  (occurrences (occurrence (quote "
            "    (rel:case-list:cons hopper:atom hopper:atom "
            "      hopper:find-dup:nil-case hopper:find-dup:cons-case "
            "      hopper:atom:n7 (list:nil hopper:atom) "
            "      hopper:atom:n7 hopper:nat:proof:zero)))) "
            "  (run-metrics 1 1 1 1 1) malformed-case-revision)"),
        "ill-indexed case-list evidence receives no native value and is Refuted by the real fallback checker");

    free(source_forms);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);

    if (failures != 0u) {
        fprintf(
            stderr,
            "PrimeHopperBranchingNativeSummary cases=%zu positive=%zu "
            "negative=%zu native=%zu canary=%zu checks=%u failures=%u\n",
            source_cases, positive_cases, negative_cases,
            native_source_proofs, canary_occurrences, checks, failures);
        return 1;
    }
    printf(
        "(PrimeHopperBranchingNativeSummary cases=%zu positive=%zu "
        "negative=%zu native=%zu canary=%zu any-there=%zu "
        "case-cons=%zu member-there=%zu checks=%u failures=0)\n",
        source_cases, positive_cases, negative_cases,
        native_source_proofs, canary_occurrences, any_there_nodes,
        case_cons_nodes, member_there_nodes, checks);
    return 0;
}
