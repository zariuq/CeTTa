/* rc_memo.c - memo tables (see rc_memo.h for the contract). */
#include "rc_memo.h"
#include "rc_intern.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t hash; uint16_t j; uint16_t n; T *args[RC_MEMO_ARGS]; T *result; } Entry;
/* two generations: new entries go to cur; when cur is full, old is dropped and cur becomes old; a
 * hit in old is copied back into cur, so the entries in use survive */
typedef struct { Entry *tab; size_t cap, count; } Gen;
static Gen cur, old;
static uint64_t hits, misses, clears;
uint64_t rc_memo_gen = 1;
static bool hooked;
#define NONE_RESULT ((T *)(uintptr_t)8)   /* a failure (no result); never a value */

static uint32_t hash_key(uint16_t j, int n, T *const *a) {
    uint64_t h = 0x9e3779b97f4a7c15ull ^ ((uint64_t)j << 8) ^ (uint64_t)n;
    for (int i = 0; i < n; i++) { h ^= (uint64_t)(uintptr_t)a[i]; h *= 0xff51afd7ed558ccdull; h ^= h >> 29; }
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;   /* every bit of the key reaches the low bits */
    uint32_t r = (uint32_t)h;
    return r ? r : 1u;   /* 0 marks an empty slot */
}
static void gen_free(Gen *g) { free(g->tab); g->tab = NULL; g->cap = g->count = 0; }
void rc_memo_clear(void) {
    rc_memo_gen++;   /* every front cache entry made before is invalid */
    if (cur.count || old.count) clears++;
    gen_free(&cur);
    gen_free(&old);
}
static void gen_grow(Gen *g) {
    size_t ncap = g->cap ? 2 * g->cap : 1024;
    Entry *nt = rc_alloc_table(ncap * sizeof *nt);   /* random lookups: large pages where possible */
    if (!nt) rc_refinement_fail("memory for a memo table");
    for (size_t i = 0; i < g->cap; i++) {
        if (!g->tab[i].hash) continue;
        size_t k = g->tab[i].hash & (ncap - 1);
        while (nt[k].hash) k = (k + 1) & (ncap - 1);
        nt[k] = g->tab[i];
    }
    free(g->tab);
    g->tab = nt;
    g->cap = ncap;
}
static Entry *gen_find(Gen *g, uint32_t h, uint16_t j, int n, T *const *args) {
    if (!g->count) return NULL;
    for (size_t k = h & (g->cap - 1); g->tab[k].hash; k = (k + 1) & (g->cap - 1)) {
        Entry *e = &g->tab[k];
        if (e->hash == h && e->j == j && e->n == n) {
            int i = 0;
            while (i < n && e->args[i] == args[i]) i++;
            if (i == n) return e;
        }
    }
    return NULL;
}
static void gen_put(uint32_t h, uint16_t j, int n, T *const *args, T *stored) {
    if (cur.count >= RC_MEMO_MAX) { gen_free(&old); old = cur; cur = (Gen){NULL, 0, 0}; }
    if (2 * (cur.count + 1) > cur.cap) gen_grow(&cur);
    size_t k = h & (cur.cap - 1);
    while (cur.tab[k].hash) k = (k + 1) & (cur.cap - 1);
    Entry *e = &cur.tab[k];
    e->hash = h; e->j = j; e->n = (uint16_t)n;
    memcpy(e->args, args, (size_t)n * sizeof(T *));
    e->result = stored;
    cur.count++;
}
bool rc_memo_get(uint16_t j, int n, T *const *args, T **out) {
    uint32_t h = hash_key(j, n, args);
    Entry *e = gen_find(&cur, h, j, n, args);
    if (!e && (e = gen_find(&old, h, j, n, args))) {
        T *stored = e->result;
        gen_put(h, j, n, args, stored);   /* keep it in the current generation */
        *out = stored == NONE_RESULT ? NULL : stored;
        hits++;
        return true;
    }
    if (!e) { misses++; return false; }
    hits++;
    *out = e->result == NONE_RESULT ? NULL : e->result;
    return true;
}
void rc_memo_put(uint16_t j, int n, T *const *args, T *result) {
    if (!hooked) { rc_intern_on_collect(rc_memo_clear); hooked = true; }
    gen_put(hash_key(j, n, args), j, n, args, result ? result : NONE_RESULT);
}
RcMemoStats rc_memo_stats(void) { return (RcMemoStats){hits, misses, clears, cur.count + old.count}; }
