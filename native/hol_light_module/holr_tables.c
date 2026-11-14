/* holr_tables.c - the module's id tables (sort Table, "table" next trie) represented by arrays that
 * are updated in place, and the judgments table-get, table-put, table-set, table-next and
 * empty-table answered on them (justified blocks: the contract is the judgments' rules).
 *
 * A Table value is an immediate, version << 20 | id.  Id 0 is the empty table (no array; it never
 * changes).  Id k > 0 names array k of the current run, and only the array's current version is
 * accepted.  The rules treat tables as values; the arrays refine them when no table value is used
 * after an update made from it, since the update changes the cells that value would read.  An
 * operation on an older version stops the run as a refinement violation, never a silent difference.
 * The module's rules thread every table through the replay state and replace it at each update.
 *
 * Cells: cell[i] is the cell last stored at id i, or NULL where none was stored (it reads as
 * c-none).  The trie the rules build is fixed by the set of ids stored at least once and their
 * cells: its nodes are the union of those ids' digit paths (least significant digit first); a stored
 * id's own node holds its cell, every other node c-none.  The read-back builds exactly that trie.
 * A value in module form ("table" next trie, built by the rules) is read in the other direction
 * into a new array, so every operation accepts both forms.
 *
 * The cells are roots of the copying collector: a young collection evacuates the slots written
 * since the last collection, an old collection every slot.  The arrays are freed when the run's
 * native store is replaced.  The read-back needs naturals represented as immediates.
 */
#include "../rule_runtime/rc_rt.h"
#include "generated/hol_module_ids.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Noreturn void holr_refused(const char *what);

typedef struct {
    T **cell;
    uint64_t len, cap;       /* slots [0, len) may hold a cell; slots [len, cap) are NULL */
    uint64_t next;
    uint64_t version;
    uint64_t *dirty;         /* slots written since the last collection */
    size_t ndirty, dirty_cap;
} HolTable;

static HolTable *tabs;
static size_t ntabs, tabs_cap;

#define TAB_ID_BITS 20
#define TAB_ID_MASK ((UINT64_C(1) << TAB_ID_BITS) - 1)
#define TAB_MAX_VERSION (UINT64_C(1) << 41)   /* (version << 20 | id) stays an immediate */
#define NAT_MAX (UINT64_MAX >> 3)             /* the naturals' immediate range (nat-inc) */
#define NUL(c) ((T *)&rc_nul[(c)])

static inline T *tab_value(uint64_t id, uint64_t version) { return RC_IMM((version << TAB_ID_BITS) | id); }
static inline unsigned bitlen(uint64_t x) { return x ? 64u - (unsigned)__builtin_clzll(x) : 0u; }

void holr_tables_reset(void) {
    for (size_t i = 0; i < ntabs; i++) { free(tabs[i].cell); free(tabs[i].dirty); }
    free(tabs);
    tabs = NULL;
    ntabs = tabs_cap = 0;
}

static uint64_t tab_new(void) {
    if (ntabs + 1 > TAB_ID_MASK) holr_refused("a table id");
    if (ntabs == tabs_cap) {
        size_t cap = tabs_cap ? 2 * tabs_cap : 8;
        HolTable *n = realloc(tabs, cap * sizeof *n);
        if (!n) holr_refused("memory");
        tabs = n;
        tabs_cap = cap;
    }
    memset(&tabs[ntabs], 0, sizeof *tabs);
    return ++ntabs;
}

static void tab_store(uint64_t id, uint64_t i, T *c) {
    HolTable *t = &tabs[id - 1];
    if (i >= t->cap) {
        if (i >= NAT_MAX) holr_refused("a table id beyond the immediate range");
        uint64_t cap = t->cap ? 2 * t->cap : 64;
        if (cap <= i) cap = i + 1;
        T **n = realloc(t->cell, cap * sizeof *n);
        if (!n) holr_refused("memory");
        memset(n + t->cap, 0, (cap - t->cap) * sizeof *n);
        t->cell = n;
        t->cap = cap;
    }
    if (i >= t->len) t->len = i + 1;
    t->cell[i] = c;
    if (t->ndirty == t->dirty_cap) {
        size_t cap = t->dirty_cap ? 2 * t->dirty_cap : 64;
        uint64_t *n = realloc(t->dirty, cap * sizeof *n);
        if (!n) holr_refused("memory");
        t->dirty = n;
        t->dirty_cap = cap;
    }
    t->dirty[t->ndirty++] = i;
}

static T *tab_bump(uint64_t id) {
    HolTable *t = &tabs[id - 1];
    if (++t->version >= TAB_MAX_VERSION) holr_refused("a table version");
    return tab_value(id, t->version);
}

/* ---- module form to array: the node at path p (d digits) ---- */
static void tab_fill(uint64_t id, T *t, uint64_t p, unsigned d) {
    if (t->tag == C_hl_tleaf) return;
    if (t->tag != C_hl_tnode) holr_refused("a trie");
    if (d > 61) holr_refused("a table id beyond the immediate range");
    T *cell = t->k[0], *t0 = t->k[1], *t1 = t->k[2];
    bool none = cell->tag == C_hl_c_none;
    if (bitlen(p) == d) {
        /* p's own node: stored when it holds a cell, or when no stored id lies below it */
        if (!none || (t0->tag == C_hl_tleaf && t1->tag == C_hl_tleaf)) tab_store(id, p, cell);
    } else if (!none) {
        holr_refused("a table cell at no id");   /* a node whose path ends in digit 0 */
    }
    tab_fill(id, t0, p, d + 1);
    tab_fill(id, t1, p | (UINT64_C(1) << d), d + 1);
}

static uint64_t tab_from_module(T *x) {
    if (x->tag != C_hl_table) holr_refused("a table");
    uint64_t id = tab_new();
    tab_fill(id, x->k[1], 0, 0);
    HolTable *t = &tabs[id - 1];
    t->next = RC_IMM_VAL(x->k[0]);
    t->version = 1;
    return id;
}

/* the array of a table value (NULL for the empty table); *id_out gets its id */
static HolTable *tab_of(T *x, uint64_t *id_out) {
    if (RC_IS_HEAP(x)) {
        uint64_t id = tab_from_module(x);
        *id_out = id;
        return &tabs[id - 1];
    }
    uint64_t v = RC_IMM_VAL(x), id = v & TAB_ID_MASK, ver = v >> TAB_ID_BITS;
    *id_out = id;
    if (id == 0) return NULL;
    if (id > ntabs) holr_refused("a table of another run");
    HolTable *t = &tabs[id - 1];
    if (t->version != ver) holr_refused("a table used after its update");
    return t;
}

/* ---- array to module form ---- */
/* is a cell stored at some id n with n = p (mod 2^d) whose digit path has at least d digits? */
static bool tab_any(const HolTable *t, uint64_t p, unsigned d) {
    uint64_t step = UINT64_C(1) << d, n = p;
    if (bitlen(n) < d) n += step;   /* p itself ends above depth d; the next candidates go deeper */
    for (; n < t->len; n += step)
        if (t->cell[n]) return true;
    return false;
}
static T *tab_trie(const HolTable *t, uint64_t p, unsigned d) {
    if (d > 62 || !tab_any(t, p, d)) return NUL(C_hl_tleaf);
    T *cell = bitlen(p) == d && p < t->len && t->cell[p] ? t->cell[p] : NUL(C_hl_c_none);
    T *t0 = tab_trie(t, p, d + 1);
    T *t1 = tab_trie(t, p | (UINT64_C(1) << d), d + 1);
    return rc_mk3(C_hl_tnode, cell, t0, t1);
}

/* the view of a native table: next, and the trie of its cells */
T *holr_table_kid(T *x, int i) {
    uint64_t id;
    HolTable *t = tab_of(x, &id);
    if (i == 0) return RC_IMM(t ? t->next : 0);
    return t ? tab_trie(t, 0, 0) : NUL(C_hl_tleaf);
}
/* a table as module data */
T *holr_table_readback(T *x) {
    if (RC_IS_HEAP(x)) return x;
    return rc_mk2(C_hl_table, holr_table_kid(x, 0), holr_table_kid(x, 1));
}

/* ---- the blocks ---- */
T *holb_empty_table(void) { return tab_value(0, 0); }

T *holb_table_next(T *tb) {
    uint64_t id;
    HolTable *t = tab_of(tb, &id);
    return RC_IMM(t ? t->next : 0);
}

T *holb_table_get(T *tb, T *n) {
    uint64_t id;
    HolTable *t = tab_of(tb, &id);
    uint64_t i = RC_IMM_VAL(n);
#ifdef HOLB_FAULT_TABLE_GET
    /* planted fault: the lookup of id 5 reads id 6 */
    if (i == 5) i = 6;
#endif
    if (!t || i >= t->len || !t->cell[i]) return NUL(C_hl_c_none);
    return t->cell[i];
}

T *holb_table_put(T *tb, T *c) {
    uint64_t id;
    HolTable *t = tab_of(tb, &id);
    if (!t) id = tab_new();
    uint64_t at = tabs[id - 1].next;
    if (at >= NAT_MAX) holr_refused("nat-inc beyond the immediate range");
    tab_store(id, at, c);
    tabs[id - 1].next = at + 1;
    return tab_bump(id);
}

T *holb_table_set(T *tb, T *n, T *c) {
    uint64_t id;
    HolTable *t = tab_of(tb, &id);
    if (!t) id = tab_new();
    tab_store(id, RC_IMM_VAL(n), c);
    return tab_bump(id);
}

/* ---- the collector ---- */
void holr_tables_roots(int major, T *(*evac)(T *x)) {
    for (size_t k = 0; k < ntabs; k++) {
        HolTable *t = &tabs[k];
        if (major) {
            for (uint64_t i = 0; i < t->len; i++)
                if (t->cell[i]) t->cell[i] = evac(t->cell[i]);
        } else {
            for (size_t d = 0; d < t->ndirty; d++) {
                uint64_t i = t->dirty[d];
                t->cell[i] = evac(t->cell[i]);
            }
        }
        t->ndirty = 0;
    }
}

/* number of arrays and stored cells (a run's statistics) */
void holr_tables_stats(uint64_t *arrays, uint64_t *cells) {
    uint64_t c = 0;
    for (size_t k = 0; k < ntabs; k++) c += tabs[k].len;
    *arrays = ntabs;
    *cells = c;
}
