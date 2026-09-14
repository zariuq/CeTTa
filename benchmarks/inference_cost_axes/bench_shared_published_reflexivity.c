#include "atom.h"
#include "match.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t parse_u32(const char *text, uint32_t fallback) {
    if (!text || !*text)
        return fallback;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (!end || *end != '\0' || value == 0ul || value > UINT32_MAX)
        return fallback;
    return (uint32_t)value;
}

int main(int argc, char **argv) {
    const uint32_t depth = parse_u32(argc > 1 ? argv[1] : NULL, 72u);
    const uint32_t iterations =
        parse_u32(argc > 2 ? argv[2] : NULL, 200000u);

    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    VarInternTable variables;
    var_intern_init(&variables);
    g_var_intern = &variables;

    HashConsTable hashcons;
    hashcons_init(&hashcons);
    Arena shared;
    arena_init(&shared);
    arena_set_hashcons(&shared, &hashcons);
    Arena local;
    arena_init(&local);

    Atom *head = hashcons_get(
        &hashcons, atom_symbol(&shared, "SharedGroundNode"));
    Atom *value = hashcons_get(&hashcons, atom_symbol(&shared, "leaf"));
    for (uint32_t level = 0u; value && level < depth; level++) {
        Atom *index = hashcons_get(
            &hashcons, atom_int(&shared, (int64_t)level));
        value = hashcons_get(
            &hashcons, atom_expr3(&shared, head, index, value));
    }

    const uint32_t required =
        ATOM_FLAG_HASHCONS_ELIGIBLE | ATOM_FLAG_ARENA_CLOSED;
    bool publication_certified = value && value->kind == ATOM_EXPR &&
        value->arena_id == 0u && !atom_has_vars(value) &&
        (value->flags & required) == required;
    if (!publication_certified) {
        fprintf(stderr,
                "publication certificate missing: kind=%d arena=%u flags=0x%x\n",
                value ? (int)value->kind : -1,
                value ? value->arena_id : 0u,
                value ? value->flags : 0u);
    }

    BindingsBuilder builder;
    bool builder_ready = bindings_builder_init(&builder, NULL);
    bool valid = publication_certified && builder_ready;
    uint32_t mark = valid ? bindings_builder_save(&builder) : 0u;
    for (uint32_t iteration = 0u; valid && iteration < iterations;
         iteration++) {
        valid = match_atoms_epoch_view_builder(
                    value, 11u, 0u, value,
                    &builder, &shared, 13u) &&
                bindings_builder_save(&builder) == mark;
    }

    Atom *cycle = atom_expr2(
        &local, atom_symbol(&local, "LocalCycle"),
        atom_symbol(&local, "seed"));
    if (cycle)
        cycle->expr.elems[1] = cycle;
    bool cycle_rejected = builder_ready && cycle &&
        !match_atoms_epoch_view_builder(
            cycle, 17u, 0u, cycle,
            &builder, &local, 19u) &&
        bindings_builder_save(&builder) == mark;

    Atom *nan_left = hashcons_get(&hashcons, atom_float(&shared, NAN));
    Atom *nan_right = hashcons_get(&hashcons, atom_float(&shared, NAN));
    Atom *shared_nan = atom_expr2(&shared, head, nan_left);
    Atom *distinct_nan = atom_expr2(&shared, head, nan_right);
    bool identity_reflexive = builder_ready && shared_nan &&
        match_atoms_epoch_view_builder(
            shared_nan, 23u, 0u, shared_nan,
            &builder, &shared, 29u) &&
        bindings_builder_save(&builder) == mark;
    bool distinct_nan_rejected = builder_ready && distinct_nan &&
        nan_left != nan_right &&
        !match_atoms_epoch_view_builder(
            shared_nan, 23u, 0u, distinct_nan,
            &builder, &shared, 29u) &&
        bindings_builder_save(&builder) == mark;

    valid = valid && cycle_rejected && identity_reflexive &&
        distinct_nan_rejected;
    if (!cycle_rejected)
        fprintf(stderr, "local cycle was not rejected\n");
    if (!identity_reflexive)
        fprintf(stderr, "shared NaN occurrence was not reflexive\n");
    if (!distinct_nan_rejected)
        fprintf(stderr, "distinct NaN terms were not rejected\n");
    printf("(SharedPublishedReflexivityBench %u %u %s)\n",
           depth, iterations, valid ? "pass" : "fail");

    if (builder_ready)
        bindings_builder_free(&builder);
    arena_free(&local);
    arena_free(&shared);
    hashcons_free(&hashcons);
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return valid ? 0 : 1;
}
