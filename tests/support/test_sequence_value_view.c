#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "petta_semantics.h"
#include "symbol.h"
#include "binding/frame_identity.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned checks;

#ifdef CETTA_TEST_SEQUENCE_READ_BOUND
static unsigned root_reads;
bool __real_bindings_observe_value_root(const Bindings *, BindingValue, BindingValue *);
bool __wrap_bindings_observe_value_root(
        const Bindings *bindings, BindingValue source, BindingValue *out) {
    ++root_reads;
    return __real_bindings_observe_value_root(bindings, source, out);
}
#endif

static bool reference_sequence(Atom *atom, Atom *const **elements, CettaExprLen *length) {
    if (!atom || atom->kind != ATOM_EXPR || atom_is_list_rest(atom) ||
        atom_is_petta_prolog_compound(atom) || atom_is_petta_partial(atom))
        return false;
    if (atom->expr.len == 3u) {
        if (atom_is_internal_tag(atom->expr.elems[0],
                CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE))
            return false;
        Atom *domain = atom->expr.elems[1];
        if (domain->kind == ATOM_EXPR && domain->expr.len == 2u &&
            atom_is_internal_tag(domain->expr.elems[0],
                CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY))
            return false;
    }
    bool list = atom_is_list(atom);
    *elements = atom->expr.elems + (list ? 1u : 0u);
    *length = atom->expr.len - (list ? 1u : 0u);
    return true;
}

/* The oracle substitutes the entire value before asking the existing
 * concrete observer. The candidate observes only constructor coordinates. */
static void compare(Arena *arena, const Bindings *bindings, BindingValue value) {
    ArenaMark mark = arena_mark(arena);
    PettaSequenceValueView view;
#ifdef CETTA_TEST_SEQUENCE_READ_BOUND
    unsigned reads_before = root_reads;
#endif
    assert(petta_semantics_sequence_value_view(bindings, value, &view));
#ifdef CETTA_TEST_SEQUENCE_READ_BOUND
    assert(root_reads - reads_before <= 4u);
#endif
    assert(arena_at_mark(arena, mark));
    Atom *image = bindings_apply_value(bindings, arena, value);
    assert(image);
    Atom *const *elements = NULL;
    CettaExprLen length = 0u;
    bool sequence = reference_sequence(image, &elements, &length);
    assert(view.sequence == sequence);
    if (sequence) {
        assert(view.length == length && view.list == atom_is_list(image));
        for (CettaExprIndex i = 0u; i < length; ++i) {
            Atom *child = bindings_apply_value(bindings, arena,
                binding_value_with_skeleton(view.value, view.elements[i]));
            assert(child && atom_eq(child, elements[i]));
        }
    }
    for (unsigned projection = CETTA_SEQUENCE_PROJECTION_HEAD;
         projection <= CETTA_SEQUENCE_PROJECTION_TAIL; ++projection) {
        CettaSequenceProjectionView selected = cetta_sequence_projection_view(
            (CettaSequenceProjectionCall){(CettaSequenceProjection)projection,
                CETTA_SEQUENCE_PETTA_VALUE},
            view.value.skeleton, view.sequence, view.elements, view.length);
        BindingValue projected = cetta_sequence_projection_materialize_value(
            arena, &selected, view.value, view.list);
        assert(projected.skeleton);
        Atom *actual = bindings_apply_value(bindings, arena, projected);
        Atom *expected = !sequence || length == 0u ? atom_unit(arena)
            : projection == CETTA_SEQUENCE_PROJECTION_HEAD ? elements[0]
            : atom_is_list(image) ? atom_list(arena, elements + 1u, length - 1u)
            : atom_expr(arena, image->expr.elems + 1u, length - 1u);
        assert(actual && expected && atom_eq(actual, expected));
        if (selected.result == CETTA_SEQUENCE_PROJECT_ELEMENT)
            assert(projected.context == view.value.context);
        if (selected.result == CETTA_SEQUENCE_PROJECT_SUFFIX)
            assert(!binding_value_context_is_persistent(projected));
    }
    ++checks;
}

static uint64_t nanoseconds(void) {
    struct timespec value;
    assert(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + value.tv_nsec;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&identities);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&identities);
    CettaFrameIdentity missing = cetta_frame_identity_scope_fresh(&identities);
    VarId ids[4];
    Atom *variables[4];
    for (unsigned i = 0u; i < 4u; ++i) {
        ids[i] = var_id_from_base(1001u + i);
        variables[i] = atom_var_with_id(&arena, "same-spelling", ids[i]);
    }
    BindingsBuilder builder;
    assert(bindings_builder_init(&builder, NULL));
    assert(bindings_builder_register_contextual_frame(&builder, ids, 4u, first));
    assert(bindings_builder_register_contextual_frame(&builder, ids, 4u, second));
    assert(bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[0], first),
        SYMBOL_ID_NONE, atom_int(&arena, 11)));
    assert(bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[0], second),
        SYMBOL_ID_NONE, atom_int(&arena, 23)));

    if (argc == 5 && strcmp(argv[1], "--bench") == 0) {
        bool reference = strcmp(argv[2], "reference") == 0;
        assert(reference || strcmp(argv[2], "view") == 0);
        unsigned length = (unsigned)strtoul(argv[3], NULL, 10);
        unsigned calls = (unsigned)strtoul(argv[4], NULL, 10);
        assert(length > 0u && calls > 0u);
        Atom **items = malloc(sizeof(*items) * length);
        assert(items);
        for (unsigned i = 0u; i < length; ++i) items[i] = variables[0];
        BindingValue input = binding_value_from_context(
            atom_expr(&arena, items, length), first);
        free(items);
        ArenaMark mark = arena_mark(&arena);
        uint64_t start = nanoseconds(), checksum = 0u;
        for (unsigned i = 0u; i < calls; ++i) {
            Atom *result;
            if (reference) {
                Atom *image = bindings_apply_value(&builder.current, &arena, input);
                assert(image && image->kind == ATOM_EXPR && image->expr.len == length);
                result = image->expr.elems[0];
            } else {
                PettaSequenceValueView view;
                assert(petta_semantics_sequence_value_view(&builder.current, input, &view));
                assert(view.sequence && view.length == length);
                result = bindings_apply_value(&builder.current, &arena,
                    binding_value_with_skeleton(view.value, view.elements[0]));
            }
            assert(result && result->kind == ATOM_GROUNDED && result->ground.ival == 11);
            checksum += result->ground.ival;
            arena_reset(&arena, mark);
        }
        printf("length=%u calls=%u checksum=%llu elapsed_ns=%llu\n", length, calls,
            (unsigned long long)checksum, (unsigned long long)(nanoseconds() - start));
    } else {
        assert(argc == 1);
        Atom *literal = atom_symbol(&arena, "ordinary");
        Atom *derived_open = atom_expr2(&arena, variables[0], variables[1]);
        Atom *derived_closed = atom_expr2(&arena, literal, literal);
        for (unsigned kind = BINDING_VALUE_MATERIALIZED;
             kind <= BINDING_VALUE_CONTEXTUAL_PERSISTENT; ++kind) {
            BindingValue source = binding_value_make(derived_open, first, (BindingValueKind)kind);
            BindingValue open = binding_value_with_derived_skeleton(source, derived_open);
            BindingValue closed = binding_value_with_derived_skeleton(source, derived_closed);
            assert(open.skeleton == derived_open && closed.skeleton == derived_closed);
            assert(binding_value_kind(open) != BINDING_VALUE_CONTEXTUAL_PERSISTENT &&
                   binding_value_kind(closed) != BINDING_VALUE_CONTEXTUAL_PERSISTENT);
            assert(cetta_frame_identity_eq(binding_value_epoch(open), first));
            assert(binding_value_kind(open) ==
                (kind == BINDING_VALUE_CONTEXTUAL_PERSISTENT ? BINDING_VALUE_CONTEXTUAL : kind));
            assert(binding_value_kind(closed) ==
                (kind == BINDING_VALUE_CONTEXTUAL_PERSISTENT
                    ? BINDING_VALUE_CONTEXTUAL : BINDING_VALUE_MATERIALIZED));
            ++checks;
        }
        Atom *items[9] = {literal, variables[0], variables[1], variables[2],
            variables[0], variables[1], variables[2], variables[3], literal};
        for (unsigned length = 0u; length <= 9u; ++length) {
            Atom *tuple = atom_expr(&arena, items, length);
            Atom *list = atom_list(&arena, items, length);
            for (unsigned frame = 0u; frame < 2u; ++frame) {
                CettaFrameIdentity identity = frame ? second : first;
                compare(&arena, &builder.current, binding_value_from_context(tuple, identity));
                compare(&arena, &builder.current, binding_value_from_persistent_context(list, identity));
            }
        }
        compare(&arena, &builder.current, binding_value_from_atom(atom_int(&arena, 7)));
        compare(&arena, &builder.current, binding_value_from_atom(literal));

        /* Source shape alone is insufficient: a domain variable may reveal
         * a private callable identity only after contextual lookup. */
        Atom *domain = atom_expr2(&arena,
            atom_internal_tag(&arena, CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY),
            variables[0]);
        uint32_t mark = bindings_builder_save(&builder);
        assert(match_binding_values_builder(
            binding_value_from_context(variables[1], first),
            binding_value_from_persistent_context(domain, second), &builder, &arena));
        Atom *carrier = atom_expr3(&arena, literal, variables[1], variables[0]);
        assert(!petta_semantics_is_nonlist_carrier(carrier));
        compare(&arena, &builder.current, binding_value_from_context(carrier, first));
        PettaSequenceValueView view;
#ifdef CETTA_TEST_SEQUENCE_READ_BOUND
        unsigned complete_header_read = root_reads;
#endif
        assert(petta_semantics_sequence_value_view(&builder.current,
            binding_value_from_context(carrier, first), &view) && !view.sequence);
#ifdef CETTA_TEST_SEQUENCE_READ_BOUND
        assert(root_reads - complete_header_read == 4u);
#endif
        ++checks;
        bindings_builder_rollback(&builder, mark);
        compare(&arena, &builder.current, binding_value_from_context(carrier, first));
        assert(petta_semantics_sequence_value_view(&builder.current,
            binding_value_from_context(carrier, first), &view) && view.sequence);
        ++checks;

        /* Aliasing a tuple enters the stored tuple's frame, not the variable's
         * caller frame. Both frames deliberately reuse the same source ids. */
        Atom *tuple = atom_expr2(&arena, variables[0], variables[2]);
        assert(match_binding_values_builder(
            binding_value_from_context(variables[3], first),
            binding_value_from_persistent_context(tuple, second), &builder, &arena));
        compare(&arena, &builder.current, binding_value_from_context(variables[3], first));
        assert(petta_semantics_sequence_value_view(&builder.current,
            binding_value_from_context(variables[3], first), &view));
        assert(cetta_frame_identity_eq(binding_value_epoch(view.value), second));
        Atom *selected = bindings_apply_value(&builder.current, &arena,
            binding_value_with_skeleton(view.value, view.elements[0]));
        assert(selected && selected->kind == ATOM_GROUNDED && selected->ground.ival == 23);
        ++checks;

        /* The independent legacy classifier also sees constructor tags
         * introduced through a binding, not merely tags visible in syntax. */
        CettaInternalTag role_tags[] = {CETTA_INTERNAL_TAG_LIST,
            CETTA_INTERNAL_TAG_PETTA_PARTIAL, CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND,
            CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE,
            CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY};
        for (size_t tag = 0u; tag < sizeof(role_tags) / sizeof(role_tags[0]); ++tag) {
            uint32_t role_mark = bindings_builder_save(&builder);
            assert(bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[2], first),
                SYMBOL_ID_NONE, atom_internal_tag(&arena, role_tags[tag])));
            for (unsigned length = 1u; length <= 9u; ++length) {
                Atom *fields[9] = {variables[2], variables[1], variables[0],
                    literal, variables[0], variables[1], literal, variables[0], literal};
                Atom *shape = atom_expr(&arena, fields, length);
                compare(&arena, &builder.current, binding_value_from_context(shape, first));
                compare(&arena, &builder.current, binding_value_from_persistent_context(shape, first));
            }
            bindings_builder_rollback(&builder, role_mark);
        }

        assert(!petta_semantics_sequence_value_view(&builder.current,
            binding_value_from_context(tuple, missing), &view) && !view.value.skeleton);
        ++checks;
        uint8_t cycle_state = builder.current.cycle_state;
        builder.current.cycle_state = 0u;
        assert(!petta_semantics_sequence_value_view(&builder.current,
            binding_value_from_context(tuple, first), &view));
        builder.current.cycle_state = cycle_state;
        ++checks;
        uint32_t facts = tuple->structural_facts;
        tuple->structural_facts = 0u;
        assert(!petta_semantics_sequence_value_view(&builder.current,
            binding_value_from_context(tuple, first), &view));
        tuple->structural_facts = facts;
        ++checks;
        for (unsigned tag = 0u; tag < 4u; ++tag) {
            CettaInternalTag kinds[] = {CETTA_INTERNAL_TAG_PETTA_OPEN_CONS,
                CETTA_INTERNAL_TAG_RATIONAL, CETTA_INTERNAL_TAG_LIST_REST,
                CETTA_INTERNAL_TAG_COUNTED_COLLECTION};
            Atom *special = atom_expr3(&arena, atom_internal_tag(&arena, kinds[tag]),
                variables[0], variables[1]);
            assert(!petta_semantics_sequence_value_view(&builder.current,
                binding_value_from_context(special, first), &view));
            ++checks;
        }
        assert(!petta_semantics_sequence_value_view(&builder.current,
            binding_value_from_atom(atom_counted_collection(&arena, 200)), &view));
        ++checks;
        uint32_t frame_mark = bindings_builder_save(&builder);
        assert(bindings_builder_register_contextual_frame(&builder, ids, 4u, missing));
        compare(&arena, &builder.current, binding_value_from_context(tuple, missing));
        bindings_builder_rollback(&builder, frame_mark);
        assert(!petta_semantics_sequence_value_view(&builder.current,
            binding_value_from_context(tuple, missing), &view));
        ++checks;
        printf("PASS: %u contextual sequence observations, scope changes, rollback and decline controls\n", checks);
    }
    bindings_builder_free(&builder);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
