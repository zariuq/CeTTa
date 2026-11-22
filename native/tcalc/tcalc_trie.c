/* tcalc_trie.c - persistent maps from 64-bit keys to 32-bit values, the
 * storage of states (tcalc_store.c).  Included by tcalc_core.c at its
 * section 3, in the Cesta unit tcalc_core.cesta with the rest.
 *
 * A map is a hash array mapped trie in the CHAMP layout (Steindorfer and
 * Vinju 2015) whose hash is the key itself, read from its high bits down, so
 * that the map iterates in the order of its keys.  A node branches on one
 * 5-bit chunk of the key (the chunks are aligned at bit 0): the chunk holding
 * the highest bit in which its keys differ.  A key alone in its chunk is held
 * in the node, inline; two or more keys sharing a chunk are a child node.
 * Every node above the chunk it branches on holds the bits its keys share
 * (its prefix), so a chain of nodes with one child each never occurs.  The
 * shape is a function of the set of keys: equal maps have equal shapes, so
 * two maps are compared node by node, and a node both hold is equal to itself
 * without being read.  A map of one key is a node of one entry branching on
 * the top chunk.
 *
 * Nodes live in one pool of 32-bit words and are named by their offset there
 * (0 names the empty map), so that the pool is plain data and may move as it
 * grows.  A node counts the maps and nodes that hold it.  A change copies the
 * path from the root to the key (path copying), node by node, except a node
 * held once by the map being changed, which is changed in place: a state made
 * from another shares every node off the paths that changed, and a map kept in
 * one version is changed in place.  A node that nothing holds any more is
 * freed, and gives up its hold on its children.
 *
 * Node layout, in words:
 *   0  holders                 the maps and nodes that hold it (free: 0)
 *   1  capacity << 8 | shift   the words it has room for; the low bit of the
 *                              chunk it branches on (0, 5, ..., 60)
 *   2  datamap                 the chunks holding a key inline (free: the next
 *                              free node of its capacity)
 *   3  nodemap                 the chunks holding a child
 *   4  prefix, low word        the key bits above the chunk; the bits of the
 *   5  prefix, high word       chunk and below it are 0
 *   6  the entries, three words each in chunk order: key low, key high, value
 *      the children, one word each in chunk order */

enum {
    TRIE_HEAD = 6,     /* words of a node before its entries          */
    TRIE_ENTRY = 3,    /* words of an entry                           */
    TRIE_TOP = 60,     /* the shift of the top chunk                  */
    TRIE_DEPTH = 13,   /* chunks of a key: the nodes on a path, at most */
    TRIE_MAX = 104,    /* the largest capacity: 32 entries, rounded   */
    TRIE_CLASSES = TRIE_MAX / 4 - 1
};

typedef struct {
    uint32_t *word; size_t words, word_cap;   /* the pool; words 0 .. 7 are no node     */
    uint32_t free[TRIE_CLASSES];              /* per capacity / 4 - 1: the first free
                                                 node of that capacity, 0 none          */
    uint64_t held;                            /* entries of live nodes whose key's low
                                                 word is not 0: the rows held          */
    uint64_t compared;                        /* node pairs read deciding equality    */
    bool failed;                              /* memory ran out                        */
} Trie;

/* ---- Bits ------------------------------------------------------------------ */

static inline uint32_t bits_count(uint32_t v) {
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    return (((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
}

/* The lowest set bit of a nonzero word. */
static inline uint32_t bits_lowest(uint32_t v) { return bits_count((v & (0u - v)) - 1u); }

/* The highest set bit of a nonzero key. */
static inline uint32_t bits_highest(uint64_t v) {
    uint32_t b = 0;
    for (uint32_t step = 32; step; step >>= 1)
        if (v >> step) { v >>= step; b += step; }
    return b;
}

static inline uint32_t key_chunk(uint64_t key, uint32_t shift) { return (uint32_t)(key >> shift) & 31u; }

/* The key bits above the chunk at `shift`. */
static inline uint64_t key_above(uint64_t key, uint32_t shift) {
    return shift + 5 >= 64 ? 0 : key >> (shift + 5);
}

/* The shift of the chunk holding the highest bit in which two keys differ. */
static inline uint32_t key_split(uint64_t a, uint64_t b) { return bits_highest(a ^ b) / 5 * 5; }

/* ---- Nodes -------------------------------------------------------------------- */

static inline uint32_t node_holders(const Trie *t, uint32_t n) { return t->word[n]; }
static inline uint32_t node_shift(const Trie *t, uint32_t n) { return t->word[n + 1] & 255u; }
static inline uint32_t node_capacity(const Trie *t, uint32_t n) { return t->word[n + 1] >> 8; }
static inline uint32_t node_datamap(const Trie *t, uint32_t n) { return t->word[n + 2]; }
static inline uint32_t node_nodemap(const Trie *t, uint32_t n) { return t->word[n + 3]; }
static inline uint64_t node_prefix(const Trie *t, uint32_t n) {
    return t->word[n + 4] | (uint64_t)t->word[n + 5] << 32;
}
static inline uint32_t node_entries(const Trie *t, uint32_t n) { return bits_count(node_datamap(t, n)); }
static inline uint32_t node_children(const Trie *t, uint32_t n) { return bits_count(node_nodemap(t, n)); }
static inline uint32_t node_size(const Trie *t, uint32_t n) {
    return TRIE_HEAD + TRIE_ENTRY * node_entries(t, n) + node_children(t, n);
}
/* The word of entry i, and of child j. */
static inline uint32_t node_entry(uint32_t n, uint32_t i) { return n + TRIE_HEAD + TRIE_ENTRY * i; }
static inline uint32_t node_child_at(const Trie *t, uint32_t n, uint32_t j) {
    return n + TRIE_HEAD + TRIE_ENTRY * node_entries(t, n) + j;
}
static inline uint64_t entry_key(const Trie *t, uint32_t e) { return t->word[e] | (uint64_t)t->word[e + 1] << 32; }
static inline uint32_t entry_value(const Trie *t, uint32_t e) { return t->word[e + 2]; }
/* The position of chunk c among the entries, or the children, of a map. */
static inline uint32_t map_rank(uint32_t map, uint32_t c) { return bits_count(map & ((1u << c) - 1u)); }
static inline bool key_held(uint64_t key) { return (uint32_t)key != 0; }

static void node_set_head(Trie *t, uint32_t n, uint32_t shift, uint32_t datamap, uint32_t nodemap,
                          uint64_t prefix) {
    t->word[n + 1] = node_capacity(t, n) << 8 | shift;
    t->word[n + 2] = datamap;
    t->word[n + 3] = nodemap;
    t->word[n + 4] = (uint32_t)prefix;
    t->word[n + 5] = (uint32_t)(prefix >> 32);
}

static uint64_t prefix_of(uint64_t key, uint32_t shift) {
    return shift + 5 >= 64 ? 0 : key >> (shift + 5) << (shift + 5);
}

/* A node with room for `words` words, held once, empty; 0 when memory ran out
 * (and t->failed). */
static uint32_t node_alloc(Trie *t, uint32_t words) {
    uint32_t capacity = (words + 3u) & ~3u;
    if (capacity < 8) capacity = 8;
    if (capacity > TRIE_MAX) capacity = TRIE_MAX;
    uint32_t class = capacity / 4 - 2, n = t->free[class];
    if (n) t->free[class] = t->word[n + 2];
    else {
        if (t->words < 8) t->words = 8;   /* offset 0 names the empty map */
        if (t->words + capacity > UINT32_MAX ||
            !reserve(&t->word, &t->word_cap, t->words + capacity)) {
            t->failed = true;
            return 0;
        }
        n = (uint32_t)t->words;
        t->words += capacity;
    }
    t->word[n] = 1;
    t->word[n + 1] = capacity << 8;
    t->word[n + 2] = t->word[n + 3] = t->word[n + 4] = t->word[n + 5] = 0;
    return n;
}

/* The rows a node holds, counted into t->held. */
static uint32_t node_rows(const Trie *t, uint32_t n) {
    uint32_t rows = 0, entries = node_entries(t, n);
    for (uint32_t i = 0; i < entries; i++) rows += key_held(entry_key(t, node_entry(n, i))) ? 1u : 0u;
    return rows;
}

static void node_free(Trie *t, uint32_t n) {
    uint32_t class = node_capacity(t, n) / 4 - 2;
    t->held -= node_rows(t, n);
    t->word[n] = 0;
    t->word[n + 2] = t->free[class];
    t->free[class] = n;
}

static void trie_retain(Trie *t, uint32_t n) { if (n) t->word[n]++; }

/* Gives up one hold on the map at n; a node no longer held is freed, and its
 * children with it. */
static void trie_release(Trie *t, uint32_t n) {
    if (!n || --t->word[n]) return;
    uint32_t children = node_children(t, n);
    for (uint32_t j = 0; j < children; j++) trie_release(t, t->word[node_child_at(t, n, j)]);
    node_free(t, n);
}

/* Node n, held once by the map being changed and with room for `extra` more
 * words: n itself when it is held once and has the room; otherwise a node of
 * the same contents that does (moved when n is held once, a copy holding its
 * children when n is shared, n losing the map's hold).  0 when memory ran out:
 * n is then as it was. */
static uint32_t node_own(Trie *t, uint32_t n, uint32_t extra) {
    uint32_t size = node_size(t, n), held = node_holders(t, n);
    if (held == 1 && size + extra <= node_capacity(t, n)) return n;
    uint32_t want = size + extra;
    if (held == 1) want += want / 2;   /* a node changed in place grows geometrically */
    uint32_t m = node_alloc(t, want);
    if (!m) return 0;
    t->word[m + 1] = node_capacity(t, m) << 8 | node_shift(t, n);
    for (uint32_t w = 2; w < size; w++) t->word[m + w] = t->word[n + w];
    if (held == 1) {   /* moved: its children are m's now, and n is free */
        uint32_t class = node_capacity(t, n) / 4 - 2;
        t->word[n] = 0;
        t->word[n + 2] = t->free[class];
        t->free[class] = n;
        return m;
    }
    uint32_t children = node_children(t, m);
    for (uint32_t j = 0; j < children; j++) trie_retain(t, t->word[node_child_at(t, m, j)]);
    t->held += node_rows(t, m);
    t->word[n]--;
    return m;
}

/* Opens `words` words at `at` in node n (held once, with room), moving the
 * words after it up to the node's size `size`. */
static void node_open(Trie *t, uint32_t n, uint32_t at, uint32_t words, uint32_t size) {
    for (uint32_t w = n + size; w-- > at;) t->word[w + words] = t->word[w];
}

/* Closes `words` words at `at` in node n of size `size`. */
static void node_close(Trie *t, uint32_t n, uint32_t at, uint32_t words, uint32_t size) {
    for (uint32_t w = at; w + words < n + size; w++) t->word[w] = t->word[w + words];
}

static void entry_write(Trie *t, uint32_t e, uint64_t key, uint32_t value) {
    t->word[e] = (uint32_t)key;
    t->word[e + 1] = (uint32_t)(key >> 32);
    t->word[e + 2] = value;
}

/* A node holding two keys of different chunks at `shift`, inline. */
static uint32_t node_pair(Trie *t, uint32_t shift, uint64_t a, uint32_t av, uint64_t b, uint32_t bv) {
    uint32_t n = node_alloc(t, TRIE_HEAD + 2 * TRIE_ENTRY);
    if (!n) return 0;
    uint32_t ca = key_chunk(a, shift), cb = key_chunk(b, shift);
    node_set_head(t, n, shift, 1u << ca | 1u << cb, 0, prefix_of(a, shift));
    entry_write(t, node_entry(n, 0), ca < cb ? a : b, ca < cb ? av : bv);
    entry_write(t, node_entry(n, 1), ca < cb ? b : a, ca < cb ? bv : av);
    t->held += (key_held(a) ? 1u : 0u) + (key_held(b) ? 1u : 0u);
    return n;
}

/* Inserts an inline entry at chunk c of node n (held once, with room). */
static void node_put_entry(Trie *t, uint32_t n, uint32_t c, uint64_t key, uint32_t value) {
    uint32_t size = node_size(t, n), at = node_entry(n, map_rank(node_datamap(t, n), c));
    node_open(t, n, at, TRIE_ENTRY, size);
    t->word[n + 2] |= 1u << c;
    entry_write(t, at, key, value);
    if (key_held(key)) t->held++;
}

/* Removes the inline entry at chunk c of node n (held once). */
static void node_drop_entry(Trie *t, uint32_t n, uint32_t c) {
    uint32_t size = node_size(t, n), at = node_entry(n, map_rank(node_datamap(t, n), c));
    if (key_held(entry_key(t, at))) t->held--;
    node_close(t, n, at, TRIE_ENTRY, size);
    t->word[n + 2] &= ~(1u << c);
}

/* Replaces the entry at chunk c of node n (held once) by the child `child`. */
static void node_entry_to_child(Trie *t, uint32_t n, uint32_t c, uint32_t child) {
    node_drop_entry(t, n, c);
    uint32_t size = node_size(t, n), nodemap = node_nodemap(t, n);
    uint32_t at = node_child_at(t, n, map_rank(nodemap, c));
    node_open(t, n, at, 1, size);
    t->word[n + 3] = nodemap | 1u << c;
    t->word[at] = child;
}

/* Replaces the child at chunk c of node n (held once, with room for two more
 * words) by the entry of the one-entry node `single`, which it frees. */
static void node_child_to_entry(Trie *t, uint32_t n, uint32_t c, uint32_t single) {
    uint32_t e = node_entry(single, 0);
    uint64_t key = entry_key(t, e);
    uint32_t value = entry_value(t, e);
    uint32_t size = node_size(t, n), nodemap = node_nodemap(t, n);
    node_close(t, n, node_child_at(t, n, map_rank(nodemap, c)), 1, size);
    t->word[n + 3] = nodemap & ~(1u << c);
    node_put_entry(t, n, c, key, value);
    trie_release(t, single);
}

/* Removes the child at chunk c of node n (held once); the child is the
 * caller's. */
static void node_drop_child(Trie *t, uint32_t n, uint32_t c) {
    uint32_t size = node_size(t, n), nodemap = node_nodemap(t, n);
    node_close(t, n, node_child_at(t, n, map_rank(nodemap, c)), 1, size);
    t->word[n + 3] = nodemap & ~(1u << c);
}

/* A node of no entries and one child is that child: the node is freed and
 * its hold on the child passes to whatever held the node. */
static uint32_t node_collapse(Trie *t, uint32_t n) {
    if (!node_datamap(t, n) && !node_nodemap(t, n)) {   /* no key left */
        trie_release(t, n);
        return 0;
    }
    if (node_datamap(t, n) || bits_count(node_nodemap(t, n)) != 1) return n;
    uint32_t child = t->word[node_child_at(t, n, 0)];
    t->word[n + 3] = 0;
    trie_release(t, n);
    return child;
}

/* Adds `delta` (modulo 2^32) to the value of `key` in the map at n, which the
 * caller holds once along the path to n: an absent key has the value 0, and a
 * key whose value becomes 0 leaves the map.  Returns the map's node after the
 * change (0 when it is empty; a node of one entry when one key is left), the
 * value after in *after.  When memory runs out t->failed is set and the node
 * returned holds the map's keys as they were or with the change made. */
static uint32_t trie_add_at(Trie *t, uint32_t n, uint64_t key, uint32_t delta, uint32_t *after) {
    if (!n) {
        *after = delta;
        if (!delta) return 0;
        uint32_t m = node_alloc(t, TRIE_HEAD + TRIE_ENTRY);
        if (!m) return 0;
        node_set_head(t, m, TRIE_TOP, 0, 0, 0);
        node_put_entry(t, m, key_chunk(key, TRIE_TOP), key, delta);
        return m;
    }
    uint32_t shift = node_shift(t, n);
    uint64_t prefix = node_prefix(t, n);
    if (key_above(key, shift) != key_above(prefix, shift)) {
        *after = delta;   /* outside the node's prefix: a node above it, of the key and n */
        if (!delta) return n;
        uint32_t split = key_split(key, prefix);
        uint32_t m = node_alloc(t, TRIE_HEAD + TRIE_ENTRY + 1);
        if (!m) return n;
        node_set_head(t, m, split, 0, 1u << key_chunk(prefix, split), prefix_of(key, split));
        t->word[node_child_at(t, m, 0)] = n;
        node_put_entry(t, m, key_chunk(key, split), key, delta);
        return m;
    }
    uint32_t c = key_chunk(key, shift);
    if (node_datamap(t, n) >> c & 1u) {
        uint32_t e = node_entry(n, map_rank(node_datamap(t, n), c));
        uint64_t other = entry_key(t, e);
        if (other == key) {
            uint32_t value = entry_value(t, e) + delta;
            *after = value;
            if (!delta) return n;
            uint32_t m = node_own(t, n, 0);
            if (!m) return n;
            if (value) t->word[node_entry(m, map_rank(node_datamap(t, m), c)) + 2] = value;
            else node_drop_entry(t, m, c);
            return node_collapse(t, m);
        }
        *after = delta;   /* two keys of one chunk: a child of the two */
        if (!delta) return n;
        uint32_t m = node_own(t, n, 0);
        if (!m) return n;
        e = node_entry(m, map_rank(node_datamap(t, m), c));
        uint32_t child = node_pair(t, key_split(key, other), other, entry_value(t, e), key, delta);
        if (!child) return m;
        node_entry_to_child(t, m, c, child);
        return node_collapse(t, m);   /* a map of one key was its root: the child is */
    }
    if (node_nodemap(t, n) >> c & 1u) {
        uint32_t m = node_own(t, n, 0);
        if (!m) { *after = 0; return n; }
        uint32_t slot = node_child_at(t, m, map_rank(node_nodemap(t, m), c));
        uint32_t child = trie_add_at(t, t->word[slot], key, delta, after);
        slot = node_child_at(t, m, map_rank(node_nodemap(t, m), c));
        if (!child) {
            node_drop_child(t, m, c);
            return node_collapse(t, m);
        }
        t->word[slot] = child;
        if (node_entries(t, child) == 1 && !node_nodemap(t, child)) {   /* one key left: inline */
            uint32_t owned = node_own(t, m, TRIE_ENTRY - 1);
            if (!owned) return m;
            node_child_to_entry(t, owned, c, child);
            return owned;
        }
        return m;
    }
    *after = delta;
    if (!delta) return n;
    uint32_t m = node_own(t, n, TRIE_ENTRY);
    if (!m) return n;
    node_put_entry(t, m, c, key, delta);
    return m;
}

/* The map's root in canonical form: a map of one key branches on the top
 * chunk. */
static uint32_t trie_root(Trie *t, uint32_t n) {
    if (!n || node_children(t, n) || node_entries(t, n) != 1 || node_shift(t, n) == TRIE_TOP) return n;
    uint32_t m = node_own(t, n, 0);
    if (!m) return n;
    uint64_t key = entry_key(t, node_entry(m, 0));
    node_set_head(t, m, TRIE_TOP, 1u << key_chunk(key, TRIE_TOP), 0, 0);
    return m;
}

/* Adds delta to the value of key in the map *root, held by the caller (a map
 * the caller shares is copied along the path).  The value after; t->failed
 * when memory ran out. */
static uint32_t trie_add(Trie *t, uint32_t *root, uint64_t key, uint32_t delta) {
    uint32_t after = 0;
    *root = trie_root(t, trie_add_at(t, *root, key, delta, &after));
    return after;
}

/* The value of key in the map (0 when absent). */
static uint32_t trie_get(const Trie *t, uint32_t n, uint64_t key) {
    while (n) {
        uint32_t shift = node_shift(t, n);
        if (key_above(key, shift) != key_above(node_prefix(t, n), shift)) return 0;
        uint32_t c = key_chunk(key, shift);
        if (node_datamap(t, n) >> c & 1u) {
            uint32_t e = node_entry(n, map_rank(node_datamap(t, n), c));
            return entry_key(t, e) == key ? entry_value(t, e) : 0;
        }
        if (!(node_nodemap(t, n) >> c & 1u)) return 0;
        n = t->word[node_child_at(t, n, map_rank(node_nodemap(t, n), c))];
    }
    return 0;
}

/* Whether two maps hold the same keys with the same values.  The shape is a
 * function of the keys, so the nodes are compared in step, and a node both
 * maps hold is equal without being read. */
static bool trie_equal(Trie *t, uint32_t a, uint32_t b) {
    if (a == b) return true;
    if (!a || !b) return false;
    t->compared++;
    if (node_shift(t, a) != node_shift(t, b) || node_datamap(t, a) != node_datamap(t, b) ||
        node_nodemap(t, a) != node_nodemap(t, b) || node_prefix(t, a) != node_prefix(t, b))
        return false;
    uint32_t entries = TRIE_ENTRY * node_entries(t, a);
    for (uint32_t w = 0; w < entries; w++)
        if (t->word[node_entry(a, 0) + w] != t->word[node_entry(b, 0) + w]) return false;
    uint32_t children = node_children(t, a);
    for (uint32_t j = 0; j < children; j++)
        if (!trie_equal(t, t->word[node_child_at(t, a, j)], t->word[node_child_at(t, b, j)])) return false;
    return true;
}

/* ---- Cursors -------------------------------------------------------------------
 * The keys of a map from `lo` to `hi`, in order.  The cursor holds the nodes
 * on its path, so the map must not change while it is read; the pool may
 * move. */
typedef struct {
    uint32_t node[TRIE_DEPTH];
    uint32_t chunk[TRIE_DEPTH];   /* in each node, the next chunk to visit */
    uint32_t depth;
    uint64_t lo, hi;
} TrieCursor;

static void trie_seek(const Trie *t, TrieCursor *c, uint32_t n, uint64_t lo, uint64_t hi) {
    *c = (TrieCursor){.lo = lo, .hi = hi};
    while (n && c->depth < TRIE_DEPTH) {
        uint32_t shift = node_shift(t, n);
        uint64_t at = key_above(lo, shift), prefix = key_above(node_prefix(t, n), shift);
        if (at != prefix) {
            if (at < prefix) {   /* every key of n is beyond lo */
                c->node[c->depth] = n;
                c->chunk[c->depth++] = 0;
            }
            return;
        }
        uint32_t k = key_chunk(lo, shift);
        c->node[c->depth] = n;
        if (node_nodemap(t, n) >> k & 1u) {
            c->chunk[c->depth++] = k + 1;
            n = t->word[node_child_at(t, n, map_rank(node_nodemap(t, n), k))];
            continue;
        }
        c->chunk[c->depth++] = k;
        return;
    }
}

/* The next key of the cursor and its value; false when none is left. */
static bool trie_next(const Trie *t, TrieCursor *c, uint64_t *key, uint32_t *value) {
    while (c->depth) {
        uint32_t n = c->node[c->depth - 1], k = c->chunk[c->depth - 1];
        uint32_t datamap = node_datamap(t, n), nodemap = node_nodemap(t, n);
        uint32_t left = k < 32 ? (datamap | nodemap) & (0xFFFFFFFFu << k) : 0;
        if (!left) { c->depth--; continue; }
        k = bits_lowest(left);
        c->chunk[c->depth - 1] = k + 1;
        if (datamap >> k & 1u) {
            uint32_t e = node_entry(n, map_rank(datamap, k));
            uint64_t at = entry_key(t, e);
            if (at < c->lo) continue;
            if (at > c->hi) { c->depth = 0; return false; }
            *key = at;
            *value = entry_value(t, e);
            return true;
        }
        if (c->depth == TRIE_DEPTH) return false;
        c->node[c->depth] = t->word[node_child_at(t, n, map_rank(nodemap, k))];
        c->chunk[c->depth++] = 0;
    }
    return false;
}

static void trie_free(Trie *t) { free(t->word); }
