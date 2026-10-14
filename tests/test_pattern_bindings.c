#include "match.h"
#include "term_graph.h"
#include "term_canon.h"
#include "binding/slot_store_internal.h"
#include "variant_shape.h"
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
#include "prime_semantics.h"
#else
#include "tests/test_runtime_stats_stubs.h"
#endif

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
    assert(bindings_add_id(&env, var_epoch_id(slot->var_id, first), atom_symbol_current_id(slot), a));
    assert(bindings_add_id(&env, var_epoch_id(slot->var_id, second), atom_symbol_current_id(slot), b));
    Atom *pair = atom_expr2(&arena, x, y);
    Atom *ordinary = bindings_apply(&env, &arena, pair);
    Atom *shared = bindings_apply_graph(&env, &arena, pair);
    assert(ordinary && shared && atom_eq(ordinary, shared));
    assert(shared->expr.elems[0]->expr.elems[1] == a);
    assert(shared->expr.elems[1]->expr.elems[1] == b);
    bindings_free(&env);
    arena_free(&arena);
}

/* A controlled quotation reading exercises the hook contract even in the
 * standalone matcher build. The full-runtime build uses the actual Prime
 * reading for this case. */
static Atom *quotation_graph_code_reading(Atom *image) {
    if (image && image->kind == ATOM_EXPR && image->expr.len == 3u &&
        atom_is_symbol(image->expr.elems[0], "quote") &&
        image->expr.elems[2]->kind == ATOM_EXPR &&
        image->expr.elems[2]->expr.len == 1u &&
        image->expr.elems[2]->expr.elems[0]->kind == ATOM_SYMBOL)
        return image->expr.elems[1];
    return image;
}

static Atom *quotation_graph_formed_node(Arena *arena, Atom *node,
                                         bool in_code) {
    return atom_expr3(arena,
        atom_symbol(arena, in_code ? "formed-code" : "formed-value"),
        node, atom_bool(arena, in_code));
}

static Atom *quotation_graph_rejected_form(Arena *arena, Atom *node,
                                           bool in_code) {
    (void)arena;
    return in_code ? NULL : node;
}

static void shared_quotation_readings(void) {
    Arena source, ordinary_arena, graph_arena;
    arena_init_detached(&source);
    arena_init_detached(&ordinary_arena);
    arena_init_detached(&graph_arena);
    const BindingsStructureHooks *previous = bindings_structure_hooks();
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
    bindings_structure_hooks_set(prime_semantics_binding_hooks());
#else
    const BindingsStructureHooks hooks = {
        .code_reading = quotation_graph_code_reading};
    bindings_structure_hooks_set(&hooks);
#endif
    Atom *quote = atom_symbol_id(&source, g_builtin_syms.quote);
    Atom *pair = atom_symbol(&source, "pair");
    Atom *row_head = atom_symbol(&source, "row");
    Atom *payload = atom_symbol(&source, "Payload");
    Atom *key[] = {atom_symbol(&source, "Key")};
    Atom *code = atom_expr3(&source, quote, payload,
                            atom_expr(&source, key, 1u));
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
    assert(prime_semantics_contextual_code(code));
#endif
    Atom *hole = atom_var(&source, "hole");
    Atom *shared = atom_expr2(&source, row_head, hole);
    Atom *quoted = atom_expr2(&source, quote, shared);
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var_acyclic(&env, hole, code));
    for (unsigned reverse = 0u; reverse < 2u; ++reverse) {
        Atom *term = reverse ? atom_expr3(&source, pair, quoted, shared)
                             : atom_expr3(&source, pair, shared, quoted);
        Atom *ordinary = bindings_apply(&env, &ordinary_arena, term);
        Atom *graph = bindings_apply_graph(&env, &graph_arena, term);
        assert(ordinary && graph && atom_eq(ordinary, graph));
        Atom *outside = graph->expr.elems[reverse ? 2u : 1u];
        Atom *inside = graph->expr.elems[reverse ? 1u : 2u]->expr.elems[1];
        assert(outside->expr.elems[1] == code);
        assert(inside->expr.elems[1] == payload);
        assert(outside != inside);
    }
    Atom *same_depth = bindings_apply_graph(&env, &graph_arena,
        atom_expr3(&source, pair, shared, shared));
    assert(same_depth && same_depth->expr.elems[1] == same_depth->expr.elems[2]);
    Atom *inside_pair = atom_expr3(&source, pair, shared, shared);
    Atom *quoted_pair = atom_expr2(&source, quote, inside_pair);
    Atom *nested = atom_expr3(&source, pair, quoted_pair,
        atom_expr2(&source, quote, quoted_pair));
    Atom *ordinary = bindings_apply(&env, &ordinary_arena, nested);
    Atom *graph = bindings_apply_graph(&env, &graph_arena, nested);
    assert(ordinary && graph && atom_eq(ordinary, graph));
    Atom *first = graph->expr.elems[1]->expr.elems[1];
    Atom *second = graph->expr.elems[2]->expr.elems[1]->expr.elems[1];
    assert(first->expr.elems[1] == first->expr.elems[2]);
    assert(second->expr.elems[1] == second->expr.elems[2]);
    assert(first->expr.elems[1] != second->expr.elems[1]);

    /* Grow and rehash both observation tables while retaining the same
     * syntax at different quotation depths and twice at each depth. */
    Atom *rows[64];
    for (unsigned i = 0u; i < 64u; ++i)
        rows[i] = atom_expr3(&source, row_head, atom_int(&source, i), hole);
    Atom *block = atom_expr(&source, rows, 64u);
    Atom *both = atom_expr3(&source, pair, block, block);
    Atom *grown = atom_expr3(&source, pair, both,
        atom_expr2(&source, quote, both));
    ordinary = bindings_apply(&env, &ordinary_arena, grown);
    graph = bindings_apply_graph(&env, &graph_arena, grown);
    assert(ordinary && graph && atom_eq(ordinary, graph));
    Atom *outside = graph->expr.elems[1];
    Atom *inside = graph->expr.elems[2]->expr.elems[1];
    assert(outside->expr.elems[1] == outside->expr.elems[2]);
    assert(inside->expr.elems[1] == inside->expr.elems[2]);
    for (unsigned i = 0u; i < 64u; ++i) {
        assert(outside->expr.elems[1]->expr.elems[i]->expr.elems[2] == code);
        assert(inside->expr.elems[1]->expr.elems[i]->expr.elems[2] == payload);
    }

    /* A raw cycle is rejected even when every traversal of its back edge
     * changes quotation depth. The explicit rational carrier is separate. */
    Atom *cycle = atom_expr2(&source, quote, hole);
    cycle->expr.elems[1] = cycle;
    assert(!bindings_apply_graph(&env, &graph_arena, cycle));
    Atom *plain_cycle = atom_expr2(&source, row_head, hole);
    plain_cycle->expr.elems[1] = plain_cycle;
    assert(!bindings_apply_graph(&env, &graph_arena, plain_cycle));
    bindings_structure_hooks_set(previous);
    bindings_free(&env);
    arena_free(&graph_arena);
    arena_free(&ordinary_arena);
    arena_free(&source);
}

static void shared_formed_quotation_readings(void) {
    Arena source, ordinary_arena, graph_arena;
    arena_init_detached(&source);
    arena_init_detached(&ordinary_arena);
    arena_init_detached(&graph_arena);
    const BindingsStructureHooks *previous = bindings_structure_hooks();
    const BindingsStructureHooks hooks = {
        .formed_node = quotation_graph_formed_node,
        .code_reading = quotation_graph_code_reading};
    bindings_structure_hooks_set(&hooks);
    Atom *head = atom_var(&source, "head");
    Atom *call = atom_expr2(&source, head, atom_int(&source, 9));
    Atom *quoted = atom_expr2(&source,
        atom_symbol_id(&source, g_builtin_syms.quote), call);
    Atom *pair = atom_symbol(&source, "pair");
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var_acyclic(&env, head, atom_symbol(&source, "new-form")));
    for (unsigned reverse = 0u; reverse < 2u; ++reverse) {
        Atom *term = reverse ? atom_expr3(&source, pair, quoted, call)
                             : atom_expr3(&source, pair, call, quoted);
        Atom *ordinary = bindings_apply(&env, &ordinary_arena, term);
        Atom *graph = bindings_apply_graph(&env, &graph_arena, term);
        assert(ordinary && graph && atom_eq(ordinary, graph));
        assert(atom_is_symbol(graph->expr.elems[reverse ? 2u : 1u]->expr.elems[0],
                              "formed-value"));
        assert(atom_is_symbol(graph->expr.elems[reverse ? 1u : 2u]->expr.elems[1]
                                  ->expr.elems[0], "formed-code"));
    }
    const BindingsStructureHooks rejecting = {
        .formed_node = quotation_graph_rejected_form,
        .code_reading = quotation_graph_code_reading};
    bindings_structure_hooks_set(&rejecting);
    for (unsigned reverse = 0u; reverse < 2u; ++reverse) {
        Atom *term = reverse ? atom_expr3(&source, pair, quoted, call)
                             : atom_expr3(&source, pair, call, quoted);
        assert(!bindings_apply_graph(&env, &graph_arena, term));
    }
    bindings_structure_hooks_set(previous);
    bindings_free(&env);
    arena_free(&graph_arena);
    arena_free(&ordinary_arena);
    arena_free(&source);
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
    uint32_t root = term_graph_add_compound(graph, atom_symbol_current_id(atom_symbol(&source, "cycle")), 2u);
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

static void frame_inventory_shared_readings(void) {
    Arena source, destination;
    arena_init_detached(&source);
    arena_init_detached(&destination);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&frames);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&frames);
    Atom *a = atom_var(&source, "inventory-a");
    Atom *b = atom_var(&source, "inventory-b");
    Atom *shared = atom_expr2(&source, a, b);
    for (unsigned depth = 0u; depth < 32u; ++depth)
        shared = atom_expr2(&source, shared, shared);
    Atom *left = atom_var(&source, "inventory-left");
    Atom *right = atom_var(&source, "inventory-right");
    Bindings env;
    bindings_init(&env);
    assert(match_binding_values(binding_value_from_atom(left),
        binding_value_from_context(shared, first), &env, &source));
    assert(match_binding_values(binding_value_from_atom(right),
        binding_value_from_context(shared, second), &env, &source));
    const BindingsFrameIndexEntry *f = bindings_frame_index_find_frame_const(
        env.frame_index, first);
    const BindingsFrameIndexEntry *g = bindings_frame_index_find_frame_const(
        env.frame_index, second);
    assert(f && g && f != g && f->slot_len == 2u && g->slot_len == 2u);
    assert(!f->schema_complete && !g->schema_complete);
    assert(!f->bound_value_count && !g->bound_value_count);
    /* Registration sorts sparse source coordinates. Both contexts retain
     * their own authoritative schema and values. */
    assert(bindings_frame_slot_source_id(f, 0u) == var_base_id(a->var_id));
    assert(bindings_frame_slot_source_id(f, 1u) == var_base_id(b->var_id));
    assert(bindings_frame_slot_source_id(g, 0u) == var_base_id(a->var_id));
    assert(bindings_frame_slot_source_id(g, 1u) == var_base_id(b->var_id));
    assert(bindings_add_id(&env, var_epoch_id(a->var_id, first), atom_symbol_current_id(a),
                           atom_int(&source, 1)));
    assert(bindings_add_id(&env, var_epoch_id(b->var_id, first), atom_symbol_current_id(b),
                           atom_int(&source, 2)));
    assert(bindings_add_id(&env, var_epoch_id(a->var_id, second), atom_symbol_current_id(a),
                           atom_int(&source, 3)));
    assert(bindings_add_id(&env, var_epoch_id(b->var_id, second), atom_symbol_current_id(b),
                           atom_int(&source, 4)));
    assert(env.len == 2u && env.frame_index->bound_value_count == 4u);
    Atom *result = bindings_apply_graph(&env, &destination,
        atom_expr2(&source, left, right));
    assert(result && result->expr.elems[0] != result->expr.elems[1]);
    for (unsigned side = 0u; side < 2u; ++side) {
        Atom *node = result->expr.elems[side];
        for (unsigned depth = 0u; depth < 32u; ++depth) {
            assert(node && node->kind == ATOM_EXPR && node->expr.len == 2u);
            assert(node->expr.elems[0] == node->expr.elems[1]);
            node = node->expr.elems[0];
        }
        assert(node && node->kind == ATOM_EXPR && node->expr.len == 2u);
        assert(atom_eq(node->expr.elems[0], atom_int(&source, 1 + 2 * side)));
        assert(atom_eq(node->expr.elems[1], atom_int(&source, 2 + 2 * side)));
    }
    bindings_free(&env);
    arena_free(&destination);
    arena_free(&source);
}

static void frame_inventory_identity_filtering(void) {
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&frames);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&frames);
    Atom *a = atom_var(&arena, "mixed-a");
    Atom *b = atom_var(&arena, "mixed-b");
    Atom *framed_a = atom_var_like(&arena, a, var_epoch_id(a->var_id, first));
    Atom *framed_b = atom_var_like(&arena, b, var_epoch_id(b->var_id, second));
    Atom *private = atom_var_with_id(&arena, "private",
                                     variant_shape_slot_id(7u));
    Atom *plain = atom_var(&arena, "unframed");
    Atom *items[] = {framed_a, private, plain, framed_b};
    Atom *row = atom_expr(&arena, items, 4u);
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, atom_var(&arena, "materialized"),
                             atom_expr2(&arena, row, row)));
    assert(env.frame_index && env.frame_index->len == 2u);
    const BindingsFrameIndexEntry *f = bindings_frame_index_find_frame_const(
        env.frame_index, first);
    const BindingsFrameIndexEntry *g = bindings_frame_index_find_frame_const(
        env.frame_index, second);
    assert(f && g && f->slot_len == 1u && g->slot_len == 1u);
    assert(bindings_frame_slot_source_id(f, 0u) == var_base_id(a->var_id));
    assert(bindings_frame_slot_source_id(g, 0u) == var_base_id(b->var_id));
    assert(!bindings_frame_index_find_frame_const(
        env.frame_index, var_epoch_suffix(private->var_id)));
    bindings_free(&env);

    bindings_init(&env);
    Atom *context_items[] = {a, private, b};
    row = atom_expr(&arena, context_items, 3u);
    assert(match_binding_values(
        binding_value_from_atom(atom_var(&arena, "contextual")),
        binding_value_from_context(atom_expr2(&arena, row, row), first),
        &env, &arena));
    f = bindings_frame_index_find_frame_const(env.frame_index, first);
    assert(f && env.frame_index->len == 1u && f->slot_len == 2u);
    assert(bindings_frame_slot_source_id(f, 0u) == var_base_id(a->var_id));
    assert(bindings_frame_slot_source_id(f, 1u) == var_base_id(b->var_id));
    bindings_free(&env);
    arena_free(&arena);
}

static void frame_inventory_raw_cycle_refusal(void) {
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&frames);
    Atom *a = atom_var(&arena, "cycle-a");
    Atom *b = atom_var(&arena, "cycle-b");
    a = atom_var_like(&arena, a, var_epoch_id(a->var_id, epoch));
    b = atom_var_like(&arena, b, var_epoch_id(b->var_id, epoch));
    Atom *capture = atom_var(&arena, "cycle-capture");
    Atom *placeholder = atom_symbol(&arena, "placeholder");
    Atom *cycle = atom_expr3(&arena, a, b, placeholder);
    cycle->expr.elems[2] = cycle;
    Bindings env, before;
    bindings_init(&env);
    assert(bindings_add_var(&env, atom_var(&arena, "sentinel"), placeholder));
    assert(bindings_clone(&before, &env));
    assert(!bindings_add_var(&env, capture, cycle));
    assert(bindings_eq(&env, &before));
    assert(!env.frame_index && env.len == before.len);
    Atom *nested = atom_expr2(&arena, cycle, placeholder);
    cycle->expr.elems[2] = nested;
    assert(!bindings_add_var(&env, capture, nested));
    assert(bindings_eq(&env, &before));
    assert(!env.frame_index && env.len == before.len);
    cycle->expr.elems[2] = placeholder;
    assert(bindings_add_var(&env, capture, atom_expr2(&arena, cycle, cycle)));
    const BindingsFrameIndexEntry *f = bindings_frame_index_find_frame_const(
        env.frame_index, epoch);
    assert(f && f->slot_len == 2u && env.len == before.len + 1u);
    assert(bindings_frame_slot_source_id(f, 0u) == var_base_id(a->var_id));
    assert(bindings_frame_slot_source_id(f, 1u) == var_base_id(b->var_id));
    bindings_free(&before);
    bindings_free(&env);
    arena_free(&arena);
}

static void frame_inventory_growth_and_cleanup(void) {
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&frames);
    Atom *common = atom_var(&arena, "growth-common");
    common = atom_var_like(&arena, common, var_epoch_id(common->var_id, epoch));
    Atom *rows[64], *variables[64];
    for (unsigned i = 0u; i < 64u; ++i) {
        Atom *variable = atom_var(&arena, "growth-slot");
        variables[i] = atom_var_like(&arena, variable,
                                      var_epoch_id(variable->var_id, epoch));
        rows[i] = atom_expr2(&arena, common, variables[i]);
    }
    Atom *block = atom_expr(&arena, rows, 64u);
    Bindings env;
    bindings_init(&env);
    Atom *capture = atom_var(&arena, "growth-capture");
    assert(bindings_add_var(&env, capture, atom_expr2(&arena, block, block)));
    const BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame_const(
        env.frame_index, epoch);
    assert(frame && frame->slot_len == 65u && frame->schema->len == 65u);
    assert(bindings_frame_slot_source_id(frame, 0u) == var_base_id(common->var_id));
    for (unsigned i = 0u; i < 64u; ++i)
        assert(bindings_frame_slot_source_id(frame, i + 1u) ==
                 var_base_id(variables[i]->var_id));
    bindings_free(&env);

    /* The rightmost rows grow the work stack, expression memo, variable
     * list and identifier set before the leftmost edge returns to the root. */
    bindings_init(&env);
    Atom *first = block->expr.elems[0];
    block->expr.elems[0] = block;
    assert(!bindings_add_var(&env, capture, block));
    assert(!env.len && !env.frame_index);
    block->expr.elems[0] = first;
    assert(bindings_add_var(&env, capture, block));
    frame = bindings_frame_index_find_frame_const(env.frame_index, epoch);
    assert(frame && frame->slot_len == 65u && env.len == 1u);
    bindings_free(&env);
    arena_free(&arena);
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
    shared_quotation_readings();
    shared_formed_quotation_readings();
    frame_inventory_shared_readings();
    frame_inventory_identity_filtering();
    frame_inventory_raw_cycle_refusal();
    frame_inventory_growth_and_cleanup();
    saved_deep_template();
    malformed_renaming();
    saved_carrier();
    opaque_context_scope();
    symbol_table_free(&symbols);
    g_symbols = NULL;
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
    puts("PASS: graph substitution preserves actual Prime quotation readings and aliasing");
#else
    puts("PASS: graph substitution preserves quotation hook readings and aliasing");
#endif
    puts("PASS: shared frame inventories preserve context, filtering and refusal cleanup");
    puts("PASS: saved matching and simultaneous renaming survive source and witness reclamation");
    return 0;
}
