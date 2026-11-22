/* rc_intern.c - the interning store (see rc_intern.h for the contract). */
#include "rc_intern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t hash; uint32_t pad; RcINode *node; } Slot;
static Slot *slot;              /* open addressing, power-of-two capacity; the hash beside the node */
static size_t cap, count;
static uint64_t bytes_live, bytes_since, ncollect, nfreed;
static bool fresh, active;
static T **nullary;             /* nullary[c]: the node of constructor c, made at first use */
static size_t nnullary;
static void (*on_collect[8])(void);
static int non_collect;

#define KEEP_BYTES ((uint64_t)64 << 20)

static _Noreturn void oom(void) { rc_refinement_fail("the interning store is out of memory"); }

/* the slot table (random lookups: a large one on large pages, rc_alloc_table) */
static Slot *slots_new(size_t n) { return rc_alloc_table(n * sizeof(Slot)); }

static inline uint32_t mix(uint64_t h) {
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return (uint32_t)h;
}
static uint32_t hash_node(uint16_t c, uint32_t n, T *const *kids) {
    uint64_t h = 0x9e3779b97f4a7c15ull ^ ((uint64_t)c << 32) ^ n;
    for (uint32_t i = 0; i < n; i++) h = (h ^ (uint64_t)(uintptr_t)kids[i]) * 0x100000001b3ull + (h >> 29);
    return mix(h);
}
static uint32_t hash_bytes(const uint8_t *b, size_t n) {
    uint64_t h = 1469598103934665603ull ^ RC_INTERN_BYTES;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return mix(h ^ n);
}

static void grow(void) {
    size_t ncap = cap ? 2 * cap : 4096;
    Slot *ns = slots_new(ncap);
    if (!ns) oom();
    for (size_t i = 0; i < cap; i++) {
        RcINode *x = slot[i].node;
        if (!x) continue;
        size_t j = x->hash & (ncap - 1);
        while (ns[j].node) j = (j + 1) & (ncap - 1);
        ns[j] = slot[i];
    }
    free(slot);
    slot = ns;
    cap = ncap;
}

static RcINode *new_node(size_t payload) {
    size_t sz = sizeof(RcINode) + payload;
    RcINode *x = malloc(sz);
    if (!x) oom();
    bytes_live += sz;
    bytes_since += sz;
    return x;
}

static inline bool same_kids(T *const *a, T *const *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}
/* A direct-mapped front cache of the nodes of at most three children looked up or made last: an
 * entry holds a node's constructor and children beside the node, so a repeated construction is found
 * without probing the table or reading the node.  Cleared whenever nodes may be freed. */
#ifndef RC_INTERN_FRONT_BITS
#define RC_INTERN_FRONT_BITS 12
#endif
typedef struct { uint16_t c; uint16_t n; uint32_t h; T *k[3]; RcINode *x; } FrontEntry;
static FrontEntry front[(size_t)1 << RC_INTERN_FRONT_BITS];
static void front_clear(void) { memset(front, 0, sizeof front); }
T *rc_intern_node(uint16_t c, uint32_t n, T *const *kids) {
    if (!active) rc_intern_begin();
    uint32_t h = hash_node(c, n, kids);
    FrontEntry *f = &front[h >> (32 - RC_INTERN_FRONT_BITS)];
    if (n <= 3 && f->x && f->h == h && f->c == c && f->n == n && same_kids(f->k, kids, n)) return RC_IVAL(f->x);
    if (2 * (count + 1) > cap) grow();
    size_t j = h & (cap - 1);
    for (; slot[j].node; j = (j + 1) & (cap - 1)) {
        RcINode *x = slot[j].node;
        if (slot[j].hash == h && x->ctor == c && x->n == n && same_kids(x->kid, kids, n)) {
            if (n <= 3) { f->c = c; f->n = (uint16_t)n; f->h = h; for (uint32_t i = 0; i < n; i++) f->k[i] = kids[i]; f->x = x; }
            return RC_IVAL(x);
        }
    }
    uint8_t ns = rc_ctor_slots[c];
    RcINode *x = new_node((n + ns) * sizeof(T *));
    x->hash = h; x->ctor = c; x->mark = 0; x->nslots = ns; x->n = n; x->pad2 = 0;
    if (n) memcpy(x->kid, kids, n * sizeof(T *));
    for (uint32_t i = 0; i < ns; i++) x->kid[n + i] = NULL;
    slot[j].hash = h; slot[j].node = x;
    count++;
    if (n <= 3) { f->c = c; f->n = (uint16_t)n; f->h = h; for (uint32_t i = 0; i < n; i++) f->k[i] = kids[i]; f->x = x; }
    return RC_IVAL(x);
}

T *rc_intern_bytes(const uint8_t *b, size_t n) {
    if (!active) rc_intern_begin();
    if (n > UINT32_MAX) rc_refinement_fail("a byte string too long for the interning store");
    uint32_t h = hash_bytes(b, n);
    if (2 * (count + 1) > cap) grow();
    size_t j = h & (cap - 1);
    for (; slot[j].node; j = (j + 1) & (cap - 1)) {
        RcINode *x = slot[j].node;
        if (slot[j].hash == h && x->ctor == RC_INTERN_BYTES && x->n == n && (n == 0 || !memcmp(x->kid, b, n))) return RC_IVAL(x);
    }
    RcINode *x = new_node((n + 7) & ~(size_t)7);
    x->hash = h; x->ctor = RC_INTERN_BYTES; x->mark = 0; x->nslots = 0; x->n = (uint32_t)n; x->pad2 = 0;
    if (n) memcpy(x->kid, b, n);
    slot[j].hash = h; slot[j].node = x;
    count++;
    return RC_IVAL(x);
}

T *rc_intern_nullary(uint16_t c) {
    if (c < nnullary && nullary[c]) return nullary[c];
    if (c >= nnullary) {
        size_t nn = (size_t)c + 64;
        T **a = realloc(nullary, nn * sizeof *a);
        if (!a) oom();
        memset(a + nnullary, 0, (nn - nnullary) * sizeof *a);
        nullary = a;
        nnullary = nn;
    }
    return nullary[c] = rc_intern_node(c, 0, NULL);
}

/* ---- collection ---- */
static RcINode **mstack;
static size_t msp, mcap;
static void mark_push(RcINode *x) {
    if (x->mark) return;
    x->mark = 1;
    if (x->ctor == RC_INTERN_BYTES || x->n == 0) return;
    if (msp == mcap) {
        mcap = mcap ? 2 * mcap : 1024;
        RcINode **s = realloc(mstack, mcap * sizeof *s);
        if (!s) oom();
        mstack = s;
    }
    mstack[msp++] = x;
}
static void mark_value(T *v, void *ctx) {
    (void)ctx;
    if (!RC_IS_INODE(v)) return;
    mark_push(RC_INODE(v));
    while (msp) {
        RcINode *x = mstack[--msp];
        for (uint32_t i = 0; i < x->n; i++)
            if (RC_IS_INODE(x->kid[i])) mark_push(RC_INODE(x->kid[i]));
    }
}

void rc_intern_on_collect(void (*fn)(void)) {
    for (int i = 0; i < non_collect; i++) if (on_collect[i] == fn) return;
    if (non_collect < 8) on_collect[non_collect++] = fn;
}

size_t (*rc_intern_held_hook)(void (*mark)(T *v));
static uint64_t epoch;
uint64_t rc_intern_epoch(void) { return epoch; }
static void mark_held(T *v) { mark_value(v, NULL); }

/* the live values are those rc_walk_live reaches and the host's held values: call after rc_major_now
 * (or with an empty heap) */
void rc_intern_collect(int nroots) {
    if (!active) return;
    front_clear();
    for (int i = 0; i < non_collect; i++) on_collect[i]();
    rc_walk_live(nroots, mark_value, NULL);
    for (size_t c = 0; c < nnullary; c++) if (nullary[c]) mark_value(nullary[c], NULL);
    if (rc_intern_held_hook) rc_intern_held_hook(mark_held);
    size_t live = 0;
    for (size_t i = 0; i < cap; i++) if (slot[i].node && slot[i].node->mark) live++;
    size_t ncap = 4096;
    while (ncap < 2 * (live + 1)) ncap *= 2;
    Slot *ns = slots_new(ncap);
    if (!ns) oom();
    for (size_t i = 0; i < cap; i++) {
        RcINode *x = slot[i].node;
        if (!x) continue;
        if (!x->mark) {
            size_t sz = sizeof(RcINode) + (x->ctor == RC_INTERN_BYTES ? ((x->n + 7) & ~(size_t)7) : (x->n + x->nslots) * sizeof(T *));
            bytes_live -= sz;
            nfreed++;
            free(x);
            continue;
        }
        x->mark = 0;
        for (uint32_t k = 0; k < x->nslots; k++) x->kid[x->n + k] = NULL;   /* the memo slots are caches */
        size_t j = x->hash & (ncap - 1);
        while (ns[j].node) j = (j + 1) & (ncap - 1);
        ns[j] = slot[i];
    }
    free(slot);
    slot = ns;
    cap = ncap;
    count = live;
    bytes_since = 0;
    ncollect++;
}

void rc_intern_safepoint(int nroots) {
    uint64_t limit = bytes_live > KEEP_BYTES ? bytes_live : KEEP_BYTES;
    if (!active || bytes_since <= limit) return;
    rc_major_now(nroots);
    rc_intern_collect(nroots);
}

/* ---- life ---- */
void rc_intern_fresh(bool f) { fresh = f; }
bool rc_intern_active(void) { return active; }
void rc_intern_free(void) {
    if (!active) return;
    front_clear();
    for (int i = 0; i < non_collect; i++) on_collect[i]();
    for (size_t i = 0; i < cap; i++) free(slot[i].node);
    free(slot);
    slot = NULL;
    cap = count = 0;
    free(nullary);
    nullary = NULL;
    nnullary = 0;
    bytes_live = bytes_since = 0;
    active = false;
    epoch++;
    rc_free_statics();
    rc_reset_constants();
}
void rc_intern_begin(void) {
    if (active && !fresh) return;
    rc_intern_free();
    active = true;
    grow();
}
void rc_intern_end(void) {
    if (!active) return;
    if (fresh) { rc_intern_free(); return; }
    if (bytes_live <= KEEP_BYTES) return;
    rc_runtime_reset();
    rc_intern_collect(0);
    if (bytes_live > KEEP_BYTES && !(rc_intern_held_hook && rc_intern_held_hook(NULL))) rc_intern_free();
}

RcInternStats rc_intern_stats(void) { return (RcInternStats){count, bytes_live, ncollect, nfreed}; }
