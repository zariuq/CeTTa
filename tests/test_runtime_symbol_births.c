#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "durable_value.h"
#include "lib_parse_native_grammar.h"
#include "gslt_rigid_coordinate_dispatch_v1.h"
#include "gslt_finite_fact_provider_v1.h"
#include "parser.h"
#include "petta_program.h"
#include "space.h"
#include "stats.h"

static Atom *parse_one(Arena *arena, const char *text, bool data) {
    size_t position = 0u;
    return data ? parse_data_sexpr_n(arena, text, strlen(text), &position)
                : parse_sexpr_n(arena, text, strlen(text), &position);
}

static bool count_symbol(SymbolId id, void *context) {
    assert(id != SYMBOL_ID_NONE);
    (*(unsigned *)context)++;
    return true;
}

static bool mark_symbol(SymbolId id, void *context) {
    return symbol_shadow_mark(context, id);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    SymbolId early = symbol_intern_cstr(&symbols, "ordinary-before-builtins");
    /* Even an already interned builtin spelling cannot turn the surrounding
     * user-name interval into builtin authority. */
    symbol_intern_cstr(&symbols, "Empty");
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    /* Appending a builtin must not make it a constructor or an unknown call.
     * Its registration is the authority, independently of the legacy range. */
#define CHECK_BUILTIN(field, text) assert(symbol_id_is_builtin(g_builtin_syms.field));
    CETTA_BUILTIN_SYMBOLS(CHECK_BUILTIN)
#undef CHECK_BUILTIN
#define CHECK_DIALECT_BUILTIN(field) do { \
    assert(symbol_id_is_builtin(g_builtin_syms.field)); \
    assert(symbol_id_is_builtin_for(g_builtin_syms.field,CETTA_LANGUAGE_PETTA)); \
    assert(!symbol_id_is_builtin_for(g_builtin_syms.field,CETTA_LANGUAGE_HE)); \
    assert(!symbol_id_is_builtin_for(g_builtin_syms.field,CETTA_LANGUAGE_PRIME)); \
} while (0);
    CETTA_PETTA_ONLY_BUILTIN_SYMBOL_FIELDS(CHECK_DIALECT_BUILTIN)
#undef CHECK_DIALECT_BUILTIN
#define CHECK_CONSTRUCTOR(field) do { \
    assert(symbol_id_is_builtin(g_builtin_syms.field)); \
    assert(!symbol_id_is_builtin_for(g_builtin_syms.field,CETTA_LANGUAGE_PETTA)); \
    assert(!symbol_id_is_builtin_for(g_builtin_syms.field,CETTA_LANGUAGE_HE)); \
    assert(!symbol_id_is_builtin_for(g_builtin_syms.field,CETTA_LANGUAGE_PRIME)); \
} while (0);
    CETTA_BUILTIN_CONSTRUCTOR_SYMBOL_FIELDS(CHECK_CONSTRUCTOR)
#undef CHECK_CONSTRUCTOR
    assert(!symbol_id_is_builtin(early));
    assert(!symbol_id_is_builtin(SYMBOL_ID_NONE));
    assert(!symbol_id_is_builtin(symbol_intern_cstr(g_symbols, "ordinary-constructor")));
    assert(!symbol_id_is_builtin(symbol_intern_data_cstr(g_symbols, "runtime-constructor")));

    Atom *runtime = parse_one(&arena, "(runtime-head $runtime-var runtime-leaf)", true);
    assert(runtime && runtime->kind == ATOM_EXPR);
#if CETTA_MANAGED_SYMBOLS
    assert(atom_symbol_is_spelled(runtime->expr.elems[0]));
#else
    assert(symbol_flags(g_symbols, atom_symbol_current_id(runtime->expr.elems[0])) & CETTA_SYMBOL_FLAG_RUNTIME_DATA);
#endif
    assert(symbol_flags(g_symbols, atom_symbol_current_id(runtime->expr.elems[1])) & CETTA_SYMBOL_FLAG_RUNTIME_DATA);
    assert(runtime->expr.elems[1]->kind == ATOM_VAR);
#if CETTA_MANAGED_SYMBOLS
    assert(atom_symbol_is_spelled(runtime->expr.elems[2]));
#else
    assert(symbol_flags(g_symbols, atom_symbol_current_id(runtime->expr.elems[2])) & CETTA_SYMBOL_FLAG_RUNTIME_DATA);
#endif
    Atom *source = parse_one(&arena, "source-owned-after-data", false);
    assert(source && !(symbol_flags(g_symbols, atom_symbol_current_id(source)) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    assert(!parse_one(&arena, "(unterminated-runtime", true));
    source = parse_one(&arena, "source-owned-after-failure", false);
    assert(source && !(symbol_flags(g_symbols, atom_symbol_current_id(source)) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    Atom *same = parse_one(&arena, "runtime-leaf", false);
    assert(atom_symbols_equal(same, runtime->expr.elems[2]));
    assert(!(symbol_flags(g_symbols, atom_symbol_current_id(same)) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));

    /* A shared DAG must cost one visit per stored node, not one visit per
     * unfolding. Variable name-key graphs protect their spellings as well. */
    Atom *dag = runtime;
    for (unsigned i = 0u; i < 40u; i++)
        dag = atom_expr2(&arena, dag, dag);
    unsigned visits = 0u;
    assert(atom_visit_syntax_symbols(dag, count_symbol, &visits));
    assert(visits == (CETTA_MANAGED_SYMBOLS ? 2u : 3u));
    Atom *name_key = atom_symbol_data(&arena, "structural-name-key");
    Atom *variable = atom_var_with_presentation(&arena,
        symbol_intern_data_cstr(g_symbols, "presented-variable"), name_key, var_id_from_base(0u));
    visits = 0u;
    assert(atom_visit_syntax_symbols(variable, count_symbol, &visits));
    assert(visits == (CETTA_MANAGED_SYMBOLS ? 1u : 2u));
    atom_pin_syntax_symbols(variable);
    assert(!(symbol_flags(g_symbols, atom_symbol_current_id(variable)) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    assert(!(symbol_flags(g_symbols, atom_symbol_current_id(name_key)) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));

    CettaDurableField field = {.kind = DURABLE_FIELD_SYMBOL, .text = {"wire-only-symbol", 16}};
    unsigned char *bytes = NULL;
    size_t size = 0u;
    assert(cetta_durable_fields_encode(&field, &bytes, &size) == DURABLE_OK);
    Atom *decoded = NULL;
    assert(cetta_durable_value_decode(&arena, bytes, size, &decoded) == DURABLE_OK);
    free(bytes);
    assert(decoded && decoded->kind == ATOM_SYMBOL &&
           atom_symbol_equals_cstr(decoded, "wire-only-symbol"));
    assert(CETTA_MANAGED_SYMBOLS ? atom_symbol_is_spelled(decoded) :
        (symbol_flags(g_symbols, atom_symbol_current_id(decoded)) & CETTA_SYMBOL_FLAG_RUNTIME_DATA) != 0u);

    SymbolShadowScan scan;
    SymbolShadowResult shadow;
    assert(symbol_shadow_begin(g_symbols, &scan));
    assert(atom_visit_syntax_symbols(dag, mark_symbol, &scan));
    assert(atom_visit_syntax_symbols(decoded, mark_symbol, &scan));
    assert(symbol_shadow_finish(&scan, &shadow));
    assert(shadow.marked_data_count == (CETTA_MANAGED_SYMBOLS ? 1u : 3u) && !shadow.snapshot_outdated);
    assert(atom_symbol_equals_cstr(decoded, "wire-only-symbol"));
    symbol_shadow_destroy(&scan);

    Registry registry;
    registry_init(&registry);
    Atom *key = atom_symbol_data(&arena, "runtime-bind-key");
    registry_bind_id(&registry, atom_symbol_register_id(key), decoded);
    assert(!(symbol_flags(g_symbols, atom_symbol_current_id(key)) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    assert(CETTA_MANAGED_SYMBOLS ? atom_symbol_is_spelled(decoded) :
        (symbol_flags(g_symbols, atom_symbol_current_id(decoded)) & CETTA_SYMBOL_FLAG_RUNTIME_DATA) != 0u);
    assert(registry_lookup_id(&registry, atom_symbol_current_id(key)) == decoded);
    registry_free(&registry);

    Space space;
    space_init(&space);
    PettaProgram *program = petta_program_new();
    assert(program);
    Atom *equation = parse_one(&arena, "(= (dynamic-owned-head $arg) dynamic-owned-body)", true);
    const PettaPlanNode *plan = petta_program_plan_dynamic_add(program, equation);
    assert(plan && petta_program_note_add(program, &space, equation, plan));
    SymbolId head = symbol_lookup_cstr(g_symbols, "dynamic-owned-head");
    SymbolId body = symbol_lookup_cstr(g_symbols, "dynamic-owned-body");
    assert(head != SYMBOL_ID_NONE);
    assert(!(symbol_flags(g_symbols, head) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
#if CETTA_MANAGED_SYMBOLS
    assert(body == SYMBOL_ID_NONE);
#else
    assert(body != SYMBOL_ID_NONE);
    assert(!(symbol_flags(g_symbols, body) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
#endif
    Atom *type = parse_one(&arena, "(: dynamic-type-subject dynamic-type-name)", true);
    assert(petta_program_note_add(program, &space, type, NULL));
    assert(symbol_lookup_cstr(g_symbols, "dynamic-type-subject") != SYMBOL_ID_NONE);
#if CETTA_MANAGED_SYMBOLS
    assert(symbol_lookup_cstr(g_symbols, "dynamic-type-name") == SYMBOL_ID_NONE);
#else
    assert(!(symbol_flags(g_symbols, symbol_lookup_cstr(g_symbols, "dynamic-type-name")) &
             CETTA_SYMBOL_FLAG_RUNTIME_DATA));
#endif
    petta_program_free(program);
    space_free(&space);

    /* Native grammar records really store numeric keys. Runtime-created
     * labels must be registered there, rather than collapsing to NONE. */
    CettaLpNativeGrammar grammar;
    cetta_lp_native_grammar_init(&grammar);
    char grammar_error[256] = {0};
    Atom *grammar_list = parse_one(&arena,
        "(Cons (Prod fresh-prod fresh-sort ((Tm fresh-terminal-a)"
        " (Tm fresh-terminal-b))) Nil)", true);
    assert(cetta_lp_native_grammar_load_list(&grammar, grammar_list,
        grammar_error, sizeof(grammar_error)));
    assert(grammar.production_len == 1u);
    assert(grammar.productions[0].label != SYMBOL_ID_NONE);
    assert(grammar.productions[0].lhs != SYMBOL_ID_NONE);
    assert(grammar.productions[0].rhs[0].name != SYMBOL_ID_NONE);
    assert(grammar.productions[0].rhs[0].name !=
           grammar.productions[0].rhs[1].name);
    cetta_lp_native_grammar_free(&grammar);

    CettaGsltRigidKeyV1 rigid_key;
    Atom *unregistered = parse_one(&arena, "unregistered-rigid-key", true);
#if CETTA_MANAGED_SYMBOLS
    assert(!cetta_gslt_rigid_key_from_atom_v1(unregistered, &rigid_key));
    assert(symbol_lookup_cstr(g_symbols, "unregistered-rigid-key") == SYMBOL_ID_NONE);
#endif
    SymbolId registered = atom_symbol_register_id(unregistered);
    assert(cetta_gslt_rigid_key_from_atom_v1(unregistered, &rigid_key));
    assert(rigid_key.symbol == registered);

    /* A fact index built before numbering must remain complete afterwards.
     * A mixed known/fresh column used to store NONE as a real group key. */
    CettaGsltProviderRequirementV1 requirement = {
        .relation = "managed-fact", .arity = 1u,
        .semantic_id = "test-managed-fact"};
    Atom *fact = parse_one(&arena, "(managed-fact late-fact-value)", true);
    Atom *other_fact = parse_one(&arena, "(managed-fact Symbol)", true);
    Atom *facts[] = {fact, other_fact, fact};
    CettaGsltFiniteFactSpanV1 span = {.rows = facts, .row_count = 3u};
    CettaGsltFiniteFactProviderSetV1 *provider_set =
        cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
            &requirement, 1u, &span, 1u, grammar_error, sizeof(grammar_error));
    assert(provider_set);
    (void)atom_symbol_register_id(fact->expr.elems[1]);
    const CettaGsltProviderRegistryV1 *providers =
        cetta_gslt_finite_fact_provider_set_registry_v1(provider_set);
    assert(providers && providers->provider_count == 1u);
    CettaGsltProviderAnswersV1 answers = {0};
    const CettaGsltProviderV1 *provider = &providers->providers[0];
    assert(provider->query(provider->context, &arena, fact, 10u, &answers,
        grammar_error, sizeof(grammar_error)) == CETTA_GSLT_PROVIDER_COMPLETED);
    assert(answers.answer_count == 2u);
    assert(atom_eq(answers.answers[0], fact) && atom_eq(answers.answers[1], fact));
    cetta_gslt_provider_answers_free_v1(&answers);
    cetta_gslt_finite_fact_provider_set_free_v1(provider_set);

    SymbolTableStats gauges;
    CettaRuntimeStats snapshot;
    symbol_table_stats_snapshot(g_symbols, &gauges);
    cetta_runtime_stats_reset();
    cetta_runtime_stats_snapshot(&snapshot);
    assert(snapshot.counters[CETTA_RUNTIME_COUNTER_SYMBOL_DATA_COUNT] == gauges.data_count);
    assert(snapshot.counters[CETTA_RUNTIME_COUNTER_SYMBOL_DATA_BYTES] == gauges.data_bytes);
    assert(snapshot.counters[CETTA_RUNTIME_COUNTER_SYMBOL_METADATA_BYTES] == gauges.metadata_bytes);
    assert(gauges.retired_count == 0u);

#if CETTA_MANAGED_SYMBOLS
    Arena encoded_owner, temporary, destination;
    arena_init(&encoded_owner);
    arena_init(&temporary);
    arena_init(&destination);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &encoded_owner);
    const char payload[] = {'e', 0, (char)0xff, 'n'};
    Atom *value = atom_symbol_data_n(&temporary, payload, sizeof(payload));
    AtomId value_id = term_universe_store_atom_id(&universe, NULL, value);
    assert(value_id != CETTA_ATOM_ID_NONE);
    arena_free(&temporary);
    Atom *restored = term_universe_get_atom(&universe, value_id);
    assert(restored && atom_symbol_is_spelled(restored));
    assert(atom_symbol_length(restored) == sizeof(payload));
    assert(memcmp(atom_symbol_bytes(restored), payload, sizeof(payload)) == 0);
    Atom *copy = term_universe_copy_atom(&universe, &destination, value_id);
    assert(copy && term_universe_atom_id_eq(&universe, value_id, copy));
    assert(atom_symbol_current_id(copy) == SYMBOL_ID_NONE);
    term_universe_free(&universe);
    arena_free(&encoded_owner);
    assert(memcmp(atom_symbol_bytes(copy), payload, sizeof(payload)) == 0);
    arena_free(&destination);
#endif
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("test_runtime_symbol_births: parser, durable values, syntax roots, registration and gauges passed");
    return 0;
}
