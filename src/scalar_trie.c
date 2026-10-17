#include "scalar_trie.h"
#include "utf8.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t edge, terminal, failure; uint64_t best, suffix_best;
    size_t best_len, suffix_len; bool has_best, has_suffix; } TrieNode;
typedef struct { uint32_t label, child, next; } TrieEdge;
typedef struct { uint64_t payload; size_t len; uint32_t next; } TrieTerminal;
typedef struct { uint32_t parent, label, child; bool used; } TrieSlot;
struct CettaScalarTrie {
    TrieNode *nodes; size_t node_len, node_cap;
    TrieEdge *edges; size_t edge_len, edge_cap;
    TrieTerminal *terminals; size_t terminal_len, terminal_cap;
    TrieSlot *slots; size_t slot_len, slot_cap;
    size_t max_length; bool prepared;
};

static bool reserve(void **p, size_t *cap, size_t n, size_t width) {
    if (n <= *cap) return true;
    size_t next = *cap ? *cap : 16u;
    while (next < n) { if (next > SIZE_MAX / 2u) { next = n; break; } next *= 2u; }
    if (next > SIZE_MAX / width) return false;
    void *copy = realloc(*p, next * width);
    if (!copy) return false;
    *p = copy; *cap = next; return true;
}
static size_t key_hash(uint32_t parent, uint32_t label) {
    uint64_t x = ((uint64_t)parent << 32u) | label;
    x ^= x >> 30u; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27u; x *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(x ^ (x >> 31u));
}
static bool slots_grow(CettaScalarTrie *t) {
    if (t->slot_cap && t->slot_len < t->slot_cap / 2u) return true;
    size_t cap = t->slot_cap ? t->slot_cap * 2u : 32u;
    if (cap < t->slot_cap || cap > SIZE_MAX / sizeof(TrieSlot)) return false;
    TrieSlot *slots = calloc(cap, sizeof(*slots));
    if (!slots) return false;
    for (size_t i = 0; i < t->slot_cap; ++i) if (t->slots[i].used) {
        TrieSlot s = t->slots[i]; size_t at = key_hash(s.parent, s.label) & (cap - 1u);
        while (slots[at].used) at = (at + 1u) & (cap - 1u);
        slots[at] = s;
    }
    free(t->slots); t->slots = slots; t->slot_cap = cap; return true;
}
CettaScalarTrie *cetta_scalar_trie_new(void) {
    CettaScalarTrie *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    if (!reserve((void **)&t->nodes, &t->node_cap, 1u, sizeof(*t->nodes))) {
        free(t); return NULL;
    }
    t->nodes[0] = (TrieNode){.edge = CETTA_TRIE_NONE, .terminal = CETTA_TRIE_NONE};
    t->node_len = 1u; return t;
}
void cetta_scalar_trie_free(CettaScalarTrie *t) {
    if (!t) return;
    free(t->nodes); free(t->edges); free(t->terminals); free(t->slots); free(t);
}
uint32_t cetta_scalar_trie_child(const CettaScalarTrie *t, uint32_t parent, uint32_t cp) {
    if (!t || parent >= t->node_len || !t->slot_cap) return CETTA_TRIE_NONE;
    size_t at = key_hash(parent, cp) & (t->slot_cap - 1u);
    while (t->slots[at].used) {
        TrieSlot s = t->slots[at];
        if (s.parent == parent && s.label == cp) return s.child;
        at = (at + 1u) & (t->slot_cap - 1u);
    }
    return CETTA_TRIE_NONE;
}
bool cetta_scalar_trie_extend(CettaScalarTrie *t, uint32_t parent, uint32_t cp, uint32_t *out) {
    if (!t || !out || parent >= t->node_len || cp > 0x10ffffu ||
        (cp >= 0xd800u && cp <= 0xdfffu)) return false;
    uint32_t child = cetta_scalar_trie_child(t, parent, cp);
    if (child != CETTA_TRIE_NONE) { *out = child; return true; }
    if (t->node_len >= UINT32_MAX || t->edge_len >= UINT32_MAX || !slots_grow(t) ||
        !reserve((void **)&t->nodes, &t->node_cap, t->node_len + 1u, sizeof(*t->nodes)) ||
        !reserve((void **)&t->edges, &t->edge_cap, t->edge_len + 1u, sizeof(*t->edges))) return false;
    child = (uint32_t)t->node_len++;
    t->nodes[child] = (TrieNode){.edge = CETTA_TRIE_NONE, .terminal = CETTA_TRIE_NONE};
    uint32_t edge = (uint32_t)t->edge_len++;
    t->edges[edge] = (TrieEdge){cp, child, t->nodes[parent].edge}; t->nodes[parent].edge = edge;
    size_t at = key_hash(parent, cp) & (t->slot_cap - 1u);
    while (t->slots[at].used) at = (at + 1u) & (t->slot_cap - 1u);
    t->slots[at] = (TrieSlot){parent, cp, child, true}; ++t->slot_len;
    t->prepared = false; *out = child; return true;
}
bool cetta_scalar_trie_terminal(CettaScalarTrie *t, uint32_t node, uint64_t payload, size_t len) {
    if (!t || node >= t->node_len || t->terminal_len >= UINT32_MAX ||
        !reserve((void **)&t->terminals, &t->terminal_cap, t->terminal_len + 1u,
                 sizeof(*t->terminals))) return false;
    uint32_t at = (uint32_t)t->terminal_len++;
    t->terminals[at] = (TrieTerminal){payload, len, t->nodes[node].terminal};
    TrieNode *n = &t->nodes[node]; n->terminal = at;
    if (!n->has_best || len > n->best_len || (len == n->best_len && payload < n->best)) {
        n->best = payload; n->best_len = len; n->has_best = true;
    }
    if (len > t->max_length) t->max_length = len;
    t->prepared = false; return true;
}
bool cetta_scalar_trie_decode(const uint8_t *bytes, size_t len, size_t at, uint32_t *cp, size_t *after) {
    uint32_t width;
    if (!after || !cetta_utf8_decode_one(bytes, len, at, cp, &width)) return false;
    *after = at + width; return true;
}
bool cetta_scalar_trie_valid_utf8(const uint8_t *s, size_t len) {
    if (!s && len) return false;
    uint32_t cp; size_t next;
    for (size_t at = 0; at < len; at = next)
        if (!cetta_scalar_trie_decode(s, len, at, &cp, &next)) return false;
    return true;
}
bool cetta_scalar_trie_insert_utf8(CettaScalarTrie *t, const uint8_t *s, size_t len, uint64_t payload) {
    if (!cetta_scalar_trie_valid_utf8(s, len)) return false;
    uint32_t node = 0u, cp; size_t next;
    for (size_t at = 0; at < len; at = next) {
        if (!cetta_scalar_trie_decode(s, len, at, &cp, &next) ||
            !cetta_scalar_trie_extend(t, node, cp, &node)) return false;
    }
    return cetta_scalar_trie_terminal(t, node, payload, len);
}
bool cetta_scalar_trie_prepare(CettaScalarTrie *t) {
    if (!t) return false;
    if (t->prepared) return true;
    if (t->node_len > SIZE_MAX / sizeof(uint32_t)) return false;
    uint32_t *queue = malloc(t->node_len * sizeof(*queue));
    if (!queue) return false;
    size_t head = 0, tail = 0; queue[tail++] = 0u;
    while (head < tail) {
        uint32_t id = queue[head++]; TrieNode *n = &t->nodes[id];
        n->has_suffix = n->has_best; n->suffix_best = n->best; n->suffix_len = n->best_len;
        if (id) {
            TrieNode *f = &t->nodes[n->failure];
            if (f->has_suffix && (!n->has_suffix || f->suffix_len > n->suffix_len ||
                (f->suffix_len == n->suffix_len && f->suffix_best < n->suffix_best))) {
                n->has_suffix = true; n->suffix_best = f->suffix_best; n->suffix_len = f->suffix_len;
            }
        }
        for (uint32_t e = n->edge; e != CETTA_TRIE_NONE; e = t->edges[e].next) {
            TrieEdge edge = t->edges[e]; uint32_t failure = 0u;
            if (id) {
                uint32_t at = n->failure, candidate;
                while ((candidate = cetta_scalar_trie_child(t, at, edge.label)) == CETTA_TRIE_NONE && at)
                    at = t->nodes[at].failure;
                if (candidate != CETTA_TRIE_NONE) failure = candidate;
            }
            t->nodes[edge.child].failure = failure; queue[tail++] = edge.child;
        }
    }
    free(queue); t->prepared = true; return true;
}
uint32_t cetta_scalar_trie_advance(const CettaScalarTrie *t, uint32_t state, uint32_t cp) {
    if (!t || !t->prepared || state >= t->node_len) return CETTA_TRIE_NONE;
    uint32_t child;
    while ((child = cetta_scalar_trie_child(t, state, cp)) == CETTA_TRIE_NONE && state)
        state = t->nodes[state].failure;
    return child == CETTA_TRIE_NONE ? 0u : child;
}
bool cetta_scalar_trie_best(const CettaScalarTrie *t, uint32_t state, bool suffix,
                           uint64_t *payload, size_t *len) {
    if (!t || state >= t->node_len || !payload || !len) return false;
    const TrieNode *n = &t->nodes[state];
    if (suffix ? !n->has_suffix : !n->has_best) return false;
    *payload = suffix ? n->suffix_best : n->best; *len = suffix ? n->suffix_len : n->best_len;
    return true;
}
size_t cetta_scalar_trie_max_length(const CettaScalarTrie *t) { return t ? t->max_length : 0u; }

typedef struct { uint32_t node, edge; void *state; bool entered; } WalkFrame;
bool cetta_scalar_trie_walk(const CettaScalarTrie *t, const CettaTrieAutomaton *a,
    const void *initial, bool (*emit)(void *, uint64_t), void *context, CettaTrieWalkStats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!t || !a || !a->state_size || !initial || !a->step || !a->accepts || !emit) return false;
    WalkFrame *stack = NULL; size_t len = 0, cap = 0; bool ok = false;
    void *root = malloc(a->state_size);
    if (!root || !reserve((void **)&stack, &cap, 1u, sizeof(*stack))) { free(root); free(stack); return false; }
    memcpy(root, initial, a->state_size); stack[len++] = (WalkFrame){0u, t->nodes[0].edge, root, false};
    while (len) {
        WalkFrame *f = &stack[len - 1u];
        if (!f->entered) {
            f->entered = true; if (stats) ++stats->nodes;
            if (t->nodes[f->node].terminal != CETTA_TRIE_NONE) {
                int accept = a->accepts(a->context, f->state); if (accept < 0) goto done;
                if (accept) for (uint32_t term = t->nodes[f->node].terminal; term != CETTA_TRIE_NONE;
                    term = t->terminals[term].next) {
                    if (stats) ++stats->terminals;
                    if (!emit(context, t->terminals[term].payload)) goto done;
                }
            }
        }
        if (f->edge == CETTA_TRIE_NONE) { free(f->state); --len; continue; }
        TrieEdge edge = t->edges[f->edge]; f->edge = edge.next;
        void *state = malloc(a->state_size); if (!state) goto done;
        if (stats) ++stats->transitions;
        int alive = a->step(a->context, f->state, edge.label, state);
        if (alive < 0) { free(state); goto done; }
        if (!alive) { free(state); continue; }
        if (!reserve((void **)&stack, &cap, len + 1u, sizeof(*stack))) { free(state); goto done; }
        stack[len++] = (WalkFrame){edge.child, t->nodes[edge.child].edge, state, false};
    }
    ok = true;
done:
    for (size_t i = 0; i < len; ++i) free(stack[i].state);
    free(stack); return ok;
}
