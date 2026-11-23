/*
 * A space keeps its generated name (space.h, name_supply.h) through every
 * operation that keeps the space: new contents (space_replace_contents) and
 * a removal of every row, which gives the space new, empty contents.  A name
 * beyond the root block holds its block: the space holds it through those
 * operations and gives it back when it is freed.  The space whose contents
 * moved gains no name, and a space without one gains none.  Run once per
 * mode, as test_name_supply is: an unforgeable name is hashed and holds no
 * block.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "atom.h"
#include "name_supply.h"
#include "space.h"
#include "symbol.h"

static int g_failures;

#define CHECK(condition, ...)                                         \
    do {                                                              \
        if (!(condition)) {                                           \
            g_failures++;                                             \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                    \
            fputc('\n', stderr);                                      \
        }                                                             \
    } while (0)

static bool keeps_name(const Space *space, CettaGeneratedName name) {
    CettaGeneratedName now;
    return space_generated_name(space, &now) &&
           cetta_generated_name_eq(now, name);
}

static bool holds_only(const Space *space, Atom *atom) {
    Atom *row = space_length64(space) == 1u ? space_get_at64(space, 0u) : NULL;
    return row && atom_eq(row, atom);
}

int main(void) {
    char error[200];
    if (!cetta_name_supply_configure(NULL, NULL, error, sizeof(error))) {
        fprintf(stderr, "FAIL: the environment's name mode reads: %s\n", error);
        return 1;
    }
    bool unforgeable = cetta_name_supply_unforgeable();
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    VarInternTable vars;
    var_intern_init(&vars);
    g_var_intern = &vars;
    Arena arena;
    arena_init_detached(&arena);
    Atom *before = atom_symbol(&arena, "before");
    Atom *after = atom_symbol(&arena, "after");

    /* A name past the root's counter, which the counter supply gives in a
       new block. */
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    root.drawn[CETTA_NAME_KIND_SPACE] = UINT64_MAX;
    CettaGeneratedName name =
        cetta_name_supply_fresh(&root, CETTA_NAME_KIND_SPACE);
    CHECK((cetta_generated_name_block(name) != NULL) == !unforgeable,
          "a %s name past the root's counter %s a block",
          unforgeable ? "hashed" : "counted",
          unforgeable ? "holds no" : "holds");
    Space named, other;
    space_init(&named);
    space_init(&other);
    named.generated_name = cetta_generated_name_retain(name);
    cetta_name_supply_dispose(&root);
    uint64_t held = unforgeable ? 0u : 1u;
    CHECK(cetta_name_supply_live_blocks() == held,
          "the space alone holds its name's block (%llu alive)",
          (unsigned long long)cetta_name_supply_live_blocks());

    space_add(&named, before);
    space_add(&other, after);
    space_replace_contents(&named, &other);
    CHECK(holds_only(&named, after), "the space has the new contents");
    CHECK(keeps_name(&named, name), "new contents keep the space's name");
    CHECK(cetta_name_supply_live_blocks() == held,
          "new contents keep the name's block (%llu alive)",
          (unsigned long long)cetta_name_supply_live_blocks());
    CHECK(!space_generated_name(&other, NULL),
          "the space the contents left gains no name");

    uint8_t every_row[1] = {1u};
    CettaCount removed = 0u;
    CHECK(space_remove_occurrence_mask_stable(&named, every_row, 1u,
                                              &removed) &&
              removed == 1u && space_length64(&named) == 0u,
          "removing every row empties the space");
    CHECK(keeps_name(&named, name), "an emptied space keeps its name");
    CHECK(cetta_name_supply_live_blocks() == held,
          "an emptied space keeps its name's block (%llu alive)",
          (unsigned long long)cetta_name_supply_live_blocks());

    /* A space without a generated name gains none from new contents. */
    Space unnamed, contents;
    space_init(&unnamed);
    space_init(&contents);
    space_add(&contents, before);
    space_replace_contents(&unnamed, &contents);
    CHECK(holds_only(&unnamed, before) &&
              !space_generated_name(&unnamed, NULL),
          "a space without a name gains none from new contents");
    space_free(&unnamed);
    space_free(&contents);

    space_free(&named);
    CHECK(cetta_name_supply_live_blocks() == 0u,
          "freed, the space gives its name's block back (%llu alive)",
          (unsigned long long)cetta_name_supply_live_blocks());
    space_free(&other);
    arena_free(&arena);
    if (g_failures)
        return 1;
    printf("PASS: a space keeps its generated name (%s mode) and the name's "
           "block through new contents and an emptying removal, and gives "
           "the block back when freed\n",
           unforgeable ? "unforgeable" : "counter");
    return 0;
}
