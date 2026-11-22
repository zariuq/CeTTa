/* tcalc_store.c - the data the calculus works on, included by tcalc_core.c at
 * its sections 3 and 4: the canonical forms of states, their runs and indexes,
 * the table of states (persistent maps of rows, tcalc_trie.c), the program's
 * pools and its view of a rule, the one store of a run by delta, and the
 * record an exploration leaves (its outcomes).  The definitions are in
 * tcalc_core.c's section headings; this file is their representation, in the
 * Cesta unit tcalc_core.cesta with the rest. */

#include "tcalc_trie.c"

/* ---- Canonical forms and the table of states (section 3) ------------------ */

typedef struct {
    const TcNode *node;
    size_t nodes;
    const TcTerm *shape;   /* per position: the row with unknowns blanked */
    const TcTerm *row;
    size_t count;
    bool shaped;
} RowOrder;

static inline int head_length_order(const TcNode *node, TcTerm a, TcTerm b) {
    const TcNode *x = &node[a], *y = &node[b];
    uint32_t xl = x->kind == TC_EXPRESSION ? x->length : 0;
    uint32_t yl = y->kind == TC_EXPRESSION ? y->length : 0;
    if (x->head != y->head) return x->head < y->head ? -1 : 1;
    if (xl != yl) return xl < yl ? -1 : 1;
    return 0;
}

static int row_compare(const RowOrder *o, uint32_t a, uint32_t b) {
    int c = head_length_order(o->node, o->row[a], o->row[b]);
    if (c) return c;
    if (o->shaped && o->shape[a] != o->shape[b]) return o->shape[a] < o->shape[b] ? -1 : 1;
    if (o->row[a] != o->row[b]) return o->row[a] < o->row[b] ? -1 : 1;
    return 0;
}

/* The order of the rows of a state with no unknowns: row_compare without shapes. */
static int ground_order(const TcNode *node, TcTerm a, TcTerm b) {
    int c = head_length_order(node, a, b);
    return c ? c : a == b ? 0 : a < b ? -1 : 1;
}

/* Sorts positions 0..n-1 by row_compare (merge sort, stable). */
static bool sort_positions(const RowOrder *o, uint32_t *pos, uint32_t n) {
    uint32_t *tmp = malloc((n ? n : 1) * sizeof *tmp);
    if (!tmp) return false;
    for (uint32_t width = 1; width < n; width *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * width) {
            uint32_t mid = lo + width < n ? lo + width : n;
            uint32_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            uint32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                tmp[k++] = row_compare(o, pos[i], pos[j]) <= 0 ? pos[i++] : pos[j++];
            while (i < mid) tmp[k++] = pos[i++];
            while (j < hi) tmp[k++] = pos[j++];
        }
        memcpy(pos, tmp, n * sizeof *pos);
    }
    free(tmp);
    return true;
}

/* The row with every unknown replaced by `blank`. */
static TcTerm blank_from(TcTerms *t, TcTerm row, TcTerm blank) {
    const TcNode *n = node_of(t, row);
    if (n->ground) return row;
    if (n->kind == TC_UNKNOWN) return blank;
    if (n->kind != TC_EXPRESSION) return row;
    TcTerm done;
    if (walk_seen(t, row, &done)) return done;
    uint32_t count = n->length, start = n->start;
    TcTerm *items = malloc((count ? count : 1) * sizeof *items);
    if (!items) return 0;
    for (uint32_t i = 0; i < count; i++)
        items[i] = blank_from(t, t->item[start + i], blank);
    TcTerm out = tc_expression(t, items, count);
    free(items);
    if (out) walk_record(t, row, out);
    return out;
}

static TcTerm blank_unknowns(TcTerms *t, TcTerm row, TcTerm blank) {
    walk_begin(t);
    return blank_from(t, row, blank);
}

/* Renumbers the variables of one kind (unknowns, or locals) in `term` by `map`
 * (old index -> new index + 1, 0 when not met yet), giving new indices in order
 * of first appearance.  Returns 0 when memory ran out.  A subterm met again has
 * all its variables numbered already, so its record is its renumbering. */
static TcTerm renumber_from(TcTerms *t, TcTerm term, TcKind kind, uint32_t *map, uint32_t *next) {
    const TcNode n = *node_of(t, term);
    if (n.ground) return term;
    if (n.kind == kind) {
        uint32_t old = (uint32_t)n.value;
        if (!map[old]) map[old] = ++*next;
        return kind == TC_UNKNOWN ? tc_unknown(t, map[old] - 1) : tc_local(t, map[old] - 1);
    }
    if (n.kind != TC_EXPRESSION) return term;
    TcTerm done;
    if (walk_seen(t, term, &done)) return done;
    uint32_t count = n.length, start = n.start;
    TcTerm *items = malloc((count ? count : 1) * sizeof *items);
    if (!items) return 0;
    for (uint32_t i = 0; i < count; i++)
        if (!(items[i] = renumber_from(t, t->item[start + i], kind, map, next))) { free(items); return 0; }
    TcTerm out = tc_expression(t, items, count);
    free(items);
    if (out) walk_record(t, term, out);
    return out;
}

static TcTerm renumber(TcTerms *t, TcTerm term, TcKind kind, uint32_t *map, uint32_t *next) {
    walk_begin(t);
    return renumber_from(t, term, kind, map, next);
}

/* One more than the largest unknown index in row (0 when it has none). */
static void max_unknown_from(TcTerms *t, TcTerm row, uint32_t *most) {
    const TcNode *n = node_of(t, row);
    if (n->ground) return;
    if (n->kind == TC_UNKNOWN) {
        if ((uint32_t)n->value + 1 > *most) *most = (uint32_t)n->value + 1;
        return;
    }
    TcTerm done;
    if (n->kind != TC_EXPRESSION || walk_seen(t, row, &done)) return;
    uint32_t count = n->length, start = n->start;
    walk_record(t, row, 0);
    for (uint32_t i = 0; i < count; i++) max_unknown_from(t, t->item[start + i], most);
}

static uint32_t max_unknown(TcTerms *t, TcTerm row) {
    uint32_t most = 0;
    walk_begin(t);
    max_unknown_from(t, row, &most);
    return most;
}

/* Puts rows[0..*n) (closed terms) in canonical form, in place; with a set store
 * equal rows collapse.  *ground: whether the rows mention no unknown; `shapes`
 * (when not null, room for *n) receives each row's shape in the same order.
 * Returns false when memory ran out. */
static bool canonical_form(TcTerms *t, TcTerm *rows, uint32_t *n, TcStore store, bool *ground,
                           TcTerm *shapes) {
    uint32_t count = *n, unknowns = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t m = max_unknown(t, rows[i]);
        if (m > unknowns) unknowns = m;
    }
    *ground = unknowns == 0;
    uint32_t *pos = malloc((count ? count : 1) * sizeof *pos);
    TcTerm *sorted = malloc((count ? count : 1) * sizeof *sorted);
    if (!pos || !sorted) { free(pos); free(sorted); return false; }
    for (uint32_t i = 0; i < count; i++) pos[i] = i;
    if (!unknowns) {
        const TcNode *nodes = t->node;
        if (!nodes) { free(pos); free(sorted); return false; }
        {
            RowOrder order = {nodes, t->nodes, rows, rows, count, false};
            if (!sort_positions(&order, pos, count)) { free(pos); free(sorted); return false; }
        }
        uint32_t out = 0;
        for (uint32_t i = 0; i < count; i++) {
            TcTerm r = rows[pos[i]];
            if (store == TC_STORE_SET && out > 0 && sorted[out - 1] == r) continue;
            sorted[out++] = r;
        }
        if (out) memcpy(rows, sorted, out * sizeof *rows);
        if (shapes && out) memcpy(shapes, sorted, out * sizeof *shapes);   /* a fixed row is its shape */
        *n = out;
        free(pos); free(sorted);
        return true;
    }
    {
        TcTerm *shape = malloc(count * sizeof *shape);
        uint32_t *map = calloc(unknowns, sizeof *map);
        TcTerm blank = tc_symbol(t, "?", 1);
        if (!shape || !map || !blank) {
            free(pos); free(shape); free(sorted); free(map);
            return false;
        }
        bool ok = true;
        for (uint32_t i = 0; ok && i < count; i++) ok = (shape[i] = blank_unknowns(t, rows[i], blank)) != 0;
        if (ok) {
            const TcNode *nodes = t->node;
            if (!nodes) ok = false;
            else {
                RowOrder order = {nodes, t->nodes, shape, rows, count, true};
                ok = sort_positions(&order, pos, count);
            }
        }
        uint32_t next = 0;
        for (uint32_t i = 0; ok && i < count; i++)
            ok = (sorted[i] = renumber(t, rows[pos[i]], TC_UNKNOWN, map, &next)) != 0;
        if (ok) {   /* the renamed rows in that order, and their shapes with them */
            for (uint32_t i = 0; i < count; i++) rows[i] = sorted[i];
            for (uint32_t i = 0; i < count; i++) sorted[i] = shape[pos[i]];
            for (uint32_t i = 0; i < count; i++) shape[i] = sorted[i];
            for (uint32_t i = 0; i < count; i++) pos[i] = i;
            const TcNode *nodes = t->node;
            if (!nodes) ok = false;
            else {
                RowOrder order = {nodes, t->nodes, shape, rows, count, true};
                ok = sort_positions(&order, pos, count);
            }
        }
        free(map);
        if (!ok) { free(pos); free(shape); free(sorted); return false; }
        uint32_t out = 0;
        for (uint32_t i = 0; i < count; i++) {
            TcTerm r = rows[pos[i]];
            if (store == TC_STORE_SET && out > 0 && sorted[out - 1] == r) continue;
            if (shapes) shapes[out] = shape[pos[i]];
            sorted[out++] = r;
        }
        if (out) memcpy(rows, sorted, out * sizeof *rows);
        *n = out;
        free(pos); free(shape); free(sorted);
    }
    return true;
}

/* ---- Runs, indexes and key nodes ----------------------------------------------
 * A run is the rows of one head and length, in the order of head_length_order
 * (a row with no symbol at its head has head 0; a row that is no expression,
 * length 0).  Within a run the canonical order of rows of fixed terms is the
 * order of their terms, so a run of a map keyed by (run, row) is read in
 * canonical order, and every run in the order of (head, length) is every row
 * in it.  Runs are numbered as an exploration meets them; their order is kept
 * beside.
 *
 * An index is one of the plans' (tcalc_plan.c): it reads one run by the fixed
 * terms at a chain of paths into its rows (path_key), and serves each search
 * whose paths are a prefix of the chain, at that prefix's level.  Its levels
 * are the prefixes some search reads.  Its keys are a tree of key nodes: its
 * root, and below a node of one level, a node for each key a row holds at the
 * paths the next level adds (one term, or the tuple of the terms when it adds
 * several), so that a node names the rows whose keys up to its level are its
 * keys.  A store keeps under each key node the rows below it, and a search
 * reads the rows under the node of its keys.  A row holding no term at a path
 * is keyed 0 there: no probe asks for key 0, which no pattern meets.  An index
 * whose searches all hold one constant at its first path keeps only the rows
 * holding it there (its split: the run cut at that path by that key).  Key
 * nodes, tuples and splits are numbered as rows reach them, once for every
 * store of an exploration; indexes are met when a probe first asks for them,
 * and each store keeps its own rows of them (section 3, the table of states;
 * tcalc_delta.c, the one store). */
enum { INDEX_PATHS = 8 };   /* the most paths an index orders */

typedef struct {
    uint32_t *run; size_t runs, run_cap;          /* 4 words a run: head, length, its first
                                                     index of every row + 1, its first cut
                                                     + 1                                      */
    uint32_t *run_slot; size_t run_slots;         /* hash: run + 1, 0 empty                    */
    uint32_t *order; size_t orders, order_cap;    /* the runs, in order of (head, length)      */
    uint32_t *index; size_t indexes, index_cap;   /* 3 words per index of the plans: its run + 1
                                                     (0: not met yet), the next index of its
                                                     run's (of every row) or of its split's
                                                     + 1, its root node                       */
    uint32_t *node; size_t nodes, node_cap;       /* 2 words a key node: its parent, its key;
                                                     node 0 is none                           */
    uint32_t *node_slot; size_t node_slots;       /* hash: node, 0 empty                       */
    uint32_t *tuple; size_t tuples, tuple_cap;    /* key tuples: their keys n, then n keys;
                                                     a tuple is its offset + 1                */
    uint32_t *tuple_slot; size_t tuple_slots;     /* hash: tuple, 0 empty                      */
    size_t tuples_held;
    uint32_t *cut; size_t cuts, cut_cap;          /* 2 words a cut: a path a run's indexes split
                                                     at, the run's next cut + 1                */
    uint32_t *split; size_t splits, split_cap;    /* 3 words a split: its cut, the key there,
                                                     its first index + 1                      */
    uint32_t *split_slot; size_t split_slots;     /* hash: split + 1, 0 empty                  */
    uint32_t met;                                 /* the indexes met: those the stores keep    */
} Places;

static size_t place_hash(uint64_t a, uint64_t b) { return hash_mix(hash_mix(0x811c9dc5u, a), b); }

static inline uint32_t run_head(const Places *p, uint32_t run) { return p->run[4 * (size_t)run]; }
static inline uint32_t run_length(const Places *p, uint32_t run) { return p->run[4 * (size_t)run + 1]; }
/* The first index of a run that keeps every row + 1 (0: none), and the next
 * index of the list an index is in (its run's, or its split's) + 1. */
static inline uint32_t run_indexes(const Places *p, uint32_t run) { return p->run[4 * (size_t)run + 2]; }
static inline uint32_t index_next(const Places *p, uint32_t index) { return p->index[3 * (size_t)index + 1]; }
/* A run's first cut + 1 (0: none); a cut's path and next cut + 1; a split's
 * first index + 1. */
static inline uint32_t run_cuts(const Places *p, uint32_t run) { return p->run[4 * (size_t)run + 3]; }
static inline uint32_t cut_path(const Places *p, uint32_t cut) { return p->cut[2 * (size_t)cut]; }
static inline uint32_t cut_next(const Places *p, uint32_t cut) { return p->cut[2 * (size_t)cut + 1]; }
static inline uint32_t split_indexes(const Places *p, uint32_t split) { return p->split[3 * (size_t)split + 2]; }
/* An index's run (UINT32_MAX: not met yet) and its root node. */
static inline uint32_t index_run(const Places *p, uint32_t index) {
    return 3 * (size_t)index < p->indexes ? p->index[3 * (size_t)index] - 1 : UINT32_MAX;
}
static inline uint32_t index_root(const Places *p, uint32_t index) { return p->index[3 * (size_t)index + 2]; }
static inline uint32_t node_parent(const Places *p, uint32_t node) { return p->node[2 * (size_t)node]; }
static inline uint32_t node_key(const Places *p, uint32_t node) { return p->node[2 * (size_t)node + 1]; }

/* The run of (head, length), UINT32_MAX when there is none. */
static uint32_t place_find(const Places *p, uint32_t head, uint32_t length) {
    size_t mask = p->run_slots ? p->run_slots - 1 : 0;
    for (size_t h = p->run_slots ? place_hash(head, length) & mask : 0; p->run_slots && p->run_slot[h];
         h = (h + 1) & mask) {
        uint32_t r = p->run_slot[h] - 1;
        if (run_head(p, r) == head && run_length(p, r) == length) return r;
    }
    return UINT32_MAX;
}

/* The run of (head, length); with `make`, made when there is none.  UINT32_MAX
 * when there is none, or memory ran out. */
static uint32_t place_run(Places *p, uint32_t head, uint32_t length, bool make) {
    uint32_t found = place_find(p, head, length);
    if (found != UINT32_MAX || !make) return found;
    uint32_t r = (uint32_t)(p->runs / 4);
    if ((r + 1) * 2 > p->run_slots) {   /* the hash rebuilt, twice the size */
        size_t slots = p->run_slots ? p->run_slots * 2 : 64;
        uint32_t *slot = calloc(slots, sizeof *slot);
        if (!slot) return UINT32_MAX;
        for (uint32_t k = 0; k < r; k++) {
            size_t h = place_hash(run_head(p, k), run_length(p, k)) & (slots - 1);
            while (slot[h]) h = (h + 1) & (slots - 1);
            slot[h] = k + 1;
        }
        free(p->run_slot);
        p->run_slot = slot;
        p->run_slots = slots;
    }
    uint32_t words[4] = {head, length, 0, 0};
    for (int k = 0; k < 4; k++)
        if (!tc_push_u32(realloc, &p->run, &p->runs, &p->run_cap, words[k])) return UINT32_MAX;
    if (!tc_push_u32(realloc, &p->order, &p->orders, &p->order_cap, r)) return UINT32_MAX;
    size_t at = p->orders - 1;   /* into the order of (head, length) */
    while (at > 0 && (run_head(p, p->order[at - 1]) > head ||
                      (run_head(p, p->order[at - 1]) == head && run_length(p, p->order[at - 1]) > length))) {
        p->order[at] = p->order[at - 1];
        at--;
    }
    p->order[at] = r;
    size_t h = place_hash(head, length) & (p->run_slots - 1);
    while (p->run_slot[h]) h = (h + 1) & (p->run_slots - 1);
    p->run_slot[h] = r + 1;
    return r;
}

/* The head and length a row's run is keyed by. */
static void row_place(const TcTerms *t, TcTerm row, uint32_t *head, uint32_t *length) {
    TcNode n = term_node(t, row);
    *head = n.head;
    *length = n.kind == TC_EXPRESSION ? n.length : 0;
}

static uint32_t row_run(Places *p, const TcTerms *t, TcTerm row, bool make) {
    uint32_t head = 0, length = 0;
    row_place(t, row, &head, &length);
    return place_run(p, head, length, make);
}

/* Whether the row is of the run, or (with `before`) of a run before it in the
 * order of (head, length). */
static bool row_in_run(const Places *p, const TcTerms *t, TcTerm row, uint32_t run, bool before) {
    uint32_t head = 0, length = 0;
    row_place(t, row, &head, &length);
    if (!before) return head == run_head(p, run) && length == run_length(p, run);
    return head < run_head(p, run) || (head == run_head(p, run) && length < run_length(p, run));
}

/* The key node of `key` below `parent` (parent 0: the root of index key - 1);
 * with `make`, made when there is none.  0 when there is none, or memory ran
 * out. */
static uint32_t key_node(Places *p, uint32_t parent, TcTerm key, bool make) {
    size_t mask = p->node_slots ? p->node_slots - 1 : 0;
    for (size_t h = p->node_slots ? place_hash(parent, key) & mask : 0; p->node_slots && p->node_slot[h];
         h = (h + 1) & mask) {
        uint32_t n = p->node_slot[h];
        if (node_parent(p, n) == parent && node_key(p, n) == key) return n;
    }
    if (!make) return 0;
    if (!p->nodes && (!tc_push_u32(realloc, &p->node, &p->nodes, &p->node_cap, 0) ||
                      !tc_push_u32(realloc, &p->node, &p->nodes, &p->node_cap, 0)))
        return 0;
    uint32_t n = (uint32_t)(p->nodes / 2);
    if ((size_t)(n + 1) * 2 > p->node_slots) {   /* the hash rebuilt, twice the size */
        size_t slots = p->node_slots ? p->node_slots * 2 : 1024;
        uint32_t *slot = calloc(slots, sizeof *slot);
        if (!slot) return 0;
        for (uint32_t k = 1; k < n; k++) {
            size_t h = place_hash(node_parent(p, k), node_key(p, k)) & (slots - 1);
            while (slot[h]) h = (h + 1) & (slots - 1);
            slot[h] = k;
        }
        free(p->node_slot);
        p->node_slot = slot;
        p->node_slots = slots;
    }
    if (!tc_push_u32(realloc, &p->node, &p->nodes, &p->node_cap, parent) ||
        !tc_push_u32(realloc, &p->node, &p->nodes, &p->node_cap, key))
        return 0;
    size_t h = place_hash(parent, key) & (p->node_slots - 1);
    while (p->node_slot[h]) h = (h + 1) & (p->node_slots - 1);
    p->node_slot[h] = n;
    return n;
}

static inline uint32_t tuple_word(const Places *p, size_t at) { return p->tuple[at]; }

static size_t tuple_hash(const TcTerm *keys, uint32_t n) {
    size_t h = place_hash(n, 0);
    for (uint32_t i = 0; i < n; i++) h = place_hash(h, keys[i]);
    return h;
}

/* The hash of the tuple held at offset `at`. */
static size_t tuple_hash_at(const Places *p, size_t at) {
    uint32_t n = tuple_word(p, at);
    size_t h = place_hash(n, 0);
    for (uint32_t i = 0; i < n; i++) h = place_hash(h, tuple_word(p, at + 1 + i));
    return h;
}

/* The tuple of keys[0..n) (n >= 2); with `make`, made when there is none.  0
 * when there is none, or memory ran out. */
static uint32_t key_tuple(Places *p, const TcTerm *keys, uint32_t n, bool make) {
    size_t mask = p->tuple_slots ? p->tuple_slots - 1 : 0;
    for (size_t h = p->tuple_slots ? tuple_hash(keys, n) & mask : 0; p->tuple_slots && p->tuple_slot[h];
         h = (h + 1) & mask) {
        size_t at = p->tuple_slot[h] - 1;
        uint32_t i = 0;
        if (tuple_word(p, at) != n) continue;
        while (i < n && tuple_word(p, at + 1 + i) == keys[i]) i++;
        if (i == n) return p->tuple_slot[h];
    }
    if (!make) return 0;
    if ((p->tuples_held + 1) * 2 > p->tuple_slots) {   /* the hash rebuilt, twice the size */
        size_t slots = p->tuple_slots ? p->tuple_slots * 2 : 1024;
        uint32_t *slot = calloc(slots, sizeof *slot);
        if (!slot) return 0;
        for (size_t k = 0; k < p->tuple_slots; k++) {
            uint32_t tuple = p->tuple_slot[k];
            if (!tuple) continue;
            size_t h = tuple_hash_at(p, tuple - 1) & (slots - 1);
            while (slot[h]) h = (h + 1) & (slots - 1);
            slot[h] = tuple;
        }
        free(p->tuple_slot);
        p->tuple_slot = slot;
        p->tuple_slots = slots;
    }
    uint32_t tuple = (uint32_t)p->tuples + 1;
    if (!tc_push_u32(realloc, &p->tuple, &p->tuples, &p->tuple_cap, n)) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (!tc_push_u32(realloc, &p->tuple, &p->tuples, &p->tuple_cap, keys[i])) return 0;
    size_t h = tuple_hash(keys, n) & (p->tuple_slots - 1);
    while (p->tuple_slot[h]) h = (h + 1) & (p->tuple_slots - 1);
    p->tuple_slot[h] = tuple;
    p->tuples_held++;
    return tuple;
}

/* The cut of `run` at `path` + 1; with `make`, made when there is none (and
 * *made then true).  0 when there is none, or memory ran out. */
static uint32_t place_cut(Places *p, uint32_t run, uint32_t path, bool make, bool *made) {
    *made = false;
    for (uint32_t c = run_cuts(p, run); c; c = cut_next(p, c - 1))
        if (cut_path(p, c - 1) == path) return c;
    if (!make) return 0;
    uint32_t c = (uint32_t)(p->cuts / 2);
    if (!tc_push_u32(realloc, &p->cut, &p->cuts, &p->cut_cap, path) ||
        !tc_push_u32(realloc, &p->cut, &p->cuts, &p->cut_cap, run_cuts(p, run)))
        return 0;
    p->run[4 * (size_t)run + 3] = c + 1;
    *made = true;
    return c + 1;
}

/* The split of cut `cut` (+ 1) by `key` + 1: the rows holding the key at the
 * cut's path; with `make`, made when there is none.  0 when there is none, or
 * memory ran out. */
static uint32_t place_split(Places *p, uint32_t cut, TcTerm key, bool make) {
    size_t mask = p->split_slots ? p->split_slots - 1 : 0;
    for (size_t h = p->split_slots ? place_hash(cut, key) & mask : 0; p->split_slots && p->split_slot[h];
         h = (h + 1) & mask) {
        uint32_t s = p->split_slot[h] - 1;
        if (p->split[3 * (size_t)s] == cut && p->split[3 * (size_t)s + 1] == key) return s + 1;
    }
    if (!make) return 0;
    uint32_t s = (uint32_t)(p->splits / 3);
    if ((size_t)(s + 1) * 2 > p->split_slots) {   /* the hash rebuilt, twice the size */
        size_t slots = p->split_slots ? p->split_slots * 2 : 64;
        uint32_t *slot = calloc(slots, sizeof *slot);
        if (!slot) return 0;
        for (uint32_t k = 0; k < s; k++) {
            size_t h = place_hash(p->split[3 * (size_t)k], p->split[3 * (size_t)k + 1]) & (slots - 1);
            while (slot[h]) h = (h + 1) & (slots - 1);
            slot[h] = k + 1;
        }
        free(p->split_slot);
        p->split_slot = slot;
        p->split_slots = slots;
    }
    uint32_t words[3] = {cut, key, 0};
    for (int k = 0; k < 3; k++)
        if (!tc_push_u32(realloc, &p->split, &p->splits, &p->split_cap, words[k])) return 0;
    size_t h = place_hash(cut, key) & (p->split_slots - 1);
    while (p->split_slot[h]) h = (h + 1) & (p->split_slots - 1);
    p->split_slot[h] = s + 1;
    return s + 1;
}

/* Meets index `index` of the plans, of the run (head, length), splitting it
 * at `path` by `key` when it keeps only the rows holding key there (path 0:
 * it keeps every row): its run and root, made when it is first met, and the
 * index listed with its run's indexes of every row or with its split's.
 * *cut, the cut it splits at + 1 when that cut is new.  False when memory ran
 * out. */
static bool place_index(Places *p, uint32_t index, uint32_t head, uint32_t length, uint32_t path, TcTerm key,
                        uint32_t *cut) {
    *cut = 0;
    while (p->indexes < 3 * ((size_t)index + 1))
        if (!tc_push_u32(realloc, &p->index, &p->indexes, &p->index_cap, 0)) return false;
    if (p->index[3 * (size_t)index]) return true;
    uint32_t run = place_run(p, head, length, true), root = key_node(p, 0, index + 1u, true);
    if (run == UINT32_MAX || !root) return false;
    p->index[3 * (size_t)index] = run + 1;
    p->index[3 * (size_t)index + 2] = root;
    if (!path) {
        p->index[3 * (size_t)index + 1] = run_indexes(p, run);
        p->run[4 * (size_t)run + 2] = index + 1;
    } else {
        bool made = false;
        uint32_t c = place_cut(p, run, path, true, &made), s = c ? place_split(p, c, key, true) : 0;
        if (!s) return false;
        p->index[3 * (size_t)index + 1] = split_indexes(p, s - 1);
        p->split[3 * (size_t)(s - 1) + 2] = index + 1;
        if (made) *cut = c;
    }
    p->met++;
    return true;
}

static void places_free(Places *p) {
    free(p->run); free(p->run_slot); free(p->order); free(p->index); free(p->node); free(p->node_slot);
    free(p->tuple); free(p->tuple_slot); free(p->cut); free(p->split); free(p->split_slot);
}

/* The fixed term of a row at a path, false when the row has none there (its
 * argument is no expression with that item).  A path is an argument, an item
 * of an argument, or an item of that item, packed as position | (item + 1)
 * << 10 | (item + 1) << 20; path 0 is the run itself, its key 0. */
static bool path_key(const TcTerms *t, TcTerm row, uint32_t path, TcTerm *key) {
    *key = 0;
    if (!path) return true;
    uint32_t at = path & 1023u, i = (path >> 10) & 1023u, j = path >> 20;
    TcNode rn = term_node(t, row);
    if (rn.kind != TC_EXPRESSION || rn.length <= at) return false;
    TcTerm a = term_item(t, rn.start + at);
    if (i) {
        TcNode an = term_node(t, a);
        if (an.kind != TC_EXPRESSION || an.length < i) return false;
        a = term_item(t, an.start + i - 1);
        if (j) {
            TcNode bn = term_node(t, a);
            if (bn.kind != TC_EXPRESSION || bn.length < j) return false;
            a = term_item(t, bn.start + j - 1);
        }
    }
    *key = a;
    return true;
}

/* ---- Maps of rows ----------------------------------------------------------------
 * A state's rows of fixed terms are a map keyed by (run, row); the value of a
 * row is its copies (a state is a bag: Core.lean's State, the copies of a term
 * counted), and the entry of row 0 under a run counts the copies in it.  An
 * index's rows are a map too: each row under each of its key nodes, with its
 * copies, so that the rows under a node are one range, in canonical order. */
static inline uint64_t map_key(uint32_t high, TcTerm row) { return (uint64_t)high << 32 | row; }

/* Adds `value` to the entry of `row` under `high` in the map *root, and
 * `copies` to the count under `high`.  False when memory ran out. */
static bool map_change(Trie *tr, uint32_t *root, uint32_t high, TcTerm row, uint32_t value, uint32_t copies) {
    (void)trie_add(tr, root, map_key(high, row), value);
    (void)trie_add(tr, root, map_key(high, 0), copies);
    return !tr->failed;
}

/* An index as a store keeps it: its paths; its levels, the l whose first l
 * keys some probe reads it by (bit l - 1; the last, its paths, always one);
 * and its domain, the keys at its first path of the rows it keeps, sorted
 * (none: every row of its run; the plans say why, tcalc_plan.c). */
typedef struct {
    const uint32_t *path;
    uint32_t paths, levels;
    const uint32_t *domain;
    uint32_t domains;
} IndexShape;

static inline uint32_t shape_path(const IndexShape *shape, uint32_t l) { return shape->path[l]; }

/* Whether level l of the index is one a probe reads. */
static inline bool shape_level(const IndexShape *shape, uint32_t l) { return shape->levels >> (l - 1) & 1u; }

/* The level kept at or below `level` (0: none, the run itself). */
static inline uint32_t shape_kept(const IndexShape *shape, uint32_t level) {
    while (level && !shape_level(shape, level)) level--;
    return level;
}

/* The levels kept above `level`. */
static inline uint32_t shape_below(const IndexShape *shape, uint32_t level) {
    uint32_t n = 0;
    for (uint32_t l = level + 1; l <= shape->paths; l++) n += shape_level(shape, l) ? 1u : 0u;
    return n;
}

static inline uint32_t domain_at(const IndexShape *shape, uint32_t i) { return shape->domain[i]; }

/* Whether the index keeps the row: it holds a key of the domain at the first
 * path, or the index keeps every row. */
static bool index_keeps(const TcTerms *t, const IndexShape *shape, TcTerm row) {
    if (!shape->domains) return true;
    TcTerm key = 0;
    if (!path_key(t, row, shape_path(shape, 0), &key)) return false;
    uint32_t lo = 0, hi = shape->domains;
    while (lo < hi) {   /* the domain is sorted */
        uint32_t mid = lo + (hi - lo) / 2, at = domain_at(shape, mid);
        if (at == key) return true;
        if (at < key) lo = mid + 1; else hi = mid;
    }
    return false;
}

/* The key of a kept level: the keys keys[0..n) of the paths it adds, one
 * term, or the tuple of several (made when new, with `make`); 0 when it has
 * none, or memory ran out. */
static TcTerm level_key(Places *p, const TcTerm *keys, uint32_t n, bool make) {
    return n == 1 ? keys[0] : key_tuple(p, keys, n, make);
}

/* The key nodes of a row in an index of root `root`: node[j] the node of its
 * keys at the j + 1 first kept levels, each below the one before, keyed by
 * the keys of the paths its level adds; made when new.  False when memory
 * ran out. */
static bool row_nodes(Places *p, const TcTerms *t, uint32_t root, const IndexShape *shape, TcTerm row,
                      uint32_t *node) {
    TcTerm keys[INDEX_PATHS] = {0};
    uint32_t at = root, from = 0, kept = 0;
    for (uint32_t l = 1; l <= shape->paths; l++) {
        TcTerm key = 0;
        if (!path_key(t, row, shape_path(shape, l - 1), &key)) key = 0;
        keys[l - 1 - from] = key;
        if (!shape_level(shape, l)) continue;
        TcTerm step = level_key(p, keys, l - from, true);
        if ((l - from > 1 && !step) || !(at = key_node(p, at, step, true))) return false;
        node[kept++] = at;
        from = l;
    }
    return true;
}

/* The key node of the keys key[0..level) (level kept) of an index, below its
 * root `root`; 0 when no row has them. */
static uint32_t keys_node(Places *p, uint32_t root, const IndexShape *shape, const TcTerm *key, uint32_t level) {
    uint32_t at = root, from = 0;
    for (uint32_t l = 1; at && l <= level; l++) {
        if (!shape_level(shape, l)) continue;
        TcTerm step = l - from == 1 ? key[from] : key_tuple(p, key + from, l - from, false);
        at = l - from > 1 && !step ? 0 : key_node(p, at, step, false);
        from = l;
    }
    return at;
}

/* A row of fixed terms arrives at an index's map with `copies` (UINT32_MAX:
 * a copy leaves), under each of its key nodes node[0..k).  False when memory
 * ran out. */
static bool index_change(Trie *tr, uint32_t *map, const IndexShape *shape, const uint32_t *node, TcTerm row,
                         uint32_t copies) {
    for (uint32_t j = shape_below(shape, 0); j > 0; j--) (void)trie_add(tr, map, map_key(node[j - 1], row), copies);
    return !tr->failed;
}

/* An index made from the rows of its run in a map of rows: those it keeps.
 * UINT32_MAX when memory ran out. */
static uint32_t index_build(Trie *tr, Places *p, const TcTerms *t, uint32_t rows, uint32_t index,
                            const IndexShape *shape) {
    uint32_t run = index_run(p, index), root = index_root(p, index), map = 0, node[INDEX_PATHS] = {0};
    TrieCursor c;
    trie_seek(tr, &c, rows, map_key(run, 1), map_key(run, UINT32_MAX));
    uint64_t key = 0;
    uint32_t value = 0;
    while (trie_next(tr, &c, &key, &value))
        if (index_keeps(t, shape, (TcTerm)key) &&
            (!row_nodes(p, t, root, shape, (TcTerm)key, node) ||
             !index_change(tr, &map, shape, node, (TcTerm)key, value))) {
            trie_release(tr, map);
            return UINT32_MAX;
        }
    return map;
}

/* ---- The table of states -------------------------------------------------------
 * A state is its rows of fixed terms, a map shared with the states it was
 * made from and to (path copying), and its rows that mention unknowns, kept
 * apart in canonical form: the canonical names of its unknowns are given by
 * those rows alone (a fixed row names none), so the state's canonical form is
 * the two together, and an assignment changes only rows of the second kind.
 *
 * A state's identity is decided by an incremental multiset hash (Clarke et
 * al. 2003: the sum of 64-bit row hashes, a copy counted each time), kept as
 * rows leave and arrive, combined with a hash of its rows with unknowns; equal
 * hashes are confirmed by equal rows, the maps compared node by node.  Each
 * state records the state it was made from and the rows of fixed terms that
 * changed, so that an index it asks for is that state's index changed by
 * those rows, not made again. */
typedef struct {
    TcState *state; size_t states, state_cap;
    uint32_t *slot; size_t slots;                   /* hash: state + 1, 0 empty                 */
    TcTerm *loose; size_t looses, loose_cap;        /* every state's rows with unknowns         */
    TcTerm *shape; size_t shapes, shape_cap;        /* each such row's shape (canonical order) */
    uint32_t *diff; size_t diffs, diff_cap;         /* 2 words a change: a row of fixed terms,
                                                       the copies added (two's complement)      */
    uint32_t *index; size_t index_slots, index_used;/* 3 words a slot: state + 1 (0 empty),
                                                       index, its map                           */
    uint32_t *chain; size_t chains, chain_cap;      /* scratch: states an index is made for     */
    uint64_t compared;                              /* rows with unknowns hashed and compared
                                                       deciding identity (work counted)         */
} StateTable;

static inline TcState state_at(const StateTable *st, uint32_t q) { return st->state[q]; }

/* A row's hash: a 64-bit mix of its term (terms are hash-consed: one term,
 * one id). */
static uint64_t row_hash(TcTerm row) {
    uint64_t z = row + UINT64_C(0x9E3779B97F4A7C15);
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/* The identity hash of a state: the sum over its rows of fixed terms, and its
 * rows with unknowns in their canonical order. */
static uint64_t state_hash(uint64_t sum, const TcTerm *loose, uint32_t looses) {
    uint64_t h = UINT64_C(1469598103934665603) ^ looses;
    for (uint32_t i = 0; i < looses; i++) { h ^= loose[i]; h *= UINT64_C(1099511628211); }
    return sum + row_hash((TcTerm)(h ^ (h >> 32)));
}

static bool states_grow(StateTable *st) {
    size_t slots = st->slots ? st->slots * 2 : 1024;
    uint32_t *slot = calloc(slots, sizeof *slot);
    if (!slot) return false;
    for (size_t k = 0; k < st->states; k++) {
        size_t i = st->state[k].hash & (slots - 1);
        while (slot[i]) i = (i + 1) & (slots - 1);
        slot[i] = (uint32_t)k + 1;
    }
    free(st->slot);
    st->slot = slot;
    st->slots = slots;
    return true;
}

/* The state `s` (its rows map held by the caller, its rows with unknowns
 * `loose`, their shapes, and its changes from its parent `diff`, outside the
 * table) entered in the table: the state equal to it when there is one (s's
 * hold on its map is then given up), else s, added.  UINT32_MAX when memory
 * ran out. */
static uint32_t state_enter(StateTable *st, Trie *tr, TcState s, const TcTerm *loose, const TcTerm *shape,
                            const uint32_t *diff, uint32_t diffs) {
    s.hash = state_hash(s.sum, loose, s.looses);
    st->compared += s.looses;
    if ((st->states + 1) * 2 > st->slots && !states_grow(st)) { trie_release(tr, s.rows); return UINT32_MAX; }
    size_t i = s.hash & (st->slots - 1);
    for (; st->slot[i]; i = (i + 1) & (st->slots - 1)) {
        TcState o = state_at(st, st->slot[i] - 1);
        if (o.hash != s.hash || o.size != s.size || o.looses != s.looses) continue;
        bool same = true;
        st->compared += s.looses;
        for (uint32_t k = 0; same && k < s.looses; k++) same = st->loose[o.loose + k] == loose[k];
        if (same && trie_equal(tr, o.rows, s.rows)) {
            trie_release(tr, s.rows);
            return st->slot[i] - 1;
        }
    }
    s.loose = (uint32_t)st->looses;
    s.diff = (uint32_t)st->diffs;
    s.diffs = diffs;
    for (uint32_t k = 0; k < s.looses; k++)
        if (!tc_push_u32(realloc, &st->loose, &st->looses, &st->loose_cap, loose[k]) ||
            !tc_push_u32(realloc, &st->shape, &st->shapes, &st->shape_cap, shape[k])) {
            trie_release(tr, s.rows);
            return UINT32_MAX;
        }
    for (uint32_t k = 0; k < 2 * diffs; k++)
        if (!tc_push_u32(realloc, &st->diff, &st->diffs, &st->diff_cap, diff[k])) {
            trie_release(tr, s.rows);
            return UINT32_MAX;
        }
    if (!tc_push_state(realloc, &st->state, &st->states, &st->state_cap, s)) {
        trie_release(tr, s.rows);
        return UINT32_MAX;
    }
    st->slot[i] = (uint32_t)st->states;
    return (uint32_t)st->states - 1;
}

/* The slot of (state, index) in the table of indexes made; with `make`, made
 * when it has none.  SIZE_MAX when it has none, or memory ran out. */
static size_t state_index_slot(StateTable *st, uint32_t q, uint32_t index, bool make) {
    if (make && (st->index_used + 1) * 2 > st->index_slots / 3) {
        size_t n = st->index_slots / 3, cap = n ? n * 2 : 256;
        uint32_t *grown = calloc(3 * cap, sizeof *grown);
        if (!grown) return SIZE_MAX;
        for (size_t k = 0; k < n; k++) {
            if (!st->index[3 * k]) continue;
            size_t h = place_hash(st->index[3 * k], st->index[3 * k + 1]) & (cap - 1);
            while (grown[3 * h]) h = (h + 1) & (cap - 1);
            for (size_t w = 0; w < 3; w++) grown[3 * h + w] = st->index[3 * k + w];
        }
        free(st->index);
        st->index = grown;
        st->index_slots = 3 * cap;
    }
    if (!st->index_slots) return SIZE_MAX;
    size_t mask = st->index_slots / 3 - 1, h = place_hash(q + 1u, index) & mask;
    for (; st->index[3 * h]; h = (h + 1) & mask)
        if (st->index[3 * h] == q + 1u && st->index[3 * h + 1] == index) return 3 * h;
    if (!make) return SIZE_MAX;
    st->index[3 * h] = q + 1u;
    st->index[3 * h + 1] = index;
    st->index[3 * h + 2] = 0;
    st->index_used++;
    return 3 * h;
}

/* The map of `index` (of that shape) at state q: made when first asked, as
 * the index of the state q was made from (made in turn when it has none),
 * changed by q's own changes; a state made from no other (the first, or one
 * entered from its rows) makes it from its rows.  False when memory ran
 * out. */
static bool state_index(StateTable *st, Trie *tr, Places *p, const TcTerms *t, uint32_t q, uint32_t index,
                        const IndexShape *shape, uint32_t *map) {
    size_t at = state_index_slot(st, q, index, false);
    if (at != SIZE_MAX) { *map = st->index[at + 2]; return true; }
    st->chains = 0;
    uint32_t from = q;   /* the nearest state with the index, or the first of the line */
    for (;;) {
        if (!tc_push_u32(realloc, &st->chain, &st->chains, &st->chain_cap, from)) return false;
        uint32_t parent = state_at(st, from).parent;
        if (parent == UINT32_MAX) break;
        if (state_index_slot(st, parent, index, false) != SIZE_MAX) { from = parent; break; }
        from = parent;
    }
    size_t k = st->chains;
    uint32_t built = 0;
    if ((at = state_index_slot(st, from, index, false)) != SIZE_MAX) built = st->index[at + 2];
    else {
        built = index_build(tr, p, t, state_at(st, from).rows, index, shape);
        if (built == UINT32_MAX || (at = state_index_slot(st, from, index, true)) == SIZE_MAX) {
            if (built != UINT32_MAX) trie_release(tr, built);
            return false;
        }
        st->index[at + 2] = built;
        k--;
    }
    uint32_t run = index_run(p, index), root = index_root(p, index), node[INDEX_PATHS] = {0};
    while (k-- > 0) {   /* down the line to q: each state's index, its parent's changed */
        uint32_t s = st->chain[k];
        TcState rec = state_at(st, s);
        uint32_t d = built;
        trie_retain(tr, d);
        for (uint32_t c = 0; c < rec.diffs; c++) {
            TcTerm row = st->diff[(size_t)rec.diff + 2 * (size_t)c];
            uint32_t copies = st->diff[(size_t)rec.diff + 2 * (size_t)c + 1];
            if (row_in_run(p, t, row, run, false) && index_keeps(t, shape, row) &&
                (!row_nodes(p, t, root, shape, row, node) || !index_change(tr, &d, shape, node, row, copies))) {
                trie_release(tr, d);
                return false;
            }
        }
        if ((at = state_index_slot(st, s, index, true)) == SIZE_MAX) { trie_release(tr, d); return false; }
        st->index[at + 2] = d;
        built = d;
    }
    *map = built;
    return true;
}

static void states_free(StateTable *st) {
    free(st->state); free(st->slot); free(st->loose); free(st->shape); free(st->diff); free(st->index);
    free(st->chain);
}

/* The canonical order of a row of fixed terms g and a row with unknowns u of
 * one run: by shape (g is its own), then by term.  Whether u is first. */
static inline bool loose_first(TcTerm u, TcTerm shape, TcTerm g) { return shape < g || (shape == g && u < g); }

/* Appends the rows of a state, each copy, in canonical order, to the list
 * (*out, *n, *cap): its rows map read run by run, merged with its rows with
 * unknowns.  False when memory ran out. */
static bool store_rows(const Trie *tr, const Places *p, const TcTerms *t, uint32_t rows, const TcTerm *loose,
                       const TcTerm *shape, uint32_t looses, TcTerm **out, size_t *n, size_t *cap) {
    uint32_t u = 0;
    for (size_t o = 0; o < p->orders; o++) {
        uint32_t run = p->order[o];
        for (; u < looses && row_in_run(p, t, loose[u], run, true); u++)
            if (!tc_push_u32(realloc, out, n, cap, loose[u])) return false;
        TrieCursor c;
        trie_seek(tr, &c, rows, map_key(run, 1), map_key(run, UINT32_MAX));
        uint64_t key = 0;
        uint32_t value = 0;
        while (trie_next(tr, &c, &key, &value)) {
            TcTerm g = (TcTerm)key;
            for (; u < looses && row_in_run(p, t, loose[u], run, false) && loose_first(loose[u], shape[u], g); u++)
                if (!tc_push_u32(realloc, out, n, cap, loose[u])) return false;
            for (uint32_t k = 0; k < value; k++)
                if (!tc_push_u32(realloc, out, n, cap, g)) return false;
        }
    }
    for (; u < looses; u++)
        if (!tc_push_u32(realloc, out, n, cap, loose[u])) return false;
    return true;
}

/* ---- Rules and programs (section 4) --------------------------------------- */

struct TcProgram {
    TcTerms *terms;
    TcStoredRule *rule; size_t rules, rule_cap;
    uint32_t *term;     size_t terms_len, term_cap;   /* every rule's terms            */
    uint64_t *check;    size_t checks_len, check_cap; /* 4 words a check: kind, left, right, count */
    char *name;         size_t name_len, name_cap;    /* rule names, NUL-terminated    */
    uint32_t *row;      size_t rows, row_cap;         /* the initial state            */
    FlatPlan *prepared; size_t prepared_count;      /* immutable rule-prefix plans  */
    uint32_t *plan;     size_t plan_len, plan_cap;    /* its rules' join plans (tcalc_plan.c),
                                                         made when first explored     */
};

TcProgram *tc_program_new(TcTerms *terms) {
    TcProgram *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    *p = (TcProgram){.terms = terms};
    return p;
}

/* Releases the program and its pools; its term table is the caller's. */
static void program_release(TcProgram *p) {
    free(p->rule); free(p->term); free(p->check); free(p->name); free(p->row); free(p->prepared); free(p->plan);
    free(p);
}

void tc_program_free(TcProgram *p) { if (p) program_release(p); }

TcTerms *tc_program_terms(TcProgram *p) { return p->terms; }
uint32_t tc_program_rules(const TcProgram *p) { return (uint32_t)p->rules; }
const TcTerm *tc_program_rows(const TcProgram *p, size_t *n) {
    *n = p->rows;
    return p->rows ? p->row : no_items;
}
const char *tc_program_rule_name(const TcProgram *p, uint32_t r) {
    return p->name + p->rule[r].name;
}

/* Whether every local of t is below `locals` and t mentions no unknown. */
static bool rule_term_ok(const TcTerms *t, TcTerm term, uint32_t locals) {
    if (!term) return false;
    TcNode n = term_node(t, term);
    if (n.ground) return true;
    if (n.kind == TC_LOCAL) return n.value < locals;
    if (n.kind == TC_UNKNOWN) return false;
    for (uint32_t i = 0; i < n.length; i++)
        if (!rule_term_ok(t, term_item(t, n.start + i), locals)) return false;
    return true;
}

static bool push_term(TcProgram *p, TcTerm term) {
    return tc_push_u32(realloc, &p->term, &p->terms_len, &p->term_cap, term);
}

/* ---- Guards ----------------------------------------------------------------
 * A check of two terms is an equation or a guard; a guard is /=, < or <=
 * (tcalc_core.h).  The operands of < and <= are integers, a sum written
 * (+ A B) among them. */
static bool check_binary(TcCheckKind k) { return k == TC_CHECK_EQUAL || k >= TC_CHECK_DIFFERENT; }
static bool check_guard(TcCheckKind k) { return k >= TC_CHECK_DIFFERENT; }
static bool check_compares(TcCheckKind k) { return k == TC_CHECK_LESS || k == TC_CHECK_LESS_EQUAL; }

/* Whether the term is written (+ A B). */
static bool written_sum(const TcTerms *t, TcTerm term) {
    TcNode n = term_node(t, term);
    if (n.kind != TC_EXPRESSION || n.length != 3) return false;
    TcTerm head = term_item(t, n.start);
    if (term_node(t, head).kind != TC_SYMBOL) return false;
    size_t length = 0;
    const char *bytes = tc_bytes(t, head, &length);
    return length == 1 && bytes[0] == '+';
}

/* Marks in `bound` (or, without `mark`, finds unmarked) the locals of a term;
 * the first local not marked in *local.  False when one is unmarked. */
static bool locals_bound(const TcTerms *t, TcTerm term, bool *bound, uint32_t locals, bool mark,
                         uint32_t *local) {
    TcNode n = term_node(t, term);
    if (n.ground) return true;
    if (n.kind == TC_LOCAL) {
        if ((uint64_t)n.value >= locals) return true;   /* refused as ill-formed elsewhere */
        if (mark) bound[n.value] = true;
        if (bound[n.value]) return true;
        *local = (uint32_t)n.value;
        return false;
    }
    if (n.kind != TC_EXPRESSION) return true;
    for (uint32_t i = 0; i < n.length; i++)
        if (!locals_bound(t, term_item(t, n.start + i), bound, locals, mark, local) && !mark)
            return false;
    return true;
}

/* The refusal of a guard operand: a summand that is a sum. */
static bool operand_ok(const TcTerms *t, TcCheckKind k, TcTerm operand) {
    if (!check_compares(k) || !written_sum(t, operand)) return true;
    TcNode n = term_node(t, operand);
    return !written_sum(t, term_item(t, n.start + 1)) && !written_sum(t, term_item(t, n.start + 2));
}

const char *tc_rule_refusal(const TcTerms *t, const TcRule *r, uint32_t *check, uint32_t *local) {
    *check = UINT32_MAX;
    *local = UINT32_MAX;
    uint32_t guards = 0;
    for (uint32_t i = 0; i < r->checks; i++) guards += check_guard(r->check[i].kind) ? 1u : 0u;
    if (!guards) return NULL;
    bool *bound = calloc(r->locals ? r->locals : 1, sizeof *bound);
    if (!bound) return "out of memory";
    uint32_t unused = 0;
    for (uint32_t i = 0; i < r->takes; i++) (void)locals_bound(t, r->take[i], bound, r->locals, true, &unused);
    for (uint32_t i = 0; i < r->reads; i++) (void)locals_bound(t, r->read[i], bound, r->locals, true, &unused);
    const char *why = NULL;
    for (uint32_t i = 0; !why && i < r->checks; i++) {
        TcCheck c = r->check[i];
        if (!check_guard(c.kind)) continue;
        *check = i;
        if (!operand_ok(t, c.kind, c.left) || !operand_ok(t, c.kind, c.right))
            why = "a summand of a sum (+ A B) is no sum";
        else if (!locals_bound(t, c.left, bound, r->locals, false, local) ||
                 !locals_bound(t, c.right, bound, r->locals, false, local))
            why = "a guard reads a variable that no take or read pattern of its rule binds";
    }
    free(bound);
    if (!why) *check = UINT32_MAX;
    return why;
}

/* Whether two stored rules are equal up to renaming: their locals are numbered
 * by first appearance, so this is equality of their terms and checks. */
static bool same_rule(const TcProgram *p, const TcStoredRule *a, const TcStoredRule *b) {
    if (a->takes != b->takes || a->reads != b->reads || a->tells != b->tells ||
        a->check_count != b->check_count || a->makes != b->makes || a->locals != b->locals)
        return false;
    size_t terms = (size_t)a->takes + a->reads + 2 * (size_t)a->tells + a->makes;
    for (size_t i = 0; i < terms; i++)
        if (p->term[a->terms + i] != p->term[b->terms + i]) return false;
    for (size_t i = 0; i < 4 * (size_t)a->check_count; i++)
        if (p->check[4 * (size_t)a->checks + i] != p->check[4 * (size_t)b->checks + i]) return false;
    return true;
}

bool tc_program_add_rule(TcProgram *p, const TcRule *r) {
    TcTerms *t = p->terms;
    if ((r->takes && !r->take) || (r->reads && !r->read) || (r->tells && !r->tell) ||
        (r->checks && !r->check) || (r->makes && !r->make)) return false;
    const TcTerm *take = r->take ? r->take : no_items;
    const TcTerm *read = r->read ? r->read : no_items;
    const TcTerm *tell = r->tell ? r->tell : no_items;
    const TcCheck *check = r->check ? r->check : no_checks;
    const TcTerm *make = r->make ? r->make : no_items;
    for (uint32_t i = 0; i < r->takes; i++) if (!rule_term_ok(t, take[i], r->locals)) return false;
    for (uint32_t i = 0; i < r->reads; i++) if (!rule_term_ok(t, read[i], r->locals)) return false;
    for (uint32_t i = 0; i < 2 * r->tells; i++) if (!rule_term_ok(t, tell[i], r->locals)) return false;
    for (uint32_t i = 0; i < r->makes; i++) if (!rule_term_ok(t, make[i], r->locals)) return false;
    for (uint32_t i = 0; i < r->checks; i++) {
        const TcCheck *c = &check[i];
        if (!rule_term_ok(t, c->left, r->locals)) return false;
        if (check_binary(c->kind) && !rule_term_ok(t, c->right, r->locals)) return false;
    }
    {
        uint32_t guard = 0, local = 0;
        if (tc_rule_refusal(t, r, &guard, &local)) return false;
    }
    TcStoredRule s = {
        .name = (uint32_t)p->name_len, .terms = (uint32_t)p->terms_len,
        .checks = (uint32_t)(p->checks_len / 4), .takes = r->takes, .reads = r->reads,
        .tells = r->tells, .check_count = r->checks, .makes = r->makes,
    };
    /* the locals renumbered by first appearance, in the order take, read, tell,
     * check, make */
    uint32_t *map = calloc(r->locals ? r->locals : 1, sizeof *map), next = 0;
    if (!map) return false;
    bool ok = true;
    const char *name = r->name ? r->name : "";
    for (size_t i = 0; ok && i <= strlen(name); i++)
        ok = tc_push_char(realloc, &p->name, &p->name_len, &p->name_cap, name[i]);
    for (uint32_t i = 0; ok && i < r->takes; i++) {
        TcTerm u = renumber(t, take[i], TC_LOCAL, map, &next);
        ok = u && push_term(p, u);
    }
    for (uint32_t i = 0; ok && i < r->reads; i++) {
        TcTerm u = renumber(t, read[i], TC_LOCAL, map, &next);
        ok = u && push_term(p, u);
    }
    for (uint32_t i = 0; ok && i < 2 * r->tells; i++) {
        TcTerm u = renumber(t, tell[i], TC_LOCAL, map, &next);
        ok = u && push_term(p, u);
    }
    for (uint32_t i = 0; ok && i < r->checks; i++) {
        const TcCheck *c = &check[i];
        TcTerm left = renumber(t, c->left, TC_LOCAL, map, &next);
        TcTerm right = check_binary(c->kind) ? renumber(t, c->right, TC_LOCAL, map, &next) : 0;
        uint64_t words[4] = {(uint64_t)c->kind, left, right, c->count};
        ok = left && (right || !check_binary(c->kind));
        for (int k = 0; ok && k < 4; k++)
            ok = tc_push_u64(realloc, &p->check, &p->checks_len, &p->check_cap, words[k]);
    }
    for (uint32_t i = 0; ok && i < r->makes; i++) {
        TcTerm u = renumber(t, make[i], TC_LOCAL, map, &next);
        ok = u && push_term(p, u);
    }
    free(map);
    s.locals = next;
    s.same = (uint32_t)p->rules;
    for (size_t k = 0; ok && k < p->rules; k++)
        if (same_rule(p, &p->rule[k], &s)) { s.same = p->rule[k].same; break; }
    if (ok && tc_push_rule(realloc, &p->rule, &p->rules, &p->rule_cap, s)) return true;
    p->name_len = s.name;   /* nothing of the refused rule stays */
    p->terms_len = s.terms;
    p->checks_len = 4 * (size_t)s.checks;
    return false;
}

/* Whether t mentions no local (a row may mention unknowns of the state). */
static bool row_term_ok(const TcTerms *t, TcTerm term) {
    if (!term) return false;
    TcNode n = term_node(t, term);
    if (n.ground || n.kind == TC_UNKNOWN) return true;
    if (n.kind == TC_LOCAL) return false;
    for (uint32_t i = 0; i < n.length; i++)
        if (!row_term_ok(t, term_item(t, n.start + i))) return false;
    return true;
}

bool tc_program_add_row(TcProgram *p, TcTerm row) {
    if (!row_term_ok(p->terms, row)) return false;
    return tc_push_u32(realloc, &p->row, &p->rows, &p->row_cap, row);
}

/* A program as one exploration reads it: its pools, borrowed.  A rule's terms
 * are consecutive: its take patterns, its read patterns, its tells (two terms
 * an equation), its made rows. */
typedef struct {
    const TcStoredRule *rule; size_t rules;
    const TcTerm *term; size_t terms;
    const uint64_t *check; size_t checks;
    const char *name;
    const FlatPlan *prepared; size_t prepared_count;
    const uint32_t *plan; size_t plan_len;
} ProgramView;

static ProgramView program_view(const TcProgram *p) {
    return (ProgramView){p->rule, p->rules, p->term, p->terms_len, p->check, p->checks_len, p->name, p->prepared,
                         p->prepared_count, p->plan, p->plan_len};
}

static inline TcStoredRule rule_at(const ProgramView *v, uint32_t rule) { return v->rule[rule]; }
/* Pattern k: the takes, then the reads. */
static inline TcTerm pattern_at(const ProgramView *v, const TcStoredRule *r, uint32_t k) {
    return v->term[r->terms + k];
}
static inline TcTerm tell_at(const ProgramView *v, const TcStoredRule *r, uint32_t i) {
    return v->term[r->terms + r->takes + r->reads + i];
}
static inline TcTerm make_at(const ProgramView *v, const TcStoredRule *r, uint32_t k) {
    return v->term[r->terms + r->takes + r->reads + 2 * r->tells + k];
}
static inline TcCheck check_at(const ProgramView *v, const TcStoredRule *r, uint32_t i) {
    const uint64_t *w = v->check + 4 * ((size_t)r->checks + i);
    return (TcCheck){(TcCheckKind)w[0], (TcTerm)w[1], (TcTerm)w[2], w[3]};
}

/* ---- One store, grown by delta (section 7, tcalc_delta.c) ------------------
 * A run by delta has a line of versions, each holding the one before: the
 * store is kept as a log of its rows in the order they arrive, and a version
 * is a length of the log.  A run's list and each key node of an index list
 * their rows the latest first, so that the rows of an earlier version are
 * those below its length. */
typedef struct {
    TcTerm *row; size_t rows, row_cap;           /* the store, in arrival order          */
    uint32_t *member; size_t member_cap;          /* the rows as a set: term ids, 0 empty */
    uint32_t *list; size_t lists, list_cap;       /* 2 words a run: 1 when its list is kept,
                                                     its latest entry + 1                 */
    uint32_t *filled; size_t filleds, filled_cap; /* per index (tcalc_store.c, runs and
                                                     indexes): 1 when its rows are kept   */
    uint32_t *node; size_t node_words, node_cap;  /* per key node (tcalc_store.c): its
                                                     latest entry + 1                     */
    uint32_t *entry; size_t entries, entry_cap;   /* 2 words an entry: the row's position,
                                                     the entry before + 1 (0 none)        */
    uint32_t *post; size_t posts, post_cap;       /* per split (tcalc_store.c): the latest
                                                     entry of its postings + 1           */
    uint32_t *place; size_t place_cap, places;    /* (head, length, path, key) -> the last
                                                     join there + 1: 5 words a slot, head 0
                                                     empty (tcalc_delta.c, joins)          */
    uint32_t *join; size_t joins, join_cap;       /* 3 words a join: rule, pattern, the
                                                     place's join before + 1               */
    uint32_t *stamp; size_t stamps, stamp_cap;    /* per join: the last step it was met     */
    uint32_t *wild; size_t wilds, wild_cap;       /* 2 words: rule, pattern of no fixed
                                                     head                                  */
    uint32_t *todo; size_t todos, todo_cap;       /* the joins a step meets                */
    uint32_t step;                                /* the steps after the first, counted    */
} DeltaStore;

/* A cursor over the rows of the one store a probe meets (tcalc_delta.c), at
 * positions lo .. hi-1, the latest first: a run's list or a key node's, or
 * every position. */
typedef struct {
    uint32_t lo, hi;    /* the window                                     */
    bool scan;          /* every position                                 */
    uint32_t at;        /* scan: the next position + 1                    */
    uint32_t entry;     /* the list's next entry + 1, 0 when none is left */
} DeltaCursor;

static void delta_free(DeltaStore *s) {
    free(s->row); free(s->member); free(s->list); free(s->filled); free(s->node); free(s->entry); free(s->post);
    free(s->place); free(s->join); free(s->stamp); free(s->wild); free(s->todo);
}

/* ---- What an exploration leaves (section 7) ------------------------------- */

struct TcOutcomes {
    TcTerm *row; size_t rows, row_cap;          /* the rows of every listed state */
    uint32_t *start; size_t starts, start_cap;  /* outcome i: rows start[i] .. */
    uint32_t *size;  size_t sizes, size_cap;
    uint32_t *amb_start; size_t ambs, amb_cap;  /* ambiguous states            */
    uint32_t *amb_size;  size_t amb_sizes, amb_size_cap;
    uint64_t *amb_matches; size_t amb_match_len, amb_match_cap;
    size_t outcomes;
    bool finished, memory_ok, cyclic, decider_failed, disagreed;
    uint64_t states, work;
    uint64_t unifications, rows_met, matches, rows_stored, patterns_tried, identity_work,
             indexes;   /* work counted */
};

/* Lists a state's rows (an outcome, a state of a run, or an ambiguous one),
 * in canonical order (store_rows). */
static bool list_state(TcOutcomes *o, const Trie *tr, const Places *p, const TcTerms *t, uint32_t rows,
                       const TcTerm *loose, const TcTerm *shape, uint32_t looses, bool ambiguous,
                       uint64_t matches) {
    uint32_t start = (uint32_t)o->rows;
    if (!store_rows(tr, p, t, rows, loose, shape, looses, &o->row, &o->rows, &o->row_cap)) return false;
    uint32_t size = (uint32_t)(o->rows - start);
    if (ambiguous)
        return tc_push_u32(realloc, &o->amb_start, &o->ambs, &o->amb_cap, start) &&
               tc_push_u32(realloc, &o->amb_size, &o->amb_sizes, &o->amb_size_cap, size) &&
               tc_push_u64(realloc, &o->amb_matches, &o->amb_match_len, &o->amb_match_cap, matches);
    o->outcomes++;
    return tc_push_u32(realloc, &o->start, &o->starts, &o->start_cap, start) &&
           tc_push_u32(realloc, &o->size, &o->sizes, &o->size_cap, size);
}

/* A structural order on terms: by kind, then by value, bytes or items, never
 * by term id, so that it does not depend on the order terms were made in. */
static int term_order(const TcTerms *t, TcTerm a, TcTerm b) {
    if (a == b) return 0;
    const TcNode *x = node_of(t, a), *y = node_of(t, b);
    if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
    if (x->kind == TC_SYMBOL || x->kind == TC_STRING) {
        uint32_t n = x->length < y->length ? x->length : y->length;
        int c = n ? memcmp(t->byte + x->start, t->byte + y->start, n) : 0;
        if (c) return c < 0 ? -1 : 1;
    } else if (x->kind == TC_EXPRESSION) {
        uint32_t n = x->length < y->length ? x->length : y->length;
        for (uint32_t i = 0; i < n; i++) {
            int c = term_order(t, t->item[x->start + i], t->item[y->start + i]);
            if (c) return c;
        }
    } else if (x->value != y->value) {   /* integer, local, unknown */
        return x->value < y->value ? -1 : 1;
    }
    return x->length < y->length ? -1 : x->length > y->length;
}

/* Sorts terms by term_order (merge sort, stable); false when memory ran out. */
static bool sort_terms(const TcTerms *t, TcTerm *items, uint32_t n) {
    TcTerm *tmp = malloc((n ? n : 1) * sizeof *tmp);
    if (!tmp) return false;
    for (uint32_t width = 1; width < n; width *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * width) {
            uint32_t mid = lo + width < n ? lo + width : n;
            uint32_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            uint32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                tmp[k++] = term_order(t, items[i], items[j]) <= 0 ? items[i++] : items[j++];
            while (i < mid) tmp[k++] = items[i++];
            while (j < hi) tmp[k++] = items[j++];
        }
        memcpy(items, tmp, n * sizeof *items);
    }
    free(tmp);
    return true;
}

/* The structural order of two lists of rows, rows[0 .. na) and the nb after
 * them: each sorted by term_order (in place), then compared in turn, the
 * shorter first when one is the other's prefix.  -2 when memory ran out. */
static int rows_order(const TcTerms *t, TcTerm *rows, uint32_t na, uint32_t nb) {
    if (!sort_terms(t, rows, na) || !sort_terms(t, rows + na, nb)) return -2;
    int verdict = 0;
    for (uint32_t i = 0; verdict == 0 && i < na && i < nb; i++) verdict = term_order(t, rows[i], rows[na + i]);
    if (verdict == 0 && na != nb) verdict = na < nb ? -1 : 1;
    return verdict;
}

static void outcomes_release(TcOutcomes *o) {
    free(o->row); free(o->start); free(o->size);
    free(o->amb_start); free(o->amb_size); free(o->amb_matches);
    free(o);
}

void tc_outcomes_free(TcOutcomes *o) { if (o) outcomes_release(o); }

size_t tc_outcomes_count(const TcOutcomes *o) { return o->outcomes; }
/* (Rows of states that are all empty are never stored: the list may be null,
 * and an empty state's rows are then the empty list.) */
const TcTerm *tc_outcome_rows(const TcOutcomes *o, size_t i, size_t *rows) {
    *rows = o->size[i];
    return o->row ? o->row + o->start[i] : no_items;
}
size_t tc_outcomes_ambiguous(const TcOutcomes *o) { return o->ambs; }
const TcTerm *tc_ambiguous_rows(const TcOutcomes *o, size_t i, size_t *rows, size_t *matches) {
    *rows = o->amb_size[i];
    *matches = (size_t)o->amb_matches[i];
    return o->row ? o->row + o->amb_start[i] : no_items;
}
bool tc_outcomes_finished(const TcOutcomes *o) { return o->finished; }
uint64_t tc_outcomes_states(const TcOutcomes *o) { return o->states; }
uint64_t tc_outcomes_work(const TcOutcomes *o) { return o->work; }
uint64_t tc_outcomes_unifications(const TcOutcomes *o) { return o->unifications; }
uint64_t tc_outcomes_rows_met(const TcOutcomes *o) { return o->rows_met; }
uint64_t tc_outcomes_matches(const TcOutcomes *o) { return o->matches; }
uint64_t tc_outcomes_rows_stored(const TcOutcomes *o) { return o->rows_stored; }
uint64_t tc_outcomes_patterns_tried(const TcOutcomes *o) { return o->patterns_tried; }
uint64_t tc_outcomes_identity_work(const TcOutcomes *o) { return o->identity_work; }
uint64_t tc_outcomes_indexes(const TcOutcomes *o) { return o->indexes; }
bool tc_outcomes_complete_memory(const TcOutcomes *o) { return o->memory_ok; }
bool tc_outcomes_cyclic(const TcOutcomes *o) { return o->cyclic; }
bool tc_outcomes_decider_failed(const TcOutcomes *o) { return o->decider_failed; }
bool tc_outcomes_shortcuts_disagreed(const TcOutcomes *o) { return o->disagreed; }
