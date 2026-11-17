/* tcalc_core.c - the transform calculus, version tcalc@0.1.
 *
 * tcalc is a moving target: a family of transform steps that differ in a few
 * choices, of which each implementation is one point (tcalc_core.h).  This file
 * reaches the points where rules sit beside one store of knowledge.
 *
 * The definition is the Lean development RuleSyntax (Mettapedia/Languages/
 * ProcessCalculi/ConstraintMultisetRewriting), at one frame, with first-order
 * terms and equations as knowledge:
 *
 *   state   a bag of rows, each row an occurrence with an identity of its own,
 *           and the knowledge, a set of equations          (Core.lean: State)
 *   rule    take patterns, read patterns, tells (equations), checks (an
 *           equation, an absence, a count) and made rows   (Core.lean: Rule)
 *   firing  the rule opened apart from the state (its unknowns renamed fresh)
 *           and a selection of rows for its take and read patterns
 *   admits  the selection is enabled: the taken rows are present and distinct,
 *           the read rows are present and none is taken; the information (the
 *           knowledge, each pattern equal to its row, the tells) is solvable;
 *           every check holds at every solution of the information
 *                                                        (Core.lean: Firing.Admits)
 *   result  the taken rows leave, the made rows arrive with new identities, and
 *           the information becomes the knowledge         (Core.lean: Firing.result)
 *
 * What one step commits is a discipline over bags of such matches
 * (RuleSyntax/JointFirings); see section 6.  Which discipline runs, whether the
 * checks see the firing's own tells, and whether the store is a multiset or a
 * set are all chosen by the caller (TcInterpretation).
 *
 * Two facts of this frame keep the code small.
 *  - Knowledge made only of equations, when solvable, is its most general
 *    unifier.  A state keeps its rows with that unifier applied and no other
 *    record of the knowledge: an unknown no row mentions can never matter again,
 *    since rules mention only their own unknowns.
 *  - "At every solution" is decided at the most general solution (the solver
 *    licence of the first-order frame): an equation holds everywhere when both
 *    sides are equal under it; a row can match a pattern at some solution
 *    exactly when the two still unify under it.  So an absence holds when no
 *    present row unifies with the pattern, and a count of N holds when exactly N
 *    rows equal the pattern and no other row unifies with it.
 *
 * States are taken up to the identities of their rows and the names of their
 * unknowns; a canonical form (section 3) decides when two states are one.  No
 * size is capped: the caller's budget bounds the work, and running out of it is
 * reported, never mistaken for an answer. */

#include "tcalc_core.h"
#include "tcalc_records.h"
#include "tcalc_lists.generated.h"
#include "tcalc_flat_layout.h"
#include "../kernel/kernel.h"
#include "../kernel/batch.h"
#include "../kernel/tcalc_bridge.h"
#include "../../src/runner.h"   /* the budget, the schedule and the exploration, shared with rho */

#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * 1. Terms
 * ====================================================================== */

struct TcTerms {
    TcNode *node;   size_t nodes, node_cap;    /* node[0] stands for no term */
    uint32_t *item; size_t items, item_cap;    /* expression items           */
    char *byte;     size_t bytes, byte_cap;    /* symbol and string bytes    */
    uint32_t *slot; size_t slots;              /* hash index: term ids, 0 empty */
    uint32_t *seen; TcTerm *seen_value; size_t seen_cap; uint32_t walk; /* see "Walks" */
    TcTerm *decided; size_t decided_len, decided_cap;  /* the rows the last tc_decide made */
    bool failed;                               /* an allocation failed       */
};

static uint32_t hash_mix(uint32_t h, uint64_t v) {
    v ^= v >> 33; v *= UINT64_C(0xff51afd7ed558ccd); v ^= v >> 33;
    return (h ^ (uint32_t)v ^ (uint32_t)(v >> 32)) * 16777619u;
}

static const TcNode *node_of(const TcTerms *t, TcTerm id) { return &t->node[id]; }

/* ---- Element access --------------------------------------------------------
 * The core's growable arrays are its storage.  The functions that keep them
 * (interning, growth, the tables and the orders) index them directly; every
 * other function reads and writes them only through these accessors, by value.
 * tcalc_core.cesta checks those functions for ownership and states the bounds
 * these accessors rely on. */
static inline TcNode term_node(const TcTerms *t, TcTerm id) { return t->node[id]; }
static inline TcTerm term_item(const TcTerms *t, size_t at) { return t->item[at]; }

static bool same_node(const TcTerms *t, TcTerm id, const TcNode *probe,
                      const uint32_t *items, const char *bytes) {
    const TcNode *n = node_of(t, id);
    if (n->kind != probe->kind || n->length != probe->length || n->value != probe->value)
        return false;
    if (n->length == 0) return true;
    if (n->kind == TC_EXPRESSION)
        return memcmp(t->item + n->start, items, n->length * sizeof *items) == 0;
    if (n->kind == TC_SYMBOL || n->kind == TC_STRING)
        return memcmp(t->byte + n->start, bytes, n->length) == 0;
    return true;
}

static bool grow_index(TcTerms *t) {
    size_t slots = t->slots ? t->slots * 2 : 1024;
    uint32_t *slot = calloc(slots, sizeof *slot);
    if (!slot) return false;
    for (size_t id = 1; id < t->nodes; id++) {
        size_t i = t->node[id].hash & (slots - 1);
        while (slot[i]) i = (i + 1) & (slots - 1);
        slot[i] = (uint32_t)id;
    }
    free(t->slot);
    t->slot = slot;
    t->slots = slots;
    return true;
}

/* The one term equal to the probe, made if it does not exist yet. */
static TcTerm intern(TcTerms *t, TcNode probe, const uint32_t *items, const char *bytes) {
    if (t->failed) return 0;
    uint32_t h = hash_mix(hash_mix(probe.kind, (uint64_t)probe.value), probe.length);
    for (uint32_t i = 0; i < probe.length; i++)
        h = hash_mix(h, probe.kind == TC_EXPRESSION ? items[i] : (uint8_t)bytes[i]);
    probe.hash = h;
    if ((t->nodes + 1) * 2 > t->slots && !grow_index(t)) { t->failed = true; return 0; }
    size_t i = h & (t->slots - 1);
    for (; t->slot[i]; i = (i + 1) & (t->slots - 1))
        if (t->node[t->slot[i]].hash == h && same_node(t, t->slot[i], &probe, items, bytes))
            return t->slot[i];
    if (probe.kind == TC_EXPRESSION) {
        probe.start = (uint32_t)t->items;
        for (uint32_t k = 0; k < probe.length; k++)
            if (!tc_push_u32(realloc, &t->item, &t->items, &t->item_cap, items[k])) {
                t->failed = true; return 0;
            }
    } else if (probe.kind == TC_SYMBOL || probe.kind == TC_STRING) {
        probe.start = (uint32_t)t->bytes;
        for (uint32_t k = 0; k < probe.length; k++)
            if (!tc_push_char(realloc, &t->byte, &t->bytes, &t->byte_cap, bytes[k])) {
                t->failed = true; return 0;
            }
    }
    TcTerm id = (TcTerm)t->nodes;
    if (!tc_push_node(realloc, &t->node, &t->nodes, &t->node_cap, probe)) {
        t->failed = true; return 0;
    }
    t->slot[i] = id;
    return id;
}

TcTerms *tc_terms_new(void) {
    TcTerms *t = calloc(1, sizeof *t);
    TcNode none = {0};
    if (!t) return NULL;
    *t = (TcTerms){0};
    TcNode *node = realloc(NULL, 8 * sizeof *node);   /* the generated worklists' first capacity */
    if (!node) { tc_terms_free(t); return NULL; }
    node[0] = none;
    uint32_t *slot = calloc(1024, sizeof *slot);      /* grow_index's first table */
    if (!slot) { free(node); tc_terms_free(t); return NULL; }
    *t = (TcTerms){.node = node, .nodes = 1, .node_cap = 8,
                   .slot = slot, .slots = 1024};
    return t;
}

/* Releases the table and every pool it owns. */
static void terms_release(TcTerms *t) {
    free(t->node); free(t->item); free(t->byte); free(t->slot);
    free(t->seen); free(t->seen_value); free(t->decided); free(t);
}

void tc_terms_free(TcTerms *t) { if (t) terms_release(t); }

/* Walks.  Terms are shared, so a term can be exponentially larger read as a
 * tree than stored as a graph: (g X X), with X itself (g Y Y), and so on.  A
 * walk over a term therefore records what it found for each subterm and meets
 * every subterm once.  walk_begin starts a walk; a record made in an earlier
 * walk is stale.  The record is only a shortcut: meeting a subterm again would
 * find the same result (every variable in it already met), so a record that
 * cannot be made costs time, never a different answer. */
static void walk_begin(TcTerms *t) {
    if (++t->walk == 0) {   /* the counter wrapped: forget every record */
        if (t->seen) memset(t->seen, 0, t->seen_cap * sizeof *t->seen);
        t->walk = 1;
    }
}

static bool walk_seen(const TcTerms *t, TcTerm id, TcTerm *value) {
    if (id >= t->seen_cap || t->seen[id] != t->walk) return false;
    *value = t->seen_value[id];
    return true;
}

static void walk_record(TcTerms *t, TcTerm id, TcTerm value) {
    if (id >= t->seen_cap) {
        size_t cap = t->seen_cap ? t->seen_cap : 1024;
        while (cap <= id) cap *= 2;
        uint32_t *seen = realloc(t->seen, cap * sizeof *seen);
        if (!seen) return;
        t->seen = seen;
        TcTerm *value_of = realloc(t->seen_value, cap * sizeof *value_of);
        if (!value_of) return;
        t->seen_value = value_of;
        memset(t->seen + t->seen_cap, 0, (cap - t->seen_cap) * sizeof *t->seen);
        t->seen_cap = cap;
    }
    t->seen[id] = t->walk;
    t->seen_value[id] = value;
}

/* The interner is never handed a null pointer: a term without items or bytes
 * points at these. */
static const uint32_t no_items[1];
static const char no_bytes[1];
static const TcCheck no_checks[1];

static TcTerm leaf(TcTerms *t, TcKind kind, int64_t value, bool ground) {
    TcNode probe = {.kind = (uint8_t)kind, .ground = ground, .value = value};
    return intern(t, probe, no_items, no_bytes);
}

TcTerm tc_symbol(TcTerms *t, const char *bytes, size_t length) {
    TcNode probe = {.kind = TC_SYMBOL, .ground = 1, .length = (uint32_t)length};
    if (!bytes) return intern(t, probe, no_items, no_bytes);
    return intern(t, probe, no_items, bytes);
}

TcTerm tc_string(TcTerms *t, const char *bytes, size_t length) {
    TcNode probe = {.kind = TC_STRING, .ground = 1, .length = (uint32_t)length};
    if (!bytes) return intern(t, probe, no_items, no_bytes);
    return intern(t, probe, no_items, bytes);
}

TcTerm tc_integer(TcTerms *t, int64_t value) { return leaf(t, TC_INTEGER, value, true); }
TcTerm tc_local(TcTerms *t, uint32_t index) { return leaf(t, TC_LOCAL, index, false); }
TcTerm tc_unknown(TcTerms *t, uint32_t index) { return leaf(t, TC_UNKNOWN, index, false); }

TcTerm tc_expression(TcTerms *t, const TcTerm *items, uint32_t count) {
    TcNode probe = {.kind = TC_EXPRESSION, .ground = 1, .length = count};
    for (uint32_t i = 0; i < count; i++) {
        if (!items[i]) return 0;
        if (!t->node[items[i]].ground) probe.ground = 0;
    }
    if (count > 0 && t->node[items[0]].kind == TC_SYMBOL) probe.head = items[0];
    return intern(t, probe, count ? items : no_items, no_bytes);
}

TcKind tc_kind(const TcTerms *t, TcTerm id) { return (TcKind)node_of(t, id)->kind; }
uint32_t tc_length(const TcTerms *t, TcTerm id) {
    return node_of(t, id)->kind == TC_EXPRESSION ? node_of(t, id)->length : 0;
}
TcTerm tc_item(const TcTerms *t, TcTerm id, uint32_t i) {
    return t->item[node_of(t, id)->start + i];
}
const char *tc_bytes(const TcTerms *t, TcTerm id, size_t *length) {
    *length = node_of(t, id)->length;
    return t->byte + node_of(t, id)->start;
}
int64_t tc_value(const TcTerms *t, TcTerm id) { return node_of(t, id)->value; }
uint32_t tc_index(const TcTerms *t, TcTerm id) { return (uint32_t)node_of(t, id)->value; }

void tc_print(const TcTerms *t, TcTerm id, FILE *out) {
    const TcNode *n = node_of(t, id);
    switch ((TcKind)n->kind) {
    case TC_SYMBOL:  fwrite(t->byte + n->start, 1, n->length, out); return;
    case TC_INTEGER: fprintf(out, "%lld", (long long)n->value); return;
    case TC_LOCAL:   fprintf(out, "$%lld", (long long)n->value); return;
    case TC_UNKNOWN: fprintf(out, "?%lld", (long long)n->value); return;
    case TC_STRING:
        fputc('"', out);
        for (uint32_t i = 0; i < n->length; i++) {
            char c = t->byte[n->start + i];
            if (c == '"' || c == '\\') fputc('\\', out);
            fputc(c, out);
        }
        fputc('"', out);
        return;
    case TC_EXPRESSION:
        fputc('(', out);
        for (uint32_t i = 0; i < n->length; i++) {
            if (i) fputc(' ', out);
            tc_print(t, t->item[n->start + i], out);
        }
        fputc(')', out);
        return;
    }
}

/* ======================================================================
 * 2. Solutions under construction
 *
 * One solver holds the values chosen so far: for the locals of the rules being
 * matched (each match has its own frame of local slots, starting at its base)
 * and for the unknowns of the state.  Every assignment is recorded on a trail,
 * so that a failed attempt is undone exactly.  An unknown's value never
 * mentions a local: a local that would flow into an unknown is first opened to
 * a fresh unknown of its own (the opening of Core.lean, done on demand).
 * ====================================================================== */

typedef struct TcKernelSolver {
    TcTerm *local;   size_t local_cap;     /* slot -> value, 0 = unassigned   */
    TcTerm *unknown; size_t unknown_cap;   /* index -> value, 0 = unassigned  */
    uint32_t unknowns;                     /* indices in use                  */
    uint32_t *trail; size_t trail_len, trail_cap; /* (index << 1) | is-local  */
    uint32_t *work;  size_t work_len, work_cap;   /* unification work list    */
    TcTerm *build;   size_t build_len, build_cap; /* items of the expressions being remade */
    bool failed;
} Solver;

typedef struct { size_t trail; uint32_t unknowns; } Mark;

static Mark mark(const Solver *s) { return (Mark){s->trail_len, s->unknowns}; }

static void undo(Solver *s, Mark m) {
    while (s->trail_len > m.trail) {
        uint32_t e = s->trail[--s->trail_len];
        if (e & 1) s->local[e >> 1] = 0; else s->unknown[e >> 1] = 0;
    }
    s->unknowns = m.unknowns;
}

/* Room for `count` slots of a table, cleared. */
static bool reserve(TcTerm **table, size_t *cap, size_t count) {
    if (count <= *cap) return true;
    size_t size = *cap ? *cap : 64;
    while (size < count) size *= 2;
    TcTerm *grown = realloc(*table, size * sizeof *grown);
    if (!grown) return false;
    memset(grown + *cap, 0, (size - *cap) * sizeof *grown);
    *table = grown;
    *cap = size;
    return true;
}

static bool assign(Solver *s, bool is_local, uint32_t index, TcTerm value) {
    if (!tc_push_u32(realloc, &s->trail, &s->trail_len, &s->trail_cap,
                     (index << 1) | (is_local ? 1u : 0u))) {
        s->failed = true;
        return false;
    }
    if (is_local) s->local[index] = value; else s->unknown[index] = value;
    return true;
}

static TcTerm fresh_unknown(TcTerms *terms, Solver *s) {
    if (!reserve(&s->unknown, &s->unknown_cap, (size_t)s->unknowns + 1)) {
        s->failed = true;
        return 0;
    }
    s->unknown[s->unknowns] = 0;
    return tc_unknown(terms, s->unknowns++);
}

/* The items of an expression being remade are pushed on the solver's build
 * stack; build_make interns the expression of the items pushed since `from`
 * and pops them.  0 when memory ran out. */
static bool build_push(Solver *s, TcTerm item) {
    if (tc_push_u32(realloc, &s->build, &s->build_len, &s->build_cap, item)) return true;
    s->failed = true;
    return false;
}

static TcTerm build_make(TcTerms *terms, Solver *s, size_t from) {
    TcTerm out = tc_expression(terms, s->build + from, (uint32_t)(s->build_len - from));
    s->build_len = from;
    return out;
}

/* The value of a local slot, of an unknown, and an entry of the work list. */
static inline TcTerm local_at(const Solver *s, size_t slot) { return s->local[slot]; }
static inline TcTerm unknown_at(const Solver *s, size_t index) { return s->unknown[index]; }
static inline uint32_t work_at(const Solver *s, size_t i) { return s->work[i]; }

/* Follows assignments until an unassigned variable or a non-variable. */
static TcTerm walk(const TcTerms *terms, const Solver *s, TcTerm t, uint32_t base) {
    for (TcTerm v = t; v;) {
        t = v;
        TcNode n = term_node(terms, t);
        v = n.kind == TC_LOCAL ? local_at(s, base + (size_t)n.value)
          : n.kind == TC_UNKNOWN ? unknown_at(s, (size_t)n.value) : 0;
    }
    return t;
}

/* The term with every assignment applied; unassigned locals stay as written. */
static TcTerm resolve_from(TcTerms *terms, Solver *s, TcTerm t, uint32_t base) {
    TcTerm done = 0;
    if (walk_seen(terms, t, &done)) return done;
    TcTerm at = walk(terms, s, t, base);
    TcNode n = term_node(terms, at);
    if (n.ground || n.kind != TC_EXPRESSION) return at;
    size_t from = s->build_len;
    for (uint32_t i = 0; i < n.length; i++)
        if (!build_push(s, resolve_from(terms, s, term_item(terms, n.start + i), base))) return 0;
    TcTerm out = build_make(terms, s, from);
    if (out) walk_record(terms, t, out);
    return out;
}

static bool tc_kernel_output(TcTerms *,Solver *,const TcTerm *,size_t,uint32_t,bool,TcTerm *);

static TcTerm resolve(TcTerms *terms, Solver *s, TcTerm t, uint32_t base) {
    if (term_node(terms,t).ground) return t;
    if (cetta_kernel_requested()==CETTA_KERNEL_X86) {
        TcTerm result=0;
        return tc_kernel_output(terms,s,&t,1,base,false,&result) ? result : 0;
    }
    if (cetta_kernel_note_c) cetta_kernel_note_c();
    walk_begin(terms);
    return resolve_from(terms, s, t, base);
}

/* As resolve, and every unassigned local is opened to a fresh unknown.  (Nodes
 * are read by value: the table may move when a term is made.) */
static TcTerm close_from(TcTerms *terms, Solver *s, TcTerm t, uint32_t base) {
    TcTerm done = 0;
    if (walk_seen(terms, t, &done)) return done;
    TcTerm at = walk(terms, s, t, base);
    TcNode n = term_node(terms, at);
    if (n.ground || n.kind == TC_UNKNOWN) return at;
    if (n.kind == TC_LOCAL) {   /* met again, it walks to this unknown */
        uint32_t slot = base + (uint32_t)n.value;
        TcTerm u = fresh_unknown(terms, s);
        return u && assign(s, true, slot, u) ? u : 0;
    }
    size_t from = s->build_len;
    for (uint32_t i = 0; i < n.length; i++)
        if (!build_push(s, close_from(terms, s, term_item(terms, n.start + i), base))) return 0;
    TcTerm out = build_make(terms, s, from);
    if (out) walk_record(terms, t, out);
    return out;
}

static TcTerm close_term(TcTerms *terms, Solver *s, TcTerm t, uint32_t base) {
    if (term_node(terms,t).ground) return t;
    if (cetta_kernel_requested()==CETTA_KERNEL_X86) {
        TcTerm result=0;
        return tc_kernel_output(terms,s,&t,1,base,true,&result) ? result : 0;
    }
    if (cetta_kernel_note_c) cetta_kernel_note_c();
    walk_begin(terms);
    return close_from(terms, s, t, base);
}

/* Whether the variable `var` (at `base`) occurs in t (at `tbase`).  A subterm
 * met before did not contain it: the search would have stopped there. */
static bool occurs_in(TcTerms *terms, const Solver *s, TcTerm var, uint32_t base, TcTerm t,
                      uint32_t tbase) {
    TcTerm done = 0;
    if (walk_seen(terms, t, &done)) return false;
    TcTerm at = walk(terms, s, t, tbase);
    TcNode n = term_node(terms, at);
    if (at == var) return n.kind == TC_UNKNOWN || tbase == base;
    if (n.ground || n.kind != TC_EXPRESSION) return false;
    for (uint32_t i = 0; i < n.length; i++)
        if (occurs_in(terms, s, var, base, term_item(terms, n.start + i), tbase)) return true;
    walk_record(terms, t, 0);
    return false;
}

static bool occurs(TcTerms *terms, const Solver *s, TcTerm var, uint32_t base, TcTerm t,
                   uint32_t tbase) {
    walk_begin(terms);
    return occurs_in(terms, s, var, base, t, tbase);
}

/* Assigns the unassigned variable v (at vbase) the term t (at tbase). */
static bool bind(TcTerms *terms, Solver *s, TcTerm v, uint32_t vbase, TcTerm t, uint32_t tbase) {
    TcNode vn = term_node(terms, v), tn = term_node(terms, t);
    bool local = vn.kind == TC_LOCAL, settled = tn.ground || tn.kind == TC_UNKNOWN;
    uint32_t index = (uint32_t)vn.value + (local ? vbase : 0);
    if (local) {
        if (settled) return assign(s, true, index, t);
        if (tbase == vbase)   /* a term of the same rule's frame */
            return !occurs(terms, s, v, vbase, t, tbase) && assign(s, true, index, t);
        TcTerm closed = close_term(terms, s, t, tbase);
        return closed && assign(s, true, index, closed);
    }
    TcTerm closed = tn.ground ? t : close_term(terms, s, t, tbase);
    if (!closed) return false;
    if (!term_node(terms, closed).ground && occurs(terms, s, v, 0, closed, 0)) return false;
    return assign(s, false, index, closed);
}

static bool push_pair(Solver *s, TcTerm a, uint32_t abase, TcTerm b, uint32_t bbase) {
    if (tc_push_u32(realloc, &s->work, &s->work_len, &s->work_cap, a) &&
        tc_push_u32(realloc, &s->work, &s->work_len, &s->work_cap, abase) &&
        tc_push_u32(realloc, &s->work, &s->work_len, &s->work_cap, b) &&
        tc_push_u32(realloc, &s->work, &s->work_len, &s->work_cap, bbase))
        return true;
    s->failed = true;
    return false;
}

/* Extends the assignments so that a (at abase) and b (at bbase) are equal, or
 * fails.  A failure may leave assignments behind: the caller undoes to a mark. */
static bool unify_c(TcTerms *terms, Solver *s, TcTerm a, uint32_t abase, TcTerm b, uint32_t bbase) {
    size_t floor = s->work_len;
    if (!push_pair(s, a, abase, b, bbase)) { s->work_len = floor; return false; }
    while (s->work_len > floor) {
        s->work_len -= 4;
        size_t top = s->work_len;
        abase = work_at(s, top + 1);
        bbase = work_at(s, top + 3);
        a = walk(terms, s, work_at(s, top), abase);
        b = walk(terms, s, work_at(s, top + 2), bbase);
        TcNode an = term_node(terms, a), bn = term_node(terms, b);
        /* one term is equal to itself unless it mentions locals of two frames */
        if (a == b && (abase == bbase || an.ground || an.kind == TC_UNKNOWN)) continue;
        bool ok;
        if (an.kind == TC_LOCAL) ok = bind(terms, s, a, abase, b, bbase);
        else if (bn.kind == TC_LOCAL) ok = bind(terms, s, b, bbase, a, abase);
        else if (an.kind == TC_UNKNOWN) ok = bind(terms, s, a, abase, b, bbase);
        else if (bn.kind == TC_UNKNOWN) ok = bind(terms, s, b, bbase, a, abase);
        else if (an.kind == TC_EXPRESSION && bn.kind == TC_EXPRESSION &&
                 an.length == bn.length) {
            ok = true;
            for (uint32_t i = an.length; ok && i-- > 0;)
                ok = push_pair(s, term_item(terms, an.start + i), abase,
                               term_item(terms, bn.start + i), bbase);
        } else ok = false;   /* distinct constants, or shapes that differ */
        if (!ok) { s->work_len = floor; return false; }
    }
    return true;
}

#include "tcalc_kernel.c"

static bool unify(TcTerms *terms,Solver *s,TcTerm a,uint32_t abase,TcTerm b,uint32_t bbase) {
    if (cetta_kernel_requested()==CETTA_KERNEL_X86) return tc_kernel_unify(terms,s,a,abase,b,bbase);
    if (cetta_kernel_note_c) cetta_kernel_note_c();
    return unify_c(terms,s,a,abase,b,bbase);
}

static void solver_free(Solver *s) {
    free(s->local); free(s->unknown); free(s->trail); free(s->work); free(s->build);
}

/* ======================================================================
 * 3. States up to identities
 *
 * A state is a bag of rows.  Its canonical form lists the rows sorted by head
 * and length (so the rows a pattern can meet are one run of the list), with its
 * unknowns renamed 0, 1, ... in order of first appearance.  Two states with
 * one canonical form are one state: the identities of rows and the names of
 * unknowns carry no meaning.  The renaming is decided by the rows' shapes with
 * unknowns blanked out, which identifies every pair of equal states except
 * some whose rows differ only in how their unknowns are shared; those are kept
 * apart (counted twice, never merged wrongly).
 *
 * Their representation, the canonical forms and the table of states, is
 * tcalc_store.c, included below with that of section 4.
 * ====================================================================== */

/* ======================================================================
 * 4. Rules and programs
 *
 * A program is a set of rules, each with its take and read patterns, its
 * tells, its checks and its made rows, the rule's unknowns numbered by first
 * appearance (RuleSyntax/Core.lean: Rule, Program).  A rule equal to another
 * up to the names of its unknowns is that rule again.  The program's pools and
 * a firing's view of a rule are in tcalc_store.c.
 * ====================================================================== */

#include "tcalc_store.c"

/* ======================================================================
 * 5. One firing: matching, admission, result
 * ====================================================================== */

/* The firings met by an exploration under step one, each by its state, its
 * rule and the row terms it selects (section 7, "Independent firings"). */
typedef struct {
    uint32_t *firing; size_t firings, firing_cap;   /* four words each: state, rule,
                                                       first term, successor */
    TcTerm *term; size_t terms, term_cap;            /* the row terms each selects */
    uint32_t *slot; size_t slots;                    /* hash index: firing + 1, 0 empty */
    uint32_t *first; size_t firsts;                  /* state -> 1 + the firing that
                                                        first reached it, 0 none */
} Fired;

/* The work of one exploration.  It holds the program's term table for its
 * duration (tc_explore and tc_successors hand it over and take it back), a
 * view of the program and a copy of the interpretation, so that the matching
 * below reaches every array it uses through the explorer. */
typedef struct {
    TcTerms *terms;
    ProgramView program;
    TcInterpretation how;
    Solver solver;
    StateTable states;
    /* the state being expanded: its rows, borrowed from the state table;
     * whether they mention no unknown; its index (UINT32_MAX outside the
     * exploration) */
    const TcTerm *row; uint32_t rows;
    bool ground;
    uint32_t at;
    /* step one: the firings met, kept while `derive` (below); and whether a
     * shortcut, checked, ever found another successor than the full one */
    Fired fired;
    bool derive, disagreed;
    TcTerm *key; size_t key_cap;   /* scratch: the row terms of the match decided */
    /* the matches at it the discipline can use (match_from) */
    TcMember *member; size_t members, member_cap;
    uint32_t *selection; size_t selections, selection_cap;
    /* those matches by their rule and the row terms they select: member index
     * + 1 per slot, 0 empty (see known_selection) */
    uint32_t *known; size_t known_cap, known_used;
    /* scratch: the rows of a successor under construction */
    TcTerm *next; size_t next_len, next_cap;
    /* the selection being chosen: a row position for each pattern */
    uint32_t *pick; size_t pick_cap;
    /* the match being decided (the protocol tc_fire): its rule, as stored and
     * as an index, and the mark of the trail before its tells */
    TcStoredRule fire; uint32_t fire_rule; Mark fire_mark;
    RunBudget budget;   /* units of work: states expanded, bags examined, orders tried */
    bool out_of_memory;
    /* backend x86-flat: each rule's packed plan, the state's rows packed four
     * words a row, and whether the decider ever gave no verdict */
    struct FlatPlan *plans; size_t plans_len;
    uint32_t *flat_words; size_t flat_words_cap;
    bool flat_failed;
    uint32_t *batch_cursor; size_t batch_cursor_cap;
    uint32_t *batch_output; size_t batch_output_cap;
    uint64_t batch_used;
    /* tc_decide: the host's store the match is decided over (0 for the
     * explorer's own states), the selection, how the store answered (ROW
     * until a cursor or an import fails) and how the host's commit went */
    TcStoreOps store;
    bool has_store;
    const TcOccurrence *selected;
    TcStoreStatus store_status;
    TcCommitStatus commit;
} Explorer;

static inline TcTerm row_at(const Explorer *x, uint32_t i) { return x->row[i]; }
static inline uint32_t pick_at(const Explorer *x, uint32_t k) { return x->pick[k]; }
static inline void pick_put(Explorer *x, uint32_t k, uint32_t row) { x->pick[k] = row; }

/* Room for a selection of `count` patterns. */
static bool ensure_pick(Explorer *x, size_t count) {
    if (count <= x->pick_cap) return true;
    uint32_t *grown = realloc(x->pick, count * sizeof *grown);
    if (!grown) return false;
    x->pick = grown;
    x->pick_cap = count;
    return true;
}

static bool next_push(Explorer *x, TcTerm row) {
    return tc_push_u32(realloc, &x->next, &x->next_len, &x->next_cap, row);
}

/* Room for the state's unknowns, 0 .. count-1, unassigned. */
static bool reserve_unknowns(Solver *s, uint32_t count) {
    s->unknowns = count;
    return reserve(&s->unknown, &s->unknown_cap, (size_t)count + 1);
}

/* One unit of work, if the budget has one left. */
static bool spend(Explorer *x) { return run_spend(&x->budget); }

/* The first of the state's sorted rows whose (head, length) is at least, or
 * with `after` beyond, the given one. */
static uint32_t first_row(const Explorer *x, uint32_t head, uint32_t length, bool after) {
    uint32_t a = 0, b = x->rows;
    while (a < b) {
        uint32_t m = a + (b - a) / 2;
        TcNode rn = term_node(x->terms, row_at(x, m));
        uint32_t rl = rn.kind == TC_EXPRESSION ? rn.length : 0;
        bool before = rn.head < head || (rn.head == head && (after ? rl <= length : rl < length));
        if (before) a = m + 1; else b = m;
    }
    return a;
}

/* The rows a pattern can meet, as two runs of the sorted rows (positions
 * lo[k] .. hi[k]-1): the rows of its head and length, and the rows with no
 * symbol at their head; or every row, when the pattern's own head is not a
 * fixed symbol. */
typedef struct { uint32_t lo[2], hi[2]; } Runs;

static Runs candidates(const Explorer *x, TcTerm pattern, uint32_t base) {
    TcTerm p = walk(x->terms, &x->solver, pattern, base);
    TcNode pn = term_node(x->terms, p);
    Runs all = {{0, 0}, {0, x->rows}};
    if (pn.kind == TC_LOCAL || pn.kind == TC_UNKNOWN) return all;
    uint32_t head = pn.head, length = pn.kind == TC_EXPRESSION ? pn.length : 0;
    if (pn.kind == TC_EXPRESSION && pn.length > 0) {
        TcTerm first = walk(x->terms, &x->solver, term_item(x->terms, pn.start), base);
        if (term_node(x->terms, first).kind != TC_SYMBOL) return all;
        head = first;
    }
    Runs r = {{0, first_row(x, head, length, false)}, {0, first_row(x, head, length, true)}};
    /* the rows with no symbol at their head, first in the order: an unknown,
     * or an expression headed by one, can meet a pattern of any head */
    if (head) r.hi[0] = first_row(x, 1, 0, false);
    return r;
}

/* Whether a check holds at every solution of what the solver now holds, over
 * the rows present (the entry state of the step).  At the most general
 * solution: a row equal to the pattern there matches it at every solution; a
 * row that only unifies with it matches at some solutions and not at others,
 * which no absence, count or census allows. */
struct TcDecideScope { Explorer *x; };

TcTerm tc_decide_unknown(TcDecideScope *scope) {
    return fresh_unknown(scope->x->terms, &scope->x->solver);
}

/* A unit of the host's work (tc_decide): false, and the decision interrupted,
 * when the host cancels. */
static bool store_spend(Explorer *x) {
    const TcStoreOps *ops = &x->store;
    if (!ops->spend || ops->spend(ops->store, 1)) return true;
    x->store_status = TC_STORE_INTERRUPTED;
    return false;
}

/* check_holds over the host's store: the rows a cursor yields for the
 * pattern, each imported.  A failure of the store is recorded and fails the
 * check, so the protocol declines and tc_decide reports the failure. */
static bool store_check_holds(Explorer *x, TcCheck c, uint32_t base, TcTerm pattern) {
    if (!x->store.open || !x->store.next || !x->store.close || !x->store.import) {
        x->store_status = TC_STORE_INVALIDATED;
        return false;
    }
    void *cursor = NULL;
    TcStoreStatus s = x->store.open(x->store.store, x->terms, pattern, &cursor);
    if (s != TC_STORE_ROW) {
        x->store_status = s == TC_STORE_END ? TC_STORE_INVALIDATED : s;
        return false;
    }
    TcDecideScope scope = {x};
    uint64_t equal = 0;
    bool holds = true;
    for (;;) {
        TcOccurrence at = 0;
        if ((s = scope.x->store.next(cursor, &at)) == TC_STORE_END) break;
        TcTerm imported = 0;
        if (s != TC_STORE_ROW || !store_spend(scope.x) ||
            (s = scope.x->store.import(scope.x->store.store, &scope, scope.x->terms, at, &imported)) != TC_STORE_ROW) {
            if (s != TC_STORE_ROW) scope.x->store_status = s;
            holds = false;
            break;
        }
        TcTerm row = resolve(scope.x->terms, &scope.x->solver, imported, 0);
        if (c.kind != TC_CHECK_ABSENT && row == pattern) { equal++; continue; }
        Mark m = mark(&scope.x->solver);
        bool meets = unify(scope.x->terms, &scope.x->solver, c.left, base, row, 0);
        undo(&scope.x->solver, m);
        if (meets) { holds = false; break; }   /* some solution makes this row match */
    }
    scope.x->store.close(cursor);
    return holds && (c.kind != TC_CHECK_COUNT || equal == c.count);
}

static bool check_holds(Explorer *x, TcCheck c, uint32_t base) {
    if (c.kind == TC_CHECK_EQUAL) {
        TcTerm left=resolve(x->terms,&x->solver,c.left,base);
        if (!left || x->solver.failed || x->terms->failed) return false;
        TcTerm right=resolve(x->terms,&x->solver,c.right,base);
        return right && !x->solver.failed && !x->terms->failed && left==right;
    }
    TcTerm pattern = resolve(x->terms, &x->solver, c.left, base);
    if (!pattern || x->solver.failed || x->terms->failed) return false;
    if (x->has_store) return store_check_holds(x, c, base, pattern);
    uint64_t equal = 0;
    Runs runs = candidates(x, c.left, base);
    for (int k = 0; k < 2; k++)
        for (uint32_t i = runs.lo[k]; i < runs.hi[k]; i++) {
            TcTerm row = resolve(x->terms, &x->solver, row_at(x, i), 0);
            if (c.kind != TC_CHECK_ABSENT && row == pattern) { equal++; continue; }
            Mark m = mark(&x->solver);
            bool meets = unify(x->terms, &x->solver, c.left, base, row, 0);
            undo(&x->solver, m);
            if (meets) return false;   /* some solution makes this row match */
        }
    return c.kind != TC_CHECK_COUNT || equal == c.count;
}

static bool checks_hold(Explorer *x, const TcStoredRule *r, uint32_t base) {
    for (uint32_t i = 0; i < r->check_count; i++)
        if (!check_holds(x, check_at(&x->program, r, i), base)) return false;
    return true;
}

static bool tells_hold(Explorer *x, const TcStoredRule *r, uint32_t base) {
    for (uint32_t i = 0; i < r->tells; i++)
        if (!unify(x->terms, &x->solver, tell_at(&x->program, r, 2 * i), base,
                   tell_at(&x->program, r, 2 * i + 1), base))
            return false;
    return true;
}

/* Admission after the matching: tells and checks in the order the
 * interpretation asks (Core.lean asks after the tells). */
static bool admitted(Explorer *x, const TcStoredRule *r, uint32_t base) {
    if (x->how.ask == TC_ASK_AFTER_TELLS) return tells_hold(x, r, base) && checks_hold(x, r, base);
    return checks_hold(x, r, base) && tells_hold(x, r, base);
}

/* Appends the made rows of a match (at base) to x->next, closed. */
static bool append_made(Explorer *x, const TcStoredRule *r, uint32_t base) {
    CettaKernelMode mode=cetta_kernel_requested();
    if (mode==CETTA_KERNEL_X86) {
        TcTerm *roots=calloc(r->makes ? r->makes : 1,sizeof *roots);
        TcTerm *made=calloc(r->makes ? r->makes : 1,sizeof *made);
        if (!roots || !made) { free(roots);free(made);x->solver.failed=true;return false; }
        for (uint32_t i=0;i<r->makes;++i) roots[i]=make_at(&x->program,r,i);
        bool ok=tc_kernel_output(x->terms,&x->solver,roots,r->makes,base,true,made);
        for (uint32_t i=0;ok && i<r->makes;++i) ok=next_push(x,made[i]);
        free(roots);free(made);return ok;
    }
    /* One lexical frame and one completed substitution: sharing this memo
     * across made roots is valid. Other bag members start their own bundle. */
    if (cetta_kernel_note_c) cetta_kernel_note_c();
    walk_begin(x->terms);
    for (uint32_t i = 0; i < r->makes; i++) {
        TcTerm made = close_from(x->terms, &x->solver, make_at(&x->program, r, i), base);
        if (!made || !next_push(x, made)) return false;
    }
    return true;
}

/* At a state with no unknowns the solver changes no row, and the state's rows
 * are in canonical order already.  So the successor's canonical rows are those
 * rows without the gone ones, merged with the made rows (x->next) put in the
 * same order: a firing costs a merge, where canonical_form would sort every
 * row.  UINT32_MAX when memory ran out. */
static uint32_t merged_successor(Explorer *x, const bool *gone) {
    uint32_t made = (uint32_t)x->next_len, m = 0;
    uint32_t *pos = malloc((made ? made : 1) * sizeof *pos);
    if (!pos) return UINT32_MAX;
    for (uint32_t i = 0; i < made; i++) pos[i] = i;
    const TcNode *nodes = x->terms->node;
    if (!nodes) { free(pos); return UINT32_MAX; }
    bool ok;
    {
        RowOrder by = {nodes, x->terms->nodes, x->next, x->next, made, false};
        ok = sort_positions(&by, pos, made);
    }
    for (uint32_t i = 0; ok && i <= x->rows; i++) {
        while (ok && m < made && (i == x->rows ||
                                  ground_order(nodes, x->next[pos[m]], x->row[i]) <= 0))
            ok = next_push(x, x->next[pos[m++]]);
        if (ok && i < x->rows && !gone[i]) ok = next_push(x, x->row[i]);
    }
    free(pos);
    if (!ok) return UINT32_MAX;
    TcTerm *rows = x->next + made;
    uint32_t n = 0;
    for (size_t i = 0; i < x->next_len - made; i++) {   /* a set store keeps one of equal rows */
        TcTerm row = rows[i];
        if (x->how.store == TC_STORE_SET && n > 0 && rows[n - 1] == row) continue;
        rows[n++] = row;
    }
    return state_index(&x->states, rows, n, true);
}

/* The state of the rows not in `gone` (a flag per row) plus x->next, under the
 * solver's present assignments, by the full computation: every row resolved,
 * the whole put in canonical form.  UINT32_MAX when memory ran out. */
static uint32_t full_successor(Explorer *x, const bool *gone) {
    size_t made = x->next_len;
    for (uint32_t i = 0; i < x->rows; i++)
        if (!gone[i]) {
            TcTerm row = resolve(x->terms, &x->solver, x->row[i], 0);
            if (!row || !next_push(x, row)) return UINT32_MAX;
        }
    for (size_t i = 0; i < made; i++) x->next[i] = resolve(x->terms, &x->solver, x->next[i], 0);
    uint32_t n = (uint32_t)x->next_len;
    bool ground;
    if (x->solver.failed || x->terms->failed ||
        !canonical_form(x->terms, x->next, &n, x->how.store, &ground))
        return UINT32_MAX;
    return state_index(&x->states, x->next, n, ground);
}

/* The state of the rows not in `gone` plus the made rows x->next: merged at a
 * state of fixed terms when every made row is fixed too, otherwise in full
 * (with shortcuts checked, both, compared).  UINT32_MAX when memory ran out. */
static uint32_t successor_state(Explorer *x, const bool *gone) {
    size_t made = x->next_len;
    if (x->solver.failed || x->terms->failed) return UINT32_MAX;
    bool fixed = x->ground && x->how.shortcuts != TC_SHORTCUTS_NONE;
    for (size_t i = 0; fixed && i < made; i++) fixed = term_node(x->terms, x->next[i]).ground;
    if (!fixed) return full_successor(x, gone);
    uint32_t merged = merged_successor(x, gone);
    if (x->how.shortcuts == TC_SHORTCUTS_TAKEN || merged == UINT32_MAX) return merged;
    x->next_len = made;   /* the made rows again, for the full computation */
    uint32_t full = full_successor(x, gone);
    if (full != merged) x->disagreed = true;
    return full;
}

/* Frame slots for one more match, its locals from `base` on, unassigned.
 * False (and the solver failed) when memory ran out: then the slots are not
 * there, and nothing may be unified at that base. */
static bool frame(Explorer *x, uint32_t base, const TcStoredRule *r) {
    if (!reserve(&x->solver.local, &x->solver.local_cap, (size_t)base + r->locals + 1)) {
        x->solver.failed = true;
        return false;
    }
    memset(x->solver.local + base, 0, r->locals * sizeof *x->solver.local);
    return true;
}

/* Records a match, its selection (x->pick) in pattern order, so that it can be
 * fitted into a bag again. */
static bool record_member(Explorer *x, uint32_t rule, uint32_t total, bool alone,
                          uint32_t successor, uint32_t firing) {
    TcMember e = {.rule = rule, .start = (uint32_t)x->selections, .successor = successor,
                  .firing = firing, .alone = alone};
    for (uint32_t i = 0; i < total; i++)
        if (!tc_push_u32(realloc, &x->selection, &x->selections, &x->selection_cap, x->pick[i]))
            return false;
    return tc_push_member(realloc, &x->member, &x->members, &x->member_cap, e);
}

/* Two matches of one rule that select the same row terms, though not the same
 * rows, are two members of the step (distinct occurrences), with one admission
 * and one successor: a state is a bag of terms.  The members met at a state are
 * kept by rule and row terms, so that a successor is computed once.  The table
 * is a shortcut: a member it cannot hold is computed again, to the same end. */
static uint64_t selection_hash(const Explorer *x, uint32_t rule, const uint32_t *pick,
                               uint32_t total) {
    uint64_t h = (rule + 1) * UINT64_C(0x9E3779B97F4A7C15);
    for (uint32_t i = 0; i < total; i++) h = (h ^ x->row[pick[i]]) * UINT64_C(0x100000001B3);
    return h ^ (h >> 29);
}

/* Whether a member with rule and the row terms of x->pick was met at this
 * state; then *met is that member. */
static bool known_selection(const Explorer *x, uint32_t rule, uint32_t total, TcMember *met) {
    for (size_t i = x->known_cap ? selection_hash(x, rule, x->pick, total) & (x->known_cap - 1) : 0;
         x->known_cap && x->known[i]; i = (i + 1) & (x->known_cap - 1)) {
        const TcMember *e = &x->member[x->known[i] - 1];
        const uint32_t *other = x->selection + e->start;
        bool same = e->rule == rule;
        for (uint32_t j = 0; same && j < total; j++) same = x->row[other[j]] == x->row[x->pick[j]];
        if (same) { *met = *e; return true; }
    }
    return false;
}

static void remember_selection(Explorer *x, uint32_t m, uint32_t total) {
    if ((x->known_used + 1) * 2 > x->known_cap) {   /* grow, and put back what is known */
        size_t cap = x->known_cap ? x->known_cap * 2 : 64;
        uint32_t *grown = calloc(cap, sizeof *grown);
        if (!grown) return;
        for (size_t i = 0; i < x->known_cap; i++) {
            if (!x->known[i]) continue;
            const TcMember *e = &x->member[x->known[i] - 1];
            uint32_t n = x->program.rule[e->rule].takes + x->program.rule[e->rule].reads;
            size_t j = selection_hash(x, e->rule, x->selection + e->start, n) & (cap - 1);
            while (grown[j]) j = (j + 1) & (cap - 1);
            grown[j] = x->known[i];
        }
        free(x->known);
        x->known = grown;
        x->known_cap = cap;
    }
    const TcMember *e = &x->member[m];
    size_t j = selection_hash(x, e->rule, x->selection + e->start, total) & (x->known_cap - 1);
    while (x->known[j]) j = (j + 1) & (x->known_cap - 1);
    x->known[j] = m + 1;
    x->known_used++;
}

/* Forgets the members of the last state. */
static void forget_members(Explorer *x) {
    x->members = 0;
    x->selections = 0;
    if (x->known_used) memset(x->known, 0, x->known_cap * sizeof *x->known);
    x->known_used = 0;
}

/* Up to symmetry, a match is its rule and the bags of (pattern, row) pairs it
 * takes and reads: which of two identical patterns took which row is not part
 * of it.  So the selections met are those whose rows rise along each run of
 * identical patterns (strictly for takes, which are distinct rows), one per
 * match.  Returns the latest position before k in the same part (take or read)
 * with the same pattern, or UINT32_MAX. */
static uint32_t identical_before(const Explorer *x, const TcStoredRule *r, uint32_t k) {
    uint32_t first = k < r->takes ? 0 : r->takes;
    TcTerm pattern = pattern_at(&x->program, r, k);
    for (uint32_t j = k; j-- > first;)
        if (pattern_at(&x->program, r, j) == pattern) return j;
    return UINT32_MAX;
}

/* Whether the discipline's bags may hold matches not admitted on their own:
 * bag, synchronous and maximal ask their members' checks only at the joint
 * information (Bag.Admits, Bag.AdmitsSnapshot). */
static bool loose_members(TcStep step) {
    return step == TC_STEP_BAG || step == TC_STEP_SYNCHRONOUS || step == TC_STEP_MAXIMAL;
}

/* The state the match of x->pick reaches on its own: its taken rows gone and
 * its made rows added, under the solver's present assignments.  UINT32_MAX
 * when memory ran out. */
static uint32_t own_successor(Explorer *x, const TcStoredRule *r) {
    bool *gone = calloc(x->rows ? x->rows : 1, sizeof *gone);
    if (!gone) return UINT32_MAX;
    for (uint32_t i = 0; i < r->takes; i++) gone[x->pick[i]] = true;
    x->next_len = 0;
    uint32_t succ = append_made(x, r, 0) ? successor_state(x, gone) : UINT32_MAX;
    free(gone);
    return succ;
}

/* ---- Independent firings explored once (step one) -------------------------
 * Two firings are independent at a state p of fixed terms (no unknowns) when
 * their rules are positive (their checks are equations) and their selections
 * fit together: every take a row of its own, every read a row no take takes.
 * The tree's commutation theorems (RuleSyntax/JointFirings/Independent.lean)
 * say what follows: the bag of the two is admitted exactly when each is
 * (admitsOwn_iff_members), and it is a run in either order, ending at the same
 * rows (any_order).  So the two orders of an independent pair end at one state.
 *
 * The exploration uses this under step one.  Expanding state x, first reached
 * from p by a, a match u admitted at x that is independent of a at p, whose
 * rule and row terms were met at p (reaching p.u) and where a's were met at
 * p.u in turn, reaches what a reached there: the firing is looked up, not made
 * again.  (Only the pairs met at the state x was first reached from are so
 * looked up; those at its other parents are computed.)  The state looked up
 * must have no unknowns either, so that
 * its canonical form is the one a computation would give.  Every step is still
 * found with the state it reaches, so outcomes, counts and cycles are as
 * before.  The table is a shortcut: when it cannot grow, firings are computed. */
static uint64_t fired_hash(uint32_t state, uint32_t rule, const TcTerm *terms, uint32_t n) {
    uint64_t h = (state + UINT64_C(1)) * UINT64_C(0x9E3779B97F4A7C15) ^ (rule + UINT64_C(1));
    for (uint32_t i = 0; i < n; i++) h = (h ^ terms[i]) * UINT64_C(0x100000001B3);
    return h ^ (h >> 29);
}

/* The number of row terms firing k selects: its terms run to the next one's. */
static uint32_t fired_terms(const Fired *f, size_t k) {
    size_t end = 4 * (k + 1) < f->firings ? f->firing[4 * (k + 1) + 2] : f->terms;
    return (uint32_t)(end - f->firing[4 * k + 2]);
}

/* The firing met at `state` by `rule` selecting these row terms, or UINT32_MAX. */
static uint32_t fired_find(const Fired *f, uint32_t state, uint32_t rule, const TcTerm *terms,
                           uint32_t n) {
    if (!f->slots) return UINT32_MAX;
    for (size_t i = fired_hash(state, rule, terms, n) & (f->slots - 1); f->slot[i];
         i = (i + 1) & (f->slots - 1)) {
        const uint32_t *e = f->firing + 4 * (size_t)(f->slot[i] - 1);
        if (e[0] == state && e[1] == rule &&
            (n == 0 || memcmp(f->term + e[2], terms, n * sizeof *terms) == 0))
            return f->slot[i] - 1;
    }
    return UINT32_MAX;
}

static bool fired_grow(Fired *f) {
    size_t slots = f->slots ? f->slots * 2 : 1024;
    uint32_t *slot = calloc(slots, sizeof *slot);
    if (!slot) return false;
    for (size_t k = 0; k < f->firings / 4; k++) {
        const uint32_t *e = f->firing + 4 * k;
        size_t i = fired_hash(e[0], e[1], f->term + e[2], fired_terms(f, k)) & (slots - 1);
        while (slot[i]) i = (i + 1) & (slots - 1);
        slot[i] = (uint32_t)k + 1;
    }
    free(f->slot);
    f->slot = slot;
    f->slots = slots;
    return true;
}

/* Adds a firing met; its index, or UINT32_MAX when memory ran out. */
static uint32_t fired_add(Fired *f, uint32_t state, uint32_t rule, const TcTerm *terms,
                          uint32_t n, uint32_t successor) {
    size_t count = f->firings / 4;
    if ((count + 1) * 2 > f->slots && !fired_grow(f)) return UINT32_MAX;
    uint32_t words[4] = {state, rule, (uint32_t)f->terms, successor};
    for (uint32_t i = 0; i < n; i++)
        if (!tc_push_u32(realloc, &f->term, &f->terms, &f->term_cap, terms[i])) return UINT32_MAX;
    for (int i = 0; i < 4; i++)
        if (!tc_push_u32(realloc, &f->firing, &f->firings, &f->firing_cap, words[i]))
            return UINT32_MAX;
    size_t i = fired_hash(state, rule, terms, n) & (f->slots - 1);
    while (f->slot[i]) i = (i + 1) & (f->slots - 1);
    f->slot[i] = (uint32_t)count + 1;
    return (uint32_t)count;
}

/* Marks `firing` as the one that first reached `state`; false on memory. */
static bool fired_first(Fired *f, uint32_t state, uint32_t firing) {
    if (state >= f->firsts) {
        size_t firsts = f->firsts ? f->firsts : 64;
        while (firsts <= state) firsts *= 2;
        uint32_t *g = realloc(f->first, firsts * sizeof *g);
        if (!g) return false;
        memset(g + f->firsts, 0, (firsts - f->firsts) * sizeof *g);
        f->first = g;
        f->firsts = firsts;
    }
    f->first[state] = firing + 1;
    return true;
}

static void fired_free(Fired *f) { free(f->firing); free(f->term); free(f->slot); free(f->first); }

/* The row terms the match decided selects (x->pick), in pattern order, in x->key. */
static bool selection_key(Explorer *x, uint32_t total) {
    if (total > x->key_cap) {
        TcTerm *grown = realloc(x->key, total * sizeof *grown);
        if (!grown) return false;
        x->key = grown;
        x->key_cap = total;
    }
    for (uint32_t i = 0; i < total; i++) x->key[i] = x->row[x->pick[i]];
    return true;
}

/* A positive rule (Scope.Positive): its checks are equations; it has no
 * absence, count or census. */
static bool positive_rule(const ProgramView *v, uint32_t rule) {
    TcStoredRule r = rule_at(v, rule);
    for (uint32_t i = 0; i < r.check_count; i++)
        if (check_at(v, &r, i).kind != TC_CHECK_EQUAL) return false;
    return true;
}

/* How many rows of state q, a state of fixed terms and so in ground order,
 * are the term. */
static uint32_t state_count(const StateTable *st, const TcTerms *t, uint32_t q, TcTerm term) {
    TcState s = st->state[q];
    const TcTerm *row = st->row + s.start;
    uint32_t lo = 0, hi = s.rows, n = 0;
    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2;
        if (ground_order(t->node, row[m], term) < 0) lo = m + 1; else hi = m;
    }
    while (lo + n < s.rows && row[lo + n] == term) n++;
    return n;
}

/* Whether two selections of row terms (each a rule's takes, then its reads)
 * fit together in state q: every take a row of its own, every read a row no
 * take takes.  Their joint selection is then enabled as one commit. */
static bool selections_fit(const Explorer *x, uint32_t q, const TcTerm *a, uint32_t a_takes,
                           uint32_t a_total, const TcTerm *b, uint32_t b_takes, uint32_t b_total) {
    for (uint32_t i = 0; i < a_total + b_total; i++) {
        TcTerm t = i < a_total ? a[i] : b[i - a_total];
        bool earlier = false, read = false;
        uint32_t takes = 0;
        for (uint32_t j = 0; j < a_total + b_total; j++) {
            TcTerm s = j < a_total ? a[j] : b[j - a_total];
            if (s != t) continue;
            if (j < i) earlier = true;
            if (j < a_takes || (j >= a_total && j - a_total < b_takes)) takes++;
            else read = true;
        }
        if (!earlier && takes + (read ? 1u : 0u) > state_count(&x->states, x->terms, q, t))
            return false;
    }
    return true;
}

/* The state the match decided (rule x->fire_rule, selection x->pick) reaches
 * when it is the second order of an independent pair met before; UINT32_MAX
 * when it is not.  Independent: at a state p of fixed terms, two firings of
 * positive rules whose selections fit together.  Independent.lean's
 * admitsOwn_iff_members admits their bag, and any_order runs it in both
 * orders to one state, so the state a reached from p.u is u's here. */
static uint32_t derived_successor(Explorer *x, uint32_t total) {
    const Fired *f = &x->fired;
    if (!x->derive || !x->ground || x->at >= f->firsts || !f->first[x->at]) return UINT32_MAX;
    size_t a = f->first[x->at] - 1;                       /* p --a--> the state expanded */
    uint32_t p = f->firing[4 * a], a_rule = f->firing[4 * a + 1];
    const TcTerm *a_terms = f->term + f->firing[4 * a + 2];
    if (!x->states.ground[p] || !positive_rule(&x->program, x->fire_rule) ||
        !positive_rule(&x->program, a_rule) || !selection_key(x, total))
        return UINT32_MAX;
    uint32_t u = fired_find(f, p, x->fire_rule, x->key, total);   /* p --u--> p.u */
    if (u == UINT32_MAX) return UINT32_MAX;
    uint32_t far = fired_find(f, f->firing[4 * (size_t)u + 3], a_rule, a_terms,
                              fired_terms(f, a));                  /* p.u --a--> */
    if (far == UINT32_MAX) return UINT32_MAX;
    uint32_t target = f->firing[4 * (size_t)far + 3];
    if (!x->states.ground[target] ||
        !selections_fit(x, p, x->key, x->fire.takes, total, a_terms,
                        rule_at(&x->program, a_rule).takes, fired_terms(f, a)))
        return UINT32_MAX;
    return target;
}

/* Keeps the match decided, admitted at the state expanded and reaching
 * `successor`, among the firings met: its index, or UINT32_MAX when firings
 * are not kept. */
static uint32_t remember_firing(Explorer *x, uint32_t total, uint32_t successor) {
    if (!x->derive) return UINT32_MAX;
    uint32_t k = selection_key(x, total)
               ? fired_add(&x->fired, x->at, x->fire_rule, x->key, total, successor) : UINT32_MAX;
    if (k == UINT32_MAX) x->derive = false;   /* without room, every firing is computed */
    return k;
}

/* ---- One match decided ---------------------------------------------------
 * A match is a rule (x->fire) and a selection of rows for its patterns
 * (x->pick), unified in the solver.  The order of its decision is the protocol
 * tc_fire, declared in Fire.module and checked by Cesta: the tells and the
 * checks in the order of the ask axis, the trail undone on every path, a match
 * recorded as admitted only after both.  The hooks below are its steps. */
static bool tc_fire_after_tells(Explorer *x) { return x->how.ask == TC_ASK_AFTER_TELLS; }
static bool tc_fire_mark(Explorer *x) { x->fire_mark = mark(&x->solver); return true; }
static void tc_fire_undo(Explorer *x) { undo(&x->solver, x->fire_mark); }
/* The trail's transfer, which tc_fire never uses: every path undoes the trail,
 * since a successor is computed and none of the solver's bindings is kept. */
__attribute__((unused)) static void tc_fire_keep(Explorer *x) { (void)x; }
static bool tc_fire_tells(Explorer *x) {
    TcStoredRule r = x->fire;
    return tells_hold(x, &r, 0);
}
static bool tc_fire_checks(Explorer *x) {
    TcStoredRule r = x->fire;
    return checks_hold(x, &r, 0);
}
static bool tc_fire_checks_first(Explorer *x) { return tc_fire_checks(x); }
static bool tc_fire_loose(Explorer *x) { return loose_members(x->how.step); }

/* Admitted on its own: its successor is looked up (the second order of an
 * independent pair met before) or computed, and recorded.  With shortcuts
 * checked it is computed always, and compared. */
/* tc_decide's admit: the host commits the rows taken and made.  The buffer the
 * made rows are returned in is reserved first, so that nothing can fail after
 * the host's effects. */
static bool store_admit(Explorer *x, const TcStoredRule *r) {
    const TcStoreOps *ops = &x->store;
    if (x->solver.failed || x->terms->failed) return false;
    if (!ops->commit) { x->store_status = TC_STORE_INVALIDATED; return false; }
    x->next_len = 0;
    if (!append_made(x, r, 0) ||
        !reserve(&x->terms->decided, &x->terms->decided_cap, x->next_len ? x->next_len : 1))
        return false;
    TcOccurrence empty_taken = 0;
    TcTerm empty_made = 0;
    if (r->takes) {
        if (!x->selected) return false;
        if (x->next_len) {
            if (!x->next) return false;
            x->commit = ops->commit(ops->store, x->terms, x->selected, r->takes,
                                    x->next, x->next_len);
        } else
            x->commit = ops->commit(ops->store, x->terms, x->selected, r->takes,
                                    &empty_made, 0);
    } else if (x->next_len) {
        if (!x->next) return false;
        x->commit = ops->commit(ops->store, x->terms, &empty_taken, 0,
                                x->next, x->next_len);
    } else {
        x->commit = ops->commit(ops->store, x->terms, &empty_taken, 0,
                                &empty_made, 0);
    }
    if (x->commit == TC_COMMIT_DONE) {
        if (x->next_len) memcpy(x->terms->decided, x->next, x->next_len * sizeof *x->next);
        x->terms->decided_len = x->next_len;
    }
    return true;
}

static bool tc_fire_admit(Explorer *x) {
    TcStoredRule r = x->fire;
    if (x->has_store) return store_admit(x, &r);
    uint32_t total = r.takes + r.reads, succ = derived_successor(x, total);
    if (succ == UINT32_MAX || x->how.shortcuts == TC_SHORTCUTS_CHECKED) {
        uint32_t own = own_successor(x, &r);
        if (succ != UINT32_MAX && own != succ) x->disagreed = true;
        succ = own;
    }
    if (succ == UINT32_MAX) return false;
    uint32_t firing = remember_firing(x, total, succ);
    if (!record_member(x, x->fire_rule, total, true, succ, firing)) return false;
    remember_selection(x, (uint32_t)x->members - 1, total);
    return true;
}

/* Solvable but not admitted on its own: a member only the steps bag, synchronous
 * and maximal admit. */
static bool tc_fire_admit_loose(Explorer *x) {
    uint32_t total = x->fire.takes + x->fire.reads;
    if (!record_member(x, x->fire_rule, total, false, 0, UINT32_MAX)) return false;
    remember_selection(x, (uint32_t)x->members - 1, total);
    return true;
}

#include "tcalc_fire.generated.h"

/* Chooses rows (x->pick) for the patterns of rule `rule` from position k on
 * (takes first, then reads), and records every match the discipline can use:
 * each one admitted on its own, and for bag, synchronous and maximal also
 * each one whose own information (its matching and its tells) is solvable.
 *
 * Under step one a step is one match and a state is a bag of terms, so two
 * selections that differ only in which copy of a row term they choose reach
 * the same state: at each position one copy of each row term is tried (equal
 * rows are adjacent in a state).  The other steps count every copy, since
 * their members are occurrences. */
static bool match_from(Explorer *x, uint32_t rule, uint32_t k) {
    TcStoredRule r = rule_at(&x->program, rule);
    uint32_t total = r.takes + r.reads;
    if (k == total) {
        TcMember same = {0};
        if (known_selection(x, rule, total, &same))   /* the same row terms: the same */
            return record_member(x, rule, total, same.alone, same.successor,   /* decision */
                                 same.firing);
        x->fire = r;
        x->fire_rule = rule;
        return tc_fire(x) != tc_fire_engine_failure && !x->solver.failed;
    }
    bool take = k < r.takes;
    TcTerm pattern = pattern_at(&x->program, &r, k);
    Runs runs = candidates(x, pattern, 0);
    if (x->how.matches == TC_MATCHES_SYMMETRIC) {
        uint32_t twin = identical_before(x, &r, k);
        uint32_t bound = twin == UINT32_MAX ? 0 : pick_at(x, twin) + (take ? 1 : 0);
        for (int p = 0; p < 2; p++) if (runs.lo[p] < bound) runs.lo[p] = bound;
    }
    bool one = x->how.step == TC_STEP_ONE;
    for (int p = 0; p < 2; p++) {
        TcTerm tried = 0;   /* the row term last tried at this position */
        for (uint32_t i = runs.lo[p]; i < runs.hi[p]; i++) {
            bool clash = false;   /* takes distinct; a read is never a taken row */
            for (uint32_t j = 0; j < (take ? k : r.takes) && !clash; j++) clash = pick_at(x, j) == i;
            if (clash) continue;
            TcTerm row = row_at(x, i);
            if (one) {
                if (row == tried) continue;   /* another copy of the term just tried */
                tried = row;
            }
            Mark m = mark(&x->solver);
            if (unify(x->terms, &x->solver, pattern, 0, row, 0)) {
                pick_put(x, k, i);
                if (!match_from(x, rule, k + 1)) return false;
            }
            undo(&x->solver, m);
            if (x->solver.failed) return false;
        }
    }
    return true;
}

/* ---- Flat firings decided by another procedure (backend x86-flat) --------
 * The adapter that lets the flat_fire decider decide a flat rule's firings is
 * tcalc_flat.c, which follows this file in the unit: flat_start packs each
 * rule's plan when an exploration starts (false when memory ran out or the
 * decider cannot take the program), and flat_collect stands in for the
 * matching below. */
static bool flat_start(Explorer *x);
static bool flat_collect(Explorer *x);
static void resident_prepare(TcProgram *p);
static bool resident_collect(Explorer *x,uint32_t rule,const FlatPlan *plan);

/* The matches at the state (rows) the discipline can use, each once. */
static bool collect_members(Explorer *x) {
    forget_members(x);
    if (x->how.backend == TC_BACKEND_X86_FLAT) return flat_collect(x);
    uint32_t state_unknowns = 0;   /* the state's unknowns are 0 .. state_unknowns-1 */
    for (uint32_t i = 0; i < x->rows; i++) {
        uint32_t m = max_unknown(x->terms, row_at(x, i));
        if (m > state_unknowns) state_unknowns = m;
    }
    x->ground = state_unknowns == 0;
    for (uint32_t rule = 0; rule < x->program.rules; rule++) {
        TcStoredRule r = rule_at(&x->program, rule);
        if (r.same != rule) continue;   /* that rule again: its matches are met there */
        if (!ensure_pick(x, (size_t)r.takes + r.reads + 1) ||
            !reserve_unknowns(&x->solver, state_unknowns) || !frame(x, 0, &r)) return false;
        if (x->how.step==TC_STEP_ONE && x->how.shortcuts!=TC_SHORTCUTS_NONE && x->ground && rule<x->program.prepared_count &&
            x->program.prepared[rule].guards!=UINT32_MAX && cetta_kernel_batch) {
            if (!resident_collect(x,rule,&x->program.prepared[rule])) return false;
        } else if (!match_from(x,rule,0)) return false;
    }
    return true;
}

/* ======================================================================
 * 6. Disciplines: which bags of matches are steps
 *
 * A bag commits several matches at once, at one state.  Its members are fitted
 * together: their taken rows are distinct, their openings are jointly fresh
 * (each member has its own frame of locals), and the knowledge with every
 * member's matching and tells is solvable as one.  Absences, counts and census
 * always read the entry state.  Bags are never empty.  The disciplines:
 *
 *   one          bags of one member                      (JointFirings/Singletons)
 *   bag          the joint selection enabled as one commit (no member reads or
 *                takes a row another takes), the checks at every solution of the
 *                joint information                       (Bags: Bag.Admits)
 *   synchronous  each member enabled on its own, so one may read a row another
 *                takes; the checks as for bag            (Bags: Bag.AdmitsSnapshot)
 *   round        a bag whose members, fired one at a time, reach its
 *                result in every order
 *   every        all the matches of the region at once, jointly enabled, each
 *                admitted at its own information         (Every: Bag.AdmitsEvery)
 *   maximal      a bag that no admitted bag strictly contains
 *                                                         (Maximal: Bag.AdmitsMaximal)
 *   unique       every with one member: the one match of the region; a state
 *                with more has no step and is reported   (Unique: Bag.AdmitsUnique)
 *
 * The members of bag, synchronous and maximal steps need not be admitted on
 * their own: one member's tells may be what another's check asks.  Members are
 * distinct matches: a rule that only reads could otherwise join one bag any
 * number of times.
 * ====================================================================== */

typedef struct {
    uint32_t *member; size_t len, cap;   /* member indices, in order of addition */
    uint32_t *base;   size_t blen, bcap; /* each member's frame base             */
    uint32_t next_base;
    bool *taken, *read;                  /* per entry row                         */
    uint32_t rows;                       /* the entry rows taken and read count   */
    bool snapshot;   /* each member enabled on its own (synchronous), not the
                        joint selection as one commit                         */
    bool tells;      /* a member's tells are solved as it joins; otherwise the
                        bag's checks are asked first (before-tells)            */
} Bag;

/* One proposed order borrows the entry-row masks from its bag while owning
 * only its ordered member/base arrays. */
typedef struct {
    uint32_t *member; size_t len, cap;
    uint32_t *base; size_t blen, bcap;
    uint32_t next_base;
    const bool *taken, *read; uint32_t rows;
} RunOrder;

static const uint32_t *member_pick(const Explorer *x, const TcMember *e) {
    return x->selection ? x->selection + e->start : no_items;
}

static inline TcMember member_at(const Explorer *x, size_t m) { return x->member[m]; }
static inline uint32_t selection_at(const Explorer *x, size_t k) { return x->selection[k]; }
static inline bool bag_taken(const Bag *b, uint32_t row) { return b->taken[row]; }
static inline bool bag_reads(const Bag *b, uint32_t row) { return b->read[row]; }
static inline void bag_take(Bag *b, uint32_t row) { b->taken[row] = true; }
static inline void bag_read_row(Bag *b, uint32_t row) { b->read[row] = true; }
static inline uint32_t bag_member(const Bag *b, size_t k) { return b->member[k]; }
static inline uint32_t bag_base(const Bag *b, size_t k) { return b->base[k]; }

static bool bag_push(Bag *b, uint32_t m, uint32_t base) {
    return tc_push_u32(realloc, &b->member, &b->len, &b->cap, m) &&
           tc_push_u32(realloc, &b->base, &b->blen, &b->bcap, base);
}

/* Fits member m into the bag: its rows (enabled as the discipline reads it),
 * its matching and, when the bag says so, its tells, solved with the members
 * already in. */
static bool bag_add(Explorer *x, Bag *b, uint32_t m) {
    TcMember e = member_at(x, m);
    TcStoredRule r = rule_at(&x->program, e.rule);
    for (uint32_t i = 0; i < r.takes; i++) {
        uint32_t row = selection_at(x, e.start + i);
        if (bag_taken(b, row) || (!b->snapshot && bag_reads(b, row))) return false;
    }
    if (!b->snapshot)
        for (uint32_t i = r.takes; i < r.takes + r.reads; i++)
            if (bag_taken(b, selection_at(x, e.start + i))) return false;
    uint32_t base = b->next_base;
    if (!frame(x, base, &r)) return false;   /* memory ran out: the solver says so */
    for (uint32_t i = 0; i < r.takes + r.reads; i++)   /* the takes, then the reads */
        if (!unify(x->terms, &x->solver, pattern_at(&x->program, &r, i), base,
                   row_at(x, selection_at(x, e.start + i)), 0))
            return false;
    if (b->tells && !tells_hold(x, &r, base)) return false;
    for (uint32_t i = 0; i < r.takes; i++) bag_take(b, selection_at(x, e.start + i));
    for (uint32_t i = r.takes; i < r.takes + r.reads; i++)
        bag_read_row(b, selection_at(x, e.start + i));
    if (!bag_push(b, m, base)) {
        x->solver.failed = true;
        return false;
    }
    b->next_base = base + r.locals;
    return true;
}

static void bag_remove_last(Explorer *x, Bag *b) {
    uint32_t m = b->member[--b->len];
    b->next_base = b->base[--b->blen];
    const TcMember *e = &x->member[m];
    const TcStoredRule *r = &x->program.rule[e->rule];
    const uint32_t *pick = member_pick(x, e);
    for (uint32_t i = 0; i < r->takes; i++) b->taken[pick[i]] = false;
    /* reads may be shared with other members: recompute them */
    memset(b->read, 0, x->rows * sizeof *b->read);
    for (size_t k = 0; k < b->len; k++) {
        const TcMember *o = &x->member[b->member[k]];
        const TcStoredRule *orule = &x->program.rule[o->rule];
        const uint32_t *op = member_pick(x, o);
        for (uint32_t i = orule->takes; i < orule->takes + orule->reads; i++) b->read[op[i]] = true;
    }
}

/* The checks of every member at every solution of the joint information. */
static bool bag_checks_hold(Explorer *x, const Bag *b) {
    for (size_t k = 0; k < b->len; k++) {
        TcStoredRule r = rule_at(&x->program, member_at(x, bag_member(b, k)).rule);
        if (!checks_hold(x, &r, bag_base(b, k))) return false;
    }
    return true;
}

/* The tells of every member, solved with the rest (for a bag that did not
 * solve them as its members joined). */
static bool bag_tells_hold(Explorer *x, const Bag *b) {
    for (size_t k = 0; k < b->len; k++) {
        TcStoredRule r = rule_at(&x->program, member_at(x, bag_member(b, k)).rule);
        if (!tells_hold(x, &r, bag_base(b, k))) return false;
    }
    return true;
}

/* The state of the entry rows the bag leaves, with the rows it made. */
static uint32_t bag_successor(Explorer *x, const Bag *b) { return successor_state(x, b->taken); }

/* The state the bag reaches; UINT32_MAX when memory ran out. */
static uint32_t bag_result(Explorer *x, const Bag *b) {
    x->next_len = 0;
    for (size_t k = 0; k < b->len; k++) {
        TcStoredRule r = rule_at(&x->program, member_at(x, bag_member(b, k)).rule);
        if (!append_made(x, &r, bag_base(b, k))) return UINT32_MAX;
    }
    return bag_successor(x, b);
}

/* Fires the members of a bag one at a time in the given order, each admitted
 * on its own at the state the ones before it left; returns the state reached,
 * or UINT32_MAX - 1 when some member is not admitted, UINT32_MAX on memory. */
static uint32_t run_in_order(Explorer *x, const RunOrder *b, const uint32_t *order) {
    uint32_t n = x->rows;
    size_t cap = (size_t)n + 16, needed = n;
    for (size_t k = 0; k < b->len; k++) {
        const TcMember *e = &x->member[b->member[order[k]]];
        const TcStoredRule *r = &x->program.rule[e->rule];
        if ((size_t)r->makes > SIZE_MAX - needed) return UINT32_MAX;
        needed += r->makes;
    }
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) return UINT32_MAX;
        cap *= 2;
    }
    TcTerm *now = malloc(cap * sizeof *now);     /* present rows: entry rows, then made */
    bool *gone = calloc(cap, sizeof *gone);
    uint32_t count = n, result = UINT32_MAX - 1;
    if (!now || !gone) { free(now); free(gone); return UINT32_MAX; }
    if (n) {
        if (!x->row) { free(now); free(gone); return UINT32_MAX; }
        memcpy(now, x->row, n * sizeof *now);
    }
    for (size_t k = 0; k < b->len; k++) {
        const TcMember *e = &x->member[b->member[order[k]]];
        const TcStoredRule *r = &x->program.rule[e->rule];
        const uint32_t *pick = member_pick(x, e);
        uint32_t base = b->base[order[k]];
        bool ok = true;
        for (uint32_t i = 0; ok && i < r->takes + r->reads; i++) ok = !gone[pick[i]];
        for (uint32_t i = 0; ok && i < r->takes + r->reads; i++)   /* the takes, then the reads */
            ok = unify(x->terms, &x->solver, pattern_at(&x->program, r, i), base, now[pick[i]], 0);
        if (ok) {   /* the checks read the rows present now */
            TcTerm *present = malloc((count ? count : 1) * sizeof *present);
            uint32_t p = 0;
            if (!present) { result = UINT32_MAX; break; }
            for (uint32_t i = 0; i < count; i++)
                if (!gone[i]) present[p++] = resolve(x->terms, &x->solver, now[i], 0);
            uint32_t *pos = malloc((p ? p : 1) * sizeof *pos);
            TcTerm *sorted = malloc((p ? p : 1) * sizeof *sorted);
            if (!pos || !sorted) {
                free(present); free(pos); free(sorted);
                result = UINT32_MAX - 1;
                goto done;
            }
            {
                const TcNode *nodes = x->terms->node;
                if (!nodes) {
                    free(present); free(pos); free(sorted);
                    result = UINT32_MAX;
                    goto done;
                }
                RowOrder by = {nodes, x->terms->nodes, present, present, p, false};
                for (uint32_t i = 0; i < p; i++) pos[i] = i;
                ok = sort_positions(&by, pos, p);
                for (uint32_t i = 0; ok && i < p; i++) sorted[i] = present[pos[i]];
            }
            free(present); free(pos);
            if (ok) {   /* the explorer's rows are the present rows while it asks */
                const TcTerm *entry = x->row;
                uint32_t entry_rows = x->rows;
                TcStoredRule rule = *r;
                x->row = sorted;
                x->rows = p;
                ok = admitted(x, &rule, base);
                x->row = entry;
                x->rows = entry_rows;
            }
            free(sorted);
        }
        if (!ok) { result = UINT32_MAX - 1; goto done; }
        for (uint32_t i = 0; i < r->takes; i++) gone[pick[i]] = true;
        for (uint32_t i = 0; i < r->makes; i++) {
            if (!(now[count++] = close_term(x->terms, &x->solver, make_at(&x->program, r, i), base))) {
                result = UINT32_MAX;
                goto done;
            }
        }
    }
    x->next_len = 0;
    for (uint32_t i = 0; i < count; i++)
        if (!gone[i] && !tc_push_u32(realloc, &x->next, &x->next_len, &x->next_cap,
                                     resolve(x->terms, &x->solver, now[i], 0))) { result = UINT32_MAX; goto done; }
    {
        uint32_t m = (uint32_t)x->next_len;
        bool ground;
        result = canonical_form(x->terms, x->next, &m, x->how.store, &ground)
                 ? state_index(&x->states, x->next, m, ground) : UINT32_MAX;
    }
done:
    free(now); free(gone);
    return result;
}

/* Whether every order of the bag's members runs to `target`. */
static int every_order_reaches(Explorer *x, const RunOrder *b, uint32_t target) {
    size_t n = b->len;
    uint32_t *order = malloc((n ? n : 1) * sizeof *order), *c = calloc(n ? n : 1, sizeof *c);
    if (!order || !c) { free(order); free(c); return -1; }
    for (size_t i = 0; i < n; i++) order[i] = (uint32_t)i;
    int verdict = 1;
    size_t i = 0;
    for (bool first = true;;) {   /* Heap's algorithm: every permutation once */
        if (first) first = false;
        else {
            while (i < n && c[i] >= i) c[i++] = 0;
            if (i >= n) break;
            uint32_t j = (i % 2) ? c[i] : 0, tmp = order[j];
            order[j] = order[i]; order[i] = tmp;
            c[i]++; i = 0;
        }
        if (!spend(x)) { verdict = -2; break; }
        Mark m = mark(&x->solver);
        uint32_t reached = run_in_order(x, b, order);
        undo(&x->solver, m);
        if (reached == UINT32_MAX) { verdict = -1; break; }
        if (reached != target) { verdict = 0; break; }
    }
    free(order); free(c);
    return verdict;
}

/* The successors of the state under the discipline, appended to `out`. */
typedef struct {
    uint32_t *state; size_t len, cap;
    bool budget_out;
    size_t ambiguous;   /* unique: matches at a state with more than one */
} Successors;

static bool add_successor(Successors *o, uint32_t state) {
    for (size_t i = 0; i < o->len; i++) if (o->state[i] == state) return true;
    return tc_push_u32(realloc, &o->state, &o->len, &o->cap, state);
}

/* Whether the rule is one the region names (a rule named twice, under two
 * names, is one rule). */
static inline uint32_t region_at(const TcInterpretation *how, uint32_t i) { return how->region[i]; }

static bool in_region(const Explorer *x, uint32_t rule) {
    if (!x->how.regions) return true;
    for (uint32_t i = 0; i < x->how.regions; i++)
        if (rule_at(&x->program, region_at(&x->how, i)).same == rule) return true;
    return false;
}

/* Enumerates the bags over members[from..] extending b, and handles each
 * admitted one according to the discipline.  Maximal and round decide after
 * the enumeration, so they keep the admitted bags they meet: as sorted member
 * lists, with each bag's result. */
typedef struct {
    Successors *out;
    uint32_t *bags; size_t bags_len, bags_cap;
    uint32_t *bag_start; size_t starts, starts_cap;
    uint32_t *bag_result; size_t results, results_cap;
} Enumeration;

/* A step of the discipline, to the enumeration's successors. */
static bool enumeration_step(Enumeration *en, uint32_t state) { return add_successor(en->out, state); }

static bool keep_bag(Enumeration *en, const Bag *b, uint32_t result) {
    uint32_t *sorted = malloc(b->len * sizeof *sorted);
    if (!sorted) return false;
    memcpy(sorted, b->member, b->len * sizeof *sorted);
    for (size_t i = 1; i < b->len; i++)
        for (size_t j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) {
            uint32_t t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t;
        }
    bool ok = tc_push_u32(realloc, &en->bag_start, &en->starts, &en->starts_cap,
                          (uint32_t)en->bags_len) &&
              tc_push_u32(realloc, &en->bag_result, &en->results, &en->results_cap, result);
    for (size_t i = 0; ok && i < b->len; i++)
        ok = tc_push_u32(realloc, &en->bags, &en->bags_len, &en->bags_cap, sorted[i]);
    free(sorted);
    return ok;
}

/* Admission of a fitted bag: its checks at every solution of the joint
 * information, asked after the tells (already solved as the members joined) or
 * before them.  An admitted bag is a step, or kept for maximal and round. */
static bool handle_bag(Explorer *x, Bag *b, Enumeration *en) {
    TcStep step = x->how.step;
    Mark m = mark(&x->solver);
    bool admits = bag_checks_hold(x, b) && (b->tells || bag_tells_hold(x, b));
    uint32_t result = admits ? bag_result(x, b) : 0;
    undo(&x->solver, m);
    if (!admits) return !x->solver.failed;
    if (result == UINT32_MAX) return false;
    if (step == TC_STEP_MAXIMAL || step == TC_STEP_ROUND) return keep_bag(en, b, result);
    return enumeration_step(en, result);
}

static bool enumerate_bags(Explorer *x, Bag *b, uint32_t from, Enumeration *en) {
    if (b->len > 0) {
        if (!spend(x)) { en->out->budget_out = true; return true; }
        if (!handle_bag(x, b, en)) return false;
    }
    for (uint32_t m = from; m < x->members && !en->out->budget_out; m++) {
        Mark mk = mark(&x->solver);
        if (bag_add(x, b, m)) {
            if (!enumerate_bags(x, b, m + 1, en)) return false;
            bag_remove_last(x, b);
        }
        undo(&x->solver, mk);
        if (x->solver.failed) return false;
    }
    return true;
}

/* The members of the kept bag k, sorted; *n their number. */
static const uint32_t *kept_bag(const Enumeration *en, size_t k, size_t *n) {
    size_t end = k + 1 < en->starts ? en->bag_start[k + 1] : en->bags_len;
    *n = end - en->bag_start[k];
    return en->bags + en->bag_start[k];
}

/* Whether the sorted member list a is contained in the sorted list b. */
static bool members_within(const uint32_t *a, size_t na, const uint32_t *b, size_t nb) {
    size_t j = 0;
    for (size_t i = 0; i < na; i++) {
        while (j < nb && b[j] < a[i]) j++;
        if (j == nb || b[j] != a[i]) return false;
    }
    return true;
}

static bool successors(Explorer *x, Successors *out) {
    out->len = 0;
    out->budget_out = false;
    out->ambiguous = 0;
    if (!collect_members(x)) return false;
    TcStep step = x->how.step;
    if (step == TC_STEP_ONE) {
        for (size_t m = 0; m < x->members; m++)
            if (!add_successor(out, x->member[m].successor)) return false;
        return true;
    }
    if (step == TC_STEP_UNIQUE) {   /* every over the region, with one member */
        size_t in = 0, which = 0;
        for (size_t m = 0; m < x->members; m++)
            if (in_region(x, x->member[m].rule)) { in++; which = m; }
        if (in > 1) out->ambiguous = in;
        return in != 1 || add_successor(out, x->member[which].successor);
    }
    Bag b = {
        .snapshot = step == TC_STEP_SYNCHRONOUS,
        /* every asks each member's checks on its own, so its bag solves tells
         * as members join; the others ask the bag's checks in the ask's order */
        .tells = step == TC_STEP_EVERY || x->how.ask == TC_ASK_AFTER_TELLS,
    };
    b.rows = x->rows;
    size_t bag_cap = x->members ? x->members : 1;
    b.member = malloc(bag_cap * sizeof *b.member);
    b.base = malloc(bag_cap * sizeof *b.base);
    b.cap = b.bcap = bag_cap;
    b.taken = calloc(x->rows ? x->rows : 1, sizeof *b.taken);
    b.read = calloc(x->rows ? x->rows : 1, sizeof *b.read);
    if (!b.member) { free(b.base); free(b.taken); free(b.read); return false; }
    if (!b.base) { free(b.member); free(b.taken); free(b.read); return false; }
    if (!b.taken) { free(b.member); free(b.base); free(b.read); return false; }
    if (!b.read) { free(b.member); free(b.base); free(b.taken); return false; }
    bool ok = true;
    if (ok && step == TC_STEP_EVERY) {
        /* every match of the region; the checks of each were met on its own */
        Mark m = mark(&x->solver);
        size_t in = 0, fitted = 0;
        for (uint32_t k = 0; k < x->members; k++)
            if (in_region(x, x->member[k].rule)) {
                in++;
                if (!bag_add(x, &b, k)) break;
                fitted++;
            }
        if (in > 0 && fitted == in) {
            uint32_t result = bag_result(x, &b);
            ok = result != UINT32_MAX && add_successor(out, result);
        }
        undo(&x->solver, m);
    } else if (ok) {
        uint32_t *bags = malloc(8 * sizeof *bags);
        uint32_t *starts = malloc(8 * sizeof *starts);
        uint32_t *results = malloc(8 * sizeof *results);
        if (!bags || !starts || !results) {
            free(bags); free(starts); free(results);
            ok = false;
        } else {
        Enumeration en = {.out = out, .bags = bags, .bags_cap = 8,
                          .bag_start = starts, .starts_cap = 8,
                          .bag_result = results, .results_cap = 8};
        ok = enumerate_bags(x, &b, 0, &en);
        if (ok && step == TC_STEP_ROUND && !en.out->budget_out) {
            /* a bag is a round when every order of its members, each fired
             * on its own, reaches its result; the orders start from the entry
             * state, with fresh frames for the members */
            for (size_t k = 0; ok && k < en.starts && !en.out->budget_out; k++) {
                size_t start = en.bag_start[k];
                size_t end = k + 1 < en.starts ? en.bag_start[k + 1] : en.bags_len;
                size_t order_cap = end > start ? end - start : 1;
                uint32_t *order_member = malloc(order_cap * sizeof *order_member);
                uint32_t *order_base = malloc(order_cap * sizeof *order_base);
                if (!order_member || !order_base) {
                    free(order_member); free(order_base); ok = false; break;
                }
                RunOrder order = {.member = order_member, .cap = order_cap,
                    .base = order_base, .bcap = order_cap,
                    .taken = b.taken, .read = b.read, .rows = b.rows};
                for (size_t i = start; ok && i < end; i++) {
                    const TcStoredRule *r = &x->program.rule[x->member[en.bags[i]].rule];
                    uint32_t base = order.next_base;
                    ok = frame(x, base, r) &&
                         tc_push_u32(realloc, &order.member, &order.len, &order.cap, en.bags[i]) &&
                         tc_push_u32(realloc, &order.base, &order.blen, &order.bcap, base);
                    order.next_base = base + r->locals;
                }
                if (ok) {
                    int v = every_order_reaches(x, &order, en.bag_result[k]);
                    if (v == -1) ok = false;
                    else if (v == -2) en.out->budget_out = true;
                    else if (v == 1) ok = add_successor(en.out, en.bag_result[k]);
                }
                free(order.member); free(order.base);
            }
        }
        if (ok && step == TC_STEP_MAXIMAL && !en.out->budget_out) {
            /* an admitted bag is maximal when no admitted bag holds its members
             * and more (Maximal: Bag.AdmitsMaximal).  Admission is not monotone
             * (a member's check may need another member's tells), so every
             * admitted bag is compared, not only the bags of one more member. */
            for (size_t k = 0; ok && k < en.starts; k++) {
                size_t n, m;
                const uint32_t *mine = kept_bag(&en, k, &n);
                bool maximal = true;
                for (size_t j = 0; maximal && j < en.starts; j++) {
                    const uint32_t *theirs = kept_bag(&en, j, &m);
                    if (m > n && members_within(mine, n, theirs, m)) maximal = false;
                }
                if (maximal) ok = add_successor(en.out, en.bag_result[k]);
            }
        }
        free(en.bags); free(en.bag_start); free(en.bag_result);
        }
    }
    free(b.member); free(b.base); free(b.taken); free(b.read);
    return ok && !x->solver.failed;   /* a bag cut short by memory is no answer */
}

/* ======================================================================
 * 7. Exploration
 *
 * Breadth first from the initial state, each state reached expanded once.  (A
 * state can be known to the table without being reached: computing a match's
 * own successor, or a bag's result, names a state that the discipline may never
 * step to.  Only the states a step reaches are expanded, listed and counted.)
 * A state with no step is an outcome.  The steps found are kept, so that a
 * cycle among the states reached, an infinite run, is reported and not hidden
 * by visiting each state once.  In a set store, a firing that only adds rows
 * already present changes nothing, and is no step.
 * ====================================================================== */

/* The successors of state q, its rows copied first: the state table may grow
 * while q is expanded.  False when memory ran out. */
static bool expand(Explorer *x, uint32_t q, Successors *next) {
    TcState s = x->states.state[q];
    x->row = NULL;
    x->rows = 0;
    TcTerm *mine = malloc((s.rows ? s.rows : 1) * sizeof *mine);
    if (!mine) return false;
    if (s.rows) memcpy(mine, x->states.row + s.start, s.rows * sizeof *mine);
    x->at = q;
    x->row = mine;
    x->rows = s.rows;
    bool ok = successors(x, next);
    x->row = NULL;
    x->rows = 0;
    free(mine);
    return ok;
}

/* The k-th of n states in the structural order of states: the least by a scan
 * when k is 0 (the earlier of two equal), otherwise by sorting them; a run's
 * successors are few.  False when memory ran out. */
static bool nth_least_state(Explorer *x, uint32_t *states, size_t n, size_t k, uint32_t *out) {
    if (k == 0) {
        uint32_t best = states[0];
        for (size_t i = 1; i < n; i++) {
            int c = state_order(&x->states, x->terms, states[i], best);
            if (c == -2) return false;
            if (c < 0) best = states[i];
        }
        *out = best;
        return true;
    }
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0; j--) {
            int c = state_order(&x->states, x->terms, states[j], states[j - 1]);
            if (c == -2) return false;
            if (c >= 0) break;
            uint32_t t = states[j]; states[j] = states[j - 1]; states[j - 1] = t;
        }
    *out = states[k];
    return true;
}

/* observe run: from state q, each step to the successor the schedule picks of
 * those in the structural order (the runner's: canonical, the first; rotating,
 * the n-th at the n-th step), listing each state of the run in turn.  The run
 * ends at a state with no step (*finished), back at a configuration it has
 * been in (an infinite run: cyclic; under canonical, a state it has been in),
 * at an ambiguous state of unique, or when the budget is spent. */
static bool run_path(Explorer *x, TcOutcomes *o, uint32_t q, bool *finished) {
    RunVisits visits = {.period = 1};
    Successors next = {0};
    uint64_t turn = 0;
    bool ok = true;
    x->row = NULL;
    x->rows = 0;
    if (!spend(x)) { run_visits_free(&visits); return true; }
    int again = run_visit(&visits, q, turn);
    if (again < 0) { run_visits_free(&visits); return false; }
    if (again) {
        o->cyclic = true;
        *finished = true;
        run_visits_free(&visits);
        return true;
    }
    if (!list_state(o, &x->states, q, false, 0)) {
        run_visits_free(&visits);
        return false;
    }
    ok = expand(x, q, &next);
    if (!ok || next.budget_out) {
        run_visits_free(&visits);
        free(next.state);
        return ok;
    }
    for (;;) {
        if (next.ambiguous) {
            ok = list_state(o, &x->states, q, true, next.ambiguous);
            *finished = true;
            break;
        }
        size_t steps = 0;   /* in a set store, a firing that changes nothing is no step */
        if (!next.len) { *finished = true; break; }
        if (!next.state) { ok = false; break; }
        for (size_t i = 0; i < next.len; i++)
            if (!(x->how.store == TC_STORE_SET && next.state[i] == q))
                next.state[steps++] = next.state[i];
        if (steps == 0) { *finished = true; break; }   /* no step: the run ends */
        run_picks_among(&visits, x->how.schedule, steps);
        if (!(ok = nth_least_state(x, next.state, steps, run_pick(x->how.schedule, steps, &turn),
                                   &q)))
            break;
        if (!spend(x)) break;
        again = run_visit(&visits, q, turn);
        if (again < 0) { ok = false; break; }
        if (again) { o->cyclic = true; *finished = true; break; }
        if (!(ok = list_state(o, &x->states, q, false, 0))) break;
        if (!(ok = expand(x, q, &next)) || next.budget_out) break;
    }
    run_visits_free(&visits);
    free(next.state);
    return ok;
}

/* An explorer of the program under the interpretation.  False when it cannot
 * start: memory ran out, or the flat decider cannot take the program. */
static bool explorer_start(Explorer *x) {
    if (x->how.backend != TC_BACKEND_X86_FLAT) return true;
    struct FlatPlan *plans = calloc(x->program.rules ? x->program.rules : 1, sizeof *plans);
    if (!plans) return false;
    free(x->plans);
    x->plans = plans;
    x->plans_len = x->program.rules;
    return flat_start(x);
}

static void explorer_free(Explorer *x) {
    free(x->member); free(x->selection); free(x->next); free(x->known); free(x->pick);
    free(x->plans); free(x->flat_words); free(x->key);
    free(x->batch_cursor); free(x->batch_output);
    fired_free(&x->fired);
    solver_free(&x->solver);
    states_free(&x->states);
}

/* The state of `n` rows, in canonical form, entered in the explorer's table;
 * UINT32_MAX when memory ran out. */
static uint32_t explorer_state(Explorer *x, const TcTerm *given, size_t n) {
    TcTerm *rows = malloc((n ? n : 1) * sizeof *rows);
    uint32_t count = (uint32_t)n, q = UINT32_MAX;
    bool ground;
    if (!rows) return UINT32_MAX;
    if (n) {
        if (!given) { free(rows); return UINT32_MAX; }
        memcpy(rows, given, n * sizeof *rows);
    }
    if (canonical_form(x->terms, rows, &count, x->how.store, &ground))
        q = state_index(&x->states, rows, count, ground);
    free(rows);
    return q;
}

/* observe outcomes: the exploration of every state reached, as the runner's
 * lane.  The runner keeps the states reached and the steps among them. */
typedef struct {
    Explorer *x;
    TcOutcomes *o;
    Successors next;   /* the successors of the state expanded last */
} Exploration;

/* Expands state q: its successors are its steps, and q is listed when it has
 * none (an outcome) or when it is ambiguous (unique). */
static RunExpansion explore_state(void *lane, uint32_t q, RunGraph *g) {
    Exploration *e = lane;
    Explorer *x = e->x;
    Successors *next = &e->next;
    if (!spend(x)) return RUN_STOP;
    const TcTerm *saved_row = x->row;
    uint32_t saved_rows = x->rows;
    bool expanded = expand(x, q, next);
    x->row = saved_row;
    x->rows = saved_rows;
    if (!expanded) return RUN_FAILED;
    if (next->budget_out) return RUN_STOP;   /* partly expanded: neither outcome nor steps */
    if (x->how.store == TC_STORE_SET && next->len) {   /* a firing that changes nothing is no step */
        if (!next->state) return RUN_FAILED;
        size_t kept = 0;
        for (size_t i = 0; i < next->len; i++)
            if (next->state[i] != q) next->state[kept++] = next->state[i];
        next->len = kept;
    }
    /* step one: each state reached for the first time, by the first member
     * that reaches it (each successor is listed once, members are not) */
    for (size_t m = 0; x->derive && m < x->members; m++) {
        uint32_t to = x->member[m].successor;
        if (!run_reached(g, to) && x->member[m].firing != UINT32_MAX &&
            (to >= x->fired.firsts || !x->fired.first[to]) &&
            !fired_first(&x->fired, to, x->member[m].firing))
            x->derive = false;
    }
    bool ok = run_steps(g, next->state, next->len);
    if (ok && next->ambiguous) ok = list_state(e->o, &x->states, q, true, next->ambiguous);
    else if (ok && next->len == 0) ok = list_state(e->o, &x->states, q, false, 0);
    return ok ? RUN_EXPANDED : RUN_FAILED;
}

TcOutcomes *tc_explore(TcProgram *program, const TcInterpretation *how) {
    TcOutcomes *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    *o = (TcOutcomes){0};
    o->memory_ok = true;
    if (how->backend==TC_BACKEND_C && how->step==TC_STEP_ONE && how->shortcuts!=TC_SHORTCUTS_NONE) resident_prepare(program);
    Explorer x = {.terms = program->terms, .program = program_view(program),
                  .how = *how, .budget = {.limit = how->budget}};
    bool ok = explorer_start(&x), finished = false;
    if (!ok) {
        o->finished = false;
        o->decider_failed = x.flat_failed;
        o->disagreed = x.disagreed;
        o->work = x.budget.spent;
        bool memory_ok = x.flat_failed && !x.solver.failed && !x.terms->failed;
        explorer_free(&x);
        o->memory_ok = memory_ok;
        return o;
    }
    uint32_t initial = explorer_state(&x, program->row, program->rows);
    if (initial == UINT32_MAX) {
        o->finished = false;
        o->decider_failed = x.flat_failed;
        o->disagreed = x.disagreed;
        o->work = x.budget.spent;
        bool memory_ok = x.flat_failed && !x.solver.failed && !x.terms->failed;
        explorer_free(&x);
        o->memory_ok = memory_ok;
        return o;
    }
    if (how->observe == TC_OBSERVE_RUN) {   /* one run instead of every outcome */
        ok = run_path(&x, o, initial, &finished);
        o->states = o->outcomes;
        o->finished = ok && finished;
        o->decider_failed = x.flat_failed;
        o->disagreed = x.disagreed;
        o->work = x.budget.spent;
        bool memory_ok = (ok || x.flat_failed) && !x.solver.failed && !x.terms->failed;
        explorer_free(&x);   /* the term table is the program's again */
        o->memory_ok = memory_ok;
        return o;
    } else {
        /* step one keeps the firings met, to look up independent ones (section 5) */
        x.derive = how->step == TC_STEP_ONE && how->store == TC_STORE_MULTISET &&
                   how->backend == TC_BACKEND_C && how->shortcuts != TC_SHORTCUTS_NONE;
        bool memory_ok;
        {
        Exploration e = {.x = &x, .o = o};
        RunGraph graph = {.keep_steps = true};
        ok = run_breadth_first(&graph, initial, explore_state, &e, &finished);
        e.o->states = graph.len;
        if (ok) e.o->cyclic = run_cyclic(&graph);
        run_graph_free(&graph);
        free(e.next.state);
        e.o->finished = ok && finished;
        e.o->decider_failed = e.x->flat_failed;
        e.o->disagreed = e.x->disagreed;
        e.o->work = e.x->budget.spent;
        bool flat_failed = e.x->flat_failed;
        bool solver_failed = e.x->solver.failed;
        bool terms_failed = e.x->terms->failed;
        memory_ok = (ok || flat_failed) && !solver_failed && !terms_failed;
        explorer_free(e.x);   /* the term table is the program's again */
        }
        o->memory_ok = memory_ok;
        return o;
    }
}

TcOutcomes *tc_successors(TcProgram *program, const TcInterpretation *how,
                          const TcTerm *rows, size_t n) {
    if (n) {
        if (!rows || !row_term_ok(program->terms, rows[0])) return NULL;
        for (size_t i = 1; i < n; i++)
            if (!row_term_ok(program->terms, rows[i])) return NULL;
    }
    TcOutcomes *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    *o = (TcOutcomes){0};
    if (how->backend==TC_BACKEND_C && how->step==TC_STEP_ONE && how->shortcuts!=TC_SHORTCUTS_NONE) resident_prepare(program);
    Explorer x = {.terms = program->terms, .program = program_view(program),
                  .how = *how, .budget = {.limit = how->budget}};
    Successors next = {0};
    bool ok = explorer_start(&x);
    bool expanded = false;
#define TC_SUCCESSORS_RETURN() { \
        o->finished = ok && expanded && !next.budget_out; \
        o->decider_failed = x.flat_failed; \
        o->disagreed = x.disagreed; \
        o->states = x.states.states; \
        o->work = x.budget.spent; \
        free(next.state); \
        bool memory_ok = (ok || x.flat_failed) && !x.solver.failed && !x.terms->failed; \
        explorer_free(&x); \
        o->memory_ok = memory_ok; \
        return o; \
    }
    if (!ok) TC_SUCCESSORS_RETURN();
    uint32_t q = explorer_state(&x, rows, n);
    if (q == UINT32_MAX) { ok = false; TC_SUCCESSORS_RETURN(); }
    if (!spend(&x)) TC_SUCCESSORS_RETURN();   /* unfinished, not a failure */
    expanded = true;
    ok = expand(&x, q, &next);
    if (!ok || next.budget_out) TC_SUCCESSORS_RETURN();
    if (next.ambiguous) {
        ok = list_state(o, &x.states, q, true, next.ambiguous);
        TC_SUCCESSORS_RETURN();
    }
    if (!next.len) TC_SUCCESSORS_RETURN();
    if (!next.state) { ok = false; TC_SUCCESSORS_RETURN(); }
    size_t first = 0;
    while (first < next.len && x.how.store == TC_STORE_SET && next.state[first] == q) first++;
    if (first == next.len) TC_SUCCESSORS_RETURN();
    size_t i = first + 1;
    ok = list_state(o, &x.states, next.state[first], false, 0);
    if (!ok) TC_SUCCESSORS_RETURN();
    for (; i < next.len; i++)
        if (!(x.how.store == TC_STORE_SET && next.state[i] == q))
            if (!list_state(o, &x.states, next.state[i], false, 0)) {
                ok = false;
                TC_SUCCESSORS_RETURN();
            }
    TC_SUCCESSORS_RETURN();
#undef TC_SUCCESSORS_RETURN
}

int tc_quiescent(TcProgram *program, const TcInterpretation *how,
                 const TcTerm *rows, size_t n) {
    TcOutcomes *o = tc_successors(program, how, rows, n);
    if (!o) return -1;
    if (!o->memory_ok || !o->finished) { tc_outcomes_free(o); return -1; }
    int quiescent = o->outcomes == 0;   /* an ambiguous state of unique has no step either */
    tc_outcomes_free(o);
    return quiescent;
}

TcTerm *tc_canonical_key(TcTerms *terms, const TcTerm *rows, size_t n, TcStore store,
                         size_t *count) {
    for (size_t i = 0; i < n; i++)
        if (!row_term_ok(terms, rows[i])) return NULL;
    TcTerm *key = malloc((n ? n : 1) * sizeof *key);
    uint32_t m = (uint32_t)n;
    bool ground;
    if (!key) return NULL;
    if (n) memcpy(key, rows, n * sizeof *key);
    if (!canonical_form(terms, key, &m, store, &ground) || terms->failed) { free(key); return NULL; }
    *count = m;
    return key;
}

/* ---- A firing decided over a host's store ---------------------------------- */

static TcDecision store_decision(TcStoreStatus s) {
    if (s == TC_STORE_INTERRUPTED) return TC_DECIDED_INTERRUPTED;
    if (s == TC_STORE_NO_MEMORY) return TC_DECIDED_NO_MEMORY;
    return TC_DECIDED_INVALIDATED;
}

static TcDecision commit_decision(TcCommitStatus c) {
    switch (c) {
    case TC_COMMIT_DONE: return TC_DECIDED_ADMITTED;
    case TC_COMMIT_REFUSED: return TC_DECIDED_STORE_REFUSED;
    case TC_COMMIT_HOST_ERROR: return TC_DECIDED_HOST_ERROR;
    case TC_COMMIT_INTERRUPTED: return TC_DECIDED_INTERRUPTED;
    default: return TC_DECIDED_NO_MEMORY;
    }
}

/* Enabled (Core's Firing.Shapes): the takes are distinct occurrences, and a
 * read is never a taken one.  Two reads may share an occurrence. */
static bool selection_enabled(const TcOccurrence *selected, uint32_t takes, size_t total) {
    for (uint32_t i = 0; i < takes; i++)
        for (size_t j = i + 1; j < total; j++)
            if (selected[i] == selected[j]) return false;
    return true;
}

/* The match itself: the selected rows imported and unified with the rule's
 * patterns, then the protocol tc_fire, whose admit hook commits through the
 * store (tc_fire_admit). */
static TcDecision decide_match(Explorer *x, uint32_t rule,
                               const TcOccurrence *selected, size_t nselected) {
    TcStoredRule r = rule_at(&x->program, rule);
    size_t total = (size_t)r.takes + r.reads;
    if (nselected != total) return TC_DECIDED_DECLINED;
    if (total) {
        if (!selected || !selection_enabled(selected, r.takes, total))
            return TC_DECIDED_DECLINED;
    }
    if (!frame(x, 0, &r)) return TC_DECIDED_NO_MEMORY;
    if (!x->store.import) return TC_DECIDED_INVALIDATED;
    if (total) {
      if (!selected) return TC_DECIDED_DECLINED;
      for (uint32_t k = 0; k < total; k++) {
        TcTerm row = 0;
        if (!store_spend(x)) return store_decision(x->store_status);
        TcStoreStatus (*import_row)(void *, TcDecideScope *, TcTerms *, TcOccurrence, TcTerm *) =
            x->store.import;
        const void *store = x->store.store;
        TcTerms *terms = x->terms;
        TcOccurrence occurrence = selected[k];
        TcStoreStatus s;
        {
            TcDecideScope scope = {x};
            s = import_row((void *)store, &scope, terms, occurrence, &row);
        }
        if (s != TC_STORE_ROW) return store_decision(s);
        if (!unify(x->terms, &x->solver, pattern_at(&x->program, &r, k), 0, row, 0))
            return x->solver.failed || x->terms->failed ? TC_DECIDED_NO_MEMORY : TC_DECIDED_DECLINED;
      }
    }
    x->fire = r;
    x->fire_rule = rule;
    tc_fire_outcome out = tc_fire(x);
    if (x->store_status != TC_STORE_ROW) return store_decision(x->store_status);
    if (out == tc_fire_answered) return commit_decision(x->commit);
    if (out == tc_fire_declined)
        return x->solver.failed || x->terms->failed ? TC_DECIDED_NO_MEMORY : TC_DECIDED_DECLINED;
    return TC_DECIDED_NO_MEMORY;
}

TcDecision tc_decide(TcProgram *p, uint32_t rule, const TcInterpretation *how,
                     const TcStoreOps *ops, const TcOccurrence *selected, size_t nselected,
                     TcTerms *t, const TcTerm **made, size_t *nmade) {
    *made = NULL;
    *nmade = 0;
    if (!p || !how || !ops || !t || p->terms != t || rule >= p->rules || (nselected && !selected))
        return TC_DECIDED_DECLINED;
    if (!ops->open || !ops->next || !ops->close || !ops->import || !ops->commit)
        return TC_DECIDED_DECLINED;
    TcInterpretation one = *how;
    one.step = TC_STEP_ONE;                       /* one decision, one match */
    one.backend = TC_BACKEND_C;
    Explorer x = {.terms = t, .program = program_view(p),
                  .how = one, .budget = {.limit = one.budget}};
    if (!explorer_start(&x)) { explorer_free(&x); return TC_DECIDED_NO_MEMORY; }
    x.store = *ops;
    x.has_store = true;
    x.selected = selected;
    x.store_status = TC_STORE_ROW;
    x.commit = TC_COMMIT_NO_MEMORY;
    x.terms->decided_len = 0;
    TcDecision d = decide_match(&x, rule, selected, nselected);
    if (d == TC_DECIDED_ADMITTED) {
        const TcTerm *decided = x.terms->decided;
        size_t decided_len = x.terms->decided_len;
        explorer_free(&x);                        /* T's table back, decided rows with it */
        *made = decided;
        *nmade = decided_len;
        return d;
    }
    explorer_free(&x);
    return d;
}

