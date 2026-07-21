#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abt.h"
#include "atom.h"
#include "mm2_lower.h"
#include "symbol.h"

enum { ABT_MM2_EXPECTED_CHECKS = 10 };

static unsigned checks = 0;
static unsigned failures = 0;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                          \
            failures++;                                                       \
        }                                                                      \
    } while (0)

static Atom *node1(Arena *arena, const char *head, Atom *child) {
    return atom_expr2(arena, atom_symbol(arena, head), child);
}

static Atom *node2(Arena *arena, const char *head, Atom *left, Atom *right) {
    return atom_expr3(arena, atom_symbol(arena, head), left, right);
}

static uint32_t read_u32_be(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    AbtSignature signature;
    uint8_t *plain_bytes = NULL;
    size_t plain_len = 0;
    uint8_t *canonical_bytes = NULL;
    size_t canonical_len = 0;
    uint8_t *canonical_context = NULL;
    size_t canonical_context_len = 0;
    uint8_t *meta_bytes = NULL;
    size_t meta_len = 0;
    uint8_t *meta_context = NULL;
    size_t meta_context_len = 0;
    const char *error = NULL;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    abt_signature_init(&signature);
    CHECK(abt_signature_add_defaults(&signature, &arena),
          "default ABT signature admitted at MM2 boundary");

    Atom *type = node1(&arena, "Con", atom_symbol(&arena, "A"));
    Atom *bound = node1(&arena, "idx", atom_int(&arena, 0));
    Atom *canonical = node2(&arena, "Lam", type, bound);
    Atom *loose = node1(&arena, "idx", atom_int(&arena, 0));
    CHECK(abt_scope_check(&signature, 0u, canonical),
          "closed canonical binder is admitted before transport");
    CHECK(!abt_scope_check(&signature, 0u, loose),
          "loose canonical index is rejected before transport");

    bool canonical_ok = cetta_mm2_atom_to_contextual_bridge_expr_bytes(
        &arena, canonical,
        &canonical_bytes, &canonical_len,
        &canonical_context, &canonical_context_len, &error);
    CHECK(canonical_ok,
          "closed canonical binder encodes through the contextual bridge");
    CHECK(canonical_ok && canonical_context_len == 4u &&
              read_u32_be(canonical_context) == 0u,
          "de Bruijn indices never enter the bridge metavariable context");

    error = NULL;
    bool plain_ok = cetta_mm2_atom_to_bridge_expr_bytes(
        &arena, canonical, &plain_bytes, &plain_len, &error);
    CHECK(plain_ok, "closed canonical binder encodes through the plain bridge");
    CHECK(plain_ok && canonical_ok && plain_len == canonical_len &&
              memcmp(plain_bytes, canonical_bytes, plain_len) == 0,
          "plain and contextual bridges preserve identical canonical structure");

    Atom *meta = atom_var_with_id(
        &arena, "m", ((VarId)1u << 32) | (VarId)7u);
    Atom *open_schema = node2(&arena, "App", canonical, meta);
    error = NULL;
    bool meta_ok = cetta_mm2_atom_to_contextual_bridge_expr_bytes(
        &arena, open_schema,
        &meta_bytes, &meta_len,
        &meta_context, &meta_context_len, &error);
    CHECK(meta_ok,
          "schema metavariable encodes through the contextual bridge");
    CHECK(meta_ok && meta_context_len >= 4u &&
              read_u32_be(meta_context) == 1u,
          "only CeTTa metavariables enter the bridge opening context");
    CHECK(meta_ok && meta_len > canonical_len,
          "schema transport retains the metavariable-bearing outer application");

    free(plain_bytes);
    free(canonical_bytes);
    free(canonical_context);
    free(meta_bytes);
    free(meta_context);
    abt_signature_free(&signature);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;

    if (checks != ABT_MM2_EXPECTED_CHECKS) {
        fprintf(stderr, "FAIL: ABT/MM2 boundary executed %u/%u checks\n",
                checks, ABT_MM2_EXPECTED_CHECKS);
        failures++;
    }
    printf("(ABTMM2BoundarySummary %u %u %u)\n", checks,
           checks >= failures ? checks - failures : 0u, failures);
    if (failures != 0u) return 1;
    puts("PASS: closed ABT/MM2/MORK boundary");
    return 0;
}
