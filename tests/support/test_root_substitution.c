#include "match.h"
#include "symbol.h"
#include "binding/frame_identity.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

/* Independent whole substitution. Root aliases are followed before recursively
 * realizing every child, without using the production root reader or fold. */
static Atom *reference_apply(const Bindings *bindings, Arena *arena,
                             BindingValue value, unsigned depth) {
    assert(depth < 8192u && value.skeleton);
    while (value.skeleton->kind == ATOM_VAR) {
        BindingValue next = bindings_lookup_value_id((Bindings *)bindings, binding_value_variable_id(value));
        if (!next.skeleton)
            return binding_value_materialize(arena, value);
        assert(++depth < 8192u);
        value = next;
    }
    if (!atom_has_vars(value.skeleton))
        return value.skeleton;
    assert(value.skeleton->kind == ATOM_EXPR);
    CettaExprLen length = value.skeleton->expr.len;
    Atom **fields = length ? malloc(length * sizeof(*fields)) : NULL;
    assert(!length || fields);
    for (CettaExprIndex index = 0u; index < length; index++) {
        fields[index] = reference_apply(bindings, arena,
            binding_value_with_skeleton(value, value.skeleton->expr.elems[index]), depth + 1u);
        assert(fields[index]);
    }
    Atom *image = atom_expr(arena, fields, length);
    free(fields);
    return image;
}

static void compare(const Bindings *bindings, Arena *arena, BindingValue value) {
    Atom *expected = reference_apply(bindings, arena, value, 0u);
    Atom *ordinary = bindings_apply_value(bindings, arena, value);
    assert(expected && ordinary && atom_eq(expected, ordinary));
    /* The graph entry shares the same root preparation but keeps its own
     * subtree-image memo. Both must retain exact variable identities. */
    if (binding_value_kind(value) == BINDING_VALUE_MATERIALIZED) {
        Atom *graph = bindings_apply_graph((Bindings *)bindings, arena, value.skeleton);
        assert(graph && atom_eq(expected, graph));
    }
    checks++;
}

static Atom *rewrite_free(Arena *arena, Atom *value, void *context) {
    assert(value && value->kind == ATOM_VAR);
    ++*(unsigned *)context;
    return atom_int(arena, 991);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena source, result;
    arena_init_detached(&source);
    arena_init_detached(&result);
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&identities);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&identities);
    VarId ids[2] = {var_id_from_base(111u), var_id_from_base(112u)};
    Atom *x = atom_var_with_id(&source, "same", ids[0]);
    Atom *y = atom_var_with_id(&source, "same", ids[1]);
    Atom *tag = atom_symbol(&source, "Data");
    BindingsBuilder builder;
    assert(bindings_builder_init(&builder, NULL));
    assert(bindings_builder_register_contextual_frame(&builder, ids, 2u, first));
    assert(bindings_builder_register_contextual_frame(&builder, ids, 2u, second));
    assert(bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[0], first),
        SYMBOL_ID_NONE, atom_int(&source, 11)));
    assert(bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[0], second),
        SYMBOL_ID_NONE, atom_int(&source, 23)));
    unsigned chain_length = argc == 4 ? (unsigned)strtoul(argv[2], NULL, 10) : 256u;
    unsigned calls = argc == 4 ? (unsigned)strtoul(argv[3], NULL, 10) : 1u;
    assert(chain_length && chain_length <= 4096u && calls);
    Atom **cells = malloc((chain_length + 1u) * sizeof(*cells));
    assert(cells);
    for (unsigned index = 0u; index <= chain_length; index++)
        cells[index] = atom_var_with_id(&source, "alias", var_id_from_base(10001u + index));
    for (unsigned index = chain_length; index > 0u; index--)
        assert(bindings_builder_add_id_fresh(&builder, cells[index - 1u]->var_id,
            SYMBOL_ID_NONE, cells[index]));
    uint32_t mark = bindings_builder_save(&builder);
    if (argc == 4) {
        assert(strcmp(argv[1], "--bench") == 0);
        assert(bindings_builder_add_id_fresh(&builder, cells[chain_length]->var_id,
            SYMBOL_ID_NONE, atom_int(&source, 17)));
        ArenaMark arena_marked = arena_mark(&result);
        uint64_t checksum = 0u;
        for (unsigned call = 0u; call < calls; call++) {
            Atom *image = bindings_apply(&builder.current, &result, cells[0]);
            assert(image && image->kind == ATOM_GROUNDED && image->ground.ival == 17);
            checksum += (uint64_t)image->ground.ival;
            arena_reset(&result, arena_marked);
        }
        printf("aliases=%u calls=%u checksum=%llu\n", chain_length, calls,
            (unsigned long long)checksum);
    } else {
        assert(argc == 1);
        compare(&builder.current, &result, binding_value_from_atom(cells[0]));
        for (unsigned variant = 0u; variant < 5u; variant++) {
            BindingValue terminal = variant == 0u
                ? binding_value_from_atom(atom_int(&source, 17))
                : variant == 1u ? binding_value_from_atom(atom_expr3(&source, tag, x, y))
                : variant == 2u ? binding_value_from_context(atom_expr3(&source, tag, x, y), first)
                : variant == 3u ? binding_value_from_persistent_context(atom_expr3(&source, tag, x, y), second)
                : binding_value_from_context(y, first);
            assert(match_binding_values_builder(binding_value_from_atom(cells[chain_length]), terminal, &builder, &result));
            compare(&builder.current, &result, binding_value_from_atom(cells[0]));
            unsigned known = builder.current.cycle_state;
            builder.current.cycle_state = 0u;
            compare(&builder.current, &result, binding_value_from_atom(cells[0]));
            builder.current.cycle_state = known;
            if (variant >= 2u) {
                Atom *image = bindings_apply(&builder.current, &result, cells[0]);
                if (variant < 4u)
                    assert(image->expr.elems[1]->ground.ival == (variant == 2u ? 11 : 23));
                else
                    assert(image->kind == ATOM_VAR && var_id_eq(image->var_id, var_epoch_id(ids[1], first)));
            }
            unsigned rewritten = 0u;
            Atom *image = bindings_apply_rewrite_vars(&builder.current, &result,
                cells[0], rewrite_free, &rewritten);
            assert(image && rewritten == (variant == 0u ? 0u : variant == 1u ? 2u : 1u));
            checks++;
            bindings_builder_rollback(&builder, mark);
            compare(&builder.current, &result, binding_value_from_atom(cells[0]));
        }
        /* External rewrites can invalidate acyclicity. A variable bound to
         * an expression containing itself must keep the active-path reader:
         * removing its initial pending variable would unfold one extra node. */
        Bindings cyclic;
        bindings_init(&cyclic);
        Atom *z = atom_var_with_id(&source, "cycle", var_id_from_base(30001u));
        assert(bindings_add_var(&cyclic, z, atom_int(&source, 1)));
        assert(bindings_prepare_logical_write(&cyclic));
        for (uint32_t index = 0u; index < cyclic.len; index++) {
            if (var_id_eq(cyclic.entries[index].var_id, z->var_id))
                cyclic.entries[index].value.skeleton = atom_expr2(&source, tag, z);
        }
        bindings_invalidate_after_key_rewrite(&cyclic);
        assert(bindings_has_loop(&cyclic));
        Atom *cut = bindings_apply(&cyclic, &result, z);
        assert(cut && cut->kind == ATOM_EXPR && cut->expr.len == 2u &&
               cut->expr.elems[1]->kind == ATOM_VAR);
        assert(var_id_eq(cut->expr.elems[1]->var_id, z->var_id));
        checks++;
        bindings_free(&cyclic);
        printf("PASS: %u root substitution controls; aliases, lexical scopes, graph images, "
               "custom rewrites, cycle fallback, unknown evidence and rollback\n", checks);
    }
    free(cells);
    bindings_builder_free(&builder);
    arena_free(&result);
    arena_free(&source);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
