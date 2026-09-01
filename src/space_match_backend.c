#define _POSIX_C_SOURCE 200809L

#include "space.h"
#include "eval.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "parser.h"
#include "shared_transition.h"
#include "stats.h"
#include "generated/cetta_execution_contracts.generated.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define IMPORTED_COREF_LIMIT 144
#define IMPORTED_MORK_QUERY_ONLY_V2_MAGIC 0x43544252u
#define IMPORTED_MORK_QUERY_ONLY_V2_VERSION 5u
#define IMPORTED_MORK_MULTI_REF_V3_VERSION 6u
#define IMPORTED_MORK_CONTEXTUAL_ROWS_WIRE_VERSION 6u
#define IMPORTED_MORK_CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION 9u
#define IMPORTED_MORK_CONTEXTUAL_EXACT_ROWS_FLAGS 0x0000u
#define IMPORTED_MORK_CONTEXTUAL_QUERY_ROWS_FLAGS 0x0000u
#define IMPORTED_MORK_CONTEXTUAL_INDEXED_QUERY_ROWS_FLAGS 0x0001u
#define IMPORTED_MORK_OPEN_REF_EXACT 0u
#define IMPORTED_MORK_OPEN_REF_QUERY_SLOT 1u
#define IMPORTED_MORK_OPEN_REF_MATCHED_EXACT 2u
#define IMPORTED_MORK_OPEN_REF_MATCHED_INSTANCE 3u
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
#define IMPORTED_MORK_CURSOR_EXPR_ROW_BATCH_ROWS 65536u
#define IMPORTED_MORK_CURSOR_EXPR_ROW_BATCH_BYTES (8u * 1024u * 1024u)
#define IMPORTED_MORK_QUERY_ROW_BATCH_ROWS 65536u
#define IMPORTED_MORK_QUERY_ROW_BATCH_BYTES (8u * 1024u * 1024u)

static uint64_t pathmap_monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void pathmap_record_elapsed(CettaRuntimeCounter counter,
                                   uint64_t started_ns) {
    uint64_t finished_ns;
    (void)counter;
    if (started_ns == 0)
        return;
    finished_ns = pathmap_monotonic_ns();
    if (finished_ns >= started_ns)
        cetta_runtime_stats_add(counter, finished_ns - started_ns);
}

typedef enum {
    PATHMAP_QUERY_INDEX_MODE_UNKNOWN = -1,
    PATHMAP_QUERY_INDEX_MODE_OFF = 0,
    PATHMAP_QUERY_INDEX_MODE_AUTO = 1,
    PATHMAP_QUERY_INDEX_MODE_FORCED = 2,
} PathmapQueryIndexMode;

static PathmapQueryIndexMode pathmap_indexed_query_mode(void) {
    static _Thread_local PathmapQueryIndexMode mode =
        PATHMAP_QUERY_INDEX_MODE_UNKNOWN;
    if (mode == PATHMAP_QUERY_INDEX_MODE_UNKNOWN) {
        const char *value = getenv("CETTA_PATHMAP_QUERY_INDEX");
        if (!value || !*value) {
            mode = PATHMAP_QUERY_INDEX_MODE_AUTO;
        } else if (strcmp(value, "0") == 0 ||
                   strcmp(value, "false") == 0 ||
                   strcmp(value, "off") == 0 ||
                   strcmp(value, "no") == 0) {
            mode = PATHMAP_QUERY_INDEX_MODE_OFF;
        } else {
            mode = PATHMAP_QUERY_INDEX_MODE_FORCED;
        }
    }
    return mode;
}

static bool pathmap_indexed_query_enabled(void) {
    return pathmap_indexed_query_mode() != PATHMAP_QUERY_INDEX_MODE_OFF;
}

/* The flat catalog duplicates relation rows and access paths.  In automatic
 * mode reserve that cost for conjunction planning; a one-factor query can
 * descend the counted PathMap directly.  Explicit enablement remains useful
 * for mechanism tests and workload-specific experiments. */
static bool pathmap_indexed_single_factor_enabled(void) {
    return pathmap_indexed_query_mode() == PATHMAP_QUERY_INDEX_MODE_FORCED;
}

static bool pathmap_indexed_factor_count_enabled(CettaExprLen factor_count) {
    return factor_count > 1u ? pathmap_indexed_query_enabled()
                             : pathmap_indexed_single_factor_enabled();
}

static bool pathmap_batch_mutation_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("CETTA_PATHMAP_BATCH_MUTATION");
        enabled = !value || !*value ||
                  (strcmp(value, "0") != 0 &&
                   strcmp(value, "false") != 0 &&
                   strcmp(value, "off") != 0 &&
                   strcmp(value, "no") != 0);
    }
    return enabled != 0;
}

static bool pathmap_local_visit_bindings_indexed(
    Space *s,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx,
    bool *out_attempted);
static SpaceMatchPullVisitResult
pathmap_local_visit_conjunction_indexed(
    Space *s,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx);

static bool pathmap_indexed_cursor_stat(
    const CettaMorkQueryCursorHandle *cursor,
    CettaMorkIndexedCursorStat stat,
    CettaRuntimeCounter counter) {
    uint64_t value = 0;
    if (!cetta_mork_bridge_query_cursor_indexed_stat(cursor, stat, &value))
        return false;
    cetta_runtime_stats_add(counter, value);
    return true;
}

static bool pathmap_indexed_cursor_is_pure_residual(
    const CettaMorkQueryCursorHandle *cursor,
    bool *out_pure_residual) {
    uint64_t has_residual = 0u;
    uint64_t has_exact_partition = 0u;
    if (!cursor || !out_pure_residual ||
        !cetta_mork_bridge_query_cursor_indexed_stat(
            cursor, CETTA_MORK_INDEXED_CURSOR_STAT_HAS_RESIDUAL,
            &has_residual) ||
        !cetta_mork_bridge_query_cursor_indexed_stat(
            cursor, CETTA_MORK_INDEXED_CURSOR_STAT_HAS_EXACT_PARTITION,
            &has_exact_partition)) {
        return false;
    }
    *out_pure_residual = has_residual != 0u && has_exact_partition == 0u;
    return true;
}

static bool pathmap_indexed_cursor_rows_available(
    const CettaMorkQueryCursorHandle *cursor,
    bool *out_rows_available) {
    uint64_t rows_available = 0u;
    if (!cursor || !out_rows_available ||
        !cetta_mork_bridge_query_cursor_indexed_stat(
            cursor, CETTA_MORK_INDEXED_CURSOR_STAT_ROWS_AVAILABLE,
            &rows_available)) {
        return false;
    }
    *out_rows_available = rows_available != 0u;
    return true;
}

static void pathmap_record_indexed_query_stats(
    const CettaMorkQueryCursorHandle *cursor) {
    if (!cursor || !cetta_runtime_stats_is_enabled())
        return;

    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_QUERY);
    bool ok = true;
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_HAS_RESIDUAL,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_RESIDUAL_QUERY);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_CATALOG_BUILDS,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_CATALOG_BUILD);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_CATALOG_ROWS_SCANNED,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_CATALOG_ROW_SCAN);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_ACCESS_PATH_BUILDS,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_ACCESS_PATH_BUILD);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_ACCESS_PATH_ROWS_INDEXED,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_ACCESS_PATH_ROW);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_PLAN_BUILDS,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_PLAN_BUILD);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_PLAN_CACHE_HITS,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_PLAN_CACHE_HIT);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_TRIE_SEEKS,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_TRIE_SEEK);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_TRIE_DESCENTS,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_TRIE_DESCENT);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_ROWS_EMITTED,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_ROW_EMIT);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_ROWS_AGGREGATED,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_ROW_AGGREGATE);
    ok &= pathmap_indexed_cursor_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_REPLAY_HIT,
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_REPLAY_HIT);

    uint64_t frame_cells = 0;
    ok &= cetta_mork_bridge_query_cursor_indexed_stat(
        cursor, CETTA_MORK_INDEXED_CURSOR_STAT_MAX_FRAME_CELLS,
        &frame_cells);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_FRAME_CELL_PEAK, frame_cells);
    if (!ok)
        space_match_backend_clear_error();
}

static int cmp_cetta_index(const void *a, const void *b) {
    CettaIndex va = *(const CettaIndex *)a, vb = *(const CettaIndex *)b;
    return (va > vb) - (va < vb);
}

static bool pathmap_local_ensure_bridge_space(PathmapLocalState *st);
static bool mork_imported_ensure_bridge_space(MorkImportedState *st);
static bool backend_rebuild_bridge(Space *s);
static uint64_t imported_logical_len(const Space *s);
static bool pathmap_local_count_conjunction(
    Space *s, Arena *a, Atom **patterns, CettaExprLen npatterns,
    const Bindings *seed, uint64_t *out_count);
static void native_candidate_exact_query(Space *s, Arena *a, Atom *query,
                                         SubstMatchSet *out);
static void imported_epoch_query_candidates(Space *s, Arena *a, Atom *query,
                                            SubstMatchSet *out);
static bool imported_bridge_query_conjunction_fast(Space *s, Arena *a,
                                                   Atom **patterns, CettaExprLen npatterns,
                                                   const Bindings *seed,
                                                   BindingSet *out,
                                                   bool indexed_only);
static bool mork_query_collect_bindings(const Bindings *bindings, void *ctx);
static bool mork_visit_collected_bindings(const BindingSet *set,
                                          CettaMorkBindingsVisitor visitor,
                                          void *ctx);
static bool imported_bridge_query_text_has_internal_vars(const char *text);
static bool imported_text_may_contain_vars(const uint8_t *text, size_t len);
static bool imported_materialize_bridge_space(Space *dst,
                                              Arena *persistent_arena,
                                              CettaMorkSpaceHandle *bridge,
                                              uint64_t *out_loaded);
static void space_query_conjunction_default(Space *s, Arena *a,
                                            Atom **patterns, CettaExprLen npatterns,
                                            const Bindings *seed,
                                            BindingSet *out);
static void imported_projection_clear(ImportedBridgeState *st);
static bool imported_storage_ensure_projection(Space *s);
static bool imported_shadow_refresh_from_projection(Space *s);
static bool bridge_space_logical_len64(const ImportedBridgeState *st,
                                       uint64_t *out_logical);
static bool pathmap_local_ensure_bridge_live(Space *s);
static void imported_mark_bridge_untrusted(Space *s);
static bool pathmap_local_apply_text_chunk_direct(Space *s,
                                                  const uint8_t *text,
                                                  size_t len,
                                                  bool remove_atoms,
                                                  uint64_t *out_changed);
static bool imported_bridge_packet_count_ok(uint64_t count);
static bool imported_bridge_packet_count_sum_ok(uint64_t total,
                                                uint64_t add);
static AtomId space_match_backend_candidate_atom_id_at64(
    const Space *s, CettaIndex idx);

static CettaIndex sort_unique_cetta_index(CettaIndex *items, CettaIndex len) {
    if (!items || len <= 1)
        return len;
    qsort(items, len, sizeof(CettaIndex), cmp_cetta_index);
    CettaIndex w = 1;
    for (CettaIndex r = 1; r < len; r++) {
        if (items[r] != items[r - 1])
            items[w++] = items[r];
    }
    return w;
}

static bool imported_bridge_packet_count_ok(uint64_t count) {
    if (count > space_match_backend_packet_materialization_limit()) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        return false;
    }
    return true;
}

static bool imported_bridge_packet_count_sum_ok(uint64_t total,
                                                uint64_t add) {
    uint64_t limit = space_match_backend_packet_materialization_limit();
    if (total > limit || add > limit - total) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        return false;
    }
    return true;
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
    CettaExprLen arity = tu_arity(universe, atom_id);
    for (CettaExprIndex i = 0; i < arity; i++) {
        if (!native_atom_id_insertable(universe, tu_child(universe, atom_id, i)))
            return false;
    }
    return true;
}

static void native_insert_match_trie_entry(Space *s, CettaIndex atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    AtomId atom_id =
        space_match_backend_candidate_atom_id_at64(s, atom_idx);
    if (native_atom_id_insertable(s->native.universe, atom_id) &&
        disc_insert_id(st->match_trie, s->native.universe, atom_id, atom_idx)) {
        return;
    }
    Atom *atom = space_match_backend_candidate_at64(s, atom_idx);
    if (atom)
        disc_insert(st->match_trie, atom, atom_idx);
}

static void native_insert_stree_entry(Space *s, CettaIndex atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    /* Ground-term sharing (doctrine: inner loops speak IDs, not copies): build
     * the discrimination tree directly from the interned atom_id, walking the
     * universe's compact storage, instead of materialising a fresh decoded Atom
     * per fact.  The tree only reads structure (it stores discrimination nodes,
     * not the atom), so this is byte-for-byte the same tree.  Guarded to spaces
     * without an overlay base, whose atoms would belong to a different universe
     * than s->native.universe. */
    if (s->overlay_base == NULL) {
        AtomId atom_id =
            space_match_backend_candidate_atom_id_at64(s, atom_idx);
        if (atom_id != CETTA_ATOM_ID_NONE &&
            stree_insert_id(st->stree, s->native.universe, atom_id, atom_idx))
            return;
    }
    Atom *atom = space_match_backend_candidate_at64(s, atom_idx);
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
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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

static void subst_matchset_push(SubstMatchSet *out, CettaIndex atom_idx,
                                uint32_t epoch, const Bindings *bindings,
                                bool exact) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_SMSET_ROWS);
    if (out->len >= out->cap) {
        if (out->cap > UINT64_MAX / 2u) {
            fputs("CeTTa: substitution match-set capacity exhausted\n",
                  stderr);
            abort();
        }
        CettaIndex next_cap = out->cap ? out->cap * 2 : 8;
        if (next_cap > SIZE_MAX / sizeof(SubstMatch)) {
            fputs("CeTTa: substitution match-set capacity exhausted\n",
                  stderr);
            abort();
        }
        if (out->items == out->inline_items) {
            SubstMatch *next =
                cetta_malloc(sizeof(SubstMatch) * (size_t)next_cap);
            if (out->len > 0) {
                memcpy(next, out->items,
                       sizeof(SubstMatch) * (size_t)out->len);
            }
            out->items = next;
        } else {
            out->items =
                cetta_realloc(out->items, sizeof(SubstMatch) * (size_t)next_cap);
        }
        out->cap = next_cap;
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

static inline bool subst_match_order_le(const SubstMatch *a,
                                        const SubstMatch *b) {
    if (a->atom_idx != b->atom_idx)
        return a->atom_idx < b->atom_idx;
    return a->epoch <= b->epoch;  /* ties keep left-run order (stable) */
}

/* Stable O(n log n) sort by (atom_idx, epoch) via bottom-up merge, using the
 * ownership-transferring subst_match_move so Bindings resources are never
 * duplicated or dropped.  Replaces an insertion sort that was O(n^2) whenever
 * the discrimination-tree walk did not already emit atoms in index order --
 * which stopped holding once high-fan-out int children became hash-indexed, and
 * was never guaranteed for any match that visits variable branches. */
static void subst_matchset_sort(SubstMatch *items, CettaIndex len,
                                SubstMatch *tmp) {
    SubstMatch *src = items;
    SubstMatch *dst = tmp;
    for (CettaIndex width = 1; width < len;) {
        for (CettaIndex i = 0; i < len;) {
            CettaIndex left_len = len - i < width ? len - i : width;
            CettaIndex mid = i + left_len;
            CettaIndex right_len = len - mid < width ? len - mid : width;
            CettaIndex end = mid + right_len;
            CettaIndex a = i, b = mid, w = i;
            while (a < mid && b < end) {
                if (subst_match_order_le(&src[a], &src[b]))
                    subst_match_move(&dst[w++], &src[a++]);
                else
                    subst_match_move(&dst[w++], &src[b++]);
            }
            while (a < mid) subst_match_move(&dst[w++], &src[a++]);
            while (b < end) subst_match_move(&dst[w++], &src[b++]);
            i = end;
        }
        SubstMatch *t = src; src = dst; dst = t;
        if (width > len / 2u)
            break;
        width *= 2u;
    }
    if (src != items)
        for (CettaIndex i = 0; i < len; i++)
            subst_match_move(&items[i], &src[i]);
}

static void subst_matchset_normalize(SubstMatchSet *out) {
    if (out->len <= 1)
        return;
    if (out->len > SIZE_MAX / sizeof(SubstMatch)) {
        fputs("CeTTa: substitution match-set capacity exhausted\n", stderr);
        abort();
    }
    SubstMatch *tmp =
        cetta_malloc(sizeof(SubstMatch) * (size_t)out->len);
    subst_matchset_sort(out->items, out->len, tmp);
    free(tmp);
    CettaIndex w = 1;
    for (CettaIndex r = 1; r < out->len; r++) {
        /* Compare with the last RETAINED row, not the previous source row.
         * Once an earlier duplicate has been removed, moving a later unique
         * row left empties its source Bindings; r-1 is then no longer a valid
         * representative for the next duplicate run. */
        if (!subst_match_same(&out->items[r], &out->items[w - 1u])) {
            if (w != r)
                subst_match_move(&out->items[w], &out->items[r]);
            w++;
        } else {
            bindings_free(&out->items[r].bindings);
        }
    }
    out->len = w;
}

void space_match_backend_diag_normalize_subst_matches(SubstMatchSet *matches) {
    if (matches)
        subst_matchset_normalize(matches);
}

static void native_rebuild_match_trie(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_NATIVE_PATTERN_INDEX_DIRTY_REBUILD);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_NATIVE_PATTERN_INDEX_BUILD_ROW,
        s->native.len);
    disc_node_free(st->match_trie);
    st->match_trie = disc_node_new();
    for (CettaIndex i = 0; i < s->native.len; i++)
        native_insert_match_trie_entry(s, i);
    st->match_trie_dirty = false;
    st->match_trie_stale_occurrences = 0u;
}

static void native_ensure_match_trie(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->match_trie) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_NATIVE_PATTERN_INDEX_COLD_BUILD);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_NATIVE_PATTERN_INDEX_BUILD_ROW,
            s->native.len);
        st->match_trie = disc_node_new();
        for (CettaIndex i = 0; i < s->native.len; i++)
            native_insert_match_trie_entry(s, i);
        st->match_trie_dirty = false;
        st->match_trie_stale_occurrences = 0u;
    } else if (st->match_trie_dirty) {
        native_rebuild_match_trie(s);
    }
}

static void native_ensure_stree(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->stree) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_NATIVE_SUBSTITUTION_INDEX_COLD_BUILD);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_NATIVE_SUBSTITUTION_INDEX_BUILD_ROW,
            s->native.len);
        st->stree = cetta_malloc(sizeof(SubstTree));
        stree_init(st->stree);
        for (CettaIndex i = 0; i < s->native.len; i++)
            native_insert_stree_entry(s, i);
        st->stree_dirty = false;
        st->stree_stale_occurrences = 0u;
    } else if (st->stree_dirty) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_NATIVE_SUBSTITUTION_INDEX_DIRTY_REBUILD);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_NATIVE_SUBSTITUTION_INDEX_BUILD_ROW,
            s->native.len);
        stree_free(st->stree);
        stree_init(st->stree);
        for (CettaIndex i = 0; i < s->native.len; i++)
            native_insert_stree_entry(s, i);
        st->stree_dirty = false;
        st->stree_stale_occurrences = 0u;
    }
}

static void native_free(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    disc_node_free(st->match_trie);
    st->match_trie = NULL;
    st->match_trie_dirty = false;
    st->match_trie_stale_occurrences = 0u;
    if (st->stree) {
        stree_free(st->stree);
        free(st->stree);
        st->stree = NULL;
    }
    st->stree_dirty = false;
    st->stree_stale_occurrences = 0u;
}

static void native_note_add(Space *s, AtomId atom_id, Atom *atom,
                            CettaIndex atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (st->match_trie && !st->match_trie_dirty) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_NATIVE_PATTERN_INDEX_INCREMENTAL_ADD);
        if (!(native_atom_id_insertable(s->native.universe, atom_id) &&
              disc_insert_id(st->match_trie, s->native.universe, atom_id,
                             atom_idx)) &&
            atom) {
            disc_insert(st->match_trie, atom, atom_idx);
        }
    }
    if (st->stree && !st->stree_dirty) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_NATIVE_SUBSTITUTION_INDEX_INCREMENTAL_ADD);
        if (atom) {
            stree_insert(st->stree, atom, atom_idx);
        } else {
            /* Stable AtomId appends, including synchronized PathMap shadow
             * appends, can extend the live tree without rebuilding it. */
            native_insert_stree_entry(s, atom_idx);
        }
    }
}

static void native_note_remove(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    (void)s;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_NATIVE_MATCH_INDEX_REMOVE_NOTE);
    if ((st->match_trie && !st->match_trie_dirty) ||
        (st->stree && !st->stree_dirty)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_NATIVE_MATCH_INDEX_REMOVE_DIRTY_TRANSITION);
    }
    st->match_trie_dirty = true;
    st->stree_dirty = true;
    st->match_trie_stale_occurrences = 0u;
    st->stree_stale_occurrences = 0u;
}

static bool stable_occurrence_transport_is_valid(
        const SpaceStableOccurrenceTransport *transport) {
    if (!transport ||
        (!transport->source_to_target && transport->source_len != 0u) ||
        transport->target_len > transport->source_len) {
        return false;
    }
    CettaIndex next_target = 0u;
    for (CettaIndex source = 0u;
         source < transport->source_len; source++) {
        CettaIndex target = transport->source_to_target[source];
        if (target == SPACE_OCCURRENCE_COORDINATE_REMOVED)
            continue;
        if (target != next_target)
            return false;
        next_target++;
    }
    return next_target == transport->target_len;
}

static bool native_stale_index_requires_rebuild(
        CettaCount stale, CettaCount removed, CettaCount target_len) {
    if (removed > UINT64_MAX - stale)
        return true;
    return stale + removed >= target_len;
}

static SpaceBackendBatchResult
native_transport_stable_occurrence_coordinates(
        Space *s, const SpaceStableOccurrenceTransport *transport) {
    if (!s || !stable_occurrence_transport_is_valid(transport) ||
        transport->source_len != s->native.len) {
        return SPACE_BACKEND_BATCH_ERROR;
    }

    SpaceMatchNativeState *st = &s->match_backend.native;
    CettaCount removed = transport->source_len - transport->target_len;

    /* Small spaces are queried linearly.  Releasing an old realized tree is
     * both exact and better than retaining dead structural branches that no
     * future small-space query would visit. */
    if (transport->target_len <= MATCH_TRIE_THRESHOLD) {
        if (st->match_trie) {
            disc_node_free(st->match_trie);
            st->match_trie = NULL;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_NATIVE_STALE_INDEX_SMALL_SPACE_RELEASE);
        }
        st->match_trie_dirty = false;
        st->match_trie_stale_occurrences = 0u;
        if (st->stree) {
            stree_free(st->stree);
            free(st->stree);
            st->stree = NULL;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_NATIVE_STALE_INDEX_SMALL_SPACE_RELEASE);
        }
        st->stree_dirty = false;
        st->stree_stale_occurrences = 0u;
        return SPACE_BACKEND_BATCH_APPLIED;
    }

    if (st->match_trie && !st->match_trie_dirty) {
        if (native_stale_index_requires_rebuild(
                st->match_trie_stale_occurrences, removed,
                transport->target_len)) {
            st->match_trie_dirty = true;
            st->match_trie_stale_occurrences = 0u;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_NATIVE_STALE_INDEX_AMORTIZED_REBUILD);
        } else {
            CettaCount removed_leaves = disc_transport_stable_coordinates(
                st->match_trie, transport->source_to_target,
                transport->source_len);
            st->match_trie_stale_occurrences += removed_leaves;
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_REMOVED_LEAF,
                removed_leaves);
        }
    }

    if (st->stree && !st->stree_dirty) {
        if (native_stale_index_requires_rebuild(
                st->stree_stale_occurrences, removed,
                transport->target_len)) {
            st->stree_dirty = true;
            st->stree_stale_occurrences = 0u;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_NATIVE_STALE_INDEX_AMORTIZED_REBUILD);
        } else {
            CettaCount removed_leaves = stree_transport_stable_coordinates(
                st->stree, transport->source_to_target,
                transport->source_len);
            st->stree_stale_occurrences += removed_leaves;
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_REMOVED_LEAF,
                removed_leaves);
        }
    }
    return SPACE_BACKEND_BATCH_APPLIED;
}

static bool match_space_atom_epoch(Space *s, CettaIndex atom_idx, Atom *query,
                                   Bindings *b, Arena *a, uint32_t epoch) {
    if (!s || !query || !b || atom_idx >= s->native.len)
        return false;
    AtomId atom_id = space_get_atom_id_at64(s, atom_idx);
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
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint8_t *expr_bytes = NULL;
    size_t expr_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && universe && tu_hdr(universe, atom_id)) {
        ok = cetta_mm2_atom_id_to_bridge_expr_packet(
            scratch, universe, atom_id, &packet, &packet_len, &encode_error);
    } else if (bridge_space && fallback_atom) {
        ok = cetta_mm2_atom_to_bridge_expr_packet(
            scratch, fallback_atom, &packet, &packet_len, &encode_error);
    } else if (bridge_space && universe && atom_id != CETTA_ATOM_ID_NONE) {
        Atom *decoded = term_universe_get_atom(universe, atom_id);
        if (decoded) {
            ok = cetta_mm2_atom_to_bridge_expr_packet(
                scratch, decoded, &packet, &packet_len, &encode_error);
        }
    }

    if (ok) {
        ok = cetta_mork_bridge_space_normalize_expr_packet(
            bridge_space, packet, packet_len, &expr_bytes, &expr_len);
    }
    free(packet);
    if (ok && imported_bridge_add_atom_bytes(
                  bridge_space, expr_bytes, expr_len)) {
        cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
        return true;
    }
    cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
    return false;
}

static bool imported_bridge_add_atom_contextual_exact(Arena *scratch,
                                                      CettaMorkSpaceHandle *bridge_space,
                                                      const TermUniverse *universe,
                                                      AtomId atom_id) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint8_t *expr_bytes = NULL;
    uint8_t *context_bytes = NULL;
    size_t expr_len = 0;
    size_t context_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (!bridge_space || !universe || !tu_hdr(universe, atom_id))
        return false;

    ok = cetta_mm2_atom_id_to_contextual_bridge_expr_packet(
        scratch, universe, atom_id, &packet, &packet_len, &context_bytes,
        &context_len, &encode_error);
    if (ok) {
        ok = cetta_mork_bridge_space_normalize_expr_packet(
            bridge_space, packet, packet_len, &expr_bytes, &expr_len);
    }
    free(packet);
    if (ok) {
        ok = cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
            bridge_space, expr_bytes, expr_len, context_bytes, context_len, NULL);
    }
    cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
    free(context_bytes);
    return ok;
}

static bool imported_bridge_add_atom_contextual_exact_atom(
    Arena *scratch,
    CettaMorkSpaceHandle *bridge_space,
    Atom *atom) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint8_t *expr_bytes = NULL;
    uint8_t *context_bytes = NULL;
    size_t expr_len = 0;
    size_t context_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && atom) {
        ok = cetta_mm2_atom_to_contextual_bridge_expr_packet(
            scratch, atom, &packet, &packet_len, &context_bytes,
            &context_len, &encode_error);
    }
    if (ok) {
        ok = cetta_mork_bridge_space_normalize_expr_packet(
            bridge_space, packet, packet_len, &expr_bytes, &expr_len);
    }
    free(packet);

    if (ok && cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
                  bridge_space, expr_bytes, expr_len, context_bytes,
                  context_len, NULL)) {
        cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
        free(context_bytes);
        return true;
    }
    cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
    free(context_bytes);
    return false;
}

static bool imported_bridge_remove_atom_structural(Arena *scratch,
                                                   CettaMorkSpaceHandle *bridge_space,
                                                   const TermUniverse *universe,
                                                   AtomId atom_id,
                                                   Atom *fallback_atom,
                                                   uint64_t *out_removed) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint8_t *expr_bytes = NULL;
    size_t expr_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && universe && tu_hdr(universe, atom_id)) {
        ok = cetta_mm2_atom_id_to_bridge_expr_packet(
            scratch, universe, atom_id, &packet, &packet_len, &encode_error);
    } else if (bridge_space && fallback_atom) {
        ok = cetta_mm2_atom_to_bridge_expr_packet(
            scratch, fallback_atom, &packet, &packet_len, &encode_error);
    } else if (bridge_space && universe && atom_id != CETTA_ATOM_ID_NONE) {
        Atom *decoded = term_universe_get_atom(universe, atom_id);
        if (decoded) {
            ok = cetta_mm2_atom_to_bridge_expr_packet(
                scratch, decoded, &packet, &packet_len, &encode_error);
        }
    }

    if (ok) {
        ok = cetta_mork_bridge_space_normalize_expr_packet(
            bridge_space, packet, packet_len, &expr_bytes, &expr_len);
    }
    free(packet);
    if (ok && cetta_mork_bridge_space_remove_expr_bytes(
                  bridge_space, expr_bytes, expr_len, out_removed)) {
        cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
        return true;
    }
    cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
    return false;
}

static Atom *imported_opening_restore_source_vars(Arena *a, Atom *atom,
                                                  bool *changed);

static bool imported_bridge_remove_atom_contextual_exact(Arena *scratch,
                                                         CettaMorkSpaceHandle *bridge_space,
                                                         const TermUniverse *universe,
                                                         AtomId atom_id,
                                                         uint64_t *out_removed) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint8_t *expr_bytes = NULL;
    uint8_t *context_bytes = NULL;
    size_t expr_len = 0;
    size_t context_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && universe && tu_hdr(universe, atom_id)) {
        Atom *decoded = term_universe_get_atom(universe, atom_id);
        bool restored = false;
        Atom *source_atom = decoded
            ? imported_opening_restore_source_vars(
                  scratch, decoded, &restored)
            : NULL;
        if (!source_atom) {
            ok = false;
        } else if (restored) {
            ok = cetta_mm2_atom_to_contextual_bridge_expr_packet(
                scratch, source_atom, &packet, &packet_len, &context_bytes,
                &context_len, &encode_error);
        } else {
            ok = cetta_mm2_atom_id_to_contextual_bridge_expr_packet(
                scratch, universe, atom_id, &packet, &packet_len,
                &context_bytes, &context_len, &encode_error);
        }
    }

    if (ok) {
        ok = cetta_mork_bridge_space_normalize_expr_packet(
            bridge_space, packet, packet_len, &expr_bytes, &expr_len);
    }
    free(packet);
    if (ok && cetta_mork_bridge_space_remove_contextual_exact_expr_bytes(
                  bridge_space, expr_bytes, expr_len, context_bytes,
                  context_len, out_removed)) {
        cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
        free(context_bytes);
        return true;
    }
    cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
    free(context_bytes);
    return false;
}

static bool imported_bridge_remove_atom_contextual_exact_atom(Arena *scratch,
                                                              CettaMorkSpaceHandle *bridge_space,
                                                              Atom *atom,
                                                              uint64_t *out_removed) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint8_t *expr_bytes = NULL;
    uint8_t *context_bytes = NULL;
    size_t expr_len = 0;
    size_t context_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (bridge_space && atom) {
        bool restored = false;
        Atom *source_atom = imported_opening_restore_source_vars(
            scratch, atom, &restored);
        if (!source_atom)
            return false;
        ok = cetta_mm2_atom_to_contextual_bridge_expr_packet(
            scratch, source_atom, &packet, &packet_len, &context_bytes,
            &context_len, &encode_error);
    }
    if (ok) {
        ok = cetta_mork_bridge_space_normalize_expr_packet(
            bridge_space, packet, packet_len, &expr_bytes, &expr_len);
    }
    free(packet);

    if (ok && cetta_mork_bridge_space_remove_contextual_exact_expr_bytes(
                  bridge_space, expr_bytes, expr_len, context_bytes,
                  context_len, out_removed)) {
        cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
        free(context_bytes);
        return true;
    }
    cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
    free(context_bytes);
    return false;
}

static bool imported_bridge_contains_atom_structural(Arena *scratch,
                                                     CettaMorkSpaceHandle *bridge_space,
                                                     Atom *atom,
                                                     bool *out_found) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint8_t *expr_bytes = NULL;
    size_t expr_len = 0;
    const char *encode_error = NULL;
    bool ok = false;

    if (out_found)
        *out_found = false;
    if (!scratch || !bridge_space || !atom)
        return false;

    ok = cetta_mm2_atom_to_bridge_expr_packet(
        scratch, atom, &packet, &packet_len, &encode_error);
    if (ok) {
        ok = cetta_mork_bridge_space_normalize_expr_packet(
            bridge_space, packet, packet_len, &expr_bytes, &expr_len);
    }
    free(packet);
    if (!ok) {
        cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
        return false;
    }

    ok = cetta_mork_bridge_space_contains_expr_bytes(
        bridge_space, expr_bytes, expr_len, out_found);
    cetta_mork_bridge_bytes_free(expr_bytes, expr_len);
    return ok;
}

static void imported_binding_set_to_exact_matches(SubstMatchSet *out,
                                                  const BindingSet *matches) {
    smset_init(out);
    if (!matches)
        return;
    for (CettaIndex i = 0; i < matches->len; i++)
        subst_matchset_push(out, 0, 0, &matches->items[i], true);
}

static CettaIndex native_candidates(Space *s, Atom *pattern, CettaIndex **out) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (s->native.len <= MATCH_TRIE_THRESHOLD) {
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_NATIVE_CANDIDATES,
                                s->native.len);
        *out = cetta_malloc(sizeof(CettaIndex) * (s->native.len ? s->native.len : 1));
        for (CettaIndex i = 0; i < s->native.len; i++) (*out)[i] = i;
        return s->native.len;
    }
    native_ensure_match_trie(s);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_NATIVE_TRIE_LOOKUP);
    CettaIndex ncand = 0, ccand = 0;
    disc_lookup(st->match_trie, pattern, out, &ncand, &ccand);
    if (ncand > 1) {
        ncand = sort_unique_cetta_index(*out, ncand);
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_NATIVE_CANDIDATES, ncand);
    return ncand;
}

static bool native_pattern_is_flat_linear(Atom *pattern) {
    if (!pattern || pattern->kind != ATOM_EXPR ||
        pattern->expr.len == 0u) {
        return false;
    }
    if ((size_t)pattern->expr.len >
        SIZE_MAX / sizeof(VarId)) {
        return false;
    }
    VarId *seen = cetta_malloc(
        (size_t)pattern->expr.len * sizeof(*seen));
    size_t seen_len = 0u;
    bool admissible = true;
    for (CettaExprIndex index = 0u;
         index < pattern->expr.len; index++) {
        Atom *item = pattern->expr.elems[index];
        if (!item) {
            admissible = false;
            break;
        }
        if (item->kind != ATOM_VAR) {
            if (atom_has_vars(item)) {
                admissible = false;
                break;
            }
            continue;
        }
        for (size_t previous = 0u;
             previous < seen_len; previous++) {
            if (seen[previous] == item->var_id) {
                admissible = false;
                break;
            }
        }
        if (!admissible)
            break;
        seen[seen_len++] = item->var_id;
    }
    free(seen);
    return admissible;
}

/*
 * For a flat linear query and a ground stored expression, matching reduces
 * to positional equality at every non-variable column.  Variables are
 * independent wildcards, so the exact bag count can be obtained from stored
 * AtomIds without allocating bindings, fresh variables, or decoded rows.
 *
 * The uncommon variable-bearing and fallback-encoded candidates still use
 * the ordinary matcher individually.  Thus stored-side correlations and
 * contextual grounded values remain exact without forcing every ordinary
 * ground row through binding construction.
 */
static bool native_count_flat_linear(
    Space *s, Arena *scratch, Atom *pattern,
    uint64_t *count, CettaIndex *examined) {
    if (count)
        *count = 0u;
    if (examined)
        *examined = 0u;
    if (!s || !scratch || !pattern || !count || !examined ||
        s->overlay_base ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind !=
             SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !s->native.universe ||
        !native_pattern_is_flat_linear(pattern)) {
        return false;
    }

    /*
     * A cold one-shot aggregate must not build and decode the complete
     * discrimination trie merely to avoid a compact AtomId scan.  Reuse a
     * clean index when one already exists; otherwise scan the authoritative
     * logical sequence.  Query-driven positional indexes can later replace
     * repeated scans without changing this semantic seam.
     */
    SpaceMatchNativeState *state =
        &s->match_backend.native;
    bool indexed =
        state->match_trie && !state->match_trie_dirty;
    CettaIndex *candidates = NULL;
    CettaIndex candidate_len = indexed
        ? native_candidates(s, pattern, &candidates)
        : s->native.len;
    if (!indexed) {
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_MATCH_NATIVE_CANDIDATES,
            candidate_len);
    }
    uint64_t matches = 0u;
    bool supported = true;
    for (CettaIndex position = 0u;
         position < candidate_len; position++) {
        CettaIndex logical_index = indexed
            ? candidates[position] : position;
        if (logical_index >= s->native.len) {
            supported = false;
            break;
        }
        AtomId candidate_id =
            space_get_atom_id_at64(s, logical_index);
        const CettaTermHdr *header =
            tu_hdr(s->native.universe, candidate_id);
        if (!header) {
            Atom *candidate =
                space_get_at64(s, logical_index);
            Bindings bindings;
            bindings_init(&bindings);
            bool matched = candidate &&
                match_atoms_epoch(
                    pattern, candidate, &bindings,
                    scratch, fresh_var_suffix()) &&
                !bindings_has_loop(&bindings);
            bindings_free(&bindings);
            if (matched) {
                if (matches == UINT64_MAX) {
                    supported = false;
                    break;
                }
                matches++;
            }
            continue;
        }
        if (tu_kind(s->native.universe, candidate_id) !=
                ATOM_EXPR ||
            tu_arity(s->native.universe, candidate_id) !=
                pattern->expr.len) {
            continue;
        }

        if (tu_has_vars(s->native.universe, candidate_id)) {
            Bindings bindings;
            bindings_init(&bindings);
            bool matched = match_atoms_atom_id_epoch(
                pattern, s->native.universe, candidate_id,
                &bindings, scratch, fresh_var_suffix()) &&
                !bindings_has_loop(&bindings);
            bindings_free(&bindings);
            if (matched) {
                if (matches == UINT64_MAX) {
                    supported = false;
                    break;
                }
                matches++;
            }
            continue;
        }

        bool matches_candidate = true;
        for (CettaExprIndex index = 0u;
             index < pattern->expr.len; index++) {
            Atom *item = pattern->expr.elems[index];
            if (item->kind == ATOM_VAR)
                continue;
            AtomId child = tu_child(
                s->native.universe, candidate_id, index);
            if (child == CETTA_ATOM_ID_NONE ||
                !term_universe_atom_id_eq(
                    s->native.universe, child, item)) {
                matches_candidate = false;
                break;
            }
        }
        if (matches_candidate) {
            if (matches == UINT64_MAX) {
                supported = false;
                break;
            }
            matches++;
        }
    }
    free(candidates);
    if (!supported)
        return false;
    *count = matches;
    *examined = candidate_len;
    return true;
}

static void native_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_NATIVE_PROBE);
    smset_init(out);
    if (s->native.len == 0) return;
    if (s->native.len <= MATCH_TRIE_THRESHOLD) {
        for (CettaIndex i = 0; i < s->native.len; i++) {
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
    CettaIndex len, cap;
} ImportedFlatBuilder;

typedef struct {
    VarId var_id;
    SymbolId spelling;
    CettaIndex idx;
    CettaIndex span;
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
    uint8_t source_env;
    uint32_t opening_instance;
    uint16_t source_slot;
    VarId var_id;
    SymbolId spelling;
    uint16_t query_slot;
} ImportedOpeningExactEntry;

typedef struct {
    uint32_t context_id;
    ImportedOpeningExactEntry *entries;
    uint32_t len;
} ImportedOpeningContext;

typedef struct {
    uint32_t opening_instance;
    uint64_t source_identity;
    VarId source_var_id;
    VarId opened_var_id;
} ImportedOpeningVarSlot;

typedef struct {
    VarId opened_var_id;
    VarId source_var_id;
} ImportedOpeningReverseSlot;

typedef struct {
    ImportedOpeningVarSlot *slots;
    size_t cap;
    size_t len;
    ImportedOpeningReverseSlot *reverse_slots;
    size_t reverse_cap;
    size_t reverse_len;
} ImportedOpeningVarMap;

typedef struct ImportedOpeningScope {
    const ImportedOpeningVarMap *vars;
    const struct ImportedOpeningScope *previous;
} ImportedOpeningScope;

static __thread const ImportedOpeningScope *g_imported_opening_scope = NULL;

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
    CettaIndex cap;
    CettaIndex used;
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
static Atom *imported_bridge_alpha_canonicalize_query(
    Arena *a, Atom *atom, const ImportedBridgeVarMap *map);
static bool imported_bridge_query_var_slots_contextual_ok(
    const ImportedBridgeVarMap *map,
    bool set_error);
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
static bool imported_bridge_parse_integer_token_id(TermUniverse *universe,
                                                   const char *tok,
                                                   AtomId *out_id);
static Atom *imported_bridge_parse_integer_token_atom(Arena *a,
                                                      const char *tok);
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
                                                    CettaExprLen npatterns);
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
    st->projected_atom_id_width_bits = 0;
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
    st->native_shadow_synced = false;
    st->preserve_logical_order = false;
}

static uint64_t imported_logical_len(const Space *s) {
    const MorkImportedState *mork = mork_imported_state_const(s);
    const ImportedBridgeState *st = backend_bridge_state_const(s);
    if (backend_uses_bridge_adapter(s) && mork && mork->attached_compiled)
        return mork->attached_count;
    if (s && s->match_backend.kind == SPACE_ENGINE_PATHMAP && st) {
        if (st->projection_valid)
            return st->projected_len;
        {
            uint64_t logical = 0;
            if (bridge_space_logical_len64(st, &logical))
                return logical;
            if (st->bridge_active && st->bridge_space)
                return s->native.len;
        }
    }
    return s->native.len;
}

static uint64_t shadow_storage_logical_len(const Space *s) {
    return s ? s->native.len : 0;
}

static AtomId imported_projected_atom_id_at(const ImportedBridgeState *st,
                                            CettaIndex idx) {
    size_t width = 0;
    if (!st || !st->projected_atom_ids || idx >= st->projected_len)
        return CETTA_ATOM_ID_NONE;
    width = cetta_atom_id_storage_width_bytes_from_bits(
        st->projected_atom_id_width_bits);
    if (width == 0)
        return CETTA_ATOM_ID_NONE;
    return cetta_atom_id_storage_load_bits(
        st->projected_atom_ids + ((size_t)idx * width),
        st->projected_atom_id_width_bits);
}

static bool imported_projected_pack_atom_ids(
    const TermUniverse *universe,
    const AtomId *atom_ids,
    CettaCount len,
    uint8_t **out_storage,
    uint8_t *out_bits) {
    uint32_t bits = 0;
    size_t width = 0;
    uint8_t *storage = NULL;
    if (out_storage)
        *out_storage = NULL;
    if (out_bits)
        *out_bits = 0;
    if (!universe || !out_storage || !out_bits)
        return false;
    bits = term_universe_store_format_atom_id_width_bits(
        term_universe_store_format(universe));
    width = cetta_atom_id_storage_width_bytes_from_bits(bits);
    if (width == 0)
        return false;
    if (len == 0) {
        *out_bits = (uint8_t)bits;
        return true;
    }
    if ((size_t)len > SIZE_MAX / width)
        return false;
    storage = cetta_malloc((size_t)len * width);
    if (!storage)
        return false;
    for (CettaIndex i = 0; i < len; i++) {
        if (!cetta_atom_id_storage_store_bits(
                storage + ((size_t)i * width), bits, atom_ids[i])) {
            free(storage);
            return false;
        }
    }
    *out_storage = storage;
    *out_bits = (uint8_t)bits;
    return true;
}

static bool imported_projected_pack_space_atom_ids(const Space *space,
                                                   uint8_t **out_storage,
                                                   uint8_t *out_bits) {
    AtomId *scratch_ids = NULL;
    bool ok = false;
    if (out_storage)
        *out_storage = NULL;
    if (out_bits)
        *out_bits = 0;
    if (!space || !space->native.universe || !out_storage || !out_bits)
        return false;
    if (space->native.len == 0) {
        *out_bits = (uint8_t)term_universe_store_format_atom_id_width_bits(
            term_universe_store_format(space->native.universe));
        return true;
    }
    scratch_ids = cetta_malloc(sizeof(*scratch_ids) * (size_t)space->native.len);
    if (!scratch_ids)
        return false;
    for (CettaIndex i = 0; i < space->native.len; i++)
        scratch_ids[i] = space_get_atom_id_at64(space, i);
    ok = imported_projected_pack_atom_ids(space->native.universe, scratch_ids,
                                          space->native.len, out_storage,
                                          out_bits);
    free(scratch_ids);
    return ok;
}

static inline AtomId shadow_storage_get_atom_id_at_direct(
        const Space *s, uint64_t idx) {
    size_t physical_idx = 0;
    if (!s || idx >= s->native.len)
        return CETTA_ATOM_ID_NONE;
    if (idx > (uint64_t)SIZE_MAX)
        return CETTA_ATOM_ID_NONE;
    physical_idx = (size_t)(
        s->kind == SPACE_KIND_QUEUE ? (s->native.start + idx) : idx);
    return cetta_atom_id_storage_load_bits(
        s->native.atom_ids +
            (physical_idx *
             cetta_atom_id_storage_width_bytes_from_bits(
                 s->native.atom_id_width_bits)),
        s->native.atom_id_width_bits);
}

static AtomId shadow_storage_get_atom_id_at(const Space *s, uint64_t idx) {
    return shadow_storage_get_atom_id_at_direct(s, idx);
}

static Atom *shadow_storage_get_at(const Space *s, uint64_t idx) {
    AtomId atom_id = shadow_storage_get_atom_id_at_direct(s, idx);
    return term_universe_get_atom(s ? s->native.universe : NULL, atom_id);
}

static uint64_t imported_storage_logical_len(const Space *s) {
    if (s && s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        s->match_backend.pathmap.bridge.preserve_logical_order)
        return shadow_storage_logical_len(s);
    return imported_logical_len(s);
}

static bool bridge_space_logical_len64(const ImportedBridgeState *st,
                                       uint64_t *out_logical) {
    uint64_t logical = 0;
    if (out_logical)
        *out_logical = 0;
    if (!out_logical || !st || !st->bridge_active || !st->bridge_space)
        return false;
    if (!cetta_mork_bridge_space_size(
            (const CettaMorkSpaceHandle *)st->bridge_space, &logical))
        return false;
    *out_logical = logical;
    return true;
}

static bool bridge_handle_logical_len64(CettaMorkSpaceHandle *bridge,
                                        uint64_t *out_logical) {
    uint64_t logical = 0;
    if (out_logical)
        *out_logical = 0;
    if (!out_logical || !bridge)
        return false;
    if (!cetta_mork_bridge_space_size(bridge, &logical))
        return false;
    *out_logical = logical;
    return true;
}

/* Transitional pathmap/mork projection moves through an explicit
   backend-derived snapshot when possible, falling back to the legacy shadow
   only while the ownership migration is still incomplete. */
static AtomId imported_storage_get_atom_id_at(const Space *s, uint64_t idx) {
    Space *mutable_space = (Space *)s;
    ImportedBridgeState *st = backend_bridge_state(mutable_space);
    if (st && s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        st->preserve_logical_order)
        return shadow_storage_get_atom_id_at(s, idx);
    if (st && imported_storage_ensure_projection(mutable_space)) {
        if (idx >= st->projected_len)
            return CETTA_ATOM_ID_NONE;
        return imported_projected_atom_id_at(st, (CettaIndex)idx);
    }
    return shadow_storage_get_atom_id_at(s, idx);
}

static Atom *imported_storage_get_at(const Space *s, uint64_t idx) {
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
    for (CettaIndex i = 0; i < memo->cap; i++) {
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

    CettaIndex idx = (CettaIndex)(hash & (uint64_t)(memo->cap - 1u));
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
                                             CettaIndex new_cap) {
    CettaIndex i = 0;
    ImportedBridgeExprMemoSlot *new_slots =
        cetta_malloc(sizeof(ImportedBridgeExprMemoSlot) * (size_t)new_cap);
    memset(new_slots, 0, sizeof(ImportedBridgeExprMemoSlot) * (size_t)new_cap);

    if (memo->slots) {
        for (i = 0; i < memo->cap; i++) {
            ImportedBridgeExprMemoSlot old = memo->slots[i];
            if (!old.occupied)
                continue;
            CettaIndex idx = (CettaIndex)(old.hash & (uint64_t)(new_cap - 1u));
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
                                              CettaIndex needed) {
    CettaIndex new_cap;
    CettaIndex limit;
    if (!memo)
        return false;
    if (memo->cap > 0 &&
        needed <= (memo->cap / 2u) + (memo->cap / 4u))
        return true;

    limit = (CettaIndex)(SIZE_MAX / sizeof(ImportedBridgeExprMemoSlot));
    if (needed > limit)
        return false;
    new_cap = memo->cap ? memo->cap : 16u;
    while (needed > (new_cap / 2u) + (new_cap / 4u)) {
        if (new_cap > limit / 2u)
            new_cap = limit;
        else
            new_cap *= 2u;
        if (new_cap < needed)
            return false;
        if (new_cap == limit)
            break;
    }
    if (new_cap < needed)
        return false;
    while ((new_cap & (new_cap - 1u)) != 0u) {
        if (new_cap > limit / 2u)
            return false;
        new_cap *= 2u;
    }
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

static bool imported_bridge_parse_integer_token_id(TermUniverse *universe,
                                                   const char *tok,
                                                   AtomId *out_id) {
    if (!universe || !tok || !out_id)
        return false;

    char *canonical = cetta_bigint_canonicalize_owned(tok);
    if (!canonical)
        return false;

    int64_t small = 0;
    if (cetta_bigint_text_fits_i64(canonical, &small))
        *out_id = tu_intern_int(universe, small);
    else
        *out_id = tu_intern_bigint(universe, canonical);
    free(canonical);
    return *out_id != CETTA_ATOM_ID_NONE;
}

static Atom *imported_bridge_parse_integer_token_atom(Arena *a,
                                                      const char *tok) {
    if (!a || !tok)
        return NULL;

    char *canonical = cetta_bigint_canonicalize_owned(tok);
    if (!canonical)
        return NULL;

    int64_t small = 0;
    Atom *out = cetta_bigint_text_fits_i64(canonical, &small)
        ? atom_int(a, small)
        : atom_bigint(a, canonical);
    free(canonical);
    return out;
}

static bool imported_bridge_parse_rational_token_id(TermUniverse *universe,
                                                    const char *tok,
                                                    AtomId *out_id) {
    if (!universe || !tok || !out_id)
        return false;
    AtomId id = tu_intern_rational(universe, tok);
    if (id == CETTA_ATOM_ID_NONE)
        return false;
    *out_id = id;
    return true;
}

static Atom *imported_bridge_parse_rational_token_atom(Arena *a,
                                                       const char *tok) {
    if (!a || !tok)
        return NULL;
    return atom_rational(a, tok);
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

    if (imported_bridge_parse_integer_token_id(universe, tok, out_id))
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;

    if (strchr(tok, '/') &&
        imported_bridge_parse_rational_token_id(universe, tok, out_id))
        return IMPORTED_BRIDGE_EXPR_DECODE_OK;

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
    if (!contexts)
        return;
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

static void imported_opening_var_map_free(ImportedOpeningVarMap *map) {
    if (!map)
        return;
    free(map->slots);
    free(map->reverse_slots);
    map->slots = NULL;
    map->cap = 0u;
    map->len = 0u;
    map->reverse_slots = NULL;
    map->reverse_cap = 0u;
    map->reverse_len = 0u;
}

static size_t imported_opening_mix64(uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (size_t)value;
}

static size_t imported_opening_var_hash(uint32_t opening_instance,
                                        uint64_t source_identity) {
    return imported_opening_mix64(
        source_identity ^ ((uint64_t)opening_instance << 32));
}

static size_t imported_opening_reverse_hash(VarId opened_var_id) {
    return imported_opening_mix64((uint64_t)opened_var_id);
}

static bool imported_opening_var_map_grow(ImportedOpeningVarMap *map) {
    size_t next_cap;
    ImportedOpeningVarSlot *next;

    if (!map)
        return false;
    next_cap = map->cap ? map->cap * 2u : 16u;
    if (next_cap < map->cap ||
        next_cap > SIZE_MAX / sizeof(*map->slots)) {
        return false;
    }
    next = cetta_malloc(next_cap * sizeof(*next));
    memset(next, 0, next_cap * sizeof(*next));
    for (size_t i = 0; i < map->cap; i++) {
        ImportedOpeningVarSlot slot = map->slots[i];
        if (slot.opening_instance == 0u)
            continue;
        size_t pos = imported_opening_var_hash(
            slot.opening_instance, slot.source_identity) & (next_cap - 1u);
        while (next[pos].opening_instance != 0u)
            pos = (pos + 1u) & (next_cap - 1u);
        next[pos] = slot;
    }
    free(map->slots);
    map->slots = next;
    map->cap = next_cap;
    return true;
}

static bool imported_opening_reverse_map_grow(ImportedOpeningVarMap *map) {
    size_t next_cap;
    ImportedOpeningReverseSlot *next;

    if (!map)
        return false;
    next_cap = map->reverse_cap ? map->reverse_cap * 2u : 16u;
    if (next_cap < map->reverse_cap ||
        next_cap > SIZE_MAX / sizeof(*map->reverse_slots)) {
        return false;
    }
    next = cetta_malloc(next_cap * sizeof(*next));
    memset(next, 0, next_cap * sizeof(*next));
    for (size_t i = 0; i < map->reverse_cap; i++) {
        ImportedOpeningReverseSlot slot = map->reverse_slots[i];
        if (slot.opened_var_id == VAR_ID_NONE)
            continue;
        size_t pos = imported_opening_reverse_hash(slot.opened_var_id) &
                     (next_cap - 1u);
        while (next[pos].opened_var_id != VAR_ID_NONE)
            pos = (pos + 1u) & (next_cap - 1u);
        next[pos] = slot;
    }
    free(map->reverse_slots);
    map->reverse_slots = next;
    map->reverse_cap = next_cap;
    return true;
}

static bool imported_opening_var_map_get(
    ImportedOpeningVarMap *map,
    uint32_t opening_instance,
    uint64_t source_identity,
    VarId source_var_id,
    VarId *out_var_id) {
    size_t pos;

    if (!map || !out_var_id || opening_instance == 0u ||
        source_var_id == VAR_ID_NONE)
        return false;
    if (map->cap == 0u || (map->len + 1u) * 2u > map->cap) {
        if (!imported_opening_var_map_grow(map))
            return false;
    }
    pos = imported_opening_var_hash(opening_instance, source_identity) &
          (map->cap - 1u);
    for (;;) {
        ImportedOpeningVarSlot *slot = &map->slots[pos];
        if (slot->opening_instance == 0u) {
            size_t reverse_pos;
            VarId fresh = fresh_var_id();
            if (fresh == VAR_ID_NONE)
                return false;
            if (map->reverse_cap == 0u ||
                (map->reverse_len + 1u) * 2u > map->reverse_cap) {
                if (!imported_opening_reverse_map_grow(map))
                    return false;
            }
            reverse_pos = imported_opening_reverse_hash(fresh) &
                          (map->reverse_cap - 1u);
            while (map->reverse_slots[reverse_pos].opened_var_id !=
                   VAR_ID_NONE) {
                if (map->reverse_slots[reverse_pos].opened_var_id == fresh)
                    return false;
                reverse_pos = (reverse_pos + 1u) & (map->reverse_cap - 1u);
            }
            *slot = (ImportedOpeningVarSlot){
                .opening_instance = opening_instance,
                .source_identity = source_identity,
                .source_var_id = source_var_id,
                .opened_var_id = fresh,
            };
            map->reverse_slots[reverse_pos] = (ImportedOpeningReverseSlot){
                .opened_var_id = fresh,
                .source_var_id = source_var_id,
            };
            map->len++;
            map->reverse_len++;
            *out_var_id = fresh;
            return true;
        }
        if (slot->opening_instance == opening_instance &&
            slot->source_identity == source_identity) {
            if (slot->source_var_id != source_var_id)
                return false;
            *out_var_id = slot->opened_var_id;
            return true;
        }
        pos = (pos + 1u) & (map->cap - 1u);
    }
}

static bool imported_opening_source_var_id(VarId opened_var_id,
                                           VarId *out_source_var_id) {
    if (opened_var_id == VAR_ID_NONE || !out_source_var_id)
        return false;
    for (const ImportedOpeningScope *scope = g_imported_opening_scope;
         scope; scope = scope->previous) {
        const ImportedOpeningVarMap *map = scope->vars;
        size_t pos;
        if (!map || map->reverse_cap == 0u)
            continue;
        pos = imported_opening_reverse_hash(opened_var_id) &
              (map->reverse_cap - 1u);
        while (map->reverse_slots[pos].opened_var_id != VAR_ID_NONE) {
            if (map->reverse_slots[pos].opened_var_id == opened_var_id) {
                *out_source_var_id = map->reverse_slots[pos].source_var_id;
                return true;
            }
            pos = (pos + 1u) & (map->reverse_cap - 1u);
        }
    }
    return false;
}

static Atom *imported_opening_restore_source_vars(Arena *a, Atom *atom,
                                                  bool *changed) {
    if (!a || !atom || !changed)
        return NULL;
    if (atom->kind == ATOM_VAR) {
        VarId source_var_id = VAR_ID_NONE;
        if (!imported_opening_source_var_id(atom->var_id, &source_var_id))
            return atom;
        *changed = true;
        return atom_var_like(a, atom, source_var_id);
    }
    if (atom->kind != ATOM_EXPR)
        return atom;

    Atom **children = atom->expr.len
        ? arena_alloc(a, sizeof(*children) * atom->expr.len)
        : NULL;
    bool child_changed = false;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        bool this_changed = false;
        children[i] = imported_opening_restore_source_vars(
            a, atom->expr.elems[i], &this_changed);
        if (!children[i])
            return NULL;
        child_changed = child_changed || this_changed;
    }
    if (!child_changed)
        return atom;
    *changed = true;
    return atom_expr(a, children, atom->expr.len);
}

Atom *space_match_backend_restore_opening_provenance(Arena *a, Atom *atom) {
    bool changed = false;
    return imported_opening_restore_source_vars(a, atom, &changed);
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
        .source_env = 0,
        .opening_instance = 0,
        .source_slot = 0,
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
        .source_env = 0,
        .opening_instance = 0,
        .source_slot = 0,
        .var_id = VAR_ID_NONE,
        .spelling = SYMBOL_ID_NONE,
        .query_slot = query_slot,
    };
    return true;
}

static bool imported_opening_context_add_matched_entry(
    ImportedOpeningContext *context,
    uint16_t slot,
    uint8_t source_env,
    VarId var_id,
    SymbolId spelling) {
    if (!context || source_env == 0 || var_id == VAR_ID_NONE ||
        spelling == SYMBOL_ID_NONE)
        return false;
    if (imported_opening_context_slot(context, slot))
        return false;
    context->entries = cetta_realloc(
        context->entries, sizeof(ImportedOpeningExactEntry) * (context->len + 1u));
    context->entries[context->len++] = (ImportedOpeningExactEntry){
        .slot = slot,
        .kind = IMPORTED_MORK_OPEN_REF_MATCHED_EXACT,
        .source_env = source_env,
        .opening_instance = 0,
        .source_slot = 0,
        .var_id = var_id,
        .spelling = spelling,
        .query_slot = 0,
    };
    return true;
}

static bool imported_opening_context_add_matched_instance_entry(
    ImportedOpeningContext *context,
    uint16_t slot,
    uint32_t opening_instance,
    uint16_t source_slot,
    VarId var_id,
    SymbolId spelling) {
    if (!context || opening_instance == 0u || var_id == VAR_ID_NONE ||
        spelling == SYMBOL_ID_NONE)
        return false;
    if (imported_opening_context_slot(context, slot))
        return false;
    context->entries = cetta_realloc(
        context->entries, sizeof(ImportedOpeningExactEntry) * (context->len + 1u));
    context->entries[context->len++] = (ImportedOpeningExactEntry){
        .slot = slot,
        .kind = IMPORTED_MORK_OPEN_REF_MATCHED_INSTANCE,
        .source_env = 0,
        .opening_instance = opening_instance,
        .source_slot = source_slot,
        .var_id = var_id,
        .spelling = spelling,
        .query_slot = 0,
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
    uint64_t total_entries = 0;

    if (out_contexts)
        *out_contexts = NULL;
    if (!packet || !off || !out_contexts)
        return false;
    if (!imported_bridge_packet_count_ok(context_count))
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
        if (!imported_bridge_packet_count_sum_ok(total_entries, entry_count))
            goto fail;
        total_entries += entry_count;
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
            } else if (kind == IMPORTED_MORK_OPEN_REF_MATCHED_EXACT &&
                       allow_query_refs) {
                uint8_t source_env = 0;
                uint8_t matched_reserved = 0;
                uint64_t var_id = 0;
                uint32_t spelling_len = 0;
                SymbolId spelling = SYMBOL_ID_NONE;
                if (!imported_bridge_read_u8(packet, packet_len, off, &source_env) ||
                    !imported_bridge_read_u8(packet, packet_len, off,
                                             &matched_reserved) ||
                    source_env == 0 || matched_reserved != 0 ||
                    !imported_bridge_read_u64(packet, packet_len, off, &var_id) ||
                    !imported_bridge_read_u32(packet, packet_len, off,
                                              &spelling_len) ||
                    spelling_len == 0 ||
                    *off + (size_t)spelling_len > packet_len)
                    goto fail;
                spelling = symbol_intern_bytes(g_symbols, packet + *off,
                                               spelling_len);
                *off += spelling_len;
                if (!imported_opening_context_add_matched_entry(
                        &contexts[i], slot, source_env, (VarId)var_id,
                        spelling))
                    goto fail;
            } else if (kind == IMPORTED_MORK_OPEN_REF_MATCHED_INSTANCE &&
                       allow_query_refs) {
                uint32_t opening_instance = 0;
                uint16_t source_slot = 0;
                uint64_t var_id = 0;
                uint32_t spelling_len = 0;
                SymbolId spelling = SYMBOL_ID_NONE;
                if (!imported_bridge_read_u32(packet, packet_len, off,
                                              &opening_instance) ||
                    opening_instance == 0u ||
                    !imported_bridge_read_u16(packet, packet_len, off,
                                              &source_slot) ||
                    !imported_bridge_read_u64(packet, packet_len, off, &var_id) ||
                    !imported_bridge_read_u32(packet, packet_len, off,
                                              &spelling_len) ||
                    spelling_len == 0 ||
                    *off + (size_t)spelling_len > packet_len)
                    goto fail;
                spelling = symbol_intern_bytes(g_symbols, packet + *off,
                                               spelling_len);
                *off += spelling_len;
                if (!imported_opening_context_add_matched_instance_entry(
                        &contexts[i], slot, opening_instance, source_slot,
                        (VarId)var_id, spelling))
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
    ImportedOpeningVarMap *row_openings,
    ImportedOpeningVarMap *cursor_openings,
    uint16_t *introduced_vars,
    bool *ok) {
    if (!a || !expr || !off || !context || !query_vars || !row_openings ||
        !introduced_vars || !ok || !*ok || *off >= len) {
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
        if (entry->kind == IMPORTED_MORK_OPEN_REF_MATCHED_EXACT) {
            VarId opened_var_id = VAR_ID_NONE;
            if (!imported_opening_var_map_get(
                    row_openings, entry->source_env,
                    (uint64_t)entry->var_id, entry->var_id,
                    &opened_var_id)) {
                *ok = false;
                return NULL;
            }
            return atom_var_with_spelling(
                a, entry->spelling, opened_var_id);
        }
        if (entry->kind == IMPORTED_MORK_OPEN_REF_MATCHED_INSTANCE) {
            VarId opened_var_id = VAR_ID_NONE;
            if (!imported_opening_var_map_get(
                    cursor_openings, entry->opening_instance,
                    (uint64_t)entry->source_slot, entry->var_id,
                    &opened_var_id)) {
                *ok = false;
                return NULL;
            }
            return atom_var_with_spelling(
                a, entry->spelling, opened_var_id);
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
        if (entry->kind == IMPORTED_MORK_OPEN_REF_MATCHED_EXACT) {
            VarId opened_var_id = VAR_ID_NONE;
            if (!imported_opening_var_map_get(
                    row_openings, entry->source_env,
                    (uint64_t)entry->var_id, entry->var_id,
                    &opened_var_id)) {
                *ok = false;
                return NULL;
            }
            return atom_var_with_spelling(
                a, entry->spelling, opened_var_id);
        }
        if (entry->kind == IMPORTED_MORK_OPEN_REF_MATCHED_INSTANCE) {
            VarId opened_var_id = VAR_ID_NONE;
            if (!imported_opening_var_map_get(
                    cursor_openings, entry->opening_instance,
                    (uint64_t)entry->source_slot, entry->var_id,
                    &opened_var_id)) {
                *ok = false;
                return NULL;
            }
            return atom_var_with_spelling(
                a, entry->spelling, opened_var_id);
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
                a, expr, len, off, context, query_vars, row_openings,
                cursor_openings,
                introduced_vars, ok);
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
    ImportedOpeningVarMap *row_openings,
    ImportedOpeningVarMap *cursor_openings,
    bool *ok) {
    size_t off = 0;
    uint16_t introduced_vars = 0;
    if (!ok || !*ok || !expr || expr_len == 0 || !context || !query_vars ||
        !row_openings || value_flags != 0) {
        if (ok) *ok = false;
        return NULL;
    }
    Atom *value = imported_bridge_parse_value_contextual_rec(
        a, expr, expr_len, &off, context, query_vars, row_openings,
        cursor_openings,
        &introduced_vars, ok);
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
    uint64_t row_count,
    ImportedBridgeVarMap *query_vars,
    const Bindings *seed,
    bool repeat_multiplicity,
    ImportedOpeningVarMap *cursor_openings,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint64_t parsed_rows = 0;
    uint32_t context_count = 0;
    ImportedOpeningContext *contexts = NULL;
    bool success = true;
    bool direct_multiplicity = false;

    if (!a || !packet || !query_vars || !visitor)
        return false;

    if (!imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &flags) ||
        !imported_bridge_read_u64(packet, packet_len, &off, &parsed_rows) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &context_count))
        return false;
    direct_multiplicity =
        version == IMPORTED_MORK_CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION &&
        flags == IMPORTED_MORK_CONTEXTUAL_INDEXED_QUERY_ROWS_FLAGS;
    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        !((version == IMPORTED_MORK_CONTEXTUAL_ROWS_WIRE_VERSION &&
           flags == IMPORTED_MORK_CONTEXTUAL_QUERY_ROWS_FLAGS) ||
          direct_multiplicity) ||
        parsed_rows != row_count)
        return false;
    if (!imported_bridge_packet_count_ok(parsed_rows))
        return false;

    if (!imported_bridge_read_contextual_opening_contexts(
            packet, packet_len, &off, context_count, true, &contexts))
        return false;

    for (uint64_t row = 0; row < parsed_rows && success; row++) {
        uint64_t multiplicity = 1u;
        uint32_t binding_count = 0;
        ImportedOpeningVarMap row_openings = {0};
        Bindings merged;
        bool merged_inited = false;

        if ((direct_multiplicity &&
             (!imported_bridge_read_u64(packet, packet_len, &off,
                                        &multiplicity) ||
              multiplicity == 0u)) ||
            !imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            success = false;
            break;
        }
        if (!imported_bridge_packet_count_ok(binding_count)) {
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
                &row_openings, cursor_openings, &value_ok);
            off += expr_len;
            if (!value_ok ||
                !bindings_add_id(&merged, key_slot->var_id,
                                 key_slot->spelling, value)) {
                success = false;
                break;
            }
        }

        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_MORK_DIRECT_ROW);
        if (success && !bindings_has_loop(&merged)) {
            uint64_t repeats = repeat_multiplicity ? multiplicity : 1u;
            ImportedOpeningScope opening_scope = {
                .vars = cursor_openings,
                .previous = g_imported_opening_scope,
            };
            if (cursor_openings)
                g_imported_opening_scope = &opening_scope;
            for (uint64_t rep = 0u; rep < repeats; rep++) {
                if (!visitor(&merged, ctx)) {
                    success = false;
                    break;
                }
            }
            if (cursor_openings)
                g_imported_opening_scope = opening_scope.previous;
        }
        if (merged_inited)
            bindings_free(&merged);
        imported_opening_var_map_free(&row_openings);
    }

    if (success && off != packet_len)
        success = false;
    imported_opening_contexts_free(contexts, context_count);
    return success;
}

static bool imported_bridge_contextual_exact_rows_append_atom(AtomId **items,
                                                              CettaCount *len,
                                                              CettaCount *cap,
                                                              AtomId atom_id,
                                                              uint32_t multiplicity) {
    CettaCount limit;
    CettaCount needed;
    CettaCount next;
    if (!items || !len || !cap || atom_id == CETTA_ATOM_ID_NONE ||
        multiplicity == 0)
        return false;
    limit = (CettaCount)space_match_backend_packet_materialization_limit();
    if (*len > limit || (CettaCount)multiplicity > limit - *len) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        return false;
    }
    needed = *len + (CettaCount)multiplicity;
    if (needed > *cap) {
        next = *cap ? *cap : 8u;
        while (next < needed) {
            if (next > limit / 2u)
                next = needed;
            else
                next *= 2u;
        }
        if (next > limit)
            next = limit;
        if (next < needed) {
            space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
            return false;
        }
        *items = cetta_realloc(*items, sizeof(AtomId) * (size_t)next);
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
    CettaCount *out_len) {
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint64_t row_count = 0;
    uint32_t context_count = 0;
    ImportedOpeningContext *contexts = NULL;
    AtomId *items = NULL;
    CettaCount len = 0;
    CettaCount cap = 0;
    uint64_t total_entries = 0;
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
        !imported_bridge_read_u64(packet, packet_len, &off, &row_count) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &context_count))
        goto done;

    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_CONTEXTUAL_ROWS_WIRE_VERSION ||
        flags != IMPORTED_MORK_CONTEXTUAL_EXACT_ROWS_FLAGS)
        goto done;
    if (!imported_bridge_packet_count_ok(row_count)) {
        goto done;
    }
    if ((row_count == 0 && context_count != 0) ||
        (row_count != 0 && context_count == 0))
        goto done;
    if (!imported_bridge_packet_count_ok(context_count))
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
        if (!imported_bridge_packet_count_sum_ok(total_entries, entry_count))
            goto done;
        total_entries += entry_count;
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

    for (uint64_t row = 0; row < row_count; row++) {
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

static bool imported_bridge_decode_expr_row_packet_append(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    const uint8_t *packet,
    size_t packet_len,
    uint64_t packet_rows,
    AtomId **items,
    CettaCount *len,
    CettaCount *cap) {
    size_t off = 0;
    uint64_t decoded_rows = 0;
    uint64_t limit = space_match_backend_packet_materialization_limit();

    if (!universe || !scratch || !memo || !items || !len || !cap)
        return false;
    if (packet_rows == 0)
        return packet_len == 0;
    if (!packet)
        return false;
    if ((uint64_t)*len > limit || packet_rows > limit - (uint64_t)*len) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        return false;
    }

    while (off < packet_len) {
        ArenaMark scratch_mark = arena_mark(scratch);
        AtomId atom_id = CETTA_ATOM_ID_NONE;
        uint32_t expr_len = 0;
        ImportedBridgeExprDecodeResult result;

        if (!imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
            off + expr_len > packet_len) {
            arena_reset(scratch, scratch_mark);
            return false;
        }
        result = imported_bridge_packet_expr_to_atom_id_cached(
            universe, scratch, memo, packet + off, expr_len, &atom_id);
        off += expr_len;
        if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK ||
            atom_id == CETTA_ATOM_ID_NONE ||
            !imported_bridge_contextual_exact_rows_append_atom(
                items, len, cap, atom_id, 1)) {
            arena_reset(scratch, scratch_mark);
            return false;
        }
        decoded_rows++;
        arena_reset(scratch, scratch_mark);
    }

    return decoded_rows == packet_rows;
}

static bool imported_bridge_decode_expr_rows_from_cursor(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    CettaMorkSpaceHandle *bridge,
    AtomId **out_items,
    CettaCount *out_len) {
    CettaMorkCursorHandle *cursor = NULL;
    AtomId *items = NULL;
    CettaCount len = 0;
    CettaCount cap = 0;
    bool ok = true;

    if (out_items)
        *out_items = NULL;
    if (out_len)
        *out_len = 0;
    if (!universe || !scratch || !memo || !bridge || !out_items || !out_len)
        return false;

    cursor = cetta_mork_bridge_cursor_new(bridge);
    if (!cursor)
        return false;

    while (ok) {
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        uint64_t packet_rows = 0;

        ok = cetta_mork_bridge_cursor_next_expr_rows(
            cursor,
            IMPORTED_MORK_CURSOR_EXPR_ROW_BATCH_ROWS,
            IMPORTED_MORK_CURSOR_EXPR_ROW_BATCH_BYTES,
            &packet, &packet_len, &packet_rows);
        if (ok && packet_rows == 0) {
            ok = packet_len == 0;
            cetta_mork_bridge_bytes_free(packet, packet_len);
            break;
        }
        if (ok) {
            ok = imported_bridge_decode_expr_row_packet_append(
                universe, scratch, memo, packet, packet_len, packet_rows,
                &items, &len, &cap);
        }
        cetta_mork_bridge_bytes_free(packet, packet_len);
    }

    cetta_mork_bridge_cursor_free(cursor);
    if (!ok) {
        free(items);
        return false;
    }

    *out_items = items;
    *out_len = len;
    return true;
}

static bool imported_bridge_decode_expr_rows_from_dump(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    CettaMorkSpaceHandle *bridge,
    AtomId **out_items,
    CettaCount *out_len) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t packet_rows = 0;
    AtomId *items = NULL;
    CettaCount len = 0;
    CettaCount cap = 0;
    bool ok = false;

    if (out_items)
        *out_items = NULL;
    if (out_len)
        *out_len = 0;
    if (!universe || !scratch || !memo || !bridge || !out_items || !out_len)
        return false;

    if (!cetta_mork_bridge_space_dump_expr_rows(
            bridge, &packet, &packet_len, &packet_rows))
        return false;

    ok = imported_bridge_decode_expr_row_packet_append(
        universe, scratch, memo, packet, packet_len, packet_rows,
        &items, &len, &cap);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    if (!ok) {
        free(items);
        return false;
    }

    *out_items = items;
    *out_len = len;
    return true;
}

static bool imported_bridge_visit_atoms_dump(
    CettaMorkSpaceHandle *bridge,
    TermUniverse *universe,
    Arena *scratch,
    CettaMorkAtomVisitor visitor,
    void *ctx) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t packet_rows = 0;
    char *text = NULL;
    size_t pos = 0;
    uint64_t decoded_rows = 0;
    bool ok = false;

    if (!bridge || !universe || !scratch || !visitor)
        return false;
    if (!cetta_mork_bridge_space_dump(bridge, &packet, &packet_len, &packet_rows))
        return false;
    if (packet_rows == 0) {
        cetta_mork_bridge_bytes_free(packet, packet_len);
        return packet_len == 0;
    }
    if (!packet || packet_len == 0) {
        cetta_mork_bridge_bytes_free(packet, packet_len);
        return false;
    }

    text = arena_alloc(scratch, packet_len + 1u);
    memcpy(text, packet, packet_len);
    text[packet_len] = '\0';
    cetta_mork_bridge_bytes_free(packet, packet_len);
    packet = NULL;

    while (text[pos]) {
        size_t before = pos;
        AtomId atom_id = parse_sexpr_to_id(universe, text, &pos);
        Atom *atom = NULL;

        if (atom_id == CETTA_ATOM_ID_NONE) {
            while (text[pos] && isspace((unsigned char)text[pos]))
                pos++;
            ok = (text[pos] == '\0');
            return ok && decoded_rows == packet_rows;
        }
        if (pos == before)
            return false;

        atom = term_universe_get_atom(universe, atom_id);
        if (!atom || !visitor(atom, ctx))
            return false;
        decoded_rows++;
    }

    return decoded_rows == packet_rows;
}

typedef enum {
    IMPORTED_BRIDGE_PACKET_VISIT_ERROR = 0,
    IMPORTED_BRIDGE_PACKET_VISIT_COMPLETE = 1,
    IMPORTED_BRIDGE_PACKET_VISIT_STOPPED = 2,
} ImportedBridgePacketVisitResult;

static ImportedBridgePacketVisitResult imported_bridge_visit_expr_row_packet(
    TermUniverse *universe,
    Arena *scratch,
    ImportedBridgeExprMemo *memo,
    const uint8_t *packet,
    size_t packet_len,
    uint64_t packet_rows,
    CettaMorkAtomVisitor visitor,
    void *ctx) {
    size_t off = 0;
    uint64_t decoded_rows = 0;

    if (!universe || !scratch || !memo || !visitor)
        return IMPORTED_BRIDGE_PACKET_VISIT_ERROR;
    if (packet_rows == 0)
        return packet_len == 0
            ? IMPORTED_BRIDGE_PACKET_VISIT_COMPLETE
            : IMPORTED_BRIDGE_PACKET_VISIT_ERROR;
    if (!packet)
        return IMPORTED_BRIDGE_PACKET_VISIT_ERROR;

    while (off < packet_len) {
        ArenaMark scratch_mark = arena_mark(scratch);
        AtomId atom_id = CETTA_ATOM_ID_NONE;
        Atom *atom = NULL;
        uint32_t expr_len = 0;
        ImportedBridgeExprDecodeResult result;

        if (!imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
            off + expr_len > packet_len) {
            arena_reset(scratch, scratch_mark);
            return IMPORTED_BRIDGE_PACKET_VISIT_ERROR;
        }
        result = imported_bridge_packet_expr_to_atom_id_cached(
            universe, scratch, memo, packet + off, expr_len, &atom_id);
        off += expr_len;
        if (result != IMPORTED_BRIDGE_EXPR_DECODE_OK ||
            atom_id == CETTA_ATOM_ID_NONE) {
            arena_reset(scratch, scratch_mark);
            return IMPORTED_BRIDGE_PACKET_VISIT_ERROR;
        }
        atom = term_universe_get_atom(universe, atom_id);
        arena_reset(scratch, scratch_mark);
        if (!atom)
            return IMPORTED_BRIDGE_PACKET_VISIT_ERROR;
        if (!visitor(atom, ctx))
            return IMPORTED_BRIDGE_PACKET_VISIT_STOPPED;
        decoded_rows++;
    }

    return decoded_rows == packet_rows
        ? IMPORTED_BRIDGE_PACKET_VISIT_COMPLETE
        : IMPORTED_BRIDGE_PACKET_VISIT_ERROR;
}

static SpaceMatchPullVisitResult imported_bridge_visit_atoms_cursor(
    CettaMorkSpaceHandle *bridge,
    TermUniverse *universe,
    Arena *scratch,
    CettaMorkAtomVisitor visitor,
    void *ctx) {
    CettaMorkCursorHandle *cursor = NULL;
    ImportedBridgeExprMemo memo;
    SpaceMatchPullVisitResult result = SPACE_MATCH_PULL_VISIT_COMPLETE;

    if (!bridge || !universe || !scratch || !visitor)
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    cursor = cetta_mork_bridge_cursor_new(bridge);
    if (!cursor)
        return SPACE_MATCH_PULL_VISIT_DECLINED;

    imported_bridge_expr_memo_init(&memo);
    for (;;) {
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        uint64_t packet_rows = 0;
        ImportedBridgePacketVisitResult packet_result;

        if (!cetta_mork_bridge_cursor_next_expr_rows(
                cursor, IMPORTED_MORK_CURSOR_EXPR_ROW_BATCH_ROWS,
                IMPORTED_MORK_CURSOR_EXPR_ROW_BATCH_BYTES,
                &packet, &packet_len, &packet_rows)) {
            result = SPACE_MATCH_PULL_VISIT_TERMINATED;
            cetta_mork_bridge_bytes_free(packet, packet_len);
            break;
        }
        if (packet_rows == 0) {
            result = packet_len == 0
                ? SPACE_MATCH_PULL_VISIT_COMPLETE
                : SPACE_MATCH_PULL_VISIT_TERMINATED;
            cetta_mork_bridge_bytes_free(packet, packet_len);
            break;
        }
        packet_result = imported_bridge_visit_expr_row_packet(
            universe, scratch, &memo, packet, packet_len, packet_rows,
            visitor, ctx);
        cetta_mork_bridge_bytes_free(packet, packet_len);
        if (packet_result != IMPORTED_BRIDGE_PACKET_VISIT_COMPLETE) {
            result = SPACE_MATCH_PULL_VISIT_TERMINATED;
            break;
        }
    }

    imported_bridge_expr_memo_free(&memo);
    cetta_mork_bridge_cursor_free(cursor);
    return result;
}

bool space_match_backend_mork_visit_atoms_direct(
    CettaMorkSpaceHandle *bridge,
    TermUniverse *universe,
    Arena *scratch,
    CettaMorkAtomVisitor visitor,
    void *ctx) {
    SpaceMatchPullVisitResult result = imported_bridge_visit_atoms_cursor(
        bridge, universe, scratch, visitor, ctx);
    if (result == SPACE_MATCH_PULL_VISIT_COMPLETE)
        return true;
    if (result == SPACE_MATCH_PULL_VISIT_TERMINATED)
        return false;
    return imported_bridge_visit_atoms_dump(
        bridge, universe, scratch, visitor, ctx);
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
    if (!cursor) {
        AtomId *items = NULL;
        CettaCount len = 0;
        if (!imported_bridge_decode_expr_rows_from_dump(
                dst->native.universe, &scratch, &memo, bridge,
                &items, &len)) {
            goto done;
        }
        for (CettaIndex i = 0; i < len; i++) {
            space_add_atom_id(&staged, items[i]);
            loaded++;
        }
        free(items);
        goto commit_staged;
    }

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

commit_staged:
    for (CettaIndex i = 0; i < staged.native.len; i++)
        space_add_atom_id(dst, space_get_atom_id_at64(&staged, i));
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
    uint64_t rows = 0;
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
    if (space_is_ordered(s) || space_match_backend_logical_len64(s) != 0)
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
    if (!cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space)) {
        imported_mark_bridge_untrusted(s);
        return false;
    }
    if (!cetta_mork_bridge_space_load_act_file((CettaMorkSpaceHandle *)st->bridge_space,
                                               (const uint8_t *)path, strlen(path),
                                               &loaded)) {
        return false;
    }
    st->bridge_active = true;
    mst->attached_compiled = true;
    mst->attached_count = loaded;
    st->built = true;
    st->dirty = false;
    space_discard_native_logical_view(s);
    space_note_external_backend_mutation(s);
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
#if CETTA_BUILD_WITH_RUNTIME_STATS
    uint64_t logical = 0;
#endif
    bool ok;

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
    if (st->native_shadow_synced)
        return true;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    logical = imported_logical_len(s);
#endif
    ok = imported_shadow_refresh_from_projection(s);
    if (ok) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PATHMAP_MATERIALIZE_NATIVE);
#if CETTA_BUILD_WITH_RUNTIME_STATS
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_PATHMAP_MATERIALIZE_NATIVE_ATOMS,
                                logical);
#endif
    }
    return ok;
}

static bool mork_materialize_native_storage(Space *s, Arena *persistent_arena) {
    MorkImportedState *mst;
    ImportedBridgeState *st;
    Space *fresh = NULL;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    uint64_t logical_count = 0;
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

bool space_match_backend_snapshot_clone(Space *dst, Space *src) {
    CettaMorkSpaceHandle *source_bridge = NULL;
    CettaMorkSpaceHandle *snapshot_bridge = NULL;
    ImportedBridgeState *snapshot_state = NULL;

    if (!dst || !src ||
        src->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        space_is_ordered(src) ||
        src->match_backend.pathmap.bridge.preserve_logical_order) {
        return false;
    }
    if (!pathmap_local_ensure_bridge_live(src)) {
        return false;
    }
    source_bridge =
        (CettaMorkSpaceHandle *)src->match_backend.pathmap.bridge.bridge_space;
    if (!source_bridge)
        return false;
    snapshot_bridge = cetta_mork_bridge_space_clone(source_bridge);
    if (!snapshot_bridge)
        return false;
    if (!space_match_backend_try_set(dst, SPACE_ENGINE_PATHMAP)) {
        cetta_mork_bridge_space_free(snapshot_bridge);
        return false;
    }

    snapshot_state = &dst->match_backend.pathmap.bridge;
    snapshot_state->bridge_space = snapshot_bridge;
    snapshot_state->bridge_active = true;
    snapshot_state->bridge_unavailable = false;
    snapshot_state->built = true;
    snapshot_state->dirty = false;
    dst->revision = space_revision(src);
    return true;
}

bool space_match_backend_require_logical_order(Space *s,
                                               Arena *persistent_arena) {
    ImportedBridgeState *st;

    if (!s || s->match_backend.kind != SPACE_ENGINE_PATHMAP)
        return s != NULL;
    st = &s->match_backend.pathmap.bridge;
    if (st->preserve_logical_order)
        return true;
    if (!space_match_backend_materialize_native_storage(s, persistent_arena))
        return false;
    st->preserve_logical_order = true;
    return true;
}

bool space_match_backend_store_atom_id_direct(Space *s, AtomId atom_id,
                                              Atom *atom) {
    if (!s || !s->match_backend.ops || !s->match_backend.ops->store_atom_id_direct)
        return false;
    return s->match_backend.ops->store_atom_id_direct(s, atom_id, atom);
}

bool space_match_backend_store_atom_direct(Space *s, Atom *atom) {
    if (!s || !s->match_backend.ops || !s->match_backend.ops->store_atom_direct)
        return false;
    return s->match_backend.ops->store_atom_direct(s, atom);
}

SpaceBackendBatchResult space_match_backend_store_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_added) {
    if (out_added)
        *out_added = 0;
    if (!s || !s->match_backend.ops ||
        !s->match_backend.ops->store_atom_ids_batch_direct) {
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }
    return s->match_backend.ops->store_atom_ids_batch_direct(
        s, atom_ids, atom_count, out_added);
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

SpaceBackendBatchResult space_match_backend_remove_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_removed) {
    if (out_removed)
        *out_removed = 0;
    if (!s || !s->match_backend.ops ||
        !s->match_backend.ops->remove_atom_ids_batch_direct) {
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }
    return s->match_backend.ops->remove_atom_ids_batch_direct(
        s, atom_ids, atom_count, out_removed);
}

bool space_match_backend_truncate_direct(Space *s, uint32_t new_len) {
    return space_match_backend_truncate_direct64(s, new_len);
}

bool space_match_backend_truncate_direct64(Space *s, uint64_t new_len) {
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

    /* An order-pinned PathMap bridge is a filtered accelerator, not a complete
       membership view: authored equations and type declarations remain only in
       the authoritative native shadow.  Decline rather than turn an incomplete
       bridge miss into a false negative. */
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        st->preserve_logical_order)
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

bool space_match_backend_logical_len_u32_checked(const Space *s, uint32_t *out_len) {
    return space_match_backend_u32_bound_checked(
        space_match_backend_logical_len64(s),
        SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE, out_len);
}

uint64_t space_match_backend_logical_len64(const Space *s) {
    if (!s)
        return 0;
    if (s->match_backend.ops && s->match_backend.ops->logical_len)
        return s->match_backend.ops->logical_len(s);
    return shadow_storage_logical_len(s);
}

AtomId space_match_backend_get_atom_id_at(const Space *s, uint32_t idx) {
    if (!s)
        return CETTA_ATOM_ID_NONE;
    if (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
        s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) {
        return shadow_storage_get_atom_id_at_direct(s, idx);
    }
    if (s->match_backend.ops && s->match_backend.ops->get_atom_id_at)
        return s->match_backend.ops->get_atom_id_at(s, idx);
    return shadow_storage_get_atom_id_at(s, idx);
}

AtomId space_match_backend_get_atom_id_at64(const Space *s, uint64_t idx) {
    if (!s)
        return CETTA_ATOM_ID_NONE;
    if (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
        s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) {
        return shadow_storage_get_atom_id_at_direct(s, idx);
    }
    if (s->match_backend.ops && s->match_backend.ops->get_atom_id_at)
        return s->match_backend.ops->get_atom_id_at(s, idx);
    return shadow_storage_get_atom_id_at(s, idx);
}

Atom *space_match_backend_get_at(const Space *s, uint32_t idx) {
    if (!s)
        return NULL;
    if (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
        s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) {
        AtomId atom_id = shadow_storage_get_atom_id_at_direct(s, idx);
        return term_universe_get_atom(s->native.universe, atom_id);
    }
    if (s->match_backend.ops && s->match_backend.ops->get_at)
        return s->match_backend.ops->get_at(s, idx);
    return shadow_storage_get_at(s, idx);
}

Atom *space_match_backend_get_at64(const Space *s, uint64_t idx) {
    if (!s)
        return NULL;
    if (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
        s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) {
        AtomId atom_id = shadow_storage_get_atom_id_at_direct(s, idx);
        return term_universe_get_atom(s->native.universe, atom_id);
    }
    if (s->match_backend.ops && s->match_backend.ops->get_at)
        return s->match_backend.ops->get_at(s, idx);
    return shadow_storage_get_at(s, idx);
}

static bool imported_bridge_visit_query_only_v2_packet(
    Arena *a,
    const uint8_t *packet,
    size_t packet_len,
    uint64_t row_count,
    ImportedBridgeVarMap *query_vars,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint64_t parsed_rows = 0;
    bool wide_tokens = false;

    if (!a || !packet || !query_vars || !visitor)
        return false;
    if (!imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &flags) ||
        !imported_bridge_read_u64(packet, packet_len, &off, &parsed_rows)) {
        return false;
    }
    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_QUERY_ONLY_V2_VERSION ||
        parsed_rows != row_count) {
        return false;
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
        return false;
    }

    for (uint64_t row = 0; row < parsed_rows; row++) {
        uint32_t ref_count = 0;
        uint32_t binding_count = 0;
        Bindings row_bindings;
        bool row_ok = true;
        bindings_init(&row_bindings);

        if (!imported_bridge_read_u32(packet, packet_len, &off, &ref_count) ||
            off + (size_t)ref_count * 4u > packet_len) {
            bindings_free(&row_bindings);
            return false;
        }
        if (!imported_bridge_packet_count_ok(ref_count)) {
            bindings_free(&row_bindings);
            return false;
        }
        off += (size_t)ref_count * 4u;

        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            bindings_free(&row_bindings);
            return false;
        }
        if (!imported_bridge_packet_count_ok(binding_count)) {
            bindings_free(&row_bindings);
            return false;
        }

        for (uint32_t bi = 0; bi < binding_count && row_ok; bi++) {
            uint16_t query_slot = 0;
            uint8_t value_env = 0;
            uint8_t value_flags = 0;
            uint32_t expr_len = 0;
            if (!imported_bridge_read_u16(packet, packet_len, &off, &query_slot) ||
                off + 2 > packet_len) {
                row_ok = false;
                break;
            }
            value_env = packet[off++];
            value_flags = packet[off++];
            if (!imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
                off + expr_len > packet_len) {
                row_ok = false;
                break;
            }
            ImportedBridgeVarSlot *key_slot =
                imported_bridge_varmap_lookup(query_vars, query_slot);
            if (!key_slot) {
                row_ok = false;
                break;
            }
            bool value_ok = true;
            Atom *value = imported_bridge_parse_value_raw_query_only_v2(
                a, packet + off, expr_len, value_env, value_flags,
                wide_tokens, &value_ok);
            off += expr_len;
            if (!value_ok ||
                !bindings_add_id(&row_bindings, key_slot->var_id,
                                 key_slot->spelling, value)) {
                row_ok = false;
                break;
            }
        }

        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_MORK_DIRECT_ROW);
        SpaceMatchDecodedRowVisitResult row_result =
            space_match_backend_visit_decoded_row(
                row_ok, &row_bindings, visitor, ctx);
        if (row_result != SPACE_MATCH_DECODED_ROW_CONTINUE) {
            bindings_free(&row_bindings);
            return false;
        }
        bindings_free(&row_bindings);
    }

    return off == packet_len;
}

SpaceMatchDecodedRowVisitResult space_match_backend_visit_decoded_row(
    bool decoded, Bindings *row, CettaMorkBindingsVisitor visitor, void *ctx) {
    bool cyclic = decoded && row && bindings_has_loop(row);
    CettaGsltRowDisposition disposition = cetta_gslt_classify_streamed_row(
        decoded && row && visitor, cyclic);

    switch (disposition) {
    case CETTA_GSLT_ROW_ADDITIVE_ZERO:
        return SPACE_MATCH_DECODED_ROW_CONTINUE;
    case CETTA_GSLT_ROW_TRANSPORT_FAULT:
        return SPACE_MATCH_DECODED_ROW_FAULT;
    case CETTA_GSLT_ROW_CONTRIBUTE:
        return visitor(row, ctx)
            ? SPACE_MATCH_DECODED_ROW_CONTINUE
            : SPACE_MATCH_DECODED_ROW_STOP;
    case CETTA_GSLT_ROW_CONSUMER_STOP:
        return SPACE_MATCH_DECODED_ROW_STOP;
    }
    return SPACE_MATCH_DECODED_ROW_FAULT;
}

static bool imported_bridge_visit_bindings_materialized(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    Arena persistent;
    TermUniverse universe;
    Space staged;
    SubstMatchSet matches;
    bool space_inited = false;
    bool ok = false;

    if (!bridge || !a || !query || !visitor)
        return false;

    arena_init(&persistent);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    space_init_with_universe(&staged, &universe);
    smset_init(&matches);
    space_inited = true;

    if (!imported_materialize_bridge_space(&staged, &persistent, bridge, NULL))
        goto done;

    space_match_backend_clear_error();
    space_subst_query(&staged, a, query, &matches);
    for (CettaIndex i = 0; i < matches.len; i++) {
        Bindings merged;
        bindings_init(&merged);
        if (space_subst_match_with_seed(&staged, query, &matches.items[i],
                                        NULL, a, &merged)) {
            if (!visitor(&merged, ctx)) {
                bindings_free(&merged);
                goto done;
            }
        }
        bindings_free(&merged);
    }

    ok = true;
    space_match_backend_clear_error();

done:
    smset_free(&matches);
    if (space_inited)
        space_free(&staged);
    term_universe_free(&universe);
    arena_free(&persistent);
    return ok;
}

static bool imported_bridge_visit_conjunction_materialized(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    Arena persistent;
    TermUniverse universe;
    Space staged;
    BindingSet collected;
    bool space_inited = false;
    bool ok = false;

    if (!bridge || !a || !patterns || !visitor)
        return false;

    binding_set_init(&collected);
    arena_init(&persistent);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    space_init_with_universe(&staged, &universe);
    space_inited = true;

    if (!imported_materialize_bridge_space(&staged, &persistent, bridge, NULL))
        goto done;

    space_match_backend_clear_error();
    space_query_conjunction(&staged, a, patterns, npatterns, seed, &collected);
    if (space_match_backend_last_error_code() != SPACE_MATCH_BACKEND_ERROR_NONE)
        goto done;

    ok = mork_visit_collected_bindings(&collected, visitor, ctx);
    if (ok)
        space_match_backend_clear_error();

done:
    binding_set_free(&collected);
    if (space_inited)
        space_free(&staged);
    term_universe_free(&universe);
    arena_free(&persistent);
    return ok;
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
    uint64_t row_count = 0;
    CettaMorkQueryCursorHandle *query_cursor = NULL;
    ImportedBridgeVarMap query_vars;
    bool success = true;

    if (!bridge || !a || !query || !visitor)
        return false;

    arena_init(&scratch);
    /*
     * Canonical query variables use a private presentation over compact
     * temporary VarIds.  Variable equality intentionally ignores spelling,
     * so a global hash-cons table could otherwise return an unrelated
     * same-VarId variable and silently restore its old presentation.
     */
    arena_set_hashcons(&scratch, NULL);
    imported_bridge_varmap_init(&query_vars);
    if (!imported_bridge_collect_vars(query, &query_vars)) {
        imported_bridge_varmap_free(&query_vars);
        arena_free(&scratch);
        space_match_backend_clear_error();
        return imported_bridge_visit_bindings_materialized(
            bridge, a, query, visitor, ctx);
    }
    if (!imported_bridge_query_var_slots_contextual_ok(&query_vars, false)) {
        imported_bridge_varmap_free(&query_vars);
        arena_free(&scratch);
        space_match_backend_clear_error();
        return imported_bridge_visit_bindings_materialized(
            bridge, a, query, visitor, ctx);
    }
    Atom *canonical_query = imported_bridge_alpha_canonicalize_query(
        &scratch, query, &query_vars);
    char *pattern_text = canonical_query
        ? atom_to_parseable_string(&scratch, canonical_query)
        : NULL;
    if (!pattern_text ||
        imported_bridge_query_text_has_internal_vars(pattern_text)) {
        imported_bridge_varmap_free(&query_vars);
        arena_free(&scratch);
        space_match_backend_clear_error();
        return imported_bridge_visit_bindings_materialized(
            bridge, a, query, visitor, ctx);
    }
    if (cetta_mork_bridge_space_query_contextual_rows(
            bridge, (const uint8_t *)pattern_text, strlen(pattern_text),
            &packet, &packet_len, &row_count)) {
        BindingSet collected;
        binding_set_init(&collected);
        arena_free(&scratch);
        success = imported_bridge_visit_contextual_query_rows_packet(
            a, packet, packet_len, row_count, &query_vars, NULL,
            true, NULL, mork_query_collect_bindings, &collected);
        imported_bridge_varmap_free(&query_vars);
        cetta_mork_bridge_bytes_free(packet, packet_len);
        if (success) {
            success = mork_visit_collected_bindings(&collected, visitor, ctx);
            binding_set_free(&collected);
            if (success)
                space_match_backend_clear_error();
            return success;
        }
        binding_set_free(&collected);
        space_match_backend_clear_error();
        return imported_bridge_visit_bindings_materialized(
            bridge, a, query, visitor, ctx);
    }
    cetta_mork_bridge_bytes_free(packet, packet_len);
    packet = NULL;
    packet_len = 0;
    row_count = 0;

    if (cetta_mork_bridge_query_cursor_new_query_only_v2(
            bridge, (const uint8_t *)pattern_text, strlen(pattern_text),
            &query_cursor)) {
        arena_free(&scratch);
        while (success) {
            packet = NULL;
            packet_len = 0;
            row_count = 0;
            if (!cetta_mork_bridge_query_cursor_next(
                    query_cursor, IMPORTED_MORK_QUERY_ROW_BATCH_ROWS,
                    IMPORTED_MORK_QUERY_ROW_BATCH_BYTES, &packet, &packet_len,
                    &row_count)) {
                success = false;
                cetta_mork_bridge_bytes_free(packet, packet_len);
                break;
            }
            if (row_count == 0) {
                success = packet_len == 0;
                cetta_mork_bridge_bytes_free(packet, packet_len);
                break;
            }
            success = imported_bridge_visit_query_only_v2_packet(
                a, packet, packet_len, row_count, &query_vars, visitor, ctx);
            cetta_mork_bridge_bytes_free(packet, packet_len);
        }
        cetta_mork_bridge_query_cursor_free(query_cursor);
        imported_bridge_varmap_free(&query_vars);
        return success;
    }

    bool ok = cetta_mork_bridge_space_query_bindings_query_only_v2(
        bridge, (const uint8_t *)pattern_text, strlen(pattern_text),
        &packet, &packet_len, &row_count);
    arena_free(&scratch);
    if (!ok) {
        imported_bridge_varmap_free(&query_vars);
        return imported_bridge_visit_bindings_materialized(
            bridge, a, query, visitor, ctx);
    }

    success = imported_bridge_visit_query_only_v2_packet(
        a, packet, packet_len, row_count, &query_vars, visitor, ctx);
    imported_bridge_varmap_free(&query_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    if (!success)
        return false;
    space_match_backend_clear_error();
    return true;
}

static bool mork_query_collect_bindings(const Bindings *bindings, void *ctx) {
    BindingSet *set = (BindingSet *)ctx;
    if (!set)
        return false;
    if (set->len >= CETTA_BINDING_SET_MAX_ROWS) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        return false;
    }
    return binding_set_push(set, bindings);
}

static bool imported_bridge_visit_multi_ref_v3_packet(
    Arena *a,
    const uint8_t *packet,
    size_t packet_len,
    uint64_t row_count,
    uint32_t expected_factor_count,
    ImportedBridgeVarMap *query_vars,
    const Bindings *seed,
    bool repeat_multiplicity,
    CettaMorkBindingsVisitor visitor,
    void *ctx,
    bool count_direct_rows) {
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t factor_count = 0;
    uint64_t parsed_rows = 0;
    bool multi_ref_groups = false;
    bool direct_multiplicities = false;
    bool wide_tokens = false;

    if (!a || !packet || !query_vars || !visitor)
        return false;
    if (!imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &flags) ||
        !imported_bridge_read_u32(packet, packet_len, &off, &factor_count) ||
        !imported_bridge_read_u64(packet, packet_len, &off, &parsed_rows)) {
        return false;
    }
    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_MULTI_REF_V3_VERSION) {
        return false;
    }
    multi_ref_groups =
        (flags & IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS) != 0;
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
        factor_count != expected_factor_count ||
        parsed_rows != row_count) {
        return false;
    }
    if (!imported_bridge_packet_count_ok(parsed_rows) ||
        !imported_bridge_packet_count_ok(factor_count)) {
        return false;
    }

    for (uint64_t row = 0; row < parsed_rows; row++) {
        uint64_t multiplicity = 1;
        ImportedBridgeValueVarMap value_vars;
        bool merged_inited = false;
        Bindings merged;
        imported_bridge_value_varmap_init(&value_vars);

        for (uint32_t fi = 0; fi < factor_count; fi++) {
            uint64_t factor_multiplicity = 1;
            if (direct_multiplicities) {
                uint32_t count = 0;
                if (!imported_bridge_read_u32(packet, packet_len, &off, &count) ||
                    count == 0) {
                    imported_bridge_value_varmap_free(&value_vars);
                    return false;
                }
                factor_multiplicity = count;
            } else {
                uint32_t ref_count = 0;
                if (!imported_bridge_read_u32(packet, packet_len, &off, &ref_count) ||
                    off + (size_t)ref_count * 4u > packet_len ||
                    (repeat_multiplicity && ref_count == 0)) {
                    imported_bridge_value_varmap_free(&value_vars);
                    return false;
                }
                if (!imported_bridge_packet_count_ok(ref_count)) {
                    imported_bridge_value_varmap_free(&value_vars);
                    return false;
                }
                factor_multiplicity = ref_count ? ref_count : 1u;
                off += (size_t)ref_count * 4u;
            }
            if (repeat_multiplicity) {
                if (multiplicity > UINT64_MAX / factor_multiplicity) {
                    imported_bridge_value_varmap_free(&value_vars);
                    return false;
                }
                multiplicity *= factor_multiplicity;
            }
        }

        uint32_t binding_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            imported_bridge_value_varmap_free(&value_vars);
            return false;
        }
        if (!imported_bridge_packet_count_ok(binding_count)) {
            imported_bridge_value_varmap_free(&value_vars);
            return false;
        }

        if (seed) {
            if (!bindings_clone(&merged, seed)) {
                imported_bridge_value_varmap_free(&value_vars);
                return false;
            }
        } else {
            bindings_init(&merged);
        }
        merged_inited = true;

        for (uint32_t bi = 0; bi < binding_count; bi++) {
            uint16_t query_slot = 0;
            uint8_t value_env = 0;
            uint8_t value_flags = 0;
            uint32_t expr_len = 0;
            if (!imported_bridge_read_u16(packet, packet_len, &off, &query_slot) ||
                off + 2 > packet_len) {
                if (merged_inited)
                    bindings_free(&merged);
                imported_bridge_value_varmap_free(&value_vars);
                return false;
            }
            value_env = packet[off++];
            value_flags = packet[off++];
            if (!imported_bridge_read_u32(packet, packet_len, &off, &expr_len) ||
                off + expr_len > packet_len) {
                if (merged_inited)
                    bindings_free(&merged);
                imported_bridge_value_varmap_free(&value_vars);
                return false;
            }
            ImportedBridgeVarSlot *slot =
                imported_bridge_varmap_lookup(query_vars, query_slot);
            if (!slot) {
                if (merged_inited)
                    bindings_free(&merged);
                imported_bridge_value_varmap_free(&value_vars);
                return false;
            }
            bool value_ok = true;
            Atom *value = imported_bridge_parse_value_raw_multi_ref_v3(
                a, packet + off, expr_len, value_env, value_flags,
                wide_tokens, &value_vars, &value_ok);
            off += expr_len;
            if (!value_ok ||
                !bindings_add_id(&merged, slot->var_id, slot->spelling, value)) {
                if (merged_inited)
                    bindings_free(&merged);
                imported_bridge_value_varmap_free(&value_vars);
                return false;
            }
        }

        if (count_direct_rows)
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_MORK_CONJ_DIRECT);
        if (!bindings_has_loop(&merged)) {
            if (repeat_multiplicity) {
                for (uint64_t rep = 0; rep < multiplicity; rep++) {
                    if (!visitor(&merged, ctx)) {
                        bindings_free(&merged);
                        imported_bridge_value_varmap_free(&value_vars);
                        return false;
                    }
                }
            } else if (!visitor(&merged, ctx)) {
                bindings_free(&merged);
                imported_bridge_value_varmap_free(&value_vars);
                return false;
            }
        }
        if (merged_inited)
            bindings_free(&merged);
        imported_bridge_value_varmap_free(&value_vars);
    }

    return off == packet_len;
}

static bool imported_bridge_visit_indexed_cursor_packet(
    Arena *a,
    const uint8_t *packet,
    size_t packet_len,
    uint64_t row_count,
    uint32_t expected_factor_count,
    ImportedBridgeVarMap *query_vars,
    const Bindings *seed,
    bool repeat_multiplicity,
    ImportedOpeningVarMap *opening_vars,
    CettaMorkBindingsVisitor visitor,
    void *ctx,
    bool count_direct_rows) {
    size_t off = 0u;
    uint32_t magic = 0u;
    uint16_t version = 0u;

    if (!packet ||
        !imported_bridge_read_u32(packet, packet_len, &off, &magic) ||
        !imported_bridge_read_u16(packet, packet_len, &off, &version) ||
        magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC) {
        return false;
    }
    if (version == IMPORTED_MORK_MULTI_REF_V3_VERSION) {
        return imported_bridge_visit_multi_ref_v3_packet(
            a, packet, packet_len, row_count, expected_factor_count,
            query_vars, seed, repeat_multiplicity, visitor, ctx,
            count_direct_rows);
    }
    if (version == IMPORTED_MORK_CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION) {
        return imported_bridge_visit_contextual_query_rows_packet(
            a, packet, packet_len, row_count, query_vars, seed,
            true, opening_vars, visitor, ctx);
    }
    return false;
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

bool space_match_backend_can_try_visit_atoms_direct(Space *s) {
    return s && !space_is_ordered(s) && s->native.universe &&
           s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
           !s->match_backend.pathmap.bridge.preserve_logical_order &&
           space_contains_only_exact_atoms(s);
}

SpaceMatchPullVisitResult space_match_backend_try_visit_atoms_direct(
    Space *s,
    Arena *scratch,
    CettaMorkAtomVisitor visitor,
    void *ctx) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    ImportedBridgeState *st;

    if (!scratch || !visitor ||
        !space_match_backend_can_try_visit_atoms_direct(s)) {
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    }
    if (imported_logical_len(s) == 0)
        return SPACE_MATCH_PULL_VISIT_COMPLETE;

    st = &s->match_backend.pathmap.bridge;
    if (st->bridge_unavailable)
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    if (!(st->bridge_active && st->bridge_space) &&
        !pathmap_local_ensure_bridge_live(s)) {
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    }
    return imported_bridge_visit_atoms_cursor(
        (CettaMorkSpaceHandle *)st->bridge_space,
        s->native.universe, scratch, visitor, ctx);
}

bool space_match_backend_visit_bindings_direct(
    Space *s,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    ImportedBridgeState *st;
    CettaMorkSpaceHandle *bridge = NULL;
    SpaceMatchPullVisitResult indexed_result;

    if (!s || !a || !query || !visitor || space_is_ordered(s))
        return false;

    indexed_result = space_match_backend_try_visit_bindings_indexed(
        s, a, query, visitor, ctx);
    if (indexed_result != SPACE_MATCH_PULL_VISIT_DECLINED) {
        return indexed_result == SPACE_MATCH_PULL_VISIT_COMPLETE;
    }
    space_match_backend_clear_error();

    switch (s->match_backend.kind) {
    case SPACE_ENGINE_PATHMAP:
        if (imported_logical_len(s) == 0)
            return true;
        if (imported_atom_has_epoch_vars(query) ||
            imported_atom_has_bridge_vars(query)) {
            return false;
        }
        if (atom_has_vars(query) && !space_contains_only_exact_atoms(s))
            return false;
        st = &s->match_backend.pathmap.bridge;
        if (st->bridge_unavailable)
            return false;
        if (!(st->bridge_active && st->bridge_space) &&
            !pathmap_local_ensure_bridge_live(s)) {
            return false;
        }
        bridge = (CettaMorkSpaceHandle *)st->bridge_space;
        break;
    case SPACE_ENGINE_MORK:
        if (!space_match_backend_bridge_space(s, &bridge))
            return false;
        break;
    default:
        return false;
    }

    return bridge &&
           space_match_backend_mork_visit_bindings_direct(bridge, a, query,
                                                          visitor, ctx);
}

bool space_match_backend_can_try_visit_bindings_indexed(
    const Space *s,
    const Atom *query) {
    return s && query && !space_is_ordered(s) &&
           s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
           !s->match_backend.pathmap.bridge.preserve_logical_order &&
           pathmap_indexed_single_factor_enabled() &&
           !imported_atom_has_epoch_vars((Atom *)query) &&
           !imported_atom_has_bridge_vars((Atom *)query);
}

SpaceMatchPullVisitResult
space_match_backend_try_visit_bindings_indexed(
    Space *s,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    bool attempted = false;
    bool ok;

    if (!a || !visitor ||
        !space_match_backend_can_try_visit_bindings_indexed(s, query)) {
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    }
    if (imported_logical_len(s) == 0)
        return SPACE_MATCH_PULL_VISIT_COMPLETE;

    ok = pathmap_local_visit_bindings_indexed(
        s, a, query, visitor, ctx, &attempted);
    if (!attempted)
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    return ok ? SPACE_MATCH_PULL_VISIT_COMPLETE
              : SPACE_MATCH_PULL_VISIT_TERMINATED;
}

SpaceMatchPullVisitResult
space_match_backend_try_visit_conjunction_indexed(
    Space *s,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    return pathmap_local_visit_conjunction_indexed(
        s, a, patterns, npatterns, seed, visitor, ctx);
}

static bool mork_visit_collected_bindings(const BindingSet *set,
                                          CettaMorkBindingsVisitor visitor,
                                          void *ctx) {
    if (!set || !visitor)
        return false;
    for (CettaIndex i = 0; i < set->len; i++) {
        if (!visitor(&set->items[i], ctx))
            return false;
    }
    return true;
}

static bool mork_query_conjunction_iterative(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    BindingSet *out) {
    if (!bridge || !a || !patterns || !out)
        return false;
    binding_set_init(out);
    if (npatterns == 0) {
        if (seed)
            return binding_set_push(out, seed);
        Bindings unit;
        bindings_init(&unit);
        bool ok = binding_set_push(out, &unit);
        bindings_free(&unit);
        return ok;
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

    for (CettaExprIndex pi = 0; pi < npatterns; pi++) {
        bool success = true;
        BindingSet next;
        binding_set_init(&next);
        for (CettaIndex bi = 0; bi < cur.len; bi++) {
            Atom *grounded = bindings_apply_if_vars(&cur.items[bi], a, patterns[pi]);
            BindingSet matches;
            binding_set_init(&matches);
            if (!space_match_backend_mork_query_bindings_direct(
                    bridge, a, grounded, &matches)) {
                binding_set_free(&matches);
                success = false;
                break;
            }
            for (CettaIndex mi = 0; mi < matches.len; mi++) {
                Bindings merged;
                bindings_init(&merged);
                if (!bindings_try_merge_live(&merged, &cur.items[bi]) ||
                    !bindings_try_merge_live(&merged, &matches.items[mi])) {
                    bindings_free(&merged);
                    success = false;
                    break;
                }
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_MORK_CONJ_MERGE);
                if (!bindings_has_loop(&merged) &&
                    !binding_set_push_move(&next, &merged)) {
                    space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
                    success = false;
                    bindings_free(&merged);
                    break;
                }
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
    CettaExprLen npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    if (!bridge || !a || !patterns || !visitor)
        return false;
    if (npatterns == 0) {
        if (seed)
            return visitor(seed, ctx);
        Bindings unit;
        bindings_init(&unit);
        bool ok = visitor(&unit, ctx);
        bindings_free(&unit);
        return ok;
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
    uint64_t row_count = 0;
    CettaMorkQueryCursorHandle *query_cursor = NULL;
    ImportedOpeningVarMap opening_vars = {0};

    for (CettaExprIndex i = 0; i < npatterns; i++) {
        grounded[i] = seed ? bindings_apply_if_vars(seed, &scratch, patterns[i])
                           : patterns[i];
        if (!imported_bridge_collect_vars(grounded[i], &query_vars)) {
            success = false;
            goto cleanup;
        }
    }
    if (!imported_bridge_query_var_slots_contextual_ok(&query_vars, false)) {
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

    for (CettaExprIndex i = 0; i < npatterns; i++) {
        grounded[i] = imported_bridge_alpha_canonicalize_query(
            &scratch, grounded[i], &query_vars);
        if (!grounded[i]) {
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
    if (pathmap_indexed_factor_count_enabled(npatterns) &&
        cetta_mork_bridge_query_cursor_new_indexed_multi_ref_v4(
            bridge, (const uint8_t *)query_text, strlen(query_text),
            &query_cursor)) {
        bool rows_available = false;
        if (!pathmap_indexed_cursor_rows_available(
                query_cursor, &rows_available) || !rows_available) {
            pathmap_record_indexed_query_stats(query_cursor);
            cetta_mork_bridge_query_cursor_free(query_cursor);
            query_cursor = NULL;
            space_match_backend_clear_error();
        } else {
            while (success) {
                packet = NULL;
                packet_len = 0;
                row_count = 0;
                if (!cetta_mork_bridge_query_cursor_next(
                        query_cursor, IMPORTED_MORK_QUERY_ROW_BATCH_ROWS,
                        IMPORTED_MORK_QUERY_ROW_BATCH_BYTES, &packet, &packet_len,
                        &row_count)) {
                    success = false;
                    cetta_mork_bridge_bytes_free(packet, packet_len);
                    break;
                }
                if (row_count == 0) {
                    success = packet_len == 0;
                    cetta_mork_bridge_bytes_free(packet, packet_len);
                    break;
                }
                success = imported_bridge_visit_indexed_cursor_packet(
                    a, packet, packet_len, row_count, npatterns, &query_vars, seed,
                    false, &opening_vars, visitor, ctx, true);
                cetta_mork_bridge_bytes_free(packet, packet_len);
            }
            pathmap_record_indexed_query_stats(query_cursor);
            cetta_mork_bridge_query_cursor_free(query_cursor);
            query_cursor = NULL;
            goto cleanup;
        }
    }
    space_match_backend_clear_error();

    if (cetta_mork_bridge_space_query_contextual_rows(
            bridge, (const uint8_t *)query_text, strlen(query_text),
            &packet, &packet_len, &row_count)) {
        BindingSet contextual;
        binding_set_init(&contextual);
        success = imported_bridge_visit_contextual_query_rows_packet(
            a, packet, packet_len, row_count, &query_vars, seed,
            true, NULL, mork_query_collect_bindings, &contextual);
        cetta_mork_bridge_bytes_free(packet, packet_len);
        packet = NULL;
        packet_len = 0;
        row_count = 0;
        if (success) {
            success = mork_visit_collected_bindings(&contextual, visitor, ctx);
            binding_set_free(&contextual);
            goto cleanup;
        }
        binding_set_free(&contextual);
        space_match_backend_clear_error();
    }

    if (cetta_mork_bridge_query_cursor_new_multi_ref_v3(
            bridge, (const uint8_t *)query_text, strlen(query_text),
            &query_cursor)) {
        while (success) {
            packet = NULL;
            packet_len = 0;
            row_count = 0;
            if (!cetta_mork_bridge_query_cursor_next(
                    query_cursor, IMPORTED_MORK_QUERY_ROW_BATCH_ROWS,
                    IMPORTED_MORK_QUERY_ROW_BATCH_BYTES, &packet, &packet_len,
                    &row_count)) {
                success = false;
                cetta_mork_bridge_bytes_free(packet, packet_len);
                break;
            }
            if (row_count == 0) {
                success = packet_len == 0;
                cetta_mork_bridge_bytes_free(packet, packet_len);
                break;
            }
            success = imported_bridge_visit_multi_ref_v3_packet(
                a, packet, packet_len, row_count, npatterns, &query_vars, seed,
                false, visitor, ctx, true);
            cetta_mork_bridge_bytes_free(packet, packet_len);
        }
        cetta_mork_bridge_query_cursor_free(query_cursor);
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

    success = imported_bridge_visit_multi_ref_v3_packet(
        a, packet, packet_len, row_count, npatterns, &query_vars, seed, false,
        visitor, ctx, true);
    if (!success) {
        imported_bridge_varmap_free(&query_vars);
        cetta_mork_bridge_bytes_free(packet, packet_len);
        arena_free(&scratch);
        space_match_backend_clear_error();
        return imported_bridge_visit_conjunction_materialized(
            bridge, a, patterns, npatterns, seed, visitor, ctx);
    }

cleanup:
    imported_opening_var_map_free(&opening_vars);
    imported_bridge_varmap_free(&query_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    arena_free(&scratch);
    return success;
}

bool space_match_backend_mork_query_conjunction_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
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
    CettaIndex start = b->len;
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
        } else if (atom->ground.gkind == GV_BIGINT) {
            tok.kind = IMPORTED_FLAT_BIGINT;
            tok.sym_id = symbol_intern_cstr(g_symbols, atom_bigint_cstr(atom));
        } else if (atom->ground.gkind == GV_RATIONAL) {
            tok.kind = IMPORTED_FLAT_RATIONAL;
            tok.sym_id = symbol_intern_cstr(g_symbols, atom_rational_cstr(atom));
        } else {
            tok.kind = IMPORTED_FLAT_GROUNDED_OTHER;
        }
        imported_builder_push(b, tok);
        return;
    case ATOM_EXPR:
        tok.kind = IMPORTED_FLAT_EXPR;
        tok.arity = atom->expr.len;
        imported_builder_push(b, tok);
        for (CettaExprIndex i = 0; i < atom->expr.len; i++)
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
    CettaIndex start = b->len;
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
        case GV_BIGINT:
            tok.kind = IMPORTED_FLAT_BIGINT;
            tok.sym_id = symbol_intern_cstr(g_symbols, tu_bigint_cstr(universe, atom_id));
            break;
        case GV_RATIONAL:
            tok.kind = IMPORTED_FLAT_RATIONAL;
            tok.sym_id = symbol_intern_cstr(g_symbols, tu_rational_cstr(universe, atom_id));
            break;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return false;
        }
        imported_builder_push(b, tok);
        return true;
    case ATOM_EXPR:
        tok.kind = IMPORTED_FLAT_EXPR;
        tok.arity = tu_arity(universe, atom_id);
        imported_builder_push(b, tok);
        for (CettaExprIndex i = 0; i < tok.arity; i++) {
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
    case IMPORTED_FLAT_BIGINT:
        return lhs->sym_id == rhs->sym_id;
    case IMPORTED_FLAT_RATIONAL:
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

static bool imported_flat_equal(const ImportedFlatToken *lhs, CettaIndex li,
                                const ImportedFlatToken *rhs, CettaIndex ri) {
    if (lhs[li].span != rhs[ri].span) return false;
    for (CettaIndex off = 0; off < lhs[li].span; off++) {
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
        /* The contextual bridge packet identifies variables by text slots.
           A structural name must keep its Name key, so use the
           materialized C matcher instead of reducing it to SymbolId 0. */
        if (atom->name_key)
            return false;
        if (imported_bridge_internal_var(atom))
            return true;
        return imported_bridge_varmap_add(map, atom->var_id, atom->sym_id);
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (!imported_bridge_collect_vars(atom->expr.elems[i], map))
                return false;
        }
        return true;
    default:
        return true;
    }
}

typedef struct {
    const ImportedBridgeVarMap *map;
} ImportedBridgeAlphaCanonCtx;

static bool imported_bridge_varmap_find(
    const ImportedBridgeVarMap *map, VarId var_id, uint32_t *out_index) {
    if (!map)
        return false;
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].var_id == var_id) {
            if (out_index)
                *out_index = i;
            return true;
        }
    }
    return false;
}

static Atom *imported_bridge_rewrite_alpha_query_var(
    Arena *a, Atom *src_var, void *ctx_ptr) {
    ImportedBridgeAlphaCanonCtx *ctx = ctx_ptr;
    uint32_t index = 0;
    char name[64];
    int written;

    if (!a || !src_var || src_var->kind != ATOM_VAR || !ctx ||
        imported_bridge_internal_var(src_var) ||
        !imported_bridge_varmap_find(ctx->map, src_var->var_id, &index)) {
        return NULL;
    }
    written = snprintf(
        name, sizeof(name), "__cetta_query_v%u", index);
    if (written < 0 || (size_t)written >= sizeof(name))
        return NULL;
    /*
     * The bridge parser assigns query slots from textual variable names,
     * whereas CeTTa variable identity is VarId.  Rewrite presentation at the
     * boundary so equal VarIds share one slot and distinct VarIds never alias.
     * The original VarId/spelling map remains authoritative for decoded rows.
     */
    return atom_var_with_spelling(
        a, symbol_intern_cstr(g_symbols, name), (VarId)index + 1u);
}

static Atom *imported_bridge_alpha_canonicalize_query(
    Arena *a, Atom *atom, const ImportedBridgeVarMap *map) {
    ImportedBridgeAlphaCanonCtx ctx = {
        .map = map,
    };
    if (!a || !atom || !map)
        return NULL;
    return cetta_atom_rewrite_vars(
        a, atom, imported_bridge_rewrite_alpha_query_var, &ctx, true);
}

static bool imported_bridge_query_var_slots_contextual_ok(
    const ImportedBridgeVarMap *map,
    bool set_error
) {
    if (!map)
        return false;
    if ((uint64_t)map->len > space_match_backend_contextual_query_slot_limit()) {
        if (set_error)
            space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        return false;
    }
    return true;
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

    Atom *integer = imported_bridge_parse_integer_token_atom(a, tok);
    if (integer)
        return integer;

    if (strchr(tok, '/')) {
        Atom *rational = imported_bridge_parse_rational_token_atom(a, tok);
        if (rational)
            return rational;
    }

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
                                                    CettaExprLen npatterns) {
    if (npatterns == 0)
        return NULL;
    char **parts = arena_alloc(a, sizeof(char *) * npatterns);
    size_t total = 3;
    for (CettaExprIndex i = 0; i < npatterns; i++) {
        parts[i] = atom_to_parseable_string(a, patterns[i]);
        total += 1 + strlen(parts[i]);
    }
    char *buf = arena_alloc(a, total + 1);
    size_t off = 0;
    buf[off++] = '(';
    buf[off++] = ',';
    for (CettaExprIndex i = 0; i < npatterns; i++) {
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
                                                   CettaIndex idx,
                                                   CettaIndex span,
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
                                                     CettaIndex idx,
                                                     CettaIndex span,
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
                                                       CettaIndex idx,
                                                       CettaIndex span,
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
                                          CettaIndex idx,
                                          VarId var_id) {
    CettaIndex end = idx + tokens[idx].span;
    for (CettaIndex i = idx; i < end; i++) {
        if (tokens[i].kind == IMPORTED_FLAT_VAR &&
            tokens[i].var_id == var_id)
            return true;
    }
    return false;
}

static ImportedCorefVerdict imported_bind_indexed_value(ImportedCorefState *refs,
                                                        const ImportedFlatToken *tokens,
                                                        CettaIndex var_idx,
                                                        CettaIndex value_idx) {
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
                                      value_idx, val->span, vt->origin);
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
        Atom *var = refs->query[i].origin;
        bool added = var && var->kind == ATOM_VAR
            ? bindings_add_var(out, var, val)
            : bindings_add_id(out, refs->query[i].var_id,
                              refs->query[i].spelling, val);
        if (!added) {
            bindings_free(out);
            return false;
        }
    }
    for (uint32_t i = 0; i < refs->nindexed; i++) {
        Atom *var = atom_var_like(
            a, refs->indexed[i].origin,
            var_epoch_id(refs->indexed[i].var_id, epoch));
        bool added = var
            ? bindings_add_var(
                  out, var, qtokens[refs->indexed[i].idx].origin)
            : bindings_add_id(
                  out, var_epoch_id(refs->indexed[i].var_id, epoch),
                  refs->indexed[i].spelling,
                  qtokens[refs->indexed[i].idx].origin);
        if (!added) {
            bindings_free(out);
            return false;
        }
    }
    for (uint32_t i = 0; i < refs->nindexed_value; i++) {
        Atom *val = imported_token_copy_epoch(
            a, &ctokens[refs->indexed_value[i].idx], candidate_universe, epoch);
        Atom *var = atom_var_like(
            a, refs->indexed_value[i].origin,
            var_epoch_id(refs->indexed_value[i].var_id, epoch));
        bool added = var
            ? bindings_add_var(out, var, val)
            : bindings_add_id(
                  out,
                  var_epoch_id(refs->indexed_value[i].var_id, epoch),
                  refs->indexed_value[i].spelling, val);
        if (!added) {
            bindings_free(out);
            return false;
        }
    }
    return true;
}

static void imported_bucket_push_builder(ImportedFlatBucket *bucket,
                                         ImportedFlatBuilder *builder,
                                         CettaIndex atom_idx, uint32_t epoch) {
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
                                      CettaIndex atom_idx, uint32_t epoch) {
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
    CettaIndex source_len = 0;
    bool source_from_projection = false;
    if (!st)
        return;
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    if (st->bridge_active && st->bridge_space &&
        imported_storage_ensure_projection(s)) {
        source_len = st->projected_len;
        source_from_projection = true;
    } else {
        source_len = s->native.len;
    }
    for (CettaIndex i = 0; i < source_len; i++) {
        AtomId atom_id = source_from_projection
                             ? imported_projected_atom_id_at(st, i)
                             : shadow_storage_get_atom_id_at(s, i);
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
        Atom *atom = source_from_projection
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

/* An order-pinned PathMap mirrors only ordinary data rows into its unordered
 * physical index.  A query may use that mirror only when its fixed head cannot
 * match the omitted equation/type descriptor classes. */
static bool pathmap_order_pinned_index_pattern_safe(const Atom *pattern) {
    Atom *head;
    if (!pattern || pattern->kind == ATOM_VAR)
        return false;
    if (pattern->kind != ATOM_EXPR || pattern->expr.len == 0u)
        return true;
    head = pattern->expr.elems[0];
    if (!head || head->kind == ATOM_VAR)
        return false;
    return !atom_is_symbol_id(head, g_builtin_syms.equals) &&
           !atom_is_symbol_id(head, g_builtin_syms.colon);
}

static bool pathmap_order_pinned_index_patterns_safe(
    Atom **patterns, CettaExprLen npatterns) {
    if (!patterns || npatterns == 0u)
        return false;
    for (CettaExprLen i = 0; i < npatterns; i++) {
        if (!pathmap_order_pinned_index_pattern_safe(patterns[i]))
            return false;
    }
    return true;
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
    for (CettaIndex i = 0; i < s->native.len; i++) {
        Arena scratch;
        AtomId atom_id = shadow_storage_get_atom_id_at(s, i);
        if (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
            st->preserve_logical_order &&
            space_atom_id_requires_authored_order(s, atom_id, NULL)) {
            continue;
        }
        arena_init(&scratch);
        bool ok = imported_bridge_add_atom_structural(
            &scratch, (CettaMorkSpaceHandle *)st->bridge_space, s->native.universe,
            atom_id, NULL);
        arena_free(&scratch);
        if (!ok) {
            cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space);
            imported_mark_bridge_untrusted(s);
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
    st->native_shadow_synced = true;
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
    uint8_t *snapshot = NULL;
    uint8_t snapshot_bits = 0;
    AtomId *snapshot_ids = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t packet_rows = 0;
    CettaCount rows = 0;

    if (!s || !st || !st->bridge_space || !s->native.universe)
        return false;

    arena_init(&scratch);
    imported_bridge_expr_memo_init(&memo);
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        ok = cetta_mork_bridge_space_dump_contextual_exact_rows(
            (CettaMorkSpaceHandle *)st->bridge_space, &packet, &packet_len, &packet_rows);
        if (ok) {
            ok = imported_bridge_decode_contextual_exact_rows(
                s->native.universe, &scratch, &memo, packet, packet_len,
                &snapshot_ids, &rows);
            if (ok) {
                ok = imported_projected_pack_atom_ids(
                    s->native.universe, snapshot_ids, rows, &snapshot,
                    &snapshot_bits);
                free(snapshot_ids);
                snapshot_ids = NULL;
            }
        }
        cetta_mork_bridge_bytes_free(packet, packet_len);

        if (!ok) {
            space_match_backend_clear_error();
            ok = imported_bridge_decode_expr_rows_from_cursor(
                s->native.universe, &scratch, &memo,
                (CettaMorkSpaceHandle *)st->bridge_space,
                &snapshot_ids, &rows);
            if (!ok) {
                space_match_backend_clear_error();
                ok = imported_bridge_decode_expr_rows_from_dump(
                    s->native.universe, &scratch, &memo,
                    (CettaMorkSpaceHandle *)st->bridge_space,
                    &snapshot_ids, &rows);
            }
            if (ok) {
                ok = imported_projected_pack_atom_ids(
                    s->native.universe, snapshot_ids, rows, &snapshot,
                    &snapshot_bits);
                free(snapshot_ids);
                snapshot_ids = NULL;
            }
        }
    } else {
        Space staged;
        space_init_with_universe(&staged, s->native.universe);
        ok = imported_materialize_bridge_space(
            &staged, &scratch, (CettaMorkSpaceHandle *)st->bridge_space, NULL);
        if (ok)
            ok = imported_projected_pack_space_atom_ids(
                &staged, &snapshot, &snapshot_bits);
        rows = staged.native.len;
        space_free(&staged);
    }
    imported_bridge_expr_memo_free(&memo);
    if (!ok) {
        free(snapshot_ids);
        free(snapshot);
        arena_free(&scratch);
        return false;
    }
    imported_projection_clear(st);
    st->projected_atom_ids = snapshot;
    st->projected_len = rows;
    st->projected_atom_id_width_bits = snapshot_bits;
    st->projection_valid = true;
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PATHMAP_PROJECTION_CAPTURE);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_PATHMAP_PROJECTION_ROWS,
                                rows);
    }

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
    uint8_t *next_ids = NULL;
    uint8_t next_bits = 0;

    if (!s || !st)
        return false;
    if (st->native_shadow_synced)
        return true;
    if (!imported_storage_ensure_projection(s))
        return false;
    next_bits = (uint8_t)term_universe_store_format_atom_id_width_bits(
        term_universe_store_format(s->native.universe));
    if (st->projected_len > 0) {
        size_t width = cetta_atom_id_storage_width_bytes_from_bits(next_bits);
        if (width == 0 || (size_t)st->projected_len > SIZE_MAX / width)
            return false;
        next_ids = cetta_malloc(width * (size_t)st->projected_len);
        if (!next_ids)
            return false;
        for (CettaIndex i = 0; i < st->projected_len; i++) {
            if (!cetta_atom_id_storage_store_bits(
                    next_ids + ((size_t)i * width), next_bits,
                    imported_projected_atom_id_at(st, i))) {
                free(next_ids);
                return false;
            }
        }
    }
    free(s->native.atom_ids);
    s->native.atom_ids = next_ids;
    s->native.atom_id_width_bits = next_bits;
    s->native.start = 0;
    s->native.len = st->projected_len;
    s->native.cap = st->projected_len;
    space_mark_derived_state_dirty(s);
    st->native_shadow_synced = true;
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PATHMAP_SHADOW_REFRESH);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_PATHMAP_SHADOW_REFRESH_ATOMS,
                                st->projected_len);
    }
    return true;
}

bool space_match_backend_step(Space *s, Arena *persistent_arena,
                              uint64_t steps, uint64_t *out_performed) {
    MorkImportedState *mst;
    ImportedBridgeState *st;
    Space *fresh = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t packet_rows = 0;
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
        imported_mark_bridge_untrusted(s);
        return false;
    }
    if (performed == 0)
        return true;
    fresh = cetta_malloc(sizeof(Space));
    space_init_with_universe(fresh, s ? s->native.universe : NULL);
    fresh->kind = s->kind;
    if (!space_match_backend_try_set(fresh, s->match_backend.kind)) {
        imported_mark_bridge_untrusted(s);
        goto done;
    }
    ok = imported_materialize_bridge_space(
        fresh,
        persistent_arena,
        (CettaMorkSpaceHandle *)st->bridge_space,
        NULL);
    if (!ok) {
        imported_mark_bridge_untrusted(s);
        goto done;
    }

    space_replace_contents(s, fresh);
    if (out_performed)
        *out_performed = performed;
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
    uint64_t packet_rows = 0;
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
        imported_mark_bridge_untrusted(s);
        return false;
    }
    if (added == 0)
        return true;
    fresh = cetta_malloc(sizeof(Space));
    space_init_with_universe(fresh, s ? s->native.universe : NULL);
    fresh->kind = s->kind;
    if (!space_match_backend_try_set(fresh, s->match_backend.kind)) {
        imported_mark_bridge_untrusted(s);
        goto done;
    }
    ok = imported_materialize_bridge_space(
        fresh,
        persistent_arena,
        (CettaMorkSpaceHandle *)st->bridge_space,
        NULL);
    if (!ok) {
        imported_mark_bridge_untrusted(s);
        goto done;
    }

    space_replace_contents(s, fresh);
    if (out_added)
        *out_added = added;
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
    uint64_t packet_rows = 0;
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
        imported_mark_bridge_untrusted(s);
        return false;
    }
    if (removed == 0)
        return true;
    fresh = cetta_malloc(sizeof(Space));
    space_init_with_universe(fresh, s ? s->native.universe : NULL);
    fresh->kind = s->kind;
    if (!space_match_backend_try_set(fresh, s->match_backend.kind)) {
        imported_mark_bridge_untrusted(s);
        goto done;
    }
    ok = imported_materialize_bridge_space(
        fresh,
        persistent_arena,
        (CettaMorkSpaceHandle *)st->bridge_space,
        NULL);
    if (!ok) {
        imported_mark_bridge_untrusted(s);
        goto done;
    }

    space_replace_contents(s, fresh);
    if (out_removed)
        *out_removed = removed;
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
                                                         CettaIndex qi,
                                                         const ImportedFlatToken *c,
                                                         CettaIndex ci,
                                                         ImportedCorefState *refs,
                                                         CettaIndex *qnext,
                                                         CettaIndex *cnext) {
    const ImportedFlatToken *qroot = &q[qi];
    const ImportedFlatToken *croot = &c[ci];
    if (qroot->span == 0 || croot->span == 0 ||
        qi > UINT64_MAX - qroot->span || ci > UINT64_MAX - croot->span)
        return IMPORTED_COREF_FAIL;
    CettaIndex qend = qi + qroot->span;
    CettaIndex cend = ci + croot->span;

    while (qi < qend && ci < cend) {
        const ImportedFlatToken *qt = &q[qi];
        const ImportedFlatToken *ct = &c[ci];
        if (qt->span == 0 || ct->span == 0 ||
            qt->span > qend - qi || ct->span > cend - ci)
            return IMPORTED_COREF_FAIL;

        if (qt->kind == IMPORTED_FLAT_VAR) {
            if (ct->kind == IMPORTED_FLAT_VAR &&
                (imported_find_indexed_ref(refs, ct->var_id) ||
                 imported_find_indexed_value(refs, ct->var_id)))
                return IMPORTED_COREF_NEEDS_FALLBACK;
            ImportedCorefRef *existing =
                imported_find_query_ref(refs, qt->var_id);
            if (existing) {
                if (!imported_flat_equal(c, existing->idx, c, ci)) {
                    ImportedCorefVerdict bind;
                    if (c[existing->idx].kind == IMPORTED_FLAT_VAR)
                        bind = imported_bind_indexed_value(
                            refs, c, existing->idx, ci);
                    else if (ct->kind == IMPORTED_FLAT_VAR)
                        bind = imported_bind_indexed_value(
                            refs, c, ci, existing->idx);
                    else
                        return IMPORTED_COREF_NEEDS_FALLBACK;
                    if (bind != IMPORTED_COREF_EXACT) return bind;
                }
            } else {
                ImportedCorefVerdict add = imported_add_query_ref(
                    refs, qt->var_id, qt->sym_id, ci, ct->span, qt->origin);
                if (add != IMPORTED_COREF_EXACT) return add;
            }
            qi += qt->span;
            ci += ct->span;
            continue;
        }

        if (ct->kind == IMPORTED_FLAT_VAR) {
            ImportedCorefRef *value =
                imported_find_indexed_value(refs, ct->var_id);
            if (value) return IMPORTED_COREF_NEEDS_FALLBACK;
            ImportedCorefRef *existing =
                imported_find_indexed_ref(refs, ct->var_id);
            if (existing) {
                if (!imported_flat_equal(q, existing->idx, q, qi))
                    return IMPORTED_COREF_NEEDS_FALLBACK;
            } else {
                ImportedCorefVerdict add = imported_add_indexed_ref(
                    refs, ct->var_id, ct->sym_id, qi, qt->span, ct->origin);
                if (add != IMPORTED_COREF_EXACT) return add;
            }
            qi += qt->span;
            ci += ct->span;
            continue;
        }

        if (qt->kind != ct->kind) return IMPORTED_COREF_FAIL;
        switch (qt->kind) {
        case IMPORTED_FLAT_SYMBOL:
        case IMPORTED_FLAT_STRING:
        case IMPORTED_FLAT_BIGINT:
        case IMPORTED_FLAT_RATIONAL:
            if (qt->sym_id != ct->sym_id) return IMPORTED_COREF_FAIL;
            break;
        case IMPORTED_FLAT_INT:
            if (qt->ival != ct->ival) return IMPORTED_COREF_FAIL;
            break;
        case IMPORTED_FLAT_FLOAT:
            if (qt->fval != ct->fval) return IMPORTED_COREF_FAIL;
            break;
        case IMPORTED_FLAT_BOOL:
            if (qt->bval != ct->bval) return IMPORTED_COREF_FAIL;
            break;
        case IMPORTED_FLAT_GROUNDED_OTHER:
            if (!ct->origin || !atom_eq(qt->origin, ct->origin))
                return IMPORTED_COREF_FAIL;
            break;
        case IMPORTED_FLAT_EXPR:
            if (qt->arity != ct->arity) return IMPORTED_COREF_FAIL;
            break;
        case IMPORTED_FLAT_VAR:
            return IMPORTED_COREF_FAIL;
        }
        qi++;
        ci++;
    }
    if (qi != qend || ci != cend) return IMPORTED_COREF_FAIL;
    *qnext = qi;
    *cnext = ci;
    return IMPORTED_COREF_EXACT;
}

static bool imported_match_subtree_legacy(const ImportedFlatToken *q, CettaIndex qi,
                                          const ImportedFlatToken *c, CettaIndex ci,
                                          const TermUniverse *candidate_universe,
                                          Bindings *b, Arena *a, uint32_t epoch,
                                          CettaIndex *qnext, CettaIndex *cnext) {
    const ImportedFlatToken *qroot = &q[qi];
    const ImportedFlatToken *croot = &c[ci];
    if (qroot->span == 0 || croot->span == 0 ||
        qi > UINT64_MAX - qroot->span || ci > UINT64_MAX - croot->span)
        return false;
    CettaIndex qend = qi + qroot->span;
    CettaIndex cend = ci + croot->span;

    while (qi < qend && ci < cend) {
        const ImportedFlatToken *qt = &q[qi];
        const ImportedFlatToken *ct = &c[ci];
        if (qt->span == 0 || ct->span == 0 ||
            qt->span > qend - qi || ct->span > cend - ci)
            return false;

        if (qt->kind == IMPORTED_FLAT_VAR) {
            Atom *existing = bindings_lookup_id(b, qt->var_id);
            if (existing) {
                if (ct->origin_id != CETTA_ATOM_ID_NONE) {
                    if (!match_atoms_atom_id_epoch(
                            existing, candidate_universe, ct->origin_id,
                            b, a, epoch))
                        return false;
                } else if (!match_atoms_epoch(
                               existing,
                               imported_token_atom(ct, candidate_universe),
                               b, a, epoch)) {
                    return false;
                }
            } else {
                Atom *value = imported_token_copy_epoch(
                    a, ct, candidate_universe, epoch);
                if (!value || !bindings_add_id(
                        b, qt->var_id, qt->sym_id, value))
                    return false;
            }
            qi += qt->span;
            ci += ct->span;
            continue;
        }

        if (ct->kind == IMPORTED_FLAT_VAR) {
            VarId tagged_id = var_epoch_id(ct->var_id, epoch);
            Atom *existing = bindings_lookup_id(b, tagged_id);
            if (existing) {
                if (!match_atoms(qt->origin, existing, b)) return false;
            } else if (!bindings_add_id(
                           b, tagged_id, ct->sym_id, qt->origin)) {
                return false;
            }
            qi += qt->span;
            ci += ct->span;
            continue;
        }

        if (qt->kind != ct->kind) return false;
        switch (qt->kind) {
        case IMPORTED_FLAT_SYMBOL:
        case IMPORTED_FLAT_STRING:
        case IMPORTED_FLAT_BIGINT:
        case IMPORTED_FLAT_RATIONAL:
            if (qt->sym_id != ct->sym_id) return false;
            break;
        case IMPORTED_FLAT_INT:
            if (qt->ival != ct->ival) return false;
            break;
        case IMPORTED_FLAT_FLOAT:
            if (qt->fval != ct->fval) return false;
            break;
        case IMPORTED_FLAT_BOOL:
            if (qt->bval != ct->bval) return false;
            break;
        case IMPORTED_FLAT_GROUNDED_OTHER:
            if (!atom_eq(qt->origin,
                         imported_token_atom(ct, candidate_universe)))
                return false;
            break;
        case IMPORTED_FLAT_EXPR:
            if (qt->arity != ct->arity) return false;
            break;
        case IMPORTED_FLAT_VAR:
            return false;
        }
        qi++;
        ci++;
    }
    if (qi != qend || ci != cend) return false;
    *qnext = qi;
    *cnext = ci;
    return true;
}

static void imported_collect_bucket(const ImportedFlatBucket *bucket,
                                    const ImportedFlatToken *qtokens, CettaIndex qlen,
                                    const TermUniverse *candidate_universe,
                                    Arena *a, SubstMatchSet *out) {
    for (CettaIndex i = 0; i < bucket->len; i++) {
        const ImportedFlatEntry *entry = &bucket->entries[i];
        uint32_t match_epoch = fresh_var_suffix();
        if (entry->len == 0 || qlen == 0) continue;
        CettaIndex qnext = 0, cnext = 0;
        ImportedCorefState refs = {0};
        ImportedCorefVerdict verdict =
            imported_match_subtree_coref(qtokens, 0, entry->tokens, 0, &refs,
                                         &qnext, &cnext);
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
                                              match_epoch, &qnext, &cnext) &&
                qnext == qlen && cnext == entry->len &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, entry->atom_idx, match_epoch, &b, true);
            }
            bindings_free(&b);
        }
    }
}

static __attribute__((unused)) CettaIndex
imported_candidates_flat(Space *s, Atom *pattern, CettaIndex **out) {
    ImportedBridgeState *st = backend_bridge_state(s);
    *out = NULL;
    if (!st)
        return 0;
    CettaIndex len = 0, cap = 0;
    SymbolId head = atom_head_sym(pattern);
    if (head != SYMBOL_ID_NONE) {
        ImportedFlatBucket *bucket = &st->buckets[stree_head_hash(head)];
        for (CettaIndex i = 0; i < bucket->len; i++) {
            if (len >= cap) {
                cap = cap ? cap * 2 : 8;
                *out = cetta_realloc(*out, sizeof(CettaIndex) * cap);
            }
            (*out)[len++] = bucket->entries[i].atom_idx;
        }
    } else {
        for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++) {
            ImportedFlatBucket *bucket = &st->buckets[bi];
            for (CettaIndex i = 0; i < bucket->len; i++) {
                if (len >= cap) {
                    cap = cap ? cap * 2 : 8;
                    *out = cetta_realloc(*out, sizeof(CettaIndex) * cap);
                }
                (*out)[len++] = bucket->entries[i].atom_idx;
            }
        }
    }
    for (CettaIndex i = 0; i < st->wildcard.len; i++) {
        if (len >= cap) {
            cap = cap ? cap * 2 : 8;
            *out = cetta_realloc(*out, sizeof(CettaIndex) * cap);
        }
        (*out)[len++] = st->wildcard.entries[i].atom_idx;
    }
    if (len > 1)
        len = sort_unique_cetta_index(*out, len);
    return len;
}

static CettaIndex imported_candidates(Space *s, Atom *pattern, CettaIndex **out) {
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
                              CettaIndex atom_idx) {
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

static void imported_mark_bridge_untrusted(Space *s) {
    ImportedBridgeState *st = backend_bridge_state(s);
    if (!s || !st)
        return;
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    st->bridge_active = false;
    st->built = false;
    st->dirty = true;
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP)
        st->bridge_unavailable = true;
    if (s->match_backend.kind == SPACE_ENGINE_MORK) {
        s->match_backend.mork.attached_compiled = false;
        s->match_backend.mork.attached_count = 0;
    }
}

static bool pathmap_local_ensure_bridge_live(Space *s) {
    ImportedBridgeState *st;
    if (!s || space_is_ordered(s))
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
        st->native_shadow_synced = true;
        return true;
    }
    return backend_rebuild_bridge(s);
}

static CettaIndex pathmap_local_candidates(Space *s, Atom *pattern,
                                           CettaIndex **out) {
    CettaIndex n;
    if (!out) {
        return 0;
    }
    /* PeTTa snapshots a complete candidate superset to implement logical
       update semantics.  When a synchronized AtomId occurrence shadow is
       available, its native discrimination index supplies that same ordered
       superset without scanning every PathMap row for every choice point. */
    if (!s->match_backend.pathmap.bridge.native_shadow_synced)
        (void)pathmap_materialize_native_storage(s, NULL);
    if (s->match_backend.pathmap.bridge.native_shadow_synced)
        return native_candidates(s, pattern, out);
    n = imported_logical_len(s);
    *out = cetta_malloc(sizeof(CettaIndex) * (n ? n : 1u));
    for (CettaIndex i = 0; i < n; i++)
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
    if (space_is_ordered(s)) {
        native_query(s, a, query, out);
        return;
    }
    if (st->preserve_logical_order) {
        /* An unordered access path is observer-safe when it proves that the
         * query has at most one occurrence.  Larger answer bags return to the
         * authored-order native view; no bridge row is published first. */
        uint64_t indexed_count = 0u;
        Atom *factor = query;
        if (pathmap_indexed_single_factor_enabled() && !epoch_query &&
            !bridge_query &&
            pathmap_order_pinned_index_pattern_safe(query) &&
            pathmap_local_count_conjunction(
                s, a, &factor, 1u, NULL, &indexed_count) &&
            indexed_count <= 1u) {
            if (indexed_count == 0u) {
                smset_init(out);
                return;
            }
            if (imported_bridge_query_conjunction_fast(
                    s, a, &factor, 1u, NULL, &direct_matches, true)) {
                if (direct_matches.len == 1u) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V3_HIT);
                    imported_binding_set_to_exact_matches(
                        out, &direct_matches);
                    binding_set_free(&direct_matches);
                    return;
                }
                binding_set_free(&direct_matches);
            } else {
                binding_set_free(&direct_matches);
            }
            space_match_backend_clear_error();
        }
        Arena *persist = eval_current_persistent_arena();
        if (!space_match_backend_materialize_native_storage(
                s, persist ? persist : a)) {
            smset_init(out);
            return;
        }
        native_candidate_exact_query(s, a, query, out);
        return;
    }
    if (imported_logical_len(s) == 0) {
        smset_init(out);
        return;
    }
    if (pathmap_indexed_single_factor_enabled() && !epoch_query &&
        !bridge_query &&
        pathmap_local_ensure_bridge_live(s) && st->bridge_space) {
        Atom *factor = query;
        if (imported_bridge_query_conjunction_fast(
                s, a, &factor, 1u, NULL, &direct_matches, true)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V3_HIT);
            imported_binding_set_to_exact_matches(out, &direct_matches);
            binding_set_free(&direct_matches);
            return;
        }
        binding_set_free(&direct_matches);
        space_match_backend_clear_error();
    }
    if (var_query && !epoch_query && !bridge_query &&
        !space_contains_only_exact_atoms(s)) {
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
        if (space_contains_only_exact_atoms(s)) {
            imported_binding_set_to_exact_matches(out, &direct_matches);
            binding_set_free(&direct_matches);
            return;
        }
        binding_set_free(&direct_matches);
        Arena *persist = eval_current_persistent_arena();
        if (!space_match_backend_materialize_native_storage(
                s, persist ? persist : a)) {
            smset_init(out);
            return;
        }
        /* A ground query can still match a stored-side variable.  The bridge
           answer is complete only when every stored atom is exact. */
        native_candidate_exact_query(s, a, query, out);
        return;
    }
    imported_ensure_built_flat(s);
    imported_query_flat(s, a, query, out);
}

static void pathmap_local_query_conjunction(Space *s, Arena *a,
                                            Atom **patterns, CettaExprLen npatterns,
                                            const Bindings *seed, BindingSet *out) {
    ImportedBridgeState *st = &s->match_backend.pathmap.bridge;
    if (space_is_ordered(s) || st->preserve_logical_order) {
        if (st->preserve_logical_order) {
            Arena *persist = eval_current_persistent_arena();
            if (!space_match_backend_materialize_native_storage(
                    s, persist ? persist : a)) {
                binding_set_init(out);
                return;
            }
        }
        space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
        return;
    }
    if (pathmap_indexed_factor_count_enabled(npatterns) &&
        pathmap_local_ensure_bridge_live(s) && st->bridge_space &&
        imported_bridge_query_conjunction_fast(
            s, a, patterns, npatterns, seed, out, true)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V3_HIT);
        return;
    }
    space_match_backend_clear_error();
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
    st->native_shadow_synced = false;
    space_discard_native_logical_view_preserving_dispatch(s);
}

/* A bridge transaction expressed entirely in the destination universe's
 * AtomIds preserves an already-synchronized occurrence shadow.  The caller
 * appends the successfully published ids after this returns. */
static void pathmap_local_backend_primary_known_ids_commit(
    Space *s, ImportedBridgeState *st) {
    bool keep_native_shadow;
    if (!s || !st)
        return;
    keep_native_shadow = st->native_shadow_synced;
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    st->built = false;
    st->dirty = false;
    st->bridge_unavailable = false;
    if (!keep_native_shadow)
        space_discard_native_logical_view_preserving_dispatch(s);
    st->native_shadow_synced = keep_native_shadow;
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

static bool transfer_mark_bridge_destination(Space *dst, uint64_t added,
                                             uint64_t logical) {
    ImportedBridgeState *st = backend_bridge_state(dst);
    MorkImportedState *mst = mork_imported_state(dst);

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
        if (mst) {
            mst->attached_compiled = true;
            mst->attached_count = logical;
        }
        space_discard_native_logical_view(dst);
    } else {
        return false;
    }

    space_note_external_backend_mutation(dst);
    return true;
}

static bool transfer_space_can_try_bridge_rows(Space *s) {
    ImportedBridgeState *st;

    if (!s || space_is_ordered(s) ||
        !space_engine_uses_pathmap(s->match_backend.kind)) {
        return false;
    }
    st = backend_bridge_state(s);
    return st && !st->bridge_unavailable &&
           !(s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
             st->preserve_logical_order);
}

static SpaceTransferResult transfer_bridge_logical_rows_direct(Space *dst, Space *src,
                                                               uint64_t *out_added) {
    ImportedBridgeState *dst_st = NULL;
    CettaMorkSpaceHandle *dst_bridge = NULL;
    CettaMorkSpaceHandle *dst_clone = NULL;
    CettaMorkSpaceHandle *src_bridge = NULL;
    uint64_t logical = 0;
    uint64_t added = 0;

    if (!transfer_space_can_try_bridge_rows(dst) ||
        !transfer_space_can_try_bridge_rows(src)) {
        return SPACE_TRANSFER_NEEDS_TEXT_FALLBACK;
    }
    if (!transfer_ensure_bridge_live(dst, &dst_bridge) ||
        !transfer_ensure_bridge_live(src, &src_bridge)) {
        return SPACE_TRANSFER_ERROR;
    }
    dst_st = backend_bridge_state(dst);
    if (!dst_st || !dst_bridge || !src_bridge)
        return SPACE_TRANSFER_ERROR;

    dst_clone = cetta_mork_bridge_space_clone(dst_bridge);
    if (!dst_clone)
        return SPACE_TRANSFER_ERROR;

    if (!cetta_mork_bridge_space_add_logical_rows_from(dst_clone, src_bridge,
                                                       &added)) {
        cetta_mork_bridge_space_free(dst_clone);
        return SPACE_TRANSFER_ERROR;
    }
    if (added == 0) {
        cetta_mork_bridge_space_free(dst_clone);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PATHMAP_DIRECT_TRANSFER_CALL);
        if (out_added)
            *out_added = 0;
        return SPACE_TRANSFER_OK;
    }

    if (dst->match_backend.kind == SPACE_ENGINE_MORK) {
        if (!bridge_handle_logical_len64(dst_clone, &logical)) {
            cetta_mork_bridge_space_free(dst_clone);
            return SPACE_TRANSFER_ERROR;
        }
    }
    if (!transfer_mark_bridge_destination(dst, added, logical)) {
        cetta_mork_bridge_space_free(dst_clone);
        return SPACE_TRANSFER_ERROR;
    }

    cetta_mork_bridge_space_free(dst_bridge);
    dst_st->bridge_space = dst_clone;
    dst_st->bridge_active = true;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PATHMAP_DIRECT_TRANSFER_CALL);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PATHMAP_DIRECT_TRANSFER_OCCURRENCE, added);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PATHMAP_DIRECT_TRANSFER_PUBLICATION);
    if (out_added)
        *out_added = added;
    return SPACE_TRANSFER_OK;
}

static bool transfer_atom_ids_direct(Space *dst, Space *src,
                                     Arena *persistent_arena,
                                     uint64_t *out_added) {
    CettaCount logical_len = 0;
    uint32_t narrow_len = 0;
    uint64_t added = 0;

    if (!dst || !src)
        return false;

    logical_len = space_length64(src);
    if (src->native.len != logical_len) {
        if (!space_length_u32_checked(src, &narrow_len))
            return false;
        logical_len = narrow_len;
    }
    for (CettaIndex i = 0; i < logical_len; i++) {
        AtomId atom_id = space_get_atom_id_at64(src, i);
        if (atom_id != CETTA_ATOM_ID_NONE &&
            dst->native.universe == src->native.universe) {
            space_add_atom_id(dst, atom_id);
            added++;
            continue;
        }

        Atom *atom = space_get_at64(src, i);
        if (!atom) {
            space_match_backend_set_error(
                SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE);
            return false;
        }
        if (!space_admit_atom(dst, persistent_arena, atom))
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
        if (!space_engine_uses_pathmap(src.space->match_backend.kind))
            return SPACE_TRANSFER_NEEDS_TEXT_FALLBACK;
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
    /* This recovery path is used after a failed single-row bridge add.  The
       bridge add wrappers validate and preflight the known failure modes before
       mutating; if that FFI contract changes, this path must become clone-based
       so a failed add cannot be projected into the native shadow. */
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
    CettaMorkSpaceHandle *clone = NULL;
    uint64_t changed = 0;

    if (out_changed)
        *out_changed = 0;
    if (!s || s->match_backend.kind != SPACE_ENGINE_PATHMAP || space_is_ordered(s))
        return false;
    if (s->match_backend.pathmap.bridge.bridge_unavailable ||
        s->match_backend.pathmap.bridge.preserve_logical_order)
        return false;
    if (!(text || len == 0))
        return false;
    if (imported_text_may_contain_vars(text, len))
        return false;

    st = &s->match_backend.pathmap.bridge;
    if (!pathmap_local_ensure_bridge_live(s) || !st->bridge_active || !st->bridge_space)
        return false;

    clone = cetta_mork_bridge_space_clone((CettaMorkSpaceHandle *)st->bridge_space);
    if (!clone)
        return false;

    if (!(remove_atoms
              ? cetta_mork_bridge_space_remove_sexpr(
                    clone, text, len, &changed)
              : cetta_mork_bridge_space_add_sexpr(
                    clone, text, len, &changed))) {
        cetta_mork_bridge_space_free(clone);
        return false;
    }

    if (out_changed)
        *out_changed = changed;
    if (changed == 0) {
        cetta_mork_bridge_space_free(clone);
        return true;
    }

    cetta_mork_bridge_space_free((CettaMorkSpaceHandle *)st->bridge_space);
    st->bridge_space = clone;
    st->bridge_active = true;
    pathmap_local_backend_primary_bulk_commit(s, st);
    space_note_external_backend_mutation(s);
    return true;
}

static SpaceBackendBatchResult pathmap_local_store_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_added) {
    ImportedBridgeState *st;
    Arena scratch;
    CettaMorkSpaceHandle *transaction = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t added = 0;
    const bool emit_timing = cetta_runtime_timing_is_enabled();
    uint64_t started_ns = 0;
    const char *encode_error = NULL;

    if (out_added)
        *out_added = 0;
    if (!s || s->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        space_is_ordered(s) || (!atom_ids && atom_count != 0) ||
        !s->native.universe) {
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }
    if (atom_count == 0)
        return SPACE_BACKEND_BATCH_APPLIED;
    if (!pathmap_batch_mutation_enabled())
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    st = &s->match_backend.pathmap.bridge;
    if (st->bridge_unavailable || st->preserve_logical_order ||
        !cetta_mork_bridge_supports_expr_bytes_batch_add())
        return SPACE_BACKEND_BATCH_UNSUPPORTED;

    /* Build the complete transaction before touching backend state.  The
       compact batch is the fast fragment; variables and target-local symbol
       normalization deliberately fall back to the singular oracle. */
    started_ns = emit_timing ? pathmap_monotonic_ns() : 0;
    arena_init(&scratch);
    bool packed = cetta_mm2_atom_ids_to_bridge_expr_bytes_batch(
        &scratch, s->native.universe, atom_ids, atom_count,
        &packet, &packet_len, &encode_error);
    arena_free(&scratch);
    pathmap_record_elapsed(CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_PACK_NS,
                           started_ns);
    (void)encode_error;
    if (!packed) {
        free(packet);
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }
    if (!pathmap_local_ensure_bridge_live(s) ||
        !st->bridge_active || !st->bridge_space) {
        free(packet);
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }

    started_ns = emit_timing ? pathmap_monotonic_ns() : 0;
    transaction = cetta_mork_bridge_space_clone(
        (CettaMorkSpaceHandle *)st->bridge_space);
    pathmap_record_elapsed(CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_CLONE_NS,
                           started_ns);
    if (!transaction) {
        free(packet);
        return SPACE_BACKEND_BATCH_ERROR;
    }

    /* Mutate a structurally shared snapshot and publish only after the bridge
       accepts the complete occurrence count.  Preflight catches expected
       failures cheaply; clone-and-swap also makes unexpected bridge failures
       rollback-safe. */
    started_ns = emit_timing ? pathmap_monotonic_ns() : 0;
    bool ok = cetta_mork_bridge_space_add_expr_bytes_batch(
        transaction, packet, packet_len, &added);
    pathmap_record_elapsed(CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_FFI_NS,
                           started_ns);
    free(packet);
    if (!ok || added != (uint64_t)atom_count) {
        cetta_mork_bridge_space_free(transaction);
        return SPACE_BACKEND_BATCH_ERROR;
    }

    cetta_mork_bridge_space_free(
        (CettaMorkSpaceHandle *)st->bridge_space);
    st->bridge_space = transaction;
    st->bridge_active = true;
    pathmap_local_backend_primary_known_ids_commit(s, st);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_MUTATION_CALL);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_MUTATION_OCCURRENCE, added);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_MUTATION_PUBLICATION);
    if (out_added)
        *out_added = added;
    return SPACE_BACKEND_BATCH_APPLIED;
}

static bool pathmap_local_store_atom_id_direct(Space *s, AtomId atom_id, Atom *atom) {
    ImportedBridgeState *st;
    bool had_live_bridge;
    bool prior_built;
    bool prior_dirty;
    if (!s || atom_id == CETTA_ATOM_ID_NONE || !s->native.universe || space_is_ordered(s))
        return false;
    st = &s->match_backend.pathmap.bridge;
    if (s->match_backend.pathmap.bridge.bridge_unavailable ||
        s->match_backend.pathmap.bridge.preserve_logical_order)
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
    pathmap_local_backend_primary_known_ids_commit(s, st);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PATHMAP_DIRECT_STORE);
    return true;
}

static bool pathmap_local_store_atom_direct(Space *s, Atom *atom) {
    ImportedBridgeState *st;
    bool had_live_bridge;
    bool prior_built;
    bool prior_dirty;
    if (!s || !atom || space_is_ordered(s))
        return false;
    st = &s->match_backend.pathmap.bridge;
    if (st->bridge_unavailable || st->preserve_logical_order)
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
    bool ok = atom_has_vars(atom)
                  ? imported_bridge_add_atom_contextual_exact_atom(
                        &scratch, (CettaMorkSpaceHandle *)st->bridge_space,
                        atom)
                  : imported_bridge_add_atom_structural(
                        &scratch, (CettaMorkSpaceHandle *)st->bridge_space,
                        NULL, CETTA_ATOM_ID_NONE, atom);
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
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PATHMAP_DIRECT_STORE);
    return true;
}

static AtomId pathmap_local_projected_remove_match(Space *s,
                                                   AtomId requested_id,
                                                   Atom *requested_atom) {
    ImportedBridgeState *st;
    Atom *target = requested_atom;
    AtomId alpha_match = CETTA_ATOM_ID_NONE;
    CettaCount alpha_count = 0;

    if (!s || !s->native.universe)
        return CETTA_ATOM_ID_NONE;
    st = &s->match_backend.pathmap.bridge;
    if (!target && requested_id != CETTA_ATOM_ID_NONE)
        target = term_universe_get_atom(s->native.universe, requested_id);
    if (!target || !imported_storage_ensure_projection(s))
        return CETTA_ATOM_ID_NONE;

    /* Native remove semantics prefer any exact occurrence immediately. */
    for (CettaIndex i = 0; i < st->projected_len; i++) {
        AtomId candidate_id = imported_projected_atom_id_at(st, i);
        Atom *candidate;
        if (candidate_id == CETTA_ATOM_ID_NONE)
            continue;
        if (candidate_id == requested_id)
            return candidate_id;
        candidate = term_universe_get_atom(s->native.universe, candidate_id);
        if (candidate && atom_eq(candidate, target))
            return candidate_id;
    }

    /* If there is no exact occurrence, native CeTTa removes a renamed row
       only when its alpha-equivalent occurrence is unique. */
    for (CettaIndex i = 0; i < st->projected_len; i++) {
        AtomId candidate_id = imported_projected_atom_id_at(st, i);
        Atom *candidate;
        if (candidate_id == CETTA_ATOM_ID_NONE)
            continue;
        candidate = term_universe_get_atom(s->native.universe, candidate_id);
        if (candidate && atom_alpha_eq(candidate, target)) {
            alpha_match = candidate_id;
            alpha_count++;
            if (alpha_count > 1)
                return CETTA_ATOM_ID_NONE;
        }
    }
    return alpha_count == 1 ? alpha_match : CETTA_ATOM_ID_NONE;
}

static bool pathmap_local_remove_atom_id_direct(Space *s, AtomId atom_id) {
    ImportedBridgeState *st;
    CettaMorkSpaceHandle *clone = NULL;
    Arena scratch;
    uint64_t removed = 0;
    bool ok = false;

    if (!s || atom_id == CETTA_ATOM_ID_NONE || !s->native.universe || space_is_ordered(s))
        return false;
    if (s->match_backend.pathmap.bridge.bridge_unavailable ||
        s->match_backend.pathmap.bridge.preserve_logical_order)
        return false;
    st = &s->match_backend.pathmap.bridge;
    if (!(st->bridge_active && st->bridge_space) &&
        !pathmap_local_ensure_bridge_live(s))
        return false;

    clone = cetta_mork_bridge_space_clone((CettaMorkSpaceHandle *)st->bridge_space);
    if (!clone)
        return false;

    arena_init(&scratch);
    ok = tu_has_vars(s->native.universe, atom_id)
             ? imported_bridge_remove_atom_contextual_exact(
                   &scratch, clone,
                   s->native.universe, atom_id, &removed)
             : imported_bridge_remove_atom_structural(
                   &scratch, clone,
                   s->native.universe, atom_id, NULL, &removed);
    if (ok && removed == 0 && tu_has_vars(s->native.universe, atom_id)) {
        AtomId projected_id = pathmap_local_projected_remove_match(
            s, atom_id, NULL);
        if (projected_id != CETTA_ATOM_ID_NONE) {
            ok = tu_has_vars(s->native.universe, projected_id)
                     ? imported_bridge_remove_atom_contextual_exact(
                           &scratch, clone, s->native.universe, projected_id,
                           &removed)
                     : imported_bridge_remove_atom_structural(
                           &scratch, clone, s->native.universe, projected_id,
                           NULL, &removed);
        }
    }
    arena_free(&scratch);
    if (!ok) {
        cetta_mork_bridge_space_free(clone);
        return false;
    }
    if (removed == 0) {
        cetta_mork_bridge_space_free(clone);
        return false;
    }

    cetta_mork_bridge_space_free((CettaMorkSpaceHandle *)st->bridge_space);
    st->bridge_space = clone;
    st->bridge_active = true;
    pathmap_local_backend_primary_bulk_commit(s, st);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PATHMAP_DIRECT_REMOVE);
    return true;
}

static bool pathmap_local_remove_atom_direct(Space *s, Atom *atom) {
    ImportedBridgeState *st;
    CettaMorkSpaceHandle *clone = NULL;
    Arena scratch;
    uint64_t removed = 0;
    bool ok = false;

    if (!s || !atom || space_is_ordered(s))
        return false;
    if (s->match_backend.pathmap.bridge.bridge_unavailable ||
        s->match_backend.pathmap.bridge.preserve_logical_order)
        return false;
    st = &s->match_backend.pathmap.bridge;
    if (!(st->bridge_active && st->bridge_space) &&
        !pathmap_local_ensure_bridge_live(s))
        return false;

    clone = cetta_mork_bridge_space_clone((CettaMorkSpaceHandle *)st->bridge_space);
    if (!clone)
        return false;

    arena_init(&scratch);
    ok = atom_has_vars(atom)
             ? imported_bridge_remove_atom_contextual_exact_atom(
                   &scratch, clone,
                   atom, &removed)
             : imported_bridge_remove_atom_structural(
                   &scratch, clone,
                   s->native.universe, CETTA_ATOM_ID_NONE, atom, &removed);
    if (ok && removed == 0 && atom_has_vars(atom)) {
        AtomId projected_id = pathmap_local_projected_remove_match(
            s, CETTA_ATOM_ID_NONE, atom);
        if (projected_id != CETTA_ATOM_ID_NONE) {
            ok = tu_has_vars(s->native.universe, projected_id)
                     ? imported_bridge_remove_atom_contextual_exact(
                           &scratch, clone, s->native.universe, projected_id,
                           &removed)
                     : imported_bridge_remove_atom_structural(
                           &scratch, clone, s->native.universe, projected_id,
                           NULL, &removed);
        }
    }
    arena_free(&scratch);
    if (!ok) {
        cetta_mork_bridge_space_free(clone);
        return false;
    }
    if (removed == 0) {
        cetta_mork_bridge_space_free(clone);
        return false;
    }

    cetta_mork_bridge_space_free((CettaMorkSpaceHandle *)st->bridge_space);
    st->bridge_space = clone;
    st->bridge_active = true;
    pathmap_local_backend_primary_bulk_commit(s, st);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PATHMAP_DIRECT_REMOVE);
    return true;
}

static SpaceBackendBatchResult pathmap_local_remove_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_removed) {
    ImportedBridgeState *st;
    Arena scratch;
    CettaMorkSpaceHandle *transaction = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t removed = 0;
    const bool emit_timing = cetta_runtime_timing_is_enabled();
    uint64_t started_ns = 0;
    const char *encode_error = NULL;

    if (out_removed)
        *out_removed = 0;
    if (!s || s->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        space_is_ordered(s) || (!atom_ids && atom_count != 0) ||
        !s->native.universe) {
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }
    if (atom_count == 0)
        return SPACE_BACKEND_BATCH_APPLIED;
    if (!pathmap_batch_mutation_enabled())
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    st = &s->match_backend.pathmap.bridge;
    if (st->bridge_unavailable || st->preserve_logical_order ||
        !cetta_mork_bridge_supports_expr_bytes_batch_remove())
        return SPACE_BACKEND_BATCH_UNSUPPORTED;

    started_ns = emit_timing ? pathmap_monotonic_ns() : 0;
    arena_init(&scratch);
    bool packed = cetta_mm2_atom_ids_to_bridge_expr_bytes_batch(
        &scratch, s->native.universe, atom_ids, atom_count,
        &packet, &packet_len, &encode_error);
    arena_free(&scratch);
    pathmap_record_elapsed(CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_PACK_NS,
                           started_ns);
    (void)encode_error;
    if (!packed) {
        free(packet);
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }
    if (!pathmap_local_ensure_bridge_live(s) ||
        !st->bridge_active || !st->bridge_space) {
        free(packet);
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }

    started_ns = emit_timing ? pathmap_monotonic_ns() : 0;
    transaction = cetta_mork_bridge_space_clone(
        (CettaMorkSpaceHandle *)st->bridge_space);
    pathmap_record_elapsed(CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_CLONE_NS,
                           started_ns);
    if (!transaction) {
        free(packet);
        return SPACE_BACKEND_BATCH_ERROR;
    }

    started_ns = emit_timing ? pathmap_monotonic_ns() : 0;
    bool ok = cetta_mork_bridge_space_remove_expr_bytes_batch(
        transaction, packet, packet_len, &removed);
    pathmap_record_elapsed(CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_FFI_NS,
                           started_ns);
    free(packet);
    if (!ok) {
        cetta_mork_bridge_space_free(transaction);
        return SPACE_BACKEND_BATCH_ERROR;
    }

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_MUTATION_CALL);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_MUTATION_OCCURRENCE,
        (uint64_t)atom_count);
    if (removed != 0) {
        cetta_mork_bridge_space_free(
            (CettaMorkSpaceHandle *)st->bridge_space);
        st->bridge_space = transaction;
        st->bridge_active = true;
        pathmap_local_backend_primary_bulk_commit(s, st);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PATHMAP_BATCH_MUTATION_PUBLICATION);
    } else {
        cetta_mork_bridge_space_free(transaction);
    }
    if (out_removed)
        *out_removed = removed;
    return SPACE_BACKEND_BATCH_APPLIED;
}

static bool pathmap_local_truncate_direct(Space *s, uint64_t new_len) {
    ImportedBridgeState *st;
    CettaMorkSpaceHandle *clone = NULL;
    uint64_t logical_len;
    bool ok = true;

    if (!s || s->match_backend.kind != SPACE_ENGINE_PATHMAP || space_is_ordered(s))
        return false;
    if (s->match_backend.pathmap.bridge.bridge_unavailable ||
        s->match_backend.pathmap.bridge.preserve_logical_order)
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
        CettaMorkSpaceHandle *empty = cetta_mork_bridge_space_new_pathmap();
        if (!empty)
            return false;
        cetta_mork_bridge_space_free((CettaMorkSpaceHandle *)st->bridge_space);
        st->bridge_space = empty;
        st->bridge_active = true;
        pathmap_local_backend_primary_bulk_commit(s, st);
        return true;
    }

    if (!imported_storage_ensure_projection(s))
        return false;
    if ((uint64_t)st->projected_len != logical_len)
        return false;

    clone = cetta_mork_bridge_space_clone((CettaMorkSpaceHandle *)st->bridge_space);
    if (!clone)
        return false;

    for (CettaIndex i = st->projected_len; i > (CettaIndex)new_len; i--) {
        Arena scratch;
        uint64_t removed = 0;
        AtomId atom_id = imported_projected_atom_id_at(st, i - 1u);

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
                                   CettaIndex atom_idx) {
    ImportedBridgeState *st = &s->match_backend.pathmap.bridge;
    imported_projection_clear(st);
    /* This hook runs after the native append.  Any live bridge update below
       mirrors that same atom; otherwise native storage remains authoritative. */
    st->native_shadow_synced = true;
    if (st->preserve_logical_order)
        native_note_add(s, atom_id, atom, atom_idx);
    if (st->preserve_logical_order &&
        space_atom_id_requires_authored_order(s, atom_id, atom)) {
        return;
    }
    if (st->preserve_logical_order && !st->bridge_active) {
        if (!backend_rebuild_bridge(s)) {
            st->built = false;
            st->dirty = true;
        }
        return;
    }
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
        imported_projection_clear(st);
        imported_flat_state_clear(st);
        st->bridge_active = false;
        st->bridge_unavailable = true;
        st->built = false;
        st->dirty = true;
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
    if (st->preserve_logical_order)
        native_note_remove(s);
    imported_projection_clear(st);
    imported_flat_state_clear(st);
    st->bridge_active = false;
    st->bridge_unavailable = false;
    st->built = false;
    st->dirty = true;
    st->native_shadow_synced = true;
}

static void pathmap_local_free_backend(Space *s) {
    imported_state_free(&s->match_backend.pathmap.bridge);
}

static void imported_free_backend(Space *s) {
    imported_state_free(&s->match_backend.mork.bridge);
    s->match_backend.mork.attached_compiled = false;
    s->match_backend.mork.attached_count = 0;
}

static CettaIndex all_linear_candidates(Space *s, CettaIndex **out) {
    CettaIndex len = s ? s->native.len : 0;
    *out = cetta_malloc(sizeof(CettaIndex) * (len ? len : 1u));
    for (CettaIndex i = 0; i < len; i++)
        (*out)[i] = i;
    return len;
}

static void native_candidate_exact_query(Space *s, Arena *a, Atom *query,
                                         SubstMatchSet *out) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_NATIVE_PROBE);
    smset_init(out);
    if (s->native.len == 0) return;
    CettaIndex *candidates = NULL;
    bool native_index_authoritative =
        s->match_backend.kind == SPACE_ENGINE_NATIVE ||
        s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT ||
        (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
         s->match_backend.pathmap.bridge.preserve_logical_order);
    CettaIndex ncand =
        native_index_authoritative
            ? native_candidates(s, query, &candidates)
            : all_linear_candidates(s, &candidates);
    for (CettaIndex ci = 0; ci < ncand; ci++) {
        CettaIndex idx = candidates[ci];
        if (idx >= s->native.len) continue;
        uint32_t epoch = fresh_var_suffix();
        Bindings b;
        bindings_init(&b);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_NATIVE_CANDIDATE);
        if (match_space_atom_epoch(s, idx, query, &b, a, epoch) &&
            !bindings_has_loop(&b)) {
            /* This lane has already re-derived the complete match against the
               selected atom.  Mark the bindings final so consumers do not
               redundantly match the same pair again.  Besides wasting work,
               that second traversal can reject a deep value which was bound
               atomically to a query variable during the first match. */
            subst_matchset_push(out, idx, epoch, &b, true);
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
    CettaExprIndex idx;
    CettaIndex estimate;
} ImportedConjStep;

typedef struct {
    Arena scratch;
    ImportedBridgeVarMap query_vars;
    Atom **grounded;
    char *query_text;
    bool initialized;
} ImportedPreparedConjunction;

static CettaIndex imported_pattern_estimate(Space *s, Atom *pattern) {
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
    CettaIndex total = st->wildcard.len;
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

static void imported_prepared_conjunction_free(
    ImportedPreparedConjunction *prepared) {
    if (!prepared || !prepared->initialized)
        return;
    imported_bridge_varmap_free(&prepared->query_vars);
    arena_free(&prepared->scratch);
    memset(prepared, 0, sizeof(*prepared));
}

/*
 * Prepare the one canonical conjunction representation consumed by indexed
 * enumeration, aggregate pushdown, and semi-naive evaluation. Keeping this
 * in one seam prevents variable-slot order and admitted query shapes from
 * drifting between those execution modes.
 */
static bool imported_prepare_conjunction(
    Space *s, Atom **patterns, CettaExprLen npatterns, const Bindings *seed,
    bool record_seed_apply, ImportedPreparedConjunction *prepared) {
    ImportedConjStep order[IMPORTED_CONJUNCTION_PATTERN_LIMIT];

    if (!prepared)
        return false;
    memset(prepared, 0, sizeof(*prepared));
    if (!s || !patterns || npatterns == 0u ||
        npatterns > IMPORTED_CONJUNCTION_PATTERN_LIMIT) {
        return false;
    }

    arena_init(&prepared->scratch);
    arena_set_hashcons(&prepared->scratch, NULL);
    imported_bridge_varmap_init(&prepared->query_vars);
    prepared->initialized = true;

    for (CettaExprIndex i = 0; i < npatterns; i++) {
        order[i].pattern = patterns[i];
        order[i].idx = i;
        order[i].estimate = imported_pattern_estimate(s, patterns[i]);
    }
    qsort(order, npatterns, sizeof(ImportedConjStep),
          imported_cmp_conj_step);

    prepared->grounded =
        arena_alloc(&prepared->scratch, sizeof(Atom *) * npatterns);
    for (CettaExprIndex i = 0; i < npatterns; i++) {
        if (seed && record_seed_apply) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_SPACE_CONJ_IMPORTED);
        }
        prepared->grounded[i] =
            seed ? bindings_apply_if_vars(
                       seed, &prepared->scratch, order[i].pattern)
                 : order[i].pattern;
        if (!imported_bridge_collect_vars(
                prepared->grounded[i], &prepared->query_vars)) {
            return false;
        }
    }
    if (!imported_bridge_query_var_slots_contextual_ok(
            &prepared->query_vars, false)) {
        return false;
    }

    for (CettaExprIndex i = 0; i < npatterns; i++) {
        prepared->grounded[i] =
            imported_bridge_alpha_canonicalize_query(
                &prepared->scratch, prepared->grounded[i],
                &prepared->query_vars);
        if (!prepared->grounded[i])
            return false;
    }
    prepared->query_text = imported_bridge_build_conjunction_text(
        &prepared->scratch, prepared->grounded, npatterns);
    return prepared->query_text &&
           !imported_bridge_query_text_has_internal_vars(
               prepared->query_text);
}

/* Pull one admitted flat query through the same indexed cursor used by the
   conjunction engine, but deliver each counted occurrence directly to the
   consumer.  `out_attempted` becomes true only after cursor creation: before
   that point the caller may use its oracle; afterward replay would duplicate
   an already-observed prefix. */
static bool pathmap_local_visit_bindings_indexed(
    Space *s,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx,
    bool *out_attempted) {
    ImportedBridgeState *st;
    ImportedPreparedConjunction prepared = {0};
    CettaMorkQueryCursorHandle *cursor = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t row_count = 0;
    bool success = false;
    ImportedOpeningVarMap opening_vars = {0};
    Atom *patterns[1];

    if (out_attempted)
        *out_attempted = false;
    if (!s || !a || !query || !visitor || space_is_ordered(s) ||
        s->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        s->match_backend.pathmap.bridge.preserve_logical_order ||
        !pathmap_indexed_single_factor_enabled()) {
        return false;
    }
    st = &s->match_backend.pathmap.bridge;
    if (!pathmap_local_ensure_bridge_live(s) || !st->bridge_space)
        return false;

    patterns[0] = query;
    if (!imported_prepare_conjunction(
            s, patterns, 1u, NULL, false, &prepared)) {
        goto cleanup;
    }
    if (!cetta_mork_bridge_query_cursor_new_indexed_multi_ref_v4(
            (CettaMorkSpaceHandle *)st->bridge_space,
            (const uint8_t *)prepared.query_text,
            strlen(prepared.query_text), &cursor)) {
        goto cleanup;
    }
    {
        bool rows_available = false;
        if (!pathmap_indexed_cursor_rows_available(
                cursor, &rows_available) || !rows_available) {
            pathmap_record_indexed_query_stats(cursor);
            cetta_mork_bridge_query_cursor_free(cursor);
            cursor = NULL;
            goto cleanup;
        }
    }
    if (out_attempted)
        *out_attempted = true;

    success = true;
    while (success) {
        packet = NULL;
        packet_len = 0;
        row_count = 0;
        if (!cetta_mork_bridge_query_cursor_next(
                cursor, IMPORTED_MORK_QUERY_ROW_BATCH_ROWS,
                IMPORTED_MORK_QUERY_ROW_BATCH_BYTES,
                &packet, &packet_len, &row_count)) {
            success = false;
            cetta_mork_bridge_bytes_free(packet, packet_len);
            packet = NULL;
            packet_len = 0;
            break;
        }
        if (row_count == 0) {
            success = packet_len == 0;
            cetta_mork_bridge_bytes_free(packet, packet_len);
            packet = NULL;
            packet_len = 0;
            break;
        }
        success = imported_bridge_visit_indexed_cursor_packet(
            a, packet, packet_len, row_count, 1u,
            &prepared.query_vars, NULL, true, &opening_vars,
            visitor, ctx, false);
        cetta_mork_bridge_bytes_free(packet, packet_len);
        packet = NULL;
        packet_len = 0;
    }

cleanup:
    imported_opening_var_map_free(&opening_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    if (cursor) {
        pathmap_record_indexed_query_stats(cursor);
        cetta_mork_bridge_query_cursor_free(cursor);
    }
    imported_prepared_conjunction_free(&prepared);
    return success;
}

static SpaceMatchPullVisitResult
pathmap_local_visit_conjunction_indexed(
    Space *s,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx) {
    ImportedBridgeState *st;
    ImportedPreparedConjunction prepared = {0};
    CettaMorkQueryCursorHandle *cursor = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t row_count = 0;
    SpaceMatchPullVisitResult result = SPACE_MATCH_PULL_VISIT_DECLINED;
    ImportedOpeningVarMap opening_vars = {0};

    if (!s || !a || !patterns || npatterns == 0u || !visitor ||
        space_is_ordered(s) ||
        s->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        s->match_backend.pathmap.bridge.preserve_logical_order ||
        !pathmap_indexed_query_enabled()) {
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    }
    if (imported_logical_len(s) == 0)
        return SPACE_MATCH_PULL_VISIT_COMPLETE;

    st = &s->match_backend.pathmap.bridge;
    if (!pathmap_local_ensure_bridge_live(s) || !st->bridge_space)
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    if (!imported_prepare_conjunction(
            s, patterns, npatterns, seed, true, &prepared)) {
        goto cleanup;
    }
    if (!cetta_mork_bridge_query_cursor_new_indexed_multi_ref_v4(
            (CettaMorkSpaceHandle *)st->bridge_space,
            (const uint8_t *)prepared.query_text,
            strlen(prepared.query_text), &cursor)) {
        goto cleanup;
    }
    {
        bool rows_available = false;
        if (!pathmap_indexed_cursor_rows_available(
                cursor, &rows_available) || !rows_available) {
            pathmap_record_indexed_query_stats(cursor);
            cetta_mork_bridge_query_cursor_free(cursor);
            cursor = NULL;
            goto cleanup;
        }
    }
    {
        bool pure_residual = false;
        if (npatterns > 1u &&
            pathmap_indexed_cursor_is_pure_residual(
                cursor, &pure_residual) &&
            pure_residual) {
            pathmap_record_indexed_query_stats(cursor);
            cetta_mork_bridge_query_cursor_free(cursor);
            cursor = NULL;
            goto cleanup;
        }
    }

    /* Cursor admission is the no-replay boundary.  Each compact row carries
       the exact factor multiplicities; expand those occurrences directly to
       the consumer instead of first allocating a BindingSet of identical
       environments. */
    result = SPACE_MATCH_PULL_VISIT_COMPLETE;
    for (;;) {
        packet = NULL;
        packet_len = 0;
        row_count = 0;
        if (!cetta_mork_bridge_query_cursor_next(
                cursor, IMPORTED_MORK_QUERY_ROW_BATCH_ROWS,
                IMPORTED_MORK_QUERY_ROW_BATCH_BYTES,
                &packet, &packet_len, &row_count)) {
            result = SPACE_MATCH_PULL_VISIT_TERMINATED;
            break;
        }
        if (row_count == 0) {
            if (packet_len != 0)
                result = SPACE_MATCH_PULL_VISIT_TERMINATED;
            break;
        }
        if (!imported_bridge_visit_indexed_cursor_packet(
                a, packet, packet_len, row_count, npatterns,
                &prepared.query_vars, seed, true, &opening_vars,
                visitor, ctx, false)) {
            result = SPACE_MATCH_PULL_VISIT_TERMINATED;
            break;
        }
        cetta_mork_bridge_bytes_free(packet, packet_len);
        packet = NULL;
        packet_len = 0;
    }

cleanup:
    imported_opening_var_map_free(&opening_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    if (cursor) {
        pathmap_record_indexed_query_stats(cursor);
        cetta_mork_bridge_query_cursor_free(cursor);
    }
    imported_prepared_conjunction_free(&prepared);
    if (result == SPACE_MATCH_PULL_VISIT_DECLINED)
        space_match_backend_clear_error();
    return result;
}

static void space_query_conjunction_default(Space *s, Arena *a,
                                            Atom **patterns, CettaExprLen npatterns,
                                            const Bindings *seed, BindingSet *out) {
    bool success = true;
    binding_set_init(out);
    if (npatterns == 0) {
        if (seed) {
            if (!binding_set_push(out, seed))
                space_match_backend_set_error(
                    SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        } else {
            Bindings unit;
            bindings_init(&unit);
            if (!binding_set_push(out, &unit))
                space_match_backend_set_error(
                    SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
            bindings_free(&unit);
        }
        return;
    }

    BindingSet cur;
    binding_set_init(&cur);
    if (seed) {
        if (!binding_set_push(&cur, seed))
            success = false;
    } else {
        Bindings empty;
        bindings_init(&empty);
        if (!binding_set_push(&cur, &empty))
            success = false;
        bindings_free(&empty);
    }
    if (!success) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        binding_set_free(&cur);
        return;
    }

    for (CettaExprIndex pi = 0; pi < npatterns && success; pi++) {
        BindingSet next;
        binding_set_init(&next);
        for (CettaIndex bi = 0; bi < cur.len; bi++) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_SPACE_CONJ_DEFAULT);
            Atom *grounded = bindings_apply_if_vars(&cur.items[bi], a, patterns[pi]);
            SubstMatchSet smr;
            smset_init(&smr);
            space_subst_query(s, a, grounded, &smr);
            for (CettaIndex mi = 0; mi < smr.len; mi++) {
                Bindings merged;
                if (space_subst_match_with_seed(s, grounded, &smr.items[mi],
                                                &cur.items[bi], a, &merged)) {
                    if (!binding_set_push_move(&next, &merged)) {
                        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
                        success = false;
                        bindings_free(&merged);
                        break;
                    }
                    bindings_free(&merged);
                }
            }
            smset_free(&smr);
            if (!success)
                break;
        }
        binding_set_free(&cur);
        if (!success) {
            binding_set_free(&next);
            binding_set_init(out);
            return;
        }
        cur = next;
        if (cur.len == 0) break;
    }

    *out = cur;
}

static bool
imported_bridge_query_conjunction_fast(Space *s, Arena *a,
                                       Atom **patterns, CettaExprLen npatterns,
                                       const Bindings *seed,
                                       BindingSet *out,
                                       bool indexed_only) {
    if (!s || space_is_ordered(s))
        return false;
    if (npatterns == 0) {
        binding_set_init(out);
        if (seed)
            return binding_set_push(out, seed);
        Bindings unit;
        bindings_init(&unit);
        bool ok = binding_set_push(out, &unit);
        bindings_free(&unit);
        return ok;
    }
    ImportedPreparedConjunction prepared = {0};
    binding_set_init(out);

    bool success = true;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t row_count = 0;
    CettaMorkQueryCursorHandle *query_cursor = NULL;
    ImportedOpeningVarMap opening_vars = {0};

    if (!imported_prepare_conjunction(
            s, patterns, npatterns, seed, true, &prepared)) {
        success = false;
        goto cleanup;
    }

    if (pathmap_indexed_factor_count_enabled(npatterns) &&
        cetta_mork_bridge_query_cursor_new_indexed_multi_ref_v4(
            (CettaMorkSpaceHandle *)backend_bridge_state(s)->bridge_space,
            (const uint8_t *)prepared.query_text,
            strlen(prepared.query_text), &query_cursor)) {
        bool pure_residual = false;
        bool rows_available = false;
        if (!pathmap_indexed_cursor_rows_available(
                query_cursor, &rows_available) || !rows_available) {
            pathmap_record_indexed_query_stats(query_cursor);
            cetta_mork_bridge_query_cursor_free(query_cursor);
            query_cursor = NULL;
            success = false;
            goto cleanup;
        }
        if (npatterns > 1u &&
            pathmap_indexed_cursor_is_pure_residual(
                query_cursor, &pure_residual) &&
            pure_residual) {
            pathmap_record_indexed_query_stats(query_cursor);
            cetta_mork_bridge_query_cursor_free(query_cursor);
            query_cursor = NULL;
            success = false;
            goto cleanup;
        }
        while (success) {
            packet = NULL;
            packet_len = 0;
            row_count = 0;
            if (!cetta_mork_bridge_query_cursor_next(
                    query_cursor, IMPORTED_MORK_QUERY_ROW_BATCH_ROWS,
                    IMPORTED_MORK_QUERY_ROW_BATCH_BYTES, &packet, &packet_len,
                    &row_count)) {
                success = false;
                cetta_mork_bridge_bytes_free(packet, packet_len);
                break;
            }
            if (row_count == 0) {
                success = packet_len == 0;
                cetta_mork_bridge_bytes_free(packet, packet_len);
                break;
            }
            success = imported_bridge_visit_indexed_cursor_packet(
                a, packet, packet_len, row_count, npatterns,
                &prepared.query_vars, seed, true, &opening_vars,
                mork_query_collect_bindings, out, false);
            cetta_mork_bridge_bytes_free(packet, packet_len);
        }
        pathmap_record_indexed_query_stats(query_cursor);
        cetta_mork_bridge_query_cursor_free(query_cursor);
        query_cursor = NULL;
        goto cleanup;
    }
    space_match_backend_clear_error();
    if (indexed_only) {
        success = false;
        goto cleanup;
    }

    if (cetta_mork_bridge_space_query_contextual_rows(
            (CettaMorkSpaceHandle *)backend_bridge_state(s)->bridge_space,
            (const uint8_t *)prepared.query_text,
            strlen(prepared.query_text),
            &packet, &packet_len, &row_count)) {
        success = imported_bridge_visit_contextual_query_rows_packet(
            a, packet, packet_len, row_count, &prepared.query_vars, seed,
            true, NULL, mork_query_collect_bindings, out);
        goto cleanup;
    }
    cetta_mork_bridge_bytes_free(packet, packet_len);
    packet = NULL;
    packet_len = 0;
    row_count = 0;

    if (cetta_mork_bridge_query_cursor_new_multi_ref_v3(
            (CettaMorkSpaceHandle *)backend_bridge_state(s)->bridge_space,
            (const uint8_t *)prepared.query_text,
            strlen(prepared.query_text), &query_cursor)) {
        while (success) {
            packet = NULL;
            packet_len = 0;
            row_count = 0;
            if (!cetta_mork_bridge_query_cursor_next(
                    query_cursor, IMPORTED_MORK_QUERY_ROW_BATCH_ROWS,
                    IMPORTED_MORK_QUERY_ROW_BATCH_BYTES, &packet, &packet_len,
                    &row_count)) {
                success = false;
                cetta_mork_bridge_bytes_free(packet, packet_len);
                break;
            }
            if (row_count == 0) {
                success = packet_len == 0;
                cetta_mork_bridge_bytes_free(packet, packet_len);
                break;
            }
            success = imported_bridge_visit_multi_ref_v3_packet(
                a, packet, packet_len, row_count, npatterns,
                &prepared.query_vars, seed, true, mork_query_collect_bindings,
                out, false);
            cetta_mork_bridge_bytes_free(packet, packet_len);
        }
        cetta_mork_bridge_query_cursor_free(query_cursor);
        query_cursor = NULL;
        goto cleanup;
    }

    if (!cetta_mork_bridge_space_query_bindings_multi_ref_v3(
            (CettaMorkSpaceHandle *)backend_bridge_state(s)->bridge_space,
            (const uint8_t *)prepared.query_text,
            strlen(prepared.query_text),
            &packet, &packet_len, &row_count)) {
        success = false;
        goto cleanup;
    }

    success = imported_bridge_visit_multi_ref_v3_packet(
        a, packet, packet_len, row_count, npatterns, &prepared.query_vars,
        seed, true, mork_query_collect_bindings, out, false);

cleanup:
    imported_opening_var_map_free(&opening_vars);
    if (query_cursor)
        cetta_mork_bridge_query_cursor_free(query_cursor);
    if (!success) {
        binding_set_free(out);
        binding_set_init(out);
    }
    cetta_mork_bridge_bytes_free(packet, packet_len);
    imported_prepared_conjunction_free(&prepared);
    return success;
}

static bool pathmap_local_count_conjunction(
    Space *s, Arena *a, Atom **patterns, CettaExprLen npatterns,
    const Bindings *seed, uint64_t *out_count) {
    ImportedBridgeState *st;
    ImportedPreparedConjunction prepared = {0};
    CettaMorkQueryCursorHandle *query_cursor = NULL;
    bool success = false;

    if (out_count)
        *out_count = 0u;
    if (!s || !a || !patterns || !out_count ||
        space_is_ordered(s) ||
        s->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        npatterns == 0u ||
        !pathmap_indexed_factor_count_enabled(npatterns) ||
        npatterns > IMPORTED_CONJUNCTION_PATTERN_LIMIT) {
        return false;
    }
    st = &s->match_backend.pathmap.bridge;
    if (st->preserve_logical_order &&
        !pathmap_order_pinned_index_patterns_safe(patterns, npatterns)) {
        return false;
    }
    if (!pathmap_local_ensure_bridge_live(s) || !st->bridge_space)
        return false;

    if (!imported_prepare_conjunction(
            s, patterns, npatterns, seed, false, &prepared)) {
        goto cleanup;
    }
    if (!cetta_mork_bridge_query_cursor_new_indexed_multi_ref_v4(
            (CettaMorkSpaceHandle *)st->bridge_space,
            (const uint8_t *)prepared.query_text,
            strlen(prepared.query_text),
            &query_cursor)) {
        goto cleanup;
    }
    if (!cetta_mork_bridge_query_cursor_count_remaining(
            query_cursor, out_count)) {
        goto cleanup;
    }
    success = true;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_COUNT_PUSHDOWN);

cleanup:
    if (query_cursor) {
        pathmap_record_indexed_query_stats(query_cursor);
        cetta_mork_bridge_query_cursor_free(query_cursor);
    }
    imported_prepared_conjunction_free(&prepared);
    if (!success) {
        *out_count = 0u;
        space_match_backend_clear_error();
    }
    return success;
}

typedef struct {
    ImportedPreparedConjunction prepared;
    CettaMorkSpaceHandle *delta;
    CettaMorkQueryCursorHandle *cursor;
    CettaExprLen factor_count;
} PathmapSemiNaiveQuery;

static void pathmap_semi_naive_query_close(PathmapSemiNaiveQuery *query) {
    if (!query)
        return;
    if (query->cursor) {
        pathmap_record_indexed_query_stats(query->cursor);
        cetta_mork_bridge_query_cursor_free(query->cursor);
    }
    if (query->delta)
        cetta_mork_bridge_space_free(query->delta);
    imported_prepared_conjunction_free(&query->prepared);
    memset(query, 0, sizeof(*query));
}

static bool pathmap_semi_naive_query_open(
    Space *known, Space *old, Atom **patterns, CettaExprLen npatterns,
    const Bindings *seed, PathmapSemiNaiveQuery *query) {
    ImportedBridgeState *known_state;
    ImportedBridgeState *old_state;

    if (!query)
        return false;
    memset(query, 0, sizeof(*query));
    if (!known || !old || known == old || !patterns || npatterns == 0u ||
        npatterns > IMPORTED_CONJUNCTION_PATTERN_LIMIT ||
        known->overlay_base || old->overlay_base ||
        space_is_ordered(known) || space_is_ordered(old) ||
        known->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        old->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        !pathmap_indexed_query_enabled()) {
        return false;
    }
    if (!pathmap_local_ensure_bridge_live(known) ||
        !pathmap_local_ensure_bridge_live(old)) {
        goto decline;
    }
    known_state = &known->match_backend.pathmap.bridge;
    old_state = &old->match_backend.pathmap.bridge;
    if (!known_state->bridge_space || !old_state->bridge_space)
        goto decline;

    query->delta = cetta_mork_bridge_space_monotone_delta(
        (CettaMorkSpaceHandle *)known_state->bridge_space,
        (CettaMorkSpaceHandle *)old_state->bridge_space);
    if (!query->delta)
        goto decline;
    if (!imported_prepare_conjunction(
            known, patterns, npatterns, seed, true, &query->prepared)) {
        goto decline;
    }
    if (!cetta_mork_bridge_query_cursor_new_indexed_semi_naive_multi_ref_v4(
            (CettaMorkSpaceHandle *)known_state->bridge_space,
            (CettaMorkSpaceHandle *)old_state->bridge_space,
            query->delta,
            (const uint8_t *)query->prepared.query_text,
            strlen(query->prepared.query_text), &query->cursor)) {
        goto decline;
    }
    query->factor_count = npatterns;
    return true;

decline:
    pathmap_semi_naive_query_close(query);
    space_match_backend_clear_error();
    return false;
}

bool space_match_backend_visit_conjunction_semi_naive(
    Space *known, Space *old, Arena *a, Atom **patterns,
    CettaExprLen npatterns, const Bindings *seed,
    CettaMorkBindingsVisitor visitor, void *ctx) {
    PathmapSemiNaiveQuery query;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint64_t row_count = 0;
    bool success = true;

    if (!a || !visitor ||
        !pathmap_semi_naive_query_open(
            known, old, patterns, npatterns, seed, &query)) {
        return false;
    }

    while (success) {
        packet = NULL;
        packet_len = 0;
        row_count = 0;
        if (!cetta_mork_bridge_query_cursor_next(
                query.cursor, IMPORTED_MORK_QUERY_ROW_BATCH_ROWS,
                IMPORTED_MORK_QUERY_ROW_BATCH_BYTES, &packet, &packet_len,
                &row_count)) {
            success = false;
        } else if (row_count == 0) {
            success = packet_len == 0;
            cetta_mork_bridge_bytes_free(packet, packet_len);
            packet = NULL;
            packet_len = 0;
            break;
        } else {
            success = imported_bridge_visit_multi_ref_v3_packet(
                a, packet, packet_len, row_count, query.factor_count,
                &query.prepared.query_vars, seed, true, visitor, ctx, false);
        }
        cetta_mork_bridge_bytes_free(packet, packet_len);
        packet = NULL;
        packet_len = 0;
    }
    cetta_mork_bridge_bytes_free(packet, packet_len);
    pathmap_semi_naive_query_close(&query);
    return success;
}

bool space_match_backend_count_conjunction_semi_naive(
    Space *known, Space *old, Arena *a, Atom **patterns,
    CettaExprLen npatterns, const Bindings *seed, uint64_t *out_count) {
    PathmapSemiNaiveQuery query;
    bool success;

    if (out_count)
        *out_count = 0u;
    if (!a || !out_count ||
        !pathmap_semi_naive_query_open(
            known, old, patterns, npatterns, seed, &query)) {
        return false;
    }
    success = cetta_mork_bridge_query_cursor_count_remaining(
        query.cursor, out_count);
    if (success) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_COUNT_PUSHDOWN);
    } else {
        *out_count = 0u;
    }
    pathmap_semi_naive_query_close(&query);
    return success;
}

static void imported_query_conjunction_flat(Space *s, Arena *a, Atom **patterns,
                                            CettaExprLen npatterns, const Bindings *seed,
                                            BindingSet *out) {
    bool success = true;
    if (npatterns == 0) {
        binding_set_init(out);
        if (seed) {
            if (!binding_set_push(out, seed))
                space_match_backend_set_error(
                    SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        } else {
            Bindings unit;
            bindings_init(&unit);
            if (!binding_set_push(out, &unit))
                space_match_backend_set_error(
                    SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
            bindings_free(&unit);
        }
        return;
    }

    imported_ensure_built_flat(s);
    ImportedConjStep order[IMPORTED_CONJUNCTION_PATTERN_LIMIT];
    if (npatterns > IMPORTED_CONJUNCTION_PATTERN_LIMIT) {
        space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
        return;
    }
    for (CettaExprIndex i = 0; i < npatterns; i++) {
        order[i].pattern = patterns[i];
        order[i].idx = i;
        order[i].estimate = imported_pattern_estimate(s, patterns[i]);
    }
    qsort(order, npatterns, sizeof(ImportedConjStep), imported_cmp_conj_step);

    BindingSet cur;
    binding_set_init(&cur);
    if (seed) {
        if (!binding_set_push(&cur, seed))
            success = false;
    } else {
        Bindings empty;
        bindings_init(&empty);
        if (!binding_set_push(&cur, &empty))
            success = false;
        bindings_free(&empty);
    }
    if (!success) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        binding_set_free(&cur);
        binding_set_init(out);
        return;
    }

    for (CettaExprIndex pi = 0; pi < npatterns && success; pi++) {
        BindingSet next;
        binding_set_init(&next);
        for (CettaIndex bi = 0; bi < cur.len; bi++) {
            Atom *grounded = bindings_apply_if_vars(&cur.items[bi], a, order[pi].pattern);
            SubstMatchSet smr;
            smset_init(&smr);
            space_subst_query(s, a, grounded, &smr);
            for (CettaIndex mi = 0; mi < smr.len; mi++) {
                Bindings merged;
                if (space_subst_match_with_seed(s, grounded, &smr.items[mi],
                                                &cur.items[bi], a, &merged)) {
                    if (!binding_set_push_move(&next, &merged)) {
                        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
                        success = false;
                        bindings_free(&merged);
                        break;
                    }
                    bindings_free(&merged);
                }
            }
            smset_free(&smr);
            if (!success)
                break;
        }
        binding_set_free(&cur);
        if (!success) {
            binding_set_free(&next);
            binding_set_init(out);
            return;
        }
        cur = next;
        if (cur.len == 0) break;
    }
    *out = cur;
}

static void imported_query_conjunction(Space *s, Arena *a, Atom **patterns,
                                       CettaExprLen npatterns, const Bindings *seed,
                                       BindingSet *out) {
    ImportedBridgeState *st = backend_bridge_state(s);
    MorkImportedState *mst = mork_imported_state(s);
    imported_ensure_built(s);
    if (!backend_uses_bridge_adapter(s)) {
        imported_query_conjunction_flat(s, a, patterns, npatterns, seed, out);
        return;
    }
    if (st && st->bridge_active) {
        if (imported_bridge_query_conjunction_fast(
                s, a, patterns, npatterns, seed, out, false)) {
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
    .store_atom_direct = NULL,
    .store_atom_id_direct = NULL,
    .store_atom_ids_batch_direct = NULL,
    .remove_atom_id_direct = NULL,
    .remove_atom_direct = NULL,
    .remove_atom_ids_batch_direct = NULL,
    .truncate_direct = NULL,
    .logical_len = shadow_storage_logical_len,
    .get_atom_id_at = shadow_storage_get_atom_id_at,
    .get_at = shadow_storage_get_at,
    .materialize_native_storage = native_materialize_native_storage,
    .supports_direct_bindings = true,
    .free = native_free,
    .note_add = native_note_add,
    .note_remove = native_note_remove,
    .transport_stable_occurrence_coordinates =
        native_transport_stable_occurrence_coordinates,
    .candidates = native_candidates,
    .count_flat_linear = native_count_flat_linear,
    .query = native_query,
    .query_conjunction = NULL,
};

static const SpaceMatchBackendOps NATIVE_CANDIDATE_EXACT_BACKEND_OPS = {
    .name = "native-candidate-exact",
    .store_atom_direct = NULL,
    .store_atom_id_direct = NULL,
    .store_atom_ids_batch_direct = NULL,
    .remove_atom_id_direct = NULL,
    .remove_atom_direct = NULL,
    .remove_atom_ids_batch_direct = NULL,
    .truncate_direct = NULL,
    .logical_len = shadow_storage_logical_len,
    .get_atom_id_at = shadow_storage_get_atom_id_at,
    .get_at = shadow_storage_get_at,
    .materialize_native_storage = native_materialize_native_storage,
    .supports_direct_bindings = true,
    .free = native_free,
    .note_add = native_note_add,
    .note_remove = native_note_remove,
    .transport_stable_occurrence_coordinates =
        native_transport_stable_occurrence_coordinates,
    .candidates = native_candidates,
    .count_flat_linear = native_count_flat_linear,
    .query = native_candidate_exact_query,
    .query_conjunction = NULL,
};

/* Generic SPACE_ENGINE_PATHMAP stays local to CeTTa-owned atoms. */
static const SpaceMatchBackendOps PATHMAP_BACKEND_OPS = {
    .name = "pathmap",
    .store_atom_direct = pathmap_local_store_atom_direct,
    .store_atom_id_direct = pathmap_local_store_atom_id_direct,
    .store_atom_ids_batch_direct = pathmap_local_store_atom_ids_batch_direct,
    .remove_atom_id_direct = pathmap_local_remove_atom_id_direct,
    .remove_atom_direct = pathmap_local_remove_atom_direct,
    .remove_atom_ids_batch_direct = pathmap_local_remove_atom_ids_batch_direct,
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
    .count_conjunction = pathmap_local_count_conjunction,
    .query = pathmap_local_query,
    .query_conjunction = pathmap_local_query_conjunction,
};

/* Explicit MORK spaces keep the bridge-backed imported lane. */
static const SpaceMatchBackendOps MORK_BRIDGE_BACKEND_OPS = {
    .name = "mork",
    .store_atom_direct = NULL,
    .store_atom_id_direct = NULL,
    .store_atom_ids_batch_direct = NULL,
    .remove_atom_id_direct = NULL,
    .remove_atom_direct = NULL,
    .remove_atom_ids_batch_direct = NULL,
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
    s->match_backend.native.match_trie_stale_occurrences = 0u;
    s->match_backend.native.stree = NULL;
    s->match_backend.native.stree_dirty = false;
    s->match_backend.native.stree_stale_occurrences = 0u;
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
                                  CettaIndex atom_idx) {
    if (s->match_backend.ops && s->match_backend.ops->note_add)
        s->match_backend.ops->note_add(s, atom_id, atom, atom_idx);
}

void space_match_backend_note_native_shadow_add(Space *s, AtomId atom_id,
                                                CettaIndex atom_idx) {
    if (!s || s->match_backend.kind != SPACE_ENGINE_PATHMAP ||
        !s->match_backend.pathmap.bridge.native_shadow_synced ||
        atom_idx >= s->native.len) {
        return;
    }
    native_note_add(s, atom_id, NULL, atom_idx);
}

void space_match_backend_note_remove(Space *s) {
    if (s->match_backend.ops && s->match_backend.ops->note_remove)
        s->match_backend.ops->note_remove(s);
}

SpaceBackendBatchResult
space_match_backend_transport_stable_occurrence_coordinates(
        Space *s, const SpaceStableOccurrenceTransport *transport) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_ATTEMPT);
    if (transport) {
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_SOURCE_ROW,
            transport->source_len);
    }
    if (!s || !s->match_backend.ops ||
        !s->match_backend.ops->transport_stable_occurrence_coordinates) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_DECLINE);
        return SPACE_BACKEND_BATCH_UNSUPPORTED;
    }
    SpaceBackendBatchResult result =
        s->match_backend.ops->transport_stable_occurrence_coordinates(
            s, transport);
    if (result == SPACE_BACKEND_BATCH_APPLIED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_COMMIT);
    } else if (result == SPACE_BACKEND_BATCH_UNSUPPORTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_DECLINE);
    } else {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_ERROR);
    }
    return result;
}

CettaIndex space_match_backend_candidates64(Space *s, Atom *pattern,
                                            CettaIndex **out) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (!s->match_backend.ops || !s->match_backend.ops->candidates) {
        if (out)
            *out = NULL;
        return 0;
    }
    space_linearize(s);
    return s->match_backend.ops->candidates(s, pattern, out);
}

uint32_t space_match_backend_candidates(Space *s, Atom *pattern, uint32_t **out) {
    CettaIndex *wide = NULL;
    CettaIndex n64 = space_match_backend_candidates64(s, pattern, &wide);
    uint32_t n32 = 0;
    if (out)
        *out = NULL;
    if (!space_match_backend_u32_bound_checked(
            n64, SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE, &n32)) {
        free(wide);
        return 0;
    }
    if (n32 == 0) {
        free(wide);
        return 0;
    }
    uint32_t *narrow = cetta_malloc(sizeof(uint32_t) * n32);
    for (uint32_t i = 0; i < n32; i++) {
        if (!space_match_backend_u32_bound_checked(
                wide[i], SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE,
                &narrow[i])) {
            free(narrow);
            free(wide);
            return 0;
        }
    }
    free(wide);
    if (out)
        *out = narrow;
    return n32;
}

static AtomId space_match_backend_candidate_atom_id_at64(
    const Space *s, CettaIndex idx) {
    if (!s)
        return CETTA_ATOM_ID_NONE;
    /* A synchronized PathMap shadow is an indexed candidate coordinate
       system, not the public backend iteration order. */
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        s->match_backend.pathmap.bridge.native_shadow_synced)
        return shadow_storage_get_atom_id_at(s, idx);
    return space_match_backend_get_atom_id_at64(s, idx);
}

Atom *space_match_backend_candidate_at64(const Space *s, CettaIndex idx) {
    AtomId atom_id = space_match_backend_candidate_atom_id_at64(s, idx);
    return term_universe_get_atom(s ? s->native.universe : NULL, atom_id);
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
        return "generic pathmap-backed spaces require a bridge build (BUILD=mork or BUILD=main)";
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
    fprintf(out, " (requires a bridge build: BUILD=mork or BUILD=main)");
#endif
    fprintf(out, "\n");
    fprintf(out, "  native-candidate-exact diagnostic native exact-matcher lane\n");
}

CettaIndex space_match_candidates64(Space *s, Atom *pattern, CettaIndex **out) {
    return space_match_backend_candidates64(s, pattern, out);
}

uint32_t space_match_candidates(Space *s, Atom *pattern, uint32_t **out) {
    return space_match_backend_candidates(s, pattern, out);
}

Atom *space_match_candidate_at64(const Space *s, CettaIndex idx) {
    return space_match_backend_candidate_at64(s, idx);
}

bool space_match_count_flat_linear64(
    Space *s, Arena *scratch, Atom *pattern,
    uint64_t *count, CettaIndex *examined) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (count)
        *count = 0u;
    if (examined)
        *examined = 0u;
    if (!s || !scratch || !pattern || !count || !examined ||
        !s->match_backend.ops ||
        !s->match_backend.ops->count_flat_linear) {
        return false;
    }
    space_linearize(s);
    bool admitted = s->match_backend.ops->count_flat_linear(
        s, scratch, pattern, count, examined);
    if (admitted) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_FLAT_COUNT_ADMISSION);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_MATCH_FLAT_COUNT_ROWS_EXAMINED,
            (uint64_t)*examined);
    }
    return admitted;
}

bool space_match_count_conjunction64(
    Space *s, Arena *scratch, Atom **patterns,
    CettaExprLen npatterns, const Bindings *seed,
    uint64_t *count) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (count)
        *count = 0u;
    if (!s || !scratch || !patterns || !count ||
        !s->match_backend.ops ||
        !s->match_backend.ops->count_conjunction) {
        return false;
    }
    space_linearize(s);
    return s->match_backend.ops->count_conjunction(
        s, scratch, patterns, npatterns, seed, count);
}

/* Overlay spaces resolve reads through their base.  A base query may be
 * delegated to its indexed backend only when the entire current base is
 * visible, or when its match rows carry authoritative physical atom
 * indices that can enforce the overlay's frozen prefix.  Otherwise the
 * logical overlay view is walked so later base appends and removals remain
 * invisible.  Local scratch atoms are matched directly.  Results carry
 * finalized bindings because overlay logical indices need not address
 * backend storage. */
static void overlay_subst_query(Space *s, Arena *a, Atom *query,
                                SubstMatchSet *out) {
    smset_init(out);
    Space *base = (Space *)s->overlay_base;
    CettaCount base_visible = s->overlay_base_visible_len;
    bool base_is_fully_visible =
        base_visible == space_length64(base);
    bool base_matches_have_physical_indices =
        base->match_backend.kind == SPACE_ENGINE_NATIVE ||
        base->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT;
    if (s->overlay_removed_base_len == 0 &&
        (base_is_fully_visible || base_matches_have_physical_indices)) {
        SubstMatchSet base_matches;
        space_subst_query(base, a, query, &base_matches);
        for (CettaIndex i = 0; i < base_matches.len; i++) {
            Bindings final_b;
            if (!base_is_fully_visible &&
                base_matches.items[i].atom_idx >= base_visible)
                continue;
            bindings_init(&final_b);
            if (space_subst_match_with_seed(base, query,
                                            &base_matches.items[i], NULL, a,
                                            &final_b)) {
                subst_matchset_push(out, base_matches.items[i].atom_idx,
                                    base_matches.items[i].epoch, &final_b,
                                    true);
            }
            bindings_free(&final_b);
        }
        smset_free(&base_matches);
        for (CettaIndex i = 0; i < s->native.len; i++) {
            uint32_t epoch = fresh_var_suffix();
            Bindings b;
            bindings_init(&b);
            if (match_space_atom_epoch(s, i, query, &b, a, epoch) &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, base_visible + i, epoch, &b, true);
            }
            bindings_free(&b);
        }
        return;
    }
    CettaCount logical_len = space_length64(s);
    for (CettaIndex i = 0; i < logical_len; i++) {
        Atom *atom = space_get_at64(s, i);
        uint32_t epoch;
        Bindings b;
        if (!atom)
            continue;
        epoch = fresh_var_suffix();
        bindings_init(&b);
        if (match_atoms_epoch(query, atom, &b, a, epoch) &&
            !bindings_has_loop(&b)) {
            subst_matchset_push(out, i, epoch, &b, true);
        }
        bindings_free(&b);
    }
}

void space_subst_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (s && s->overlay_base) {
        overlay_subst_query(s, a, query, out);
        goto finalize_shared_snapshot;
    }
    bool use_exact_shortcut =
        !s || s->match_backend.kind != SPACE_ENGINE_PATHMAP;
    if (use_exact_shortcut) {
        CettaIndex *exact = NULL;
        CettaIndex nexact = space_exact_match_indices64(s, query, &exact);
        if (nexact > 0) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SUBST_QUERY_EXACT_SHORTCUT);
            smset_init(out);
            for (CettaIndex i = 0; i < nexact; i++) {
                Bindings empty;
                bindings_init(&empty);
                subst_matchset_push(out, exact[i], 0, &empty, true);
                bindings_free(&empty);
            }
            free(exact);
            goto finalize_shared_snapshot;
        }
        free(exact);
        /* If query is exact-indexable and space has only exact atoms, no need for full scan */
        if (query && space_atom_is_exact_indexable(query) &&
            space_contains_only_exact_atoms(s)) {
            smset_init(out);
            goto finalize_shared_snapshot;
        }
    }
    space_match_backend_query(s, a, query, out);

finalize_shared_snapshot:
    /* A substitution-tree row may still name a live Space coordinate that
     * the caller normally verifies lazily.  Such a coordinate cannot cross a
     * concurrent operation boundary: a later removal could retarget it.
     * Resolve every row to an owned binding while the authority and its
     * derived index are bracketed, then expose only exact rows.  Outside a
     * concurrent scope the established deferred representation is retained. */
    if (cetta_shared_transition_scope_active() && out) {
        CettaIndex write = 0u;
        for (CettaIndex read = 0u; read < out->len; read++) {
            SubstMatch *item = &out->items[read];
            bool keep = item->exact;
            if (!keep) {
                Bindings exact;
                if (space_subst_match_with_seed(
                        s, query, item, NULL, a, &exact)) {
                    bindings_free(&item->bindings);
                    bindings_move(&item->bindings, &exact);
                    item->exact = true;
                    keep = true;
                }
            }
            if (!keep) {
                bindings_free(&item->bindings);
                bindings_init(&item->bindings);
                continue;
            }
            if (write != read)
                subst_match_move(&out->items[write], item);
            write++;
        }
        out->len = write;
    }
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

bool space_match_backend_supports_seeded_candidates(Space *s) {
    /* Candidate coordinates are a private deferred representation.  A
       concurrent caller may use them only while it already owns the physical
       transition bracket; otherwise choose the exact binding snapshot path. */
    return s &&
           (!cetta_shared_transition_scope_active() ||
            cetta_shared_transition_guard_held_by_current_thread()) &&
           !s->overlay_base &&
           (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
            s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) &&
           !space_match_backend_is_attached_compiled(s);
}

bool space_match_backend_match_atom_seeded(Space *s, CettaIndex atom_idx,
                                           Atom *pattern, Bindings *env,
                                           Arena *a) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (!s || atom_idx >= s->native.len)
        return false;
    uint32_t suffix = fresh_var_suffix();
    return match_space_atom_epoch(s, atom_idx, pattern, env, a, suffix) &&
           !bindings_has_loop(env);
}

void space_query_conjunction(Space *s, Arena *a, Atom **patterns, CettaExprLen npatterns,
                             const Bindings *seed, BindingSet *out) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    space_linearize(s);
    if (s->match_backend.ops && s->match_backend.ops->query_conjunction) {
        s->match_backend.ops->query_conjunction(s, a, patterns, npatterns, seed, out);
        return;
    }
    space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
}
