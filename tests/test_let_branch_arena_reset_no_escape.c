/* No-escape probe for Upgrade A (per-branch eval-arena mark/reset).
 *
 * Premise under test: for an effect-only, unit-result let-direct branch whose
 * body is a grounded store like (add-atom &space X), NOTHING allocated in the
 * branch's eval-arena scratch escapes.  The native store deep-copies X into
 * term-universe (persistent-arena) storage, and the (unit) result () is a
 * hash-consed, malloc-owned atom.  Therefore resetting the per-branch eval
 * scratch after the effect commits is sound and caps peak scratch at O(1).
 *
 * This probe simulates N branches: mark the scratch arena, build heavy scratch
 * plus a row atom, store the row in a native Space, then reset the scratch to
 * the mark.  After all resets, every stored atom must still be queryable, and
 * the per-branch scratch peak must be O(1) in N.  A second check confirms the
 * unit atom is stable across resets when hash-consing is active.
 *
 * If any stored atom does NOT survive the reset, the premise is false and
 * Upgrade A must NOT ship. */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "atom.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "term_universe.h"
#include "tests/test_runtime_stats_stubs.h"

/* Minimal native-only space match backend (no mork bridge), mirroring the
 * stubs used by tests/test_space_term_universe_membership.c. */
void space_match_backend_init(Space *s) {
    memset(&s->match_backend, 0, sizeof(s->match_backend));
    s->match_backend.kind = SPACE_ENGINE_NATIVE;
}
void space_match_backend_free(Space *s) { (void)s; }
bool space_match_backend_try_set(Space *s, SpaceEngine kind) {
    if (s)
        s->match_backend.kind = kind;
    return true;
}
bool space_match_backend_needs_atom_on_add(const Space *s, AtomId atom_id) {
    (void)s;
    (void)atom_id;
    return false;
}
void space_match_backend_note_add(Space *s, AtomId atom_id, Atom *atom,
                                  CettaIndex atom_idx) {
    (void)s;
    (void)atom_id;
    (void)atom;
    (void)atom_idx;
}
void space_match_backend_note_remove(Space *s) { (void)s; }
CettaIndex space_match_backend_candidates64(Space *s, Atom *pattern,
                                            CettaIndex **out) {
    (void)s;
    (void)pattern;
    if (out)
        *out = NULL;
    return 0;
}
uint32_t space_match_backend_candidates(Space *s, Atom *pattern, uint32_t **out) {
    (void)s;
    (void)pattern;
    if (out)
        *out = NULL;
    return 0;
}
void space_match_backend_query(Space *s, Arena *a, Atom *query,
                               SubstMatchSet *out) {
    (void)s;
    (void)a;
    (void)query;
    if (out) {
        out->items = NULL;
        out->len = 0;
        out->cap = 0;
    }
}
void space_match_backend_query_conjunction(Space *s, Arena *a, Atom **patterns,
                                           uint32_t npatterns,
                                           const Bindings *seed,
                                           BindingSet *out) {
    (void)s;
    (void)a;
    (void)patterns;
    (void)npatterns;
    (void)seed;
    if (out) {
        out->items = NULL;
        out->len = 0;
        out->cap = 0;
    }
}
const char *space_match_backend_name(const Space *s) {
    (void)s;
    return "stub";
}
bool space_match_backend_supports_direct_bindings(const Space *s) {
    (void)s;
    return false;
}
const char *space_match_backend_kind_name(SpaceEngine kind) {
    (void)kind;
    return "stub";
}
bool space_match_backend_kind_from_name(const char *name, SpaceEngine *out) {
    (void)name;
    if (out)
        *out = SPACE_ENGINE_NATIVE;
    return true;
}
const char *space_match_backend_unavailable_reason(SpaceEngine kind) {
    (void)kind;
    return NULL;
}
bool space_match_backend_attach_act_file(Space *s, const char *path,
                                         uint64_t *out_loaded) {
    (void)s;
    (void)path;
    if (out_loaded)
        *out_loaded = 0;
    return false;
}
bool space_match_backend_materialize_attached(Space *s, Arena *persistent_arena) {
    (void)s;
    (void)persistent_arena;
    return true;
}
bool space_match_backend_materialize_native_storage(Space *s,
                                                    Arena *persistent_arena) {
    (void)s;
    (void)persistent_arena;
    return true;
}
bool space_match_backend_store_atom_id_direct(Space *s, AtomId atom_id,
                                              Atom *atom) {
    (void)s;
    (void)atom_id;
    (void)atom;
    return false;
}
bool space_match_backend_store_atom_direct(Space *s, Atom *atom) {
    (void)s;
    (void)atom;
    return false;
}
SpaceBackendBatchResult space_match_backend_store_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_added) {
    (void)s;
    (void)atom_ids;
    (void)atom_count;
    if (out_added)
        *out_added = 0;
    return SPACE_BACKEND_BATCH_UNSUPPORTED;
}
bool space_match_backend_remove_atom_id_direct(Space *s, AtomId atom_id) {
    (void)s;
    (void)atom_id;
    return false;
}
bool space_match_backend_remove_atom_direct(Space *s, Atom *atom) {
    (void)s;
    (void)atom;
    return false;
}
SpaceBackendBatchResult space_match_backend_remove_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_removed) {
    (void)s;
    (void)atom_ids;
    (void)atom_count;
    if (out_removed)
        *out_removed = 0;
    return SPACE_BACKEND_BATCH_UNSUPPORTED;
}
bool space_match_backend_truncate_direct(Space *s, uint32_t new_len) {
    (void)s;
    (void)new_len;
    return false;
}
bool space_match_backend_truncate_direct64(Space *s, uint64_t new_len) {
    if (new_len > UINT32_MAX)
        return false;
    return space_match_backend_truncate_direct(s, (uint32_t)new_len);
}
bool space_match_backend_load_sexpr_chunk(Space *s, Arena *persistent_arena,
                                          const uint8_t *text, size_t len,
                                          uint64_t *out_added) {
    (void)s;
    (void)persistent_arena;
    (void)text;
    (void)len;
    if (out_added)
        *out_added = 0;
    return false;
}
bool space_match_backend_remove_sexpr_chunk(Space *s, Arena *persistent_arena,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed) {
    (void)s;
    (void)persistent_arena;
    (void)text;
    (void)len;
    if (out_removed)
        *out_removed = 0;
    return false;
}
bool space_match_backend_step(Space *s, Arena *persistent_arena, uint64_t steps,
                              uint64_t *out_performed) {
    (void)s;
    (void)persistent_arena;
    (void)steps;
    if (out_performed)
        *out_performed = 0;
    return false;
}
bool space_match_backend_is_attached_compiled(const Space *s) {
    (void)s;
    return false;
}
bool space_match_backend_bridge_space(Space *s, CettaMorkSpaceHandle **out_bridge) {
    (void)s;
    if (out_bridge)
        *out_bridge = NULL;
    return false;
}
uint64_t space_match_backend_logical_len64(const Space *s) {
    return s ? s->len : 0;
}
static AtomId probe_native_atom_id_at(const Space *s, uint64_t idx) {
    if (!s || !s->atom_ids || idx >= s->len)
        return CETTA_ATOM_ID_NONE;
    return cetta_atom_id_storage_load_bits(
        s->atom_ids + ((size_t)(s->start + idx) *
                       cetta_atom_id_storage_width_bytes_from_bits(
                           s->atom_id_width_bits)),
        s->atom_id_width_bits);
}
AtomId space_match_backend_get_atom_id_at(const Space *s, uint32_t idx) {
    return probe_native_atom_id_at(s, idx);
}
AtomId space_match_backend_get_atom_id_at64(const Space *s, uint64_t idx) {
    return probe_native_atom_id_at(s, idx);
}
Atom *space_match_backend_get_at(const Space *s, uint32_t idx) {
    AtomId atom_id = space_match_backend_get_atom_id_at(s, idx);
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return NULL;
    return term_universe_get_atom(s->native.universe, atom_id);
}
Atom *space_match_backend_get_at64(const Space *s, uint64_t idx) {
    if (idx > UINT32_MAX)
        return NULL;
    return space_match_backend_get_at(s, (uint32_t)idx);
}
bool space_match_backend_mork_query_bindings_direct(CettaMorkSpaceHandle *bridge,
                                                    Arena *a, Atom *query,
                                                    BindingSet *out) {
    (void)bridge;
    (void)a;
    (void)query;
    if (out) {
        out->items = NULL;
        out->len = 0;
        out->cap = 0;
    }
    return false;
}
bool space_match_backend_mork_query_conjunction_direct(
    CettaMorkSpaceHandle *bridge, Arena *a, Atom **patterns,
    CettaExprLen npatterns, const Bindings *seed, BindingSet *out) {
    (void)bridge;
    (void)a;
    (void)patterns;
    (void)npatterns;
    (void)seed;
    if (out) {
        out->items = NULL;
        out->len = 0;
        out->cap = 0;
    }
    return false;
}
void space_match_backend_print_inventory(FILE *out) { (void)out; }

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    Arena persistent;
    Arena scratch;
    arena_init(&persistent);
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_EVAL);
    arena_set_runtime_kind(&persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);

    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    Space space;
    space_init_with_universe(&space, &universe);

    SymbolId friend = symbol_intern_cstr(g_symbols, "friend");
    SymbolId sam = symbol_intern_cstr(g_symbols, "sam");

    const int N = 4000;
    size_t peak_branch_live = 0;

    for (int i = 0; i < N; i++) {
        /* --- begin simulated let-direct branch --- */
        ArenaMark mark = arena_mark(&scratch);

        /* Heavy per-branch scratch: substitution body, intermediates. */
        Atom *row = atom_expr3(&scratch,
                               atom_symbol_id(&scratch, friend),
                               atom_symbol_id(&scratch, sam),
                               atom_int(&scratch, i));
        for (int k = 0; k < 16; k++) {
            Atom *junk = atom_expr2(&scratch, row, atom_int(&scratch, k));
            (void)atom_expr2(&scratch, junk, junk);
        }

        /* The effect: store the row into the native space (deep-copies into
         * the persistent term universe, NOT into `scratch`). */
        space_add(&space, row);

        /* Sample per-branch scratch peak right before the reset. */
        if (scratch.live_bytes > peak_branch_live)
            peak_branch_live = scratch.live_bytes;

        /* Upgrade A: reclaim the branch scratch. */
        arena_reset(&scratch, mark);
        /* --- end simulated let-direct branch --- */
    }

    /* Survival: every stored row must still be present and queryable after all
     * the per-branch resets. */
    assert(space_length64(&space) == (CettaCount)N);
    int checked = 0;
    for (int i = 0; i < N; i++) {
        Atom *q = atom_expr3(&persistent,
                             atom_symbol_id(&persistent, friend),
                             atom_symbol_id(&persistent, sam),
                             atom_int(&persistent, i));
        AtomId id = term_universe_lookup_atom_id(&universe, q);
        assert(id != CETTA_ATOM_ID_NONE);
        Atom *got = space_get_at64(&space, (CettaIndex)i);
        assert(got != NULL);
        assert(atom_eq(got, q));
        checked++;
    }
    assert(checked == N);

    /* Scratch peak must be O(1) in N (independent of loop length). */
    fprintf(stderr,
            "no_escape_probe: N=%d survivors=%d branch_peak_live_bytes=%zu\n",
            N, checked, peak_branch_live);
    assert(peak_branch_live < 8192);

    /* Unit-result stability: when hash-consing is active (as in the real
     * runtime), atom_unit() returns a stable, malloc-owned atom that survives
     * an eval-arena reset. */
    {
        HashConsTable hc;
        hashcons_init(&hc);
        g_hashcons = &hc;
        arena_set_hashcons(&scratch, &hc);
        ArenaMark mark = arena_mark(&scratch);
        Atom *u1 = atom_unit(&scratch);
        assert(u1 != NULL && u1->kind == ATOM_EXPR && u1->expr.len == 0);
        arena_reset(&scratch, mark);
        Atom *u2 = atom_unit(&scratch);
        assert(u1 == u2); /* stable shared pointer, survived the reset */
        assert(u2->kind == ATOM_EXPR && u2->expr.len == 0);
        g_hashcons = NULL;
        arena_set_hashcons(&scratch, NULL);
        hashcons_free(&hc);
    }

    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    puts("PASS: let-branch arena reset no-escape probe");
    return 0;
}
