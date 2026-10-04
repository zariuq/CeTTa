/* Real runtime structures with independently specified future reads. This
 * selected owner matrix is deliberately smaller than the full C census. */
#include "shadow_probe.h"
#include "delay_service.h"
#include "durable_value.h"
#include "eval.h"
#include "parser.h"
#include "term_graph.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Atom *parse(Arena *arena, const char *text) {
    size_t offset = 0;
    Atom *result = parse_sexpr(arena, text, &offset); assert(result); return result;
}
static bool visit_delay(void *unused, const CettaDelayGoal *goal) {
    (void)unused; gc_probe_visit_atom(goal->goal, "delays"); return true;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const char *family = argv[1];
    SymbolTable symbols;
    symbol_table_init(&symbols); g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_hashcons = NULL; g_var_intern = NULL;
    Arena arena;
    arena_init(&arena); arena_set_hashcons(&arena, NULL);
    /* This probes future numbered-table collection, independently of the
     * managed-spelling switch. No production payload is reclaimed here. */
    Atom *payload = atom_symbol_id(&arena, symbol_intern_data_cstr(
        g_symbols, "only-the-owner-keeps-this-payload"));
    SymbolId expected = atom_symbol_current_id(payload);
    Atom *root = NULL;
    Space space; space_init(&space);
    StateCell cell = {.value = payload};
    CaptureClosure closure = {.space_ptr = &space};
    Bindings bindings; bindings_init(&bindings);
    Registry registry; registry_init(&registry);
    CettaTermGraph *graph = NULL;
    CettaDelayService *delay = NULL;
    CettaPreparedPureProgram *program = NULL;
    CettaPreparedPureAnswerCursor *cursor = NULL;
    const char *borrowed = NULL;
    if (strcmp(family, "syntax") == 0) root = atom_expr(&arena, &payload, 1);
    else if (strcmp(family, "spaces") == 0) {
        space_add(&space, payload); root = atom_space(&arena, &space);
    } else if (strcmp(family, "state") == 0) root = atom_state(&arena, &cell);
    else if (strcmp(family, "closures") == 0) {
        space_add(&space, payload); root = atom_capture(&arena, &closure);
    } else if (strcmp(family, "bindings") == 0) {
        assert(bindings_add_id(&bindings, 1, SYMBOL_ID_NONE, payload));
    } else if (strcmp(family, "named-state") == 0) {
        assert(registry_state_set(&registry, symbol_intern_cstr(g_symbols, "named-key"), payload));
    } else if (strcmp(family, "term-graphs") == 0) {
        graph = term_graph_new();
        uint32_t node = term_graph_add_compound(graph, expected, 1);
        assert(term_graph_set_child(graph, node, 0, node));
        assert(term_graph_seal(graph)); root = atom_term_graph(&arena, graph, node);
    } else if (strcmp(family, "delays") == 0) {
        delay = cetta_delay_service_new(); VarId variable = 1;
        assert(cetta_delay_suspend(delay, 0, CETTA_DELAY_CLIENT_PROLOG,
                                  payload, &variable, 1, false));
    } else if (strcmp(family, "foreign-leases") == 0) {
        borrowed = symbol_bytes(g_symbols, expected);
    } else if (strcmp(family, "durable") == 0) {
        CettaDurableField field = {.kind = DURABLE_FIELD_SYMBOL,
            .text = {"decoded-owner-only", 18}};
        unsigned char *bytes; size_t size;
        assert(cetta_durable_fields_encode(&field, &bytes, &size) == DURABLE_OK);
        assert(cetta_durable_value_decode(&arena, bytes, size, &root) == DURABLE_OK);
        free(bytes);
#if CETTA_MANAGED_SYMBOLS
        root = atom_symbol_id(&arena, symbol_intern_data_bytes(
            g_symbols, (const uint8_t *)atom_symbol_bytes(root),
            atom_symbol_length(root)));
#endif
        expected = atom_symbol_current_id(root);
    } else if (strcmp(family, "prepared") == 0) {
        space_add(&space, parse(&arena, "(= (pick $x) $x)"));
        space_add(&space, parse(&arena, "(= (pick $x) $x)"));
        Atom *call = atom_expr2(&arena, atom_symbol(&arena, "pick"), payload);
        program = cetta_prepared_pure_program_compile_closed_answers(
            &space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always,
            NULL, NULL, NULL, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0});
        assert(program);
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = 1024, .max_scratch_bytes = 1024 * 1024,
                       .construct_allocation_bound = atom_expr_allocation_bound},
            .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS,
            .allow_continuations = true,
        };
        cursor = cetta_prepared_pure_answer_cursor_open(program, &options); assert(cursor);
    } else abort();
    assert(symbol_flags(g_symbols, expected) & CETTA_SYMBOL_FLAG_RUNTIME_DATA);
    assert(gc_probe_shadow_begin(1));
    if (root) gc_probe_visit_atom(root, family);
    if (strcmp(family, "bindings") == 0) gc_probe_visit_bindings(&bindings, family);
    if (strcmp(family, "named-state") == 0) gc_probe_visit_named_states(&registry);
    if (delay) assert(cetta_delay_visit_live(delay, visit_delay, NULL));
    if (cursor) gc_probe_visit_prepared(cursor);
    if (borrowed) gc_probe_visit_id(expected, family);
    gc_probe_shadow_end();
    /* This oracle is independent of scanner traversal. It identifies the
     * owner-specific future read even when raw C borrows bypass wrappers. */
    bool covered = gc_probe_is_marked(expected);
    if (borrowed) assert(borrowed[0] == 'o');
    else if (cursor && covered) {
        Atom *answer;
        for (unsigned i = 0; i < 2; i++) {
            assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) == CETTA_PREPARED_PURE_CURSOR_ANSWER);
            assert(answer->kind == ATOM_SYMBOL &&
                   atom_symbol_current_id(answer) == expected);
        }
        assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) == CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    } else (void)symbol_bytes(g_symbols, expected);
    const char *omit = getenv("CETTA_GC_SHADOW_OMIT");
    if (omit) assert(!covered);
    else assert(covered && gc_probe_missed_reads() == 0);
    printf("%s: %s\n", family, covered ? "future read covered" : "detected omitted visitor");
    if (cursor) cetta_prepared_pure_answer_cursor_close(cursor);
    if (program) cetta_prepared_pure_program_free(program);
    if (delay) cetta_delay_service_free(delay);
    if (graph) term_graph_release(graph);
    bindings_free(&bindings); registry_free(&registry); space_free(&space); arena_free(&arena);
    symbol_table_free(&symbols); g_symbols = NULL;
    return covered ? 0 : 1;
}
