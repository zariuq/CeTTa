#include "subst_tree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Epoch counter ─────────────────────────────────────────────────────── */

static uint32_t g_stree_epoch = 1;

uint32_t stree_next_epoch(void) { return g_stree_epoch++; }

/* ── SubstNode lifecycle ───────────────────────────────────────────────── */

SubstNode *snode_new(void) {
    SubstNode *n = cetta_malloc(sizeof(SubstNode));
    memset(n, 0, sizeof(SubstNode));
    return n;
}

void snode_free(SubstNode *n) {
    if (!n) return;
    for (uint32_t i = 0; i < n->nsym; i++) snode_free(n->sym[i].child);
    free(n->sym);
    for (uint32_t i = 0; i < n->nvars; i++) snode_free(n->vars[i].child);
    free(n->vars);
    for (uint32_t i = 0; i < n->nexpr; i++) snode_free(n->expr[i].child);
    free(n->expr);
    for (uint32_t i = 0; i < n->nints; i++) snode_free(n->ints[i].child);
    free(n->ints);
    free(n->leaves);
    free(n);
}

/* ── Branch helpers ────────────────────────────────────────────────────── */

static SubstNode *snode_get_sym(SubstNode *n, SymbolId key) {
    for (uint32_t i = 0; i < n->nsym; i++)
        if (n->sym[i].key == key)
            return n->sym[i].child;
    if (n->nsym >= n->csym) {
        n->csym = n->csym ? n->csym * 2 : 4;
        n->sym = cetta_realloc(n->sym, sizeof(n->sym[0]) * n->csym);
    }
    SubstNode *child = snode_new();
    n->sym[n->nsym].key = key;
    n->sym[n->nsym].child = child;
    n->nsym++;
    return child;
}

static SubstNode *snode_get_var(SubstNode *n, VarId var_id, SymbolId spelling) {
    for (uint32_t i = 0; i < n->nvars; i++)
        if (n->vars[i].var_id == var_id) return n->vars[i].child;
    if (n->nvars >= n->cvars) {
        n->cvars = n->cvars ? n->cvars * 2 : 4;
        n->vars = cetta_realloc(n->vars, sizeof(n->vars[0]) * n->cvars);
    }
    SubstNode *child = snode_new();
    n->vars[n->nvars].var_id = var_id;
    n->vars[n->nvars].spelling = spelling;
    n->vars[n->nvars].child = child;
    n->nvars++;
    return child;
}

static SubstNode *snode_get_expr(SubstNode *n, uint32_t arity) {
    for (uint32_t i = 0; i < n->nexpr; i++)
        if (n->expr[i].arity == arity) return n->expr[i].child;
    if (n->nexpr >= n->cexpr) {
        n->cexpr = n->cexpr ? n->cexpr * 2 : 4;
        n->expr = cetta_realloc(n->expr, sizeof(n->expr[0]) * n->cexpr);
    }
    SubstNode *child = snode_new();
    n->expr[n->nexpr].arity = arity;
    n->expr[n->nexpr].child = child;
    n->nexpr++;
    return child;
}

static SubstNode *snode_get_int(SubstNode *n, int64_t val) {
    for (uint32_t i = 0; i < n->nints; i++)
        if (n->ints[i].val == val) return n->ints[i].child;
    if (n->nints >= n->cints) {
        n->cints = n->cints ? n->cints * 2 : 4;
        n->ints = cetta_realloc(n->ints, sizeof(n->ints[0]) * n->cints);
    }
    SubstNode *child = snode_new();
    n->ints[n->nints].val = val;
    n->ints[n->nints].child = child;
    n->nints++;
    return child;
}

static void snode_add_leaf(SubstNode *n, uint32_t idx, uint32_t epoch) {
    if (n->nleaves >= n->cleaves) {
        n->cleaves = n->cleaves ? n->cleaves * 2 : 4;
        n->leaves = cetta_realloc(n->leaves, sizeof(n->leaves[0]) * n->cleaves);
    }
    n->leaves[n->nleaves].idx = idx;
    n->leaves[n->nleaves].epoch = epoch;
    n->nleaves++;
}

/* ── Insertion ─────────────────────────────────────────────────────────── */

static SubstNode *snode_insert_atom(SubstNode *node, Atom *a) {
    switch (a->kind) {
    case ATOM_SYMBOL: return snode_get_sym(node, a->sym_id);
    case ATOM_VAR:    return snode_get_var(node, a->var_id, a->sym_id);
    case ATOM_GROUNDED:
        if (a->ground.gkind == GV_INT) return snode_get_int(node, a->ground.ival);
        if (a->ground.gkind == GV_STRING) {
            SymbolId string_id = symbol_intern_cstr(g_symbols, a->ground.sval);
            return snode_get_sym(node, string_id);
        }
        return snode_get_var(node,
                             g_var_intern ? var_intern(g_var_intern,
                                                       symbol_intern_cstr(g_symbols, "__grounded__"))
                                          : fresh_var_id(),
                             symbol_intern_cstr(g_symbols, "__grounded__"));
    case ATOM_EXPR: {
        SubstNode *cur = snode_get_expr(node, a->expr.len);
        for (uint32_t i = 0; i < a->expr.len; i++)
            cur = snode_insert_atom(cur, a->expr.elems[i]);
        return cur;
    }
    }
    return node;
}

/* ── SubstTree (head-partitioned) ──────────────────────────────────────── */

void stree_init(SubstTree *t) {
    for (uint32_t i = 0; i < STREE_BUCKETS; i++)
        stree_bucket_init(&t->buckets[i]);
    stree_bucket_init(&t->wildcard);
}

void stree_free(SubstTree *t) {
    for (uint32_t i = 0; i < STREE_BUCKETS; i++)
        stree_bucket_free(&t->buckets[i]);
    stree_bucket_free(&t->wildcard);
}

uint32_t stree_head_hash(SymbolId id) {
    uint32_t mixed = (uint32_t)((uint64_t)id * 2654435761u);
    return mixed % STREE_BUCKETS;
}

static SymbolId atom_head_sym(Atom *a) {
    if (a->kind == ATOM_SYMBOL) return a->sym_id;
    if (a->kind == ATOM_EXPR && a->expr.len > 0 &&
        a->expr.elems[0]->kind == ATOM_SYMBOL)
        return a->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

void stree_bucket_init(SubstBucket *bucket) {
    bucket->root = NULL;
    bucket->count = 0;
}

void stree_bucket_free(SubstBucket *bucket) {
    snode_free(bucket->root);
    bucket->root = NULL;
    bucket->count = 0;
}

void stree_bucket_insert(SubstBucket *bucket, Atom *atom, uint32_t atom_idx) {
    uint32_t epoch = stree_next_epoch();
    if (!bucket->root) bucket->root = snode_new();
    SubstNode *leaf = snode_insert_atom(bucket->root, atom);
    snode_add_leaf(leaf, atom_idx, epoch);
    bucket->count++;
}

void stree_insert(SubstTree *t, Atom *atom, uint32_t atom_idx) {
    SymbolId head = atom_head_sym(atom);
    SubstBucket *bucket = head != SYMBOL_ID_NONE
                        ? &t->buckets[stree_head_hash(head)]
                        : &t->wildcard;
    stree_bucket_insert(bucket, atom, atom_idx);
}

/* ── Result Set ────────────────────────────────────────────────────────── */

void smset_init(SubstMatchSet *s) { s->items = NULL; s->len = 0; s->cap = 0; }

void smset_free(SubstMatchSet *s) {
    for (uint32_t i = 0; i < s->len; i++)
        bindings_free(&s->items[i].bindings);
    free(s->items);
    s->items = NULL;
    s->len = 0;
    s->cap = 0;
}

static void smset_push(SubstMatchSet *s, uint32_t atom_idx, uint32_t epoch, Bindings *b) {
    if (s->len >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->items = cetta_realloc(s->items, sizeof(SubstMatch) * s->cap);
    }
    s->items[s->len].atom_idx = atom_idx;
    s->items[s->len].epoch = epoch;
    if (!bindings_clone(&s->items[s->len].bindings, b))
        return;
    s->items[s->len].exact = false;
    s->len++;
}

/* ── Retrieval ─────────────────────────────────────────────────────────── *
 *
 * Phase 1 strategy (simple, correct, no continuation-passing):
 *
 * Use the SubstTree as a BETTER DiscNode — the tree walk filters
 * candidates and produces INDEXED-SIDE variable bindings. But for
 * query-side variables, we still fall back to extracting sub-atoms
 * from the original atom at leaf time.
 *
 * This already eliminates rename_vars (epoch-tagging instead) and
 * produces partial bindings during the walk (no separate match_atoms
 * for indexed variables). The remaining match_atoms work is only for
 * query variables hitting indexed expressions — resolved at the leaf.
 *
 * Phase 2 would eliminate even that by doing the full walk without
 * any leaf-time extraction.                                              */

/* Flat token for depth-first query encoding */
#define MAX_FLAT 256

typedef struct {
    enum { FT_SYM, FT_VAR, FT_EXPR, FT_INT, FT_GROUNDED_OTHER } kind;
    union { SymbolId sym_id; uint32_t arity; int64_t ival; };
    VarId var_id;
    Atom *original;
} FlatToken;

/* Forward declarations for mutual recursion */
static void st_flat_walk(SubstNode *node, FlatToken *flat, uint32_t nflat,
                         uint32_t idx, Bindings *b, Arena *a,
                         Atom **atoms, SubstMatchSet *out);
static void st_flat_skip(SubstNode *node, uint32_t remaining,
                         FlatToken *flat, uint32_t nflat, uint32_t resume_idx,
                         Bindings *b, Arena *a, Atom **atoms,
                         SubstMatchSet *out);

/* Collect leaves: epoch-tag variable names, check occurs, push results */
static void st_collect(SubstNode *node, Bindings *b, Arena *a,
                       Atom **atoms, SubstMatchSet *out) {
    (void)atoms;
    for (uint32_t li = 0; li < node->nleaves; li++) {
        Bindings tagged;
        if (!bindings_clone(&tagged, b))
            continue;
        uint32_t epoch = node->leaves[li].epoch;
        /* Epoch-tag indexed-side variable ids */
        for (uint32_t bi = 0; bi < tagged.len; bi++) {
            tagged.entries[bi].var_id = var_epoch_id(tagged.entries[bi].var_id, epoch);
        }
        if (!bindings_has_loop(&tagged))
            smset_push(out, node->leaves[li].idx, epoch, &tagged);
        bindings_free(&tagged);
    }
}

/* ── Flat-sequence retrieval ──────────────────────────────────────────── */

/* Flatten query atom depth-first into tokens */
static uint32_t flatten_atom(Atom *a, FlatToken *buf, uint32_t pos) {
    if (pos >= MAX_FLAT) return pos;
    switch (a->kind) {
    case ATOM_SYMBOL:
        buf[pos] = (FlatToken){.kind = FT_SYM, .sym_id = a->sym_id, .original = a};
        return pos + 1;
    case ATOM_VAR:
        buf[pos] = (FlatToken){.kind = FT_VAR, .sym_id = a->sym_id,
                               .var_id = a->var_id, .original = a};
        return pos + 1;
    case ATOM_GROUNDED:
        if (a->ground.gkind == GV_INT) {
            buf[pos] = (FlatToken){.kind = FT_INT, .ival = a->ground.ival, .original = a};
            return pos + 1;
        }
        if (a->ground.gkind == GV_STRING) {
            SymbolId string_id = symbol_intern_cstr(g_symbols, a->ground.sval);
            buf[pos] = (FlatToken){.kind = FT_SYM, .sym_id = string_id, .original = a};
            return pos + 1;
        }
        buf[pos] = (FlatToken){.kind = FT_GROUNDED_OTHER, .original = a};
        return pos + 1;
    case ATOM_EXPR:
        buf[pos] = (FlatToken){.kind = FT_EXPR, .arity = a->expr.len, .original = a};
        pos++;
        for (uint32_t i = 0; i < a->expr.len && pos < MAX_FLAT; i++)
            pos = flatten_atom(a->expr.elems[i], buf, pos);
        return pos;
    }
    return pos;
}

/* Walk flat token sequence through the tree. idx = current position in flat[].
   When idx == nflat, we've matched the full query — collect leaves. */
static void st_flat_walk(SubstNode *node, FlatToken *flat, uint32_t nflat,
                         uint32_t idx, Bindings *b, Arena *a,
                         Atom **atoms, SubstMatchSet *out) {
    if (!node) return;
    if (idx == nflat) {
        st_collect(node, b, a, atoms, out);
        return;
    }

    FlatToken *tok = &flat[idx];

    switch (tok->kind) {
    case FT_SYM:
        /* Exact symbol match */
        for (uint32_t i = 0; i < node->nsym; i++)
            if (node->sym[i].key == tok->sym_id)
                st_flat_walk(node->sym[i].child, flat, nflat, idx+1,
                             b, a, atoms, out);
        /* Indexed variable matches query symbol */
        for (uint32_t i = 0; i < node->nvars; i++) {
            Bindings b2;
            if (!bindings_clone(&b2, b))
                continue;
            if (bindings_add_id(&b2, node->vars[i].var_id, node->vars[i].spelling,
                                tok->original))
                st_flat_walk(node->vars[i].child, flat, nflat, idx+1,
                             &b2, a, atoms, out);
            bindings_free(&b2);
        }
        break;

    case FT_VAR:
        /* Query variable matches ANY indexed sub-term.
           Skip one sub-term in the trie, then resume flat walk at idx+1 */
        {
            /* Skip one indexed sub-term, then resume flat walk at idx+1 */
            /* For each branch: one trie step forward = skip one atomic token */
            for (uint32_t i = 0; i < node->nsym; i++)
                st_flat_walk(node->sym[i].child, flat, nflat, idx+1,
                             b, a, atoms, out);
            for (uint32_t i = 0; i < node->nvars; i++)
                st_flat_walk(node->vars[i].child, flat, nflat, idx+1,
                             b, a, atoms, out);
            for (uint32_t i = 0; i < node->nints; i++)
                st_flat_walk(node->ints[i].child, flat, nflat, idx+1,
                             b, a, atoms, out);
            /* Expression branches: skip arity MORE tokens after arity branch */
            for (uint32_t i = 0; i < node->nexpr; i++)
                st_flat_skip(node->expr[i].child, node->expr[i].arity,
                             flat, nflat, idx+1, b, a, atoms, out);
        }
        break;

    case FT_INT:
        for (uint32_t i = 0; i < node->nints; i++)
            if (node->ints[i].val == tok->ival)
                st_flat_walk(node->ints[i].child, flat, nflat, idx+1,
                             b, a, atoms, out);
        for (uint32_t i = 0; i < node->nvars; i++) {
            Bindings b2;
            if (!bindings_clone(&b2, b))
                continue;
            if (bindings_add_id(&b2, node->vars[i].var_id, node->vars[i].spelling,
                                tok->original))
                st_flat_walk(node->vars[i].child, flat, nflat, idx+1,
                             &b2, a, atoms, out);
            bindings_free(&b2);
        }
        break;

    case FT_EXPR:
        /* Match arity, continue with children (already flat in the sequence) */
        for (uint32_t i = 0; i < node->nexpr; i++)
            if (node->expr[i].arity == tok->arity)
                st_flat_walk(node->expr[i].child, flat, nflat, idx+1,
                             b, a, atoms, out);
        /* Indexed variable matches query expression: bind it, skip the expression tokens */
        for (uint32_t i = 0; i < node->nvars; i++) {
            Bindings b2;
            if (!bindings_clone(&b2, b))
                continue;
            if (bindings_add_id(&b2, node->vars[i].var_id, node->vars[i].spelling,
                                tok->original)) {
                /* Skip past all tokens of this expression */
                uint32_t skip_end = idx + 1;
                uint32_t depth = tok->arity;
                while (depth > 0 && skip_end < nflat) {
                    if (flat[skip_end].kind == FT_EXPR) depth += flat[skip_end].arity;
                    depth--;
                    skip_end++;
                }
                st_flat_walk(node->vars[i].child, flat, nflat, skip_end,
                             &b2, a, atoms, out);
            }
            bindings_free(&b2);
        }
        break;

    case FT_GROUNDED_OTHER:
        /* Treat as wildcard — match any indexed variable */
        for (uint32_t i = 0; i < node->nvars; i++) {
            Bindings b2;
            if (!bindings_clone(&b2, b))
                continue;
            if (bindings_add_id(&b2, node->vars[i].var_id, node->vars[i].spelling,
                                tok->original))
                st_flat_walk(node->vars[i].child, flat, nflat, idx+1,
                             &b2, a, atoms, out);
            bindings_free(&b2);
        }
        break;
    }
}

/* Skip `remaining` indexed sub-terms in the flat walk, then resume */
static void st_flat_skip(SubstNode *node, uint32_t remaining,
                         FlatToken *flat, uint32_t nflat, uint32_t resume_idx,
                         Bindings *b, Arena *a, Atom **atoms,
                         SubstMatchSet *out) {
    if (!node) return;
    if (remaining == 0) {
        st_flat_walk(node, flat, nflat, resume_idx, b, a, atoms, out);
        return;
    }
    for (uint32_t i = 0; i < node->nsym; i++)
        st_flat_skip(node->sym[i].child, remaining - 1,
                     flat, nflat, resume_idx, b, a, atoms, out);
    for (uint32_t i = 0; i < node->nvars; i++)
        st_flat_skip(node->vars[i].child, remaining - 1,
                     flat, nflat, resume_idx, b, a, atoms, out);
    for (uint32_t i = 0; i < node->nints; i++)
        st_flat_skip(node->ints[i].child, remaining - 1,
                     flat, nflat, resume_idx, b, a, atoms, out);
    for (uint32_t i = 0; i < node->nexpr; i++)
        st_flat_skip(node->expr[i].child, remaining - 1 + node->expr[i].arity,
                     flat, nflat, resume_idx, b, a, atoms, out);
}

/* ── Bucket-level query ────────────────────────────────────────────────── */

void stree_query_bucket(SubstBucket *bucket, Arena *a, Atom *query,
                        Atom **space_atoms, SubstMatchSet *out) {
    if (!bucket->root || bucket->count == 0) return;
    FlatToken flat[MAX_FLAT];
    uint32_t nflat = flatten_atom(query, flat, 0);
    Bindings b;
    bindings_init(&b);
    st_flat_walk(bucket->root, flat, nflat, 0, &b, a, space_atoms, out);
    bindings_free(&b);
}
