#include <stdio.h>

#include "atom.h"
#include "name_key.h"
#include "symbol.h"

static unsigned checks = 0u;
static unsigned failures = 0u;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                          \
            failures++;                                                       \
        }                                                                      \
    } while (0)

static Atom *node1(Arena *arena, const char *head, Atom *value) {
    return atom_expr2(arena, atom_symbol(arena, head), value);
}

int main(void) {
    SymbolTable symbols;
    Arena inputs;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&inputs);

    NameKeyTable *table = name_key_table_new_with_hash_mask(8u, 0u);
    CHECK(table != NULL, "name-key table initializes");

    Atom *doc = atom_symbol(&inputs, "doc");
    Atom *doc_copy = atom_symbol(&inputs, "doc");
    Atom *agent = node1(
        &inputs, "agent", atom_string(&inputs, "Max Botnick"));
    Atom *state = node1(&inputs, "state", atom_string(&inputs, "Max Botnick"));

    NameId doc_id = name_key_intern(table, doc);
    CHECK(doc_id != NAME_ID_NONE, "symbol key interns");
    CHECK(name_key_intern(table, doc) == doc_id,
          "same pointer reuses name identity");
    CHECK(name_key_intern(table, doc_copy) == doc_id,
          "structurally equal atom reuses name identity");

    NameId agent_id = name_key_intern(table, agent);
    NameId state_id = name_key_intern(table, state);
    CHECK(agent_id != NAME_ID_NONE && state_id != NAME_ID_NONE,
          "structured keys intern");
    CHECK(agent_id != state_id,
          "digest collision does not establish name equality");
    CHECK(atom_eq((Atom *)name_key_lookup(table, agent_id), agent),
          "NameId lookup preserves the structural key");
    CHECK(name_key_count(table) == 3u,
          "only structurally distinct keys allocate identities");
    CHECK(name_key_find(table, agent) == agent_id,
          "non-mutating lookup finds a structural key");
    Atom *unknown = node1(&inputs, "agent", atom_string(&inputs, "Oruzi"));
    CHECK(name_key_find(table, unknown) == NAME_ID_NONE,
          "non-mutating lookup rejects an unknown key");
    CHECK(name_key_count(table) == 3u,
          "unknown lookup does not allocate a name identity");

    Atom *open = node1(&inputs, "agent", atom_var(&inputs, "who"));
    CHECK(name_key_intern(table, open) == NAME_ID_NONE,
          "open structural key fails closed");
    Atom *contextual = atom_space(&inputs, NULL);
    CHECK(name_key_intern(table, contextual) == NAME_ID_NONE,
          "contextual grounded key fails closed");
    Atom *cycle = node1(&inputs, "loop", atom_symbol(&inputs, "seed"));
    cycle->expr.elems[1] = cycle;
    CHECK(name_key_intern(table, cycle) == NAME_ID_NONE,
          "cyclic structural key fails closed");
    CHECK(name_key_lookup(table, NAME_ID_NONE) == NULL,
          "invalid NameId lookup fails closed");
    CHECK(name_key_lookup(table, 999u) == NULL,
          "unknown NameId lookup fails closed");

    bool scale_interned = true;
    Atom *scale_keys[128];
    NameId scale_ids[128];
    for (uint32_t i = 0u; i < 128u; i++) {
        scale_keys[i] = node1(&inputs, "slot", atom_int(&inputs, (int64_t)i));
        scale_ids[i] = name_key_intern(table, scale_keys[i]);
        if (scale_ids[i] == NAME_ID_NONE)
            scale_interned = false;
    }
    CHECK(scale_interned, "structural keys survive interner growth");
    CHECK(name_key_count(table) == 131u,
          "interner growth preserves exact key cardinality");
    bool scale_found = true;
    for (uint32_t i = 0u; i < 128u; i++)
        if (name_key_find(table, scale_keys[i]) != scale_ids[i])
            scale_found = false;
    CHECK(scale_found, "rehash preserves every structural identity");

    printf("(NameKeySummary %u %u %u)\n", checks, checks - failures, failures);
    name_key_table_delete(table);
    arena_free(&inputs);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failures == 0u && checks == 19u ? 0 : 1;
}
