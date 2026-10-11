#include "atom.h"
#include "match.h"
#include "parser.h"
#include "symbol.h"
#include "term_universe.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TERM_DEPTH = 32768, SYNTAX_DEPTH = 4095,
       STATE_DEPTH = 2048, CONTEXT_DEPTH = 2048 };

typedef struct {
    CettaBindingsValue value;
    unsigned holds, equal_calls;
    int64_t observed;
} EqualityProbe;

static void equality_probe_retain(void *owner) {
    ((EqualityProbe *)owner)->holds++;
}

static void equality_probe_release(void *owner) {
    EqualityProbe *probe = owner;
    assert(probe->holds > 0u);
    probe->holds--;
}

static Atom *equality_probe_observe(Arena *arena, const CettaBindingsValue *value) {
    return atom_int(arena, ((const EqualityProbe *)value)->observed);
}

static bool equality_probe_equal(const CettaBindingsValue *left,
                                 const CettaBindingsValue *right) {
    EqualityProbe *probe = (EqualityProbe *)left;
    probe->equal_calls++;
    return probe->observed == ((const EqualityProbe *)right)->observed;
}

static void alpha_equality_service_control(void) {
    EqualityProbe left = {.value = {.retain = equality_probe_retain,
        .release = equality_probe_release, .observe = equality_probe_observe,
        .equal = equality_probe_equal}, .observed = 7};
    EqualityProbe right = left;
    Arena arena;
    arena_init_detached(&arena);
    Atom *head = atom_symbol(&arena, "opaque-alpha-control");
    Atom *a = atom_bindings_value(&arena, &left.value);
    Atom *b = atom_bindings_value(&arena, &right.value);
    assert(a && b);
    Atom *nested_left = atom_expr2(&arena, head, atom_int(&arena, 9));
    Atom *nested_right = atom_expr2(&arena, head, atom_int(&arena, 9));
    assert(atom_alpha_eq(atom_expr3(&arena, head, a, nested_left),
                         atom_expr3(&arena, head, b, nested_right)));
    assert(left.equal_calls == 1u);
    right.observed = 8;
    assert(!atom_alpha_eq(atom_expr3(&arena, head, a, nested_left),
                          atom_expr3(&arena, head, b, nested_right)));
    assert(left.equal_calls == 2u);
    arena_free(&arena);
    assert(!left.holds && !right.holds);
    puts("PASS: alpha equality invokes an opaque equality service once before nested fallback, with owner release");
}

static Atom *spine(Arena *arena, Atom *head, Atom *leaf) {
    for (unsigned i = 0; i < TERM_DEPTH; i++)
        leaf = atom_expr2(arena, head, leaf);
    return leaf;
}

static Atom *spine_leaf(Atom *term, Atom *head) {
    for (unsigned i = 0; i < TERM_DEPTH; i++) {
        assert(term && term->kind == ATOM_EXPR && term->expr.len == 2u);
        assert(term->expr.elems[0] == head);
        term = term->expr.elems[1];
    }
    return term;
}

static void exercise(Arena *arena, const char *operation) {
    Atom *head = atom_symbol(arena, "depth-node");
    Atom *x = atom_var_with_id(arena, "same-name", UINT64_C(90001));
    Atom *y = atom_var_with_id(arena, "same-name", UINT64_C(90002));
    Atom *left = spine(arena, head, x);
    if (!strcmp(operation, "rename")) {
        Atom *renamed = rename_vars(arena, left, 17u);
        Atom *leaf = spine_leaf(renamed, head);
        assert(leaf && leaf->kind == ATOM_VAR);
        assert(leaf->var_id == var_epoch_id(x->var_id, 17u));
    } else if (!strcmp(operation, "freshen")) {
        Atom *renamed = atom_freshen_epoch(arena, left, 19u);
        Atom *leaf = spine_leaf(renamed, head);
        assert(leaf && leaf->kind == ATOM_VAR);
        assert(leaf->var_id == var_epoch_id(x->var_id, 19u));
    } else if (!strcmp(operation, "only") || !strcmp(operation, "except")) {
        Atom *renamed = !strcmp(operation, "only")
            ? rename_vars_only(arena, left, x)
            : rename_vars_except(arena, left, y);
        Atom *leaf = spine_leaf(renamed, head);
        assert(leaf && leaf->kind == ATOM_VAR && leaf->var_id != x->var_id);
    } else if (!strcmp(operation, "simple")) {
        Bindings bindings;
        bindings_init(&bindings);
        Atom *value = atom_int(arena, 42);
        assert(simple_match(left, spine(arena, head, value), &bindings, arena));
        assert(bindings_lookup_value_id(&bindings, x->var_id).skeleton == value);
        bindings_free(&bindings);
    } else if (!strcmp(operation, "builder")) {
        BindingsBuilder builder;
        assert(bindings_builder_init(&builder, NULL));
        Atom *value = atom_int(arena, 42);
        assert(simple_match_builder(left, spine(arena, head, value), &builder, arena));
        assert(bindings_lookup_value_id(&builder.current, x->var_id).skeleton == value);
        bindings_builder_free(&builder);
    } else if (!strcmp(operation, "alpha")) {
        Atom *right = spine(arena, head, y);
        assert(atom_alpha_eq(left, right));
        assert(!atom_alpha_eq(left, spine(arena, head, atom_int(arena, 42))));
    } else if (!strcmp(operation, "equal") ||
               !strcmp(operation, "value-equal")) {
        Atom *right = spine(arena, head, x);
        Atom *different = spine(arena, head, y);
        bool (*compare)(Atom *, Atom *) = !strcmp(operation, "equal")
            ? atom_eq : atom_value_eq;
        assert(compare(left, right));
        assert(!compare(left, different));
    } else if (!strcmp(operation, "hash") ||
               !strcmp(operation, "value-hash")) {
        Atom *right = spine(arena, head, x);
        uint32_t (*hash)(Atom *) = !strcmp(operation, "hash")
            ? atom_hash : atom_value_hash;
        assert(hash(left) == hash(right));
    } else {
        assert(!"unknown operation");
    }
    assert(spine_leaf(left, head) == x);
    printf("PASS: %s preserves semantics at %u levels\n", operation, TERM_DEPTH);
}

static void controls(Arena *arena) {
    Atom *head = atom_symbol(arena, "control-node");
    Atom *x = atom_var_with_id(arena, "equal-spelling", UINT64_C(91001));
    Atom *y = atom_var_with_id(arena, "equal-spelling", UINT64_C(91002));
    Atom *u = atom_var_with_id(arena, "equal-spelling", UINT64_C(91003));
    Atom *one = atom_int(arena, 1), *two = atom_int(arena, 2);
    Atom *shared = atom_expr3(arena, head, x, x);
    Atom *dag = atom_expr3(arena, head, shared, shared);
    Atom *renamed = rename_vars(arena, dag, 23u);
    assert(renamed && renamed->expr.elems[1] == renamed->expr.elems[2]);
    Atom *row = renamed->expr.elems[1];
    assert(row->expr.elems[1]->var_id == var_epoch_id(x->var_id, 23u));
    assert(row->expr.elems[1] == row->expr.elems[2]);
    assert(rename_vars_only(arena, dag, y) == dag);
    assert(rename_vars_except(arena, dag, x) == dag);
    assert(atom_freshen_epoch(arena, atom_expr2(arena, head, one), 23u)
        ->expr.elems[1] == one);
    Atom *named_key = atom_expr2(arena, head, one);
    Atom *named = atom_var_with_name_key(arena, named_key, UINT64_C(92001));
    Atom *named_fresh = atom_freshen_epoch(arena, named, 25u);
    assert(named_fresh && named_fresh->var_id == var_epoch_id(named->var_id, 25u));
    assert(atom_eq(named_fresh->name_key, named_key));
    Atom *context = atom_prime_context_bind(arena, NULL, head, x);
    assert(rename_vars(arena, context, 29u) == context);
    assert(atom_freshen_epoch(arena, context, 29u) == context);
    assert(atom_alpha_eq(shared, atom_expr3(arena, head, y, y)));
    assert(!atom_alpha_eq(shared, atom_expr3(arena, head, y, u)));
    assert(!atom_alpha_eq(atom_expr3(arena, head, x, y),
                         atom_expr3(arena, head, u, u)));

    for (unsigned use_builder = 0; use_builder < 2; use_builder++) {
        Bindings bindings;
        BindingsBuilder builder;
        bindings_init(&bindings);
        assert(bindings_builder_init(&builder, NULL));
        Bindings *current = use_builder ? &builder.current : &bindings;
#define MATCH(p, t) (use_builder \
    ? simple_match_builder((p), (t), &builder, arena) \
    : simple_match((p), (t), &bindings, arena))
        assert(!MATCH(atom_expr3(arena, head, x, one),
                      atom_expr3(arena, head, two, two)));
        assert(bindings_lookup_value_id(current, x->var_id).skeleton == two);
        if (use_builder) bindings_builder_rollback(&builder, 0u);
        else { bindings_free(&bindings); bindings_init(&bindings); }
        assert(!MATCH(atom_expr3(arena, head, one, x),
                      atom_expr3(arena, head, two, one)));
        assert(!bindings_lookup_value_id(current, x->var_id).skeleton);
        Atom *prefix[] = {x}, *items[] = {one, two, one};
        assert(MATCH(atom_list_with_rest(arena, prefix, 1u, y),
                     atom_list(arena, items, 3u)));
        assert(bindings_lookup_value_id(current, x->var_id).skeleton == one);
        Atom *rest_items[] = {two, one};
        assert(atom_eq(bindings_lookup_value_id(current, y->var_id).skeleton,
                       atom_list(arena, rest_items, 2u)));
        if (use_builder) bindings_builder_rollback(&builder, 0u);
        else { bindings_free(&bindings); bindings_init(&bindings); }
        assert(!MATCH(one, x));
        assert(!bindings_lookup_value_id(current, x->var_id).skeleton);
        assert(!MATCH(shared, atom_expr3(arena, head, one, two)));
        assert(bindings_lookup_value_id(current, x->var_id).skeleton == one);
        bindings_builder_free(&builder);
        bindings_free(&bindings);
#undef MATCH
    }

    Atom *cycle = atom_expr2(arena, head, x);
    cycle->expr.elems[1] = cycle;
    assert(rename_vars(arena, cycle, 31u) == NULL);
    assert(atom_freshen_epoch(arena, cycle, 31u) == NULL);
    assert(rename_vars_only(arena, cycle, x) == NULL);
    assert(rename_vars_except(arena, cycle, y) == NULL);
    assert(!atom_alpha_eq(cycle, cycle));
    Bindings bindings;
    bindings_init(&bindings);
    assert(!simple_match(cycle, cycle, &bindings, arena));
    BindingsBuilder builder;
    assert(bindings_builder_init(&builder, NULL));
    assert(!simple_match_builder(cycle, cycle, &builder, arena));
    bindings_builder_free(&builder);
    bindings_free(&bindings);
    cycle->expr.elems[1] = x;
    Atom *capability = atom_prime_need_capability_with_rights(
        arena, 11u, 12u, 13u, CETTA_PRIME_NEED_RIGHT_FORCE);
    Atom *same_capability = atom_prime_need_capability_with_rights(
        arena, 11u, 12u, 13u, CETTA_PRIME_NEED_RIGHT_FORCE);
    Atom *other_authority = atom_prime_need_capability_with_rights(
        arena, 11u, 12u, 14u, CETTA_PRIME_NEED_RIGHT_FORCE);
    assert(capability && same_capability && other_authority);
    assert(atom_eq(capability, same_capability));
    assert(!atom_eq(capability, other_authority));
    puts("PASS: sharing, lexical names, opaque contexts, binding order, list rests and raw-cycle controls");
}

static void state_context_depth(Arena *arena, const char *operation) {
    Atom *left = atom_int(arena, 1), *right = atom_int(arena, 1);
    Atom *different = atom_int(arena, 2);
    if (!strcmp(operation, "state-equal")) {
        for (unsigned i = 0; i < STATE_DEPTH; i++) {
            StateCell *a = arena_alloc(arena, sizeof(*a));
            StateCell *b = arena_alloc(arena, sizeof(*b));
            StateCell *c = arena_alloc(arena, sizeof(*c));
            assert(a && b && c);
            *a = (StateCell){.value = left};
            *b = (StateCell){.value = right};
            *c = (StateCell){.value = different};
            left = atom_state(arena, a);
            right = atom_state(arena, b);
            different = atom_state(arena, c);
        }
        assert(atom_eq(left, right));
        assert(atom_value_eq(left, right));
        assert(!atom_eq(left, different));
        StateCell *ca = arena_alloc(arena, sizeof(*ca));
        StateCell *cb = arena_alloc(arena, sizeof(*cb));
        assert(ca && cb);
        Atom *cycle_a = atom_state(arena, ca);
        Atom *cycle_b = atom_state(arena, cb);
        *ca = (StateCell){.value = cycle_a};
        *cb = (StateCell){.value = cycle_b};
        assert(atom_eq(cycle_a, cycle_b));
        puts("PASS: state equality and coinduction at 2048 levels");
    } else {
        Atom *key = atom_symbol(arena, "context-key");
        for (unsigned i = 0; i < CONTEXT_DEPTH; i++) {
            left = atom_prime_context_bind(arena, NULL, key, left);
            right = atom_prime_context_bind(arena, NULL, key, right);
            different = atom_prime_context_bind(arena, NULL, key, different);
            assert(left && right && different);
        }
        assert(atom_eq(left, right));
        assert(!atom_eq(left, different));
        puts("PASS: nested context equality at 2048 levels");
    }
}

static void syntax_depth(Arena *arena, const char *operation) {
    char text[SYNTAX_DEPTH * 2u + 3u];
    memset(text, '(', SYNTAX_DEPTH);
    memcpy(text + SYNTAX_DEPTH, "42", 2u);
    memset(text + SYNTAX_DEPTH + 2u, ')', SYNTAX_DEPTH);
    text[SYNTAX_DEPTH * 2u + 2u] = '\0';
    size_t position = 0u;
    if (!strcmp(operation, "parse")) {
        Atom *value = parse_sexpr(arena, text, &position);
        assert(value && position == strlen(text));
        for (unsigned i = 0; i < SYNTAX_DEPTH; i++) {
            assert(value->kind == ATOM_EXPR && value->expr.len == 1u);
            value = value->expr.elems[0];
        }
        assert(value->kind == ATOM_GROUNDED && value->ground.ival == 42);
    } else if (!strcmp(operation, "render")) {
        Atom *value = atom_int(arena, 42);
        for (unsigned i = 0; i < SYNTAX_DEPTH; i++)
            value = atom_expr(arena, &value, 1u);
        char *rendered = parser_render_syntax(arena, value, PARSER_SYNTAX_PRINT_COMPACT);
        assert(rendered && !strcmp(rendered, text));
    } else {
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, arena);
        AtomId id = CETTA_ATOM_ID_NONE;
        if (!strcmp(operation, "parse-id")) {
            id = parse_sexpr_to_id(&universe, text, &position);
            assert(id != CETTA_ATOM_ID_NONE && position == strlen(text));
        } else {
            Atom *value = atom_int(arena, 42);
            for (unsigned i = 0; i < SYNTAX_DEPTH; i++)
                value = atom_expr(arena, &value, 1u);
            AtomId *ids = NULL;
            assert(parser_project_document_ids(&value, 1u, &universe, &ids));
            assert(ids);
            id = ids[0];
            free(ids);
        }
        for (unsigned i = 0; i < SYNTAX_DEPTH; i++) {
            assert(tu_kind(&universe, id) == ATOM_EXPR && tu_arity(&universe, id) == 1u);
            id = tu_child(&universe, id, 0u);
        }
        assert(tu_kind(&universe, id) == ATOM_GROUNDED);
        term_universe_free(&universe);
    }
    printf("PASS: %s at %u syntax levels\n", operation, SYNTAX_DEPTH);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    const char *operations[] = {"rename", "freshen", "only", "except",
                                "simple", "builder", "alpha", "equal",
                                "value-equal", "hash", "value-hash"};
    for (size_t i = 0; i < sizeof operations / sizeof operations[0]; i++) {
        if (argc > 1 && strcmp(argv[1], operations[i]))
            continue;
        Arena arena;
        arena_init_detached(&arena);
        exercise(&arena, operations[i]);
        arena_free(&arena);
    }
    if (argc == 1 || !strcmp(argv[1], "controls")) {
        Arena arena;
        arena_init_detached(&arena);
        controls(&arena);
        arena_free(&arena);
        alpha_equality_service_control();
    }
    const char *context_operations[] = {"state-equal", "context-equal"};
    for (size_t i = 0; i < sizeof context_operations / sizeof context_operations[0]; i++) {
        if (argc > 1 && strcmp(argv[1], context_operations[i]))
            continue;
        Arena arena;
        arena_init_detached(&arena);
        state_context_depth(&arena, context_operations[i]);
        arena_free(&arena);
    }
    const char *syntax_operations[] = {"parse", "parse-id", "project", "render"};
    for (size_t i = 0; i < sizeof syntax_operations / sizeof syntax_operations[0]; i++) {
        if (argc > 1 && strcmp(argv[1], syntax_operations[i]))
            continue;
        Arena arena;
        arena_init_detached(&arena);
        syntax_depth(&arena, syntax_operations[i]);
        arena_free(&arena);
    }
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
