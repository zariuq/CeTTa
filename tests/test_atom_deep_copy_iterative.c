#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "atom.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

enum { COPY_DEPTH = 100000 };

typedef struct {
    _Atomic bool *start;
    Arena source;
    Arena destination;
    Atom *held;
    Atom *value;
} HeldProjectionThread;

static void *project_held_on_worker(void *context) {
    HeldProjectionThread *worker = context;
    arena_init_detached(&worker->source);
    arena_init_detached(&worker->destination);
    while (!atomic_load_explicit(worker->start, memory_order_acquire))
        sched_yield();
    for (unsigned i = 0u; i < 256u; i++) {
        worker->value = atom_int(&worker->source, (int64_t)i);
        worker->held = atom_prime_held_wrap(&worker->source, worker->value);
        assert(worker->held && atom_prime_held_made());
        assert(atom_contains_prime_held(worker->held));
        assert(!atom_contains_prime_held(worker->value));
        assert(atom_prime_held_strip(&worker->destination, worker->held) ==
               worker->value);
    }
    return NULL;
}

/* Each worker owns its arenas; only the monotone held-tag summary is shared.
 * A joined worker's value must also project correctly on another thread. */
static void check_parallel_held_projection(void) {
    enum { WORKERS = 4 };
    _Atomic bool start = false;
    HeldProjectionThread workers[WORKERS] = {0};
    pthread_t threads[WORKERS];
    for (unsigned i = 0u; i < WORKERS; i++) {
        workers[i].start = &start;
        assert(pthread_create(&threads[i], NULL, project_held_on_worker,
                              &workers[i]) == 0);
    }
    atomic_store_explicit(&start, true, memory_order_release);
    for (unsigned i = 0u; i < WORKERS; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(atom_prime_held_made());
    Arena destination;
    arena_init_detached(&destination);
    for (unsigned i = 0u; i < WORKERS; i++) {
        assert(atom_contains_prime_held(workers[i].held));
        assert(atom_prime_held_strip(&destination, workers[i].held) ==
               workers[i].value);
        assert(atom_prime_held_strip(&destination, workers[i].value) ==
               workers[i].value);
        arena_free(&workers[i].destination);
        arena_free(&workers[i].source);
    }
    arena_free(&destination);
    puts("PASS: held values project across worker-owned arenas and joined threads");
}

typedef struct { unsigned visits, refuse_at; } TestGraphProjection;

static bool project_expression(void *context, Arena *arena, Atom *atom) {
    (void)context; (void)arena;
    return atom && atom->kind == ATOM_EXPR && atom->expr.len != 0u;
}

static Atom *project_held_payload(void *context, Arena *arena, Atom *atom,
                                  Atom **children, bool changed) {
    TestGraphProjection *trace = context;
    trace->visits++;
    if (trace->refuse_at && trace->visits == trace->refuse_at) return NULL;
    if (atom_prime_held_is(atom)) return children[1];
    return changed ? atom_expr_rebuild(arena, atom, children, atom->expr.len) : atom;
}

static void check_graph_projection(Arena *source, Arena *destination) {
    Atom *leaf = atom_int(source, 73);
    Atom *held = atom_prime_held_wrap(source, leaf);
    Atom *dag = held;
    for (unsigned i = 0u; i < 80u; i++) dag = atom_expr2(source, dag, dag);
    dag = atom_he_evaluated_copy(source, dag);
    TestGraphProjection trace = {0};
    Atom *image = atom_graph_image(&trace, destination, dag,
        project_expression, project_held_payload);
    assert(image && trace.visits == 81u && atom_is_he_evaluated(image));
    Atom *cursor = image;
    for (unsigned i = 0u; i < 80u; i++) {
        assert(cursor->expr.elems[0] == cursor->expr.elems[1]);
        cursor = cursor->expr.elems[0];
    }
    assert(cursor == leaf && atom_prime_held_is(held));
    Atom *stripped = atom_prime_held_strip(destination, dag);
    assert(stripped && stripped->expr.elems[0] == stripped->expr.elems[1]);
    trace = (TestGraphProjection){.refuse_at = 8u};
    assert(!atom_graph_image(&trace, destination, dag,
        project_expression, project_held_payload));
    assert(trace.visits == 8u && atom_prime_held_is(held));

    Atom *plain = atom_expr2(source, leaf, leaf);
    trace = (TestGraphProjection){0};
    assert(atom_graph_image(&trace, destination, plain,
        project_expression, project_held_payload) == plain);
    assert(trace.visits == 1u);
    Atom *deep = held;
    for (unsigned i = 0u; i < COPY_DEPTH; i++)
        deep = atom_expr2(source, atom_symbol(source, "projection-link"), deep);
    stripped = atom_prime_held_strip(destination, deep);
    assert(stripped);
    for (unsigned i = 0u; i < COPY_DEPTH; i++) stripped = stripped->expr.elems[1];
    assert(stripped == leaf);

    Arena cyclic;
    arena_init_detached(&cyclic);
    Atom *cycle = atom_expr2(&cyclic, leaf, leaf);
    cycle->expr.elems[1] = cycle;
    trace = (TestGraphProjection){0};
    assert(!atom_graph_image(&trace, destination, cycle,
        project_expression, project_held_payload));
    assert(cycle->expr.elems[1] == cycle);
    arena_free(&cyclic);
}

typedef struct {
    Atom *from;
    Atom *to;
} TestCopyResolution;

static Atom *resolve_test_atom(void *context, Atom *src) {
    TestCopyResolution *resolution = context;
    return resolution && src == resolution->from
        ? resolution->to : src;
}

static Atom *make_deep_shared_list(Arena *arena, Atom *payload) {
    Atom *list = atom_symbol(arena, "Nil");

    for (uint32_t i = 0; i < COPY_DEPTH; i++)
        list = atom_expr3(arena, atom_symbol(arena, "Cons"), payload, list);
    return list;
}

static void check_deep_shared_list(Atom *list) {
    Atom *first_payload = NULL;
    uint32_t count = 0;

    while (!atom_is_symbol(list, "Nil")) {
        assert(list != NULL);
        assert(list->kind == ATOM_EXPR);
        assert(list->expr.len == 3);
        assert(atom_is_symbol(list->expr.elems[0], "Cons"));
        if (!first_payload)
            first_payload = list->expr.elems[1];
        else
            assert(list->expr.elems[1] == first_payload);
        list = list->expr.elems[2];
        count++;
    }
    assert(count == COPY_DEPTH);
}

static void check_deep_print(Atom *list, bool petta) {
    FILE *stream = tmpfile();
    assert(stream != NULL);
    if (petta)
        atom_print_petta(list, stream);
    else
        atom_print(list, stream);
    long length = ftell(stream);
    assert(length > (long)COPY_DEPTH);
    rewind(stream);
    assert(fgetc(stream) == '(');
    assert(fseek(stream, -1L, SEEK_END) == 0);
    assert(fgetc(stream) == ')');
    fclose(stream);
}

static void check_copy_session_clone(Arena *source, Arena *destination) {
    Atom *roots[128];
    AtomDeepCopySession *session = atom_deep_copy_session_new(destination);
    assert(session && !atom_deep_copy_session_clone(NULL));
    for (size_t i = 0u; i < 128u; i++) {
        roots[i] = atom_expr2(source, atom_symbol(source, "clone-prefix"),
                             atom_int(source, (int64_t)i));
        assert(atom_deep_copy_session_copy(session, roots[i]));
    }
    AtomDeepCopySession *copy = atom_deep_copy_session_clone(session);
    assert(copy);
    for (size_t i = 0u; i < 128u; i++)
        assert(atom_deep_copy_session_forwarded(copy, roots[i]) ==
               atom_deep_copy_session_forwarded(session, roots[i]));
    Atom *suffix = atom_expr2(source, atom_symbol(source, "clone-suffix"),
                             roots[127]);
    Atom *suffix_copy = atom_deep_copy_session_copy(copy, suffix);
    assert(suffix_copy && suffix_copy->expr.elems[1] ==
           atom_deep_copy_session_forwarded(session, roots[127]));
    assert(!atom_deep_copy_session_forwarded(session, suffix));
    Atom *other = atom_expr2(source, atom_symbol(source, "source-only"), roots[0]);
    assert(atom_deep_copy_session_copy(session, other));
    assert(!atom_deep_copy_session_forwarded(copy, other));
    atom_deep_copy_session_free(session);
    assert(atom_deep_copy_session_copy(copy, suffix) == suffix_copy);
    assert(atom_deep_copy_session_copy(copy, other)->expr.elems[1] ==
           atom_deep_copy_session_forwarded(copy, roots[0]));
    atom_deep_copy_session_free(copy);

    /* Refusal cannot discard or partially overwrite either special transport.
     * Their resolver/region authority is deliberately not borrowed by a clone. */
    session = atom_deep_copy_session_new(destination);
    TestCopyResolution resolution = {.from = roots[0], .to = roots[1]};
    atom_deep_copy_session_set_resolver(session, resolve_test_atom, &resolution);
    assert(!atom_deep_copy_session_clone(session));
    assert(atom_eq(atom_deep_copy_session_copy(session, roots[0]), roots[1]));
    atom_deep_copy_session_free(session);
    Arena transient;
    arena_init(&transient);
    ArenaMark mark = arena_mark(&transient);
    Atom *dying = atom_expr2(&transient, atom_symbol(&transient, "dying"), roots[0]);
    session = atom_deep_copy_session_new(destination);
    assert(atom_deep_copy_session_forward_region(session, &transient, mark));
    assert(!atom_deep_copy_session_clone(session));
    Atom *survivor = atom_deep_copy_session_copy(session, dying);
    assert(survivor && atom_is_symbol(survivor->expr.elems[0], "dying"));
    atom_deep_copy_session_free(session);
    arena_free(&transient);
    assert(atom_eq(survivor->expr.elems[1], roots[0]));
    puts("PASS: cloned transport preserves shared prefix, independent suffix and refusal authority");
}

static void check_copy_session_relocation(void) {
    Arena source, original_output, destination, destination_inputs;
    arena_init(&source);
    arena_init(&original_output);
    arena_init(&destination);
    arena_init(&destination_inputs);
    AtomDeepCopySession *original = atom_deep_copy_session_new(&original_output);
    AtomDeepCopySession *relocation = atom_deep_copy_session_new(&destination);
    Atom *roots[128], *images[128], *values[128];
    for (size_t i = 0u; i < 128u; i++) {
        /* Equal payloads still have distinct source headers. */
        roots[i] = atom_expr2(&source, atom_symbol(&source, "relocated-key"),
                             atom_int(&source, (int64_t)(i % 2u)));
        values[i] = atom_deep_copy_session_copy(original, roots[i]);
        assert(values[i]);
    }
    Atom *longest = atom_expr(&source, NULL, 0u);
    for (unsigned i = 0u; i < 12u; i++)
        longest = atom_expr_prepend(&source, atom_int(&source, i), longest);
    Atom *shortest = atom_expr_suffix(&source, longest, longest->expr.len - 1u);
    /* This header views the existing checked front-slack allocation; the
     * preceding slot is inside that allocation even when already occupied. */
    shortest->structural_facts |= ATOM_STRUCTURAL_FRONT_SLACK;
    assert(longest != shortest && longest->expr.elems + longest->expr.len ==
           shortest->expr.elems + shortest->expr.len);
    Atom *long_value = atom_deep_copy_session_copy(original, longest);
    Atom *short_value = atom_deep_copy_session_copy(original, shortest);
    assert(long_value && short_value &&
           long_value->expr.elems + long_value->expr.len ==
           short_value->expr.elems + short_value->expr.len);
    assert(!atom_deep_copy_session_relocate(NULL, relocation));
    assert(!atom_deep_copy_session_relocate(original, original));
    const AtomDeepCopyArenaPair pairs[]={{&source,&destination_inputs},{&original_output,&destination}};
    assert(atom_deep_copy_session_set_arena_map(relocation,pairs,2u));
    AtomDeepCopySession *copy = atom_deep_copy_session_relocate(original, relocation);
    assert(copy && atom_deep_copy_session_targets(copy, &destination));
    for (size_t i = 0u; i < 128u; i++) {
        images[i] = atom_deep_copy_session_forwarded(relocation, roots[i]);
        Atom *value_image = atom_deep_copy_session_forwarded(relocation, values[i]);
        assert(images[i] && value_image &&
               atom_deep_copy_session_forwarded(copy, images[i]) == value_image);
        assert(atom_deep_copy_session_forwarded(original, roots[i]) == values[i]);
        if (i > 1u) assert(images[i] != images[i - 2u]);
    }
    Atom *long_image = atom_deep_copy_session_forwarded(relocation, longest);
    Atom *short_image = atom_deep_copy_session_forwarded(relocation, shortest);
    Atom *long_value_image = atom_deep_copy_session_forwarded(relocation, long_value);
    Atom *short_value_image = atom_deep_copy_session_forwarded(relocation, short_value);
    assert(long_image && short_image && long_value_image && short_value_image);
    assert(long_image->expr.elems + long_image->expr.len ==
           short_image->expr.elems + short_image->expr.len);
    assert(long_value_image->expr.elems + long_value_image->expr.len ==
           short_value_image->expr.elems + short_value_image->expr.len);
    atom_deep_copy_session_free(original);
    atom_deep_copy_session_free(relocation);
    arena_free(&source);
    arena_free(&original_output);
    for (size_t i = 0u; i < 128u; i++) {
        Atom *value = atom_deep_copy_session_copy(copy, images[i]);
        assert(value && atom_eq(value, images[i]));
    }
    assert(atom_deep_copy_session_copy(copy, long_image) == long_value_image);
    assert(atom_deep_copy_session_copy(copy, short_image) == short_value_image);
    Arena late;
    arena_init(&late);
    arena_set_older_generation(&late, &destination_inputs);
    Atom *new_view = atom_expr_suffix(&late, long_image, 3u);
    new_view->structural_facts |= ATOM_STRUCTURAL_FRONT_SLACK;
    Atom *new_value = atom_deep_copy_session_copy(copy, new_view);
    assert(new_value && atom_eq(new_value, new_view));
    assert(new_value->expr.elems + new_value->expr.len ==
           long_value_image->expr.elems + long_value_image->expr.len);
    atom_deep_copy_session_free(copy);
    arena_free(&late);
    arena_free(&destination);
    arena_free(&destination_inputs);

    /* Short-first copying can fix two images with different buffer ends.
     * Header values alone cannot authorize transporting the shared-end key. */
    for (unsigned fracture_values = 0u; fracture_values < 2u; fracture_values++) {
    arena_init(&source);
    arena_init(&original_output);
    arena_init(&destination);
    arena_init(&destination_inputs);
    original = atom_deep_copy_session_new(&original_output);
    relocation = atom_deep_copy_session_new(&destination);
    longest = atom_expr(&source, NULL, 0u);
    for (unsigned i = 0u; i < 12u; i++)
        longest = atom_expr_prepend(&source, atom_int(&source, i), longest);
    shortest = atom_expr_suffix(&source, longest, longest->expr.len - 1u);
    shortest->structural_facts |= ATOM_STRUCTURAL_FRONT_SLACK;
    long_value = atom_deep_copy_session_copy(original, longest);
    short_value = atom_deep_copy_session_copy(original, shortest);
    assert(atom_deep_copy_session_set_arena_map(relocation,pairs,2u));
    assert(long_value->expr.elems + long_value->expr.len ==
           short_value->expr.elems + short_value->expr.len);
    short_image = atom_deep_copy_session_copy(
        relocation, fracture_values ? short_value : shortest);
    long_image = atom_deep_copy_session_copy(
        relocation, fracture_values ? long_value : longest);
    assert(short_image->expr.elems + short_image->expr.len !=
           long_image->expr.elems + long_image->expr.len);
    assert(!atom_deep_copy_session_relocate(original, relocation));
    assert(atom_deep_copy_session_copy(original, longest) == long_value);
    assert(atom_deep_copy_session_copy(original, shortest) == short_value);
    atom_deep_copy_session_free(original);
    atom_deep_copy_session_free(relocation);
    arena_free(&source);
    arena_free(&original_output);
    arena_free(&destination);
    arena_free(&destination_inputs);
    }
    puts("PASS: relocated transport owns keys and values after retirement and refuses fractured shared ends");
}

static void check_copy_nested_buffer_views(void) {
    Arena input, output, mapped_input, mapped_output;
    arena_init(&input); arena_init(&output);
    arena_init(&mapped_input); arena_init(&mapped_output);
    Atom *parent = atom_expr_builder_begin(&input, 3u);
    parent->expr.elems[0] = atom_int(&input, 70);
    parent->expr.elems[1] = atom_int(&input, 71);
    parent->expr.elems[2] = atom_int(&input, 72);
    parent = atom_expr_builder_finish(&input, parent);
    Atom *suffix = atom_expr_suffix(&input, parent, 1u);
    /* The private parent is still under construction. The shared suffix
     * includes only its already-final tail, so replacing the head is safe. */
    parent->expr.elems[0] = suffix;
    parent = atom_expr_builder_finish(&input, parent);
    assert(parent->expr.elems[0] == suffix && parent != suffix);
    assert(parent->expr.elems + 3u == suffix->expr.elems + 2u);
    assert(!(parent->structural_facts & ATOM_STRUCTURAL_FRONT_SLACK));
    assert(!(suffix->structural_facts & ATOM_STRUCTURAL_FRONT_SLACK));
    AtomDeepCopySession *original = atom_deep_copy_session_new(&output);
    Atom *value = atom_deep_copy_session_copy(original, parent);
    assert(value && value->expr.elems[0] ==
           atom_deep_copy_session_forwarded(original, suffix));
    AtomDeepCopySession *relocation = atom_deep_copy_session_new(&mapped_output);
    const AtomDeepCopyArenaPair pairs[] = {
        {&input, &mapped_input}, {&output, &mapped_output},
    };
    assert(atom_deep_copy_session_set_arena_map(relocation, pairs, 2u));
    AtomDeepCopySession *copy = atom_deep_copy_session_relocate(original, relocation);
    assert(copy);
    Atom *parent_image = atom_deep_copy_session_forwarded(relocation, parent);
    Atom *suffix_image = atom_deep_copy_session_forwarded(relocation, suffix);
    Atom *value_image = atom_deep_copy_session_forwarded(relocation, value);
    assert(parent_image && suffix_image && value_image);
    assert(parent_image->expr.elems[0] == suffix_image);
    assert(parent_image->expr.elems + 3u == suffix_image->expr.elems + 2u);
    assert(parent_image->structural_facts == parent->structural_facts);
    assert(suffix_image->structural_facts == suffix->structural_facts);
    atom_deep_copy_session_free(original);
    atom_deep_copy_session_free(relocation);
    arena_free(&output); arena_free(&input);
    assert(suffix_image->expr.elems[0]->ground.ival == 71);
    assert(suffix_image->expr.elems[1]->ground.ival == 72);
    assert(atom_deep_copy_session_copy(copy, parent_image) == value_image);
    assert(atom_eq(parent_image, value_image));
    atom_deep_copy_session_free(copy);
    arena_free(&mapped_output); arena_free(&mapped_input);
    puts("PASS: nested ordinary suffix headers retain their actual backing end and summaries after source retirement");
}

static void check_copy_distinct_scalar_nodes(void) {
    Arena source, destination;
    arena_init(&source); arena_init(&destination);
    arena_set_runtime_kind(&source, CETTA_ARENA_RUNTIME_KIND_EVAL);
    arena_set_runtime_kind(&destination, CETTA_ARENA_RUNTIME_KIND_EVAL);
    Atom *evicted = atom_symbol(&source, "transport-evicted-symbol");
    for (unsigned i = 0u; i < 256u; i++) {
        char spelling[64];
        snprintf(spelling, sizeof(spelling), "transport-cache-fill-%u", i);
        (void)atom_symbol(&source, spelling);
    }
    Atom *live = atom_symbol(&source, "transport-evicted-symbol");
    Atom *first_int = atom_int(&source, 7);
    Atom *second_int = atom_int(&source, 7);
    assert(evicted != live && atom_eq(evicted, live));
    assert(evicted->arena_id == live->arena_id);
    assert(first_int != second_int && atom_eq(first_int, second_int));
    assert(first_int->arena_id == second_int->arena_id);
    assert(atom_symbol(&source, "transport-evicted-symbol") == live);
    Atom *root = atom_expr(&source,
        (Atom *[]){evicted, live, first_int, second_int}, 4u);
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    const AtomDeepCopyArenaPair pair = {&source, &destination};
    assert(atom_deep_copy_session_set_arena_map(session, &pair, 1u));
    Atom *image = atom_deep_copy_session_copy(session, root);
    assert(image && atom_eq(image, root));
    assert(image->expr.elems[0] != image->expr.elems[1]);
    assert(image->expr.elems[2] != image->expr.elems[3]);
    assert(atom_symbol(&destination, "transport-evicted-symbol") == image->expr.elems[1]);
    atom_deep_copy_session_free(session);
    arena_free(&source);
    assert(atom_eq(image->expr.elems[0], image->expr.elems[1]));
    assert(atom_eq(image->expr.elems[2], image->expr.elems[3]));
    assert(atom_symbol(&destination, "transport-evicted-symbol") == image->expr.elems[1]);
    arena_free(&destination);
    puts("PASS: distinct equal scalar nodes stay distinct and the captured live cache slot remains the factory result");
}

static void check_copy_retained_owner(void) {
    for (unsigned scalar_cache = 0u; scalar_cache < 2u; scalar_cache++) {
        Arena external, source, destination;
        arena_init(&external); arena_init(&source); arena_init(&destination);
        arena_set_runtime_kind(&external, CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_scalar_cache(&external, scalar_cache != 0u);
        arena_set_older_generation(&source, &external);
        arena_set_older_generation(&destination, &external);
        Atom *live = atom_symbol(&external, "retained-row-symbol");
        Atom *row = atom_expr2(&external,
            atom_int(&external, 100000), atom_int(&external, 100001));
        assert(atom_graph_is_closed_for_arena(&external, row));
        Atom *view = atom_expr_suffix(&source, row, 1u);
        assert(view->expr.elems + view->expr.len == row->expr.elems + row->expr.len);
        Atom *root = atom_expr(&source, (Atom *[]){row, live, view}, 3u);
        Atom *open = atom_expr2(&external, row, root);
        assert(!atom_graph_is_closed_for_arena(&external, open));
        AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
        const AtomDeepCopyArenaPair pairs[] = {
            {&source, &destination}, {&external, &external},
        };
        ArenaMark external_before = arena_mark(&external);
        assert(atom_deep_copy_session_set_arena_map(session, pairs, 2u));
        assert(atom_deep_copy_session_maps_arena(session, &external, &external));
        assert(atom_deep_copy_session_copy(session, row) == row);
        Atom *image = atom_deep_copy_session_copy(session, root);
        assert(image && image != root && image->expr.elems[0] == row &&
               image->expr.elems[1] == live);
        assert(image->expr.elems[2] != view &&
               image->expr.elems[2]->expr.elems == view->expr.elems);
        assert(atom_symbol(&external, "retained-row-symbol") == live);
        ArenaMark external_after = arena_mark(&external);
        assert(external_after.head == external_before.head &&
               external_after.used == external_before.used);
        assert(!atom_deep_copy_session_copy(session, open));
        atom_deep_copy_session_free(session);
        arena_free(&source);
        assert(image->expr.elems[0] == row && image->expr.elems[1] == live);
        assert(image->expr.elems[2]->expr.elems[0]->ground.ival == 100001);
        /* External storage stays live through every destination use. */
        arena_free(&destination); arena_free(&external);
    }
    puts("PASS: explicit external identity pairs preserve closed row/cache identities and refuse open graphs without allocation into the borrowed owner");
}

static void check_copy_arena_map(void) {
    for (unsigned scenario=0u;scenario<8u;scenario++) {
        unsigned profile=scenario/2u;
        Arena input, result, copied_input, copied_result, unknown;
        arena_init(&input); arena_init(&result);
        arena_init(&copied_input); arena_init(&copied_result); arena_init(&unknown);
        if (profile>0u) {
            arena_set_runtime_kind(&input,CETTA_ARENA_RUNTIME_KIND_EVAL);
            arena_set_runtime_kind(&result,CETTA_ARENA_RUNTIME_KIND_EVAL);
            arena_set_runtime_kind(&copied_input,CETTA_ARENA_RUNTIME_KIND_EVAL);
            arena_set_runtime_kind(&copied_result,CETTA_ARENA_RUNTIME_KIND_EVAL);
        }
        if (profile==2u) {
            arena_set_scalar_cache(&input,true); arena_set_scalar_cache(&result,true);
            arena_set_scalar_cache(&copied_input,true); arena_set_scalar_cache(&copied_result,true);
        }
        if (profile==3u) {
            arena_set_older_generation(&result,&input);
            arena_set_older_generation(&copied_result,&copied_input);
        }
        Atom *input_head=atom_symbol(&input,"region-head");
        Atom *result_head=atom_symbol(&result,"region-head");
        assert(input_head!=result_head && atom_eq(input_head,result_head));
        Atom *small=atom_int(&input,7);
        Atom *payload=atom_expr2(&input,input_head,small);
        Atom *equal_payload=atom_expr2(&input,input_head,small);
        assert(payload!=equal_payload && atom_eq(payload,equal_payload));
        Atom *named=atom_var_with_name_key(&input,payload,var_id_from_base(UINT64_C(440051)));
        Atom *key=named->name_key;
        assert(key==payload);
        Atom global=*small; global.arena_id=0u;
        Atom *root=atom_expr(&result,(Atom *[]){result_head,payload,equal_payload,named,key,&global},6u);
        bool input_closed=atom_graph_is_closed_for_arena(&input,payload);
        bool result_closed=atom_graph_is_closed_for_arena(&result,payload);
        bool input_settled=atom_settled_for_arena(&input,payload);
        bool result_settled=atom_settled_for_arena(&result,payload);
        AtomDeepCopySession *session=atom_deep_copy_session_new(&copied_result);
        const AtomDeepCopyArenaPair collapsed[]={{&result,&copied_result},{&input,&copied_result}};
        assert(!atom_deep_copy_session_set_arena_map(session,collapsed,2u));
        const AtomDeepCopyArenaPair pairs[]={{&result,&copied_result},{&input,&copied_input}};
        assert(atom_deep_copy_session_set_arena_map(session,pairs,2u));
        assert(!atom_deep_copy_session_set_arena_map(session,pairs,2u));
        assert(atom_deep_copy_session_maps_arena(session,&input,&copied_input));
        assert(!atom_deep_copy_session_maps_arena(session,&input,&copied_result));
        assert(!atom_deep_copy_session_clone(session));
        Atom *image=atom_deep_copy_session_copy(session,root);
        assert(image && image->expr.len==6u && atom_eq(image,root));
        Atom *mapped=atom_deep_copy_session_forwarded(session,payload);
        assert(mapped && mapped==image->expr.elems[1]);
        assert(mapped!=image->expr.elems[2] && atom_eq(mapped,image->expr.elems[2]));
        assert(image->expr.elems[3]->name_key==image->expr.elems[4]);
        assert(image->expr.elems[5]==&global);
        assert(mapped->expr.elems[0]!=image->expr.elems[0]);
        assert(atom_graph_is_closed_for_arena(&copied_input,mapped)==input_closed);
        assert(atom_graph_is_closed_for_arena(&copied_result,mapped)==result_closed);
        assert(atom_settled_for_arena(&copied_input,mapped)==input_settled);
        assert(atom_settled_for_arena(&copied_result,mapped)==result_settled);
        if (profile>0u) {
            assert(atom_symbol(&input,"region-head")==input_head);
            assert(atom_symbol(&copied_input,"region-head")==mapped->expr.elems[0]);
            assert(atom_symbol(&copied_result,"region-head")==image->expr.elems[0]);
        }
        if (profile==2u)
            assert(atom_int(&copied_input,7)==atom_deep_copy_session_forwarded(session,small));
        assert(atom_deep_copy_session_copy(session,root)==image);
        if (scenario%2u==0u) {
            Atom *outside=atom_expr2(&unknown,atom_symbol(&unknown,"unmapped"),root);
            assert(!atom_deep_copy_session_copy(session,outside));
            /* A failed graph is unpublished; no subsequent root may escape
             * the session's sticky failure, even if it was copied earlier. */
            assert(!atom_deep_copy_session_copy(session,root));
        } else {
            /* Staleness has its own fresh session: a prior copy refusal
             * must not mask failure to validate the captured arena epoch. */
            assert(atom_deep_copy_session_maps_arena(session,&result,&copied_result));
            arena_reset(&input,arena_mark(&input));
            assert(!atom_deep_copy_session_copy(session,root));
            assert(!atom_deep_copy_session_maps_arena(session,&result,&copied_result));
        }
        atom_deep_copy_session_free(session);
        arena_free(&result); arena_free(&input); arena_free(&unknown);
        assert(image->expr.elems[3]->name_key==image->expr.elems[4]);
        assert(atom_is_symbol(mapped->expr.elems[0],"region-head"));
        arena_free(&copied_result); arena_free(&copied_input);
    }
    puts("PASS: region transport preserves cache ownership, closure, generations, name-key sharing and global lifetime; stale/unmapped/collapsed captures refuse");
}

static void check_copy_destination_scope(void) {
    for (unsigned stale = 0u; stale < 3u; stale++) {
        Arena source, child_source, destination, child_destination, external, wrong;
        Arena *owners[] = {&source, &child_source, &destination,
            &child_destination, &external, &wrong};
        for (size_t i = 0u; i < sizeof(owners) / sizeof(*owners); i++) {
            arena_init(owners[i]); arena_set_hashcons(owners[i], NULL);
        }
        Atom *root = atom_expr2(&child_source, atom_symbol(&child_source, "child"),
                               atom_int(&child_source, 19));
        AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
        AtomDeepCopyArenaPair collapsed[] = {{&source, &destination},
            {&child_source, &destination}};
        assert(!atom_deep_copy_session_set_arena_map(session, collapsed, 2u));
        AtomDeepCopyArenaPair pairs[] = {{&source, &destination},
            {&child_source, &child_destination}, {&external, &external}};
        assert(atom_deep_copy_session_set_arena_map(session, pairs, 3u));
        AtomDeepCopyDestinationScope outer = {0}, inner = {0}, refused = {0};
        size_t external_bytes = arena_accounted_live_bytes(&external);
        assert(!atom_deep_copy_destination_enter(session, &external, &refused));
        assert(!atom_deep_copy_destination_enter(session, &wrong, &refused));
        assert(!refused.active && atom_deep_copy_session_targets(session, &destination));
        assert(arena_accounted_live_bytes(&external) == external_bytes);
        assert(!atom_deep_copy_session_targets(session, &child_destination));
        assert(atom_deep_copy_destination_enter(session, &child_destination, &outer));
        assert(atom_deep_copy_session_targets(session, &child_destination));
        assert(!atom_deep_copy_session_targets(session, &destination));
        assert(!atom_deep_copy_destination_enter(session, &destination, &outer));
        Atom *image = atom_deep_copy_session_copy(session, root);
        assert(image && arena_owns_atom(&child_destination, image));
        CettaFrameIdentity identity = CETTA_FRAME_IDENTITY_NONE;
        assert(cetta_frame_identity_acquire(&identity));
        assert(atom_deep_copy_session_retain_frame(session, identity));
        cetta_frame_identity_release(identity);
        assert(atom_deep_copy_destination_enter(session, &destination, &inner));
        assert(!atom_deep_copy_destination_leave(&outer));
        assert(atom_deep_copy_session_targets(session, &destination));
        assert(atom_deep_copy_session_copy(session, root) == image);
        assert(atom_deep_copy_destination_leave(&inner));
        assert(atom_deep_copy_session_targets(session, &child_destination));
        Atom *unmapped = atom_expr2(&wrong, atom_symbol(&wrong, "unmapped"), root);
        assert(!atom_deep_copy_session_copy(session, unmapped));
        assert(atom_deep_copy_session_targets(session, &child_destination));
        if (stale) {
            Arena *changed = stale == 1u ? &child_source : &child_destination;
            arena_reset(changed, arena_mark(changed));
            assert(!atom_deep_copy_session_targets(session, &child_destination));
            assert(!atom_deep_copy_destination_enter(session, &destination, &refused));
            assert(!atom_deep_copy_session_copy(session, root));
        }
        assert(atom_deep_copy_destination_leave(&outer));
        assert(atom_deep_copy_destination_leave(&outer));
        assert(stale || atom_deep_copy_session_targets(session, &destination));
        assert(arena_accounted_live_bytes(&external) == external_bytes);
        atom_deep_copy_session_free(session);
        arena_free(&source); arena_free(&child_source); arena_free(&destination);
        /* The old primary owner cannot be the only holder of the child's
         * metadata identity. It must survive under the child's actual owner. */
        assert(cetta_frame_identity_retain(identity));
        cetta_frame_identity_release(identity);
        assert(image->expr.elems[1]->ground.ival == 19);
        arena_free(&child_destination);
        assert(!cetta_frame_identity_retain(identity));
        arena_free(&external); arena_free(&wrong);
    }
    Arena external, source, destination;
    arena_init(&external); arena_init(&source); arena_init(&destination);
    AtomDeepCopySession *session = atom_deep_copy_session_new(&external);
    AtomDeepCopyArenaPair pairs[] = {{&external, &external}, {&source, &destination}};
    assert(atom_deep_copy_session_set_arena_map(session, pairs, 2u));
    AtomDeepCopyDestinationScope scope = {0};
    size_t before = arena_accounted_live_bytes(&external);
    assert(!atom_deep_copy_destination_enter(session, &external, &scope));
    assert(!atom_deep_copy_session_targets(session, &external));
    CettaFrameIdentity identity = CETTA_FRAME_IDENTITY_NONE;
    assert(cetta_frame_identity_acquire(&identity));
    assert(!atom_deep_copy_session_retain_frame(session, identity));
    assert(arena_accounted_live_bytes(&external) == before);
    assert(atom_deep_copy_destination_enter(session, &destination, &scope));
    assert(atom_deep_copy_session_retain_frame(session, identity));
    assert(atom_deep_copy_destination_leave(&scope));
    cetta_frame_identity_release(identity);
    atom_deep_copy_session_free(session);
    arena_free(&external); arena_free(&source);
    assert(cetta_frame_identity_retain(identity));
    cetta_frame_identity_release(identity);
    arena_free(&destination);
    assert(!cetta_frame_identity_retain(identity));
    puts("PASS: mapped destination scopes retain one memo, nested restoration and child-owned identity pins; borrowed, collapsed, unmapped and reset owners refuse");
}

static void check_copy_metadata_identities(void) {
    static const char frame_kind, other_kind;
    Arena source, destination;
    arena_init_detached(&source); arena_init_detached(&destination);
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pair = {&source, &destination};
    assert(session && atom_deep_copy_session_set_arena_map(session, &pair, 1u));
    enum { NODE_COUNT = 257 };
    uint64_t *keys[NODE_COUNT], *images[NODE_COUNT];
    for (size_t i = 0u; i < NODE_COUNT; i++) {
        keys[i] = arena_alloc(&source, sizeof(*keys[i]));
        images[i] = arena_alloc(&destination, sizeof(*images[i]));
        assert(keys[i] && images[i]);
        *keys[i] = *images[i] = 7u;
        assert(atom_deep_copy_session_object_record(session, &frame_kind,
            keys[i], images[i], sizeof(*keys[i])));
    }
    for (size_t i = 0u; i < NODE_COUNT; i++) {
        assert(atom_deep_copy_session_object_forwarded(session, &frame_kind,
            keys[i], sizeof(*keys[i])) == images[i]);
        assert(atom_deep_copy_session_object_record(session, &frame_kind,
            keys[i], images[i], sizeof(*keys[i])));
    }
    assert(!atom_deep_copy_session_object_record(session, &frame_kind,
        keys[0], images[1], sizeof(*keys[0])));
    assert(!atom_deep_copy_session_object_record(session, &frame_kind,
        keys[0], images[0], 1u));
    uint64_t extra = 7u;
    assert(!atom_deep_copy_session_object_record(session, &frame_kind,
        &extra, images[0], sizeof(extra)));
    assert(!atom_deep_copy_session_object_forwarded(session, &frame_kind, keys[0], 1u));
    assert(!atom_deep_copy_session_object_forwarded(session, &other_kind,
        keys[0], sizeof(*keys[0])));
    assert(atom_deep_copy_session_object_record(session, &other_kind,
        keys[0], images[1], sizeof(*keys[0])));
    assert(atom_deep_copy_session_object_forwarded(session, &other_kind,
        keys[0], sizeof(*keys[0])) == images[1]);
    assert(!atom_deep_copy_session_clone(session));
    const Arena *owner = NULL;
    Arena *image_owner = NULL;
    assert(atom_deep_copy_session_owner_of_span(session, keys[0], sizeof(*keys[0]),
        NULL, &owner, &image_owner) && owner == &source && image_owner == &destination);
    assert(!atom_deep_copy_session_owner_of_span(session, &extra, sizeof(extra),
        &source, &owner, &image_owner));
    assert(!atom_deep_copy_session_owner_of_span(session, keys[0], SIZE_MAX,
        &source, &owner, &image_owner));
    assert(atom_deep_copy_session_destination_for_owner(session, &source, &image_owner) &&
        image_owner == &destination);
    ArenaMark mark = arena_mark(&destination);
    (void)arena_alloc(&destination, 16u);
    arena_reset(&destination, mark);
    assert(!atom_deep_copy_session_object_forwarded(session, &frame_kind,
        keys[0], sizeof(*keys[0])));
    assert(!atom_deep_copy_session_object_record(session, &frame_kind,
        &extra, images[0], sizeof(extra)));
    assert(!atom_deep_copy_session_destination_for_owner(session, &source, &image_owner));
    atom_deep_copy_session_free(session);
    arena_free(&destination); arena_free(&source);
}

typedef struct {
    unsigned holds, retained, released;
} MetadataLifetime;

static void metadata_retain(const void *object) {
    MetadataLifetime *item = (MetadataLifetime *)object;
    item->holds++;
    item->retained++;
}

static void metadata_release(const void *object) {
    MetadataLifetime *item = (MetadataLifetime *)object;
    assert(item->holds > 0u);
    item->holds--;
    item->released++;
}

static void check_copy_metadata_lifetimes(void) {
    static const char kind;
    enum { COUNT = 257 };
    MetadataLifetime sources[COUNT] = {{0}}, images[COUNT] = {{0}};
    Arena destination;
    arena_init_detached(&destination);
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    assert(session);
    for (size_t i = 0u; i < COUNT; i++) {
        assert(atom_deep_copy_session_object_record_retained(session, &kind,
            &sources[i], &images[i], sizeof(sources[i]), metadata_retain, metadata_release));
        assert(atom_deep_copy_session_object_record_retained(session, &kind,
            &sources[i], &images[i], sizeof(sources[i]), metadata_retain, metadata_release));
        assert(sources[i].holds == 1u && sources[i].retained == 1u &&
               images[i].holds == 1u && images[i].retained == 1u);
        assert(atom_deep_copy_session_object_current(session, &kind, &sources[i]) &&
               atom_deep_copy_session_object_current(session, &kind, &images[i]));
        assert(atom_deep_copy_session_object_forwarded(session, &kind,
            &sources[i], sizeof(sources[i])) == &images[i]);
    }
    assert(!atom_deep_copy_session_clone(session));
    assert(!atom_deep_copy_session_object_record_retained(session, &kind,
        &sources[0], &images[1], sizeof(sources[0]), metadata_retain, metadata_release));
    assert(!atom_deep_copy_session_object_record_retained(session, &kind,
        &sources[0], &images[0], 1u, metadata_retain, metadata_release));
    assert(!atom_deep_copy_session_object_record_retained(session, &kind,
        &sources[0], &images[0], sizeof(sources[0]), NULL, metadata_release));
    assert(!atom_deep_copy_session_object_record_retained(session, &kind,
        &sources[0], &images[0], sizeof(sources[0]), metadata_retain, NULL));
    assert(!atom_deep_copy_session_object_record(session, &kind,
        &sources[0], &images[0], sizeof(sources[0])));
    ArenaMark mark = arena_mark(&destination);
    (void)arena_alloc(&destination, 16u);
    arena_reset(&destination, mark);
    assert(!atom_deep_copy_session_object_forwarded(session, &kind,
        &sources[0], sizeof(sources[0])));
    assert(!atom_deep_copy_session_object_original(session, &kind,
        &images[0], sizeof(images[0])));
    assert(!atom_deep_copy_session_object_current(session, &kind, &sources[0]) &&
           !atom_deep_copy_session_object_current(session, &kind, &images[0]));
    assert(!atom_deep_copy_session_object_record_retained(session, &kind,
        &sources[0], &images[0], sizeof(sources[0]), metadata_retain, metadata_release));
    atom_deep_copy_session_free(session);
    for (size_t i = 0u; i < COUNT; i++)
        assert(sources[i].holds == 0u && sources[i].retained == 1u &&
               sources[i].released == 1u && images[i].holds == 0u &&
               images[i].retained == 1u && images[i].released == 1u);
    arena_free(&destination);
}

typedef struct {
    Atom *variable;
    unsigned visited;
} MapProbe;

static Atom *map_probe_leaf(Arena *arena, Atom *atom, void *context) {
    MapProbe *probe = context;
    probe->visited++;
    if (atom->kind == ATOM_VAR)
        return probe->variable;
    return atom_deep_copy(arena, atom);
}

static void check_map(Arena *source, Arena *destination) {
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats before, after;
    cetta_runtime_stats_snapshot(&before);
#endif
    Atom *node = atom_var(source, "source-name");
    Atom *head = atom_symbol(source, "Pair");
    for (unsigned depth = 0; depth < COPY_DEPTH; depth++)
        node = atom_expr3(source, head, node, node);
    MapProbe first = {atom_var(destination, "first-image"), 0};
    MapProbe second = {atom_var(destination, "second-image"), 0};
    Atom *one = atom_deep_copy_mapped(destination, node, map_probe_leaf, &first);
    Atom *two = atom_deep_copy_mapped(destination, node, map_probe_leaf, &second);
    assert(first.visited == 2u && second.visited == 2u);
    AtomDeepCopySession *session = atom_deep_copy_session_new_mapped(
        destination, map_probe_leaf, &first);
    assert(session != NULL);
    Atom *left = atom_expr2(source, head, node);
    Atom *right = atom_expr2(source, node, head);
    Atom *left_copy = atom_deep_copy_session_copy(session, left);
    Atom *right_copy = atom_deep_copy_session_copy(session, right);
    assert(left_copy->expr.elems[1] == right_copy->expr.elems[0]);
    assert(first.visited == 4u);
    atom_deep_copy_session_free(session);

    /* Storage already belonging to the destination still needs mapping. */
    Atom *resident = atom_expr2(destination, head, first.variable);
    session = atom_deep_copy_session_new_mapped(
        destination, map_probe_leaf, &second);
    assert(!atom_deep_copy_session_settled(session, resident));
    Atom *mapped_resident = atom_deep_copy_session_copy(session, resident);
    assert(mapped_resident != resident);
    assert(mapped_resident->expr.elems[1] == second.variable);
    atom_deep_copy_session_free(session);

    for (unsigned depth = 0; depth < COPY_DEPTH; depth++) {
        assert(one->expr.elems[1] == one->expr.elems[2]);
        assert(two->expr.elems[1] == two->expr.elems[2]);
        one = one->expr.elems[1];
        two = two->expr.elems[1];
    }
    assert(one == first.variable && two == second.variable && one != two);
    MapProbe failure = {NULL, 0};
    assert(atom_deep_copy_mapped(destination, node, map_probe_leaf, &failure) == NULL);
    session = atom_deep_copy_session_new_mapped(destination, map_probe_leaf, &failure);
    assert(atom_deep_copy_session_copy(session, node) == NULL);
    unsigned visits = failure.visited;
    assert(atom_deep_copy_session_copy(session, head) == NULL);
    assert(failure.visited == visits);
    atom_deep_copy_session_free(session);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_snapshot(&after);
    uint64_t allocated = after.counters[CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES] -
        before.counters[CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES];
    uint64_t released = after.counters[CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES] -
        before.counters[CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES];
    assert(allocated > 0u && allocated == released);
#endif
}

typedef struct {
    unsigned releases;
} SessionInventoryProbe;

static void release_session_inventory(void *value) {
    SessionInventoryProbe *probe = value;
    assert(probe->releases == 0u);
    probe->releases++;
}

static void check_session_companion_cleanup(Arena *destination) {
    static const int first_kind = 1, second_kind = 2;
    for (unsigned fail = 0u; fail < 2u; ++fail) {
        Arena nursery;
        arena_init(&nursery);
        Atom *retained = atom_int(&nursery, 11);
        ArenaMark mark = arena_mark(&nursery);
        Atom *young = atom_expr2(&nursery, retained, atom_int(&nursery, 23));
        SessionInventoryProbe first = {0}, second = {0}, rejected = {0};
        AtomDeepCopySession *session = atom_deep_copy_session_new(destination);
        assert(session != NULL);
        assert(atom_deep_copy_session_forward_region(session, &nursery, mark));
        assert(!atom_deep_copy_session_forward_region(session, &nursery, mark));
        assert(atom_deep_copy_session_install_inventory(session, &first_kind,
            &first, release_session_inventory, NULL));
        assert(atom_deep_copy_session_install_inventory(session, &second_kind,
            &second, release_session_inventory, NULL));
        assert(!atom_deep_copy_session_install_inventory(session, &first_kind,
            &rejected, release_session_inventory, NULL));
        assert(atom_deep_copy_session_inventory(session, &first_kind) == &first);
        if (fail) {
            assert(atom_deep_copy_session_copy(session, NULL) == NULL);
            assert(atom_deep_copy_session_failed(session));
        } else {
            Atom *copy = atom_deep_copy_session_copy(session, young);
            assert(copy && atom_eq(copy->expr.elems[0], retained));
            assert(copy->expr.elems[1]->kind == ATOM_GROUNDED);
            assert(copy->expr.elems[1]->ground.gkind == GV_INT);
            assert(copy->expr.elems[1]->ground.ival == 23);
        }
        atom_deep_copy_session_free(session);
        assert(first.releases == 1u && second.releases == 1u);
        assert(rejected.releases == 0u);
        arena_reset(&nursery, mark);
        assert(retained->kind == ATOM_GROUNDED &&
               retained->ground.gkind == GV_INT && retained->ground.ival == 11);
        arena_free(&nursery);
    }
    atom_deep_copy_session_free(NULL);
}

/* A represented DAG must resume without expanding its occurrence tree or
 * restarting its copied prefix. The two grants produce the same endpoint and
 * exact decision debit as the joined grant. */
static void check_copy_slices(void) {
    enum { DEPTH = 256 };
    Arena source, joined, split;
    arena_init_detached(&source);arena_init_detached(&joined);arena_init_detached(&split);
    Atom *root=atom_int(&source,42);
    for (unsigned i=0u;i<DEPTH;i++) root=atom_expr2(&source,root,root);
    AtomDeepCopySession *one=atom_deep_copy_session_new(&joined);
    AtomDeepCopySession *many=atom_deep_copy_session_new(&split);
    Atom *whole=NULL,*pieces=NULL;
    uint64_t all=0u,used=99u,total=0u;
    assert(atom_deep_copy_session_advance(one,root,UINT64_MAX,&whole,&all)
        ==ATOM_DEEP_COPY_COMPLETE && whole && all==3u*DEPTH+1u);
    size_t bytes=arena_accounted_live_bytes(&split);
    assert(atom_deep_copy_session_advance(many,root,0u,&pieces,&used)
        ==ATOM_DEEP_COPY_PAUSED && !pieces && !used);
    assert(!atom_deep_copy_session_pending(many) &&
        arena_accounted_live_bytes(&split)==bytes);
    AtomDeepCopyStep step=ATOM_DEEP_COPY_PAUSED;
    for (unsigned turn=0u;step==ATOM_DEEP_COPY_PAUSED;turn++) {
        uint64_t grant=turn%7u+1u;
        step=atom_deep_copy_session_advance(many,root,grant,&pieces,&used);
        assert(used<=grant && used>0u);
        total+=used;
        assert(total<=all);
        if (step==ATOM_DEEP_COPY_PAUSED) {
            assert(!pieces && atom_deep_copy_session_pending(many));
            assert(!atom_deep_copy_session_forwarded(many,root));
            assert(!atom_deep_copy_session_clone(many));
            assert(!atom_deep_copy_session_relocate(many,one));
            assert(!atom_deep_copy_session_relocate(one,many));
            assert(!atom_deep_copy_session_copy(many,root));
            bytes=arena_accounted_live_bytes(&split);
            assert(atom_deep_copy_session_advance(many,root,0u,&pieces,&used)
                ==ATOM_DEEP_COPY_PAUSED && !pieces && !used);
            assert(arena_accounted_live_bytes(&split)==bytes);
            Atom *other=atom_int(&source,43);
            assert(atom_deep_copy_session_advance(many,other,1u,&pieces,&used)
                ==ATOM_DEEP_COPY_REFUSED && !pieces && !used);
            assert(!atom_deep_copy_session_failed(many));
            TestCopyResolution changed={.from=root,.to=other};
            atom_deep_copy_session_set_resolver(many,resolve_test_atom,&changed);
            assert(!atom_deep_copy_session_set_snapshot(many));
            AtomDeepCopyArenaPair pairs[]={{&source,&split}};
            assert(!atom_deep_copy_session_set_arena_map(many,pairs,1u));
            assert(!atom_deep_copy_session_forward_region(many,&source,arena_mark(&source)));
        }
    }
    assert(step==ATOM_DEEP_COPY_COMPLETE && total==all && pieces);
    assert(!atom_deep_copy_session_pending(many));
    assert(atom_deep_copy_session_copy(many,root)==pieces);
    assert(atom_deep_copy_session_advance(many,root,1u,&pieces,&used)
        ==ATOM_DEEP_COPY_COMPLETE && used==1u);
    Atom *left=whole,*right=pieces;
    for (unsigned i=0u;i<DEPTH;i++) {
        assert(left->kind==ATOM_EXPR && right->kind==ATOM_EXPR);
        assert(left->expr.len==2u && right->expr.len==2u);
        assert(left->expr.elems[0]==left->expr.elems[1]);
        assert(right->expr.elems[0]==right->expr.elems[1]);
        left=left->expr.elems[0];right=right->expr.elems[0];
    }
    assert(left->ground.ival==42 && right->ground.ival==42);
    atom_deep_copy_session_free(one);atom_deep_copy_session_free(many);
    arena_free(&source);
    assert(left->ground.ival==42 && right->ground.ival==42);
    arena_free(&joined);arena_free(&split);

    /* A destination reset cannot make the saved stack valid again. Refusal
     * happens before reading its reclaimed partial backing. */
    arena_init_detached(&source);arena_init_detached(&split);
    root=atom_expr2(&source,atom_int(&source,1),atom_int(&source,2));
    many=atom_deep_copy_session_new(&split);
    ArenaMark mark=arena_mark(&split);
    assert(atom_deep_copy_session_advance(many,root,1u,&pieces,&used)
        ==ATOM_DEEP_COPY_PAUSED);
    arena_reset(&split,mark);
    assert(atom_deep_copy_session_advance(many,root,1u,&pieces,&used)
        ==ATOM_DEEP_COPY_REFUSED && !pieces && !used);
    assert(atom_deep_copy_session_failed(many));
    assert(!atom_deep_copy_session_forwarded(many,root));
    atom_deep_copy_session_free(many);
    many=atom_deep_copy_session_new(&split);
    assert(atom_deep_copy_session_advance(many,root,1u,&pieces,&used)
        ==ATOM_DEEP_COPY_PAUSED);
    atom_deep_copy_session_free(many); /* Cancel before retiring the source. */
    arena_free(&source);arena_free(&split);

    /* One traversal decision can reserve a wide node's backing. A traversal
     * grant is therefore not an allocation or scalar-byte limit. */
    arena_init_detached(&source);arena_init_detached(&split);
    enum { WIDTH = 4096 };
    Atom *children[WIDTH];
    for (unsigned i=0u;i<WIDTH;i++) children[i]=atom_int(&source,i);
    root=atom_expr(&source,children,WIDTH);
    many=atom_deep_copy_session_new(&split);
    assert(atom_deep_copy_session_advance(many,root,1u,&pieces,&used)
        ==ATOM_DEEP_COPY_PAUSED && used==1u);
    assert(arena_accounted_live_bytes(&split)>=WIDTH*sizeof(Atom *));
    atom_deep_copy_session_free(many);

    /* A load-time admission cannot authorize a changed intern context. */
    many=atom_deep_copy_session_new(&split);
    assert(atom_deep_copy_session_advance(many,root,1u,&pieces,&used)
        ==ATOM_DEEP_COPY_PAUSED);
    SymbolTable replacement;
    symbol_table_init(&replacement);
    SymbolTable *previous=g_symbols;g_symbols=&replacement;
    assert(atom_deep_copy_session_advance(many,root,1u,&pieces,&used)
        ==ATOM_DEEP_COPY_REFUSED && !pieces && !used);
    g_symbols=previous;symbol_table_free(&replacement);
    atom_deep_copy_session_free(many);
    many=atom_deep_copy_session_new(&split);
    assert(atom_deep_copy_session_set_snapshot(many));
    assert(atom_deep_copy_session_advance(many,root,1u,&pieces,&used)
        ==ATOM_DEEP_COPY_REFUSED && !pieces && !used);
    assert(!atom_deep_copy_session_failed(many));
    assert(atom_deep_copy_session_copy(many,root));
    atom_deep_copy_session_free(many);
    arena_free(&source);arena_free(&split);
    puts("PASS: sliced graph copy retains one shared DAG, exact debit, inert zero grants and cancellation; changed roots/profiles/epochs refuse");
}

static void check_global_canonical_domain(void) {
    HashConsTable table;
    hashcons_init_compact(&table);
    HashConsTable *previous_hashcons=g_hashcons;
    g_hashcons=&table;
    Arena source,destination;
    arena_init_detached(&source);arena_init_detached(&destination);
    arena_set_hashcons(&source,&table);arena_set_hashcons(&destination,&table);
    SymbolId first_id=symbol_intern_cstr(g_symbols,"checkpoint-global-first");
    Atom *first=atom_symbol_canonical(first_id),*unit=atom_unit_canonical();
    assert(first && unit && !first->arena_id && !unit->arena_id &&
        symbol_canonical_count(g_symbols)>0u);
    AtomDeepCopySession *session=atom_deep_copy_session_new(&destination);
    size_t bytes=0u,work=0u;
    assert(session && atom_checkpoint_global_budget(&table,100000u,&bytes,&work));
    AtomDeepCopyGlobalInspection inspected={0};
    assert(atom_deep_copy_session_restrict_globals(session,&table,work,&inspected));
    AtomDeepCopyArenaPair pair={&source,&destination};
    assert(atom_deep_copy_session_set_runtime_arena_map(session,&pair,1u));
    assert(atom_deep_copy_session_copy(session,first)==first &&
        atom_deep_copy_session_copy(session,unit)==unit);

    SymbolId later_id=symbol_intern_cstr(g_symbols,"checkpoint-global-later");
    Atom *later=atom_symbol_canonical(later_id);
    assert(later && later!=first && symbol_canonical_count(g_symbols)>1u);
    size_t extra_bytes=0u,extra_work=0u;
    assert(atom_deep_copy_session_global_extension_budget(session,&table,100000u,
        &extra_bytes,&extra_work) && extra_bytes && extra_work);
    inspected=(AtomDeepCopyGlobalInspection){0};
    assert(atom_deep_copy_session_extend_globals(session,&table,100000u,extra_work,
        extra_bytes,&inspected) && atom_deep_copy_session_copy(session,later)==later &&
        atom_deep_copy_session_copy(session,first)==first &&
        atom_deep_copy_session_copy(session,unit)==unit);
    atom_deep_copy_session_free(session);
    arena_free(&destination);arena_free(&source);
    g_hashcons=previous_hashcons;hashcons_free(&table);
    puts("PASS: checkpoint global domains retain canonical symbols and unit and extend by published prefix");
}

int main(void) {
    SymbolTable symbols;
    Arena source;
    Arena destination;
    Atom *payload;
    Atom *list;
    Atom *copy;

    check_parallel_held_projection();
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    check_global_canonical_domain();
    check_copy_slices();
    check_copy_destination_scope();
    check_copy_metadata_identities();
    check_copy_metadata_lifetimes();
    arena_init(&source);
    arena_init(&destination);
    arena_set_runtime_kind(&source, CETTA_ARENA_RUNTIME_KIND_EVAL);
    arena_set_runtime_kind(&destination, CETTA_ARENA_RUNTIME_KIND_EVAL);
    check_copy_session_clone(&source, &destination);
    check_copy_session_relocation();
    check_copy_nested_buffer_views();
    check_copy_distinct_scalar_nodes();
    check_copy_retained_owner();
    check_copy_arena_map();
    /* Rebuilding preserves expression role, while ownership and variable
     * summaries are derived from the actual replacement children. */
    Atom *role_source=atom_he_evaluated_expr(&source,
        (Atom *[]){atom_symbol(&source,"RoleBox"),atom_int(&source,1)},2u);
    Atom *replacement=atom_var(&destination,"role-variable");
    Atom *role_copy=atom_expr_rebuild(&destination,role_source,
        (Atom *[]){atom_symbol(&destination,"RoleBox"),replacement},2u);
    assert(role_copy && atom_is_he_evaluated(role_copy) && atom_has_vars(role_copy));
    assert(role_copy->expr.elems[1]==replacement && role_copy->arena_id==destination.identity);
    Atom *plain_source=atom_expr2(&source,atom_symbol(&source,"RoleBox"),atom_int(&source,1));
    Atom *plain_copy=atom_expr_rebuild(&destination,plain_source,
        (Atom *[]){atom_symbol(&destination,"RoleBox"),atom_int(&destination,2)},2u);
    assert(plain_copy && !atom_is_he_evaluated(plain_copy) && !atom_has_vars(plain_copy));
    check_map(&source, &destination);
    check_session_companion_cleanup(&destination);

    /* A name in a reusable buffer is read by its spelling, not its address
     * (atom_is_symbol_named); atom_is_symbol is for static names. */
    char spelling[32];
    Atom *first_spelling = atom_symbol(&source, "mutable-spelling-first");
    Atom *second_spelling = atom_symbol(&source, "mutable-spelling-next");
    strcpy(spelling, "mutable-spelling-first");
    assert(atom_is_symbol_named(first_spelling, spelling));
    assert(!atom_is_symbol_named(second_spelling, spelling));
    strcpy(spelling, "mutable-spelling-next");
    assert(atom_is_symbol_named(second_spelling, spelling));
    assert(!atom_is_symbol_named(first_spelling, spelling));
    assert(atom_is_symbol(first_spelling, "mutable-spelling-first"));
    assert(!atom_is_symbol(second_spelling, "mutable-spelling-first"));

    /* Names 64 bytes apart collide in the literal cache. Hits, evictions
       and fallback lookup must all preserve positive and negative tests. */
    static const char colliding_names[][64] = {
        "literal-cache-left", "literal-cache-right", "literal-cache-third"
    };
    Atom *colliding_symbols[3];
    for (unsigned i = 0u; i < 3u; i++)
        colliding_symbols[i] = atom_symbol(&source, colliding_names[i]);
    for (unsigned turn = 0u; turn < 30u; turn++) {
        for (unsigned i = 0u; i < 3u; i++) {
            assert(atom_eq(atom_symbol_literal(&source, colliding_names[i]),
                           colliding_symbols[i]));
            assert(atom_is_symbol(colliding_symbols[i], colliding_names[i]));
            assert(!atom_is_symbol(colliding_symbols[i],
                                   colliding_names[(i + 1u) % 3u]));
        }
    }
    /* A generic symbol predicate also accepts mutable names. Unlike a
       literal constructor, it must not cache the buffer's address. */
    char mutable_name[64];
    strcpy(mutable_name, colliding_names[0]);
    assert(atom_is_symbol(colliding_symbols[0], mutable_name));
    strcpy(mutable_name, colliding_names[1]);
    assert(atom_is_symbol(colliding_symbols[1], mutable_name));
    assert(!atom_is_symbol(colliding_symbols[0], mutable_name));
    assert(!atom_is_symbol(atom_int(&source, 1), mutable_name));
    SymbolTable other_symbols;
    symbol_table_init(&other_symbols);
    (void)symbol_intern_cstr(&other_symbols, "different-table-prefix");
    g_symbols = &other_symbols;
    Atom *other_literal = atom_symbol_literal(&destination, colliding_names[0]);
    assert(atom_eq(other_literal, atom_symbol(&destination, colliding_names[0])));
    assert(atom_is_symbol(other_literal, colliding_names[0]));
    assert(!atom_is_symbol(other_literal, colliding_names[1]));
    g_symbols = &symbols;
    assert(atom_is_symbol(colliding_symbols[0], colliding_names[0]));
    symbol_table_free(&other_symbols);
    /* Reusing a table's address does not reuse its literal-symbol identities. */
    symbol_table_init(&other_symbols);
    (void)symbol_intern_cstr(&other_symbols, "reused-table-prefix-one");
    (void)symbol_intern_cstr(&other_symbols, "reused-table-prefix-two");
    g_symbols = &other_symbols;
    Atom *reused_literal = atom_symbol_literal(&destination, colliding_names[0]);
    assert(reused_literal->symbol_number != other_literal->symbol_number);
    assert(atom_eq(reused_literal, atom_symbol(&destination, colliding_names[0])));
    assert(atom_is_symbol(reused_literal, colliding_names[0]));
    assert(!atom_is_symbol(reused_literal, colliding_names[1]));
    g_symbols = &symbols;
    symbol_table_free(&other_symbols);

    /* Immutable symbols are canonical only within their owning arena. */
    Atom *source_symbol_first = atom_symbol(&source, "arena-symbol-cache");
    Atom *source_symbol_second = atom_symbol(&source, "arena-symbol-cache");
    Atom *destination_symbol =
        atom_symbol(&destination, "arena-symbol-cache");
    assert(source_symbol_first == source_symbol_second);
    assert(source_symbol_first != destination_symbol);
    assert(atom_eq(source_symbol_first, destination_symbol));

    /* A reset invalidates cached atoms from the prior allocation epoch. */
    Arena reset_probe;
    arena_init(&reset_probe);
    arena_set_runtime_kind(&reset_probe, CETTA_ARENA_RUNTIME_KIND_EVAL);
    ArenaMark reset_origin = arena_mark(&reset_probe);
    Atom *before_reset = atom_symbol_literal(&reset_probe, "reset-symbol-cache");
    arena_reset(&reset_probe, reset_origin);
    (void)arena_alloc(&reset_probe, sizeof(Atom));
    Atom *after_reset = atom_symbol_literal(&reset_probe, "reset-symbol-cache");
    assert(after_reset != before_reset);
    assert(arena_owns_atom(&reset_probe, after_reset));
    assert(atom_is_symbol(after_reset, "reset-symbol-cache"));
    arena_free(&reset_probe);

    /* Incremental construction fills the final combined allocation directly;
       finishing under a different arena is rejected. */
    Atom *builder_draft = atom_expr_builder_begin(&source, 2u);
    assert(builder_draft != NULL);
    builder_draft->expr.elems[0] = atom_symbol(&source, "builder");
    builder_draft->expr.elems[1] = atom_var(&source, "builder-value");
    assert(atom_expr_builder_finish(&destination, builder_draft) == NULL);
    Atom *builder_result = atom_expr_builder_finish(&source, builder_draft);
    assert(builder_result == builder_draft);
    assert(builder_result->expr.elems == (Atom **)(builder_result + 1));
    assert(atom_has_vars(builder_result));
    assert(atom_is_symbol(builder_result->expr.elems[0], "builder"));

    payload = atom_expr2(&source, atom_symbol(&source, "payload"),
                         atom_int(&source, 7));
    list = make_deep_shared_list(&source, payload);
    copy = atom_deep_copy(&destination, list);
    assert(copy != NULL);
    assert(copy != list);
    check_deep_shared_list(copy);
    check_deep_print(copy, false);
    check_deep_print(copy, true);

    Atom *shared = atom_expr2(
        &source, atom_symbol(&source, "shared"),
        atom_int(&source, 11));
    Atom *left = atom_expr2(
        &source, atom_symbol(&source, "left"), shared);
    Atom *right = atom_expr2(
        &source, atom_symbol(&source, "right"), shared);
    Atom *equal_left = atom_expr2(
        &source, atom_symbol(&source, "equal"),
        atom_int(&source, 12));
    Atom *equal_right = atom_expr2(
        &source, atom_symbol(&source, "equal"),
        atom_int(&source, 12));
    assert(equal_left != equal_right);

    AtomDeepCopySession *session =
        atom_deep_copy_session_new(&destination);
    assert(session != NULL);
    assert(atom_deep_copy_session_forwarded(session, left) == NULL);
    assert(atom_deep_copy_session_forwarded(session, shared) == NULL);
    Atom *left_copy = atom_deep_copy_session_copy(session, left);
    assert(atom_deep_copy_session_forwarded(session, left) == left_copy);
    assert(atom_deep_copy_session_forwarded(session, shared) ==
           left_copy->expr.elems[1]);
    Atom *right_copy = atom_deep_copy_session_copy(session, right);
    Atom *equal_left_copy =
        atom_deep_copy_session_copy(session, equal_left);
    Atom *equal_right_copy =
        atom_deep_copy_session_copy(session, equal_right);
    assert(left_copy != NULL && right_copy != NULL);
    assert(left_copy->expr.elems[1] == right_copy->expr.elems[1]);
    assert(equal_left_copy != equal_right_copy);
    assert(atom_deep_copy_session_copy(session, left_copy) == left_copy);
    atom_deep_copy_session_free(session);

    Atom *suspension = atom_expr2(
        &source, atom_symbol(&source, "suspended"),
        atom_int(&source, 13));
    Atom *resolved = atom_expr2(
        &source, atom_symbol(&source, "resolved"),
        atom_int(&source, 21));
    Atom *wrapped = atom_expr2(
        &source, atom_symbol(&source, "wrapped"), suspension);
    TestCopyResolution resolution = {
        .from = suspension,
        .to = resolved,
    };
    session = atom_deep_copy_session_new(&destination);
    assert(session != NULL);
    atom_deep_copy_session_set_resolver(
        session, resolve_test_atom, &resolution);
    Atom *wrapped_copy = atom_deep_copy_session_copy(session, wrapped);
    assert(wrapped_copy != NULL);
    assert(atom_eq(wrapped_copy->expr.elems[1], resolved));
    assert(atom_deep_copy_session_forwarded(session, suspension) == NULL);
    assert(atom_deep_copy_session_forwarded(session, resolved) ==
           wrapped_copy->expr.elems[1]);
    atom_deep_copy_session_free(session);

    /*
     * A young expression may point into an immutable older generation.
     * Promotion must copy the young parent while retaining the old child
     * exactly; structural equality alone would not catch repeated copying.
     */
    Arena nursery;
    arena_init(&nursery);
    Atom *old_child = atom_expr2(
        &destination, atom_symbol(&destination, "old"),
        atom_int(&destination, 17));
    Atom *young_parent = atom_expr2(
        &nursery, atom_symbol(&nursery, "young"), old_child);
    Atom *young_child = atom_expr2(
        &nursery, atom_symbol(&nursery, "temporary"),
        atom_int(&nursery, 23));
    Atom *mixed_parent = atom_expr2(
        &destination, atom_symbol(&destination, "mixed"), young_child);
    uint32_t mixed_min_id = 0u;
    uint32_t mixed_max_id = 0u;
    assert(atom_graph_arena_id_bounds(
        young_parent, &mixed_min_id, &mixed_max_id));
    assert(mixed_min_id ==
           (destination.identity < nursery.identity
                ? destination.identity : nursery.identity));
    assert(mixed_max_id ==
           (destination.identity > nursery.identity
                ? destination.identity : nursery.identity));
    int external_space_identity = 0;
    Atom *identity_resource = atom_space(
        &nursery, &external_space_identity);
    assert(!atom_graph_arena_id_bounds(
        identity_resource, &mixed_min_id, &mixed_max_id));
    assert(arena_owns_atom(&destination, old_child));
    assert(!arena_owns_atom(&nursery, old_child));
    assert(arena_owns_atom(&destination, mixed_parent));
    assert(!arena_owns_atom(&destination, young_child));
    session = atom_deep_copy_session_new(&destination);
    assert(session != NULL);
    Atom *promoted_parent =
        atom_deep_copy_session_copy(session, young_parent);
    Atom *closed_mixed_parent =
        atom_deep_copy_session_copy(session, mixed_parent);
    assert(promoted_parent != NULL);
    assert(promoted_parent != young_parent);
    assert(promoted_parent->expr.elems[1] == old_child);
    /*
     * Shallow ownership is not closure: a destination-owned wrapper around a
     * nursery child must be traversed and repaired before the nursery dies.
     */
    assert(closed_mixed_parent != NULL);
    assert(closed_mixed_parent != mixed_parent);
    assert(closed_mixed_parent->expr.elems[1] != young_child);
    assert(arena_owns_atom(
        &destination, closed_mixed_parent->expr.elems[1]));
    assert(atom_eq(closed_mixed_parent, mixed_parent));
    assert(atom_graph_arena_id_bounds(
        closed_mixed_parent, &mixed_min_id, &mixed_max_id));
    assert(mixed_min_id == destination.identity &&
           mixed_max_id == destination.identity);
    atom_deep_copy_session_free(session);

    /* A single publication uses the same full-graph ownership boundary.
     * An owned header with a nursery child must still be repaired. */
    assert(atom_deep_copy_single(NULL, old_child) == NULL);
    assert(atom_deep_copy_single(&destination, NULL) == NULL);
    assert(atom_deep_copy_single(&destination, old_child) == old_child);
    Atom *single_parent = atom_deep_copy_single(&destination, young_parent);
    Atom *single_mixed = atom_deep_copy_single(&destination, mixed_parent);
    assert(single_parent && single_parent != young_parent);
    assert(single_parent->expr.elems[1] == old_child);
    assert(single_mixed && single_mixed != mixed_parent);
    assert(single_mixed->expr.elems[1] != young_child);
    assert(atom_eq(single_mixed, closed_mixed_parent));
    assert(atom_graph_arena_id_bounds(single_mixed, &mixed_min_id, &mixed_max_id));
    assert(mixed_min_id == destination.identity &&
           mixed_max_id == destination.identity);
    Atom *single_shared = atom_deep_copy_single(&destination,
        atom_expr2(&nursery, young_child, young_child));
    assert(single_shared && single_shared->expr.elems[0] ==
                            single_shared->expr.elems[1]);
    Atom *owned_resource = atom_space(&destination, &external_space_identity);
    Atom *copied_resource = atom_deep_copy_single(&destination, owned_resource);
    assert(copied_resource && copied_resource != owned_resource);
    assert(copied_resource->ground.ptr == &external_space_identity);
    arena_free(&nursery);
    assert(atom_is_symbol(
        closed_mixed_parent->expr.elems[1]->expr.elems[0],
        "temporary"));
    assert(atom_is_symbol(single_mixed->expr.elems[1]->expr.elems[0], "temporary"));
    assert(atom_is_symbol(single_parent->expr.elems[1]->expr.elems[0], "old"));

    check_graph_projection(&source, &destination);

    arena_free(&destination);
    arena_free(&source);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: iterative atom copy, printing, and multi-root closure");
    return 0;
}
