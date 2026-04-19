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
#define IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY 0x0001u
#define IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES 0x0002u
#define IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS 0x0004u
#define IMPORTED_MORK_MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES 0x0008u
#define IMPORTED_MORK_TAG_VARREF_MASK 0xC0u
#define IMPORTED_MORK_TAG_VARREF_PREFIX 0x80u
#define IMPORTED_MORK_TAG_SYMBOL_PREFIX 0xC0u
#define IMPORTED_MORK_TAG_NEWVAR 0xC0u
#define IMPORTED_CONJUNCTION_PATTERN_LIMIT 32u

static int cmp_uint32(const void *a, const void *b) {
    uint32_t va = *(const uint32_t *)a, vb = *(const uint32_t *)b;
    return (va > vb) - (va < vb);
}

static bool imported_ensure_bridge_space(PathmapImportedState *st);
static bool imported_rebuild_bridge(Space *s);
static void native_candidate_exact_query(Space *s, Arena *a, Atom *query,
                                         SubstMatchSet *out);
static void imported_epoch_query_candidates(Space *s, Arena *a, Atom *query,
                                            SubstMatchSet *out);
static bool imported_bridge_query_conjunction_fast(Space *s, Arena *a,
                                                   Atom **patterns, uint32_t npatterns,
                                                   const Bindings *seed,
                                                   BindingSet *out);
static bool imported_materialize_bridge_space(Space *dst,
                                              Arena *persistent_arena,
                                              CettaMorkSpaceHandle *bridge,
                                              uint64_t *out_loaded);

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

typedef struct {
    VarId *items;
    uint32_t len;
    uint32_t cap;
    bool repeated;
} NativeQueryVarSet;

static bool native_query_var_set_add(NativeQueryVarSet *set, VarId var_id) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i] == var_id) {
            set->repeated = true;
            return true;
        }
    }
    if (set->len >= set->cap) {
        uint32_t next_cap = set->cap ? set->cap * 2 : 8;
        VarId *next = cetta_realloc(set->items, sizeof(VarId) * next_cap);
        if (!next)
            return false;
        set->items = next;
        set->cap = next_cap;
    }
    set->items[set->len++] = var_id;
    return true;
}

static bool native_query_collect_vars(Atom *atom, NativeQueryVarSet *set) {
    if (!atom || set->repeated)
        return true;
    switch (atom->kind) {
    case ATOM_VAR:
        return native_query_var_set_add(set, atom->var_id);
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (!native_query_collect_vars(atom->expr.elems[i], set))
                return false;
            if (set->repeated)
                return true;
        }
        return true;
    default:
        return true;
    }
}

static bool native_query_has_repeated_vars(Atom *query) {
    NativeQueryVarSet set = {0};
    bool ok = native_query_collect_vars(query, &set);
    bool repeated = ok ? set.repeated : true;
    free(set.items);
    return repeated;
}

static void native_insert_match_trie_entry(Space *s, uint32_t atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    AtomId atom_id = space_get_atom_id_at(s, atom_idx);
    if (native_atom_id_insertable(s->universe, atom_id) &&
        disc_insert_id(st->match_trie, s->universe, atom_id, atom_idx)) {
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
    for (uint32_t i = 0; i < s->len; i++)
        native_insert_match_trie_entry(s, i);
    st->match_trie_dirty = false;
}

static void native_ensure_match_trie(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->match_trie) {
        st->match_trie = disc_node_new();
        for (uint32_t i = 0; i < s->len; i++)
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
        for (uint32_t i = 0; i < s->len; i++)
            native_insert_stree_entry(s, i);
        st->stree_dirty = false;
    } else if (st->stree_dirty) {
        stree_free(st->stree);
        stree_init(st->stree);
        for (uint32_t i = 0; i < s->len; i++)
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
        if (!(native_atom_id_insertable(s->universe, atom_id) &&
              disc_insert_id(st->match_trie, s->universe, atom_id, atom_idx)) &&
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
    if (!s || !query || !b || atom_idx >= s->len)
        return false;
    AtomId atom_id = space_get_atom_id_at(s, atom_idx);
    return match_atoms_atom_id_epoch(query, s->universe, atom_id, b, a, epoch);
}

static char *imported_bridge_atom_text(Arena *scratch,
                                       const TermUniverse *universe,
                                       AtomId atom_id, Atom *fallback_atom) {
    if (scratch && universe && tu_hdr(universe, atom_id))
        return term_universe_atom_to_parseable_string(scratch, universe, atom_id);
    if (fallback_atom)
        return atom_to_parseable_string(scratch, fallback_atom);
    if (universe && atom_id != CETTA_ATOM_ID_NONE) {
        Atom *decoded = term_universe_get_atom(universe, atom_id);
        if (decoded)
            return atom_to_parseable_string(scratch, decoded);
    }
    return NULL;
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

    char *sexpr = imported_bridge_atom_text(scratch, universe, atom_id, fallback_atom);
    return sexpr &&
           cetta_mork_bridge_space_add_text(bridge_space, sexpr, NULL);
}

static void imported_binding_set_to_exact_matches(SubstMatchSet *out,
                                                  const BindingSet *matches) {
    smset_init(out);
    if (!matches)
        return;
    for (uint32_t i = 0; i < matches->len; i++)
        subst_matchset_push(out, 0, 0, &matches->items[i], true);
}

static void native_exact_scan_query(Space *s, Arena *a, Atom *query,
                                    SubstMatchSet *out) {
    smset_init(out);
    if (!s || s->len == 0)
        return;
    for (uint32_t i = 0; i < s->len; i++) {
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
}

static uint32_t native_candidates(Space *s, Atom *pattern, uint32_t **out) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (s->len <= MATCH_TRIE_THRESHOLD) {
        *out = cetta_malloc(sizeof(uint32_t) * (s->len ? s->len : 1));
        for (uint32_t i = 0; i < s->len; i++) (*out)[i] = i;
        return s->len;
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
    if (s->len == 0) return;
    if (s->len <= MATCH_TRIE_THRESHOLD) {
        native_exact_scan_query(s, a, query, out);
        return;
    }
    if (native_query_has_repeated_vars(query)) {
        native_exact_scan_query(s, a, query, out);
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

static void imported_bridge_varmap_init(ImportedBridgeVarMap *map);
static void imported_bridge_varmap_free(ImportedBridgeVarMap *map);
static ImportedBridgeVarSlot *imported_bridge_varmap_lookup(ImportedBridgeVarMap *map,
                                                            uint32_t query_slot);
static bool imported_bridge_collect_vars(Atom *atom, ImportedBridgeVarMap *map);
static bool imported_bridge_read_u32(const uint8_t *packet, size_t len, size_t *off,
                                     uint32_t *out);
static bool imported_bridge_read_u16(const uint8_t *packet, size_t len, size_t *off,
                                     uint16_t *out);
static ImportedBridgeExprDecodeResult imported_bridge_expr_to_atom_id(
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

static void imported_flat_state_clear(PathmapImportedState *st) {
    for (uint32_t i = 0; i < STREE_BUCKETS; i++)
        imported_bucket_free(&st->buckets[i]);
    imported_bucket_free(&st->wildcard);
}

static void imported_state_free(PathmapImportedState *st) {
    imported_flat_state_clear(st);
    if (st->bridge_space) {
        cetta_mork_bridge_space_free((CettaMorkSpaceHandle *)st->bridge_space);
        st->bridge_space = NULL;
    }
    st->bridge_active = false;
    st->attached_compiled = false;
    st->attached_count = 0;
    st->built = false;
    st->dirty = false;
}

static uint32_t imported_logical_len(const Space *s) {
    const PathmapImportedState *st = &s->match_backend.imported;
    if (st->attached_compiled)
        return st->attached_count;
    return s->len;
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

    if (s && s->universe) {
        AtomId *atom_ids = NULL;
        int n = parse_metta_text_ids(text, s->universe, &atom_ids);
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
            if (s && s->universe) {
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

static ImportedBridgeExprDecodeResult imported_bridge_expr_to_atom_id_rec(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    size_t *off,
    AtomId *out_id) {
    if (!universe || !scratch || !expr || !off || !out_id || *off >= expr_len)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;

    uint8_t tag = expr[*off];
    if (tag == IMPORTED_MORK_TAG_NEWVAR ||
        (tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_VARREF_PREFIX) {
        return IMPORTED_BRIDGE_EXPR_DECODE_NEEDS_TEXT_FALLBACK;
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
    AtomId *child_ids = arity ? arena_alloc(scratch, sizeof(AtomId) * arity) : NULL;
    for (uint32_t i = 0; i < arity; i++) {
        ImportedBridgeExprDecodeResult child_result =
            imported_bridge_expr_to_atom_id_rec(universe, scratch, expr, expr_len,
                                                off, &child_ids[i]);
        if (child_result != IMPORTED_BRIDGE_EXPR_DECODE_OK)
            return child_result;
    }
    *out_id = tu_expr_from_ids(universe, child_ids, arity);
    if (*out_id == CETTA_ATOM_ID_NONE)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    return IMPORTED_BRIDGE_EXPR_DECODE_OK;
}

static ImportedBridgeExprDecodeResult imported_bridge_expr_to_atom_id(
    TermUniverse *universe,
    Arena *scratch,
    const uint8_t *expr,
    size_t expr_len,
    AtomId *out_id) {
    size_t off = 0;
    ImportedBridgeExprDecodeResult result =
        imported_bridge_expr_to_atom_id_rec(universe, scratch, expr, expr_len,
                                            &off, out_id);
    if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK)
        return result;
    if (off != expr_len)
        return IMPORTED_BRIDGE_EXPR_DECODE_ERROR;
    return IMPORTED_BRIDGE_EXPR_DECODE_OK;
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
    if (!dst->universe)
        return SPACE_BRIDGE_IMPORT_NEEDS_TEXT_FALLBACK;

    arena_init(&scratch);
    imported_bridge_expr_memo_init(&memo);
    space_init_with_universe(&staged, dst->universe);
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
            imported_bridge_expr_to_atom_id_cached(dst->universe, &scratch, &memo,
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

    for (uint32_t i = 0; i < staged.len; i++)
        space_add_atom_id(dst, staged.atom_ids[i]);
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
    if (out_loaded)
        *out_loaded = 0;
    SpaceBridgeImportResult result =
        space_match_backend_import_bridge_space(dst, bridge, out_loaded);
    if (result == SPACE_BRIDGE_IMPORT_OK)
        return true;
    if (result != SPACE_BRIDGE_IMPORT_NEEDS_TEXT_FALLBACK)
        return false;

    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t rows = 0;
    bool ok = cetta_mork_bridge_space_dump(bridge, &packet, &packet_len, &rows) &&
              imported_parse_dump_text_into_space(dst, persistent_arena, packet, packet_len);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    (void)rows;
    if (ok && out_loaded)
        *out_loaded = dst->len;
    return ok;
}
bool space_match_backend_attach_act_file(Space *s, const char *path, uint64_t *out_loaded) {
    PathmapImportedState *st;
    uint64_t loaded = 0;
    if (out_loaded)
        *out_loaded = 0;
    if (!s || !path)
        return false;
    if (s->match_backend.kind != SPACE_ENGINE_MORK)
        return false;
    if (space_is_ordered(s) || s->len != 0)
        return false;

    st = &s->match_backend.imported;
    imported_flat_state_clear(st);
    st->built = false;
    st->dirty = false;
    st->bridge_active = false;
    st->attached_compiled = false;
    st->attached_count = 0;

    if (!imported_ensure_bridge_space(st))
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
    st->attached_compiled = true;
    st->attached_count = (uint32_t)loaded;
    st->built = true;
    st->dirty = false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_OPEN);
    if (out_loaded)
        *out_loaded = loaded;
    return true;
}

bool space_match_backend_materialize_attached(Space *s, Arena *persistent_arena) {
    PathmapImportedState *st;
    Space *fresh = NULL;
    uint32_t logical_count = 0;
    bool ok = false;

    if (!s)
        return false;
    st = &s->match_backend.imported;
    if (!backend_uses_bridge_adapter(s))
        return !st->attached_compiled;
    if (!st->attached_compiled)
        return true;
    if (!st->bridge_active || !st->bridge_space)
        return false;
    logical_count = st->attached_count;
    fresh = cetta_malloc(sizeof(Space));
    space_init_with_universe(fresh, s ? s->universe : NULL);
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
    st->attached_compiled = false;
    st->attached_count = 0;
    if (st->bridge_space)
        (void)cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space);

    space_replace_contents(s, fresh);
    if (ok) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_MATERIALIZE);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_MATERIALIZE_ATOMS,
                                logical_count);
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

bool space_match_backend_is_attached_compiled(const Space *s) {
    return s && backend_uses_bridge_adapter(s) &&
           s->match_backend.imported.attached_compiled;
}

bool space_match_backend_bridge_space(Space *s,
                                      CettaMorkSpaceHandle **out_bridge) {
    PathmapImportedState *st;

    if (out_bridge)
        *out_bridge = NULL;
    if (!s || !backend_uses_bridge_adapter(s) ||
        space_is_ordered(s)) {
        return false;
    }

    st = &s->match_backend.imported;
    if (st->attached_compiled) {
        if (!st->bridge_space)
            return false;
        if (out_bridge)
            *out_bridge = (CettaMorkSpaceHandle *)st->bridge_space;
        return true;
    }

    if (!st->bridge_active && !imported_rebuild_bridge(s))
        return false;
    if (!st->bridge_active || !st->bridge_space)
        return false;
    if (out_bridge)
        *out_bridge = (CettaMorkSpaceHandle *)st->bridge_space;
    return true;
}

uint32_t space_match_backend_logical_len(const Space *s) {
    if (!s)
        return 0;
    if (space_engine_uses_pathmap(s->match_backend.kind))
        return imported_logical_len(s);
    return s->len;
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

    if (!bridge || !a || !query || !visitor)
        return false;

    arena_init(&scratch);
    char *pattern_text = atom_to_parseable_string(&scratch, query);
    bool ok = cetta_mork_bridge_space_query_bindings_query_only_v2(
        bridge, (const uint8_t *)pattern_text, strlen(pattern_text),
        &packet, &packet_len, &row_count);
    arena_free(&scratch);
    if (!ok)
        return false;

    imported_bridge_varmap_init(&query_vars);
    if (!imported_bridge_collect_vars(query, &query_vars)) {
        imported_bridge_varmap_free(&query_vars);
        cetta_mork_bridge_bytes_free(packet, packet_len);
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
        flags != (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                  IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES) ||
        parsed_rows != row_count) {
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
                a, packet + off, expr_len, value_env, value_flags, &value_ok);
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
    if ((flags & (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                  IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES)) !=
            (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
             IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES) ||
        (flags & ~(IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES |
                   IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS |
                   IMPORTED_MORK_MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES)) != 0 ||
        multi_ref_groups == direct_multiplicities ||
        factor_count != npatterns || parsed_rows != row_count) {
        success = false;
        goto cleanup;
    }

    for (uint32_t row = 0; row < parsed_rows && success; row++) {
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
        if (!success)
            break;

        uint32_t binding_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            success = false;
            break;
        }

        Bindings merged;
        if (seed) {
            if (!bindings_clone(&merged, seed)) {
                success = false;
                break;
            }
        } else {
            bindings_init(&merged);
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
            ImportedBridgeVarSlot *slot =
                imported_bridge_varmap_lookup(&query_vars, query_slot);
            if (!slot) {
                success = false;
                break;
            }
            bool value_ok = true;
            Atom *value = imported_bridge_parse_value_raw_query_only_v2(
                a, packet + off, expr_len, value_env, value_flags, &value_ok);
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
        bindings_free(&merged);
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

static void imported_bridge_varmap_free(ImportedBridgeVarMap *map) {
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

static Atom *imported_bridge_parse_token_bytes(Arena *a,
                                               const uint8_t *bytes,
                                               uint32_t len,
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
       longer token", so the imported fast path must refuse it and fall back. */
    if (len == 63) {
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
            a, expr + *off + 1u, sym_len, ok);
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
    Atom *value = imported_bridge_parse_value_raw_query_only_v2_rec(
        a, expr, expr_len, &off, ok);
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

static ImportedFlatBucket *imported_bucket_for_atom(PathmapImportedState *st, Atom *atom) {
    SymbolId head = atom_head_sym(atom);
    return head != SYMBOL_ID_NONE ? &st->buckets[stree_head_hash(head)] : &st->wildcard;
}

static ImportedFlatBucket *imported_bucket_for_atom_id(PathmapImportedState *st,
                                                       const TermUniverse *universe,
                                                       AtomId atom_id) {
    SymbolId head = tu_head_sym(universe, atom_id);
    return head != SYMBOL_ID_NONE ? &st->buckets[stree_head_hash(head)] : &st->wildcard;
}

static void imported_rebuild_flat(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    imported_flat_state_clear(st);
    for (uint32_t i = 0; i < s->len; i++) {
        AtomId atom_id = space_get_atom_id_at(s, i);
        if (s->universe && tu_hdr(s->universe, atom_id)) {
            ImportedFlatBuilder b = {0};
            if (imported_flatten_atom_id(&b, s->universe, atom_id)) {
                ImportedFlatBucket *bucket =
                    imported_bucket_for_atom_id(st, s->universe, atom_id);
                imported_bucket_push_builder(bucket, &b, i, stree_next_epoch());
                continue;
            }
            free(b.items);
        }
        Atom *atom = space_get_at(s, i);
        ImportedFlatBucket *bucket = imported_bucket_for_atom(st, atom);
        imported_bucket_add_entry(bucket, atom, i, stree_next_epoch());
    }
    st->bridge_active = false;
    st->attached_compiled = false;
    st->attached_count = 0;
    st->built = true;
    st->dirty = false;
}

static bool imported_ensure_bridge_space(PathmapImportedState *st) {
    if (st->bridge_space)
        return true;
    st->bridge_space = cetta_mork_bridge_space_new_pathmap();
    return st->bridge_space != NULL;
}

static bool imported_rebuild_bridge(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    if (!s->universe)
        return false;
    if (!imported_ensure_bridge_space(st))
        return false;
    if (!cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space))
        return false;

    for (uint32_t i = 0; i < s->len; i++) {
        Arena scratch;
        arena_init(&scratch);
        AtomId atom_id = space_get_atom_id_at(s, i);
        bool ok = imported_bridge_add_atom_structural(
            &scratch, (CettaMorkSpaceHandle *)st->bridge_space, s->universe,
            atom_id, NULL);
        arena_free(&scratch);
        if (!ok) {
            cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space);
            st->bridge_active = false;
            return false;
        }
    }

    imported_flat_state_clear(st);
    st->bridge_active = true;
    st->attached_compiled = false;
    st->attached_count = 0;
    st->built = true;
    st->dirty = false;
    return true;
}

static void imported_rebuild(Space *s) {
    if (backend_uses_bridge_adapter(s) && imported_rebuild_bridge(s))
        return;
    imported_rebuild_flat(s);
}

static void imported_ensure_built_flat(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    if (!st->built || st->dirty || st->bridge_active || st->attached_compiled)
        imported_rebuild_flat(s);
}

bool space_match_backend_step(Space *s, Arena *persistent_arena,
                              uint64_t steps, uint64_t *out_performed) {
    PathmapImportedState *st;
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
    if (!space_match_backend_materialize_attached(s, persistent_arena))
        return false;

    st = &s->match_backend.imported;
    if (!st->bridge_active && !imported_rebuild_bridge(s))
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
    space_init_with_universe(fresh, s ? s->universe : NULL);
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
    PathmapImportedState *st;
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
        return imported_parse_text_atoms_into_space(s, persistent_arena, text, len,
                                                    false, out_added);
    }
    if (!space_match_backend_materialize_attached(s, persistent_arena))
        return false;

    st = &s->match_backend.imported;
    if (!st->bridge_active && !imported_rebuild_bridge(s))
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
    space_init_with_universe(fresh, s ? s->universe : NULL);
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
    PathmapImportedState *st;
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
        return imported_parse_text_atoms_into_space(s, persistent_arena, text, len,
                                                    true, out_removed);
    }
    if (!space_match_backend_materialize_attached(s, persistent_arena))
        return false;

    st = &s->match_backend.imported;
    if (!st->bridge_active && !imported_rebuild_bridge(s))
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
    space_init_with_universe(fresh, s ? s->universe : NULL);
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
    PathmapImportedState *st = &s->match_backend.imported;
    if (!st->built || st->dirty)
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
    PathmapImportedState *st = &s->match_backend.imported;
    *out = NULL;
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
    if (backend_uses_bridge_adapter(s) &&
        s->match_backend.imported.attached_compiled) {
        Arena scratch;
        arena_init(&scratch);
        if (!space_match_backend_materialize_attached(
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
    PathmapImportedState *st = &s->match_backend.imported;
    ImportedFlatBuilder q = {0};
    smset_init(out);
    if (imported_logical_len(s) == 0) return;
    imported_ensure_built(s);
    imported_flatten_atom(&q, query);
    SymbolId head = atom_head_sym(query);
    if (head != SYMBOL_ID_NONE) {
        imported_collect_bucket(&st->buckets[stree_head_hash(head)],
                                q.items, q.len, s->universe, a, out);
    } else {
        for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++)
            imported_collect_bucket(&st->buckets[bi], q.items, q.len,
                                    s->universe, a, out);
    }
    imported_collect_bucket(&st->wildcard, q.items, q.len, s->universe, a, out);
    free(q.items);

    subst_matchset_normalize(out);
}

static void imported_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    PathmapImportedState *st = &s->match_backend.imported;
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
    if (!st->bridge_active) {
        imported_query_flat(s, a, query, out);
        return;
    }

    if (space_match_backend_mork_query_bindings_direct(
            (CettaMorkSpaceHandle *)st->bridge_space, a, query, &direct_matches)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V2_HIT);
        if (st->attached_compiled)
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_QUERY);
        imported_binding_set_to_exact_matches(out, &direct_matches);
        binding_set_free(&direct_matches);
        return;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V2_FALLBACK);
    if (st->attached_compiled) {
        if (!space_match_backend_materialize_attached(
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
    PathmapImportedState *st = &s->match_backend.imported;
    if (st->attached_compiled) {
        st->bridge_active = false;
        st->attached_compiled = false;
        st->attached_count = 0;
        st->built = false;
        st->dirty = false;
    }
    if (!st->built || st->dirty) return;
    if (st->bridge_active) {
        Arena scratch;
        arena_init(&scratch);
        bool ok = imported_bridge_add_atom_structural(
            &scratch, (CettaMorkSpaceHandle *)st->bridge_space, s->universe,
            atom_id, atom);
        arena_free(&scratch);
        if (ok)
            return;
        st->bridge_active = false;
        st->dirty = true;
        return;
    }
    if (tu_hdr(s->universe, atom_id)) {
        ImportedFlatBuilder b = {0};
        if (imported_flatten_atom_id(&b, s->universe, atom_id)) {
            imported_bucket_push_builder(imported_bucket_for_atom_id(st, s->universe, atom_id),
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
    return !native_atom_id_insertable(s->universe, atom_id);
}

static bool imported_needs_atom_on_add(const Space *s, AtomId atom_id) {
    const PathmapImportedState *st =
        s ? &s->match_backend.imported : NULL;
    if (!st)
        return false;
    if (st->attached_compiled)
        return false;
    if (!(st->built && !st->dirty))
        return false;
    return !tu_hdr(s->universe, atom_id);
}

static void imported_note_remove(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    (void)s;
    if (st->built) {
        st->bridge_active = false;
        st->attached_compiled = false;
        st->attached_count = 0;
        st->dirty = true;
    }
}

static void imported_free_backend(Space *s) {
    imported_state_free(&s->match_backend.imported);
}

static void native_candidate_exact_query(Space *s, Arena *a, Atom *query,
                                         SubstMatchSet *out) {
    smset_init(out);
    if (s->len == 0) return;
    uint32_t *candidates = NULL;
    uint32_t ncand = native_candidates(s, query, &candidates);
    for (uint32_t ci = 0; ci < ncand; ci++) {
        uint32_t idx = candidates[ci];
        if (idx >= s->len) continue;
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
    PathmapImportedState *st = &s->match_backend.imported;
    BindingSet direct_matches;
    if (st->attached_compiled) {
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
        if (!space_match_backend_materialize_attached(s, persist ? persist : a)) {
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
    PathmapImportedState *st = &s->match_backend.imported;
    imported_ensure_built(s);
    if (st->attached_compiled)
        return imported_logical_len(s);
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

    if (!cetta_mork_bridge_space_query_bindings_multi_ref_v3(
            (CettaMorkSpaceHandle *)s->match_backend.imported.bridge_space,
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
    if ((flags & (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                  IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES)) !=
            (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
             IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES) ||
        (flags & ~(IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES |
                   IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS |
                   IMPORTED_MORK_MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES)) != 0 ||
        multi_ref_groups == direct_multiplicities ||
        factor_count != npatterns || parsed_rows != row_count) {
        success = false;
        goto cleanup;
    }

    for (uint32_t row = 0; row < parsed_rows && success; row++) {
        uint64_t multiplicity = 1;
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
        if (!success)
            break;

        uint32_t binding_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            success = false;
            break;
        }

        Bindings merged;
        if (seed) {
            if (!bindings_clone(&merged, seed)) {
                success = false;
                break;
            }
        } else {
            bindings_init(&merged);
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
            const uint8_t *expr = packet + off;
            off += expr_len;

            ImportedBridgeVarSlot *slot =
                imported_bridge_varmap_lookup(&query_vars, query_slot);
            if (!slot) {
                success = false;
                break;
            }

            bool value_ok = true;
            Atom *value = imported_bridge_parse_value_raw_query_only_v2(
                a, expr, expr_len, value_env, value_flags, &value_ok);
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
        bindings_free(&merged);
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
    imported_ensure_built(s);
    if (!backend_uses_bridge_adapter(s)) {
        imported_query_conjunction_flat(s, a, patterns, npatterns, seed, out);
        return;
    }
    if (s->match_backend.imported.bridge_active) {
        if (imported_bridge_query_conjunction_fast(s, a, patterns, npatterns, seed, out)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V3_HIT);
            if (s->match_backend.imported.attached_compiled)
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATTACHED_ACT_QUERY);
            return;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V3_FALLBACK);
        if (s->match_backend.imported.attached_compiled) {
            if (!space_match_backend_materialize_attached(
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
    .supports_direct_bindings = true,
    .free = imported_free_backend,
    .note_add = imported_note_add,
    .note_remove = imported_note_remove,
    .candidates = imported_candidates,
    .query = imported_query,
    .query_conjunction = imported_query_conjunction,
};

/* Explicit MORK spaces keep the bridge-backed imported lane. */
static const SpaceMatchBackendOps MORK_BRIDGE_BACKEND_OPS = {
    .name = "mork",
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
    memset(&s->match_backend.imported, 0, sizeof(s->match_backend.imported));
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
        if (!space_match_backend_materialize_attached(space, persistent ? persistent : a)) {
            bindings_free(&merged);
            return false;
        }
    }

    if (sm->atom_idx >= space->len) {
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
