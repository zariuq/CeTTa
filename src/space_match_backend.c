#include "space.h"
#include "eval.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "parser.h"
#include "stats.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define IMPORTED_COREF_LIMIT 144
#define IMPORTED_MORK_QUERY_ONLY_V2_MAGIC 0x43544252u
#define IMPORTED_MORK_QUERY_ONLY_V2_VERSION 2u
#define IMPORTED_MORK_MULTI_REF_V3_VERSION 3u
#define IMPORTED_MORK_CONTEXTUAL_ROWS_WIRE_VERSION 4u
#define IMPORTED_MORK_CONTEXTUAL_EXACT_ROWS_FLAGS 0x0000u
#define IMPORTED_MORK_CONTEXTUAL_QUERY_ROWS_FLAGS 0x0000u
#define IMPORTED_MORK_OPEN_REF_EXACT 0u
#define IMPORTED_MORK_OPEN_REF_QUERY_SLOT 1u
#define IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY 0x0001u
#define IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES 0x0002u
#define IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS 0x0004u
#define IMPORTED_MORK_MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES 0x0008u
#define IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS 0x0010u
#define IMPORTED_MORK_TAG_VARREF_MASK 0xC0u
#define IMPORTED_MORK_TAG_VARREF_PREFIX 0x80u
#define IMPORTED_MORK_TAG_SYMBOL_PREFIX 0xC0u
#define IMPORTED_MORK_TAG_NEWVAR 0xC0u
#define IMPORTED_MORK_WIDE_TAG_ARITY 0x00u
#define IMPORTED_MORK_WIDE_TAG_SYMBOL 0x01u
#define IMPORTED_MORK_WIDE_TAG_NEWVAR 0x02u
#define IMPORTED_MORK_WIDE_TAG_VARREF 0x03u
#define IMPORTED_CONJUNCTION_PATTERN_LIMIT 32u

static int cmp_uint32(const void *a, const void *b) {
    uint32_t va = *(const uint32_t *)a, vb = *(const uint32_t *)b;
    return (va > vb) - (va < vb);
}

static bool pathmap_local_ensure_bridge_space(PathmapLocalState *st);
static bool mork_imported_ensure_bridge_space(MorkImportedState *st);
static bool backend_rebuild_bridge(Space *s);
static uint32_t imported_logical_len(const Space *s);
static void native_candidate_exact_query(Space *s, Arena *a, Atom *query,
                                         SubstMatchSet *out);
static void imported_epoch_query_candidates(Space *s, Arena *a, Atom *query,
                                            SubstMatchSet *out);
static bool imported_bridge_query_conjunction_fast(Space *s, Arena *a,
                                                   Atom **patterns, uint32_t npatterns,
                                                   const Bindings *seed,
                                                   BindingSet *out);
static bool imported_bridge_query_text_has_internal_vars(const char *text);
static bool imported_text_may_contain_vars(const uint8_t *text, size_t len);
static bool imported_materialize_bridge_space(Space *dst,
                                              Arena *persistent_arena,
                                              CettaMorkSpaceHandle *bridge,
                                              uint64_t *out_loaded);
static void space_query_conjunction_default(Space *s, Arena *a,
                                            Atom **patterns, uint32_t npatterns,
                                            const Bindings *seed,
                                            BindingSet *out);
static void imported_projection_clear(ImportedBridgeState *st);
static bool imported_storage_ensure_projection(Space *s);
static bool imported_shadow_refresh_from_projection(Space *s);
static uint32_t bridge_space_logical_len(const ImportedBridgeState *st);
static bool pathmap_local_ensure_bridge_live(Space *s);
static bool pathmap_local_apply_text_chunk_direct(Space *s,
                                                  const uint8_t *text,
                                                  size_t len,
                                                  bool remove_atoms,
                                                  uint64_t *out_changed);

static uint32_t sort_unique_uint32(uint32_t *items, uint32_t len) {
    if (!items || len <= 1)
        return len;
    qsort(items, len, sizeof(uint32_t), cmp_uint32);
    uint32_t w = 1;
    for (uint32_t r = 1; r < len; r++) {
        if (items[r] != items[r - 1])
            items[w++] = items[r];
    }
    return w;
}

static bool backend_uses_bridge_adapter(const Space *s) {
    return s && s->match_backend.kind == SPACE_ENGINE_MORK;
}

static ImportedBridgeState *backend_bridge_state(Space *s) {
    if (!s)
        return NULL;
    switch (s->match_backend.kind) {
    case SPACE_ENGINE_PATHMAP:
        return &s->match_backend.pathmap.bridge;
    case SPACE_ENGINE_MORK:
        return &s->match_backend.mork.bridge;
    default:
        return NULL;
    }
}

static const ImportedBridgeState *backend_bridge_state_const(const Space *s) {
    if (!s)
        return NULL;
    switch (s->match_backend.kind) {
    case SPACE_ENGINE_PATHMAP:
        return &s->match_backend.pathmap.bridge;
    case SPACE_ENGINE_MORK:
        return &s->match_backend.mork.bridge;
    default:
        return NULL;
    }
}

static MorkImportedState *mork_imported_state(Space *s) {
    return s ? &s->match_backend.mork : NULL;
}

static const MorkImportedState *mork_imported_state_const(const Space *s) {
    return s ? &s->match_backend.mork : NULL;
}

static SymbolId atom_head_sym(Atom *a) {
    if (a->kind == ATOM_SYMBOL) return a->sym_id;
    if (a->kind == ATOM_EXPR && a->expr.len > 0 &&
        a->expr.elems[0]->kind == ATOM_SYMBOL)
        return a->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

static bool native_atom_id_insertable(const TermUniverse *universe,
                                      AtomId atom_id) {
    if (!universe || atom_id == CETTA_ATOM_ID_NONE || !tu_hdr(universe, atom_id))
        return false;
    if (tu_kind(universe, atom_id) != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < tu_arity(universe, atom_id); i++) {
        if (!native_atom_id_insertable(universe, tu_child(universe, atom_id, i)))
            return false;
    }
    return true;
}

static void native_insert_match_trie_entry(Space *s, uint32_t atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    AtomId atom_id = space_get_atom_id_at(s, atom_idx);
    if (native_atom_id_insertable(s->native.universe, atom_id) &&
        disc_insert_id(st->match_trie, s->native.universe, atom_id, atom_idx)) {
        return;
    }
    Atom *atom = space_get_at(s, atom_idx);
    if (atom)
        disc_insert(st->match_trie, atom, atom_idx);
}

static void native_insert_stree_entry(Space *s, uint32_t atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    Atom *atom = space_get_at(s, atom_idx);
    if (atom)
        stree_insert(st->stree, atom, atom_idx);
}

static __attribute__((unused)) bool imported_atom_has_vars(const Atom *atom) {
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return true;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (imported_atom_has_vars(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool imported_atom_has_epoch_vars(const Atom *atom) {
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return var_epoch_suffix(atom->var_id) != 0;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (imported_atom_has_epoch_vars(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool imported_atom_has_bridge_vars(const Atom *atom) {
    const char *text;
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
    case ATOM_SYMBOL: {
        text = symbol_bytes(g_symbols, atom->sym_id);
        return text &&
               (strncmp(text, "$__mork_", 8) == 0 ||
                strncmp(text, "$$__mork_", 9) == 0);
    }
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (imported_atom_has_bridge_vars(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool imported_bridge_internal_var(const Atom *atom) {
    const char *text;
    if (!atom || atom->kind != ATOM_VAR)
        return false;
    text = symbol_bytes(g_symbols, atom->sym_id);
    return text &&
           (strncmp(text, "$__mork_", 8) == 0 ||
            strncmp(text, "$$__mork_", 9) == 0);
}

static bool subst_match_same(const SubstMatch *a, const SubstMatch *b) {
    return a->atom_idx == b->atom_idx &&
           a->epoch == b->epoch &&
           a->exact == b->exact &&
           bindings_eq((Bindings *)&a->bindings, (Bindings *)&b->bindings);
}

static void subst_matchset_push(SubstMatchSet *out, uint32_t atom_idx,
                                uint32_t epoch, const Bindings *bindings,
                                bool exact) {
    if (out->len >= out->cap) {
        out->cap = out->cap ? out->cap * 2 : 8;
        out->items = cetta_realloc(out->items, sizeof(SubstMatch) * out->cap);
    }
    out->items[out->len].atom_idx = atom_idx;
    out->items[out->len].epoch = epoch;
    if (!bindings_clone(&out->items[out->len].bindings, bindings))
        return;
    out->items[out->len].exact = exact;
    out->len++;
}

static void subst_match_move(SubstMatch *dst, SubstMatch *src) {
    dst->atom_idx = src->atom_idx;
    dst->epoch = src->epoch;
    bindings_move(&dst->bindings, &src->bindings);
    dst->exact = src->exact;
}

static void subst_matchset_normalize(SubstMatchSet *out) {
    if (out->len <= 1)
        return;
    for (uint32_t i = 1; i < out->len; i++) {
        SubstMatch tmp;
        subst_match_move(&tmp, &out->items[i]);
        uint32_t j = i;
        while (j > 0 &&
               (out->items[j - 1].atom_idx > tmp.atom_idx ||
                (out->items[j - 1].atom_idx == tmp.atom_idx &&
                 out->items[j - 1].epoch > tmp.epoch))) {
            subst_match_move(&out->items[j], &out->items[j - 1]);
            j--;
        }
        subst_match_move(&out->items[j], &tmp);
    }
    uint32_t w = 1;
    for (uint32_t r = 1; r < out->len; r++) {
        if (!subst_match_same(&out->items[r], &out->items[r - 1])) {
            if (w != r)
                subst_match_move(&out->items[w], &out->items[r]);
            w++;
        } else {
            bindings_free(&out->items[r].bindings);
        }
    }
    out->len = w;
}

static void native_rebuild_match_trie(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    disc_node_free(st->match_trie);
    st->match_trie = disc_node_new();
    for (uint32_t i = 0; i < s->native.len; i++)
        native_insert_match_trie_entry(s, i);
    st->match_trie_dirty = false;
}

static void native_ensure_match_trie(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->match_trie) {
        st->match_trie = disc_node_new();
        for (uint32_t i = 0; i < s->native.len; i++)
            native_insert_match_trie_entry(s, i);
        st->match_trie_dirty = false;
    } else if (st->match_trie_dirty) {
        native_rebuild_match_trie(s);
    }
}

static void native_ensure_stree(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->stree) {
        st->stree = cetta_malloc(sizeof(SubstTree));
        stree_init(st->stree);
        for (uint32_t i = 0; i < s->native.len; i++)
            native_insert_stree_entry(s, i);
        st->stree_dirty = false;
    } else if (st->stree_dirty) {
        stree_free(st->stree);
        stree_init(st->stree);
        for (uint32_t i = 0; i < s->native.len; i++)
            native_insert_stree_entry(s, i);
        st->stree_dirty = false;
    }
}

static void native_free(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    disc_node_free(st->match_trie);
    st->match_trie = NULL;
    st->match_trie_dirty = false;
    if (st->stree) {
        stree_free(st->stree);
        free(st->stree);
        st->stree = NULL;
    }
    st->stree_dirty = false;
}

static void native_note_add(Space *s, AtomId atom_id, Atom *atom, uint32_t atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (st->match_trie) {
        if (!(native_atom_id_insertable(s->native.universe, atom_id) &&
              disc_insert_id(st->match_trie, s->native.universe, atom_id, atom_idx)) &&
            atom) {
            disc_insert(st->match_trie, atom, atom_idx);
        }
    }
    if (st->stree) {
        if (atom) {
            stree_insert(st->stree, atom, atom_idx);
        } else {
            st->stree_dirty = true;
        }
    }
}

static void native_note_remove(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    (void)s;
    st->match_trie_dirty = true;
    st->stree_dirty = true;
}

static bool match_space_atom_epoch(Space *s, uint32_t atom_idx, Atom *query,
                                   Bindings *b, Arena *a, uint32_t epoch) {
    if (!s || !query || !b || atom_idx >= s->native.len)
        return false;
    AtomId atom_id = space_get_atom_id_at(s, atom_idx);
    return match_atoms_atom_id_epoch(query, s->native.universe, atom_id, b, a, epoch);
}

static bool imported_bridge_add_atom_bytes(CettaMorkSpaceHandle *bridge_space,
                                           const uint8_t *expr_bytes,
                                           size_t expr_len) {
    return bridge_space && expr_bytes &&
           cetta_mork_bridge_space_add_expr_bytes(bridge_space, expr_bytes,
                                                  expr_len, NULL);
}

static bool imported_bridge_add_atom_structural(Arena *scratch,
                                                CettaMorkSpaceHandle *bridge_space,
                                                const TermUniverse *universe,
                                                AtomId atom_id,
                                                Atom *fallback_atom) {
    uint8_t *expr_bytes = NULL;
    size_t expr_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && universe && tu_hdr(universe, atom_id)) {
        ok = cetta_mm2_atom_id_to_bridge_expr_bytes(
            scratch, universe, atom_id, &expr_bytes, &expr_len, &encode_error);
    } else if (bridge_space && fallback_atom) {
        ok = cetta_mm2_atom_to_bridge_expr_bytes(
            scratch, fallback_atom, &expr_bytes, &expr_len, &encode_error);
    } else if (bridge_space && universe && atom_id != CETTA_ATOM_ID_NONE) {
        Atom *decoded = term_universe_get_atom(universe, atom_id);
        if (decoded) {
            ok = cetta_mm2_atom_to_bridge_expr_bytes(
                scratch, decoded, &expr_bytes, &expr_len, &encode_error);
        }
    }

    if (ok && imported_bridge_add_atom_bytes(bridge_space, expr_bytes, expr_len)) {
        free(expr_bytes);
        return true;
    }
    free(expr_bytes);
    return false;
}

static bool imported_bridge_add_atom_contextual_exact(Arena *scratch,
                                                      CettaMorkSpaceHandle *bridge_space,
                                                      const TermUniverse *universe,
                                                      AtomId atom_id) {
    uint8_t *expr_bytes = NULL;
    uint8_t *context_bytes = NULL;
    size_t expr_len = 0;
    size_t context_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (!bridge_space || !universe || !tu_hdr(universe, atom_id))
        return false;

    ok = cetta_mm2_atom_id_to_contextual_bridge_expr_bytes(
        scratch, universe, atom_id, &expr_bytes, &expr_len, &context_bytes,
        &context_len, &encode_error);
    if (ok) {
        ok = cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
            bridge_space, expr_bytes, expr_len, context_bytes, context_len, NULL);
    }
    free(expr_bytes);
    free(context_bytes);
    return ok;
}

static bool imported_bridge_remove_atom_structural(Arena *scratch,
                                                   CettaMorkSpaceHandle *bridge_space,
                                                   const TermUniverse *universe,
                                                   AtomId atom_id,
                                                   Atom *fallback_atom,
                                                   uint64_t *out_removed) {
    uint8_t *expr_bytes = NULL;
    size_t expr_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && universe && tu_hdr(universe, atom_id)) {
        ok = cetta_mm2_atom_id_to_bridge_expr_bytes(
            scratch, universe, atom_id, &expr_bytes, &expr_len, &encode_error);
    } else if (bridge_space && fallback_atom) {
        ok = cetta_mm2_atom_to_bridge_expr_bytes(
            scratch, fallback_atom, &expr_bytes, &expr_len, &encode_error);
    } else if (bridge_space && universe && atom_id != CETTA_ATOM_ID_NONE) {
        Atom *decoded = term_universe_get_atom(universe, atom_id);
        if (decoded) {
            ok = cetta_mm2_atom_to_bridge_expr_bytes(
                scratch, decoded, &expr_bytes, &expr_len, &encode_error);
        }
    }

    if (ok && cetta_mork_bridge_space_remove_expr_bytes(
                  bridge_space, expr_bytes, expr_len, out_removed)) {
        free(expr_bytes);
        return true;
    }
    free(expr_bytes);
    return false;
}

static bool imported_bridge_remove_atom_contextual_exact(Arena *scratch,
                                                         CettaMorkSpaceHandle *bridge_space,
                                                         const TermUniverse *universe,
                                                         AtomId atom_id,
                                                         uint64_t *out_removed) {
    uint8_t *expr_bytes = NULL;
    uint8_t *context_bytes = NULL;
    size_t expr_len = 0;
    size_t context_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && universe && tu_hdr(universe, atom_id)) {
        ok = cetta_mm2_atom_id_to_contextual_bridge_expr_bytes(
            scratch, universe, atom_id, &expr_bytes, &expr_len, &context_bytes,
            &context_len, &encode_error);
    }

    if (ok && cetta_mork_bridge_space_remove_contextual_exact_expr_bytes(
                  bridge_space, expr_bytes, expr_len, context_bytes,
                  context_len, out_removed)) {
        free(expr_bytes);
        free(context_bytes);
        return true;
    }
    free(expr_bytes);
    free(context_bytes);
    return false;
}

static bool imported_bridge_remove_atom_contextual_exact_atom(Arena *scratch,
                                                              CettaMorkSpaceHandle *bridge_space,
                                                              Atom *atom,
                                                              uint64_t *out_removed) {
    uint8_t *expr_bytes = NULL;
    uint8_t *context_bytes = NULL;
    size_t expr_len = 0;
    size_t context_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && atom) {
        ok = cetta_mm2_atom_to_contextual_bridge_expr_bytes(
            scratch, atom, &expr_bytes, &expr_len, &context_bytes,
            &context_len, &encode_error);
    }

    if (ok && cetta_mork_bridge_space_remove_contextual_exact_expr_bytes(
                  bridge_space, expr_bytes, expr_len, context_bytes,
                  context_len, out_removed)) {
        free(expr_bytes);
        free(context_bytes);
        return true;
    }
    free(expr_bytes);
    free(context_bytes);
    return false;
}

static bool imported_bridge_contains_atom_structural(Arena *scratch,
                                                     CettaMorkSpaceHandle *bridge_space,
                                                     Atom *atom,
                                                     bool *out_found) {
    uint8_t *expr_bytes = NULL;
    size_t expr_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (out_found)
        *out_found = false;
    if (!scratch || !bridge_space || !atom)
        return false;

    ok = cetta_mm2_atom_to_bridge_expr_bytes(
        scratch, atom, &expr_bytes, &expr_len, &encode_error);
    if (!ok) {
        free(expr_bytes);
        return false;
    }

    ok = cetta_mork_bridge_space_contains_expr_bytes(
        bridge_space, expr_bytes, expr_len, out_found);
    free(expr_bytes);
    return ok;
}

static void imported_binding_set_to_exact_matches(SubstMatchSet *out,
                                                  const BindingSet *matches) {
    smset_init(out);
    if (!matches)
        return;
    for (uint32_t i = 0; i < matches->len; i++)
        subst_matchset_push(out, 0, 0, &matches->items[i], true);
}

static uint32_t native_candidates(Space *s, Atom *pattern, uint32_t **out) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (s->native.len <= MATCH_TRIE_THRESHOLD) {
        *out = cetta_malloc(sizeof(uint32_t) * (s->native.len ? s->native.len : 1));
        for (uint32_t i = 0; i < s->native.len; i++) (*out)[i] = i;
        return s->native.len;
    }
    native_ensure_match_trie(s);
    uint32_t ncand = 0, ccand = 0;
    disc_lookup(st->match_trie, pattern, out, &ncand, &ccand);
    if (ncand > 1) {
        qsort(*out, ncand, sizeof(uint32_t), cmp_uint32);
        uint32_t w = 1;
        for (uint32_t r = 1; r < ncand; r++)
            if ((*out)[r] != (*out)[r - 1]) (*out)[w++] = (*out)[r];
        ncand = w;
    }
    return ncand;
}

static void native_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    smset_init(out);
    if (s->native.len == 0) return;
    if (s->native.len <= MATCH_TRIE_THRESHOLD) {
        for (uint32_t i = 0; i < s->native.len; i++) {
            uint32_t epoch = fresh_var_suffix();
            Bindings b;
            bindings_init(&b);
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_NATIVE_QUERY);
            if (match_space_atom_epoch(s, i, query, &b, a, epoch) &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, i, epoch, &b, false);
            }
            bindings_free(&b);
        }
        return;
    }
    native_ensure_stree(s);

    SymbolId head = SYMBOL_ID_NONE;
    if (query->kind == ATOM_SYMBOL) head = query->sym_id;
    else if (query->kind == ATOM_EXPR && query->expr.len > 0 &&
             query->expr.elems[0]->kind == ATOM_SYMBOL)
        head = query->expr.elems[0]->sym_id;

    if (head != SYMBOL_ID_NONE) {
        stree_query_bucket(&st->stree->buckets[stree_head_hash(head)],
                           a, query, NULL, out);
    } else {
        for (uint32_t i = 0; i < STREE_BUCKETS; i++)
            stree_query_bucket(&st->stree->buckets[i], a, query, NULL, out);
    }
    stree_query_bucket(&st->stree->wildcard, a, query, NULL, out);

    subst_matchset_normalize(out);
}

typedef struct {
    ImportedFlatToken *items;
    uint32_t len, cap;
} ImportedFlatBuilder;

typedef struct {
    VarId var_id;
    SymbolId spelling;
    uint32_t idx;
    uint32_t span;
    Atom *origin;
} ImportedCorefRef;

typedef struct {
    ImportedCorefRef query[IMPORTED_COREF_LIMIT];
    uint32_t nquery;
    ImportedCorefRef indexed[IMPORTED_COREF_LIMIT];
    uint32_t nindexed;
    ImportedCorefRef indexed_value[IMPORTED_COREF_LIMIT];
    uint32_t nindexed_value;
} ImportedCorefState;

typedef struct {
    VarId var_id;
    SymbolId spelling;
} ImportedBridgeVarSlot;

typedef struct {
    ImportedBridgeVarSlot *items;
    uint32_t len, cap;
} ImportedBridgeVarMap;

typedef struct {
    uint8_t index;
    VarId var_id;
    SymbolId spelling;
} ImportedBridgeExprVarSlot;

typedef struct {
    ImportedBridgeExprVarSlot *items;
    uint32_t len;
    uint32_t cap;
} ImportedBridgeExprVarMap;

typedef struct {
    uint16_t slot;
    uint8_t kind;
    VarId var_id;
    SymbolId spelling;
    uint16_t query_slot;
} ImportedOpeningExactEntry;

typedef struct {
    uint32_t context_id;
    ImportedOpeningExactEntry *entries;
    uint32_t len;
} ImportedOpeningContext;

typedef enum {
    IMPORTED_BRIDGE_EXPR_DECODE_ERROR = 0,
    IMPORTED_BRIDGE_EXPR_DECODE_OK = 1,
    IMPORTED_BRIDGE_EXPR_DECODE_NEEDS_TEXT_FALLBACK = 2,
} ImportedBridgeExprDecodeResult;

typedef struct {
    uint64_t hash;
    size_t len;
    uint8_t *bytes;
    AtomId atom_id;
    ImportedBridgeExprDecodeResult result;
    bool occupied;
} ImportedBridgeExprMemoSlot;

typedef struct {
    ImportedBridgeExprMemoSlot *slots;
    uint32_t cap;
    uint32_t used;
} ImportedBridgeExprMemo;

typedef struct {
    uint8_t side;
    uint8_t index;
    const uint8_t *text;
    uint32_t text_len;
} ImportedBridgeBindingEntry;

typedef struct {
    uint16_t query_slot;
    uint8_t value_env;
    uint8_t value_flags;
    const uint8_t *expr;
    uint32_t expr_len;
} ImportedBridgeBindingEntryV2;

typedef struct {
    uint8_t value_env;
    uint8_t value_index;
    VarId var_id;
    SymbolId spelling;
} ImportedBridgeValueVar;

typedef struct {
    ImportedBridgeValueVar *items;
    uint32_t len;
    uint32_t cap;
    uint32_t spelling_nonce;
} ImportedBridgeValueVarMap;

static void imported_bridge_varmap_init(ImportedBridgeVarMap *map);
static void imported_bridge_varmap_free(ImportedBridgeVarMap *map);
static ImportedBridgeVarSlot *imported_bridge_varmap_lookup(ImportedBridgeVarMap *map,
                                                            uint32_t query_slot);
static bool imported_bridge_collect_vars(Atom *atom, ImportedBridgeVarMap *map);
static void imported_bridge_expr_varmap_init(ImportedBridgeExprVarMap *map);
static void imported_bridge_expr_varmap_free(ImportedBridgeExprVarMap *map);
static bool imported_bridge_read_u32(const uint8_t *packet, size_t len, size_t *off,
                                     uint32_t *out);
static bool imported_bridge_read_u16(const uint8_t *packet, size_t len, size_t *off,
                                     uint16_t *out);
static bool imported_bridge_read_u8(const uint8_t *packet, size_t len, size_t *off,
                                    uint8_t *out);
static bool imported_bridge_read_u64(const uint8_t *packet, size_t len, size_t *off,
                                     uint64_t *out);
static Atom *imported_bridge_parse_token_bytes(Arena *a,
                                               const uint8_t *bytes,
                                               uint32_t len,
                                               bool exact_len_reliable,
                                               bool *ok);
static ImportedBridgeExprDecodeResult imported_bridge_expr_to_atom_id(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id);
static ImportedBridgeExprDecodeResult imported_bridge_packet_expr_to_atom_id(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id);
static Atom *imported_bridge_parse_value_raw_query_only_v2(
    Arena *a,
    const uint8_t *expr,
    uint32_t expr_len,
    uint8_t value_env,
    uint8_t value_flags,
    bool wide_tokens,
    bool *value_ok);
static void imported_bridge_value_varmap_init(ImportedBridgeValueVarMap *map);
static void imported_bridge_value_varmap_free(ImportedBridgeValueVarMap *map);
static Atom *imported_bridge_parse_value_raw_multi_ref_v3(
    Arena *a,
    const uint8_t *expr,
    uint32_t expr_len,
    uint8_t value_env,
    uint8_t value_flags,
    bool wide_tokens,
    ImportedBridgeValueVarMap *vars,
    bool *value_ok);
static char *imported_bridge_build_conjunction_text(Arena *a, Atom **patterns,
                                                    uint32_t npatterns);
static void imported_bridge_expr_memo_init(ImportedBridgeExprMemo *memo);
static void imported_bridge_expr_memo_free(ImportedBridgeExprMemo *memo);
static ImportedBridgeExprDecodeResult imported_bridge_expr_to_atom_id_cached(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id);
static ImportedBridgeExprDecodeResult imported_bridge_packet_expr_to_atom_id_cached(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id);

typedef enum {
    IMPORTED_COREF_FAIL = 0,
    IMPORTED_COREF_EXACT = 1,
    IMPORTED_COREF_NEEDS_FALLBACK = 2,
} ImportedCorefVerdict;

static void imported_bucket_free(ImportedFlatBucket *bucket) {
    for (uint32_t i = 0; i < bucket->len; i++)
        free(bucket->entries[i].tokens);
    free(bucket->entries);
    bucket->entries = NULL;
    bucket->len = 0;
    bucket->cap = 0;
}

static void imported_projection_clear(ImportedBridgeState *st) {
    if (!st)
        return;
    free(st->projected_atom_ids);
    st->projected_atom_ids = NULL;
    st->projected_len = 0;
    st->projection_valid = false;
}

static void imported_flat_state_clear(ImportedBridgeState *st) {
    for (uint32_t i = 0; i < STREE_BUCKETS; i++)
        imported_bucket_free(&st->buckets[i]);
    imported_bucket_free(&st->wildcard);
}

static void imported_state_free(ImportedBridgeState *st) {
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    if (st->bridge_space) {
        cetta_mork_bridge_space_free((CettaMorkSpaceHandle *)st->bridge_space);
        st->bridge_space = NULL;
    }
    st->bridge_active = false;
    st->bridge_unavailable = false;
    st->built = false;
    st->dirty = false;
}

static uint32_t imported_logical_len(const Space *s) {
    const MorkImportedState *mork = mork_imported_state_const(s);
    const ImportedBridgeState *st = backend_bridge_state_const(s);
    if (backend_uses_bridge_adapter(s) && mork && mork->attached_compiled)
        return mork->attached_count;
    if (s && s->match_backend.kind == SPACE_ENGINE_PATHMAP && st) {
        if (st->projection_valid)
            return st->projected_len;
        {
            uint32_t logical = bridge_space_logical_len(st);
            if (logical != UINT32_MAX)
                return logical;
        }
    }
    return s->native.len;
}

static uint32_t shadow_storage_logical_len(const Space *s) {
    return s ? s->native.len : 0;
}

static AtomId shadow_storage_get_atom_id_at(const Space *s, uint32_t idx) {
    if (!s || idx >= s->native.len)
        return CETTA_ATOM_ID_NONE;
    return s->native.atom_ids[space_is_queue(s) ? (s->native.start + idx) : idx];
}

static Atom *shadow_storage_get_at(const Space *s, uint32_t idx) {
    AtomId atom_id = shadow_storage_get_atom_id_at(s, idx);
    return term_universe_get_atom(s ? s->native.universe : NULL, atom_id);
}

static uint32_t imported_storage_logical_len(const Space *s) {
    return imported_logical_len(s);
}

static uint32_t bridge_space_logical_len(const ImportedBridgeState *st) {
    uint64_t logical = 0;
    if (!st || !st->bridge_active || !st->bridge_space)
        return UINT32_MAX;
    if (!cetta_mork_bridge_space_size(
            (const CettaMorkSpaceHandle *)st->bridge_space, &logical))
        return UINT32_MAX;
    return logical > UINT32_MAX ? UINT32_MAX : (uint32_t)logical;
}

/* Transitional pathmap/mork projection moves through an explicit
   backend-derived snapshot when possible, falling back to the legacy shadow
   only while the ownership migration is still incomplete. */
static AtomId imported_storage_get_atom_id_at(const Space *s, uint32_t idx) {
    Space *mutable_space = (Space *)s;
    ImportedBridgeState *st = backend_bridge_state(mutable_space);
    if (st && imported_storage_ensure_projection(mutable_space)) {
        if (idx >= st->projected_len)
            return CETTA_ATOM_ID_NONE;
        return st->projected_atom_ids[idx];
    }
    return shadow_storage_get_atom_id_at(s, idx);
}

static Atom *imported_storage_get_at(const Space *s, uint32_t idx) {
    AtomId atom_id = imported_storage_get_atom_id_at(s, idx);
    return term_universe_get_atom(s ? s->native.universe : NULL, atom_id);
}

static bool imported_parse_text_atoms_into_space(Space *s,
                                                 Arena *persistent_arena,
                                                 const uint8_t *bytes,
                                                 size_t len,
                                                 bool remove_atoms,
                                                 uint64_t *out_changed) {
    Arena *dst = persistent_arena ? persistent_arena : eval_current_persistent_arena();
    if (!dst)
        return false;
    if (out_changed)
        *out_changed = 0;

    char *text = cetta_malloc(len + 1);
    memcpy(text, bytes, len);
    text[len] = '\0';

    if (s && s->native.universe) {
        AtomId *atom_ids = NULL;
        int n = parse_metta_text_ids(text, s->native.universe, &atom_ids);
        if (n < 0) {
            free(text);
            return false;
        }
        for (int i = 0; i < n; i++) {
            bool changed = remove_atoms ?
                space_remove_atom_id(s, atom_ids[i]) :
                (space_add_atom_id(s, atom_ids[i]), true);
            if (changed && out_changed)
                (*out_changed)++;
        }
        free(atom_ids);
        free(text);
        return true;
    }

    size_t pos = 0;
    while (text[pos]) {
        size_t before = pos;
        Atom *atom = parse_sexpr(dst, text, &pos);
        if (!atom) {
            while (text[pos] && isspace((unsigned char)text[pos]))
                pos++;
            if (text[pos] == '\0')
                break;
            free(text);
            return false;
        }
        if (pos == before) {
            free(text);
            return false;
        }
        if (remove_atoms) {
            /* Legacy text-only fallback for callers outside the canonical
               universe-backed PATHMAP seam. */
            if (space_remove(s, atom) && out_changed)
                (*out_changed)++;
        } else {
            if (s && s->native.universe) {
                if (!space_admit_atom(s, dst, atom)) {
                    free(text);
                    return false;
                }
            } else {
                /* Legacy text-only fallback; structural PATHMAP materialization
                   still assumes a live TermUniverse. */
                space_add(s, atom);
            }
            if (out_changed)
                (*out_changed)++;
        }
    }

    free(text);
    return true;
}

static bool imported_parse_dump_text_into_space(Space *s,
                                                Arena *persistent_arena,
                                                const uint8_t *bytes,
                                                size_t len) {
    return imported_parse_text_atoms_into_space(s, persistent_arena, bytes, len,
                                                false, NULL);
}
static uint64_t imported_bridge_expr_hash_bytes(const uint8_t *bytes, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)bytes[i];
        h *= 1099511628211ULL;
    }
    return h ? h : 1ULL;
}

static void imported_bridge_expr_memo_init(ImportedBridgeExprMemo *memo) {
    if (!memo)
        return;
    memo->slots = NULL;
    memo->cap = 0;
    memo->used = 0;
}

static void imported_bridge_expr_memo_free(ImportedBridgeExprMemo *memo) {
    if (!memo)
        return;
    for (uint32_t i = 0; i < memo->cap; i++) {
        if (memo->slots[i].occupied)
            free(memo->slots[i].bytes);
    }
    free(memo->slots);
    memo->slots = NULL;
    memo->cap = 0;
    memo->used = 0;
}

static bool imported_bridge_expr_memo_matches(const ImportedBridgeExprMemoSlot *slot,
                                              const uint8_t *expr,
                                              size_t expr_len,
                                              uint64_t hash) {
    return slot && slot->occupied && slot->hash == hash &&
           slot->len == expr_len &&
           (expr_len == 0 || memcmp(slot->bytes, expr, expr_len) == 0);
}

static ImportedBridgeExprMemoSlot *imported_bridge_expr_memo_probe(
    ImportedBridgeExprMemo *memo,
    const uint8_t *expr,
    size_t expr_len,
    uint64_t hash,
    bool *found) {
    if (found)
        *found = false;
    if (!memo || memo->cap == 0)
        return NULL;

    uint32_t idx = (uint32_t)(hash & (uint64_t)(memo->cap - 1u));
    for (;;) {
        ImportedBridgeExprMemoSlot *slot = &memo->slots[idx];
        if (!slot->occupied)
            return slot;
        if (imported_bridge_expr_memo_matches(slot, expr, expr_len, hash)) {
            if (found)
                *found = true;
            return slot;
        }
        idx = (idx + 1u) & (memo->cap - 1u);
    }
}

static bool imported_bridge_expr_memo_resize(ImportedBridgeExprMemo *memo,
                                             uint32_t new_cap) {
    ImportedBridgeExprMemoSlot *new_slots =
        cetta_malloc(sizeof(ImportedBridgeExprMemoSlot) * new_cap);
    memset(new_slots, 0, sizeof(ImportedBridgeExprMemoSlot) * new_cap);

    if (memo->slots) {
        for (uint32_t i = 0; i < memo->cap; i++) {
            ImportedBridgeExprMemoSlot old = memo->slots[i];
            if (!old.occupied)
                continue;
            uint32_t idx = (uint32_t)(old.hash & (uint64_t)(new_cap - 1u));
            while (new_slots[idx].occupied)
                idx = (idx + 1u) & (new_cap - 1u);
            new_slots[idx] = old;
        }
        free(memo->slots);
    }

    memo->slots = new_slots;
    memo->cap = new_cap;
    return true;
}

static bool imported_bridge_expr_memo_reserve(ImportedBridgeExprMemo *memo,
                                              uint32_t needed) {
    if (!memo)
        return false;
    if (memo->cap > 0 && needed * 10u <= memo->cap * 7u)
        return true;

    uint32_t new_cap = memo->cap ? memo->cap : 16u;
    while (needed * 10u > new_cap * 7u)
        new_cap *= 2u;
    return imported_bridge_expr_memo_resize(memo, new_cap);
}

static bool imported_bridge_expr_memo_lookup(ImportedBridgeExprMemo *memo,
                                             const uint8_t *expr,
                                             size_t expr_len,
                                             AtomId *out_id,
                                             ImportedBridgeExprDecodeResult *out_result) {
    bool found = false;
    uint64_t hash = imported_bridge_expr_hash_bytes(expr, expr_len);
    ImportedBridgeExprMemoSlot *slot =
        imported_bridge_expr_memo_probe(memo, expr, expr_len, hash, &found);
    if (!slot || !found)
        return false;
    if (out_id)
        *out_id = slot->atom_id;
    if (out_result)
        *out_result = slot->result;
    return true;
}

static bool imported_bridge_expr_memo_store(ImportedBridgeExprMemo *memo,
                                            const uint8_t *expr,
                                            size_t expr_len,
                                            AtomId atom_id,
                                            ImportedBridgeExprDecodeResult result) {
    bool found = false;
    uint64_t hash = imported_bridge_expr_hash_bytes(expr, expr_len);
    if (!imported_bridge_expr_memo_reserve(memo, memo->used + 1u))
        return false;

    ImportedBridgeExprMemoSlot *slot =
        imported_bridge_expr_memo_probe(memo, expr, expr_len, hash, &found);
    if (!slot)
        return false;
    if (found) {
        slot->atom_id = atom_id;
        slot->result = result;
        return true;
    }

    slot->bytes = NULL;
    if (expr_len > 0) {
        slot->bytes = cetta_malloc(expr_len);
        memcpy(slot->bytes, expr, expr_len);
    }
    slot->hash = hash;
    slot->len = expr_len;
    slot->atom_id = atom_id;
    slot->result = result;
    slot->occupied = true;
    memo->used++;
    return true;
}

static ImportedBridgeExprDecodeResult imported_bridge_expr_to_atom_id_cached(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id) {
    ImportedBridgeExprDecodeResult result = IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    AtomId atom_id = CETTA_ATOM_ID_NONE;

    if (memo && imported_bridge_expr_memo_lookup(memo, expr, expr_len,
                                                 &atom_id, &result)) {
        if (out_id)
            *out_id = atom_id;
        return result;
    }

    result = imported_bridge_expr_to_atom_id(universe, scratch, expr, expr_len,
                                             &atom_id);
    if (memo && (result == IMPORTED_BRIDGE_EXPR_DECODE_OK ||
                 result == IMPORTED_BRIDGE_EXPR_DECODE_NEEDS_TEXT_FALLBACK)) {
        if (!imported_bridge_expr_memo_store(memo, expr, expr_len,
                                             atom_id, result)) {
            if (out_id)
                *out_id = CETTA_ATOM_ID_NONE;
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        }
    }
    if (out_id)
        *out_id = atom_id;
    return result;
}

static ImportedBridgeExprDecodeResult imported_bridge_packet_expr_to_atom_id_cached(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id) {
    ImportedBridgeExprDecodeResult result = IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    AtomId atom_id = CETTA_ATOM_ID_NONE;

    if (memo && imported_bridge_expr_memo_lookup(memo, expr, expr_len,
                                                 &atom_id, &result)) {
        if (out_id)
            *out_id = atom_id;
        return result;
    }

    result = imported_bridge_packet_expr_to_atom_id(universe, scratch, expr,
                                                   expr_len, &atom_id);
    if (memo && result == IMPORTED_BRIDGE_EXPR_DECODE_OK) {
        if (!imported_bridge_expr_memo_store(memo, expr, expr_len,
                                             atom_id, result)) {
            if (out_id)
                *out_id = CETTA_ATOM_ID_NONE;
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        }
    }
    if (out_id)
        *out_id = atom_id;
    return result;
}

static ImportedBridgeExprDecodeResult imported_bridge_token_to_atom_id(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *bytes,
    uint32_t len,
    AtomId *out_id) {
    if (!universe || !scratch || !bytes || !out_id || len == 0)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;

    *out_id = CETTA_ATOM_ID_NONE;

    /* Bridge tokens use 6-bit lengths. A 63-byte token is a semantic boundary:
       the transport does not distinguish "exactly 63 bytes" from a longer token
       that had to be truncated upstream, so the structural importer must defer
       to the text path there. */
    if (len == 63)
        return IMPORTED_BRIDGE_EXPR_DECODE_NEEDS_TEXT_FALLBACK;

    char *tok = arena_alloc(scratch, (size_t)len + 1u);
    memcpy(tok, bytes, len);
    tok[len] = '\0';

    if (tok[0] == '"' && len >= 2 && tok[len - 1] == '"') {
        size_t pos = 0;
        AtomId parsed = parse_sexpr_to_id(universe, tok, &pos);
        if (parsed == CETTA_ATOM_ID_NONE || pos != len)
            return IMPORTED_BRIDGE_EXPR_DECODE_NEEDS_TEXT_FALLBACK;
        *out_id = parsed;
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }

    if (strcmp(tok, "True") == 0) {
        *out_id = tu_intern_bool(universe, true);
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }
    if (strcmp(tok, "False") == 0) {
        *out_id = tu_intern_bool(universe, false);
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }
    if (strcmp(tok, "PI") == 0) {
        *out_id = tu_intern_float(universe, 3.14159265358979323846);
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }
    if (strcmp(tok, "EXP") == 0) {
        *out_id = tu_intern_float(universe, 2.71828182845904523536);
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }
    if (strcmp(tok, "NaN") == 0) {
        *out_id = tu_intern_float(universe, NAN);
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }
    if (strcmp(tok, "inf") == 0) {
        *out_id = tu_intern_float(universe, INFINITY);
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }
    if (strcmp(tok, "-inf") == 0) {
        *out_id = tu_intern_float(universe, -INFINITY);
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }

    char *endp = NULL;
    errno = 0;
    long long ival = strtoll(tok, &endp, 10);
    if (*endp == '\0' && errno == 0) {
        *out_id = tu_intern_int(universe, (int64_t)ival);
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }

    if (strchr(tok, '.')) {
        char *fendp = NULL;
        errno = 0;
        double fval = strtod(tok, &fendp);
        if (*fendp == '\0' && errno == 0) {
            *out_id = tu_intern_float(universe, fval);
            return IMPORTED_BRIDGE_EXPR_DECODE_OK;
        }
    }

    const char *canonical = parser_canonicalize_namespace_token(scratch, tok);
    *out_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, canonical));
    return IMPORTED_BRIDGE_EXPR_DECODE_OK;
}

static ImportedBridgeExprVarSlot *imported_bridge_expr_varmap_get_or_add(
    ImportedBridgeExprVarMap *vars,
    uint8_t index) {
    if (!vars)
        return NULL;
    for (uint32_t i = 0; i < vars->len; i++) {
        if (vars->items[i].index == index)
            return &vars->items[i];
    }
    if (vars->len >= vars->cap) {
        vars->cap = vars->cap ? vars->cap * 2u : 8u;
        vars->items = cetta_realloc(
            vars->items, sizeof(ImportedBridgeExprVarSlot) * vars->cap);
    }
    char name[32];
    snprintf(name, sizeof(name), "$__mork_p%u", (unsigned)index);
    vars->items[vars->len] = (ImportedBridgeExprVarSlot){
        .index = index,
        /* Bridge variable numbers are local to one decoded expression. */
        .var_id = fresh_var_id(),
        .spelling = symbol_intern_cstr(g_symbols, name),
    };
    return &vars->items[vars->len++];
}

static ImportedBridgeExprDecodeResult imported_bridge_expr_to_atom_id_rec(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    size_t *off,
    ImportedBridgeExprVarMap *vars,
    uint8_t *introduced_vars,
    AtomId *out_id) {
    if (!universe || !scratch || !expr || !off || !out_id ||
        !vars || !introduced_vars || *off >= expr_len)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    *out_id = CETTA_ATOM_ID_NONE;

    uint8_t tag = expr[*off];
    if (tag == IMPORTED_MORK_TAG_NEWVAR) {
        ImportedBridgeExprVarSlot *slot =
            imported_bridge_expr_varmap_get_or_add(vars, *introduced_vars);
        if (!slot)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        (*introduced_vars)++;
        (*off)++;
        *out_id = tu_intern_var(universe, slot->spelling, slot->var_id);
        return *out_id != CETTA_ATOM_ID_NONE ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                                             : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    if ((tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_VARREF_PREFIX) {
        uint8_t ref = (uint8_t)(tag & 0x3Fu);
        ImportedBridgeExprVarSlot *slot =
            imported_bridge_expr_varmap_get_or_add(vars, ref);
        if (!slot)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        (*off)++;
        *out_id = tu_intern_var(universe, slot->spelling, slot->var_id);
        return *out_id != CETTA_ATOM_ID_NONE ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                                             : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }

    if ((tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_SYMBOL_PREFIX) {
        uint32_t sym_len = (uint32_t)(tag & 0x3Fu);
        if (sym_len == 0 || *off + 1u + sym_len > expr_len)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        ImportedBridgeExprDecodeResult result =
            imported_bridge_token_to_atom_id(universe, scratch, expr + *off + 1u,
                                             sym_len, out_id);
        if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK)
            return result;
        *off += 1u + sym_len;
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }

    uint32_t arity = (uint32_t)(tag & 0x3Fu);
    (*off)++;
    AtomId *children = arity ? cetta_malloc(sizeof(AtomId) * arity) : NULL;
    for (uint32_t i = 0; i < arity; i++) {
        ImportedBridgeExprDecodeResult child_result =
            imported_bridge_expr_to_atom_id_rec(universe, scratch, expr, expr_len,
                                                off, vars, introduced_vars,
                                                &children[i]);
        if (child_result != IMPORTED_BRIDGE_EXPR_DECODE_OK) {
            free(children);
            return child_result;
        }
    }
    *out_id = tu_expr_from_ids(universe, children, arity);
    free(children);
    return *out_id != CETTA_ATOM_ID_NONE ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                                         : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
}

static ImportedBridgeExprDecodeResult imported_bridge_expr_to_atom_id(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id) {
    size_t off = 0;
    uint8_t introduced_vars = 0;
    ImportedBridgeExprVarMap vars;
    imported_bridge_expr_varmap_init(&vars);
    ImportedBridgeExprDecodeResult result =
        imported_bridge_expr_to_atom_id_rec(universe, scratch, expr, expr_len,
                                            &off, &vars, &introduced_vars, out_id);
    if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK) {
        imported_bridge_expr_varmap_free(&vars);
        return result;
    }
    if (off != expr_len) {
        imported_bridge_expr_varmap_free(&vars);
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    imported_bridge_expr_varmap_free(&vars);
    return *out_id != CETTA_ATOM_ID_NONE
        ? IMPORTED_BRIDGE_EXPR_DECODE_OK
        : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
}

static ImportedBridgeExprDecodeResult imported_bridge_packet_expr_to_atom_id_rec(
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    size_t *off,
    ImportedBridgeExprVarMap *vars,
    uint8_t *introduced_vars,
    Atom **out_atom) {
    if (!scratch || !expr || !off || !out_atom ||
        !vars || !introduced_vars || *off >= expr_len)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;

    uint8_t tag = expr[(*off)++];
    switch (tag) {
    case IMPORTED_MORK_WIDE_TAG_NEWVAR: {
        ImportedBridgeExprVarSlot *slot =
            imported_bridge_expr_varmap_get_or_add(vars, *introduced_vars);
        if (!slot)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        (*introduced_vars)++;
        *out_atom = atom_var_with_spelling(scratch, slot->spelling, slot->var_id);
        return *out_atom ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                         : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    case IMPORTED_MORK_WIDE_TAG_VARREF: {
        if (*off >= expr_len)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        ImportedBridgeExprVarSlot *slot =
            imported_bridge_expr_varmap_get_or_add(vars, expr[(*off)++]);
        if (!slot)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        *out_atom = atom_var_with_spelling(scratch, slot->spelling, slot->var_id);
        return *out_atom ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                         : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    case IMPORTED_MORK_WIDE_TAG_SYMBOL: {
        uint32_t sym_len = 0;
        bool ok = true;
        if (!imported_bridge_read_u32(expr, expr_len, off, &sym_len) ||
            sym_len == 0 || *off + sym_len > expr_len)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        *out_atom = imported_bridge_parse_token_bytes(
            scratch, expr + *off, sym_len, true, &ok);
        if (!ok || !*out_atom)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        *off += sym_len;
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }
    case IMPORTED_MORK_WIDE_TAG_ARITY: {
        uint32_t arity = 0;
        if (!imported_bridge_read_u32(expr, expr_len, off, &arity))
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        Atom **children = arity ? arena_alloc(scratch, sizeof(Atom *) * arity) : NULL;
        for (uint32_t i = 0; i < arity; i++) {
            ImportedBridgeExprDecodeResult child_result =
                imported_bridge_packet_expr_to_atom_id_rec(
                    scratch, expr, expr_len, off, vars, introduced_vars,
                    &children[i]);
            if (child_result != IMPORTED_BRIDGE_EXPR_DECODE_OK)
                return child_result;
        }
        *out_atom = atom_expr(scratch, children, arity);
        return *out_atom ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                         : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    default:
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
}

static ImportedBridgeExprDecodeResult imported_bridge_packet_expr_to_atom_id(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id) {
    if (!universe || !scratch || !expr || !out_id || expr_len == 0)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;

    size_t off = 0;
    uint8_t introduced_vars = 0;
    Atom *atom = NULL;
    ImportedBridgeExprVarMap vars;
    imported_bridge_expr_varmap_init(&vars);
    ImportedBridgeExprDecodeResult result =
        imported_bridge_packet_expr_to_atom_id_rec(
            scratch, expr, expr_len, &off, &vars, &introduced_vars, &atom);
    if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK) {
        imported_bridge_expr_varmap_free(&vars);
        return result;
    }
    if (off != expr_len) {
        imported_bridge_expr_varmap_free(&vars);
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    *out_id = term_universe_store_atom_id(universe, NULL, atom);
    imported_bridge_expr_varmap_free(&vars);
    return *out_id != CETTA_ATOM_ID_NONE
        ? IMPORTED_BRIDGE_EXPR_DECODE_OK
        : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
}

static void imported_opening_contexts_free(ImportedOpeningContext *contexts,
                                           uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        free(contexts[i].entries);
        contexts[i].entries = NULL;
        contexts[i].len = 0;
    }
    free(contexts);
}

static ImportedOpeningContext *imported_opening_context_lookup(
    ImportedOpeningContext *contexts,
    uint32_t count,
    uint32_t context_id) {
    for (uint32_t i = 0; i < count; i++) {
        if (contexts[i].context_id == context_id)
            return &contexts[i];
    }
    return NULL;
}

static ImportedOpeningExactEntry *imported_opening_context_slot(
    ImportedOpeningContext *context,
    uint16_t slot) {
    if (!context)
        return NULL;
    for (uint32_t i = 0; i < context->len; i++) {
        if (context->entries[i].slot == slot)
            return &context->entries[i];
    }
    return NULL;
}

static bool imported_opening_context_add_entry(ImportedOpeningContext *context,
                                               uint16_t slot,
                                               VarId var_id,
                                               SymbolId spelling) {
    if (!context || var_id == VAR_ID_NONE || spelling == SYMBOL_ID_NONE)
        return false;
    if (imported_opening_context_slot(context, slot))
        return false;
    context->entries = cetta_realloc(
        context->entries, sizeof(ImportedOpeningExactEntry) * (context->len + 1u));
    context->entries[context->len++] = (ImportedOpeningExactEntry){
        .slot = slot,
        .kind = IMPORTED_MORK_OPEN_REF_EXACT,
        .var_id = var_id,
        .spelling = spelling,
        .query_slot = 0,
    };
    return true;
}

static bool imported_opening_context_add_query_slot_entry(
    ImportedOpeningContext *context,
    uint16_t slot,
    uint16_t query_slot) {
    if (!context)
        return false;
    if (imported_opening_context_slot(context, slot))
        return false;
    context->entries = cetta_realloc(
        context->entries, sizeof(ImportedOpeningExactEntry) * (context->len + 1u));
    context->entries[context->len++] = (ImportedOpeningExactEntry){
        .slot = slot,
        .kind = IMPORTED_MORK_OPEN_REF_QUERY_SLOT,
        .var_id = VAR_ID_NONE,
        .spelling = SYMBOL_ID_NONE,
        .query_slot = query_slot,
    };
    return true;
}

static bool imported_bridge_read_contextual_opening_contexts(
    const uint8_t *packet,
    size_t packet_len,
    size_t *off,
    uint32_t context_count,
    bool allow_query_refs,
    ImportedOpeningContext **out_contexts) {
    ImportedOpeningContext *contexts = NULL;

    if (out_contexts)
        *out_contexts = NULL;
    if (!packet || !off || !out_contexts)
        return false;
    if (context_count) {
        contexts = cetta_malloc(sizeof(ImportedOpeningContext) * context_count);
        memset(contexts, 0, sizeof(ImportedOpeningContext) * context_count);
    }

    for (uint32_t i = 0; i < context_count; i++) {
        uint32_t context_id = 0;
        uint32_t entry_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, off, &context_id) ||
            !imported_bridge_read_u32(packet, packet_len, off, &entry_count) ||
            imported_opening_context_lookup(contexts, i, context_id))
            goto fail;
        contexts[i].context_id = context_id;
        for (uint32_t entry_idx = 0; entry_idx < entry_count; entry_idx++) {
            uint16_t slot = 0;
            uint8_t kind = 0;
            uint8_t reserved = 0;
            if (!imported_bridge_read_u16(packet, packet_len, off, &slot) ||
                !imported_bridge_read_u8(packet, packet_len, off, &kind) ||
                !imported_bridge_read_u8(packet, packet_len, off, &reserved) ||
                reserved != 0)
                goto fail;

            if (kind == IMPORTED_MORK_OPEN_REF_EXACT) {
                uint64_t var_id = 0;
                uint32_t spelling_len = 0;
                SymbolId spelling = SYMBOL_ID_NONE;
                if (!imported_bridge_read_u64(packet, packet_len, off, &var_id) ||
                    !imported_bridge_read_u32(packet, packet_len, off, &spelling_len) ||
                    spelling_len == 0 ||
                    *off + (size_t)spelling_len > packet_len)
                    goto fail;
                spelling = symbol_intern_bytes(g_symbols, packet + *off, spelling_len);
                *off += spelling_len;
                if (!imported_opening_context_add_entry(
                        &contexts[i], slot, (VarId)var_id, spelling))
                    goto fail;
            } else if (kind == IMPORTED_MORK_OPEN_REF_QUERY_SLOT && allow_query_refs) {
                uint16_t query_slot = 0;
                if (!imported_bridge_read_u16(packet, packet_len, off, &query_slot) ||
                    !imported_opening_context_add_query_slot_entry(
                        &contexts[i], slot, query_slot))
                    goto fail;
            } else {
                goto fail;
            }
        }
    }

    *out_contexts = contexts;
    return true;

fail:
    imported_opening_contexts_free(contexts, context_count);
    return false;
}

static ImportedBridgeExprDecodeResult imported_bridge_packet_expr_to_atom_id_exact_rec(
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    size_t *off,
    ImportedOpeningContext *context,
    uint16_t *introduced_vars,
    Atom **out_atom) {
    if (!scratch || !expr || !off || !out_atom || !introduced_vars ||
        *off >= expr_len)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;

    uint8_t tag = expr[(*off)++];
    switch (tag) {
    case IMPORTED_MORK_WIDE_TAG_NEWVAR: {
        ImportedOpeningExactEntry *entry =
            imported_opening_context_slot(context, *introduced_vars);
        (*introduced_vars)++;
        if (!entry || entry->kind != IMPORTED_MORK_OPEN_REF_EXACT)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        *out_atom = atom_var_with_spelling(scratch, entry->spelling, entry->var_id);
        return *out_atom ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                         : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    case IMPORTED_MORK_WIDE_TAG_VARREF: {
        uint8_t slot = 0;
        if (!imported_bridge_read_u8(expr, expr_len, off, &slot))
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        ImportedOpeningExactEntry *entry =
            imported_opening_context_slot(context, slot);
        if (!entry || entry->kind != IMPORTED_MORK_OPEN_REF_EXACT)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        *out_atom = atom_var_with_spelling(scratch, entry->spelling, entry->var_id);
        return *out_atom ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                         : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    case IMPORTED_MORK_WIDE_TAG_SYMBOL: {
        uint32_t sym_len = 0;
        bool ok = true;
        if (!imported_bridge_read_u32(expr, expr_len, off, &sym_len) ||
            sym_len == 0 || *off + sym_len > expr_len)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        *out_atom = imported_bridge_parse_token_bytes(
            scratch, expr + *off, sym_len, true, &ok);
        if (!ok || !*out_atom)
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        *off += sym_len;
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;
    }
    case IMPORTED_MORK_WIDE_TAG_ARITY: {
        uint32_t arity = 0;
        if (!imported_bridge_read_u32(expr, expr_len, off, &arity))
            return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
        Atom **children = arity ? arena_alloc(scratch, sizeof(Atom *) * arity) : NULL;
        for (uint32_t i = 0; i < arity; i++) {
            ImportedBridgeExprDecodeResult child_result =
                imported_bridge_packet_expr_to_atom_id_exact_rec(
                    scratch, expr, expr_len, off, context, introduced_vars,
                    &children[i]);
            if (child_result != IMPORTED_BRIDGE_EXPR_DECODE_OK)
                return child_result;
        }
        *out_atom = atom_expr(scratch, children, arity);
        return *out_atom ? IMPORTED_BRIDGE_EXPR_DECODE_OK
                         : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
    default:
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    }
}

static ImportedBridgeExprDecodeResult imported_bridge_packet_expr_to_atom_id_exact(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    ImportedOpeningContext *context,
    AtomId *out_id) {
    if (!universe || !scratch || !expr || !out_id || expr_len == 0)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;

    size_t off = 0;
    uint16_t introduced_vars = 0;
    Atom *atom = NULL;
    ImportedBridgeExprDecodeResult result =
        imported_bridge_packet_expr_to_atom_id_exact_rec(
            scratch, expr, expr_len, &off, context, &introduced_vars, &atom);
    if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK || off != expr_len)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    *out_id = term_universe_store_atom_id(universe, NULL, atom);
    return *out_id != CETTA_ATOM_ID_NONE
        ? IMPORTED_BRIDGE_EXPR_DECODE_OK
        : IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
}

static Atom *imported_bridge_parse_value_contextual_rec(
    Arena *a,
    const uint8_t *expr,
    size_t len,
    size_t *off,
    ImportedOpeningContext *context,
    ImportedBridgeVarMap *query_vars,
    uint16_t *introduced_vars,
    bool *ok) {
    if (!a || !expr || !off || !context || !query_vars || !introduced_vars ||
        !ok || !*ok || *off >= len) {
        if (ok) *ok = false;
        return NULL;
    }

    uint8_t tag = expr[(*off)++];
    switch (tag) {
    case IMPORTED_MORK_WIDE_TAG_NEWVAR: {
        ImportedOpeningExactEntry *entry =
            imported_opening_context_slot(context, *introduced_vars);
        (*introduced_vars)++;
        if (!entry) {
            *ok = false;
            return NULL;
        }
        if (entry->kind == IMPORTED_MORK_OPEN_REF_EXACT) {
            return atom_var_with_spelling(a, entry->spelling, entry->var_id);
        }
        if (entry->kind == IMPORTED_MORK_OPEN_REF_QUERY_SLOT) {
            ImportedBridgeVarSlot *slot =
                imported_bridge_varmap_lookup(query_vars, entry->query_slot);
            if (!slot) {
                *ok = false;
                return NULL;
            }
            return atom_var_with_spelling(a, slot->spelling, slot->var_id);
        }
        *ok = false;
        return NULL;
    }
    case IMPORTED_MORK_WIDE_TAG_VARREF: {
        uint8_t slot_idx = 0;
        if (!imported_bridge_read_u8(expr, len, off, &slot_idx)) {
            *ok = false;
            return NULL;
        }
        ImportedOpeningExactEntry *entry =
            imported_opening_context_slot(context, slot_idx);
        if (!entry) {
            *ok = false;
            return NULL;
        }
        if (entry->kind == IMPORTED_MORK_OPEN_REF_EXACT) {
            return atom_var_with_spelling(a, entry->spelling, entry->var_id);
        }
        if (entry->kind == IMPORTED_MORK_OPEN_REF_QUERY_SLOT) {
            ImportedBridgeVarSlot *slot =
                imported_bridge_varmap_lookup(query_vars, entry->query_slot);
            if (!slot) {
                *ok = false;
                return NULL;
            }
            return atom_var_with_spelling(a, slot->spelling, slot->var_id);
        }
        *ok = false;
        return NULL;
    }
    case IMPORTED_MORK_WIDE_TAG_SYMBOL: {
        uint32_t sym_len = 0;
        if (!imported_bridge_read_u32(expr, len, off, &sym_len) ||
            sym_len == 0 || *off + sym_len > len) {
            *ok = false;
            return NULL;
        }
        Atom *atom = imported_bridge_parse_token_bytes(
            a, expr + *off, sym_len, true, ok);
        if (!*ok)
            return NULL;
        *off += sym_len;
        return atom;
    }
    case IMPORTED_MORK_WIDE_TAG_ARITY: {
        uint32_t arity = 0;
        if (!imported_bridge_read_u32(expr, len, off, &arity)) {
            *ok = false;
            return NULL;
        }
        Atom **elems = arity ? arena_alloc(a, sizeof(Atom *) * arity) : NULL;
        for (uint32_t i = 0; i < arity; i++) {
            elems[i] = imported_bridge_parse_value_contextual_rec(
                a, expr, len, off, context, query_vars, introduced_vars, ok);
            if (!*ok)
                return NULL;
        }
        return atom_expr(a, elems, arity);
    }
    default:
        *ok = false;
        return NULL;
    }
}

static Atom *imported_bridge_parse_value_contextual(
    Arena *a,
    const uint8_t *expr,
    uint32_t expr_len,
    uint32_t value_flags,
    ImportedOpeningContext *context,
    ImportedBridgeVarMap *query_vars,
    bool *ok) {
    size_t off = 0;
    uint16_t introduced_vars = 0;
    if (!ok || !*ok || !expr || expr_len == 0 || !context || !query_vars ||
        value_flags != 0) {
        if (ok) *ok = false;
        return NULL;
    }
    Atom *value = imported_bridge_parse_value_contextual_rec(
        a, expr, expr_len, &off, context, query_vars, &introduced_vars, ok);
    if (!*ok || off != expr_len) {
        *ok = false;
        return NULL;
    }
    return value;
}

static bool imported_bridge_visit_contextual_query_rows_packet(
    Arena *a,
    const uint8_t *packet,
    size_t packet_len,
    uint32_t row_count,
    ImportedBridgeVarMap *query_vars,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t parsed_rows = 0;
    uint32_t context_count = 0;
    ImportedOpeningContext *contexts = NULL;
    bool success = true;

    if (!a || !packet || !query_vars || !visitor)
        return false;

    if (!imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &flags) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &parsed_rows) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &context_count))
        return false;
    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_CONTEXTUAL_ROWS_WIRE_VERSION ||
        flags != IMPORTED_MORK_CONTEXTUAL_QUERY_ROWS_FLAGS ||
        parsed_rows != row_count)
        return false;

    if (!imported_bridge_read_contextual_opening_contexts(
            packet, packet_len, &off, context_count, true, &contexts))
        return false;

    for (uint32_t row = 0; row < parsed_rows && success; row++) {
        uint32_t binding_count = 0;
        Bindings merged;
        bool merged_inited = false;

        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            success = false;
            break;
        }

        if (seed) {
            if (!bindings_clone(&merged, seed)) {
                success = false;
                break;
            }
        } else {
            bindings_init(&merged);
        }
        merged_inited = true;

        for (uint32_t bi = 0; bi < binding_count && success; bi++) {
            uint16_t query_slot = 0;
            uint32_t value_context_id = 0;
            uint32_t value_flags = 0;
            uint32_t expr_len = 0;

            if (!imported_bridge_read_u16(packet, packet_len, &off, &query_slot) ||
                !imported_bridge_read_u32(packet, packet_len, &off, &value_context_id) ||
                !imported_bridge_read_u32(packet, packet_len, &off, &value_flags) ||
                !imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
                off + expr_len > packet_len) {
                success = false;
                break;
            }

            ImportedBridgeVarSlot *key_slot =
                imported_bridge_varmap_lookup(query_vars, query_slot);
            ImportedOpeningContext *value_context =
                imported_opening_context_lookup(contexts, context_count,
                                                value_context_id);
            if (!key_slot || !value_context) {
                success = false;
                break;
            }

            bool value_ok = true;
            Atom *value = imported_bridge_parse_value_contextual(
                a, packet + off, expr_len, value_flags, value_context, query_vars,
                &value_ok);
            off += expr_len;
            if (!value_ok ||
                !bindings_add_id(&merged, key_slot->var_id,
                                 key_slot->spelling, value)) {
                success = false;
                break;
            }
        }

        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_MORK_DIRECT_ROW);
        if (success && !bindings_has_loop(&merged) &&
            !visitor(&merged, ctx))
            success = false;
        if (merged_inited)
            bindings_free(&merged);
    }

    if (success && off != packet_len)
        success = false;
    imported_opening_contexts_free(contexts, context_count);
    return success;
}

static bool imported_bridge_contextual_exact_rows_append_atom(AtomId **items,
                                                   uint32_t *len,
                                                   uint32_t *cap,
                                                   AtomId atom_id,
                                                   uint32_t multiplicity) {
    if (!items || !len || !cap || atom_id == CETTA_ATOM_ID_NONE ||
        multiplicity == 0 || *len > UINT32_MAX - multiplicity)
        return false;
    if (*len + multiplicity > *cap) {
        uint32_t next = *cap ? *cap : 8u;
        while (next < *len + multiplicity) {
            if (next > UINT32_MAX / 2u)
                next = *len + multiplicity;
            else
                next *= 2u;
        }
        *items = cetta_realloc(*items, sizeof(AtomId) * next);
        *cap = next;
    }
    for (uint32_t i = 0; i < multiplicity; i++)
        (*items)[(*len)++] = atom_id;
    return true;
}

static bool imported_bridge_decode_contextual_exact_rows(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    const uint8_t *packet,
    size_t packet_len,
    AtomId **out_items,
    uint32_t *out_len) {
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t row_count = 0;
    uint32_t context_count = 0;
    ImportedOpeningContext *contexts = NULL;
    AtomId *items = NULL;
    uint32_t len = 0;
    uint32_t cap = 0;
    bool ok = false;
    (void)memo;

    if (out_items)
        *out_items = NULL;
    if (out_len)
        *out_len = 0;
    if (!universe || !scratch || !memo || !packet || !out_items || !out_len)
        return false;

    if (!imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &flags) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &row_count) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &context_count))
        goto done;

    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_CONTEXTUAL_ROWS_WIRE_VERSION ||
        flags != IMPORTED_MORK_CONTEXTUAL_EXACT_ROWS_FLAGS ||
        (row_count == 0 && context_count != 0) ||
        (row_count != 0 && context_count == 0))
        goto done;

    if (context_count) {
        contexts = cetta_malloc(sizeof(ImportedOpeningContext) * context_count);
        memset(contexts, 0, sizeof(ImportedOpeningContext) * context_count);
    }
    for (uint32_t i = 0; i < context_count; i++) {
        uint32_t context_id = 0;
        uint32_t entry_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &context_id) ||
            !imported_bridge_read_u32(packet, packet_len, &off, &entry_count) ||
            imported_opening_context_lookup(contexts, i, context_id))
            goto done;
        contexts[i].context_id = context_id;
        for (uint32_t entry_idx = 0; entry_idx < entry_count; entry_idx++) {
            uint16_t slot = 0;
            uint8_t kind = 0;
            uint8_t reserved = 0;
            uint64_t var_id = 0;
            uint32_t spelling_len = 0;
            SymbolId spelling = SYMBOL_ID_NONE;
            if (!imported_bridge_read_u16(packet, packet_len, &off, &slot) ||
                !imported_bridge_read_u8(packet, packet_len, &off, &kind) ||
                !imported_bridge_read_u8(packet, packet_len, &off, &reserved) ||
                kind != IMPORTED_MORK_OPEN_REF_EXACT ||
                reserved != 0 ||
                !imported_bridge_read_u64(packet, packet_len, &off, &var_id) ||
                !imported_bridge_read_u32(packet, packet_len, &off, &spelling_len) ||
                spelling_len == 0 ||
                off + (size_t)spelling_len > packet_len)
                goto done;
            spelling = symbol_intern_bytes(g_symbols, packet + off, spelling_len);
            off += spelling_len;
            if (!imported_opening_context_add_entry(
                    &contexts[i], slot, (VarId)var_id, spelling))
                goto done;
        }
    }

    for (uint32_t row = 0; row < row_count; row++) {
        ArenaMark scratch_mark = arena_mark(scratch);
        uint32_t context_id = 0;
        uint32_t multiplicity = 0;
        uint32_t expr_len = 0;
        AtomId atom_id = CETTA_ATOM_ID_NONE;
        ImportedBridgeExprDecodeResult result;
        ImportedOpeningContext *context = NULL;

        if (!imported_bridge_read_u32(packet, packet_len, &off, &context_id) ||
            !imported_bridge_read_u32(packet, packet_len, &off, &multiplicity) ||
            !imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
            multiplicity == 0 ||
            off + (size_t)expr_len > packet_len) {
            arena_reset(scratch, scratch_mark);
            goto done;
        }
        context = imported_opening_context_lookup(contexts, context_count, context_id);
        if (!context) {
            arena_reset(scratch, scratch_mark);
            goto done;
        }

        result = imported_bridge_packet_expr_to_atom_id_exact(
            universe, scratch, packet + off, expr_len, context, &atom_id);
        off += expr_len;
        if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK ||
            !imported_bridge_contextual_exact_rows_append_atom(&items, &len, &cap,
                                                    atom_id, multiplicity)) {
            arena_reset(scratch, scratch_mark);
            goto done;
        }
        arena_reset(scratch, scratch_mark);
    }

    if (off != packet_len)
        goto done;

    *out_items = items;
    *out_len = len;
    items = NULL;
    ok = true;

done:
    imported_opening_contexts_free(contexts, context_count);
    free(items);
    return ok;
}

SpaceBridgeImportResult space_match_backend_import_bridge_space(
    Space *dst,
    CettaMorkSpaceHandle *bridge,
    uint64_t *out_loaded) {
    Arena scratch;
    ImportedBridgeExprMemo memo;
    CettaMorkCursorHandle *cursor = NULL;
    Space staged;
    uint64_t loaded = 0;
    SpaceBridgeImportResult outcome = SPACE_BRIDGE_IMPORT_ERROR;

    if (out_loaded)
        *out_loaded = 0;
    if (!dst || !bridge)
        return SPACE_BRIDGE_IMPORT_ERROR;
    if (!dst->native.universe)
        return SPACE_BRIDGE_IMPORT_NEEDS_TEXT_FALLBACK;

    arena_init(&scratch);
    imported_bridge_expr_memo_init(&memo);
    space_init_with_universe(&staged, dst->native.universe);
    cursor = cetta_mork_bridge_cursor_new(bridge);
    if (!cursor)
        goto done;

    for (;;) {
        ArenaMark scratch_mark = arena_mark(&scratch);
        bool moved = false;
        uint8_t *bytes = NULL;
        size_t len = 0;
        AtomId atom_id = CETTA_ATOM_ID_NONE;

        if (!cetta_mork_bridge_cursor_next_val(cursor, &moved))
            goto done;
        if (!moved)
            break;
        if (!cetta_mork_bridge_cursor_path_bytes(cursor, &bytes, &len)) {
            cetta_mork_bridge_bytes_free(bytes, len);
            goto done;
        }

        ImportedBridgeExprDecodeResult result =
            imported_bridge_expr_to_atom_id_cached(dst->native.universe, &scratch, &memo,
                                                   bytes, len, &atom_id);
        cetta_mork_bridge_bytes_free(bytes, len);
        if (result == IMPORTED_BRIDGE_EXPR_DECODE_NEEDS_TEXT_FALLBACK) {
            arena_reset(&scratch, scratch_mark);
            outcome = SPACE_BRIDGE_IMPORT_NEEDS_TEXT_FALLBACK;
            goto done;
        }
        if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK ||
            atom_id == CETTA_ATOM_ID_NONE) {
            arena_reset(&scratch, scratch_mark);
            goto done;
        }
        space_add_atom_id(&staged, atom_id);
        loaded++;
        arena_reset(&scratch, scratch_mark);
    }

    for (uint32_t i = 0; i < staged.native.len; i++)
        space_add_atom_id(dst, staged.native.atom_ids[i]);
    outcome = SPACE_BRIDGE_IMPORT_OK;

done:
    if (cursor)
        cetta_mork_bridge_cursor_free(cursor);
    space_free(&staged);
    imported_bridge_expr_memo_free(&memo);
    arena_free(&scratch);
    if (outcome == SPACE_BRIDGE_IMPORT_OK && out_loaded)
        *out_loaded = loaded;
    return outcome;
}

static bool imported_materialize_bridge_space(Space *dst,
                                              Arena *persistent_arena,
                                              CettaMorkSpaceHandle *bridge,
                                              uint64_t *out_loaded) {
    SpaceTransferEndpoint dst_endpoint = {
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = dst,
    };
    SpaceTransferEndpoint src_endpoint = {
        .kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE,
        .bridge = bridge,
    };

    if (out_loaded)
        *out_loaded = 0;
    SpaceTransferResult result =
        space_match_backend_transfer_resolved_result(
            dst_endpoint, src_endpoint, persistent_arena, out_loaded);
    if (result == SPACE_TRANSFER_OK)
        return true;
    if (result != SPACE_TRANSFER_NEEDS_TEXT_FALLBACK)
        return false;

    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t rows = 0;
    bool ok = cetta_mork_bridge_space_dump(bridge, &packet, &packet_len, &rows) &&
              imported_parse_dump_text_into_space(dst, persistent_arena, packet, packet_len);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    (void)rows;
    if (ok && out_loaded)
        *out_loaded = dst->native.len;
    return ok;
}
bool space_match_backend_attach_act_file(Space *s, const char *path, uint64_t *out_loaded) {
    MorkImportedState *mst;
    ImportedBridgeState *st;
    uint64_t loaded = 0;
    if (out_loaded)
        *out_loaded = 0;
    if (!s || !path)
        return false;
    if (s->match_backend.kind != SPACE_ENGINE_MORK)
        return false;
    if (space_is_ordered(s) || space_length(s) != 0)
        return false;

    mst = mork_imported_state(s);
    st = mst ? &mst->bridge : NULL;
    if (!st)
        return false;
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    st->built = false;
    st->dirty = false;
    st->bridge_active = false;
    mst->attached_compiled = false;
    mst->attached_count = 0;

    if (!mork_imported_ensure_bridge_space(mst))
        return false;
    if (!cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space))
        return false;
    if (!cetta_mork_bridge_space_load_act_file((CettaMorkSpaceHandle *)st->bridge_space,
                                               (const uint8_t *)path, strlen(path),
                                               &loaded)) {
        return false;
    }
    if (loaded > UINT32_MAX) {
        cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space);
        return false;
    }

    st->bridge_active = true;
    mst->attached_compiled = true;
    mst->attached_count = (uint32_t)loaded;
    st->built = true;
    st->dirty = false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_OPEN);
    if (out_loaded)
        *out_loaded = loaded;
    return true;
}

static bool native_materialize_native_storage(Space *s, Arena *persistent_arena) {
    (void)s;
    (void)persistent_arena;
    return true;
}

static bool pathmap_materialize_native_storage(Space *s, Arena *persistent_arena) {
    ImportedBridgeState *st;

    if (!s)
        return false;
    (void)persistent_arena;
    st = s->match_backend.kind == SPACE_ENGINE_PATHMAP
        ? &s->match_backend.pathmap.bridge
        : NULL;
    if (!st)
        return false;
    if (!st->bridge_active)
        return true;
    return imported_shadow_refresh_from_projection(s);
}

static bool mork_materialize_native_storage(Space *s, Arena *persistent_arena) {
    MorkImportedState *mst;
    ImportedBridgeState *st;
    Space *fresh = NULL;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    uint32_t logical_count = 0;
#endif
    bool ok = false;

    if (!s)
        return false;
    mst = mork_imported_state(s);
    st = mst ? &mst->bridge : NULL;
    if (!st)
        return false;
    if (!mst->attached_compiled)
        return true;
    if (!st->bridge_active || !st->bridge_space)
        return false;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    logical_count = mst->attached_count;
#endif
    fresh = cetta_malloc(sizeof(Space));
    space_init_with_universe(fresh, s ? s->native.universe : NULL);
    fresh->kind = s->kind;
    if (!space_match_backend_try_set(fresh, s->match_backend.kind)) {
        goto done;
    }
    ok = imported_materialize_bridge_space(
        fresh,
        persistent_arena,
        (CettaMorkSpaceHandle *)st->bridge_space,
        NULL);
    if (!ok)
        goto done;

    imported_flat_state_clear(st);
    st->built = false;
    st->dirty = false;
    st->bridge_active = false;
    mst->attached_compiled = false;
    mst->attached_count = 0;
    if (st->bridge_space)
        (void)cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space);

    space_replace_contents(s, fresh);
    if (ok) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_MATERIALIZE);
#if CETTA_BUILD_WITH_RUNTIME_STATS
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_MATERIALIZE_ATOMS,
                                logical_count);
#endif
    }

done:
    if (fresh) {
        if (ok) {
            free(fresh);
        } else {
            space_free(fresh);
            free(fresh);
        }
    }
    return ok;
}

bool space_match_backend_materialize_native_storage(Space *s,
                                                    Arena *persistent_arena) {
    if (!s)
        return false;
    if (s->match_backend.ops && s->match_backend.ops->materialize_native_storage)
        return s->match_backend.ops->materialize_native_storage(s, persistent_arena);
    return true;
}

bool space_match_backend_materialize_attached(Space *s, Arena *persistent_arena) {
    if (!s)
        return false;
    if (s->match_backend.kind != SPACE_ENGINE_MORK)
        return true;
    if (!space_match_backend_is_attached_compiled(s))
        return true;
    return space_match_backend_materialize_native_storage(s, persistent_arena);
}

bool space_match_backend_is_attached_compiled(const Space *s) {
    const MorkImportedState *mst = mork_imported_state_const(s);
    return s && backend_uses_bridge_adapter(s) && mst && mst->attached_compiled;
}

bool space_match_backend_bridge_space(Space *s,
                                      CettaMorkSpaceHandle **out_bridge) {
    MorkImportedState *mst;
    ImportedBridgeState *st;

    if (out_bridge)
        *out_bridge = NULL;
    if (!s || !backend_uses_bridge_adapter(s) ||
        space_is_ordered(s)) {
        return false;
    }

    mst = mork_imported_state(s);
    st = mst ? &mst->bridge : NULL;
    if (!st)
        return false;
    if (mst->attached_compiled) {
        if (!st->bridge_space)
            return false;
        if (out_bridge)
            *out_bridge = (CettaMorkSpaceHandle *)st->bridge_space;
        return true;
    }

    if (!st->bridge_active && !backend_rebuild_bridge(s))
        return false;
    if (!st->bridge_active || !st->bridge_space)
        return false;
    if (out_bridge)
        *out_bridge = (CettaMorkSpaceHandle *)st->bridge_space;
    return true;
}

bool space_match_backend_store_atom_id_direct(Space *s, AtomId atom_id,
                                              Atom *atom) {
    if (!s || !s->match_backend.ops || !s->match_backend.ops->store_atom_id_direct)
        return false;
    return s->match_backend.ops->store_atom_id_direct(s, atom_id, atom);
}

bool space_match_backend_remove_atom_id_direct(Space *s, AtomId atom_id) {
    if (!s || !s->match_backend.ops || !s->match_backend.ops->remove_atom_id_direct)
        return false;
    return s->match_backend.ops->remove_atom_id_direct(s, atom_id);
}

bool space_match_backend_remove_atom_direct(Space *s, Atom *atom) {
    if (!s || !s->match_backend.ops || !s->match_backend.ops->remove_atom_direct)
        return false;
    return s->match_backend.ops->remove_atom_direct(s, atom);
}

bool space_match_backend_truncate_direct(Space *s, uint32_t new_len) {
    if (!s || !s->match_backend.ops || !s->match_backend.ops->truncate_direct)
        return false;
    return s->match_backend.ops->truncate_direct(s, new_len);
}

bool space_match_backend_contains_atom_structural_direct(Space *s,
                                                         Atom *atom,
                                                         bool *out_found) {
    ImportedBridgeState *st;
    CettaMorkSpaceHandle *bridge_space = NULL;
    Arena scratch;
    bool ok = false;

    if (out_found)
        *out_found = false;
    if (!s || !atom || space_is_ordered(s) ||
        !space_engine_uses_pathmap(s->match_backend.kind)) {
        return false;
    }

    st = backend_bridge_state(s);
    if (!st)
        return false;

    switch (s->match_backend.kind) {
    case SPACE_ENGINE_PATHMAP:
        if (!(st->bridge_active && st->bridge_space) &&
            !pathmap_local_ensure_bridge_live(s)) {
            return false;
        }
        bridge_space = (CettaMorkSpaceHandle *)st->bridge_space;
        break;
    case SPACE_ENGINE_MORK:
        if (!space_match_backend_bridge_space(s, &bridge_space))
            return false;
        break;
    default:
        return false;
    }

    if (!bridge_space)
        return false;

    arena_init(&scratch);
    ok = imported_bridge_contains_atom_structural(
        &scratch, bridge_space, atom, out_found);
    arena_free(&scratch);
    return ok;
}

uint32_t space_match_backend_logical_len(const Space *s) {
    if (!s)
        return 0;
    if (s->match_backend.ops && s->match_backend.ops->logical_len)
        return s->match_backend.ops->logical_len(s);
    return shadow_storage_logical_len(s);
}

AtomId space_match_backend_get_atom_id_at(const Space *s, uint32_t idx) {
    if (!s)
        return CETTA_ATOM_ID_NONE;
    if (s->match_backend.ops && s->match_backend.ops->get_atom_id_at)
        return s->match_backend.ops->get_atom_id_at(s, idx);
    return shadow_storage_get_atom_id_at(s, idx);
}

Atom *space_match_backend_get_at(const Space *s, uint32_t idx) {
    if (!s)
        return NULL;
    if (s->match_backend.ops && s->match_backend.ops->get_at)
        return s->match_backend.ops->get_at(s, idx);
    return shadow_storage_get_at(s, idx);
}

bool space_match_backend_mork_visit_bindings_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    Arena scratch;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t row_count = 0;
    ImportedBridgeVarMap query_vars;
    bool success = true;
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t parsed_rows = 0;
    bool wide_tokens = false;

    if (!bridge || !a || !query || !visitor)
        return false;

    arena_init(&scratch);
    char *pattern_text = atom_to_parseable_string(&scratch, query);
    if (imported_bridge_query_text_has_internal_vars(pattern_text)) {
        arena_free(&scratch);
        return false;
    }
    imported_bridge_varmap_init(&query_vars);
    if (!imported_bridge_collect_vars(query, &query_vars)) {
        imported_bridge_varmap_free(&query_vars);
        arena_free(&scratch);
        return false;
    }
    if (cetta_mork_bridge_space_query_contextual_rows(
            bridge, (const uint8_t *)pattern_text, strlen(pattern_text),
            &packet, &packet_len, &row_count)) {
        arena_free(&scratch);
        success = imported_bridge_visit_contextual_query_rows_packet(
            a, packet, packet_len, row_count, &query_vars, NULL, visitor, ctx);
        imported_bridge_varmap_free(&query_vars);
        cetta_mork_bridge_bytes_free(packet, packet_len);
        return success;
    }
    cetta_mork_bridge_bytes_free(packet, packet_len);
    packet = NULL;
    packet_len = 0;
    row_count = 0;

    bool ok = cetta_mork_bridge_space_query_bindings_query_only_v2(
        bridge, (const uint8_t *)pattern_text, strlen(pattern_text),
        &packet, &packet_len, &row_count);
    arena_free(&scratch);
    if (!ok) {
        imported_bridge_varmap_free(&query_vars);
        return false;
    }

    if (!imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &flags) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &parsed_rows)) {
        success = false;
        goto cleanup;
    }
    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_QUERY_ONLY_V2_VERSION ||
        parsed_rows != row_count) {
        success = false;
        goto cleanup;
    }
    wide_tokens =
        (flags & IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS) != 0;
    if ((flags & ~(IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS)) != 0 ||
        (flags & (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                  IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES)) !=
            (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
             IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES)) {
        success = false;
        goto cleanup;
    }

    for (uint32_t row = 0; row < parsed_rows && success; row++) {
        uint32_t ref_count = 0;
        uint32_t binding_count = 0;
        Bindings row_bindings;
        bindings_init(&row_bindings);

        if (!imported_bridge_read_u32(packet, packet_len, &off, &ref_count) ||
            off + (size_t)ref_count * 4u > packet_len) {
            bindings_free(&row_bindings);
            success = false;
            break;
        }
        off += (size_t)ref_count * 4u;

        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            bindings_free(&row_bindings);
            success = false;
            break;
        }

        for (uint32_t bi = 0; bi < binding_count && success; bi++) {
            uint16_t query_slot = 0;
            uint8_t value_env = 0;
            uint8_t value_flags = 0;
            uint32_t expr_len = 0;
            if (!imported_bridge_read_u16(packet, packet_len, &off, &query_slot) ||
                off + 2 > packet_len) {
                success = false;
                break;
            }
            value_env = packet[off++];
            value_flags = packet[off++];
            if (!imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
                off + expr_len > packet_len) {
                success = false;
                break;
            }
            ImportedBridgeVarSlot *key_slot =
                imported_bridge_varmap_lookup(&query_vars, query_slot);
            if (!key_slot) {
                success = false;
                break;
            }
            bool value_ok = true;
            Atom *value = imported_bridge_parse_value_raw_query_only_v2(
                a, packet + off, expr_len, value_env, value_flags,
                wide_tokens, &value_ok);
            off += expr_len;
            if (!value_ok ||
                !bindings_add_id(&row_bindings, key_slot->var_id, key_slot->spelling, value)) {
                success = false;
                break;
            }
        }

        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_MORK_DIRECT_ROW);
        if (success && !bindings_has_loop(&row_bindings) &&
            !visitor(&row_bindings, ctx)) {
            success = false;
        }
        bindings_free(&row_bindings);
    }

cleanup:
    imported_bridge_varmap_free(&query_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    return success;
}

static bool mork_query_collect_bindings(const Bindings *bindings, void *ctx) {
    return binding_set_push((BindingSet *)ctx, bindings);
}

bool space_match_backend_mork_query_bindings_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom *query,
    BindingSet *out) {
    if (!out)
        return false;
    binding_set_init(out);
    if (space_match_backend_mork_visit_bindings_direct(
            bridge, a, query, mork_query_collect_bindings, out)) {
        return true;
    }
    binding_set_free(out);
    binding_set_init(out);
    return false;
}

static bool mork_visit_collected_bindings(const BindingSet *set,
                                          CettaMorkBindingsVisitor visitor,
                                          void *ctx) {
    if (!set || !visitor)
        return false;
    for (uint32_t i = 0; i < set->len; i++) {
        if (!visitor(&set->items[i], ctx))
            return false;
    }
    return true;
}

static bool mork_query_conjunction_iterative(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    uint32_t npatterns,
    const Bindings *seed,
    BindingSet *out) {
    if (!bridge || !a || !patterns || !out)
        return false;
    binding_set_init(out);
    if (npatterns == 0) {
        if (seed)
            return binding_set_push(out, seed);
        return true;
    }

    BindingSet cur;
    binding_set_init(&cur);
    if (seed) {
        if (!binding_set_push(&cur, seed)) {
            binding_set_free(&cur);
            return false;
        }
    } else {
        Bindings empty;
        bindings_init(&empty);
        if (!binding_set_push(&cur, &empty)) {
            bindings_free(&empty);
            binding_set_free(&cur);
            return false;
        }
        bindings_free(&empty);
    }

    for (uint32_t pi = 0; pi < npatterns; pi++) {
        bool success = true;
        BindingSet next;
        binding_set_init(&next);
        for (uint32_t bi = 0; bi < cur.len; bi++) {
            Atom *grounded = bindings_apply_if_vars(&cur.items[bi], a, patterns[pi]);
            BindingSet matches;
            binding_set_init(&matches);
            if (!space_match_backend_mork_query_bindings_direct(
                    bridge, a, grounded, &matches)) {
                binding_set_free(&matches);
                success = false;
                break;
            }
            for (uint32_t mi = 0; mi < matches.len; mi++) {
                Bindings merged;
                bindings_init(&merged);
                if (!bindings_try_merge_live(&merged, &cur.items[bi]) ||
                    !bindings_try_merge_live(&merged, &matches.items[mi])) {
                    bindings_free(&merged);
                    success = false;
                    break;
                }
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_MORK_CONJ_MERGE);
                if (!bindings_has_loop(&merged))
                    binding_set_push_move(&next, &merged);
                bindings_free(&merged);
            }
            binding_set_free(&matches);
            if (!success)
                break;
        }
        binding_set_free(&cur);
        if (!success) {
            binding_set_free(&next);
            binding_set_free(out);
            binding_set_init(out);
            return false;
        }
        cur = next;
        if (cur.len == 0)
            break;
    }

    *out = cur;
    return true;
}

bool space_match_backend_mork_visit_conjunction_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    uint32_t npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    if (!bridge || !a || !patterns || !visitor)
        return false;
    if (npatterns == 0) {
        if (seed)
            return visitor(seed, ctx);
        return true;
    }
    if (npatterns > IMPORTED_CONJUNCTION_PATTERN_LIMIT) {
        BindingSet collected;
        if (!mork_query_conjunction_iterative(bridge, a, patterns, npatterns,
                                              seed, &collected)) {
            return false;
        }
        bool ok = mork_visit_collected_bindings(&collected, visitor, ctx);
        binding_set_free(&collected);
        return ok;
    }

    Arena scratch;
    arena_init(&scratch);
    arena_set_hashcons(&scratch, NULL);
    Atom **grounded = arena_alloc(&scratch, sizeof(Atom *) * npatterns);
    ImportedBridgeVarMap query_vars;
    imported_bridge_varmap_init(&query_vars);

    bool success = true;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t row_count = 0;
    uint32_t factor_count = 0;
    bool multi_ref_groups = false;
    bool direct_multiplicities = false;
    bool wide_tokens = false;

    for (uint32_t i = 0; i < npatterns; i++) {
        grounded[i] = seed ? bindings_apply_if_vars(seed, &scratch, patterns[i])
                           : patterns[i];
        if (!imported_bridge_collect_vars(grounded[i], &query_vars)) {
            success = false;
            goto cleanup;
        }
    }

    char *query_text = imported_bridge_build_conjunction_text(&scratch, grounded, npatterns);
    if (!query_text) {
        success = false;
        goto cleanup;
    }
    if (imported_bridge_query_text_has_internal_vars(query_text)) {
        imported_bridge_varmap_free(&query_vars);
        arena_free(&scratch);
        BindingSet collected;
        if (!mork_query_conjunction_iterative(bridge, a, patterns, npatterns,
                                              seed, &collected)) {
            return false;
        }
        bool ok = mork_visit_collected_bindings(&collected, visitor, ctx);
        binding_set_free(&collected);
        return ok;
    }
    if (cetta_mork_bridge_space_query_contextual_rows(
            bridge, (const uint8_t *)query_text, strlen(query_text),
            &packet, &packet_len, &row_count)) {
        success = imported_bridge_visit_contextual_query_rows_packet(
            a, packet, packet_len, row_count, &query_vars, seed, visitor, ctx);
        goto cleanup;
    }
    cetta_mork_bridge_bytes_free(packet, packet_len);
    packet = NULL;
    packet_len = 0;
    row_count = 0;

    if (!cetta_mork_bridge_space_query_bindings_multi_ref_v3(
            bridge, (const uint8_t *)query_text, strlen(query_text),
            &packet, &packet_len, &row_count)) {
        imported_bridge_varmap_free(&query_vars);
        arena_free(&scratch);
        BindingSet collected;
        if (!mork_query_conjunction_iterative(bridge, a, patterns, npatterns,
                                              seed, &collected)) {
            return false;
        }
        bool ok = mork_visit_collected_bindings(&collected, visitor, ctx);
        binding_set_free(&collected);
        return ok;
    }

    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t parsed_rows = 0;
    if (!imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &flags) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &factor_count) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &parsed_rows)) {
        success = false;
        goto cleanup;
    }
    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_MULTI_REF_V3_VERSION) {
        success = false;
        goto cleanup;
    }
    multi_ref_groups = (flags & IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS) != 0;
    direct_multiplicities =
        (flags & IMPORTED_MORK_MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES) != 0;
    wide_tokens =
        (flags & IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS) != 0;
    if ((flags & (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                  IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES)) !=
            (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
             IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES) ||
        (flags & ~(IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES |
                   IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS |
                   IMPORTED_MORK_MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS)) != 0 ||
        multi_ref_groups == direct_multiplicities ||
        factor_count != npatterns || parsed_rows != row_count) {
        success = false;
        goto cleanup;
    }

    for (uint32_t row = 0; row < parsed_rows && success; row++) {
        ImportedBridgeValueVarMap value_vars;
        imported_bridge_value_varmap_init(&value_vars);
        bool merged_inited = false;
        for (uint32_t fi = 0; fi < factor_count; fi++) {
            if (direct_multiplicities) {
                uint32_t factor_multiplicity = 0;
                if (!imported_bridge_read_u32(packet, packet_len, &off,
                                              &factor_multiplicity) ||
                    factor_multiplicity == 0) {
                    success = false;
                    break;
                }
            } else {
                uint32_t ref_count = 0;
                if (!imported_bridge_read_u32(packet, packet_len, &off, &ref_count) ||
                    off + (size_t)ref_count * 4u > packet_len) {
                    success = false;
                    break;
                }
                off += (size_t)ref_count * 4u;
            }
        }
        if (!success) {
            imported_bridge_value_varmap_free(&value_vars);
            break;
        }

        uint32_t binding_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            success = false;
            imported_bridge_value_varmap_free(&value_vars);
            break;
        }

        Bindings merged;
        if (seed) {
            if (!bindings_clone(&merged, seed)) {
                success = false;
                imported_bridge_value_varmap_free(&value_vars);
                break;
            }
        } else {
            bindings_init(&merged);
        }
        merged_inited = true;

        for (uint32_t bi = 0; bi < binding_count && success; bi++) {
            uint16_t query_slot = 0;
            uint8_t value_env = 0;
            uint8_t value_flags = 0;
            uint32_t expr_len = 0;
            if (!imported_bridge_read_u16(packet, packet_len, &off, &query_slot) ||
                off + 2 > packet_len) {
                success = false;
                break;
            }
            value_env = packet[off++];
            value_flags = packet[off++];
            if (!imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
                off + expr_len > packet_len) {
                success = false;
                break;
            }
            ImportedBridgeVarSlot *slot =
                imported_bridge_varmap_lookup(&query_vars, query_slot);
            if (!slot) {
                success = false;
                break;
            }
            bool value_ok = true;
            Atom *value = imported_bridge_parse_value_raw_multi_ref_v3(
                a, packet + off, expr_len, value_env, value_flags,
                wide_tokens, &value_vars, &value_ok);
            off += expr_len;
            if (!value_ok ||
                !bindings_add_id(&merged, slot->var_id, slot->spelling, value)) {
                success = false;
                break;
            }
        }

        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_MORK_CONJ_DIRECT);
        if (success && !bindings_has_loop(&merged) && !visitor(&merged, ctx)) {
            success = false;
        }
        if (merged_inited)
            bindings_free(&merged);
        imported_bridge_value_varmap_free(&value_vars);
    }

cleanup:
    imported_bridge_varmap_free(&query_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    arena_free(&scratch);
    return success;
}

bool space_match_backend_mork_query_conjunction_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    uint32_t npatterns,
    const Bindings *seed,
    BindingSet *out) {
    if (!out)
        return false;
    binding_set_init(out);
    if (space_match_backend_mork_visit_conjunction_direct(
            bridge, a, patterns, npatterns, seed,
            mork_query_collect_bindings, out)) {
        return true;
    }
    binding_set_free(out);
    binding_set_init(out);
    return false;
}

static void imported_builder_push(ImportedFlatBuilder *b, ImportedFlatToken tok) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 16;
        b->items = cetta_realloc(b->items, sizeof(ImportedFlatToken) * b->cap);
    }
    b->items[b->len++] = tok;
}

static void imported_flatten_atom(ImportedFlatBuilder *b, Atom *atom) {
    uint32_t start = b->len;
    ImportedFlatToken tok = {0};
    tok.origin = atom;
    tok.origin_id = CETTA_ATOM_ID_NONE;
    tok.span = 1;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        tok.kind = IMPORTED_FLAT_SYMBOL;
        tok.sym_id = atom->sym_id;
        imported_builder_push(b, tok);
        return;
    case ATOM_VAR:
        tok.kind = IMPORTED_FLAT_VAR;
        tok.sym_id = atom->sym_id;
        tok.var_id = atom->var_id;
        imported_builder_push(b, tok);
        return;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT) {
            tok.kind = IMPORTED_FLAT_INT;
            tok.ival = atom->ground.ival;
        } else if (atom->ground.gkind == GV_FLOAT) {
            tok.kind = IMPORTED_FLAT_FLOAT;
            tok.fval = atom->ground.fval;
        } else if (atom->ground.gkind == GV_BOOL) {
            tok.kind = IMPORTED_FLAT_BOOL;
            tok.bval = atom->ground.bval;
        } else if (atom->ground.gkind == GV_STRING) {
            tok.kind = IMPORTED_FLAT_STRING;
            tok.sym_id = symbol_intern_cstr(g_symbols, atom->ground.sval);
        } else {
            tok.kind = IMPORTED_FLAT_GROUNDED_OTHER;
        }
        imported_builder_push(b, tok);
        return;
    case ATOM_EXPR:
        tok.kind = IMPORTED_FLAT_EXPR;
        tok.arity = atom->expr.len;
        imported_builder_push(b, tok);
        for (uint32_t i = 0; i < atom->expr.len; i++)
            imported_flatten_atom(b, atom->expr.elems[i]);
        b->items[start].span = b->len - start;
        return;
    }
}

static bool imported_flatten_atom_id(ImportedFlatBuilder *b,
                                     const TermUniverse *universe,
                                     AtomId atom_id) {
    if (!b || !universe || atom_id == CETTA_ATOM_ID_NONE || !tu_hdr(universe, atom_id))
        return false;
    uint32_t start = b->len;
    ImportedFlatToken tok = {0};
    tok.origin = NULL;
    tok.origin_id = atom_id;
    tok.span = 1;
    switch (tu_kind(universe, atom_id)) {
    case ATOM_SYMBOL:
        tok.kind = IMPORTED_FLAT_SYMBOL;
        tok.sym_id = tu_sym(universe, atom_id);
        imported_builder_push(b, tok);
        return true;
    case ATOM_VAR:
        tok.kind = IMPORTED_FLAT_VAR;
        tok.sym_id = tu_sym(universe, atom_id);
        tok.var_id = tu_var_id(universe, atom_id);
        imported_builder_push(b, tok);
        return true;
    case ATOM_GROUNDED:
        switch (tu_ground_kind(universe, atom_id)) {
        case GV_INT:
            tok.kind = IMPORTED_FLAT_INT;
            tok.ival = tu_int(universe, atom_id);
            break;
        case GV_FLOAT:
            tok.kind = IMPORTED_FLAT_FLOAT;
            tok.fval = tu_float(universe, atom_id);
            break;
        case GV_BOOL:
            tok.kind = IMPORTED_FLAT_BOOL;
            tok.bval = tu_bool(universe, atom_id);
            break;
        case GV_STRING:
            tok.kind = IMPORTED_FLAT_STRING;
            tok.sym_id = symbol_intern_cstr(g_symbols, tu_string_cstr(universe, atom_id));
            break;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        imported_builder_push(b, tok);
        return true;
    case ATOM_EXPR:
        tok.kind = IMPORTED_FLAT_EXPR;
        tok.arity = tu_arity(universe, atom_id);
        imported_builder_push(b, tok);
        for (uint32_t i = 0; i < tu_arity(universe, atom_id); i++) {
            if (!imported_flatten_atom_id(b, universe, tu_child(universe, atom_id, i)))
                return false;
        }
        b->items[start].span = b->len - start;
        return true;
    }
    return false;
}

static Atom *imported_token_atom(const ImportedFlatToken *tok,
                                 const TermUniverse *universe) {
    if (!tok)
        return NULL;
    if (tok->origin)
        return tok->origin;
    if (tok->origin_id != CETTA_ATOM_ID_NONE)
        return term_universe_get_atom(universe, tok->origin_id);
    return NULL;
}

static Atom *imported_token_copy_epoch(Arena *a, const ImportedFlatToken *tok,
                                       const TermUniverse *universe,
                                       uint32_t epoch) {
    if (!tok)
        return NULL;
    if (tok->origin_id != CETTA_ATOM_ID_NONE && universe)
        return term_universe_copy_atom_epoch(universe, a, tok->origin_id, epoch);
    if (tok->origin)
        return atom_freshen_epoch(a, tok->origin, epoch);
    return NULL;
}

static bool imported_token_equal(const ImportedFlatToken *lhs,
                                 const ImportedFlatToken *rhs) {
    if (lhs->kind != rhs->kind) return false;
    switch (lhs->kind) {
    case IMPORTED_FLAT_SYMBOL:
        return lhs->sym_id == rhs->sym_id;
    case IMPORTED_FLAT_VAR:
        return lhs->var_id == rhs->var_id;
    case IMPORTED_FLAT_EXPR:
        return lhs->arity == rhs->arity;
    case IMPORTED_FLAT_INT:
        return lhs->ival == rhs->ival;
    case IMPORTED_FLAT_FLOAT:
        return lhs->fval == rhs->fval;
    case IMPORTED_FLAT_BOOL:
        return lhs->bval == rhs->bval;
    case IMPORTED_FLAT_STRING:
        return lhs->sym_id == rhs->sym_id;
    case IMPORTED_FLAT_GROUNDED_OTHER:
        if (lhs->origin_id != CETTA_ATOM_ID_NONE &&
            rhs->origin_id != CETTA_ATOM_ID_NONE) {
            return lhs->origin_id == rhs->origin_id;
        }
        if (!lhs->origin || !rhs->origin)
            return false;
        return atom_eq(lhs->origin, rhs->origin);
    }
    return false;
}

static bool imported_flat_equal(const ImportedFlatToken *lhs, uint32_t li,
                                const ImportedFlatToken *rhs, uint32_t ri) {
    if (lhs[li].span != rhs[ri].span) return false;
    for (uint32_t off = 0; off < lhs[li].span; off++) {
        if (!imported_token_equal(&lhs[li + off], &rhs[ri + off]))
            return false;
    }
    return true;
}

static void imported_bridge_varmap_init(ImportedBridgeVarMap *map) {
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static void imported_bridge_expr_varmap_init(ImportedBridgeExprVarMap *map) {
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static void imported_bridge_varmap_free(ImportedBridgeVarMap *map) {
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static void imported_bridge_expr_varmap_free(ImportedBridgeExprVarMap *map) {
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static ImportedBridgeVarSlot *imported_bridge_varmap_lookup(ImportedBridgeVarMap *map,
                                                            uint32_t index) {
    if (!map || index >= map->len)
        return NULL;
    return &map->items[index];
}

static bool imported_bridge_varmap_add(ImportedBridgeVarMap *map,
                                       VarId var_id,
                                       SymbolId spelling) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].var_id == var_id)
            return true;
    }
    if (map->len >= map->cap) {
        map->cap = map->cap ? map->cap * 2 : 8;
        map->items = cetta_realloc(map->items, sizeof(ImportedBridgeVarSlot) * map->cap);
    }
    map->items[map->len++] = (ImportedBridgeVarSlot){
        .var_id = var_id,
        .spelling = spelling,
    };
    return true;
}

static bool imported_bridge_collect_vars(Atom *atom, ImportedBridgeVarMap *map) {
    if (!atom)
        return true;
    switch (atom->kind) {
    case ATOM_VAR:
        if (imported_bridge_internal_var(atom))
            return true;
        return imported_bridge_varmap_add(map, atom->var_id, atom->sym_id);
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (!imported_bridge_collect_vars(atom->expr.elems[i], map))
                return false;
        }
        return true;
    default:
        return true;
    }
}

static bool imported_bridge_query_text_has_internal_vars(const char *text) {
    if (!text)
        return false;
    return strstr(text, "$__mork_") != NULL ||
           strstr(text, "$$__mork_") != NULL;
}

static bool imported_text_may_contain_vars(const uint8_t *text, size_t len) {
    if (!text)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '$')
            return true;
    }
    return false;
}

static bool imported_bridge_read_u32(const uint8_t *packet, size_t len, size_t *off,
                                     uint32_t *out) {
    if (*off + 4 > len)
        return false;
    *out = ((uint32_t)packet[*off] << 24) |
           ((uint32_t)packet[*off + 1] << 16) |
           ((uint32_t)packet[*off + 2] << 8) |
           (uint32_t)packet[*off + 3];
    *off += 4;
    return true;
}

static bool imported_bridge_read_u16(const uint8_t *packet, size_t len, size_t *off,
                                     uint16_t *out) {
    if (*off + 2 > len)
        return false;
    *out = (uint16_t)(((uint16_t)packet[*off] << 8) |
                      (uint16_t)packet[*off + 1]);
    *off += 2;
    return true;
}

static bool imported_bridge_read_u8(const uint8_t *packet, size_t len, size_t *off,
                                    uint8_t *out) {
    if (*off + 1 > len)
        return false;
    *out = packet[*off];
    *off += 1;
    return true;
}

static bool imported_bridge_read_u64(const uint8_t *packet, size_t len, size_t *off,
                                     uint64_t *out) {
    if (*off + 8 > len)
        return false;
    *out = ((uint64_t)packet[*off] << 56) |
           ((uint64_t)packet[*off + 1] << 48) |
           ((uint64_t)packet[*off + 2] << 40) |
           ((uint64_t)packet[*off + 3] << 32) |
           ((uint64_t)packet[*off + 4] << 24) |
           ((uint64_t)packet[*off + 5] << 16) |
           ((uint64_t)packet[*off + 6] << 8) |
           (uint64_t)packet[*off + 7];
    *off += 8;
    return true;
}

static Atom *imported_bridge_parse_token_bytes(Arena *a,
                                               const uint8_t *bytes,
                                               uint32_t len,
                                               bool exact_len_reliable,
                                               bool *ok) {
    if (!ok || !*ok) {
        if (ok)
            *ok = false;
        return NULL;
    }
    char *tok = arena_alloc(a, (size_t)len + 1);
    memcpy(tok, bytes, len);
    tok[len] = '\0';

    /* Raw bridge expr bytes encode token lengths in 6 bits. Once a token hits
       63 bytes, CeTTa cannot distinguish "exactly 63 bytes" from "truncated
       longer token", so the imported fast path must refuse it and fall back.
       Wide packet rows carry an explicit u32 length and can accept the token. */
    if (!exact_len_reliable && len == 63) {
        *ok = false;
        return NULL;
    }

    if (len > 0 && (tok[0] == '"' || tok[len - 1] == '"')) {
        size_t pos = 0;
        Atom *parsed = parse_sexpr(a, tok, &pos);
        if (parsed && pos == len &&
            parsed->kind == ATOM_GROUNDED &&
            parsed->ground.gkind == GV_STRING)
            return parsed;
        *ok = false;
        return NULL;
    }

    if (strcmp(tok, "True") == 0)  return atom_bool(a, true);
    if (strcmp(tok, "False") == 0) return atom_bool(a, false);
    if (strcmp(tok, "PI") == 0)    return atom_float(a, 3.14159265358979323846);
    if (strcmp(tok, "EXP") == 0)   return atom_float(a, 2.71828182845904523536);

    char *endp = NULL;
    errno = 0;
    long long ival = strtoll(tok, &endp, 10);
    if (*endp == '\0' && errno == 0)
        return atom_int(a, (int64_t)ival);

    if (strchr(tok, '.')) {
        char *fendp = NULL;
        errno = 0;
        double fval = strtod(tok, &fendp);
        if (*fendp == '\0' && errno == 0)
            return atom_float(a, fval);
    }

    const char *canonical = parser_canonicalize_namespace_token(a, tok);
    return atom_symbol_id(a, symbol_intern_cstr(g_symbols, canonical));
}

static Atom *imported_bridge_parse_value_raw_query_only_v2_rec(
    Arena *a,
    const uint8_t *expr,
    size_t len,
    size_t *off,
    bool *ok
) {
    if (!expr || !off || !ok || !*ok || *off >= len) {
        if (ok) *ok = false;
        return NULL;
    }

    uint8_t tag = expr[*off];
    if (tag == IMPORTED_MORK_TAG_NEWVAR ||
        (tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_VARREF_PREFIX) {
        *ok = false;
        (*off)++;
        return NULL;
    }

    if ((tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_SYMBOL_PREFIX) {
        uint32_t sym_len = (uint32_t)(tag & 0x3Fu);
        if (sym_len == 0 || *off + 1u + sym_len > len) {
            *ok = false;
            return NULL;
        }
        Atom *atom = imported_bridge_parse_token_bytes(
            a, expr + *off + 1u, sym_len, false, ok);
        if (!*ok)
            return NULL;
        *off += 1u + sym_len;
        return atom;
    }

    uint32_t arity = (uint32_t)(tag & 0x3Fu);
    (*off)++;
    Atom **elems = arity ? arena_alloc(a, sizeof(Atom *) * arity) : NULL;
    for (uint32_t i = 0; i < arity; i++) {
        elems[i] = imported_bridge_parse_value_raw_query_only_v2_rec(
            a, expr, len, off, ok);
        if (!*ok)
            return NULL;
    }
    return atom_expr(a, elems, arity);
}

static Atom *imported_bridge_parse_value_wide_query_only_v2_rec(
    Arena *a,
    const uint8_t *expr,
    size_t len,
    size_t *off,
    bool *ok
) {
    if (!expr || !off || !ok || !*ok || *off >= len) {
        if (ok) *ok = false;
        return NULL;
    }

    uint8_t tag = expr[(*off)++];
    switch (tag) {
    case IMPORTED_MORK_WIDE_TAG_NEWVAR:
        *ok = false;
        return NULL;
    case IMPORTED_MORK_WIDE_TAG_VARREF:
        if (*off >= len) {
            *ok = false;
            return NULL;
        }
        (*off)++;
        *ok = false;
        return NULL;
    case IMPORTED_MORK_WIDE_TAG_SYMBOL: {
        uint32_t sym_len = 0;
        if (!imported_bridge_read_u32(expr, len, off, &sym_len) ||
            sym_len == 0 || *off + sym_len > len) {
            *ok = false;
            return NULL;
        }
        Atom *atom = imported_bridge_parse_token_bytes(
            a, expr + *off, sym_len, true, ok);
        if (!*ok)
            return NULL;
        *off += sym_len;
        return atom;
    }
    case IMPORTED_MORK_WIDE_TAG_ARITY: {
        uint32_t arity = 0;
        if (!imported_bridge_read_u32(expr, len, off, &arity)) {
            *ok = false;
            return NULL;
        }
        Atom **elems = arity ? arena_alloc(a, sizeof(Atom *) * arity) : NULL;
        for (uint32_t i = 0; i < arity; i++) {
            elems[i] = imported_bridge_parse_value_wide_query_only_v2_rec(
                a, expr, len, off, ok);
            if (!*ok)
                return NULL;
        }
        return atom_expr(a, elems, arity);
    }
    default:
        *ok = false;
        return NULL;
    }
}

static void imported_bridge_value_varmap_init(ImportedBridgeValueVarMap *map) {
    if (!map)
        return;
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
    map->spelling_nonce = fresh_var_suffix();
}

static void imported_bridge_value_varmap_free(ImportedBridgeValueVarMap *map) {
    if (!map)
        return;
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static ImportedBridgeValueVar *
imported_bridge_value_varmap_get_or_add(ImportedBridgeValueVarMap *map,
                                        uint8_t value_env,
                                        uint8_t value_index) {
    if (!map)
        return NULL;
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].value_env == value_env &&
            map->items[i].value_index == value_index)
            return &map->items[i];
    }
    if (map->len >= map->cap) {
        map->cap = map->cap ? map->cap * 2 : 8;
        map->items = cetta_realloc(map->items,
                                   sizeof(ImportedBridgeValueVar) * map->cap);
    }
    char name[64];
    snprintf(name, sizeof(name), "$__mork_b%u_%u#%u", (unsigned)value_env,
             (unsigned)value_index, (unsigned)map->spelling_nonce);
    map->items[map->len] = (ImportedBridgeValueVar){
        .value_env = value_env,
        .value_index = value_index,
        .var_id = fresh_var_id(),
        .spelling = symbol_intern_cstr(g_symbols, name),
    };
    return &map->items[map->len++];
}

static Atom *imported_bridge_parse_value_raw_multi_ref_v3_rec(
    Arena *a,
    const uint8_t *expr,
    size_t len,
    size_t *off,
    uint8_t value_env,
    ImportedBridgeValueVarMap *vars,
    uint8_t *introduced_vars,
    bool *ok
) {
    if (!expr || !off || !ok || !*ok || *off >= len) {
        if (ok) *ok = false;
        return NULL;
    }

    uint8_t tag = expr[*off];
    if (tag == IMPORTED_MORK_TAG_NEWVAR) {
        ImportedBridgeValueVar *slot = imported_bridge_value_varmap_get_or_add(
            vars, value_env, *introduced_vars);
        (*introduced_vars)++;
        (*off)++;
        if (!slot) {
            *ok = false;
            return NULL;
        }
        return atom_var_with_spelling(a, slot->spelling, slot->var_id);
    }
    if ((tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_VARREF_PREFIX) {
        ImportedBridgeValueVar *slot = imported_bridge_value_varmap_get_or_add(
            vars, value_env, (uint8_t)(tag & 0x3Fu));
        (*off)++;
        if (!slot) {
            *ok = false;
            return NULL;
        }
        return atom_var_with_spelling(a, slot->spelling, slot->var_id);
    }

    if ((tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_SYMBOL_PREFIX) {
        uint32_t sym_len = (uint32_t)(tag & 0x3Fu);
        if (sym_len == 0 || *off + 1u + sym_len > len) {
            *ok = false;
            return NULL;
        }
        Atom *atom = imported_bridge_parse_token_bytes(
            a, expr + *off + 1u, sym_len, false, ok);
        if (!*ok)
            return NULL;
        *off += 1u + sym_len;
        return atom;
    }

    uint32_t arity = (uint32_t)(tag & 0x3Fu);
    (*off)++;
    Atom **elems = arity ? arena_alloc(a, sizeof(Atom *) * arity) : NULL;
    for (uint32_t i = 0; i < arity; i++) {
        elems[i] = imported_bridge_parse_value_raw_multi_ref_v3_rec(
            a, expr, len, off, value_env, vars, introduced_vars, ok);
        if (!*ok)
            return NULL;
    }
    return atom_expr(a, elems, arity);
}

static Atom *imported_bridge_parse_value_wide_multi_ref_v3_rec(
    Arena *a,
    const uint8_t *expr,
    size_t len,
    size_t *off,
    uint8_t value_env,
    ImportedBridgeValueVarMap *vars,
    uint8_t *introduced_vars,
    bool *ok
) {
    if (!expr || !off || !ok || !*ok || *off >= len) {
        if (ok) *ok = false;
        return NULL;
    }

    uint8_t tag = expr[(*off)++];
    switch (tag) {
    case IMPORTED_MORK_WIDE_TAG_NEWVAR: {
        ImportedBridgeValueVar *slot = imported_bridge_value_varmap_get_or_add(
            vars, value_env, *introduced_vars);
        (*introduced_vars)++;
        if (!slot) {
            *ok = false;
            return NULL;
        }
        return atom_var_with_spelling(a, slot->spelling, slot->var_id);
    }
    case IMPORTED_MORK_WIDE_TAG_VARREF: {
        if (*off >= len) {
            *ok = false;
            return NULL;
        }
        ImportedBridgeValueVar *slot = imported_bridge_value_varmap_get_or_add(
            vars, value_env, expr[(*off)++]);
        if (!slot) {
            *ok = false;
            return NULL;
        }
        return atom_var_with_spelling(a, slot->spelling, slot->var_id);
    }
    case IMPORTED_MORK_WIDE_TAG_SYMBOL: {
        uint32_t sym_len = 0;
        if (!imported_bridge_read_u32(expr, len, off, &sym_len) ||
            sym_len == 0 || *off + sym_len > len) {
            *ok = false;
            return NULL;
        }
        Atom *atom = imported_bridge_parse_token_bytes(
            a, expr + *off, sym_len, true, ok);
        if (!*ok)
            return NULL;
        *off += sym_len;
        return atom;
    }
    case IMPORTED_MORK_WIDE_TAG_ARITY: {
        uint32_t arity = 0;
        if (!imported_bridge_read_u32(expr, len, off, &arity)) {
            *ok = false;
            return NULL;
        }
        Atom **elems = arity ? arena_alloc(a, sizeof(Atom *) * arity) : NULL;
        for (uint32_t i = 0; i < arity; i++) {
            elems[i] = imported_bridge_parse_value_wide_multi_ref_v3_rec(
                a, expr, len, off, value_env, vars, introduced_vars, ok);
            if (!*ok)
                return NULL;
        }
        return atom_expr(a, elems, arity);
    }
    default:
        *ok = false;
        return NULL;
    }
}

/* Query-only v2 currently omits ExprEnv.v for query-env subexpressions, so CeTTa
   only consumes raw values that are structurally ground. Anything with vars
   falls back to CeTTa-native rematch for semantic safety. value_env is treated
   as provenance only here: multi-factor bridge packets may legitimately source
   a ground value from factor env 2, 3, ... without changing the decoded atom. */
static Atom *imported_bridge_parse_value_raw_query_only_v2(
    Arena *a,
    const uint8_t *expr,
    uint32_t expr_len,
    uint8_t value_env,
    uint8_t value_flags,
    bool wide_tokens,
    bool *ok
) {
    if (!ok || !*ok || !expr || expr_len == 0) {
        if (ok) *ok = false;
        return NULL;
    }
    if (!(value_flags & 0x01u)) {
        *ok = false;
        return NULL;
    }
    (void)value_env;

    size_t off = 0;
    Atom *value = wide_tokens
        ? imported_bridge_parse_value_wide_query_only_v2_rec(
              a, expr, expr_len, &off, ok)
        : imported_bridge_parse_value_raw_query_only_v2_rec(
              a, expr, expr_len, &off, ok);
    if (!*ok || off != expr_len) {
        *ok = false;
        return NULL;
    }
    return value;
}

static Atom *imported_bridge_parse_value_raw_multi_ref_v3(
    Arena *a,
    const uint8_t *expr,
    uint32_t expr_len,
    uint8_t value_env,
    uint8_t value_flags,
    bool wide_tokens,
    ImportedBridgeValueVarMap *vars,
    bool *ok
) {
    size_t off = 0;
    uint8_t introduced_vars = 0;
    if (!ok || !*ok || !expr || expr_len == 0 || !vars) {
        if (ok) *ok = false;
        return NULL;
    }
    (void)value_flags;
    Atom *value = wide_tokens
        ? imported_bridge_parse_value_wide_multi_ref_v3_rec(
              a, expr, expr_len, &off, value_env, vars, &introduced_vars, ok)
        : imported_bridge_parse_value_raw_multi_ref_v3_rec(
              a, expr, expr_len, &off, value_env, vars, &introduced_vars, ok);
    if (!*ok || off != expr_len) {
        *ok = false;
        return NULL;
    }
    return value;
}

static char *imported_bridge_build_conjunction_text(Arena *a, Atom **patterns,
                                                    uint32_t npatterns) {
    if (npatterns == 0)
        return NULL;
    char **parts = arena_alloc(a, sizeof(char *) * npatterns);
    size_t total = 3;
    for (uint32_t i = 0; i < npatterns; i++) {
        parts[i] = atom_to_parseable_string(a, patterns[i]);
        total += 1 + strlen(parts[i]);
    }
    char *buf = arena_alloc(a, total + 1);
    size_t off = 0;
    buf[off++] = '(';
    buf[off++] = ',';
    for (uint32_t i = 0; i < npatterns; i++) {
        size_t len = strlen(parts[i]);
        buf[off++] = ' ';
        memcpy(buf + off, parts[i], len);
        off += len;
    }
    buf[off++] = ')';
    buf[off] = '\0';
    return buf;
}

static ImportedCorefRef *imported_find_query_ref(ImportedCorefState *refs,
                                                 VarId var_id) {
    for (uint32_t i = 0; i < refs->nquery; i++)
        if (refs->query[i].var_id == var_id)
            return &refs->query[i];
    return NULL;
}

static ImportedCorefRef *imported_find_indexed_ref(ImportedCorefState *refs,
                                                   VarId var_id) {
    for (uint32_t i = 0; i < refs->nindexed; i++)
        if (refs->indexed[i].var_id == var_id)
            return &refs->indexed[i];
    return NULL;
}

static ImportedCorefRef *imported_find_indexed_value(ImportedCorefState *refs,
                                                     VarId var_id) {
    for (uint32_t i = 0; i < refs->nindexed_value; i++)
        if (refs->indexed_value[i].var_id == var_id)
            return &refs->indexed_value[i];
    return NULL;
}

static ImportedCorefVerdict imported_add_query_ref(ImportedCorefState *refs,
                                                   VarId var_id,
                                                   SymbolId spelling,
                                                   uint32_t idx,
                                                   uint32_t span,
                                                   Atom *origin) {
    if (refs->nquery >= IMPORTED_COREF_LIMIT)
        return IMPORTED_COREF_NEEDS_FALLBACK;
    refs->query[refs->nquery++] = (ImportedCorefRef){
        .var_id = var_id, .spelling = spelling, .idx = idx, .span = span,
        .origin = origin,
    };
    return IMPORTED_COREF_EXACT;
}

static ImportedCorefVerdict imported_add_indexed_ref(ImportedCorefState *refs,
                                                     VarId var_id,
                                                     SymbolId spelling,
                                                     uint32_t idx,
                                                     uint32_t span,
                                                     Atom *origin) {
    if (refs->nindexed >= IMPORTED_COREF_LIMIT)
        return IMPORTED_COREF_NEEDS_FALLBACK;
    refs->indexed[refs->nindexed++] = (ImportedCorefRef){
        .var_id = var_id, .spelling = spelling, .idx = idx, .span = span,
        .origin = origin,
    };
    return IMPORTED_COREF_EXACT;
}

static ImportedCorefVerdict imported_add_indexed_value(ImportedCorefState *refs,
                                                       VarId var_id,
                                                       SymbolId spelling,
                                                       uint32_t idx,
                                                       uint32_t span,
                                                       Atom *origin) {
    ImportedCorefRef *existing = imported_find_indexed_value(refs, var_id);
    if (existing) {
        existing->idx = idx;
        existing->span = span;
        existing->origin = origin;
        return IMPORTED_COREF_EXACT;
    }
    if (refs->nindexed_value >= IMPORTED_COREF_LIMIT)
        return IMPORTED_COREF_NEEDS_FALLBACK;
    refs->indexed_value[refs->nindexed_value++] = (ImportedCorefRef){
        .var_id = var_id, .spelling = spelling, .idx = idx, .span = span,
        .origin = origin,
    };
    return IMPORTED_COREF_EXACT;
}

static bool imported_subtree_contains_var(const ImportedFlatToken *tokens,
                                          uint32_t idx,
                                          VarId var_id) {
    uint32_t end = idx + tokens[idx].span;
    for (uint32_t i = idx; i < end; i++) {
        if (tokens[i].kind == IMPORTED_FLAT_VAR &&
            tokens[i].var_id == var_id)
            return true;
    }
    return false;
}

static ImportedCorefVerdict imported_bind_indexed_value(ImportedCorefState *refs,
                                                        const ImportedFlatToken *tokens,
                                                        uint32_t var_idx,
                                                        uint32_t value_idx) {
    const ImportedFlatToken *vt = &tokens[var_idx];
    const ImportedFlatToken *val = &tokens[value_idx];
    ImportedCorefRef *existing = imported_find_indexed_value(refs, vt->var_id);
    if (existing) {
        if (imported_flat_equal(tokens, existing->idx, tokens, value_idx))
            return IMPORTED_COREF_EXACT;
        return IMPORTED_COREF_NEEDS_FALLBACK;
    }
    if (val->kind == IMPORTED_FLAT_VAR) {
        ImportedCorefRef *other = imported_find_indexed_value(refs, val->var_id);
        if (other)
            return imported_bind_indexed_value(refs, tokens, var_idx, other->idx);
    }
    if (imported_subtree_contains_var(tokens, value_idx, vt->var_id))
        return IMPORTED_COREF_NEEDS_FALLBACK;
    return imported_add_indexed_value(refs, vt->var_id, vt->sym_id,
                                      value_idx, val->span, val->origin);
}

static bool imported_materialize_bindings(const ImportedCorefState *refs,
                                          const ImportedFlatToken *qtokens,
                                          const ImportedFlatToken *ctokens,
                                          const TermUniverse *candidate_universe,
                                          uint32_t epoch, Arena *a,
                                          Bindings *out) {
    bindings_init(out);
    for (uint32_t i = 0; i < refs->nquery; i++) {
        Atom *val = imported_token_copy_epoch(
            a, &ctokens[refs->query[i].idx], candidate_universe, epoch);
        if (!bindings_add_id(out, refs->query[i].var_id, refs->query[i].spelling, val)) {
            bindings_free(out);
            return false;
        }
    }
    for (uint32_t i = 0; i < refs->nindexed; i++) {
        if (!bindings_add_id(out,
                             var_epoch_id(refs->indexed[i].var_id, epoch),
                             refs->indexed[i].spelling,
                             qtokens[refs->indexed[i].idx].origin)) {
            bindings_free(out);
            return false;
        }
    }
    for (uint32_t i = 0; i < refs->nindexed_value; i++) {
        Atom *val = imported_token_copy_epoch(
            a, &ctokens[refs->indexed_value[i].idx], candidate_universe, epoch);
        if (!bindings_add_id(out,
                             var_epoch_id(refs->indexed_value[i].var_id, epoch),
                             refs->indexed_value[i].spelling, val)) {
            bindings_free(out);
            return false;
        }
    }
    return true;
}

static void imported_bucket_push_builder(ImportedFlatBucket *bucket,
                                         ImportedFlatBuilder *builder,
                                         uint32_t atom_idx, uint32_t epoch) {
    if (!bucket || !builder)
        return;
    if (bucket->len >= bucket->cap) {
        bucket->cap = bucket->cap ? bucket->cap * 2 : 8;
        bucket->entries = cetta_realloc(bucket->entries,
                                        sizeof(ImportedFlatEntry) * bucket->cap);
    }
    bucket->entries[bucket->len].atom_idx = atom_idx;
    bucket->entries[bucket->len].epoch = epoch;
    bucket->entries[bucket->len].tokens = builder->items;
    bucket->entries[bucket->len].len = builder->len;
    bucket->len++;
}

static void imported_bucket_add_entry(ImportedFlatBucket *bucket, Atom *atom,
                                      uint32_t atom_idx, uint32_t epoch) {
    ImportedFlatBuilder b = {0};
    imported_flatten_atom(&b, atom);
    imported_bucket_push_builder(bucket, &b, atom_idx, epoch);
}

static ImportedFlatBucket *imported_bucket_for_atom(ImportedBridgeState *st, Atom *atom) {
    SymbolId head = atom_head_sym(atom);
    return head != SYMBOL_ID_NONE ? &st->buckets[stree_head_hash(head)] : &st->wildcard;
}

static ImportedFlatBucket *imported_bucket_for_atom_id(ImportedBridgeState *st,
                                                       const TermUniverse *universe,
                                                       AtomId atom_id) {
    SymbolId head = tu_head_sym(universe, atom_id);
    return head != SYMBOL_ID_NONE ? &st->buckets[stree_head_hash(head)] : &st->wildcard;
}

static void imported_rebuild_flat(Space *s) {
    ImportedBridgeState *st = backend_bridge_state(s);
    const AtomId *source_ids = NULL;
    uint32_t source_len = 0;
    bool source_from_projection = false;
    if (!st)
        return;
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    if (st->bridge_active && st->bridge_space &&
        imported_storage_ensure_projection(s)) {
        source_ids = st->projected_atom_ids;
        source_len = st->projected_len;
        source_from_projection = true;
    } else {
        source_len = s->native.len;
    }
    for (uint32_t i = 0; i < source_len; i++) {
        AtomId atom_id = source_ids ? source_ids[i] : shadow_storage_get_atom_id_at(s, i);
        if (s->native.universe && tu_hdr(s->native.universe, atom_id)) {
            ImportedFlatBuilder b = {0};
            if (imported_flatten_atom_id(&b, s->native.universe, atom_id)) {
                ImportedFlatBucket *bucket =
                    imported_bucket_for_atom_id(st, s->native.universe, atom_id);
                imported_bucket_push_builder(bucket, &b, i, stree_next_epoch());
                continue;
            }
            free(b.items);
        }
        Atom *atom = source_ids
            ? term_universe_get_atom(s->native.universe, atom_id)
            : shadow_storage_get_at(s, i);
        ImportedFlatBucket *bucket = imported_bucket_for_atom(st, atom);
        imported_bucket_add_entry(bucket, atom, i, stree_next_epoch());
    }
    if (source_from_projection && !imported_shadow_refresh_from_projection(s)) {
        imported_flat_state_clear(st);
        st->built = false;
        st->dirty = true;
        return;
    }
    st->bridge_active = false;
    if (s->match_backend.kind == SPACE_ENGINE_MORK) {
        s->match_backend.mork.attached_compiled = false;
        s->match_backend.mork.attached_count = 0;
    }
    st->built = true;
    st->dirty = false;
}

static bool pathmap_local_ensure_bridge_space(PathmapLocalState *st) {
    if (st->bridge.bridge_space)
        return true;
    st->bridge.bridge_space = cetta_mork_bridge_space_new_pathmap();
    return st->bridge.bridge_space != NULL;
}

static bool mork_imported_ensure_bridge_space(MorkImportedState *st) {
    if (st->bridge.bridge_space)
        return true;
    st->bridge.bridge_space = cetta_mork_bridge_space_new();
    return st->bridge.bridge_space != NULL;
}

static bool backend_rebuild_bridge(Space *s) {
    ImportedBridgeState *st = backend_bridge_state(s);
    if (!s->native.universe)
        return false;
    if (!st)
        return false;
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        if (st->bridge_unavailable)
            return false;
        if (!pathmap_local_ensure_bridge_space(&s->match_backend.pathmap))
            return false;
    } else if (s->match_backend.kind == SPACE_ENGINE_MORK) {
        if (!mork_imported_ensure_bridge_space(&s->match_backend.mork))
            return false;
    } else {
        return false;
    }
    if (!cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space))
        return false;

    imported_projection_clear(st);
    for (uint32_t i = 0; i < s->native.len; i++) {
        Arena scratch;
        arena_init(&scratch);
        AtomId atom_id = shadow_storage_get_atom_id_at(s, i);
        bool ok = imported_bridge_add_atom_structural(
            &scratch, (CettaMorkSpaceHandle *)st->bridge_space, s->native.universe,
            atom_id, NULL);
        arena_free(&scratch);
        if (!ok) {
            cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space);
            st->bridge_active = false;
            if (s->match_backend.kind == SPACE_ENGINE_PATHMAP)
                st->bridge_unavailable = true;
            return false;
        }
    }

    imported_flat_state_clear(st);
    st->bridge_active = true;
    st->bridge_unavailable = false;
    if (s->match_backend.kind == SPACE_ENGINE_MORK) {
        s->match_backend.mork.attached_compiled = false;
        s->match_backend.mork.attached_count = 0;
    }
    st->built = true;
    st->dirty = false;
    return true;
}

static void imported_rebuild(Space *s) {
    if (backend_uses_bridge_adapter(s) && backend_rebuild_bridge(s))
        return;
    imported_rebuild_flat(s);
}

static void imported_ensure_built_flat(Space *s) {
    ImportedBridgeState *st = backend_bridge_state(s);
    if (!st)
        return;
    if (!st->built || st->dirty || st->bridge_active ||
        (backend_uses_bridge_adapter(s) && s->match_backend.mork.attached_compiled))
        imported_rebuild_flat(s);
}

static bool imported_projection_capture_from_bridge(Space *s,
                                                    ImportedBridgeState *st) {
    Arena scratch;
    ImportedBridgeExprMemo memo;
    bool ok;
    AtomId *snapshot = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t rows = 0;

    if (!s || !st || !st->bridge_space || !s->native.universe)
        return false;

    arena_init(&scratch);
    imported_bridge_expr_memo_init(&memo);
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        ok = cetta_mork_bridge_space_dump_contextual_exact_rows(
            (CettaMorkSpaceHandle *)st->bridge_space, &packet, &packet_len, &rows);
        if (ok) {
            ok = imported_bridge_decode_contextual_exact_rows(
                s->native.universe, &scratch, &memo, packet, packet_len,
                &snapshot, &rows);
        }
        cetta_mork_bridge_bytes_free(packet, packet_len);

        if (!ok) {
            packet = NULL;
            packet_len = 0;
            rows = 0;
            ok = cetta_mork_bridge_space_dump_expr_rows(
                (CettaMorkSpaceHandle *)st->bridge_space, &packet, &packet_len, &rows);
            if (ok && rows > 0) {
                snapshot = cetta_malloc(sizeof(AtomId) * rows);
                size_t off = 0;
                for (uint32_t i = 0; i < rows; i++) {
                    ArenaMark scratch_mark = arena_mark(&scratch);
                    AtomId atom_id = CETTA_ATOM_ID_NONE;
                    if (off + 4u > packet_len) {
                        ok = false;
                        arena_reset(&scratch, scratch_mark);
                        break;
                    }
                    uint32_t expr_len =
                        ((uint32_t)packet[off] << 24) |
                        ((uint32_t)packet[off + 1u] << 16) |
                        ((uint32_t)packet[off + 2u] << 8) |
                        (uint32_t)packet[off + 3u];
                    off += 4u;
                    if (off + expr_len > packet_len) {
                        ok = false;
                        arena_reset(&scratch, scratch_mark);
                        break;
                    }
                    ImportedBridgeExprDecodeResult result =
                        imported_bridge_packet_expr_to_atom_id_cached(
                            s->native.universe, &scratch, &memo, packet + off, expr_len, &atom_id);
                    if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK ||
                        atom_id == CETTA_ATOM_ID_NONE) {
                        ok = false;
                        arena_reset(&scratch, scratch_mark);
                        break;
                    }
                    snapshot[i] = atom_id;
                    off += expr_len;
                    arena_reset(&scratch, scratch_mark);
                }
                if (ok && off != packet_len)
                    ok = false;
            } else if (ok && packet_len != 0) {
                ok = false;
            }
            cetta_mork_bridge_bytes_free(packet, packet_len);
        }
    } else {
        Space staged;
        space_init_with_universe(&staged, s->native.universe);
        ok = imported_materialize_bridge_space(
            &staged, &scratch, (CettaMorkSpaceHandle *)st->bridge_space, NULL);
        if (ok && staged.native.len) {
            snapshot = cetta_malloc(sizeof(AtomId) * staged.native.len);
            memcpy(snapshot, staged.native.atom_ids,
                   sizeof(AtomId) * staged.native.len);
        }
        rows = staged.native.len;
        space_free(&staged);
    }
    imported_bridge_expr_memo_free(&memo);
    if (!ok) {
        free(snapshot);
        arena_free(&scratch);
        return false;
    }
    imported_projection_clear(st);
    st->projected_atom_ids = snapshot;
    st->projected_len = rows;
    st->projection_valid = true;

    arena_free(&scratch);
    return true;
}

static bool imported_storage_ensure_projection(Space *s) {
    ImportedBridgeState *st = backend_bridge_state(s);
    MorkImportedState *mst = mork_imported_state(s);

    if (!st)
        return false;
    if (st->projection_valid)
        return true;

    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        if (st->bridge_unavailable)
            return false;
        if ((!st->bridge_active || !st->bridge_space) &&
            !backend_rebuild_bridge(s))
            return false;
    } else if (s->match_backend.kind == SPACE_ENGINE_MORK) {
        if (!(mst && mst->attached_compiled) &&
            (!st->bridge_active || !st->bridge_space) &&
            !backend_rebuild_bridge(s))
            return false;
    } else {
        return false;
    }

    if (!st->bridge_space)
        return false;
    return imported_projection_capture_from_bridge(s, st);
}

static bool imported_shadow_refresh_from_projection(Space *s) {
    ImportedBridgeState *st = backend_bridge_state(s);
    AtomId *next_ids = NULL;

    if (!s || !st)
        return false;
    if (!imported_storage_ensure_projection(s))
        return false;
    if (st->projected_len > 0) {
        next_ids = cetta_malloc(sizeof(AtomId) * st->projected_len);
        memcpy(next_ids, st->projected_atom_ids,
               sizeof(AtomId) * st->projected_len);
    }
    free(s->native.atom_ids);
    s->native.atom_ids = next_ids;
    s->native.start = 0;
    s->native.len = st->projected_len;
    s->native.cap = st->projected_len;
    s->native.eq_idx_dirty = true;
    s->native.ty_idx_dirty = true;
    s->native.exact_idx_dirty = true;
    s->native.non_exact_atoms_dirty = true;
    return true;
}

bool space_match_backend_step(Space *s, Arena *persistent_arena,
                              uint64_t steps, uint64_t *out_performed) {
    MorkImportedState *mst;
    ImportedBridgeState *st;
    Space *fresh = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t packet_rows = 0;
    uint64_t performed = 0;
    bool ok = false;

    if (out_performed)
        *out_performed = 0;
    if (!s || !space_engine_supports_exec(s->match_backend.kind))
        return false;
    if (space_is_ordered(s))
        return false;
    if (steps == 0) {
        if (out_performed)
            *out_performed = 0;
        return true;
    }
    if (!space_match_backend_materialize_native_storage(s, persistent_arena))
        return false;

    mst = mork_imported_state(s);
    st = mst ? &mst->bridge : NULL;
    if (!st)
        return false;
    if (!st->bridge_active && !backend_rebuild_bridge(s))
        return false;
    if (!st->bridge_active || !st->bridge_space)
        return false;
    if (!cetta_mork_bridge_space_step((CettaMorkSpaceHandle *)st->bridge_space,
                                      steps, &performed)) {
        st->bridge_active = false;
        st->built = false;
        st->dirty = true;
        return false;
    }
    if (out_performed)
        *out_performed = performed;
    if (performed == 0)
        return true;
    fresh = cetta_malloc(sizeof(Space));
    space_init_with_universe(fresh, s ? s->native.universe : NULL);
    fresh->kind = s->kind;
    if (!space_match_backend_try_set(fresh, s->match_backend.kind)) {
        goto done;
    }
    ok = imported_materialize_bridge_space(
        fresh,
        persistent_arena,
        (CettaMorkSpaceHandle *)st->bridge_space,
        NULL);
    if (!ok)
        goto done;

    space_replace_contents(s, fresh);
    ok = true;

done:
    if (fresh) {
        if (ok) {
            free(fresh);
        } else {
            space_free(fresh);
            free(fresh);
        }
    }
    (void)packet;
    (void)packet_len;
    (void)packet_rows;
    return ok;
}

bool space_match_backend_load_sexpr_chunk(Space *s, Arena *persistent_arena,
                                          const uint8_t *text, size_t len,
                                          uint64_t *out_added) {
    MorkImportedState *mst;
    ImportedBridgeState *st;
    Space *fresh = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t packet_rows = 0;
    uint64_t added = 0;
    bool ok = false;

    if (out_added)
        *out_added = 0;
    if (!s || !space_engine_uses_pathmap(s->match_backend.kind))
        return false;
    if (space_is_ordered(s))
        return false;
    if (!text && len != 0)
        return false;
    if (len == 0) {
        if (out_added)
            *out_added = 0;
        return true;
    }
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        if (pathmap_local_apply_text_chunk_direct(s, text, len, false, out_added))
            return true;
        return imported_parse_text_atoms_into_space(s, persistent_arena, text, len,
                                                    false, out_added);
    }
    if (!space_match_backend_materialize_native_storage(s, persistent_arena))
        return false;

    mst = mork_imported_state(s);
    st = mst ? &mst->bridge : NULL;
    if (!st)
        return false;
    if (!st->bridge_active && !backend_rebuild_bridge(s))
        return false;
    if (!st->bridge_active || !st->bridge_space)
        return false;
    if (!cetta_mork_bridge_space_add_sexpr((CettaMorkSpaceHandle *)st->bridge_space,
                                           text, len, &added)) {
        st->bridge_active = false;
        st->built = false;
        st->dirty = true;
        return false;
    }
    if (out_added)
        *out_added = added;
    if (added == 0)
        return true;
    fresh = cetta_malloc(sizeof(Space));
    space_init_with_universe(fresh, s ? s->native.universe : NULL);
    fresh->kind = s->kind;
    if (!space_match_backend_try_set(fresh, s->match_backend.kind)) {
        goto done;
    }
    ok = imported_materialize_bridge_space(
        fresh,
        persistent_arena,
        (CettaMorkSpaceHandle *)st->bridge_space,
        NULL);
    if (!ok)
        goto done;

    space_replace_contents(s, fresh);
    ok = true;

done:
    if (fresh) {
        if (ok) {
            free(fresh);
        } else {
            space_free(fresh);
            free(fresh);
        }
    }
    (void)packet;
    (void)packet_len;
    (void)packet_rows;
    return ok;
}

bool space_match_backend_remove_sexpr_chunk(Space *s, Arena *persistent_arena,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed) {
    MorkImportedState *mst;
    ImportedBridgeState *st;
    Space *fresh = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t packet_rows = 0;
    uint64_t removed = 0;
    bool ok = false;

    if (out_removed)
        *out_removed = 0;
    if (!s || !space_engine_uses_pathmap(s->match_backend.kind))
        return false;
    if (space_is_ordered(s))
        return false;
    if (!text && len != 0)
        return false;
    if (len == 0) {
        if (out_removed)
            *out_removed = 0;
        return true;
    }
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        if (pathmap_local_apply_text_chunk_direct(s, text, len, true, out_removed))
            return true;
        return imported_parse_text_atoms_into_space(s, persistent_arena, text, len,
                                                    true, out_removed);
    }
    if (!space_match_backend_materialize_native_storage(s, persistent_arena))
        return false;

    mst = mork_imported_state(s);
    st = mst ? &mst->bridge : NULL;
    if (!st)
        return false;
    if (!st->bridge_active && !backend_rebuild_bridge(s))
        return false;
    if (!st->bridge_active || !st->bridge_space)
        return false;
    if (!cetta_mork_bridge_space_remove_sexpr((CettaMorkSpaceHandle *)st->bridge_space,
                                              text, len, &removed)) {
        st->bridge_active = false;
        st->built = false;
        st->dirty = true;
        return false;
    }
    if (out_removed)
        *out_removed = removed;
    if (removed == 0)
        return true;
    fresh = cetta_malloc(sizeof(Space));
    space_init_with_universe(fresh, s ? s->native.universe : NULL);
    fresh->kind = s->kind;
    if (!space_match_backend_try_set(fresh, s->match_backend.kind)) {
        goto done;
    }
    ok = imported_materialize_bridge_space(
        fresh,
        persistent_arena,
        (CettaMorkSpaceHandle *)st->bridge_space,
        NULL);
    if (!ok)
        goto done;

    space_replace_contents(s, fresh);
    ok = true;

done:
    if (fresh) {
        if (ok) {
            free(fresh);
        } else {
            space_free(fresh);
            free(fresh);
        }
    }
    (void)packet;
    (void)packet_len;
    (void)packet_rows;
    return ok;
}

static void imported_ensure_built(Space *s) {
    ImportedBridgeState *st = backend_bridge_state(s);
    if (!st)
        return;
    if (!st->built || st->dirty ||
        (backend_uses_bridge_adapter(s) && s->match_backend.mork.attached_compiled))
        imported_rebuild(s);
}

static ImportedCorefVerdict imported_match_subtree_coref(const ImportedFlatToken *q,
                                                         uint32_t qi,
                                                         const ImportedFlatToken *c,
                                                         uint32_t ci,
                                                         ImportedCorefState *refs,
                                                         uint32_t *qnext,
                                                         uint32_t *cnext,
                                                         int depth) {
    if (depth <= 0) return false;
    const ImportedFlatToken *qt = &q[qi];
    const ImportedFlatToken *ct = &c[ci];

    if (qt->kind == IMPORTED_FLAT_VAR) {
        if (ct->kind == IMPORTED_FLAT_VAR &&
            (imported_find_indexed_ref(refs, ct->var_id) ||
             imported_find_indexed_value(refs, ct->var_id))) {
            return IMPORTED_COREF_NEEDS_FALLBACK;
        }
        ImportedCorefRef *existing = imported_find_query_ref(refs, qt->var_id);
        if (existing) {
            if (!imported_flat_equal(c, existing->idx, c, ci)) {
                if (c[existing->idx].kind == IMPORTED_FLAT_VAR) {
                    ImportedCorefVerdict bind =
                        imported_bind_indexed_value(refs, c, existing->idx, ci);
                    if (bind != IMPORTED_COREF_EXACT)
                        return bind;
                } else if (ct->kind == IMPORTED_FLAT_VAR) {
                    ImportedCorefVerdict bind =
                        imported_bind_indexed_value(refs, c, ci, existing->idx);
                    if (bind != IMPORTED_COREF_EXACT)
                        return bind;
                } else {
                    return IMPORTED_COREF_NEEDS_FALLBACK;
                }
            }
        } else {
            ImportedCorefVerdict add =
                imported_add_query_ref(refs, qt->var_id, qt->sym_id,
                                       ci, ct->span, ct->origin);
            if (add != IMPORTED_COREF_EXACT)
                return add;
        }
        *qnext = qi + qt->span;
        *cnext = ci + ct->span;
        return IMPORTED_COREF_EXACT;
    }

    if (ct->kind == IMPORTED_FLAT_VAR) {
        ImportedCorefRef *value = imported_find_indexed_value(refs, ct->var_id);
        if (value) {
            if (qt->kind == IMPORTED_FLAT_VAR) {
                ImportedCorefRef *existing_q = imported_find_query_ref(refs, qt->var_id);
                if (existing_q) {
                    if (!imported_flat_equal(c, existing_q->idx, c, value->idx))
                        return IMPORTED_COREF_NEEDS_FALLBACK;
                } else {
                    ImportedCorefVerdict add =
                        imported_add_query_ref(refs, qt->var_id, qt->sym_id, value->idx,
                                               c[value->idx].span,
                                               c[value->idx].origin);
                    if (add != IMPORTED_COREF_EXACT)
                        return add;
                }
                *qnext = qi + qt->span;
                *cnext = ci + ct->span;
                return IMPORTED_COREF_EXACT;
            }
            return IMPORTED_COREF_NEEDS_FALLBACK;
        }
        ImportedCorefRef *existing = imported_find_indexed_ref(refs, ct->var_id);
        if (existing) {
            if (!imported_flat_equal(q, existing->idx, q, qi))
                return IMPORTED_COREF_NEEDS_FALLBACK;
        } else {
            ImportedCorefVerdict add =
                imported_add_indexed_ref(refs, ct->var_id, ct->sym_id,
                                         qi, qt->span, qt->origin);
            if (add != IMPORTED_COREF_EXACT)
                return add;
        }
        *qnext = qi + qt->span;
        *cnext = ci + ct->span;
        return IMPORTED_COREF_EXACT;
    }

    if (qt->kind != ct->kind) return IMPORTED_COREF_FAIL;

    switch (qt->kind) {
    case IMPORTED_FLAT_SYMBOL:
        if (qt->sym_id != ct->sym_id) return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_INT:
        if (qt->ival != ct->ival) return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_FLOAT:
        if (qt->fval != ct->fval) return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_BOOL:
        if (qt->bval != ct->bval) return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_STRING:
        if (qt->sym_id != ct->sym_id) return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_GROUNDED_OTHER:
        if (!ct->origin || !atom_eq(qt->origin, ct->origin))
            return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_EXPR: {
        if (qt->arity != ct->arity) return IMPORTED_COREF_FAIL;
        uint32_t qcur = qi + 1;
        uint32_t ccur = ci + 1;
        for (uint32_t i = 0; i < qt->arity; i++) {
            ImportedCorefVerdict child =
                imported_match_subtree_coref(q, qcur, c, ccur, refs,
                                             &qcur, &ccur, depth - 1);
            if (child != IMPORTED_COREF_EXACT)
                return child;
        }
        *qnext = qcur;
        *cnext = ccur;
        return IMPORTED_COREF_EXACT;
    }
    case IMPORTED_FLAT_VAR:
        return IMPORTED_COREF_FAIL;
    }
    return IMPORTED_COREF_FAIL;
}

static bool imported_match_subtree_legacy(const ImportedFlatToken *q, uint32_t qi,
                                          const ImportedFlatToken *c, uint32_t ci,
                                          const TermUniverse *candidate_universe,
                                          Bindings *b, Arena *a, uint32_t epoch,
                                          uint32_t *qnext, uint32_t *cnext, int depth) {
    if (depth <= 0) return false;
    const ImportedFlatToken *qt = &q[qi];
    const ImportedFlatToken *ct = &c[ci];

    if (qt->kind == IMPORTED_FLAT_VAR) {
        Atom *existing = bindings_lookup_id(b, qt->var_id);
        if (existing) {
            if (ct->origin_id != CETTA_ATOM_ID_NONE) {
                if (!match_atoms_atom_id_epoch(existing, candidate_universe,
                                               ct->origin_id, b, a, epoch))
                    return false;
            } else if (!match_atoms_epoch(existing,
                                          imported_token_atom(ct, candidate_universe),
                                          b, a, epoch)) {
                return false;
            }
        } else {
            Atom *value =
                imported_token_copy_epoch(a, ct, candidate_universe, epoch);
            if (!bindings_add_id(b, qt->var_id, qt->sym_id,
                                 value))
                return false;
        }
        *qnext = qi + qt->span;
        *cnext = ci + ct->span;
        return true;
    }

    if (ct->kind == IMPORTED_FLAT_VAR) {
        VarId tagged_id = var_epoch_id(ct->var_id, epoch);
        Atom *existing = bindings_lookup_id(b, tagged_id);
        if (existing) {
            if (!match_atoms(qt->origin, existing, b))
                return false;
        } else {
            if (!bindings_add_id(b, tagged_id, ct->sym_id, qt->origin))
                return false;
        }
        *qnext = qi + qt->span;
        *cnext = ci + ct->span;
        return true;
    }

    if (qt->kind != ct->kind) return false;

    switch (qt->kind) {
    case IMPORTED_FLAT_SYMBOL:
        if (qt->sym_id != ct->sym_id) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_INT:
        if (qt->ival != ct->ival) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_FLOAT:
        if (qt->fval != ct->fval) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_BOOL:
        if (qt->bval != ct->bval) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_STRING:
        if (qt->sym_id != ct->sym_id) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_GROUNDED_OTHER:
        if (!atom_eq(qt->origin, imported_token_atom(ct, candidate_universe)))
            return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_EXPR: {
        if (qt->arity != ct->arity) return false;
        uint32_t qcur = qi + 1;
        uint32_t ccur = ci + 1;
        for (uint32_t i = 0; i < qt->arity; i++) {
            if (!imported_match_subtree_legacy(q, qcur, c, ccur,
                                               candidate_universe, b, a, epoch,
                                               &qcur, &ccur, depth - 1))
                return false;
        }
        *qnext = qcur;
        *cnext = ccur;
        return true;
    }
    case IMPORTED_FLAT_VAR:
        return false;
    }
    return false;
}

static void imported_collect_bucket(const ImportedFlatBucket *bucket,
                                    const ImportedFlatToken *qtokens, uint32_t qlen,
                                    const TermUniverse *candidate_universe,
                                    Arena *a, SubstMatchSet *out) {
    for (uint32_t i = 0; i < bucket->len; i++) {
        const ImportedFlatEntry *entry = &bucket->entries[i];
        uint32_t match_epoch = fresh_var_suffix();
        if (entry->len == 0 || qlen == 0) continue;
        uint32_t qnext = 0, cnext = 0;
        ImportedCorefState refs = {0};
        ImportedCorefVerdict verdict =
            imported_match_subtree_coref(qtokens, 0, entry->tokens, 0, &refs,
                                         &qnext, &cnext,
                                         CETTA_MATCH_DEPTH_LIMIT);
        if (verdict == IMPORTED_COREF_EXACT &&
            qnext == qlen && cnext == entry->len) {
            Bindings b;
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_IMPORTED_EXACT);
            if (imported_materialize_bindings(&refs, qtokens, entry->tokens,
                                              candidate_universe, match_epoch, a, &b) &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, entry->atom_idx, match_epoch, &b, true);
                bindings_free(&b);
                continue;
            }
            bindings_free(&b);
            verdict = IMPORTED_COREF_NEEDS_FALLBACK;
        }
        if (verdict == IMPORTED_COREF_NEEDS_FALLBACK) {
            Bindings b;
            bindings_init(&b);
            qnext = 0;
            cnext = 0;
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_IMPORTED_LEGACY);
            if (imported_match_subtree_legacy(qtokens, 0, entry->tokens, 0,
                                              candidate_universe, &b, a,
                                              match_epoch, &qnext, &cnext,
                                              CETTA_MATCH_DEPTH_LIMIT) &&
                qnext == qlen && cnext == entry->len &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, entry->atom_idx, match_epoch, &b, true);
            }
            bindings_free(&b);
        }
    }
}

static __attribute__((unused)) uint32_t
imported_candidates_flat(Space *s, Atom *pattern, uint32_t **out) {
    ImportedBridgeState *st = backend_bridge_state(s);
    *out = NULL;
    if (!st)
        return 0;
    uint32_t len = 0, cap = 0;
    SymbolId head = atom_head_sym(pattern);
    if (head != SYMBOL_ID_NONE) {
        ImportedFlatBucket *bucket = &st->buckets[stree_head_hash(head)];
        for (uint32_t i = 0; i < bucket->len; i++) {
            if (len >= cap) {
                cap = cap ? cap * 2 : 8;
                *out = cetta_realloc(*out, sizeof(uint32_t) * cap);
            }
            (*out)[len++] = bucket->entries[i].atom_idx;
        }
    } else {
        for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++) {
            ImportedFlatBucket *bucket = &st->buckets[bi];
            for (uint32_t i = 0; i < bucket->len; i++) {
                if (len >= cap) {
                    cap = cap ? cap * 2 : 8;
                    *out = cetta_realloc(*out, sizeof(uint32_t) * cap);
                }
                (*out)[len++] = bucket->entries[i].atom_idx;
            }
        }
    }
    for (uint32_t i = 0; i < st->wildcard.len; i++) {
        if (len >= cap) {
            cap = cap ? cap * 2 : 8;
            *out = cetta_realloc(*out, sizeof(uint32_t) * cap);
        }
        (*out)[len++] = st->wildcard.entries[i].atom_idx;
    }
    if (len > 1)
        len = sort_unique_uint32(*out, len);
    return len;
}

static uint32_t imported_candidates(Space *s, Atom *pattern, uint32_t **out) {
    MorkImportedState *mst = mork_imported_state(s);
    if (backend_uses_bridge_adapter(s) && mst && mst->attached_compiled) {
        Arena scratch;
        arena_init(&scratch);
        if (!space_match_backend_materialize_native_storage(
                s, eval_current_persistent_arena() ? eval_current_persistent_arena()
                                                   : &scratch)) {
            arena_free(&scratch);
            *out = NULL;
            return 0;
        }
        arena_free(&scratch);
    }
    if (!backend_uses_bridge_adapter(s)) {
        imported_ensure_built_flat(s);
        return imported_candidates_flat(s, pattern, out);
    }
    imported_ensure_built(s);
    return native_candidates(s, pattern, out);
}

static void imported_query_flat(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    ImportedBridgeState *st = backend_bridge_state(s);
    ImportedFlatBuilder q = {0};
    smset_init(out);
    if (!st)
        return;
    if (imported_logical_len(s) == 0) return;
    imported_ensure_built(s);
    imported_flatten_atom(&q, query);
    SymbolId head = atom_head_sym(query);
    if (head != SYMBOL_ID_NONE) {
        imported_collect_bucket(&st->buckets[stree_head_hash(head)],
                                q.items, q.len, s->native.universe, a, out);
    } else {
        for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++)
            imported_collect_bucket(&st->buckets[bi], q.items, q.len,
                                    s->native.universe, a, out);
    }
    imported_collect_bucket(&st->wildcard, q.items, q.len, s->native.universe, a, out);
    free(q.items);

    subst_matchset_normalize(out);
}

static void imported_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    MorkImportedState *mst = mork_imported_state(s);
    ImportedBridgeState *st = backend_bridge_state(s);
    BindingSet direct_matches;
    if (!backend_uses_bridge_adapter(s)) {
        imported_ensure_built_flat(s);
        if (imported_logical_len(s) == 0) {
            smset_init(out);
            return;
        }
        if (imported_atom_has_epoch_vars(query)) {
            native_candidate_exact_query(s, a, query, out);
            return;
        }
        imported_query_flat(s, a, query, out);
        return;
    }
    imported_ensure_built(s);
    if (imported_logical_len(s) == 0) {
        smset_init(out);
        return;
    }
    if (imported_atom_has_epoch_vars(query)) {
        /* Equation evaluation freshens local vars before re-entering the body.
           Imported flat matching is too brittle on these recursive epoch-tagged
           queries. Use the imported bridge only as a candidate selector, then
           let CeTTa's native matcher do the actual unification. */
        imported_epoch_query_candidates(s, a, query, out);
        return;
    }
    if (!st || !st->bridge_active) {
        imported_query_flat(s, a, query, out);
        return;
    }

    if (space_match_backend_mork_query_bindings_direct(
            (CettaMorkSpaceHandle *)st->bridge_space, a, query, &direct_matches)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V2_HIT);
        if (mst && mst->attached_compiled)
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_QUERY);
        imported_binding_set_to_exact_matches(out, &direct_matches);
        binding_set_free(&direct_matches);
        return;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V2_FALLBACK);
    if (mst && mst->attached_compiled) {
        if (!space_match_backend_materialize_native_storage(
                s, eval_current_persistent_arena() ? eval_current_persistent_arena() : a)) {
            smset_init(out);
            return;
        }
        imported_ensure_built(s);
    }
    if (imported_atom_has_epoch_vars(query)) {
        native_candidate_exact_query(s, a, query, out);
        return;
    }
    imported_query_flat(s, a, query, out);
}

static void imported_note_add(Space *s, AtomId atom_id, Atom *atom,
                              uint32_t atom_idx) {
    MorkImportedState *mst = mork_imported_state(s);
    ImportedBridgeState *st = mst ? &mst->bridge : NULL;
    if (!mst || !st)
        return;
    imported_projection_clear(st);
    if (mst->attached_compiled) {
        st->bridge_active = false;
        mst->attached_compiled = false;
        mst->attached_count = 0;
        st->built = false;
        st->dirty = false;
    }
    if (!st->built || st->dirty) return;
    if (st->bridge_active) {
        Arena scratch;
        arena_init(&scratch);
        bool ok = imported_bridge_add_atom_structural(
            &scratch, (CettaMorkSpaceHandle *)st->bridge_space, s->native.universe,
            atom_id, atom);
        arena_free(&scratch);
        if (ok)
            return;
        st->bridge_active = false;
        st->dirty = true;
        return;
    }
    if (tu_hdr(s->native.universe, atom_id)) {
        ImportedFlatBuilder b = {0};
        if (imported_flatten_atom_id(&b, s->native.universe, atom_id)) {
            imported_bucket_push_builder(
                                         imported_bucket_for_atom_id(st, s->native.universe, atom_id),
                                         &b, atom_idx, stree_next_epoch());
            return;
        }
        free(b.items);
    }
    imported_bucket_add_entry(imported_bucket_for_atom(st, atom), atom, atom_idx,
                              stree_next_epoch());
}

static bool native_needs_atom_on_add(const Space *s, AtomId atom_id) {
    const SpaceMatchNativeState *st =
        s ? &s->match_backend.native : NULL;
    if (!st || (st->match_trie == NULL && st->stree == NULL))
        return false;
    return !native_atom_id_insertable(s->native.universe, atom_id);
}

static bool imported_needs_atom_on_add(const Space *s, AtomId atom_id) {
    const ImportedBridgeState *st = backend_bridge_state_const(s);
    const MorkImportedState *mst = mork_imported_state_const(s);
    if (!st)
        return false;
    if (backend_uses_bridge_adapter(s) && mst && mst->attached_compiled)
        return false;
    if (!(st->built && !st->dirty))
        return false;
    return !tu_hdr(s->native.universe, atom_id);
}

static void imported_note_remove(Space *s) {
    MorkImportedState *mst = mork_imported_state(s);
    ImportedBridgeState *st = mst ? &mst->bridge : NULL;
    (void)s;
    if (st && st->built) {
        imported_projection_clear(st);
        st->bridge_active = false;
        mst->attached_compiled = false;
        mst->attached_count = 0;
        st->dirty = true;
    }
}

static bool pathmap_local_ensure_bridge_live(Space *s) {
    ImportedBridgeState *st;
    if (!s)
        return false;
    st = &s->match_backend.pathmap.bridge;
    if (st->bridge_unavailable)
        return false;
    if (st->bridge_active && st->bridge_space)
        return true;
    if (imported_logical_len(s) == 0) {
        if (!pathmap_local_ensure_bridge_space(&s->match_backend.pathmap))
            return false;
        if (!cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space))
            return false;
        imported_projection_clear(st);
        imported_flat_state_clear(st);
        st->bridge_active = true;
        st->bridge_unavailable = false;
        st->built = false;
        st->dirty = false;
        return true;
    }
    return backend_rebuild_bridge(s);
}

static uint32_t pathmap_local_candidates(Space *s, Atom *pattern, uint32_t **out) {
    uint32_t n;
    (void)pattern;
    if (!out) {
        return 0;
    }
    n = imported_logical_len(s);
    *out = cetta_malloc(sizeof(uint32_t) * (n ? n : 1u));
    for (uint32_t i = 0; i < n; i++)
        (*out)[i] = i;
    return n;
}

static void pathmap_local_query(Space *s, Arena *a, Atom *query,
                                SubstMatchSet *out) {
    BindingSet direct_matches;
    ImportedBridgeState *st = &s->match_backend.pathmap.bridge;
    bool epoch_query = imported_atom_has_epoch_vars(query);
    bool bridge_query = imported_atom_has_bridge_vars(query);
    bool var_query = atom_has_vars(query);
    bool indexed_vars = false;
    if (imported_logical_len(s) == 0) {
        smset_init(out);
        return;
    }
    if (var_query && !epoch_query && !bridge_query) {
        Arena *persist = eval_current_persistent_arena();
        if (!space_match_backend_materialize_native_storage(s, persist ? persist : a)) {
            smset_init(out);
            return;
        }
        /* Stored-side variables need CeTTa's bidirectional matcher; the
           bridge/flat packet paths are only parity-safe for exact rows. */
        indexed_vars = !space_contains_only_exact_atoms(s);
    }
    if (var_query && !epoch_query && !bridge_query && indexed_vars) {
        native_candidate_exact_query(s, a, query, out);
        return;
    }
    if (var_query && !epoch_query && !bridge_query &&
        pathmap_local_ensure_bridge_live(s) && st->bridge_space &&
        space_match_backend_mork_query_bindings_direct(
            (CettaMorkSpaceHandle *)st->bridge_space, a, query,
            &direct_matches)) {
        imported_binding_set_to_exact_matches(out, &direct_matches);
        binding_set_free(&direct_matches);
        return;
    }
    if (var_query && !epoch_query && !bridge_query &&
        !st->bridge_active && !st->bridge_unavailable) {
        imported_ensure_built_flat(s);
        imported_query_flat(s, a, query, out);
        return;
    }
    if (epoch_query || bridge_query || var_query) {
        Arena *persist = eval_current_persistent_arena();
        if (!space_match_backend_materialize_native_storage(s, persist ? persist : a)) {
            smset_init(out);
            return;
        }
        native_candidate_exact_query(s, a, query, out);
        return;
    }
    if (pathmap_local_ensure_bridge_live(s) && st->bridge_space &&
        space_match_backend_mork_query_bindings_direct(
            (CettaMorkSpaceHandle *)st->bridge_space, a, query, &direct_matches)) {
        if (direct_matches.len > 0 || space_contains_only_exact_atoms(s)) {
            imported_binding_set_to_exact_matches(out, &direct_matches);
            binding_set_free(&direct_matches);
            return;
        }
        binding_set_free(&direct_matches);
        native_candidate_exact_query(s, a, query, out);
        return;
    }
    imported_ensure_built_flat(s);
    imported_query_flat(s, a, query, out);
}

static void pathmap_local_query_conjunction(Space *s, Arena *a,
                                            Atom **patterns, uint32_t npatterns,
                                            const Bindings *seed, BindingSet *out) {
    /* The bridge conjunction lane is still incomplete for shared variables
       sourced from indexed-side values.  Use the semantic baseline until that
       packet path is proven equivalent. */
    space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
}

static void pathmap_local_backend_primary_bulk_commit(Space *s,
                                                      ImportedBridgeState *st) {
    if (!s || !st)
        return;
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    st->built = false;
    st->dirty = false;
    st->bridge_unavailable = false;
    free(s->native.atom_ids);
    s->native.atom_ids = NULL;
    s->native.start = 0;
    s->native.len = 0;
    s->native.cap = 0;
    s->native.eq_idx_dirty = true;
    s->native.ty_idx_dirty = true;
    s->native.exact_idx_dirty = true;
    s->native.non_exact_atoms = 0;
    s->native.non_exact_atoms_dirty = true;
}

static bool transfer_ensure_bridge_live(Space *s,
                                        CettaMorkSpaceHandle **out_bridge) {
    ImportedBridgeState *st;
    MorkImportedState *mst;

    if (out_bridge)
        *out_bridge = NULL;
    if (!s || space_is_ordered(s) ||
        !space_engine_uses_pathmap(s->match_backend.kind)) {
        return false;
    }

    st = backend_bridge_state(s);
    if (!st)
        return false;

    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        if (!pathmap_local_ensure_bridge_live(s))
            return false;
    } else if (s->match_backend.kind == SPACE_ENGINE_MORK) {
        mst = mork_imported_state(s);
        if (mst && mst->attached_compiled) {
            if (!st->bridge_space)
                return false;
        } else if (!st->bridge_active && !backend_rebuild_bridge(s)) {
            return false;
        }
    }

    if (!st->bridge_space)
        return false;
    if (out_bridge)
        *out_bridge = (CettaMorkSpaceHandle *)st->bridge_space;
    return true;
}

static bool transfer_mark_bridge_destination(Space *dst, uint64_t added) {
    ImportedBridgeState *st = backend_bridge_state(dst);
    MorkImportedState *mst = mork_imported_state(dst);
    uint32_t logical = 0;

    if (!dst || !st)
        return false;
    if (added == 0)
        return true;

    if (dst->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        pathmap_local_backend_primary_bulk_commit(dst, st);
    } else if (dst->match_backend.kind == SPACE_ENGINE_MORK) {
        imported_projection_clear(st);
        imported_flat_state_clear(st);
        st->bridge_active = true;
        st->bridge_unavailable = false;
        st->built = false;
        st->dirty = false;
        logical = bridge_space_logical_len(st);
        if (logical == UINT32_MAX)
            return false;
        if (mst) {
            mst->attached_compiled = true;
            mst->attached_count = logical;
        }
    } else {
        return false;
    }

    dst->revision++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_REVISION_BUMP);
    return true;
}

static bool transfer_space_can_try_bridge_rows(Space *s) {
    ImportedBridgeState *st;

    if (!s || space_is_ordered(s) ||
        !space_engine_uses_pathmap(s->match_backend.kind)) {
        return false;
    }
    st = backend_bridge_state(s);
    return st && !st->bridge_unavailable;
}

static SpaceTransferResult transfer_bridge_logical_rows_direct(Space *dst, Space *src,
                                                               uint64_t *out_added) {
    CettaMorkSpaceHandle *dst_bridge = NULL;
    CettaMorkSpaceHandle *src_bridge = NULL;
    uint64_t added = 0;

    if (!transfer_space_can_try_bridge_rows(dst) ||
        !transfer_space_can_try_bridge_rows(src)) {
        return SPACE_TRANSFER_NEEDS_TEXT_FALLBACK;
    }
    if (!transfer_ensure_bridge_live(dst, &dst_bridge) ||
        !transfer_ensure_bridge_live(src, &src_bridge)) {
        return SPACE_TRANSFER_ERROR;
    }

    if (!cetta_mork_bridge_space_add_logical_rows_from(dst_bridge, src_bridge,
                                                       &added)) {
        return SPACE_TRANSFER_ERROR;
    }
    if (!transfer_mark_bridge_destination(dst, added))
        return SPACE_TRANSFER_ERROR;
    if (out_added)
        *out_added = added;
    return SPACE_TRANSFER_OK;
}

static bool transfer_atom_ids_direct(Space *dst, Space *src,
                                     Arena *persistent_arena,
                                     uint64_t *out_added) {
    uint32_t len;
    uint64_t added = 0;

    if (!dst || !src)
        return false;

    len = space_length(src);
    for (uint32_t i = 0; i < len; i++) {
        AtomId atom_id = space_get_atom_id_at(src, i);
        if (atom_id != CETTA_ATOM_ID_NONE &&
            dst->native.universe == src->native.universe) {
            space_add_atom_id(dst, atom_id);
            added++;
            continue;
        }

        Atom *atom = space_get_at(src, i);
        if (!atom || !space_admit_atom(dst, persistent_arena, atom))
            return false;
        added++;
    }

    if (out_added)
        *out_added = added;
    return true;
}

SpaceTransferResult space_match_backend_transfer_resolved_result(
                                           SpaceTransferEndpoint dst,
                                           SpaceTransferEndpoint src,
                                           Arena *persistent_arena,
                                           uint64_t *out_added) {
    uint64_t added = 0;

    if (out_added)
        *out_added = 0;
    if (dst.kind == SPACE_TRANSFER_ENDPOINT_NONE ||
        src.kind == SPACE_TRANSFER_ENDPOINT_NONE) {
        return SPACE_TRANSFER_ERROR;
    }

    if (dst.kind == SPACE_TRANSFER_ENDPOINT_SPACE &&
        src.kind == SPACE_TRANSFER_ENDPOINT_SPACE) {
        SpaceTransferResult bridge_result;
        if (!dst.space || !src.space)
            return SPACE_TRANSFER_ERROR;
        bridge_result = transfer_bridge_logical_rows_direct(dst.space, src.space,
                                                           out_added);
        if (bridge_result == SPACE_TRANSFER_OK) {
            return SPACE_TRANSFER_OK;
        }
        if (bridge_result == SPACE_TRANSFER_ERROR)
            return SPACE_TRANSFER_ERROR;
        return transfer_atom_ids_direct(dst.space, src.space, persistent_arena,
                                        out_added)
            ? SPACE_TRANSFER_OK
            : SPACE_TRANSFER_ERROR;
    }

    if (dst.kind == SPACE_TRANSFER_ENDPOINT_SPACE &&
        src.kind == SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE) {
        if (!dst.space || !src.bridge)
            return SPACE_TRANSFER_ERROR;
        SpaceBridgeImportResult result =
            space_match_backend_import_bridge_space(dst.space, src.bridge, &added);
        if (result == SPACE_BRIDGE_IMPORT_NEEDS_TEXT_FALLBACK)
            return SPACE_TRANSFER_NEEDS_TEXT_FALLBACK;
        if (result != SPACE_BRIDGE_IMPORT_OK)
            return SPACE_TRANSFER_ERROR;
        if (out_added)
            *out_added = added;
        return SPACE_TRANSFER_OK;
    }

    if (dst.kind == SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE &&
        src.kind == SPACE_TRANSFER_ENDPOINT_SPACE) {
        CettaMorkSpaceHandle *src_bridge = NULL;

        if (!dst.bridge || !src.space)
            return SPACE_TRANSFER_ERROR;
        if (!transfer_ensure_bridge_live(src.space, &src_bridge) ||
            !src_bridge) {
            return SPACE_TRANSFER_ERROR;
        }
        if (!cetta_mork_bridge_space_add_logical_rows_from(dst.bridge,
                                                           src_bridge,
                                                           &added)) {
            return SPACE_TRANSFER_ERROR;
        }
        if (out_added)
            *out_added = added;
        return SPACE_TRANSFER_OK;
    }

    if (dst.kind == SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE &&
        src.kind == SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE) {
        if (!dst.bridge || !src.bridge)
            return SPACE_TRANSFER_ERROR;
        if (!cetta_mork_bridge_space_add_logical_rows_from(dst.bridge,
                                                           src.bridge,
                                                           &added)) {
            return SPACE_TRANSFER_ERROR;
        }
        if (out_added)
            *out_added = added;
        return SPACE_TRANSFER_OK;
    }

    return SPACE_TRANSFER_ERROR;
}

static bool pathmap_local_deactivate_bridge_preserving_shadow(Space *s,
                                                              ImportedBridgeState *st) {
    if (!s || !st)
        return false;
    if (st->bridge_active) {
        if (!imported_shadow_refresh_from_projection(s)) {
            st->bridge_active = false;
            st->bridge_unavailable = true;
            st->built = false;
            st->dirty = true;
            return false;
        }
    }
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    st->bridge_active = false;
    st->bridge_unavailable = true;
    st->built = false;
    st->dirty = true;
    return true;
}

static bool pathmap_local_apply_text_chunk_direct(Space *s,
                                                  const uint8_t *text,
                                                  size_t len,
                                                  bool remove_atoms,
                                                  uint64_t *out_changed) {
    ImportedBridgeState *st;
    uint64_t changed = 0;

    if (out_changed)
        *out_changed = 0;
    if (!s || s->match_backend.kind != SPACE_ENGINE_PATHMAP || space_is_ordered(s))
        return false;
    if (s->match_backend.pathmap.bridge.bridge_unavailable)
        return false;
    if (!(text || len == 0))
        return false;
    if (imported_text_may_contain_vars(text, len))
        return false;

    st = &s->match_backend.pathmap.bridge;
    if (!pathmap_local_ensure_bridge_live(s) || !st->bridge_active || !st->bridge_space)
        return false;

    if (!(remove_atoms
              ? cetta_mork_bridge_space_remove_sexpr(
                    (CettaMorkSpaceHandle *)st->bridge_space, text, len, &changed)
              : cetta_mork_bridge_space_add_sexpr(
                    (CettaMorkSpaceHandle *)st->bridge_space, text, len, &changed))) {
        st->bridge_active = false;
        st->built = false;
        st->dirty = true;
        return false;
    }

    if (out_changed)
        *out_changed = changed;
    if (changed == 0)
        return true;

    pathmap_local_backend_primary_bulk_commit(s, st);
    s->revision++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_REVISION_BUMP);
    return true;
}

static bool pathmap_local_store_atom_id_direct(Space *s, AtomId atom_id, Atom *atom) {
    ImportedBridgeState *st;
    bool had_live_bridge;
    bool prior_built;
    bool prior_dirty;
    if (!s || atom_id == CETTA_ATOM_ID_NONE || !s->native.universe || space_is_ordered(s))
        return false;
    st = &s->match_backend.pathmap.bridge;
    if (s->match_backend.pathmap.bridge.bridge_unavailable)
        return false;
    had_live_bridge = st->bridge_active && st->bridge_space;
    prior_built = st->built;
    prior_dirty = st->dirty;
    if (!(st->bridge_active && st->bridge_space) &&
        !pathmap_local_ensure_bridge_live(s)) {
        if (!had_live_bridge) {
            st->bridge_active = false;
            st->built = prior_built;
            st->dirty = prior_dirty;
        }
        return false;
    }
    Arena scratch;
    arena_init(&scratch);
    bool ok = tu_has_vars(s->native.universe, atom_id)
                  ? imported_bridge_add_atom_contextual_exact(
                        &scratch, (CettaMorkSpaceHandle *)st->bridge_space,
                        s->native.universe, atom_id)
                  : imported_bridge_add_atom_structural(
                        &scratch, (CettaMorkSpaceHandle *)st->bridge_space,
                        s->native.universe, atom_id, atom);
    arena_free(&scratch);
    if (!ok) {
        if (had_live_bridge) {
            (void)pathmap_local_deactivate_bridge_preserving_shadow(s, st);
        } else {
            st->bridge_active = false;
            st->built = prior_built;
            st->dirty = prior_dirty;
        }
        return false;
    }
    pathmap_local_backend_primary_bulk_commit(s, st);
    return true;
}

static bool pathmap_local_remove_atom_id_direct(Space *s, AtomId atom_id) {
    ImportedBridgeState *st;
    Arena scratch;
    uint64_t removed = 0;
    bool ok = false;

    if (!s || atom_id == CETTA_ATOM_ID_NONE || !s->native.universe || space_is_ordered(s))
        return false;
    if (s->match_backend.pathmap.bridge.bridge_unavailable)
        return false;
    st = &s->match_backend.pathmap.bridge;
    if (!(st->bridge_active && st->bridge_space) &&
        !pathmap_local_ensure_bridge_live(s))
        return false;

    arena_init(&scratch);
    ok = tu_has_vars(s->native.universe, atom_id)
             ? imported_bridge_remove_atom_contextual_exact(
                   &scratch, (CettaMorkSpaceHandle *)st->bridge_space,
                   s->native.universe, atom_id, &removed)
             : imported_bridge_remove_atom_structural(
                   &scratch, (CettaMorkSpaceHandle *)st->bridge_space,
                   s->native.universe, atom_id, NULL, &removed);
    arena_free(&scratch);
    if (!ok) {
        st->bridge_active = false;
        st->built = false;
        st->dirty = true;
        return false;
    }
    if (removed == 0)
        return false;

    pathmap_local_backend_primary_bulk_commit(s, st);
    return true;
}

static bool pathmap_local_remove_atom_direct(Space *s, Atom *atom) {
    ImportedBridgeState *st;
    Arena scratch;
    uint64_t removed = 0;
    bool ok = false;

    if (!s || !atom || space_is_ordered(s))
        return false;
    if (s->match_backend.pathmap.bridge.bridge_unavailable)
        return false;
    st = &s->match_backend.pathmap.bridge;
    if (!(st->bridge_active && st->bridge_space) &&
        !pathmap_local_ensure_bridge_live(s))
        return false;

    arena_init(&scratch);
    ok = atom_has_vars(atom)
             ? imported_bridge_remove_atom_contextual_exact_atom(
                   &scratch, (CettaMorkSpaceHandle *)st->bridge_space,
                   atom, &removed)
             : imported_bridge_remove_atom_structural(
                   &scratch, (CettaMorkSpaceHandle *)st->bridge_space,
                   s->native.universe, CETTA_ATOM_ID_NONE, atom, &removed);
    arena_free(&scratch);
    if (!ok) {
        st->bridge_active = false;
        st->built = false;
        st->dirty = true;
        return false;
    }
    if (removed == 0)
        return false;

    pathmap_local_backend_primary_bulk_commit(s, st);
    return true;
}

static bool pathmap_local_truncate_direct(Space *s, uint32_t new_len) {
    ImportedBridgeState *st;
    CettaMorkSpaceHandle *clone = NULL;
    uint32_t logical_len;
    bool ok = true;

    if (!s || s->match_backend.kind != SPACE_ENGINE_PATHMAP || space_is_ordered(s))
        return false;
    if (s->match_backend.pathmap.bridge.bridge_unavailable)
        return false;

    logical_len = imported_logical_len(s);
    if (new_len > logical_len)
        return false;
    if (new_len == logical_len)
        return true;

    st = &s->match_backend.pathmap.bridge;
    if (!(st->bridge_active && st->bridge_space) &&
        !pathmap_local_ensure_bridge_live(s))
        return false;
    if (!st->bridge_active || !st->bridge_space)
        return false;

    if (new_len == 0) {
        if (!cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space)) {
            st->bridge_active = false;
            st->built = false;
            st->dirty = true;
            return false;
        }
        pathmap_local_backend_primary_bulk_commit(s, st);
        return true;
    }

    if (!imported_storage_ensure_projection(s))
        return false;
    if (st->projected_len != logical_len)
        return false;

    clone = cetta_mork_bridge_space_clone((CettaMorkSpaceHandle *)st->bridge_space);
    if (!clone)
        return false;

    for (uint32_t i = logical_len; i > new_len; i--) {
        Arena scratch;
        uint64_t removed = 0;
        AtomId atom_id = st->projected_atom_ids[i - 1];

        if (atom_id == CETTA_ATOM_ID_NONE) {
            ok = false;
            break;
        }

        arena_init(&scratch);
        ok = imported_bridge_remove_atom_structural(
            &scratch, clone, s->native.universe, atom_id, NULL, &removed);
        arena_free(&scratch);
        if (!ok || removed == 0) {
            ok = false;
            break;
        }
    }

    if (!ok) {
        cetta_mork_bridge_space_free(clone);
        return false;
    }

    cetta_mork_bridge_space_free((CettaMorkSpaceHandle *)st->bridge_space);
    st->bridge_space = clone;
    st->bridge_active = true;
    pathmap_local_backend_primary_bulk_commit(s, st);
    return true;
}

static void pathmap_local_note_add(Space *s, AtomId atom_id, Atom *atom,
                                   uint32_t atom_idx) {
    ImportedBridgeState *st = &s->match_backend.pathmap.bridge;
    (void)atom_idx;
    imported_projection_clear(st);
    if (s->native.universe && tu_has_vars(s->native.universe, atom_id)) {
        st->bridge_active = false;
        st->bridge_unavailable = true;
        if (st->built && !st->dirty) {
            if (tu_hdr(s->native.universe, atom_id)) {
                ImportedFlatBuilder b = {0};
                if (imported_flatten_atom_id(&b, s->native.universe, atom_id)) {
                    imported_bucket_push_builder(
                        imported_bucket_for_atom_id(st, s->native.universe, atom_id),
                        &b, atom_idx, stree_next_epoch());
                    return;
                }
                free(b.items);
            }
            if (atom) {
                imported_bucket_add_entry(imported_bucket_for_atom(st, atom), atom, atom_idx,
                                          stree_next_epoch());
                return;
            }
        }
        st->built = false;
        st->dirty = true;
        return;
    }
    if (st->bridge_unavailable) {
        if (st->built && !st->dirty) {
            if (tu_hdr(s->native.universe, atom_id)) {
                ImportedFlatBuilder b = {0};
                if (imported_flatten_atom_id(&b, s->native.universe, atom_id)) {
                    imported_bucket_push_builder(
                        imported_bucket_for_atom_id(st, s->native.universe, atom_id),
                        &b, atom_idx, stree_next_epoch());
                    return;
                }
                free(b.items);
            }
            if (atom) {
                imported_bucket_add_entry(imported_bucket_for_atom(st, atom), atom, atom_idx,
                                          stree_next_epoch());
                return;
            }
        }
        st->built = false;
        st->dirty = true;
        return;
    }
    if (!st->bridge_active && st->built && !st->dirty) {
        if (tu_hdr(s->native.universe, atom_id)) {
            ImportedFlatBuilder b = {0};
            if (imported_flatten_atom_id(&b, s->native.universe, atom_id)) {
                imported_bucket_push_builder(
                    imported_bucket_for_atom_id(st, s->native.universe, atom_id),
                    &b, atom_idx, stree_next_epoch());
                return;
            }
            free(b.items);
        }
        if (atom) {
            imported_bucket_add_entry(imported_bucket_for_atom(st, atom), atom, atom_idx,
                                      stree_next_epoch());
            return;
        }
        st->built = false;
        st->dirty = true;
        return;
    }
    if (st->bridge_active && st->bridge_space) {
        Arena scratch;
        arena_init(&scratch);
        bool ok = imported_bridge_add_atom_structural(
            &scratch, (CettaMorkSpaceHandle *)st->bridge_space, s->native.universe,
            atom_id, atom);
        arena_free(&scratch);
        if (ok) {
            imported_flat_state_clear(st);
            st->built = false;
            st->dirty = false;
            return;
        }
        (void)pathmap_local_deactivate_bridge_preserving_shadow(s, st);
        return;
    }
    if (imported_logical_len(s) <= 1u) {
        if (!pathmap_local_ensure_bridge_space(&s->match_backend.pathmap) ||
            !cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space)) {
            st->built = false;
            st->dirty = true;
            return;
        }
        st->bridge_active = true;
        st->bridge_unavailable = false;
        Arena scratch;
        arena_init(&scratch);
        bool ok = imported_bridge_add_atom_structural(
            &scratch, (CettaMorkSpaceHandle *)st->bridge_space, s->native.universe,
            atom_id, atom);
        arena_free(&scratch);
        if (ok) {
            imported_flat_state_clear(st);
            st->built = false;
            st->dirty = false;
            return;
        }
        st->bridge_active = false;
        st->bridge_unavailable = true;
        st->built = false;
        st->dirty = true;
        return;
    }
    if (!backend_rebuild_bridge(s)) {
        st->built = false;
        st->dirty = true;
        return;
    }
    st->built = false;
    st->dirty = false;
}

static void pathmap_local_note_remove(Space *s) {
    ImportedBridgeState *st = &s->match_backend.pathmap.bridge;
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    st->bridge_active = false;
    st->bridge_unavailable = false;
    st->built = false;
    st->dirty = true;
}

static void pathmap_local_free_backend(Space *s) {
    imported_state_free(&s->match_backend.pathmap.bridge);
}

static void imported_free_backend(Space *s) {
    imported_state_free(&s->match_backend.mork.bridge);
    s->match_backend.mork.attached_compiled = false;
    s->match_backend.mork.attached_count = 0;
}

static uint32_t all_linear_candidates(Space *s, uint32_t **out) {
    uint32_t len = s ? s->native.len : 0;
    *out = cetta_malloc(sizeof(uint32_t) * (len ? len : 1u));
    for (uint32_t i = 0; i < len; i++)
        (*out)[i] = i;
    return len;
}

static void native_candidate_exact_query(Space *s, Arena *a, Atom *query,
                                         SubstMatchSet *out) {
    smset_init(out);
    if (s->native.len == 0) return;
    uint32_t *candidates = NULL;
    uint32_t ncand =
        (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
         s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT)
            ? native_candidates(s, query, &candidates)
            : all_linear_candidates(s, &candidates);
    for (uint32_t ci = 0; ci < ncand; ci++) {
        uint32_t idx = candidates[ci];
        if (idx >= s->native.len) continue;
        uint32_t epoch = fresh_var_suffix();
        Bindings b;
        bindings_init(&b);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_NATIVE_CANDIDATE);
        if (match_space_atom_epoch(s, idx, query, &b, a, epoch) &&
            !bindings_has_loop(&b)) {
            subst_matchset_push(out, idx, epoch, &b, false);
        }
        bindings_free(&b);
    }
    free(candidates);
}

static void imported_epoch_query_candidates(Space *s, Arena *a, Atom *query,
                                            SubstMatchSet *out) {
    MorkImportedState *mst = mork_imported_state(s);
    ImportedBridgeState *st = mst ? &mst->bridge : NULL;
    BindingSet direct_matches;
    if (mst && mst->attached_compiled) {
        if (st->bridge_active &&
            space_match_backend_mork_query_bindings_direct(
                (CettaMorkSpaceHandle *)st->bridge_space, a, query,
                &direct_matches)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V2_HIT);
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_QUERY);
            imported_binding_set_to_exact_matches(out, &direct_matches);
            binding_set_free(&direct_matches);
            return;
        }
        Arena *persist = eval_current_persistent_arena();
        if (!space_match_backend_materialize_native_storage(s, persist ? persist : a)) {
            smset_init(out);
            return;
        }
        imported_ensure_built(s);
    }
    native_candidate_exact_query(s, a, query, out);
}

typedef struct {
    Atom *pattern;
    uint32_t idx;
    uint32_t estimate;
} ImportedConjStep;

static uint32_t imported_pattern_estimate(Space *s, Atom *pattern) {
    ImportedBridgeState *st = backend_bridge_state(s);
    const MorkImportedState *mst = mork_imported_state_const(s);
    if (s && s->match_backend.kind == SPACE_ENGINE_PATHMAP)
        return imported_logical_len(s);
    imported_ensure_built(s);
    if (mst && mst->attached_compiled)
        return imported_logical_len(s);
    if (!st)
        return 0;
    SymbolId head = atom_head_sym(pattern);
    if (head != SYMBOL_ID_NONE)
        return st->buckets[stree_head_hash(head)].len + st->wildcard.len;
    uint32_t total = st->wildcard.len;
    for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++)
        total += st->buckets[bi].len;
    return total;
}

static int imported_cmp_conj_step(const void *lhs, const void *rhs) {
    const ImportedConjStep *a = lhs;
    const ImportedConjStep *b = rhs;
    if (a->estimate != b->estimate)
        return (a->estimate > b->estimate) - (a->estimate < b->estimate);
    return (a->idx > b->idx) - (a->idx < b->idx);
}

static void space_query_conjunction_default(Space *s, Arena *a,
                                            Atom **patterns, uint32_t npatterns,
                                            const Bindings *seed, BindingSet *out) {
    binding_set_init(out);
    if (npatterns == 0) {
        if (seed) binding_set_push(out, seed);
        return;
    }

    BindingSet cur;
    binding_set_init(&cur);
    if (seed) {
        binding_set_push(&cur, seed);
    } else {
        Bindings empty;
        bindings_init(&empty);
        binding_set_push(&cur, &empty);
    }

    for (uint32_t pi = 0; pi < npatterns; pi++) {
        BindingSet next;
        binding_set_init(&next);
        for (uint32_t bi = 0; bi < cur.len; bi++) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_SPACE_CONJ_DEFAULT);
            Atom *grounded = bindings_apply_if_vars(&cur.items[bi], a, patterns[pi]);
            SubstMatchSet smr;
            smset_init(&smr);
            space_subst_query(s, a, grounded, &smr);
            for (uint32_t mi = 0; mi < smr.len; mi++) {
                Bindings merged;
                if (space_subst_match_with_seed(s, grounded, &smr.items[mi],
                                                &cur.items[bi], a, &merged)) {
                    binding_set_push_move(&next, &merged);
                    bindings_free(&merged);
                }
            }
            smset_free(&smr);
        }
        binding_set_free(&cur);
        cur = next;
        if (cur.len == 0) break;
    }

    *out = cur;
}

static bool
imported_bridge_query_conjunction_fast(Space *s, Arena *a,
                                       Atom **patterns, uint32_t npatterns,
                                       const Bindings *seed,
                                       BindingSet *out) {
    if (npatterns == 0) {
        binding_set_init(out);
        if (seed)
            return binding_set_push(out, seed);
        return true;
    }
    if (npatterns > IMPORTED_CONJUNCTION_PATTERN_LIMIT)
        return false;

    ImportedConjStep order[IMPORTED_CONJUNCTION_PATTERN_LIMIT];
    for (uint32_t i = 0; i < npatterns; i++) {
        order[i].pattern = patterns[i];
        order[i].idx = i;
        order[i].estimate = imported_pattern_estimate(s, patterns[i]);
    }
    qsort(order, npatterns, sizeof(ImportedConjStep), imported_cmp_conj_step);

    Arena scratch;
    arena_init(&scratch);
    arena_set_hashcons(&scratch, NULL);
    Atom **grounded = arena_alloc(&scratch, sizeof(Atom *) * npatterns);
    ImportedBridgeVarMap query_vars;
    imported_bridge_varmap_init(&query_vars);
    binding_set_init(out);

    bool success = true;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t row_count = 0;
    uint32_t factor_count = 0;
    bool multi_ref_groups = false;
    bool direct_multiplicities = false;
    bool wide_tokens = false;

    for (uint32_t i = 0; i < npatterns; i++) {
        if (seed)
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_SPACE_CONJ_IMPORTED);
        grounded[i] = seed ? bindings_apply_if_vars(seed, &scratch, order[i].pattern)
                           : order[i].pattern;
        if (!imported_bridge_collect_vars(grounded[i], &query_vars)) {
            success = false;
            goto cleanup;
        }
    }

    char *query_text = imported_bridge_build_conjunction_text(&scratch, grounded, npatterns);
    if (!query_text) {
        success = false;
        goto cleanup;
    }
    if (imported_bridge_query_text_has_internal_vars(query_text)) {
        success = false;
        goto cleanup;
    }

    if (cetta_mork_bridge_space_query_contextual_rows(
            (CettaMorkSpaceHandle *)backend_bridge_state(s)->bridge_space,
            (const uint8_t *)query_text, strlen(query_text),
            &packet, &packet_len, &row_count)) {
        success = imported_bridge_visit_contextual_query_rows_packet(
            a, packet, packet_len, row_count, &query_vars, seed,
            mork_query_collect_bindings, out);
        goto cleanup;
    }
    cetta_mork_bridge_bytes_free(packet, packet_len);
    packet = NULL;
    packet_len = 0;
    row_count = 0;

    if (!cetta_mork_bridge_space_query_bindings_multi_ref_v3(
            (CettaMorkSpaceHandle *)backend_bridge_state(s)->bridge_space,
            (const uint8_t *)query_text, strlen(query_text),
            &packet, &packet_len, &row_count)) {
        success = false;
        goto cleanup;
    }

    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t parsed_rows = 0;
    if (!imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &flags) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &factor_count)) {
        success = false;
        goto cleanup;
    }
    if (!imported_bridge_read_u32(packet, packet_len, &off, &parsed_rows)) {
        success = false;
        goto cleanup;
    }
    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_MULTI_REF_V3_VERSION) {
        success = false;
        goto cleanup;
    }
    multi_ref_groups = (flags & IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS) != 0;
    direct_multiplicities =
        (flags & IMPORTED_MORK_MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES) != 0;
    wide_tokens =
        (flags & IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS) != 0;
    if ((flags & (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                  IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES)) !=
            (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
             IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES) ||
        (flags & ~(IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES |
                   IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS |
                   IMPORTED_MORK_MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS)) != 0 ||
        multi_ref_groups == direct_multiplicities ||
        factor_count != npatterns || parsed_rows != row_count) {
        success = false;
        goto cleanup;
    }

    for (uint32_t row = 0; row < parsed_rows && success; row++) {
        uint64_t multiplicity = 1;
        ImportedBridgeValueVarMap value_vars;
        imported_bridge_value_varmap_init(&value_vars);
        bool merged_inited = false;
        for (uint32_t fi = 0; fi < factor_count; fi++) {
            uint64_t factor_multiplicity = 0;
            if (direct_multiplicities) {
                uint32_t count = 0;
                if (!imported_bridge_read_u32(packet, packet_len, &off, &count) ||
                    count == 0) {
                    success = false;
                    break;
                }
                factor_multiplicity = count;
            } else {
                uint32_t ref_count = 0;
                if (!imported_bridge_read_u32(packet, packet_len, &off, &ref_count) ||
                    ref_count == 0 ||
                    off + (size_t)ref_count * 4u > packet_len) {
                    success = false;
                    break;
                }
                factor_multiplicity = ref_count;
                for (uint32_t ri = 0; ri < ref_count; ri++) {
                    uint32_t atom_idx = ((uint32_t)packet[off] << 24) |
                                        ((uint32_t)packet[off + 1] << 16) |
                                        ((uint32_t)packet[off + 2] << 8) |
                                        (uint32_t)packet[off + 3];
                    if (atom_idx >= imported_logical_len(s)) {
                        success = false;
                        break;
                    }
                    off += 4u;
                }
                if (!success)
                    break;
            }
            if (multiplicity > UINT64_MAX / factor_multiplicity) {
                success = false;
                break;
            }
            multiplicity *= factor_multiplicity;
        }
        if (!success) {
            imported_bridge_value_varmap_free(&value_vars);
            break;
        }

        uint32_t binding_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            success = false;
            imported_bridge_value_varmap_free(&value_vars);
            break;
        }

        Bindings merged;
        if (seed) {
            if (!bindings_clone(&merged, seed)) {
                success = false;
                imported_bridge_value_varmap_free(&value_vars);
                break;
            }
        } else {
            bindings_init(&merged);
        }
        merged_inited = true;

        for (uint32_t bi = 0; bi < binding_count && success; bi++) {
            uint16_t query_slot = 0;
            uint8_t value_env = 0;
            uint8_t value_flags = 0;
            uint32_t expr_len = 0;
            if (!imported_bridge_read_u16(packet, packet_len, &off, &query_slot) ||
                off + 2 > packet_len) {
                success = false;
                break;
            }
            value_env = packet[off++];
            value_flags = packet[off++];
            if (!imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
                off + expr_len > packet_len) {
                success = false;
                break;
            }
            const uint8_t *expr = packet + off;
            off += expr_len;

            ImportedBridgeVarSlot *slot =
                imported_bridge_varmap_lookup(&query_vars, query_slot);
            if (!slot) {
                success = false;
                break;
            }

            bool value_ok = true;
            Atom *value = imported_bridge_parse_value_raw_multi_ref_v3(
                a, expr, expr_len, value_env, value_flags,
                wide_tokens, &value_vars, &value_ok);
            if (!value_ok ||
                !bindings_add_id(&merged, slot->var_id, slot->spelling, value)) {
                success = false;
                break;
            }
        }

        if (success) {
            for (uint64_t rep = 0; rep < multiplicity && success; rep++) {
                if (!binding_set_push(out, &merged))
                    success = false;
            }
        }
        if (merged_inited)
            bindings_free(&merged);
        imported_bridge_value_varmap_free(&value_vars);
    }

cleanup:
    if (!success) {
        binding_set_free(out);
        binding_set_init(out);
    }
    imported_bridge_varmap_free(&query_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    arena_free(&scratch);
    return success;
}

static void imported_query_conjunction_flat(Space *s, Arena *a, Atom **patterns,
                                            uint32_t npatterns, const Bindings *seed,
                                            BindingSet *out) {
    if (npatterns == 0) {
        binding_set_init(out);
        if (seed) binding_set_push(out, seed);
        return;
    }

    imported_ensure_built_flat(s);
    ImportedConjStep order[IMPORTED_CONJUNCTION_PATTERN_LIMIT];
    if (npatterns > IMPORTED_CONJUNCTION_PATTERN_LIMIT) {
        space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
        return;
    }
    for (uint32_t i = 0; i < npatterns; i++) {
        order[i].pattern = patterns[i];
        order[i].idx = i;
        order[i].estimate = imported_pattern_estimate(s, patterns[i]);
    }
    qsort(order, npatterns, sizeof(ImportedConjStep), imported_cmp_conj_step);

    BindingSet cur;
    binding_set_init(&cur);
    if (seed) {
        binding_set_push(&cur, seed);
    } else {
        Bindings empty;
        bindings_init(&empty);
        binding_set_push(&cur, &empty);
    }

    for (uint32_t pi = 0; pi < npatterns; pi++) {
        BindingSet next;
        binding_set_init(&next);
        for (uint32_t bi = 0; bi < cur.len; bi++) {
            Atom *grounded = bindings_apply_if_vars(&cur.items[bi], a, order[pi].pattern);
            SubstMatchSet smr;
            smset_init(&smr);
            space_subst_query(s, a, grounded, &smr);
            for (uint32_t mi = 0; mi < smr.len; mi++) {
                Bindings merged;
                if (space_subst_match_with_seed(s, grounded, &smr.items[mi],
                                                &cur.items[bi], a, &merged)) {
                    binding_set_push_move(&next, &merged);
                    bindings_free(&merged);
                }
            }
            smset_free(&smr);
        }
        binding_set_free(&cur);
        cur = next;
        if (cur.len == 0) break;
    }
    *out = cur;
}

static void imported_query_conjunction(Space *s, Arena *a, Atom **patterns,
                                       uint32_t npatterns, const Bindings *seed,
                                       BindingSet *out) {
    ImportedBridgeState *st = backend_bridge_state(s);
    MorkImportedState *mst = mork_imported_state(s);
    imported_ensure_built(s);
    if (!backend_uses_bridge_adapter(s)) {
        imported_query_conjunction_flat(s, a, patterns, npatterns, seed, out);
        return;
    }
    if (st && st->bridge_active) {
        if (imported_bridge_query_conjunction_fast(s, a, patterns, npatterns, seed, out)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V3_HIT);
            if (mst && mst->attached_compiled)
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_QUERY);
            return;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V3_FALLBACK);
        if (mst && mst->attached_compiled) {
            if (!space_match_backend_materialize_native_storage(
                    s, eval_current_persistent_arena() ? eval_current_persistent_arena() : a)) {
                binding_set_init(out);
                return;
            }
            imported_ensure_built(s);
        }
        space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
        return;
    }
    imported_query_conjunction_flat(s, a, patterns, npatterns, seed, out);
}

static const SpaceMatchBackendOps NATIVE_BACKEND_OPS = {
    .name = "native-subst-tree",
    .store_atom_id_direct = NULL,
    .remove_atom_id_direct = NULL,
    .remove_atom_direct = NULL,
    .truncate_direct = NULL,
    .logical_len = shadow_storage_logical_len,
    .get_atom_id_at = shadow_storage_get_atom_id_at,
    .get_at = shadow_storage_get_at,
    .materialize_native_storage = native_materialize_native_storage,
    .supports_direct_bindings = true,
    .free = native_free,
    .note_add = native_note_add,
    .note_remove = native_note_remove,
    .candidates = native_candidates,
    .query = native_query,
    .query_conjunction = NULL,
};

static const SpaceMatchBackendOps NATIVE_CANDIDATE_EXACT_BACKEND_OPS = {
    .name = "native-candidate-exact",
    .store_atom_id_direct = NULL,
    .remove_atom_id_direct = NULL,
    .remove_atom_direct = NULL,
    .truncate_direct = NULL,
    .logical_len = shadow_storage_logical_len,
    .get_atom_id_at = shadow_storage_get_atom_id_at,
    .get_at = shadow_storage_get_at,
    .materialize_native_storage = native_materialize_native_storage,
    .supports_direct_bindings = true,
    .free = native_free,
    .note_add = native_note_add,
    .note_remove = native_note_remove,
    .candidates = native_candidates,
    .query = native_candidate_exact_query,
    .query_conjunction = NULL,
};

/* Generic SPACE_ENGINE_PATHMAP stays local to CeTTa-owned atoms. */
static const SpaceMatchBackendOps PATHMAP_BACKEND_OPS = {
    .name = "pathmap",
    .store_atom_id_direct = pathmap_local_store_atom_id_direct,
    .remove_atom_id_direct = pathmap_local_remove_atom_id_direct,
    .remove_atom_direct = pathmap_local_remove_atom_direct,
    .truncate_direct = pathmap_local_truncate_direct,
    .logical_len = imported_storage_logical_len,
    .get_atom_id_at = imported_storage_get_atom_id_at,
    .get_at = imported_storage_get_at,
    .materialize_native_storage = pathmap_materialize_native_storage,
    .supports_direct_bindings = true,
    .free = pathmap_local_free_backend,
    .note_add = pathmap_local_note_add,
    .note_remove = pathmap_local_note_remove,
    .candidates = pathmap_local_candidates,
    .query = pathmap_local_query,
    .query_conjunction = pathmap_local_query_conjunction,
};

/* Explicit MORK spaces keep the bridge-backed imported lane. */
static const SpaceMatchBackendOps MORK_BRIDGE_BACKEND_OPS = {
    .name = "mork",
    .store_atom_id_direct = NULL,
    .remove_atom_id_direct = NULL,
    .remove_atom_direct = NULL,
    .truncate_direct = NULL,
    .logical_len = imported_storage_logical_len,
    .get_atom_id_at = imported_storage_get_atom_id_at,
    .get_at = imported_storage_get_at,
    .materialize_native_storage = mork_materialize_native_storage,
    .supports_direct_bindings = true,
    .free = imported_free_backend,
    .note_add = imported_note_add,
    .note_remove = imported_note_remove,
    .candidates = imported_candidates,
    .query = imported_query,
    .query_conjunction = imported_query_conjunction,
};

void space_match_backend_init(Space *s) {
    s->match_backend.kind = SPACE_ENGINE_NATIVE;
    s->match_backend.ops = &NATIVE_BACKEND_OPS;
    s->match_backend.native.match_trie = NULL;
    s->match_backend.native.match_trie_dirty = false;
    s->match_backend.native.stree = NULL;
    s->match_backend.native.stree_dirty = false;
    memset(&s->match_backend.pathmap, 0, sizeof(s->match_backend.pathmap));
    memset(&s->match_backend.mork, 0, sizeof(s->match_backend.mork));
}

void space_match_backend_free(Space *s) {
    if (s->match_backend.ops && s->match_backend.ops->free)
        s->match_backend.ops->free(s);
}

bool space_match_backend_try_set(Space *s, SpaceEngine kind) {
    if (space_match_backend_unavailable_reason(kind))
        return false;
    const SpaceMatchBackendOps *ops = NULL;
    switch (kind) {
    case SPACE_ENGINE_NATIVE:
        ops = &NATIVE_BACKEND_OPS;
        break;
    case SPACE_ENGINE_NATIVE_CANDIDATE_EXACT:
        ops = &NATIVE_CANDIDATE_EXACT_BACKEND_OPS;
        break;
    case SPACE_ENGINE_PATHMAP:
        ops = &PATHMAP_BACKEND_OPS;
        break;
    case SPACE_ENGINE_MORK:
        ops = &MORK_BRIDGE_BACKEND_OPS;
        break;
    default:
        return false;
    }
    if (s && s->match_backend.kind != kind &&
        s->match_backend.ops &&
        s->match_backend.ops->materialize_native_storage &&
        (s->match_backend.kind == SPACE_ENGINE_PATHMAP ||
         s->match_backend.kind == SPACE_ENGINE_MORK)) {
        Arena *persistent = eval_current_persistent_arena();
        if (!s->match_backend.ops->materialize_native_storage(s, persistent))
            return false;
    }
    space_match_backend_free(s);
    space_match_backend_init(s);
    s->match_backend.kind = kind;
    s->match_backend.ops = ops;
    return true;
}

bool space_match_backend_needs_atom_on_add(const Space *s, AtomId atom_id) {
    if (!s)
        return false;
    switch (s->match_backend.kind) {
    case SPACE_ENGINE_NATIVE:
    case SPACE_ENGINE_NATIVE_CANDIDATE_EXACT:
        return native_needs_atom_on_add(s, atom_id);
    case SPACE_ENGINE_PATHMAP:
    case SPACE_ENGINE_MORK:
        return imported_needs_atom_on_add(s, atom_id);
    default:
        return false;
    }
}

void space_match_backend_note_add(Space *s, AtomId atom_id, Atom *atom,
                                  uint32_t atom_idx) {
    if (s->match_backend.ops && s->match_backend.ops->note_add)
        s->match_backend.ops->note_add(s, atom_id, atom, atom_idx);
}

void space_match_backend_note_remove(Space *s) {
    if (s->match_backend.ops && s->match_backend.ops->note_remove)
        s->match_backend.ops->note_remove(s);
}

uint32_t space_match_backend_candidates(Space *s, Atom *pattern, uint32_t **out) {
    if (!s->match_backend.ops || !s->match_backend.ops->candidates) {
        *out = NULL;
        return 0;
    }
    space_linearize(s);
    return s->match_backend.ops->candidates(s, pattern, out);
}

void space_match_backend_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    if (!s->match_backend.ops || !s->match_backend.ops->query) {
        smset_init(out);
        return;
    }
    space_linearize(s);
    s->match_backend.ops->query(s, a, query, out);
}

const char *space_match_backend_name(const Space *s) {
    if (!s)
        return "unconfigured";
    if (!s->match_backend.ops || !s->match_backend.ops->name)
        return "unconfigured";
    return space_match_backend_kind_name(s->match_backend.kind);
}

bool space_match_backend_supports_direct_bindings(const Space *s) {
    return s->match_backend.ops && s->match_backend.ops->supports_direct_bindings;
}

const char *space_match_backend_unavailable_reason(SpaceEngine kind) {
#if !CETTA_BUILD_WITH_PATHMAP_SPACE
    if (kind == SPACE_ENGINE_PATHMAP)
        return "generic pathmap-backed spaces require BUILD=pathmap or BUILD=full";
#endif
    (void)kind;
    return NULL;
}

const char *space_match_backend_kind_name(SpaceEngine kind) {
    switch (kind) {
    case SPACE_ENGINE_NATIVE:
        return "native";
    case SPACE_ENGINE_NATIVE_CANDIDATE_EXACT:
        return "native-candidate-exact";
    case SPACE_ENGINE_PATHMAP:
        return "pathmap";
    case SPACE_ENGINE_MORK:
        return "mork";
    default:
        return "unknown";
    }
}

bool space_match_backend_kind_from_name(const char *name, SpaceEngine *out) {
    if (strcmp(name, "native") == 0 || strcmp(name, "native-subst-tree") == 0) {
        *out = SPACE_ENGINE_NATIVE;
        return true;
    }
    if (strcmp(name, "native-candidate-exact") == 0) {
        *out = SPACE_ENGINE_NATIVE_CANDIDATE_EXACT;
        return true;
    }
    if (strcmp(name, "pathmap") == 0 || strcmp(name, "pathmap-imported") == 0) {
        *out = SPACE_ENGINE_PATHMAP;
        return true;
    }
    return false;
}

void space_match_backend_print_inventory(FILE *out) {
    fprintf(out, "space engines:\n");
    fprintf(out, "  native                 standard CeTTa / HE engine\n");
    fprintf(out, "  pathmap                flattened PathMap-style CeTTa engine without bridge rows");
#if !CETTA_BUILD_WITH_PATHMAP_SPACE
    fprintf(out, " (requires BUILD=pathmap or BUILD=full)");
#endif
    fprintf(out, "\n");
    fprintf(out, "  native-candidate-exact diagnostic native exact-matcher lane\n");
}

uint32_t space_match_candidates(Space *s, Atom *pattern, uint32_t **out) {
    return space_match_backend_candidates(s, pattern, out);
}

void space_subst_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    bool use_exact_shortcut =
        !s || s->match_backend.kind != SPACE_ENGINE_PATHMAP;
    if (use_exact_shortcut) {
        uint32_t *exact = NULL;
        uint32_t nexact = space_exact_match_indices(s, query, &exact);
        if (nexact > 0) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SUBST_QUERY_EXACT_SHORTCUT);
            smset_init(out);
            for (uint32_t i = 0; i < nexact; i++) {
                Bindings empty;
                bindings_init(&empty);
                subst_matchset_push(out, exact[i], 0, &empty, true);
                bindings_free(&empty);
            }
            free(exact);
            return;
        }
        free(exact);
        /* If query is exact-indexable and space has only exact atoms, no need for full scan */
        if (query && space_atom_is_exact_indexable(query) &&
            space_contains_only_exact_atoms(s)) {
            smset_init(out);
            return;
        }
    }
    space_match_backend_query(s, a, query, out);
}

bool space_subst_match_with_seed(Space *space, Atom *pattern, const SubstMatch *sm,
                                 const Bindings *seed, Arena *a, Bindings *out) {
    if (!space || !sm)
        return false;

    Bindings merged;
    bindings_init(&merged);
    if (!bindings_try_merge_live(&merged, &sm->bindings)) {
        bindings_free(&merged);
        return false;
    }
    if (seed && !bindings_try_merge_live(&merged, seed)) {
        bindings_free(&merged);
        return false;
    }

    if (sm->exact) {
        if (bindings_has_loop(&merged)) {
            bindings_free(&merged);
            return false;
        }
        bindings_move(out, &merged);
        return true;
    }

    if (space_match_backend_is_attached_compiled(space)) {
        Arena *persistent = eval_current_persistent_arena();
        if (!space_match_backend_materialize_native_storage(
                space, persistent ? persistent : a)) {
            bindings_free(&merged);
            return false;
        }
    }

    if (sm->atom_idx >= space->native.len) {
        bindings_free(&merged);
        return false;
    }

    uint32_t suffix = fresh_var_suffix();
    if (match_space_atom_epoch(space, sm->atom_idx, pattern, &merged, a, suffix) &&
        !bindings_has_loop(&merged)) {
        bindings_move(out, &merged);
        return true;
    }
    bindings_free(&merged);
    return false;
}

void space_query_conjunction(Space *s, Arena *a, Atom **patterns, uint32_t npatterns,
                             const Bindings *seed, BindingSet *out) {
    space_linearize(s);
    if (s->match_backend.ops && s->match_backend.ops->query_conjunction) {
        s->match_backend.ops->query_conjunction(s, a, patterns, npatterns, seed, out);
        return;
    }
    space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
}
