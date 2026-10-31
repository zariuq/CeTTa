#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_query.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Faults affect only the inspected operation. The rest of the runtime uses
 * the real allocator, including cleanup after an incomplete observation. */
static unsigned fail_allocator, failed_allocations;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t size) {
    if (fail_allocator == 1u) {
        failed_allocations++;
        return NULL;
    }
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
    if (fail_allocator == 2u) {
        failed_allocations++;
        return NULL;
    }
    return __real_calloc(count, size);
}
void *__wrap_realloc(void *old, size_t size) {
    if (fail_allocator == 3u) {
        failed_allocations++;
        return NULL;
    }
    return __real_realloc(old, size);
}

typedef struct { VarId variable; size_t visits; } Observation;

static bool observes_variable(const Atom *atom, void *context) {
    Observation *observation = context;
    observation->visits++;
    return atom->kind == ATOM_VAR &&
        var_id_eq(atom->var_id, observation->variable);
}

static Atom *shared_term(Arena *arena, Atom *leaf, unsigned depth, unsigned width) {
    for (unsigned i = 0u; i < depth; i++) {
        Atom **parts = arena_alloc(arena, width * sizeof(*parts));
        for (unsigned j = 0u; j < width; j++)
            parts[j] = leaf;
        leaf = atom_expr(arena, parts, width);
    }
    return leaf;
}

static unsigned observation_controls(Arena *arena, Atom *variable, Atom *absent) {
    Atom *dag = shared_term(arena, variable, 40u, 2u);
    Observation observed = {.variable = absent->var_id};
    bool complete = true;
    assert(!atom_graph_any_checked(dag, observes_variable, NULL, &observed, 128u,
        &complete) && complete && observed.visits <= 81u);
    observed.visits = 0u;
    assert(!atom_graph_any_checked(dag, observes_variable, NULL, &observed, 16u,
        &complete) && !complete && observed.visits == 16u);
    observed = (Observation){.variable = variable->var_id};
    assert(!atom_graph_any_checked(variable, observes_variable, NULL, &observed,
        0u, &complete) && !complete && !observed.visits);
    assert(atom_graph_any_checked(variable, observes_variable, NULL, &observed,
        1u, &complete) && complete && observed.visits == 1u);
    assert(!atom_graph_any_checked(dag, NULL, NULL, NULL, 128u, &complete) && !complete);
    for (unsigned fault = 1u; fault <= 3u; fault++) {
        Atom *input = fault == 2u ? dag : shared_term(arena, variable, 5u, 40u);
        observed = (Observation){.variable = absent->var_id};
        failed_allocations = 0u;
        fail_allocator = fault;
        bool found = atom_graph_any_checked(input, observes_variable, NULL,
            &observed, 512u, &complete);
        fail_allocator = 0u;
        assert(!found && !complete && failed_allocations == 1u);
    }
    Atom *self = atom_expr(arena, &variable, 1u);
    self->expr.elems[0] = self;
    observed = (Observation){.variable = absent->var_id};
    assert(!atom_graph_any_checked(self, observes_variable, NULL, &observed, 1u,
        &complete) && complete && observed.visits == 1u);
    observed.visits = 0u;
    assert(!atom_graph_any(self, observes_variable, &observed) && observed.visits == 1u);
    observed.visits = 0u;
    assert(!atom_graph_any_checked(self, observes_variable, NULL, &observed, 0u,
        &complete) && !complete && !observed.visits);

    Atom *child = atom_expr(arena, &variable, 1u);
    Atom *root = atom_expr2(arena, child, variable);
    child->expr.elems[0] = root;
    observed = (Observation){.variable = variable->var_id};
    assert(atom_graph_any_checked(root, observes_variable, NULL, &observed, 3u,
        &complete) && complete && observed.visits == 3u);
    observed = (Observation){.variable = absent->var_id};
    assert(!atom_graph_any_checked(root, observes_variable, NULL, &observed, 3u,
        &complete) && complete && observed.visits == 3u);

    child->expr.elems[0] = child;
    observed = (Observation){.variable = variable->var_id};
    assert(atom_graph_any_checked(root, observes_variable, NULL, &observed, 3u,
        &complete) && complete && observed.visits == 3u);
    observed.visits = failed_allocations = 0u;
    fail_allocator = 2u;
    bool found = atom_graph_any_checked(root, observes_variable, NULL,
        &observed, 3u, &complete);
    fail_allocator = 0u;
    assert(!found && !complete && failed_allocations == 1u && observed.visits == 1u);
    observed.visits = 0u;
    assert(atom_graph_any_checked(root, observes_variable, NULL, &observed, 3u,
        &complete) && complete && observed.visits == 3u);

    AtomVisitSet overflow = {.len = SIZE_MAX};
    bool fault = false;
    assert(!atom_visit_set_add(&overflow, root, &fault) && fault);
    return 17u;
}

typedef struct {
    TermUniverse *universe;
    Space *target;
    unsigned applied;
} Applied;

static bool apply(void *context, const CettaNativeQueryEffect *effect,
                  const AtomId *ids, size_t count) {
    Applied *state = context;
    assert(effect->target == state->target && !effect->removal);
    for (size_t i = 0u; i < count; i++) {
        space_add(state->target, term_universe_get_atom(state->universe, ids[i]));
        state->applied++;
    }
    return true;
}

static void projection_control(unsigned depth, unsigned shape, bool accepted,
                               bool count_only) {
    Arena arena;
    arena_init(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    Space source, target;
    space_init_with_universe(&source, &universe);
    space_init_with_universe(&target, &universe);
    target.kind = SPACE_KIND_SET;
    Atom *p = atom_symbol(&arena, "p"), *q = atom_symbol(&arena, "q");
    Atom *a = atom_symbol(&arena, "a"), *b = atom_symbol(&arena, "b");
    Atom *c = atom_symbol(&arena, "c"), *d = atom_symbol(&arena, "d");
    space_add(&source, atom_expr3(&arena, p, a, b));
    space_add(&source, atom_expr3(&arena, p, d, b));
    space_add(&source, atom_expr3(&arena, q, b, c));
    assert(space_dense_facts(&source) == 3u);
    Atom *x = atom_var(&arena, "x"), *y = atom_var(&arena, "y");
    Atom *z = atom_var(&arena, "z"), *outside = atom_var(&arena, "outside");
    Atom *patterns[] = {atom_expr3(&arena, p, x, y), atom_expr3(&arena, q, y, z)};
    Atom *leaf = shape == 0u ? z
        : shape == 1u ? atom_expr(&arena,
            (Atom *[]){atom_symbol(&arena, "tuple"), x, y, z}, 4u)
        : shape == 2u ? outside : c;
    Atom *data = shared_term(&arena, leaf, depth, 2u);
    CettaNativeQueryEffect effect = {.target = &target, .data = data};
    Applied state = {.universe = &universe, .target = &target};
    Atom *answer = atom_symbol(&arena, "answer");
    CettaNativeQuery query = {.source = &source, .patterns = patterns,
        .pattern_count = 2u, .template_atom = answer,
        .consumer = CETTA_NATIVE_QUERY_EFFECTS, .effects = &effect,
        .effect_count = 1u, .apply = apply, .apply_context = &state,
        .count_only = count_only};
    CettaNativeQueryResult result;
    CettaNativeQueryStatus status = cetta_native_query_run(&query, &arena, &result);
    if (accepted) {
        assert(status == CETTA_NATIVE_QUERY_DONE && result.answers == 2u);
        assert(space_length64(&target) == (shape == 1u ? 2u : 1u));
        assert(state.applied >= 1u && state.applied <= 2u);
        assert((result.collection == NULL) == count_only);
        if (result.collection) {
            assert(result.collection->expr.len == 2u);
            for (unsigned i = 0u; i < 2u; i++)
                assert(atom_eq(result.collection->expr.elems[i], answer));
        }
    } else {
        assert(status == CETTA_NATIVE_QUERY_DECLINED && !state.applied &&
            !space_length64(&target));
        /* An input-specific refusal must not poison this source's next call. */
        effect.data = z;
        assert(cetta_native_query_run(&query, &arena, &result) == CETTA_NATIVE_QUERY_DONE);
        assert(result.answers == 2u && space_length64(&target) == 1u);
    }
    space_free(&target);
    space_free(&source);
    term_universe_free(&universe);
    arena_free(&arena);
}

int main(void) {
    assert(setenv("CETTA_SPACE_DENSE", "eager", 1) == 0);
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    Arena arena;
    arena_init(&arena);
    unsigned checks = observation_controls(&arena, atom_var(&arena, "present"),
        atom_var(&arena, "absent"));
    arena_free(&arena);
    const struct { unsigned depth, shape; bool accepted; } cases[] = {
        {0u, 0u, true}, {5u, 0u, true}, {40u, 0u, false}, {4096u, 0u, false},
        {0u, 1u, true}, {3u, 1u, true}, {40u, 1u, false},
        {0u, 2u, false}, {40u, 2u, false}, {4096u, 2u, false},
        {0u, 3u, true}, {40u, 3u, true}, {4096u, 3u, true},
    };
    for (unsigned i = 0u; i < sizeof(cases) / sizeof(*cases); i++)
    for (unsigned count = 0u; count < 2u; count++) {
        projection_control(cases[i].depth, cases[i].shape, cases[i].accepted, count);
        checks++;
    }
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    printf("PASS: %u graph/projection controls preserve refusal, effects and answer multiplicity\n",
        checks);
    return 0;
}
