#include "match.h"
#include "term_graph.h"
#include "term_canon.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <stdio.h>

static void saved_match(void) {
    Arena source, saved, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&destination);
    Atom *x = atom_var(&source, "x"), *u = atom_var(&source, "u");
    VarId x_id = x->var_id, u_id = u->var_id;
    Atom *subterm = atom_expr2(&source, atom_symbol(&source, "f"), u);
    Atom *subject = atom_expr2(&source, subterm, subterm);
    CettaTermMatch match;
    assert(term_graph_match_pattern(&source, x, subject, atom_eq, &match) == CETTA_TERM_MATCH_OK);
    assert(match.count == 1u && match.values[0] == subject);
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, match.variables[0], match.values[0]));
    Atom *witness = bindings_capture_value(&saved, &env);
    assert(witness);
    bindings_free(&env);
    arena_free(&source);

    Atom *query = atom_var_with_id(&destination, "x", x_id);
    Atom *result = bindings_apply_saved(&destination, witness, query);
    assert(result && result->kind == ATOM_EXPR && result->expr.len == 2u);
    assert(result->expr.elems[0] == result->expr.elems[1]);
    arena_free(&saved);
    assert(result->expr.elems[0]->expr.elems[1]->var_id == u_id);
    assert(atom_is_symbol(result->expr.elems[0]->expr.elems[0], "f"));
    arena_free(&destination);
}

static void saved_permutation(void) {
    Arena source, saved, copied, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&copied);
    arena_init_detached(&destination);
    Atom *x = atom_var(&source, "same"), *y = atom_var(&source, "same");
    Atom *z = atom_var(&source, "z");
    VarId xi = x->var_id, yi = y->var_id, zi = z->var_id;
    Atom *keys[] = {x, y}, *values[] = {y, x};
    Atom *witness = bindings_capture_renaming(&saved, keys, values, 2u);
    assert(witness);
    Atom *copy = atom_deep_copy(&copied, witness);
    assert(copy && atom_eq(copy, witness));

    /* Mixing saved logical substitutions and renamings cannot reinterpret
     * the memory layout or recursively follow a permutation cycle. */
    Bindings env;
    bindings_init(&env);
    assert(!bindings_restore_captured_value(copy, &env));
    assert(bindings_add_var(&env, x, atom_int(&source, 1)));
    Atom *logical = bindings_capture_value(&saved, &env);
    assert(!atom_eq(logical, witness) && !atom_eq(witness, logical));
    bindings_free(&env);
    arena_free(&source);
    arena_free(&saved);

    Atom *a = atom_var_with_id(&destination, "x", xi);
    Atom *b = atom_var_with_id(&destination, "y", yi);
    Atom *c = atom_var_with_id(&destination, "z", zi);
    Atom *shared = atom_expr2(&destination, a, b);
    Atom *term = atom_expr3(&destination, shared, shared, c);
    Atom *result = bindings_apply_saved(&destination, copy, term);
    assert(result && result->expr.elems[0] == result->expr.elems[1]);
    assert(result->expr.elems[0]->expr.elems[0]->var_id == yi);
    assert(result->expr.elems[0]->expr.elems[1]->var_id == xi);
    assert(result->expr.elems[2]->var_id == zi);
    CettaBindingsValue *value = copy->ground.ptr;
    Atom *projection = value->observe(&destination, value);
    arena_free(&copied);
    assert(result->expr.elems[0]->expr.elems[0]->var_id == yi);
    assert(projection && projection->expr.elems[1]->expr.len == 2u);
    assert(projection->expr.elems[1]->expr.elems[0]->expr.elems[1]->var_id == yi);
    arena_free(&destination);
}

static void saved_open_template(void) {
    Arena source, saved, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&destination);
    Atom *x = atom_var(&source, "x"), *y = atom_var(&source, "y");
    VarId xi = x->var_id, yi = y->var_id;
    Atom *shared = atom_expr2(&source, x, y);
    /* The bound image is itself a shared open DAG. Applying the saved
     * substitution must preserve both image and template sharing. */
    for (unsigned depth = 0; depth < 32u; ++depth)
        shared = atom_expr2(&source, shared, shared);
    Atom *image = atom_var(&source, "image");
    VarId ii = image->var_id;
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, image, shared));
    assert(bindings_add_var(&env, x, atom_symbol(&source, "done")));
    Atom *witness = bindings_capture_value(&saved, &env);
    assert(witness);
    bindings_free(&env);
    arena_free(&source);

    Atom *variable = atom_var_with_id(&destination, "image", ii);
    Atom *template = atom_expr2(&destination, variable,
        atom_var_with_id(&destination, "x", xi));
    for (unsigned depth = 0; depth < 32u; ++depth)
        template = atom_expr2(&destination, template, template);
    Atom *result = bindings_apply_saved(&destination, witness, template);
    assert(result);
    arena_free(&saved);
    for (unsigned depth = 0; depth < 32u; ++depth) {
        assert(result->kind == ATOM_EXPR && result->expr.len == 2u);
        assert(result->expr.elems[0] == result->expr.elems[1]);
        result = result->expr.elems[0];
    }
    assert(atom_is_symbol(result->expr.elems[1], "done"));
    result = result->expr.elems[0];
    for (unsigned depth = 0; depth < 32u; ++depth) {
        assert(result->expr.elems[0] == result->expr.elems[1]);
        result = result->expr.elems[0];
    }
    assert(atom_is_symbol(result->expr.elems[0], "done"));
    assert(result->expr.elems[1]->var_id == yi);
    arena_free(&destination);
}

static void shared_lexical_readings(void) {
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&frames);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&frames);
    Atom *slot = atom_var(&arena, "slot"), *x = atom_var(&arena, "x");
    Atom *y = atom_var(&arena, "y");
    Atom *source = atom_expr2(&arena, atom_symbol(&arena, "node"), slot);
    Bindings env;
    bindings_init(&env);
    assert(match_binding_values(binding_value_from_atom(x),
        binding_value_from_context(source, first), &env, &arena));
    assert(match_binding_values(binding_value_from_atom(y),
        binding_value_from_context(source, second), &env, &arena));
    Atom *a = atom_symbol(&arena, "a"), *b = atom_symbol(&arena, "b");
    assert(bindings_add_id(&env, var_epoch_id(slot->var_id, first), slot->sym_id, a));
    assert(bindings_add_id(&env, var_epoch_id(slot->var_id, second), slot->sym_id, b));
    Atom *pair = atom_expr2(&arena, x, y);
    Atom *ordinary = bindings_apply(&env, &arena, pair);
    Atom *shared = bindings_apply_graph(&env, &arena, pair);
    assert(ordinary && shared && atom_eq(ordinary, shared));
    assert(shared->expr.elems[0]->expr.elems[1] == a);
    assert(shared->expr.elems[1]->expr.elems[1] == b);
    bindings_free(&env);
    arena_free(&arena);
}

static void saved_deep_template(void) {
    Arena arena;
    arena_init_detached(&arena);
    Atom *x = atom_var(&arena, "x");
    Atom *tag = atom_symbol(&arena, "next");
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, x, atom_int(&arena, 7)));
    Atom *saved = bindings_capture_value(&arena, &env);
    bindings_free(&env);
    Atom *term = x;
    for (unsigned i = 0u; i < 100000u; ++i)
        term = atom_expr2(&arena, tag, term);
    Atom *result = bindings_apply_saved(&arena, saved, term);
    for (unsigned i = 0u; i < 100000u; ++i) {
        assert(result && result->kind == ATOM_EXPR && result->expr.len == 2u);
        result = result->expr.elems[1];
    }
    assert(result && atom_eq(result, atom_int(&arena, 7)));
    arena_free(&arena);
}

static void malformed_renaming(void) {
    Arena arena;
    arena_init_detached(&arena);
    Atom *x = atom_var(&arena, "x"), *y = atom_var(&arena, "y");
    Atom *keys[] = {x, y}, *values[] = {x, x};
    assert(!bindings_capture_renaming(&arena, keys, values, 2u));
    values[1] = atom_int(&arena, 1);
    assert(!bindings_capture_renaming(&arena, keys, values, 2u));
    keys[1] = x; values[1] = y;
    assert(!bindings_capture_renaming(&arena, keys, values, 2u));
    assert(!bindings_capture_renaming(&arena, NULL, NULL, 1u));
    assert(!bindings_capture_renaming(&arena, keys, values, SIZE_MAX));
    Atom *identity = bindings_capture_renaming(&arena, NULL, NULL, 0u);
    assert(identity && atom_eq(bindings_apply_saved(&arena, identity, x), x));
    assert(!bindings_apply_saved(&arena, x, y));
    arena_free(&arena);
}

static void saved_carrier(void) {
    Arena source, saved, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&destination);
    Atom *x = atom_var(&source, "x"), *u = atom_var(&source, "u");
    VarId xi = x->var_id, ui = u->var_id;
    CettaTermGraph *graph = term_graph_new();
    assert(graph);
    uint32_t parameter = term_graph_add_param(graph, 0u);
    uint32_t root = term_graph_add_compound(graph, atom_symbol(&source, "cycle")->sym_id, 2u);
    assert(term_graph_set_child(graph, root, 0u, parameter));
    assert(term_graph_set_child(graph, root, 1u, root));
    assert(term_graph_seal(graph));
    Atom *parameters[] = {u};
    Atom *subject = term_graph_node_value(&source, graph, root, parameters);
    assert(subject);
    term_graph_release(graph);

    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, x, subject));
    Atom *witness = bindings_capture_value(&saved, &env);
    Atom *keys[] = {u}, *values[] = {x};
    Atom *permutation = bindings_capture_renaming(&saved, keys, values, 1u);
    assert(witness && permutation);
    bindings_free(&env);
    arena_free(&source);

    Atom *query = atom_var_with_id(&destination, "x", xi);
    Atom *result = bindings_apply_saved(&destination, witness, query);
    Atom *renamed = bindings_apply_saved(&destination, permutation, result);
    assert(result && renamed);
    arena_free(&saved);
    Atom *result_parameter = term_graph_value_param(result, 0u);
    Atom *renamed_parameter = term_graph_value_param(renamed, 0u);
    assert(result_parameter && result_parameter->kind == ATOM_VAR && result_parameter->var_id == ui);
    assert(renamed_parameter && renamed_parameter->kind == ATOM_VAR && renamed_parameter->var_id == xi);
    const CettaTermGraphRef *reference;
    Atom *const *arguments;
    uint32_t argument_count;
    assert(term_graph_value_node(result, &reference, &arguments, &argument_count));
    assert(argument_count == 1u && arguments[0] == result_parameter);
    assert(term_graph_open_value(&destination, result));
    assert(term_graph_open_value(&destination, renamed));
    arena_free(&destination);
}

static void opaque_context_scope(void) {
    Arena source, saved, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&destination);
    Atom *key = atom_symbol(&source, "local");
    Atom *inside = atom_var(&source, "same");
    Atom *other = atom_var(&source, "same");
    Atom *capture = atom_var(&source, "capture");
    VarId inside_id = inside->var_id, capture_id = capture->var_id;
    Atom *context = atom_prime_context_bind(&source, NULL, key, inside);
    Atom *different = atom_prime_context_bind(&source, NULL, key, other);
    CettaTermMatch found;
    assert(term_graph_match_pattern(&source, context, different, atom_eq, &found)
        == CETTA_TERM_MATCH_MISMATCH);
    assert(term_graph_match_pattern(&source, context, context, atom_eq, &found)
        == CETTA_TERM_MATCH_OK && found.count == 0u);
    assert(term_graph_match_pattern(&source, capture, context, atom_eq, &found)
        == CETTA_TERM_MATCH_OK && found.count == 1u);
    Atom *witness = bindings_capture_images(&saved, found.variables, found.values,
        found.count, false);
    Atom *keys[] = {inside};
    Atom *images[] = {atom_symbol(&source, "outside")};
    Atom *outer = bindings_capture_images(&saved, keys, images, 1u, false);
    assert(witness && outer);
    arena_free(&source);
    Atom *result = bindings_apply_saved(&destination, witness,
        atom_var_with_id(&destination, "capture", capture_id));
    result = bindings_apply_saved(&destination, outer, result);
    assert(result && atom_prime_context_value(result));
    arena_free(&saved);
    Atom *value = atom_prime_context_lookup(atom_prime_context_value(result),
        atom_symbol(&destination, "local"));
    assert(value && value->kind == ATOM_VAR && value->var_id == inside_id);
    arena_free(&destination);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    saved_match();
    saved_permutation();
    saved_open_template();
    shared_lexical_readings();
    saved_deep_template();
    malformed_renaming();
    saved_carrier();
    opaque_context_scope();
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: saved matching and simultaneous renaming survive source and witness reclamation");
    return 0;
}
