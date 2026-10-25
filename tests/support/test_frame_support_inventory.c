#define _POSIX_C_SOURCE 200809L
#include "match.h"
#include "binding/slot_store_internal.h"
#include "variant_shape.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* This oracle reads actual variable leaves, without consulting constructor
 * support, variable-presence flags, or a memoized graph inventory. */
static size_t reference(BindingValue value, VarId *out, size_t capacity) {
    size_t length = 0u, used = 1u, reserved = 64u;
    Atom **pending = malloc(reserved * sizeof(*pending));
    assert(pending);
    pending[0] = value.skeleton;
    while (used != 0u) {
        Atom *atom = pending[--used];
        assert(atom);
        if (atom->kind == ATOM_VAR) {
            CettaFrameIdentity epoch = binding_value_is_contextual(value)
                ? binding_value_epoch(value) : var_epoch_suffix(atom->var_id);
            if (cetta_frame_identity_is_none(epoch) || variant_private_var_id(atom->var_id))
                continue;
            VarId id = var_epoch_id(var_source_id(atom->var_id), epoch);
            bool present = false;
            for (size_t i = 0u; i < length; i++)
                present = present || var_id_eq(out[i], id);
            if (!present) {
                assert(length < capacity);
                out[length++] = id;
            }
        } else if (atom->kind == ATOM_EXPR) {
            size_t needed = used + atom->expr.len;
            if (needed > reserved) {
                while (reserved < needed)
                    reserved *= 2u;
                pending = realloc(pending, reserved * sizeof(*pending));
                assert(pending);
            }
            for (CettaExprIndex i = 0u; i < atom->expr.len; i++)
                pending[used++] = atom->expr.elems[i];
        }
    }
    free(pending);
    return length;
}

static Atom *spine(Arena *arena, Atom *tag, Atom *leaf, unsigned depth) {
    while (depth-- != 0u)
        leaf = atom_expr2(arena, tag, leaf);
    return leaf;
}

static unsigned checks;

static void frame_chronology_preserves_implicit_slots(CettaFrameIdentity epoch) {
    const uint32_t widths[] = {2u, 65u, 257u};
    for (unsigned i = 0u; i < sizeof widths / sizeof widths[0]; i++) {
        Bindings source, retained, merged, captured;
        bindings_init(&source); bindings_init(&retained);
        bindings_init(&merged); bindings_init(&captured);
        VarId first = var_id_from_base(1u);
        assert(bindings_frame_index_register(&source, &first, 1u, epoch));
        assert(bindings_frame_index_grow_slots(&source, epoch, widths[i]));
        BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame(source.frame_index, epoch);
        uint32_t written = widths[i] > 5u ? 5u : 1u;
        assert(bindings_frame_index_replace_slot_version(frame, written, 91u));
        source.frame_index->write_clock = 91u;
        assert(bindings_clone(&captured, &source));
        assert(bindings_retain_frame_state(&retained, &source));
        assert(bindings_try_merge(&merged, &source));
        const Bindings *images[] = {&source, &retained, &merged, &captured};
        for (unsigned image = 0u; image < 4u; image++) {
            BindingsFrameStateIterator iterator = {.bindings = images[image]};
            BindingsFrameStateView view;
            assert(bindings_frame_state_next(&iterator, &view));
            assert(view.slot_count == widths[i] && view.write_clock == 91u);
            for (uint32_t slot = 0u; slot < widths[i]; slot++) {
                BindingsFrameSlotStateView state;
                assert(bindings_frame_state_read_slot(&view, slot, &state));
                assert(!state.value.skeleton && state.write_version == (slot == written ? 91u : 0u));
                checks++;
            }
        }
        assert(bindings_frame_index_detach(&source));
        frame = bindings_frame_index_find_frame(source.frame_index, epoch);
        assert(bindings_frame_index_replace_slot_version(frame, written, 92u));
        const BindingsFrameIndexEntry *old = bindings_frame_index_find_frame_const(captured.frame_index, epoch);
        assert(bindings_frame_slot_version(old, written) == 91u);
        if (widths[i] > BINDINGS_DIRECT_FRAME_SLOT_CAP)
            assert(frame->cap < frame->slot_len && old->cap < old->slot_len);
        bindings_free(&captured); bindings_free(&merged);
        bindings_free(&retained); bindings_free(&source);
    }
}

static void compare(BindingValue value) {
    VarId actual[128], expected[128];
    size_t length = 0u;
    size_t wanted = reference(value, expected, 128u);
    assert(bindings_frame_support_test(value, actual, 128u, &length));
    assert(length == wanted);
    for (size_t i = 0u; i < wanted; i++)
        assert(var_id_eq(actual[i], expected[i]));
    checks++;
    if (wanted != 0u) {
        VarId untouched = var_id_from_base(918273u);
        actual[0] = untouched;
        length = 0u;
        assert(!bindings_frame_support_test(value, actual, wanted - 1u, &length));
        assert(length == wanted && var_id_eq(actual[0], untouched));
        checks++;
    }
}

static uint64_t nanoseconds(void) {
    struct timespec value;
    assert(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(identity_scope);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&identity_scope);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&identity_scope);
    Atom *tag = atom_symbol(&arena, "support-node");

    if ((argc == 2 || argc == 3) && strcmp(argv[1], "--bench") == 0) {
        const unsigned depths[] = {32u, 256u, 2048u, 16384u};
        unsigned selected = argc == 3 ? (unsigned)strtoul(argv[2], NULL, 10) : 0u;
        unsigned measurements = 0u;
        for (size_t i = 0u; i < sizeof(depths) / sizeof(depths[0]); i++) {
            if (argc == 3 && selected != depths[i])
                continue;
            Atom *left = atom_var_with_id(&arena, "left", var_id_from_base(1u));
            Atom *right = atom_var_with_id(&arena, "right", var_id_from_base(2u));
            Atom *root = atom_expr3(&arena, tag, spine(&arena, tag, left, depths[i]),
                                    spine(&arena, tag, right, depths[i]));
            BindingValue value = binding_value_from_context(root, first);
            uint64_t start = nanoseconds();
            for (unsigned repeat = 0u; repeat < 1000u; repeat++) {
                VarId ids[2];
                size_t count = 0u;
                assert(bindings_frame_support_test(value, ids, 2u, &count));
                assert(count == 2u &&
                    var_id_eq(ids[0], var_epoch_id(var_id_from_base(2u), first)) &&
                    var_id_eq(ids[1], var_epoch_id(var_id_from_base(1u), first)));
            }
            printf("depth=%u calls=1000 elapsed_ns=%llu\n", depths[i],
                   (unsigned long long)(nanoseconds() - start));
            measurements++;
        }
        assert(measurements != 0u);
    } else {
        assert(argc == 1);
        frame_chronology_preserves_implicit_slots(first);
        Atom *children[128];
        for (unsigned i = 0u; i < 128u; i++) {
            VarId id = var_id_from_base(1000u + i % 32u);
            if (i % 7u == 0u)
                id = variant_shape_slot_id(i % 32u);
            else if (i % 3u != 0u)
                id = var_epoch_id(id, i % 3u == 1u ? first : second);
            Atom *leaf = atom_var_with_id(&arena, "same-spelling", id);
            children[i] = spine(&arena, tag, leaf, 32u + i % 5u);
        }
        for (unsigned size = 2u; size <= 128u; size *= 2u) {
            Atom *root = atom_expr(&arena, children, size);
            compare(binding_value_from_atom(root));
            compare(binding_value_from_context(root, first));
            compare(binding_value_from_context(root, second));
            compare(binding_value_from_persistent_context(root, first));
        }
        /* Shared subgraphs, unknown singleton metadata, and structured
         * variable presentation retain their leaf-derived coordinates. */
        Atom *presented = atom_var_with_name_key(&arena,
            atom_expr2(&arena, tag, atom_int(&arena, 9)),
            var_epoch_id(var_id_from_base(9001u), first));
        Atom *unknown = spine(&arena, tag, presented, 128u);
        unknown->var_id = VAR_ID_NONE;
        Atom *dag = atom_expr3(&arena, tag, unknown, unknown);
        for (unsigned depth = 0u; depth < 8u; depth++)
            dag = atom_expr3(&arena, tag, dag, dag);
        compare(binding_value_from_atom(dag));
        compare(binding_value_from_context(dag, second));
        compare(binding_value_from_context(atom_int(&arena, 4), first));
        puts("PASS: ordered frame support agrees with the metadata-free leaf oracle");
        printf("FrameSupportInventorySummary %u\n", checks);
    }
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
