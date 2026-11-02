#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "space.h"
#include "space_path_index.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HEADS = 128, ROWS = 33 };

static Atom *read_atom(Arena *arena, const char *text) {
    size_t pos = 0u;
    Atom *atom = parse_sexpr(arena, text, &pos);
    assert(atom);
    return atom;
}

static void check_rows(Space *space, CettaIndex *positions, CettaCount count,
                        SymbolId head, const SymbolId *values, bool extra) {
    assert(count == 11u + (extra ? 1u : 0u));
    bool seen[ROWS + 1] = {0};
    for (CettaCount i = 0u; i < count; i++) {
        Atom *row = space_get_at64(space, positions[i]);
        assert(row && row->kind == ATOM_EXPR && row->expr.len == 4u);
        assert(atom_is_symbol_id(row->expr.elems[0], head));
        assert(row->expr.elems[1]->kind == ATOM_GROUNDED &&
            row->expr.elems[1]->ground.gkind == GV_INT &&
            row->expr.elems[1]->ground.ival == 1);
        unsigned j = 0u;
        while (j <= ROWS && !atom_is_symbol_id(row->expr.elems[2], values[j]))
            j++;
        assert(j <= ROWS && !seen[j]);
        assert(j == ROWS ? extra : j % 3u == 1u);
        seen[j] = true;
    }
    for (unsigned j = 0u; j < ROWS; j++)
        assert(seen[j] == (j % 3u == 1u));
    assert(seen[ROWS] == extra);
    free(positions);
}

static void query(Space *space, Arena *scratch, unsigned h, SymbolId head,
                   const SymbolId *values, bool prefix, bool build,
                   bool expected, bool extra) {
    ArenaMark mark = arena_mark(scratch);
    char text[96];
    snprintf(text, sizeof(text), "(directory-h%u 1 $value $tail)", h);
    Atom *pattern = read_atom(scratch, text);
    CettaIndex *positions = NULL;
    CettaCount count = 0u;
    bool got = prefix
        ? space_path_index_prefix_candidates(space, pattern, build, &positions, &count)
        : space_path_index_lead_candidates(space, pattern, build, &positions, &count);
    assert(got == expected);
    if (got)
        check_rows(space, positions, count, head, values, extra);
    else
        free(positions);
    arena_reset(scratch, mark);
}

static void exercise_mutations(Space *space, Arena *scratch,
                                 const SymbolId *heads, const SymbolId *values,
                                 bool prefixes) {
    char text[128];
    for (unsigned h = 0u; h < HEADS; h += 7u) {
        ArenaMark mark = arena_mark(scratch);
        snprintf(text, sizeof(text), "(directory-h%u 1 directory-r%u tail)", h, ROWS);
        Atom *extra = read_atom(scratch, text);
        assert(space_admit_atom(space, space->native.universe->persistent_arena, extra));
        query(space, scratch, h, heads[h], values, false, false, true, true);
        query(space, scratch, h, heads[h], values, true, false, prefixes, true);
        assert(space_remove(space, extra));
        arena_reset(scratch, mark);
    }
    ArenaMark mark = arena_mark(scratch);
    Atom *extras[2];
    const unsigned moved_heads[] = {11u, 93u};
    for (unsigned i = 0u; i < 2u; i++) {
        char text[128];
        snprintf(text, sizeof(text), "(directory-h%u 1 directory-r%u tail)", moved_heads[i], ROWS);
        extras[i] = read_atom(scratch, text);
        assert(space_admit_atom(space, space->native.universe->persistent_arena, extras[i]));
    }
    assert(space_remove(space, extras[0]));
    query(space, scratch, 93u, heads[93], values, false, false, true, true);
    query(space, scratch, 93u, heads[93], values, true, false, prefixes, true);
    assert(space_remove(space, extras[1]));
    arena_reset(scratch, mark);
}

static void exercise_unknown_heads(Space *space, Arena *scratch,
                                    const SymbolId *heads, const SymbolId *values) {
    const char *forms[] = {
        "($directory-open-head 1 directory-r33 tail)",
        "$directory-open-row",
    };
    for (unsigned form = 0u; form < 2u; form++) {
        ArenaMark mark = arena_mark(scratch);
        Atom *open = read_atom(scratch, forms[form]);
        assert(space_admit_atom(space, space->native.universe->persistent_arena, open));
        for (unsigned h = 5u; h < HEADS; h += 62u) {
            for (unsigned prefix = 0u; prefix < 2u; prefix++) {
                char text[96];
                snprintf(text, sizeof(text), "(directory-h%u 1 $v $tail)", h);
                Atom *pattern = read_atom(scratch, text);
                CettaIndex *positions = NULL;
                CettaCount count = 0u;
                bool got = prefix
                    ? space_path_index_prefix_candidates(space, pattern, false, &positions, &count)
                    : space_path_index_lead_candidates(space, pattern, false, &positions, &count);
                assert(got && count == 12u);
                CettaIndex *ordinary = malloc(11u * sizeof(*ordinary));
                assert(ordinary);
                unsigned named = 0u, unknown = 0u;
                for (CettaCount i = 0u; i < count; i++) {
                    Atom *row = space_get_at64(space, positions[i]);
                    assert(row);
                    if (row->kind == ATOM_VAR || (row->kind == ATOM_EXPR &&
                        row->expr.len != 0u && row->expr.elems[0]->kind == ATOM_VAR)) {
                        unknown++;
                    } else {
                        assert(named < 11u);
                        ordinary[named++] = positions[i];
                    }
                }
                assert(named == 11u && unknown == 1u);
                check_rows(space, ordinary, named, heads[h], values, false);
                free(positions);
            }
        }
        assert(space_remove(space, open));
        arena_reset(scratch, mark);
    }
}

static void exercise_shared_budget(TermUniverse *universe, Arena *scratch) {
    enum { DIMENSIONS = 12u, FACTS = 2560u };
    Space space;
    space_init_with_universe(&space, universe);
    SymbolId head = symbol_intern_cstr(g_symbols, "directory-budget");
    for (unsigned row = 0u; row < FACTS; row++) {
        ArenaMark mark = arena_mark(scratch);
        Atom *parts[DIMENSIONS + 1u];
        parts[0] = atom_symbol_id(scratch, head);
        for (unsigned col = 1u; col <= DIMENSIONS; col++)
            parts[col] = atom_int(scratch, (int64_t)row * 16 + col);
        assert(space_admit_atom(&space, universe->persistent_arena,
                                atom_expr(scratch, parts, DIMENSIONS + 1u)));
        arena_reset(scratch, mark);
    }
    CettaIndex *held = NULL;
    bool retired = false;
    size_t largest = 0u;
    for (unsigned bound = 1u; bound <= DIMENSIONS; bound++) {
        ArenaMark mark = arena_mark(scratch);
        Atom *parts[DIMENSIONS + 1u];
        parts[0] = atom_symbol_id(scratch, head);
        for (unsigned col = 1u; col <= DIMENSIONS; col++) {
            if (col == bound) {
                parts[col] = atom_int(scratch, (int64_t)bound * 7 * 16 + col);
            } else {
                char name[48];
                snprintf(name, sizeof(name), "$budget-column-%u", col);
                parts[col] = read_atom(scratch, name);
            }
        }
        CettaIndex *positions = NULL;
        CettaCount count = 0u;
        assert(space_path_index_lead_candidates(&space,
            atom_expr(scratch, parts, DIMENSIONS + 1u), true, &positions, &count));
        assert(count == 1u);
        Atom *row = space_get_at64(&space, positions[0]);
        assert(row && row->kind == ATOM_EXPR && row->expr.len == DIMENSIONS + 1u);
        assert(row->expr.elems[bound]->ground.ival == (int64_t)bound * 7 * 16 + bound);
        CettaSpacePathIndexInfo info;
        assert(space_path_indexes_info(&space, &info));
        assert(info.retained_bytes <= info.budget_bytes);
        retired |= info.index_count < bound;
        if (info.retained_bytes > largest)
            largest = info.retained_bytes;
        if (!held)
            held = positions;
        else
            free(positions);
        arena_reset(scratch, mark);
    }
    assert(retired && held);
    /* The first index has retired; its copied result still names its row. */
    Atom *first = space_get_at64(&space, held[0]);
    assert(first && first->kind == ATOM_EXPR && first->expr.elems[1]->ground.ival == 113);
    free(held);
    space_free(&space);
    printf("PASS: twelve column indexes share one budget (%zu bytes retained at most); copied candidates outlive retirement\n", largest);
}


int main(void) {
    assert(setenv("CETTA_SPACE_DENSE", "off", 1) == 0);
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variables);
    g_var_intern = &variables;
    Arena persistent, scratch;
    arena_init_detached(&persistent);
    arena_init_detached(&scratch);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space space;
    space_init_with_universe(&space, &universe);
    SymbolId heads[HEADS], values[ROWS + 1];
    char text[128];
    for (unsigned h = 0u; h < HEADS; h++) {
        snprintf(text, sizeof(text), "directory-h%u", h);
        heads[h] = symbol_intern_cstr(&symbols, text);
    }
    for (unsigned j = 0u; j <= ROWS; j++) {
        snprintf(text, sizeof(text), "directory-r%u", j);
        values[j] = symbol_intern_cstr(&symbols, text);
    }
    for (unsigned h = 0u; h < HEADS; h++) {
        for (unsigned j = 0u; j < ROWS; j++) {
            ArenaMark mark = arena_mark(&scratch);
            snprintf(text, sizeof(text), "(directory-h%u %u directory-r%u tail)", h, j % 3u, j);
            assert(space_admit_atom(&space, &persistent, read_atom(&scratch, text)));
            arena_reset(&scratch, mark);
        }
    }
    /* Equal head/length/path descriptions with distinct index kinds coexist.
     * Their construction order differs from their canonical key order. */
    for (unsigned i = 0u; i < HEADS; i++) {
        unsigned h = (i * 37u) % HEADS;
        query(&space, &scratch, h, heads[h], values, false, true, true, false);
        query(&space, &scratch, h, heads[h], values, true, true, true, false);
    }
    CettaSpacePathIndexInfo info;
    assert(space_path_indexes_info(&space, &info) && info.index_count == 2u * HEADS);
    exercise_mutations(&space, &scratch, heads, values, true);
    exercise_unknown_heads(&space, &scratch, heads, values);
    space_path_indexes_retire_prefixes(&space);
    assert(space_path_indexes_info(&space, &info) && info.index_count == HEADS);
    for (unsigned h = 0u; h < HEADS; h++) {
        query(&space, &scratch, h, heads[h], values, false, false, true, false);
        query(&space, &scratch, h, heads[h], values, true, false, false, false);
    }
    exercise_mutations(&space, &scratch, heads, values, false);
    for (unsigned i = HEADS; i-- > 0u;) {
        unsigned h = (i * 37u) % HEADS;
        query(&space, &scratch, h, heads[h], values, true, true, true, false);
    }
    assert(space_path_indexes_info(&space, &info) && info.index_count == 2u * HEADS);
    space_path_indexes_retire(&space);
    assert(!space_path_indexes_info(&space, &info));
    query(&space, &scratch, 0u, heads[0], values, false, false, false, false);
    exercise_shared_budget(&universe, &scratch);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    puts("PASS: complete index keys survive mixed insertion, mutation, prefix retirement and rebuilding");
    return 0;
}
