#define _GNU_SOURCE
#include "atom.h"
#include "stats.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaBigInt {
    const char *text;
#if CETTA_BUILD_WITH_GMP
    mpz_t value;
    bool has_value;
#endif
};

struct CettaRational {
    const char *text;
#if CETTA_BUILD_WITH_GMP
    mpq_t value;
    bool has_value;
#endif
};

struct ArenaFinalizer {
    void (*fn)(void *);
    void *ptr;
    struct ArenaFinalizer *next;
};

static CettaBigInt *cetta_bigint_clone_owned(const CettaBigInt *src);
static void cetta_bigint_free_owned(CettaBigInt *big);
static CettaRational *cetta_rational_clone_owned(const CettaRational *src);
static void cetta_rational_free_owned(CettaRational *rat);

/* ── Arena ──────────────────────────────────────────────────────────────── */

static void cetta_oom(size_t size) {
    fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", size);
    abort();
}

static bool arena_runtime_counter_lane(CettaArenaRuntimeKind kind,
                                       CettaRuntimeCounter *alloc_counter,
                                       CettaRuntimeCounter *live_peak_counter,
                                       CettaRuntimeCounter *reserved_peak_counter) {
    switch (kind) {
    case CETTA_ARENA_RUNTIME_KIND_PERSISTENT:
        if (alloc_counter)
            *alloc_counter = CETTA_RUNTIME_COUNTER_PERSISTENT_ARENA_ALLOC_BYTES;
        if (live_peak_counter)
            *live_peak_counter = CETTA_RUNTIME_COUNTER_PERSISTENT_ARENA_LIVE_BYTES_PEAK;
        if (reserved_peak_counter)
            *reserved_peak_counter = CETTA_RUNTIME_COUNTER_PERSISTENT_ARENA_RESERVED_BYTES_PEAK;
        return true;
    case CETTA_ARENA_RUNTIME_KIND_EVAL:
        if (alloc_counter)
            *alloc_counter = CETTA_RUNTIME_COUNTER_EVAL_ARENA_ALLOC_BYTES;
        if (live_peak_counter)
            *live_peak_counter = CETTA_RUNTIME_COUNTER_EVAL_ARENA_LIVE_BYTES_PEAK;
        if (reserved_peak_counter)
            *reserved_peak_counter = CETTA_RUNTIME_COUNTER_EVAL_ARENA_RESERVED_BYTES_PEAK;
        return true;
    case CETTA_ARENA_RUNTIME_KIND_SCRATCH:
        if (alloc_counter)
            *alloc_counter = CETTA_RUNTIME_COUNTER_SCRATCH_ARENA_ALLOC_BYTES;
        if (live_peak_counter)
            *live_peak_counter = CETTA_RUNTIME_COUNTER_SCRATCH_ARENA_LIVE_BYTES_PEAK;
        if (reserved_peak_counter)
            *reserved_peak_counter = CETTA_RUNTIME_COUNTER_SCRATCH_ARENA_RESERVED_BYTES_PEAK;
        return true;
    case CETTA_ARENA_RUNTIME_KIND_SURVIVOR:
        if (alloc_counter)
            *alloc_counter =
                CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_ALLOC_BYTES;
        if (live_peak_counter)
            *live_peak_counter =
                CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_LIVE_BYTES_PEAK;
        if (reserved_peak_counter)
            *reserved_peak_counter =
                CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_RESERVED_BYTES_PEAK;
        return true;
    default:
        return false;
    }
}

static void arena_runtime_note_usage(const Arena *a) {
    CettaRuntimeCounter live_peak_counter;
    CettaRuntimeCounter reserved_peak_counter;

    if (!a ||
        !arena_runtime_counter_lane(a->runtime_kind, NULL, &live_peak_counter,
                                    &reserved_peak_counter))
        return;
    cetta_runtime_stats_update_max(live_peak_counter, (uint64_t)a->live_bytes);
    cetta_runtime_stats_update_max(reserved_peak_counter,
                                   (uint64_t)a->reserved_bytes);
}

static void arena_runtime_note_alloc(const Arena *a, size_t size) {
    CettaRuntimeCounter alloc_counter;
    CettaRuntimeCounter live_peak_counter;
    CettaRuntimeCounter reserved_peak_counter;

    if (!a ||
        !arena_runtime_counter_lane(a->runtime_kind, &alloc_counter,
                                    &live_peak_counter,
                                    &reserved_peak_counter))
        return;
    cetta_runtime_stats_add(alloc_counter, (uint64_t)size);
    cetta_runtime_stats_update_max(live_peak_counter, (uint64_t)a->live_bytes);
    cetta_runtime_stats_update_max(reserved_peak_counter,
                                   (uint64_t)a->reserved_bytes);
}

void *cetta_malloc(size_t size) {
    void *ptr = malloc(size == 0 ? 1 : size);
    if (!ptr) cetta_oom(size);
    return ptr;
}

void *cetta_realloc(void *ptr, size_t size) {
    void *out = realloc(ptr, size == 0 ? 1 : size);
    if (!out) cetta_oom(size);
    return out;
}

void arena_init(Arena *a) {
    a->head = NULL;
    a->hashcons = g_hashcons;
    a->live_bytes = 0;
    a->reserved_bytes = 0;
    a->block_count = 0;
    a->runtime_kind = CETTA_ARENA_RUNTIME_KIND_OTHER;
    a->finalizers = NULL;
}

static void arena_run_finalizers_until(Arena *a, ArenaFinalizer *stop) {
    while (a->finalizers != stop) {
        ArenaFinalizer *node = a->finalizers;
        a->finalizers = node->next;
        node->fn(node->ptr);
        free(node);
    }
}

#if CETTA_BUILD_WITH_GMP
static void arena_register_finalizer(Arena *a, void (*fn)(void *), void *ptr) {
    ArenaFinalizer *node = cetta_malloc(sizeof(ArenaFinalizer));
    node->fn = fn;
    node->ptr = ptr;
    node->next = a->finalizers;
    a->finalizers = node;
}
#endif

void arena_free(Arena *a) {
    arena_run_finalizers_until(a, NULL);
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
    a->live_bytes = 0;
    a->reserved_bytes = 0;
    a->block_count = 0;
    a->finalizers = NULL;
}

void arena_set_hashcons(Arena *a, HashConsTable *hc) {
    if (!a) return;
    a->hashcons = hc;
}

void arena_set_runtime_kind(Arena *a, CettaArenaRuntimeKind kind) {
    if (!a) return;
    a->runtime_kind = kind;
    arena_runtime_note_usage(a);
}

ArenaMark arena_mark(const Arena *a) {
    ArenaMark mark = {0};
    if (!a) {
        return mark;
    }
    mark.head = a->head;
    mark.used = a->head ? a->head->used : 0;
    mark.live_bytes = a->live_bytes;
    mark.reserved_bytes = a->reserved_bytes;
    mark.block_count = a->block_count;
    mark.finalizers = a->finalizers;
    return mark;
}

void arena_reset(Arena *a, ArenaMark mark) {
    if (!a) return;
    arena_run_finalizers_until(a, mark.finalizers);
    while (a->head && a->head != mark.head) {
        ArenaBlock *next = a->head->next;
        free(a->head);
        a->head = next;
    }
    if (a->head && a->head->used > mark.used) {
        a->head->used = mark.used;
    }
    a->live_bytes = mark.live_bytes;
    a->reserved_bytes = mark.reserved_bytes;
    a->block_count = mark.block_count;
    arena_runtime_note_usage(a);
}

/* ── Hash-Consing ──────────────────────────────────────────────────────── */

HashConsTable *g_hashcons = NULL;

static uint32_t atom_hash_compute(Atom *a) {
    if (!a) return 0;
    uint32_t h = 5381;
    h = ((h << 5) + h) ^ (uint32_t)a->kind;
    switch (a->kind) {
    case ATOM_SYMBOL: {
        uint64_t sh = symbol_hash_value(g_symbols, a->sym_id);
        h = ((h << 5) + h) ^ (uint32_t)(sh & 0xFFFFFFFFu);
        h = ((h << 5) + h) ^ (uint32_t)(sh >> 32);
        break;
    }
    case ATOM_VAR:
        h = ((h << 5) + h) ^ (uint32_t)(a->var_id & 0xFFFFFFFFu);
        h = ((h << 5) + h) ^ (uint32_t)(a->var_id >> 32);
        break;
    case ATOM_GROUNDED:
        h = ((h << 5) + h) ^ (uint32_t)a->ground.gkind;
        switch (a->ground.gkind) {
        case GV_INT: h = ((h << 5) + h) ^ (uint32_t)(a->ground.ival & 0xFFFFFFFF); break;
        case GV_FLOAT: { union { double d; uint64_t u; } conv; conv.d = a->ground.fval;
                         h = ((h << 5) + h) ^ (uint32_t)(conv.u & 0xFFFFFFFF); break; }
        case GV_BOOL: h = ((h << 5) + h) ^ (uint32_t)a->ground.bval; break;
        case GV_STRING: {
            for (const char *p = a->ground.sval; *p; p++)
                h = ((h << 5) + h) ^ (uint32_t)*p;
            break;
        }
        case GV_BIGINT: {
            for (const char *p = atom_bigint_cstr(a); p && *p; p++)
                h = ((h << 5) + h) ^ (uint32_t)*p;
            break;
        }
        case GV_RATIONAL: {
            for (const char *p = atom_rational_cstr(a); p && *p; p++)
                h = ((h << 5) + h) ^ (uint32_t)*p;
            break;
        }
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            break; /* mutable/contextual — don't hash-cons */
        }
        break;
    case ATOM_EXPR:
        h = ((h << 5) + h) ^ (uint32_t)(a->expr.len & 0xFFFFFFFFu);
        h = ((h << 5) + h) ^ (uint32_t)(a->expr.len >> 32);
        for (CettaExprIndex i = 0; i < a->expr.len; i++)
            h = ((h << 5) + h) ^ atom_hash(a->expr.elems[i]);
        break;
    }
    return h;
}

uint32_t atom_hash(Atom *a) {
    if (!a)
        return 0;
    if ((a->flags & ATOM_FLAG_HASH_VALID) != 0)
        return a->hash_cache;
    uint32_t h = atom_hash_compute(a);
    a->hash_cache = h;
    a->flags |= ATOM_FLAG_HASH_VALID;
    return h;
}

bool atom_eq_fast(Atom *a, Atom *b) {
    if (a == b) return true;
    /* If both are hash-consed, pointer equality is authoritative for immutable atoms */
    return atom_eq(a, b);
}

void hashcons_init(HashConsTable *hc) {
    hc->size = HASHCONS_TABLE_SIZE;
    hc->used = 0;
    hc->table = cetta_malloc(sizeof(Atom *) * hc->size);
    memset(hc->table, 0, sizeof(Atom *) * hc->size);
}

void hashcons_free(HashConsTable *hc) {
    if (!hc || !hc->table) return;
    for (uint32_t i = 0; i < hc->size; i++) {
        Atom *atom = hc->table[i];
        if (!atom) continue;
        if (atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_STRING)
            free((void *)atom->ground.sval);
        if (atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_BIGINT)
            cetta_bigint_free_owned(atom->ground.bigint);
        if (atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_RATIONAL)
            cetta_rational_free_owned(atom->ground.rational);
        if (atom->kind == ATOM_EXPR)
            free(atom->expr.elems);
        free(atom);
    }
    free(hc->table);
    hc->table = NULL;
    hc->size = hc->used = 0;
}

static bool atom_is_hash_stable(const Atom *atom) {
    if (!atom) return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BOOL:
        case GV_STRING:
        case GV_BIGINT:
        case GV_RATIONAL:
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        return false;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (!atom_is_hash_stable(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool atom_expr_is_contextual(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0 &&
           atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.native_handle);
}

static bool atom_can_hashcons(const Atom *atom) {
    if (!atom)
        return false;
    if (!atom_is_hash_stable(atom) || atom_expr_is_contextual(atom))
        return false;
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        if (!atom_can_hashcons(atom->expr.elems[i]))
            return false;
    }
    return true;
}

static uint32_t hashcons_find_slot(HashConsTable *hc, Atom *atom, bool *found) {
    uint32_t h = atom_hash(atom) % hc->size;
    for (uint32_t i = 0; i < hc->size; i++) {
        uint32_t idx = (h + i) % hc->size;
        if (!hc->table[idx]) {
            *found = false;
            return idx;
        }
        if (atom_eq(hc->table[idx], atom)) {
            *found = true;
            return idx;
        }
    }
    *found = false;
    return hc->size;
}

static void hashcons_grow(HashConsTable *hc) {
    Atom **old_table = hc->table;
    uint32_t old_size = hc->size;
    hc->size *= 2;
    hc->used = 0;
    hc->table = cetta_malloc(sizeof(Atom *) * hc->size);
    memset(hc->table, 0, sizeof(Atom *) * hc->size);
    for (uint32_t i = 0; i < old_size; i++) {
        Atom *atom = old_table[i];
        if (!atom) continue;
        bool found = false;
        uint32_t slot = hashcons_find_slot(hc, atom, &found);
        hc->table[slot] = atom;
        hc->used++;
    }
    free(old_table);
}

static Atom *hashcons_intern(HashConsTable *hc, Atom *atom);

static Atom *hashcons_alloc_owned(const Atom *atom) {
    Atom *owned = cetta_malloc(sizeof(Atom));
    *owned = *atom;
    if (atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING) {
        owned->ground.sval = strdup(atom->ground.sval);
        if (!owned->ground.sval)
            cetta_oom(strlen(atom->ground.sval) + 1);
    } else if (atom->kind == ATOM_GROUNDED &&
               atom->ground.gkind == GV_BIGINT) {
        owned->ground.bigint = cetta_bigint_clone_owned(atom->ground.bigint);
    } else if (atom->kind == ATOM_GROUNDED &&
               atom->ground.gkind == GV_RATIONAL) {
        owned->ground.rational =
            cetta_rational_clone_owned(atom->ground.rational);
    } else if (atom->kind == ATOM_EXPR) {
        if (atom->expr.len == 0) {
            owned->expr.elems = NULL;
        } else {
            if (!cetta_expr_len_mul_fits_size(atom->expr.len, sizeof(Atom *)))
                cetta_oom(SIZE_MAX);
            owned->expr.elems =
                cetta_malloc((size_t)atom->expr.len * sizeof(Atom *));
            memcpy(owned->expr.elems, atom->expr.elems,
                   (size_t)atom->expr.len * sizeof(Atom *));
        }
    }
    return owned;
}

static Atom *hashcons_intern(HashConsTable *hc, Atom *atom) {
    if (!hc || !atom)
        return atom;
    if (!atom_can_hashcons(atom))
        return atom;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_ATTEMPT);
    bool found = false;
    uint32_t slot = hashcons_find_slot(hc, atom, &found);
    if (found) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_HIT);
        return hc->table[slot];
    }
    if (slot >= hc->size)
        return atom;
    Atom *owned = hashcons_alloc_owned(atom);
    hc->table[slot] = owned;
    hc->used++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_INSERT);
    if (hc->used * 10 > hc->size * 7)
        hashcons_grow(hc);
    return owned;
}

Atom *hashcons_get(HashConsTable *hc, Atom *atom) {
    return hashcons_intern(hc, atom);
}

/* ── Variables / cached literal lookups ────────────────────────────────── */

VarInternTable *g_var_intern = NULL;

#define SYMBOL_LITERAL_CACHE_SIZE 64

typedef struct {
    const SymbolTable *table;
    const char *literal;
    SymbolId id;
} SymbolLiteralCacheEntry;

static SymbolLiteralCacheEntry g_symbol_literal_cache[SYMBOL_LITERAL_CACHE_SIZE];

static uint32_t g_var_base_counter = 1;

VarId fresh_var_id(void) {
    return (VarId)g_var_base_counter++;
}

uint32_t var_base_id(VarId id) {
    return (uint32_t)(id & 0xFFFFFFFFu);
}

uint32_t var_epoch_suffix(VarId id) {
    return (uint32_t)(id >> 32);
}

VarId var_epoch_id(VarId id, uint32_t epoch) {
    uint32_t base = var_base_id(id);
    if (base == 0) {
        base = (uint32_t)fresh_var_id();
    }
    return ((VarId)epoch << 32) | (VarId)base;
}

void var_intern_init(VarInternTable *t) {
    t->size = 4096;
    t->used = 0;
    t->spellings = cetta_malloc(sizeof(SymbolId) * t->size);
    t->ids = cetta_malloc(sizeof(VarId) * t->size);
    memset(t->spellings, 0, sizeof(SymbolId) * t->size);
    memset(t->ids, 0, sizeof(VarId) * t->size);
}

void var_intern_free(VarInternTable *t) {
    free(t->spellings);
    free(t->ids);
    t->spellings = NULL;
    t->ids = NULL;
    t->size = t->used = 0;
}

static void var_intern_insert_owned(VarInternTable *t, SymbolId spelling, VarId id) {
    uint32_t h = spelling % t->size;
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (t->spellings[idx] == SYMBOL_ID_NONE) {
            t->spellings[idx] = spelling;
            t->ids[idx] = id;
            t->used++;
            return;
        }
    }
    fprintf(stderr, "fatal: variable intern table insertion failed during resize\n");
    abort();
}

static void var_intern_resize(VarInternTable *t, uint32_t new_size) {
    SymbolId *old_spellings = t->spellings;
    VarId *old_ids = t->ids;
    uint32_t old_size = t->size;
    t->size = new_size;
    t->used = 0;
    t->spellings = cetta_malloc(sizeof(SymbolId) * t->size);
    t->ids = cetta_malloc(sizeof(VarId) * t->size);
    memset(t->spellings, 0, sizeof(SymbolId) * t->size);
    memset(t->ids, 0, sizeof(VarId) * t->size);
    for (uint32_t i = 0; i < old_size; i++) {
        if (old_spellings[i] != SYMBOL_ID_NONE)
            var_intern_insert_owned(t, old_spellings[i], old_ids[i]);
    }
    free(old_spellings);
    free(old_ids);
}

VarId var_intern(VarInternTable *t, SymbolId spelling) {
    if (!t || spelling == SYMBOL_ID_NONE) return fresh_var_id();
    if ((t->used + 1) * 10 > t->size * 7)
        var_intern_resize(t, t->size ? t->size * 2 : 4096);
    uint32_t h = spelling % t->size;
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (t->spellings[idx] == SYMBOL_ID_NONE) {
            VarId id = fresh_var_id();
            t->spellings[idx] = spelling;
            t->ids[idx] = id;
            t->used++;
            return id;
        }
        if (t->spellings[idx] == spelling)
            return t->ids[idx];
    }
    return fresh_var_id();
}

static SymbolId symbol_cached_literal(const char *name) {
    if (!g_symbols || !name || !*name) return SYMBOL_ID_NONE;

    uintptr_t key = (((uintptr_t)g_symbols) >> 4) ^ (((uintptr_t)name) >> 4);
    uint32_t idx = (uint32_t)(key % SYMBOL_LITERAL_CACHE_SIZE);
    SymbolLiteralCacheEntry *entry = &g_symbol_literal_cache[idx];

    if (entry->table == g_symbols && entry->literal == name &&
        entry->id != SYMBOL_ID_NONE) {
        return entry->id;
    }

    SymbolId id = symbol_intern_cstr(g_symbols, name);
    entry->table = g_symbols;
    entry->literal = name;
    entry->id = id;
    return id;
}

/* ── Arena ──────────────────────────────────────────────────────────────── */

void *arena_alloc(Arena *a, size_t size) {
    size_t block_capacity = 0;
    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;
    if (!a->head || a->head->used + size > ARENA_BLOCK_SIZE) {
        size_t block_size = sizeof(ArenaBlock);
        if (size > ARENA_BLOCK_SIZE)
            block_size = sizeof(ArenaBlock) - ARENA_BLOCK_SIZE + size;
        ArenaBlock *b = cetta_malloc(block_size);
        block_capacity = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
        b->capacity = block_capacity;
        b->used = 0;
        b->next = a->head;
        a->head = b;
        a->reserved_bytes += block_capacity;
        a->block_count++;
    }
    void *ptr = a->head->data + a->head->used;
    a->head->used += size;
    a->live_bytes += size;
    arena_runtime_note_alloc(a, size);
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    size_t len = strlen(s) + 1;
    char *dst = arena_alloc(a, len);
    memcpy(dst, s, len);
    return dst;
}

char *cetta_bigint_canonicalize_owned(const char *text) {
    if (!text || !*text)
        return NULL;
    const unsigned char *p = (const unsigned char *)text;
    bool neg = false;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    if (!isdigit(*p))
        return NULL;
    while (*p == '0')
        p++;
    const unsigned char *digits = p;
    while (isdigit(*p))
        p++;
    if (*p != '\0')
        return NULL;
    if (digits == p) {
        char *zero = cetta_malloc(2);
        zero[0] = '0';
        zero[1] = '\0';
        return zero;
    }
    size_t ndigits = (size_t)(p - digits);
    char *out = cetta_malloc(ndigits + (neg ? 2u : 1u));
    size_t pos = 0;
    if (neg)
        out[pos++] = '-';
    memcpy(out + pos, digits, ndigits);
    out[pos + ndigits] = '\0';
    return out;
}

bool cetta_bigint_text_fits_i64(const char *text, int64_t *out) {
    if (!text)
        return false;
    char *endp = NULL;
    errno = 0;
    long long value = strtoll(text, &endp, 10);
    if (errno != 0 || !endp || *endp != '\0')
        return false;
    if (out)
        *out = (int64_t)value;
    return true;
}

int cetta_bigint_compare_cstr(const char *lhs, const char *rhs) {
    char *lc = cetta_bigint_canonicalize_owned(lhs);
    char *rc = cetta_bigint_canonicalize_owned(rhs);
    if (!lc || !rc) {
        const unsigned char *lp = (const unsigned char *)(lhs ? lhs : "");
        const unsigned char *rp = (const unsigned char *)(rhs ? rhs : "");
        while (*lp && *lp == *rp) {
            lp++;
            rp++;
        }
        int result = (*lp > *rp) - (*lp < *rp);
        free(lc);
        free(rc);
        return result;
    }

    bool lneg = lc[0] == '-';
    bool rneg = rc[0] == '-';
    int result = 0;
    if (lneg != rneg) {
        result = lneg ? -1 : 1;
    } else {
        const char *ld = lneg ? lc + 1 : lc;
        const char *rd = rneg ? rc + 1 : rc;
        size_t llen = strlen(ld);
        size_t rlen = strlen(rd);
        if (llen != rlen) {
            result = llen < rlen ? -1 : 1;
        } else {
            int raw = memcmp(ld, rd, llen);
            result = raw;
            if (result < 0)
                result = -1;
            else if (result > 0)
                result = 1;
        }
        if (lneg)
            result = -result;
    }

    free(lc);
    free(rc);
    return result;
}

#if CETTA_BUILD_WITH_GMP
static void cetta_bigint_clear_value(void *ptr) {
    CettaBigInt *big = ptr;
    if (big && big->has_value) {
        mpz_clear(big->value);
        big->has_value = false;
    }
}
#endif

static CettaBigInt *cetta_bigint_new_arena(Arena *a, const char *canonical) {
    CettaBigInt *big = arena_alloc(a, sizeof(CettaBigInt));
    big->text = arena_strdup(a, canonical);
#if CETTA_BUILD_WITH_GMP
    mpz_init_set_str(big->value, canonical, 10);
    big->has_value = true;
    arena_register_finalizer(a, cetta_bigint_clear_value, big);
#endif
    return big;
}

static CettaBigInt *cetta_bigint_new_owned_from_text(const char *canonical) {
    CettaBigInt *big = cetta_malloc(sizeof(CettaBigInt));
    big->text = strdup(canonical);
    if (!big->text)
        cetta_oom(strlen(canonical) + 1);
#if CETTA_BUILD_WITH_GMP
    mpz_init_set_str(big->value, canonical, 10);
    big->has_value = true;
#endif
    return big;
}

#if CETTA_BUILD_WITH_GMP
static CettaBigInt *cetta_bigint_new_owned_from_mpz(const char *canonical,
                                                   const mpz_t value) {
    CettaBigInt *big = cetta_malloc(sizeof(CettaBigInt));
    big->text = strdup(canonical);
    if (!big->text)
        cetta_oom(strlen(canonical) + 1);
    mpz_init_set(big->value, value);
    big->has_value = true;
    return big;
}
#endif

static CettaBigInt *cetta_bigint_clone_owned(const CettaBigInt *src) {
    if (!src)
        return NULL;
#if CETTA_BUILD_WITH_GMP
    if (src->has_value)
        return cetta_bigint_new_owned_from_mpz(src->text, src->value);
#endif
    return cetta_bigint_new_owned_from_text(src->text);
}

static void cetta_bigint_free_owned(CettaBigInt *big) {
    if (!big)
        return;
#if CETTA_BUILD_WITH_GMP
    if (big->has_value)
        mpz_clear(big->value);
#endif
    free((void *)big->text);
    free(big);
}

#if CETTA_BUILD_WITH_GMP
static void gmp_free_string(char *text) {
    if (!text)
        return;
    void (*free_func)(void *, size_t) = NULL;
    mp_get_memory_functions(NULL, NULL, &free_func);
    free_func(text, strlen(text) + 1);
}

static bool mpq_set_canonical_text(mpq_t out, const char *text) {
    const char *slash = text ? strchr(text, '/') : NULL;
    if (!slash || strchr(slash + 1, '/'))
        return false;
    size_t nlen = (size_t)(slash - text);
    size_t dlen = strlen(slash + 1);
    if (nlen == 0 || dlen == 0)
        return false;

    char *num_text = cetta_malloc(nlen + 1);
    char *den_text = cetta_malloc(dlen + 1);
    memcpy(num_text, text, nlen);
    num_text[nlen] = '\0';
    memcpy(den_text, slash + 1, dlen + 1);

    mpz_t num, den;
    mpz_inits(num, den, NULL);
    bool ok = mpz_set_str(num, num_text, 10) == 0 &&
              mpz_set_str(den, den_text, 10) == 0 &&
              mpz_sgn(den) != 0;
    if (ok) {
        mpq_set_num(out, num);
        mpq_set_den(out, den);
        mpq_canonicalize(out);
    }
    mpz_clears(num, den, NULL);
    free(num_text);
    free(den_text);
    return ok;
}
#endif

char *cetta_rational_canonicalize_owned(const char *text) {
#if CETTA_BUILD_WITH_GMP
    if (!text || !*text)
        return NULL;
    mpq_t q;
    mpq_init(q);
    if (!mpq_set_canonical_text(q, text)) {
        mpq_clear(q);
        return NULL;
    }
    char *num = mpz_get_str(NULL, 10, mpq_numref(q));
    char *den = mpz_get_str(NULL, 10, mpq_denref(q));
    size_t nlen = strlen(num);
    size_t dlen = strlen(den);
    char *out = cetta_malloc(nlen + dlen + 2);
    memcpy(out, num, nlen);
    out[nlen] = '/';
    memcpy(out + nlen + 1, den, dlen + 1);
    gmp_free_string(num);
    gmp_free_string(den);
    mpq_clear(q);
    return out;
#else
    (void)text;
    return NULL;
#endif
}

int cetta_rational_compare_cstr(const char *lhs, const char *rhs) {
    char *lc = cetta_rational_canonicalize_owned(lhs);
    char *rc = cetta_rational_canonicalize_owned(rhs);
    if (!lc || !rc) {
        const unsigned char *lp = (const unsigned char *)(lhs ? lhs : "");
        const unsigned char *rp = (const unsigned char *)(rhs ? rhs : "");
        while (*lp && *lp == *rp) {
            lp++;
            rp++;
        }
        int result = (*lp > *rp) - (*lp < *rp);
        free(lc);
        free(rc);
        return result;
    }
    const unsigned char *lp = (const unsigned char *)lc;
    const unsigned char *rp = (const unsigned char *)rc;
    while (*lp && *lp == *rp) {
        lp++;
        rp++;
    }
    int result = (*lp > *rp) - (*lp < *rp);
    free(lc);
    free(rc);
    return result;
}

#if CETTA_BUILD_WITH_GMP
static void cetta_rational_clear_value(void *ptr) {
    CettaRational *rat = ptr;
    if (rat && rat->has_value) {
        mpq_clear(rat->value);
        rat->has_value = false;
    }
}
#endif

static CettaRational *cetta_rational_new_arena(Arena *a, const char *canonical)
    __attribute__((unused));
static CettaRational *cetta_rational_new_arena(Arena *a, const char *canonical) {
    CettaRational *rat = arena_alloc(a, sizeof(CettaRational));
    rat->text = arena_strdup(a, canonical);
#if CETTA_BUILD_WITH_GMP
    mpq_init(rat->value);
    mpq_set_canonical_text(rat->value, canonical);
    rat->has_value = true;
    arena_register_finalizer(a, cetta_rational_clear_value, rat);
#endif
    return rat;
}

static CettaRational *cetta_rational_new_owned_from_text(const char *canonical) {
    CettaRational *rat = cetta_malloc(sizeof(CettaRational));
    rat->text = strdup(canonical);
    if (!rat->text)
        cetta_oom(strlen(canonical) + 1);
#if CETTA_BUILD_WITH_GMP
    mpq_init(rat->value);
    mpq_set_canonical_text(rat->value, canonical);
    rat->has_value = true;
#endif
    return rat;
}

#if CETTA_BUILD_WITH_GMP
static CettaRational *cetta_rational_new_owned_from_mpq(const char *canonical,
                                                       const mpq_t value) {
    CettaRational *rat = cetta_malloc(sizeof(CettaRational));
    rat->text = strdup(canonical);
    if (!rat->text)
        cetta_oom(strlen(canonical) + 1);
    mpq_init(rat->value);
    mpq_set(rat->value, value);
    rat->has_value = true;
    return rat;
}
#endif

static CettaRational *cetta_rational_clone_owned(const CettaRational *src) {
    if (!src)
        return NULL;
#if CETTA_BUILD_WITH_GMP
    if (src->has_value)
        return cetta_rational_new_owned_from_mpq(src->text, src->value);
#endif
    return cetta_rational_new_owned_from_text(src->text);
}

static void cetta_rational_free_owned(CettaRational *rat) {
    if (!rat)
        return;
#if CETTA_BUILD_WITH_GMP
    if (rat->has_value)
        mpq_clear(rat->value);
#endif
    free((void *)rat->text);
    free(rat);
}

/* ── Constructors ───────────────────────────────────────────────────────── */

static Atom *atom_maybe_hashcons(Arena *a, const Atom *temp) {
    if (!a || !a->hashcons || !atom_can_hashcons(temp))
        return NULL;
    return hashcons_get(a->hashcons, (Atom *)temp);
}

static uint8_t atom_flags_for_symbol_id(SymbolId sym_id) {
    uint8_t flags = 0;
    const char *bytes = symbol_bytes(g_symbols, sym_id);
    if (bytes && bytes[0] == '&')
        flags |= ATOM_FLAG_HAS_REGISTRY_REFS;
    return flags;
}

static uint8_t atom_flags_from_children(Atom **elems, CettaExprLen len) {
    uint8_t flags = 0;
    for (CettaExprIndex i = 0; i < len; i++) {
        if (atom_has_vars(elems[i]))
            flags |= ATOM_FLAG_HAS_VARS;
        if (atom_has_registry_refs(elems[i]))
            flags |= ATOM_FLAG_HAS_REGISTRY_REFS;
    }
    return flags;
}

Atom *atom_symbol_id(Arena *a, SymbolId sym_id) {
    Atom temp = {0};
    temp.kind = ATOM_SYMBOL;
    temp.flags = atom_flags_for_symbol_id(sym_id);
    temp.var_id = VAR_ID_NONE;
    temp.sym_id = sym_id;
    temp.hash_cache = 0;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_symbol(Arena *a, const char *name) {
    return atom_symbol_id(a, symbol_intern_cstr(g_symbols, name));
}

Atom *atom_var_with_spelling(Arena *a, SymbolId spelling, VarId id) {
    Atom temp = {0};
    temp.kind = ATOM_VAR;
    temp.flags = ATOM_FLAG_HAS_VARS;
    temp.var_id = id ? id : fresh_var_id();
    temp.sym_id = spelling;
    temp.hash_cache = 0;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_var_with_id(Arena *a, const char *name, VarId id) {
    return atom_var_with_spelling(a, symbol_intern_cstr(g_symbols, name), id);
}

Atom *atom_var(Arena *a, const char *name) {
    SymbolId spelling = symbol_intern_cstr(g_symbols, name);
    VarId id = g_var_intern ? var_intern(g_var_intern, spelling) : fresh_var_id();
    Atom *at = atom_var_with_id(a, name, id);
    return at;
}

Atom *atom_int(Arena *a, int64_t val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = 0;
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.ground.gkind = GV_INT;
    temp.ground.ival = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_bigint(Arena *a, const char *val) {
    char *canonical = cetta_bigint_canonicalize_owned(val);
    if (!canonical)
        return NULL;
    int64_t small = 0;
    if (cetta_bigint_text_fits_i64(canonical, &small)) {
        free(canonical);
        return atom_int(a, small);
    }

    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = 0;
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.ground.gkind = GV_BIGINT;
    CettaBigInt temp_big = {0};
    temp_big.text = canonical;
    temp.ground.bigint = &temp_big;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) {
        free(canonical);
        return shared;
    }
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = temp.kind;
    at->flags = temp.flags;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->ground.gkind = temp.ground.gkind;
    at->ground.bigint = cetta_bigint_new_arena(a, canonical);
    free(canonical);
    return at;
}

const char *atom_bigint_cstr(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_BIGINT || !atom->ground.bigint)
        return NULL;
    return atom->ground.bigint->text;
}

#if CETTA_BUILD_WITH_GMP
bool atom_bigint_get_mpz(const Atom *atom, mpz_t out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_BIGINT || !atom->ground.bigint)
        return false;
    CettaBigInt *big = atom->ground.bigint;
    if (big->has_value) {
        mpz_set(out, big->value);
        return true;
    }
    return mpz_set_str(out, big->text, 10) == 0;
}

Atom *atom_bigint_from_mpz(Arena *a, const mpz_t value) {
    char *text = mpz_get_str(NULL, 10, value);
    int64_t small = 0;
    if (cetta_bigint_text_fits_i64(text, &small)) {
        void (*free_func)(void *, size_t) = NULL;
        mp_get_memory_functions(NULL, NULL, &free_func);
        free_func(text, strlen(text) + 1);
        return atom_int(a, small);
    }

    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = 0;
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.ground.gkind = GV_BIGINT;
    CettaBigInt temp_big = {0};
    temp_big.text = text;
    mpz_init_set(temp_big.value, value);
    temp_big.has_value = true;
    temp.ground.bigint = &temp_big;

    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) {
        mpz_clear(temp_big.value);
        void (*free_func)(void *, size_t) = NULL;
        mp_get_memory_functions(NULL, NULL, &free_func);
        free_func(text, strlen(text) + 1);
        return shared;
    }

    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = temp.kind;
    at->flags = temp.flags;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->ground.gkind = temp.ground.gkind;
    at->ground.bigint = cetta_bigint_new_arena(a, text);
    mpz_set(at->ground.bigint->value, value);
    mpz_clear(temp_big.value);
    void (*free_func)(void *, size_t) = NULL;
    mp_get_memory_functions(NULL, NULL, &free_func);
    free_func(text, strlen(text) + 1);
    return at;
}
#endif

bool cetta_rational_text_exceeds_digit_limit(const char *text,
                                             uint64_t max_digits,
                                             bool *valid_out) {
    if (valid_out)
        *valid_out = false;
#if CETTA_BUILD_WITH_GMP
    if (!text || !*text)
        return false;
    mpq_t q;
    mpq_init(q);
    bool ok = mpq_set_canonical_text(q, text);
    if (valid_out)
        *valid_out = ok;
    if (!ok) {
        mpq_clear(q);
        return false;
    }
    if (max_digits == UINT64_MAX || mpz_cmp_ui(mpq_denref(q), 1u) == 0) {
        mpq_clear(q);
        return false;
    }
    size_t n_digits = mpz_sizeinbase(mpq_numref(q), 10);
    size_t d_digits = mpz_sizeinbase(mpq_denref(q), 10);
    bool too_large = n_digits > UINT64_MAX - d_digits ||
        (uint64_t)(n_digits + d_digits) > max_digits;
    mpq_clear(q);
    return too_large;
#else
    (void)text;
    (void)max_digits;
    return false;
#endif
}

Atom *atom_rational_limited(Arena *a, const char *val, uint64_t max_digits,
                            bool *too_large_out) {
    if (too_large_out)
        *too_large_out = false;
#if CETTA_BUILD_WITH_GMP
    if (!a || !val)
        return NULL;
    mpq_t q;
    mpq_init(q);
    bool ok = mpq_set_canonical_text(q, val);
    if (!ok) {
        mpq_clear(q);
        return NULL;
    }
    if (max_digits != UINT64_MAX && mpz_cmp_ui(mpq_denref(q), 1u) != 0) {
        size_t n_digits = mpz_sizeinbase(mpq_numref(q), 10);
        size_t d_digits = mpz_sizeinbase(mpq_denref(q), 10);
        if (n_digits > UINT64_MAX - d_digits ||
            (uint64_t)(n_digits + d_digits) > max_digits) {
            if (too_large_out)
                *too_large_out = true;
            mpq_clear(q);
            return NULL;
        }
    }
    Atom *out = atom_rational_from_mpq(a, q);
    mpq_clear(q);
    return out;
#else
    (void)a;
    (void)val;
    (void)max_digits;
    return NULL;
#endif
}

Atom *atom_rational(Arena *a, const char *val) {
#if CETTA_BUILD_WITH_GMP
    if (!a || !val)
        return NULL;
    mpq_t q;
    mpq_init(q);
    bool ok = mpq_set_canonical_text(q, val);
    if (!ok) {
        mpq_clear(q);
        return NULL;
    }
    Atom *out = atom_rational_from_mpq(a, q);
    mpq_clear(q);
    return out;
#else
    (void)a;
    (void)val;
    return NULL;
#endif
}

const char *atom_rational_cstr(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_RATIONAL || !atom->ground.rational)
        return NULL;
    return atom->ground.rational->text;
}

#if CETTA_BUILD_WITH_GMP
bool atom_rational_get_mpq(const Atom *atom, mpq_t out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_RATIONAL || !atom->ground.rational)
        return false;
    CettaRational *rat = atom->ground.rational;
    if (rat->has_value) {
        mpq_set(out, rat->value);
        return true;
    }
    return mpq_set_canonical_text(out, rat->text);
}

Atom *atom_rational_from_mpq(Arena *a, const mpq_t value) {
    mpq_t q;
    mpq_init(q);
    mpq_set(q, value);
    mpq_canonicalize(q);
    if (mpz_cmp_ui(mpq_denref(q), 1u) == 0) {
        Atom *out = atom_bigint_from_mpz(a, mpq_numref(q));
        mpq_clear(q);
        return out;
    }

    char *num = mpz_get_str(NULL, 10, mpq_numref(q));
    char *den = mpz_get_str(NULL, 10, mpq_denref(q));
    size_t nlen = strlen(num);
    size_t dlen = strlen(den);
    char *text = cetta_malloc(nlen + dlen + 2);
    memcpy(text, num, nlen);
    text[nlen] = '/';
    memcpy(text + nlen + 1, den, dlen + 1);

    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = 0;
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.ground.gkind = GV_RATIONAL;
    CettaRational temp_rat = {0};
    temp_rat.text = text;
    mpq_init(temp_rat.value);
    mpq_set(temp_rat.value, q);
    temp_rat.has_value = true;
    temp.ground.rational = &temp_rat;

    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) {
        mpq_clear(temp_rat.value);
        free(text);
        gmp_free_string(num);
        gmp_free_string(den);
        mpq_clear(q);
        return shared;
    }

    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = temp.kind;
    at->flags = temp.flags;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->ground.gkind = temp.ground.gkind;
    at->ground.rational = cetta_rational_new_arena(a, text);
    mpq_set(at->ground.rational->value, q);

    mpq_clear(temp_rat.value);
    free(text);
    gmp_free_string(num);
    gmp_free_string(den);
    mpq_clear(q);
    return at;
}
#endif

Atom *atom_space(Arena *a, void *space_ptr) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->flags = 0;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->ground.gkind = GV_SPACE;
    at->ground.ptr = space_ptr;
    return at;
}

Atom *atom_state(Arena *a, StateCell *cell) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->flags = 0;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->ground.gkind = GV_STATE;
    at->ground.ptr = cell;
    return at;
}

Atom *atom_capture(Arena *a, CaptureClosure *closure) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->flags = 0;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->ground.gkind = GV_CAPTURE;
    at->ground.ptr = closure;
    return at;
}

Atom *atom_foreign(Arena *a, CettaForeignValue *value) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->flags = 0;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->ground.gkind = GV_FOREIGN;
    at->ground.ptr = value;
    return at;
}

Atom *atom_float(Arena *a, double val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = 0;
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.ground.gkind = GV_FLOAT;
    temp.ground.fval = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_bool(Arena *a, bool val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = 0;
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.ground.gkind = GV_BOOL;
    temp.ground.bval = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_string(Arena *a, const char *val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = 0;
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.ground.gkind = GV_STRING;
    temp.ground.sval = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = temp.kind;
    at->flags = temp.flags;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->ground.gkind = temp.ground.gkind;
    at->ground.sval = arena_strdup(a, val);
    return at;
}

Atom *atom_expr(Arena *a, Atom **elems, CettaExprLen len) {
    Atom temp = {0};
    size_t elems_bytes = 0;
    if (len > 0 && !cetta_expr_len_mul_fits_size(len, sizeof(Atom *)))
        cetta_oom(SIZE_MAX);
    elems_bytes = (size_t)len * sizeof(Atom *);
    temp.kind = ATOM_EXPR;
    temp.flags = atom_flags_from_children(elems, len);
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.expr.len = len;
    temp.expr.elems = elems;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = temp.kind;
    at->flags = temp.flags;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->hash_cache = 0;
    at->expr.len = len;
    at->expr.elems = arena_alloc(a, elems_bytes);
    if (elems && elems_bytes > 0) memcpy(at->expr.elems, elems, elems_bytes);
    return at;
}

Atom *atom_expr_shared(Arena *a, Atom **elems, CettaExprLen len) {
    return atom_expr(a, elems, len);
}

Atom *atom_expr2(Arena *a, Atom *a1, Atom *a2) {
    Atom *elems[2] = {a1, a2};
    return atom_expr(a, elems, 2);
}

Atom *atom_expr3(Arena *a, Atom *a1, Atom *a2, Atom *a3) {
    Atom *elems[3] = {a1, a2, a3};
    return atom_expr(a, elems, 3);
}

/* ── Special atoms ──────────────────────────────────────────────────────── */

Atom *atom_empty(Arena *a) { return atom_symbol_id(a, g_builtin_syms.empty); }
Atom *atom_unit(Arena *a)  { return atom_expr(a, NULL, 0); }
Atom *atom_true(Arena *a)  { return atom_bool(a, true); }
Atom *atom_false(Arena *a) { return atom_bool(a, false); }

/* ── Type system atoms ──────────────────────────────────────────────────── */

Atom *atom_undefined_type(Arena *a) { return atom_symbol_id(a, g_builtin_syms.undefined_type); }
Atom *atom_atom_type(Arena *a)      { return atom_symbol_id(a, g_builtin_syms.atom); }
Atom *atom_symbol_type(Arena *a)    { return atom_symbol_id(a, g_builtin_syms.symbol); }
Atom *atom_variable_type(Arena *a)  { return atom_symbol_id(a, g_builtin_syms.variable); }
Atom *atom_expression_type(Arena *a){ return atom_symbol_id(a, g_builtin_syms.expression); }
Atom *atom_grounded_type(Arena *a)  { return atom_symbol_id(a, g_builtin_syms.grounded); }

Atom *get_meta_type(Arena *a, Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL:   return atom_symbol_type(a);
    case ATOM_VAR:      return atom_variable_type(a);
    case ATOM_GROUNDED: return atom_grounded_type(a);
    case ATOM_EXPR:     return atom_expression_type(a);
    }
    return atom_undefined_type(a);
}

bool atom_is_meta_type(Atom *type) {
    return atom_is_symbol_id(type, g_builtin_syms.atom) ||
           atom_is_symbol_id(type, g_builtin_syms.symbol) ||
           atom_is_symbol_id(type, g_builtin_syms.variable) ||
           atom_is_symbol_id(type, g_builtin_syms.expression) ||
           atom_is_symbol_id(type, g_builtin_syms.grounded);
}

bool atom_meta_type_accepts(Arena *a, Atom *formal, Atom *actual) {
    if (atom_is_symbol_id(formal, g_builtin_syms.atom))
        return true;
    if (!atom_is_meta_type(formal))
        return false;
    return atom_eq(formal, get_meta_type(a, actual));
}

Atom *atom_error(Arena *a, Atom *source, Atom *message) {
    return atom_expr3(a, atom_symbol_id(a, g_builtin_syms.error), source, message);
}

/* ── Predicates ─────────────────────────────────────────────────────────── */

bool atom_is_symbol(Atom *a, const char *name) {
    return atom_is_symbol_id(a, symbol_cached_literal(name));
}

bool atom_is_empty(Atom *a) {
    return atom_is_symbol_id(a, g_builtin_syms.empty);
}

bool atom_is_error(Atom *a) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1 &&
           atom_is_symbol_id(a->expr.elems[0], g_builtin_syms.error);
}

bool atom_is_empty_or_error(Atom *a) {
    return atom_is_empty(a) || atom_is_error(a);
}

bool atom_is_var(Atom *a)  { return a->kind == ATOM_VAR; }
bool atom_is_expr(Atom *a) { return a->kind == ATOM_EXPR; }

bool atom_is_symbol_id(Atom *a, SymbolId id) {
    return a && a->kind == ATOM_SYMBOL && a->sym_id == id;
}

const char *atom_name_cstr(Atom *a) {
    if (!a) return "";
    if (a->kind == ATOM_SYMBOL || a->kind == ATOM_VAR)
        return symbol_bytes(g_symbols, a->sym_id);
    return "";
}

SymbolId atom_head_symbol_id(Atom *a) {
    if (!a) return SYMBOL_ID_NONE;
    if (a->kind == ATOM_SYMBOL) return a->sym_id;
    if (a->kind == ATOM_EXPR && a->expr.len > 0 &&
        a->expr.elems[0]->kind == ATOM_SYMBOL)
        return a->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

/* ── Comparison ─────────────────────────────────────────────────────────── */

bool atom_eq(Atom *a, Atom *b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case ATOM_SYMBOL:
        return a->sym_id == b->sym_id;
    case ATOM_VAR:
        return a->var_id == b->var_id;
    case ATOM_GROUNDED:
        if (a->ground.gkind != b->ground.gkind) return false;
        switch (a->ground.gkind) {
        case GV_INT:    return a->ground.ival == b->ground.ival;
        case GV_FLOAT:  return a->ground.fval == b->ground.fval;
        case GV_BOOL:   return a->ground.bval == b->ground.bval;
        case GV_STRING: return strcmp(a->ground.sval, b->ground.sval) == 0;
        case GV_BIGINT:
            return cetta_bigint_compare_cstr(atom_bigint_cstr(a),
                                            atom_bigint_cstr(b)) == 0;
        case GV_RATIONAL:
            return cetta_rational_compare_cstr(atom_rational_cstr(a),
                                               atom_rational_cstr(b)) == 0;
        case GV_SPACE:  return a->ground.ptr == b->ground.ptr;
        case GV_CAPTURE: return a->ground.ptr == b->ground.ptr;
        case GV_FOREIGN: return a->ground.ptr == b->ground.ptr;
        case GV_STATE: {
            StateCell *ca = (StateCell *)a->ground.ptr;
            StateCell *cb = (StateCell *)b->ground.ptr;
            return atom_eq(ca->value, cb->value);
        }
        }
        return false;
    case ATOM_EXPR:
        if (a->expr.len != b->expr.len) return false;
        for (CettaExprIndex i = 0; i < a->expr.len; i++)
            if (!atom_eq(a->expr.elems[i], b->expr.elems[i])) return false;
        return true;
    }
    return false;
}

/* ── Printing ───────────────────────────────────────────────────────────── */

/* ── Deep copy ──────────────────────────────────────────────────────────── */

static Atom *atom_deep_copy_impl(Arena *dst, Atom *src, bool share) {
    switch (src->kind) {
    case ATOM_SYMBOL:   return atom_symbol_id(dst, src->sym_id);
    case ATOM_VAR:      return atom_var_with_spelling(dst, src->sym_id, src->var_id);
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_int(dst, src->ground.ival))
                                       : atom_int(dst, src->ground.ival);
        case GV_FLOAT:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_float(dst, src->ground.fval))
                                       : atom_float(dst, src->ground.fval);
        case GV_BOOL:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_bool(dst, src->ground.bval))
                                       : atom_bool(dst, src->ground.bval);
        case GV_STRING:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_string(dst, src->ground.sval))
                                       : atom_string(dst, src->ground.sval);
        case GV_BIGINT:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_bigint(dst, atom_bigint_cstr(src)))
                                       : atom_bigint(dst, atom_bigint_cstr(src));
        case GV_RATIONAL:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_rational(dst, atom_rational_cstr(src)))
                                       : atom_rational(dst, atom_rational_cstr(src));
        case GV_SPACE:  return atom_space(dst, src->ground.ptr);
        case GV_STATE:  return atom_state(dst, (StateCell *)src->ground.ptr);
        case GV_CAPTURE: return atom_capture(dst, (CaptureClosure *)src->ground.ptr);
        case GV_FOREIGN: return atom_foreign(dst, (CettaForeignValue *)src->ground.ptr);
        }
        return atom_symbol(dst, "?");
    case ATOM_EXPR: {
        if (src->expr.len > 0 &&
            !cetta_expr_len_mul_fits_size(src->expr.len, sizeof(Atom *)))
            cetta_oom(SIZE_MAX);
        Atom **elems = arena_alloc(dst, (size_t)src->expr.len * sizeof(Atom *));
        for (CettaExprIndex i = 0; i < src->expr.len; i++)
            elems[i] = atom_deep_copy_impl(dst, src->expr.elems[i], share);
        return share ? atom_expr_shared(dst, elems, src->expr.len)
                     : atom_expr(dst, elems, src->expr.len);
    }
    }
    return atom_symbol(dst, "?");
}

Atom *atom_deep_copy(Arena *dst, Atom *src) {
    return atom_deep_copy_impl(dst, src, false);
}

Atom *atom_deep_copy_shared(Arena *dst, Atom *src) {
    return atom_deep_copy_impl(dst, src, true);
}

/* ── Printing ───────────────────────────────────────────────────────────── */

int cetta_format_float(char *buf, size_t size, double value) {
    if (isnan(value)) {
        return snprintf(buf, size, "NaN");
    } else if (isinf(value)) {
        return snprintf(buf, size, "%s", signbit(value) ? "-inf" : "inf");
    } else if (isfinite(value) && floor(value) == value) {
        if (fabs(value) < 9007199254740992.0)
            return snprintf(buf, size, "%.1f", value);
        return snprintf(buf, size, "%.16e", value);
    } else {
        return snprintf(buf, size, "%.16g", value);
    }
}

static void atom_print_float(double value, FILE *out) {
    char buf[64];
    int printed = cetta_format_float(buf, sizeof(buf), value);
    if (printed > 0)
        fputs(buf, out);
}

void atom_print(Atom *a, FILE *out) {
    switch (a->kind) {
    case ATOM_SYMBOL:
        fputs(atom_name_cstr(a), out);
        break;
    case ATOM_VAR:
        fprintf(out, "$%s", atom_name_cstr(a));
        {
            uint32_t epoch = var_epoch_suffix(a->var_id);
            if (epoch != 0)
                fprintf(out, "#%u", epoch);
        }
        break;
    case ATOM_GROUNDED:
        switch (a->ground.gkind) {
        case GV_INT:    fprintf(out, "%ld", (long)a->ground.ival); break;
        case GV_FLOAT:
            atom_print_float(a->ground.fval, out);
            break;
        case GV_BOOL:   fputs(a->ground.bval ? "True" : "False", out); break;
        case GV_BIGINT: fputs(atom_bigint_cstr(a), out); break;
        case GV_RATIONAL: fputs(atom_rational_cstr(a), out); break;
        case GV_STRING: {
            fputc('"', out);
            for (const char *p = a->ground.sval; *p; p++) {
                if (*p == '\n') fputs("\\n", out);
                else if (*p == '"') fputs("\\\"", out);
                else if (*p == '\\') fputs("\\\\", out);
                else fputc(*p, out);
            }
            fputc('"', out);
            break;
        }
        case GV_SPACE:  fprintf(out, "<space %p>", a->ground.ptr); break;
        case GV_STATE: {
            StateCell *cell = (StateCell *)a->ground.ptr;
            fputs("(State ", out);
            atom_print(cell->value, out);
            fputc(')', out);
            break;
        }
        case GV_CAPTURE:
            fputs("capture", out);
            break;
        case GV_FOREIGN:
            fprintf(out, "<foreign %p>", a->ground.ptr);
            break;
        }
        break;
    case ATOM_EXPR:
        fputc('(', out);
        for (CettaExprIndex i = 0; i < a->expr.len; i++) {
            if (i > 0) fputc(' ', out);
            atom_print(a->expr.elems[i], out);
        }
        fputc(')', out);
        break;
    }
}

char *atom_to_parseable_string(Arena *a, Atom *atom) {
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);
    if (!mem) {
        return arena_strdup(a, "");
    }
    atom_print(atom, mem);
    fclose(mem);
    char *out = arena_strdup(a, buf ? buf : "");
    free(buf);
    return out;
}

char *atom_to_string(Arena *a, Atom *atom) {
    if (atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        return arena_strdup(a, atom->ground.sval);
    }
    return atom_to_parseable_string(a, atom);
}
