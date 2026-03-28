#include "space.h"
#include "mork_space_bridge_runtime.h"
#include "parser.h"
#include "stats.h"
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
#define IMPORTED_MORK_TAG_VARREF_MASK 0xC0u
#define IMPORTED_MORK_TAG_VARREF_PREFIX 0x80u
#define IMPORTED_MORK_TAG_SYMBOL_PREFIX 0xC0u
#define IMPORTED_MORK_TAG_NEWVAR 0xC0u

static int cmp_uint32(const void *a, const void *b) {
    uint32_t va = *(const uint32_t *)a, vb = *(const uint32_t *)b;
    return (va > vb) - (va < vb);
}

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

static SymbolId atom_head_sym(Atom *a) {
    if (a->kind == ATOM_SYMBOL) return a->sym_id;
    if (a->kind == ATOM_EXPR && a->expr.len > 0 &&
        a->expr.elems[0]->kind == ATOM_SYMBOL)
        return a->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
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
        disc_insert(st->match_trie, s->atoms[i], i);
    st->match_trie_dirty = false;
}

static void native_ensure_match_trie(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->match_trie) {
        st->match_trie = disc_node_new();
        for (uint32_t i = 0; i < s->len; i++)
            disc_insert(st->match_trie, s->atoms[i], i);
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
            stree_insert(st->stree, s->atoms[i], i);
        st->stree_dirty = false;
    } else if (st->stree_dirty) {
        stree_free(st->stree);
        stree_init(st->stree);
        for (uint32_t i = 0; i < s->len; i++)
            stree_insert(st->stree, s->atoms[i], i);
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

static void native_note_add(Space *s, Atom *atom, uint32_t atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (st->match_trie)
        disc_insert(st->match_trie, atom, atom_idx);
    if (st->stree)
        stree_insert(st->stree, atom, atom_idx);
}

static void native_note_remove(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    (void)s;
    st->match_trie_dirty = true;
    st->stree_dirty = true;
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
        for (uint32_t i = 0; i < s->len; i++) {
            uint32_t epoch = fresh_var_suffix();
            Bindings b;
            bindings_init(&b);
            if (match_atoms_epoch(query, s->atoms[i], &b, a, epoch) &&
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
                           a, query, s->atoms, out);
    } else {
        for (uint32_t i = 0; i < STREE_BUCKETS; i++)
            stree_query_bucket(&st->stree->buckets[i], a, query, s->atoms, out);
    }
    stree_query_bucket(&st->stree->wildcard, a, query, s->atoms, out);

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
    st->built = false;
    st->dirty = false;
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
    ImportedFlatToken tok;
    tok.origin = atom;
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
    case IMPORTED_FLAT_GROUNDED_OTHER:
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

static bool imported_bridge_parse_marker(const char *name,
                                         uint8_t *side_out,
                                         uint8_t *index_out) {
    unsigned side = 0, index = 0;
    char tail = '\0';
    if (sscanf(name, "__mork_b%u_%u%c", &side, &index, &tail) != 2)
        return false;
    if (side > 255 || index > 255)
        return false;
    *side_out = (uint8_t)side;
    *index_out = (uint8_t)index;
    return true;
}

static Atom *imported_bridge_rewrite_value_atom(
    Arena *a,
    Atom *atom,
    ImportedBridgeVarMap *query_vars,
    ImportedBridgeVarMap *candidate_vars,
    uint32_t epoch,
    bool *ok
) {
    if (!atom || !*ok)
        return atom;
    switch (atom->kind) {
    case ATOM_VAR: {
        uint8_t side = 0, index = 0;
        const char *marker = atom_name_cstr(atom);
        if (!marker || !imported_bridge_parse_marker(marker, &side, &index))
            return atom;
        ImportedBridgeVarSlot *slot =
            side == 0 ? imported_bridge_varmap_lookup(query_vars, index)
                      : imported_bridge_varmap_lookup(candidate_vars, index);
        if (!slot) {
            *ok = false;
            return atom;
        }
        VarId id = side == 0 ? slot->var_id : var_epoch_id(slot->var_id, epoch);
        return atom_var_with_spelling(a, slot->spelling, id);
    }
    case ATOM_EXPR: {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            elems[i] = imported_bridge_rewrite_value_atom(
                a, atom->expr.elems[i], query_vars, candidate_vars, epoch, ok);
            if (!*ok)
                return atom;
        }
        return atom_expr(a, elems, atom->expr.len);
    }
    default:
        return atom;
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
                                               uint32_t len) {
    char *tok = arena_alloc(a, (size_t)len + 1);
    memcpy(tok, bytes, len);
    tok[len] = '\0';

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

    return atom_symbol_id(a, symbol_intern_bytes(g_symbols, bytes, len));
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
        Atom *atom = imported_bridge_parse_token_bytes(a, expr + *off + 1u, sym_len);
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

static Atom *imported_bridge_parse_value_text(Arena *a,
                                              const uint8_t *text,
                                              uint32_t text_len) {
    char *buf = arena_alloc(a, (size_t)text_len + 1);
    memcpy(buf, text, text_len);
    buf[text_len] = '\0';
    size_t pos = 0;
    Atom *value = parse_sexpr(a, buf, &pos);
    if (!value)
        return NULL;
    while (buf[pos] && ((unsigned char)buf[pos] == ' ' ||
                        (unsigned char)buf[pos] == '\t' ||
                        (unsigned char)buf[pos] == '\n' ||
                        (unsigned char)buf[pos] == '\r'))
        pos++;
    return buf[pos] == '\0' ? value : NULL;
}

static char *imported_bridge_build_conjunction_text(Arena *a, Atom **patterns,
                                                    uint32_t npatterns) {
    if (npatterns == 0)
        return NULL;
    char **parts = arena_alloc(a, sizeof(char *) * npatterns);
    size_t total = 3;
    for (uint32_t i = 0; i < npatterns; i++) {
        parts[i] = atom_to_string(a, patterns[i]);
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
                                          uint32_t epoch, Arena *a,
                                          Bindings *out) {
    bindings_init(out);
    for (uint32_t i = 0; i < refs->nquery; i++) {
        Atom *val = rename_vars(a, ctokens[refs->query[i].idx].origin, epoch);
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
        Atom *val = rename_vars(a, ctokens[refs->indexed_value[i].idx].origin, epoch);
        if (!bindings_add_id(out,
                             var_epoch_id(refs->indexed_value[i].var_id, epoch),
                             refs->indexed_value[i].spelling, val)) {
            bindings_free(out);
            return false;
        }
    }
    return true;
}

static void imported_bucket_add_entry(ImportedFlatBucket *bucket, Atom *atom,
                                      uint32_t atom_idx, uint32_t epoch) {
    ImportedFlatBuilder b = {0};
    imported_flatten_atom(&b, atom);
    if (bucket->len >= bucket->cap) {
        bucket->cap = bucket->cap ? bucket->cap * 2 : 8;
        bucket->entries = cetta_realloc(bucket->entries,
                                        sizeof(ImportedFlatEntry) * bucket->cap);
    }
    bucket->entries[bucket->len].atom_idx = atom_idx;
    bucket->entries[bucket->len].epoch = epoch;
    bucket->entries[bucket->len].tokens = b.items;
    bucket->entries[bucket->len].len = b.len;
    bucket->len++;
}

static ImportedFlatBucket *imported_bucket_for_atom(PathmapImportedState *st, Atom *atom) {
    SymbolId head = atom_head_sym(atom);
    return head != SYMBOL_ID_NONE ? &st->buckets[stree_head_hash(head)] : &st->wildcard;
}

static void imported_rebuild_flat(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    imported_flat_state_clear(st);
    for (uint32_t i = 0; i < s->len; i++) {
        ImportedFlatBucket *bucket = imported_bucket_for_atom(st, s->atoms[i]);
        imported_bucket_add_entry(bucket, s->atoms[i], i, stree_next_epoch());
    }
    st->bridge_active = false;
    st->built = true;
    st->dirty = false;
}

static bool imported_ensure_bridge_space(PathmapImportedState *st) {
    if (st->bridge_space)
        return true;
    st->bridge_space = cetta_mork_bridge_space_new();
    return st->bridge_space != NULL;
}

static bool imported_rebuild_bridge(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    if (!imported_ensure_bridge_space(st))
        return false;
    if (!cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space))
        return false;

    for (uint32_t i = 0; i < s->len; i++) {
        Arena scratch;
        arena_init(&scratch);
        char *sexpr = atom_to_string(&scratch, s->atoms[i]);
        bool ok = cetta_mork_bridge_space_add_indexed_sexpr(
            (CettaMorkSpaceHandle *)st->bridge_space, i,
            (const uint8_t *)sexpr, strlen(sexpr));
        arena_free(&scratch);
        if (!ok) {
            cetta_mork_bridge_space_clear((CettaMorkSpaceHandle *)st->bridge_space);
            st->bridge_active = false;
            return false;
        }
    }

    imported_flat_state_clear(st);
    st->bridge_active = true;
    st->built = true;
    st->dirty = false;
    return true;
}

static void imported_rebuild(Space *s) {
    if (!imported_rebuild_bridge(s))
        imported_rebuild_flat(s);
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
    case IMPORTED_FLAT_GROUNDED_OTHER:
        if (!atom_eq(qt->origin, ct->origin)) return IMPORTED_COREF_FAIL;
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
                                          Bindings *b, Arena *a, uint32_t epoch,
                                          uint32_t *qnext, uint32_t *cnext, int depth) {
    if (depth <= 0) return false;
    const ImportedFlatToken *qt = &q[qi];
    const ImportedFlatToken *ct = &c[ci];

    if (qt->kind == IMPORTED_FLAT_VAR) {
        Atom *existing = bindings_lookup_id(b, qt->var_id);
        if (existing) {
            if (!match_atoms_epoch(existing, ct->origin, b, a, epoch))
                return false;
        } else {
            if (!bindings_add_id(b, qt->var_id, qt->sym_id,
                                 rename_vars(a, ct->origin, epoch)))
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
    case IMPORTED_FLAT_GROUNDED_OTHER:
        if (!atom_eq(qt->origin, ct->origin)) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_EXPR: {
        if (qt->arity != ct->arity) return false;
        uint32_t qcur = qi + 1;
        uint32_t ccur = ci + 1;
        for (uint32_t i = 0; i < qt->arity; i++) {
            if (!imported_match_subtree_legacy(q, qcur, c, ccur, b, a, epoch,
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
                                    Arena *a, SubstMatchSet *out) {
    for (uint32_t i = 0; i < bucket->len; i++) {
        const ImportedFlatEntry *entry = &bucket->entries[i];
        if (entry->len == 0 || qlen == 0) continue;
        uint32_t qnext = 0, cnext = 0;
        ImportedCorefState refs = {0};
        ImportedCorefVerdict verdict =
            imported_match_subtree_coref(qtokens, 0, entry->tokens, 0, &refs,
                                         &qnext, &cnext, 64);
        if (verdict == IMPORTED_COREF_EXACT &&
            qnext == qlen && cnext == entry->len) {
            Bindings b;
            if (imported_materialize_bindings(&refs, qtokens, entry->tokens,
                                              entry->epoch, a, &b) &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, entry->atom_idx, entry->epoch, &b, true);
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
            if (imported_match_subtree_legacy(qtokens, 0, entry->tokens, 0, &b, a,
                                              entry->epoch, &qnext, &cnext, 64) &&
                qnext == qlen && cnext == entry->len &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, entry->atom_idx, entry->epoch, &b, true);
            }
            bindings_free(&b);
        }
    }
}

static __attribute__((unused)) bool
imported_bridge_query_indices(Space *s, Atom *pattern,
                              uint32_t **out, uint32_t *nout) {
    PathmapImportedState *st = &s->match_backend.imported;
    Arena scratch;
    arena_init(&scratch);
    char *pattern_text = atom_to_string(&scratch, pattern);
    bool ok = cetta_mork_bridge_space_query_indices(
        (CettaMorkSpaceHandle *)st->bridge_space,
        (const uint8_t *)pattern_text, strlen(pattern_text),
        out, nout);
    arena_free(&scratch);
    if (!ok)
        return false;
    *nout = sort_unique_uint32(*out, *nout);
    return true;
}

static __attribute__((unused)) bool
imported_bridge_query_bindings(Space *s, Arena *a, Atom *query,
                               SubstMatchSet *out) {
    PathmapImportedState *st = &s->match_backend.imported;
    Arena scratch;
    arena_init(&scratch);
    char *pattern_text = atom_to_string(&scratch, query);
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t row_count = 0;
    bool ok = cetta_mork_bridge_space_query_bindings(
        (CettaMorkSpaceHandle *)st->bridge_space,
        (const uint8_t *)pattern_text, strlen(pattern_text),
        &packet, &packet_len, &row_count);
    arena_free(&scratch);
    if (!ok)
        return false;

    smset_init(out);

    ImportedBridgeVarMap query_vars;
    imported_bridge_varmap_init(&query_vars);
    if (!imported_bridge_collect_vars(query, &query_vars)) {
        imported_bridge_varmap_free(&query_vars);
        cetta_mork_bridge_bytes_free(packet, packet_len);
        return false;
    }

    bool success = true;
    size_t off = 0;
    uint32_t parsed_rows = 0;
    if (!imported_bridge_read_u32(packet, packet_len, &off, &parsed_rows) ||
        parsed_rows != row_count) {
        success = false;
        goto cleanup;
    }

    for (uint32_t row = 0; row < parsed_rows && success; row++) {
        uint32_t ref_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &ref_count)) {
            success = false;
            break;
        }
        if (off + (size_t)ref_count * 4u > packet_len) {
            success = false;
            break;
        }
        const uint8_t *ref_bytes = packet + off;
        off += (size_t)ref_count * 4u;

        uint32_t binding_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            success = false;
            break;
        }

        ImportedBridgeBindingEntry *entries = NULL;
        if (binding_count > 0) {
            entries = cetta_malloc(sizeof(ImportedBridgeBindingEntry) * binding_count);
            for (uint32_t bi = 0; bi < binding_count; bi++) {
                if (off + 2 > packet_len) {
                    success = false;
                    break;
                }
                entries[bi].side = packet[off++];
                entries[bi].index = packet[off++];
                if (!imported_bridge_read_u32(packet, packet_len, &off, &entries[bi].text_len)) {
                    success = false;
                    break;
                }
                if (off + entries[bi].text_len > packet_len) {
                    success = false;
                    break;
                }
                entries[bi].text = packet + off;
                off += entries[bi].text_len;
            }
        }
        if (!success) {
            free(entries);
            break;
        }

        for (uint32_t ri = 0; ri < ref_count && success; ri++) {
            uint32_t atom_idx = ((uint32_t)ref_bytes[(size_t)ri * 4u] << 24) |
                                ((uint32_t)ref_bytes[(size_t)ri * 4u + 1] << 16) |
                                ((uint32_t)ref_bytes[(size_t)ri * 4u + 2] << 8) |
                                (uint32_t)ref_bytes[(size_t)ri * 4u + 3];
            if (atom_idx >= s->len)
                continue;

            ImportedBridgeVarMap candidate_vars;
            imported_bridge_varmap_init(&candidate_vars);
            if (!imported_bridge_collect_vars(s->atoms[atom_idx], &candidate_vars)) {
                imported_bridge_varmap_free(&candidate_vars);
                success = false;
                break;
            }

            uint32_t epoch = fresh_var_suffix();
            Bindings row_bindings;
            bindings_init(&row_bindings);

            for (uint32_t bi = 0; bi < binding_count && success; bi++) {
                /* Imported bridge rows that bind candidate-side vars are not yet
                   semantically stable for chained nested-match workloads.
                   Reject and fall back to candidate-index + local rematch. */
                if (entries[bi].side != 0) {
                    success = false;
                    break;
                }
                ImportedBridgeVarSlot *key_slot =
                    imported_bridge_varmap_lookup(&query_vars, entries[bi].index);
                if (!key_slot) {
                    success = false;
                    break;
                }

                Atom *parsed = imported_bridge_parse_value_text(
                    a, entries[bi].text, entries[bi].text_len);
                if (!parsed) {
                    success = false;
                    break;
                }

                bool rewrite_ok = true;
                Atom *value = imported_bridge_rewrite_value_atom(
                    a, parsed, &query_vars, &candidate_vars, epoch, &rewrite_ok);
                if (!rewrite_ok) {
                    success = false;
                    break;
                }

                VarId binding_id = key_slot->var_id;
                if (!bindings_add_id(&row_bindings, binding_id, key_slot->spelling, value)) {
                    success = false;
                    break;
                }
            }

            if (success && !bindings_has_loop(&row_bindings))
                subst_matchset_push(out, atom_idx, epoch, &row_bindings, true);
            bindings_free(&row_bindings);
            imported_bridge_varmap_free(&candidate_vars);
        }
        free(entries);
    }

cleanup:
    if (!success) {
        smset_free(out);
        smset_init(out);
    } else {
        subst_matchset_normalize(out);
    }
    imported_bridge_varmap_free(&query_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    return success;
}

static bool imported_bridge_query_bindings_query_only_v2(Space *s, Arena *a,
                                                         Atom *query,
                                                         SubstMatchSet *out) {
    PathmapImportedState *st = &s->match_backend.imported;
    Arena scratch;
    arena_init(&scratch);
    char *pattern_text = atom_to_string(&scratch, query);
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t row_count = 0;
    bool ok = cetta_mork_bridge_space_query_bindings_query_only_v2(
        (CettaMorkSpaceHandle *)st->bridge_space,
        (const uint8_t *)pattern_text, strlen(pattern_text),
        &packet, &packet_len, &row_count);
    arena_free(&scratch);
    if (!ok)
        return false;

    smset_init(out);

    ImportedBridgeVarMap query_vars;
    imported_bridge_varmap_init(&query_vars);
    if (!imported_bridge_collect_vars(query, &query_vars)) {
        imported_bridge_varmap_free(&query_vars);
        cetta_mork_bridge_bytes_free(packet, packet_len);
        return false;
    }

    bool success = true;
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t parsed_rows = 0;

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
        if (!imported_bridge_read_u32(packet, packet_len, &off, &ref_count)) {
            success = false;
            break;
        }
        if (off + (size_t)ref_count * 4u > packet_len) {
            success = false;
            break;
        }
        const uint8_t *ref_bytes = packet + off;
        off += (size_t)ref_count * 4u;

        uint32_t binding_count = 0;
        if (!imported_bridge_read_u32(packet, packet_len, &off, &binding_count)) {
            success = false;
            break;
        }

        ImportedBridgeBindingEntryV2 *entries = NULL;
        if (binding_count > 0) {
            entries = cetta_malloc(sizeof(ImportedBridgeBindingEntryV2) * binding_count);
            for (uint32_t bi = 0; bi < binding_count; bi++) {
                if (!imported_bridge_read_u16(packet, packet_len, &off, &entries[bi].query_slot) ||
                    off + 2 > packet_len) {
                    success = false;
                    break;
                }
                entries[bi].value_env = packet[off++];
                entries[bi].value_flags = packet[off++];
                if (!imported_bridge_read_u32(packet, packet_len, &off, &entries[bi].expr_len) ||
                    off + entries[bi].expr_len > packet_len) {
                    success = false;
                    break;
                }
                entries[bi].expr = packet + off;
                off += entries[bi].expr_len;
            }
        }
        if (!success) {
            free(entries);
            break;
        }

        for (uint32_t ri = 0; ri < ref_count && success; ri++) {
            uint32_t atom_idx = ((uint32_t)ref_bytes[(size_t)ri * 4u] << 24) |
                                ((uint32_t)ref_bytes[(size_t)ri * 4u + 1] << 16) |
                                ((uint32_t)ref_bytes[(size_t)ri * 4u + 2] << 8) |
                                (uint32_t)ref_bytes[(size_t)ri * 4u + 3];
            if (atom_idx >= s->len)
                continue;

            Bindings row_bindings;
            bindings_init(&row_bindings);

            for (uint32_t bi = 0; bi < binding_count && success; bi++) {
                ImportedBridgeVarSlot *key_slot =
                    imported_bridge_varmap_lookup(&query_vars, entries[bi].query_slot);
                if (!key_slot) {
                    success = false;
                    break;
                }
                bool value_ok = true;
                Atom *value = imported_bridge_parse_value_raw_query_only_v2(
                    a,
                    entries[bi].expr,
                    entries[bi].expr_len,
                    entries[bi].value_env,
                    entries[bi].value_flags,
                    &value_ok);
                if (!value_ok ||
                    !bindings_add_id(&row_bindings, key_slot->var_id, key_slot->spelling, value)) {
                    success = false;
                    break;
                }
            }

            if (success && !bindings_has_loop(&row_bindings))
                subst_matchset_push(out, atom_idx, 0, &row_bindings, true);
            bindings_free(&row_bindings);
        }
        free(entries);
    }

cleanup:
    if (!success) {
        smset_free(out);
        smset_init(out);
    } else {
        subst_matchset_normalize(out);
    }
    imported_bridge_varmap_free(&query_vars);
    cetta_mork_bridge_bytes_free(packet, packet_len);
    return success;
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
    imported_ensure_built(s);
    return native_candidates(s, pattern, out);
}

static void imported_query_flat(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    PathmapImportedState *st = &s->match_backend.imported;
    ImportedFlatBuilder q = {0};
    smset_init(out);
    if (s->len == 0) return;
    imported_ensure_built(s);
    imported_flatten_atom(&q, query);
    SymbolId head = atom_head_sym(query);
    if (head != SYMBOL_ID_NONE) {
        imported_collect_bucket(&st->buckets[stree_head_hash(head)],
                                q.items, q.len, a, out);
    } else {
        for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++)
            imported_collect_bucket(&st->buckets[bi], q.items, q.len, a, out);
    }
    imported_collect_bucket(&st->wildcard, q.items, q.len, a, out);
    free(q.items);

    subst_matchset_normalize(out);
}

static void imported_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    PathmapImportedState *st = &s->match_backend.imported;
    imported_ensure_built(s);
    if (!st->bridge_active) {
        imported_query_flat(s, a, query, out);
        return;
    }
    if (imported_bridge_query_bindings_query_only_v2(s, a, query, out)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V2_HIT);
        return;
    }
    /* Query-only v2 is a strict unary fast path. If the bridge packet is
       unavailable or semantically unsupported, fall back to CeTTa-native
       candidate enumeration and local rematch. */
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_IMPORTED_BRIDGE_V2_FALLBACK);
    uint32_t *candidates = NULL;
    uint32_t ncand = native_candidates(s, query, &candidates);
    smset_init(out);
    for (uint32_t i = 0; i < ncand; i++) {
        Bindings empty;
        bindings_init(&empty);
        subst_matchset_push(out, candidates[i], 0, &empty, false);
        bindings_free(&empty);
    }
    free(candidates);
}

static void imported_note_add(Space *s, Atom *atom, uint32_t atom_idx) {
    PathmapImportedState *st = &s->match_backend.imported;
    if (!st->built || st->dirty) return;
    if (st->bridge_active) {
        Arena scratch;
        arena_init(&scratch);
        char *sexpr = atom_to_string(&scratch, atom);
        bool ok = cetta_mork_bridge_space_add_indexed_sexpr(
            (CettaMorkSpaceHandle *)st->bridge_space, atom_idx,
            (const uint8_t *)sexpr, strlen(sexpr));
        arena_free(&scratch);
        if (ok)
            return;
        st->bridge_active = false;
        st->dirty = true;
        return;
    }
    imported_bucket_add_entry(imported_bucket_for_atom(st, atom), atom, atom_idx,
                              stree_next_epoch());
}

static void imported_note_remove(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    (void)s;
    if (st->built) {
        st->bridge_active = false;
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
        if (match_atoms_epoch(query, s->atoms[idx], &b, a, epoch) &&
            !bindings_has_loop(&b)) {
            subst_matchset_push(out, idx, epoch, &b, false);
        }
        bindings_free(&b);
    }
    free(candidates);
}

typedef struct {
    Atom *pattern;
    uint32_t idx;
    uint32_t estimate;
} ImportedConjStep;

static uint32_t imported_pattern_estimate(Space *s, Atom *pattern) {
    PathmapImportedState *st = &s->match_backend.imported;
    imported_ensure_built(s);
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
            Atom *grounded = bindings_apply(&cur.items[bi], a, patterns[pi]);
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
    if (npatterns > 32)
        return false;

    ImportedConjStep order[32];
    for (uint32_t i = 0; i < npatterns; i++) {
        order[i].pattern = patterns[i];
        order[i].idx = i;
        order[i].estimate = imported_pattern_estimate(s, patterns[i]);
    }
    qsort(order, npatterns, sizeof(ImportedConjStep), imported_cmp_conj_step);

    Arena scratch;
    arena_init(&scratch);
    Atom **grounded = arena_alloc(&scratch, sizeof(Atom *) * npatterns);
    ImportedBridgeVarMap query_vars;
    imported_bridge_varmap_init(&query_vars);
    binding_set_init(out);

    bool success = true;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t row_count = 0;
    uint32_t factor_count = 0;

    for (uint32_t i = 0; i < npatterns; i++) {
        grounded[i] = seed ? bindings_apply((Bindings *)seed, &scratch, order[i].pattern)
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
        version != IMPORTED_MORK_MULTI_REF_V3_VERSION ||
        flags != (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                  IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES |
                  IMPORTED_MORK_MULTI_REF_V3_FLAG_MULTI_REF_GROUPS) ||
        factor_count != npatterns ||
        parsed_rows != row_count) {
        success = false;
        goto cleanup;
    }

    for (uint32_t row = 0; row < parsed_rows && success; row++) {
        uint64_t multiplicity = 1;
        for (uint32_t fi = 0; fi < factor_count; fi++) {
            uint32_t ref_count = 0;
            if (!imported_bridge_read_u32(packet, packet_len, &off, &ref_count) ||
                ref_count == 0 ||
                off + (size_t)ref_count * 4u > packet_len) {
                success = false;
                break;
            }
            if (multiplicity > UINT64_MAX / ref_count) {
                success = false;
                break;
            }
            multiplicity *= ref_count;
            for (uint32_t ri = 0; ri < ref_count; ri++) {
                uint32_t atom_idx = ((uint32_t)packet[off] << 24) |
                                    ((uint32_t)packet[off + 1] << 16) |
                                    ((uint32_t)packet[off + 2] << 8) |
                                    (uint32_t)packet[off + 3];
                if (atom_idx >= s->len) {
                    success = false;
                    break;
                }
                off += 4u;
            }
            if (!success)
                break;
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

static void imported_query_conjunction(Space *s, Arena *a, Atom **patterns,
                                       uint32_t npatterns, const Bindings *seed,
                                       BindingSet *out) {
    if (s->match_backend.imported.bridge_active) {
        if (imported_bridge_query_conjunction_fast(s, a, patterns, npatterns, seed, out))
            return;
        space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
        return;
    }
    if (npatterns == 0) {
        binding_set_init(out);
        if (seed) binding_set_push(out, seed);
        return;
    }

    ImportedConjStep order[32];
    if (npatterns > 32) {
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
            Atom *grounded = bindings_apply(&cur.items[bi], a, order[pi].pattern);
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

static const SpaceMatchBackendOps IMPORTED_BACKEND_OPS = {
    .name = "pathmap-imported",
    .supports_direct_bindings = true,
    .free = imported_free_backend,
    .note_add = imported_note_add,
    .note_remove = imported_note_remove,
    .candidates = imported_candidates,
    .query = imported_query,
    .query_conjunction = imported_query_conjunction,
};

void space_match_backend_init(Space *s) {
    s->match_backend.kind = SPACE_MATCH_BACKEND_NATIVE;
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

bool space_match_backend_try_set(Space *s, SpaceMatchBackendKind kind) {
    const SpaceMatchBackendOps *ops = NULL;
    switch (kind) {
    case SPACE_MATCH_BACKEND_NATIVE:
        ops = &NATIVE_BACKEND_OPS;
        break;
    case SPACE_MATCH_BACKEND_NATIVE_CANDIDATE_EXACT:
        ops = &NATIVE_CANDIDATE_EXACT_BACKEND_OPS;
        break;
    case SPACE_MATCH_BACKEND_PATHMAP_IMPORTED:
        ops = &IMPORTED_BACKEND_OPS;
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

void space_match_backend_note_add(Space *s, Atom *atom, uint32_t atom_idx) {
    if (s->match_backend.ops && s->match_backend.ops->note_add)
        s->match_backend.ops->note_add(s, atom, atom_idx);
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
    if (!s->match_backend.ops || !s->match_backend.ops->name)
        return "unconfigured";
    return s->match_backend.ops->name;
}

bool space_match_backend_supports_direct_bindings(const Space *s) {
    return s->match_backend.ops && s->match_backend.ops->supports_direct_bindings;
}

const char *space_match_backend_kind_name(SpaceMatchBackendKind kind) {
    switch (kind) {
    case SPACE_MATCH_BACKEND_NATIVE:
        return "native-subst-tree";
    case SPACE_MATCH_BACKEND_NATIVE_CANDIDATE_EXACT:
        return "native-candidate-exact";
    case SPACE_MATCH_BACKEND_PATHMAP_IMPORTED:
        return "pathmap-imported";
    default:
        return "unknown";
    }
}

bool space_match_backend_kind_from_name(const char *name, SpaceMatchBackendKind *out) {
    if (strcmp(name, "native-subst-tree") == 0) {
        *out = SPACE_MATCH_BACKEND_NATIVE;
        return true;
    }
    if (strcmp(name, "native-candidate-exact") == 0) {
        *out = SPACE_MATCH_BACKEND_NATIVE_CANDIDATE_EXACT;
        return true;
    }
    if (strcmp(name, "pathmap-imported") == 0) {
        *out = SPACE_MATCH_BACKEND_PATHMAP_IMPORTED;
        return true;
    }
    return false;
}

void space_match_backend_print_inventory(FILE *out) {
    fprintf(out, "space match backends:\n");
    fprintf(out, "  native-subst-tree      direct bindings via substitution tree fast-path\n");
    fprintf(out, "  native-candidate-exact exact matcher over trie-pruned candidates\n");
    fprintf(out, "  pathmap-imported       flat/path imported matcher with direct bindings + coref fast-path\n");
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
    space_match_backend_query(s, a, query, out);
}

bool space_subst_match_with_seed(Space *space, Atom *pattern, const SubstMatch *sm,
                                 const Bindings *seed, Arena *a, Bindings *out) {
    if (sm->atom_idx >= space->len) return false;
    Bindings merged;
    if (!bindings_clone_merge(&merged, seed, &sm->bindings)) {
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
    Atom *matched_atom = space_get_at(space, sm->atom_idx);
    if (match_atoms_epoch(pattern, matched_atom, &merged, a, sm->epoch) &&
        !bindings_has_loop(&merged)) {
        bindings_move(out, &merged);
        return true;
    }
    bindings_free(&merged);

    {
        uint32_t suffix = fresh_var_suffix();
        Atom *renamed = rename_vars(a, matched_atom, suffix);
        Bindings exact;
        if (!bindings_clone(&exact, seed))
            return false;
        if (match_atoms(pattern, renamed, &exact) && !bindings_has_loop(&exact)) {
            bindings_move(out, &exact);
            return true;
        }
        bindings_free(&exact);
    }
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
