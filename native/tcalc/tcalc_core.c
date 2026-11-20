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

/* Room for the records of the terms below count. */
static bool walk_reserve(TcTerms *t, size_t count) {
    if (count <= t->seen_cap) return true;
    size_t cap = t->seen_cap ? t->seen_cap : 1024;
    while (cap < count) cap *= 2;
    uint32_t *seen = realloc(t->seen, cap * sizeof *seen);
    if (!seen) return false;
    t->seen = seen;
    TcTerm *value_of = realloc(t->seen_value, cap * sizeof *value_of);
    if (!value_of) return false;
    t->seen_value = value_of;
    memset(t->seen + t->seen_cap, 0, (cap - t->seen_cap) * sizeof *t->seen);
    t->seen_cap = cap;
    return true;
}

static void walk_record(TcTerms *t, TcTerm id, TcTerm value) {
    if (!walk_reserve(t, (size_t)id + 1)) return;
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
    uint32_t *native_out;   size_t native_out_cap;   /* the native term builder's entries */
    uint32_t *native_stack; size_t native_stack_cap; /* its roots and frames, then the entries' terms */
    uint64_t native[TU_FACTS];             /* the native unifier's state words (tcunify.metta) */
    uint64_t native_build[TB_FACTS];       /* the native term builder's (tcbuild.metta) */
    bool failed;
    uint64_t unifications;                 /* work counted: unifications asked */
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
    s->unifications++;
    if (cetta_kernel_requested()==CETTA_KERNEL_X86) return tc_kernel_unify(terms,s,a,abase,b,bbase);
    if (cetta_kernel_note_c) cetta_kernel_note_c();
    return unify_c(terms,s,a,abase,b,bbase);
}

static void solver_free(Solver *s) {
    free(s->local); free(s->unknown); free(s->trail); free(s->work); free(s->build);
    free(s->native_out); free(s->native_stack);
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
 * A state is kept as its rows of fixed terms, a persistent map that shares
 * every node off the paths a step changed with the state it was made from
 * (tcalc_trie.c), and its rows with unknowns in canonical form; its identity is
 * an incremental multiset hash, confirmed by comparing the maps.  The
 * representation, the canonical forms and the table of states, is
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
#include "tcalc_plan.c"

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

/* The store being matched (the view): a state of the table, the one store of
 * a run by delta (VIEW_ONE), or a store of no state (VIEW_BARE: the rows
 * present partway through a round, section 6), whose indexes are not made.
 * Its rows with unknowns are copied out of the table, which may grow while
 * the view is matched. */
enum { VIEW_ONE = UINT32_MAX - 1, VIEW_BARE = UINT32_MAX };

typedef struct {
    uint32_t rows;        /* its rows of fixed terms: a map (tcalc_store.c)        */
    uint32_t state;       /* its state, VIEW_ONE or VIEW_BARE                       */
    uint64_t sum;         /* the hash sum of its rows of fixed terms               */
    uint32_t size;        /* its rows, copies counted                              */
    uint32_t unknowns;    /* the unknowns its rows mention: 0 .. unknowns-1        */
    TcTerm *loose; size_t looses, loose_cap;   /* its rows with unknowns, canonical */
    TcTerm *shape; size_t shape_cap;           /* their shapes                      */
} View;

/* An occurrence of a row: the row's term and which of its copies, packed as
 * copy << 32 | term.  Distinct occurrences are distinct rows of the state,
 * whatever their terms (Core.lean: a row is an occurrence with an identity of
 * its own). */
typedef uint64_t Occurrence;
#define NO_OCCURRENCE UINT64_MAX
static inline Occurrence occurrence(TcTerm row, uint32_t copy) { return (uint64_t)copy << 32 | row; }
static inline TcTerm occurrence_row(Occurrence o) { return (TcTerm)o; }

/* Occurrences counted: an open-addressing table, key 0 empty (no row is term
 * 0).  A key whose count falls to 0 stays, counted 0. */
typedef struct {
    Occurrence *key; size_t key_cap;
    uint32_t *count; size_t count_cap;
    size_t used;
} Occurrences;

static inline uint32_t occurrences_count(const Occurrences *s, Occurrence o) {
    if (!s->key_cap) return 0;
    size_t mask = s->key_cap - 1;
    for (size_t h = row_hash((TcTerm)(o ^ (o >> 29))) & mask; s->key[h]; h = (h + 1) & mask)
        if (s->key[h] == o) return s->count[h];
    return 0;
}

/* Adds delta to the occurrence's count; false when memory ran out. */
static bool occurrences_add(Occurrences *s, Occurrence o, uint32_t delta) {
    if ((s->used + 1) * 2 > s->key_cap) {
        size_t cap = s->key_cap ? s->key_cap * 2 : 16;
        Occurrence *key = calloc(cap, sizeof *key);
        uint32_t *count = calloc(cap, sizeof *count);
        if (!key || !count) { free(key); free(count); return false; }
        for (size_t i = 0; i < s->key_cap; i++) {
            if (!s->key[i]) continue;
            size_t h = row_hash((TcTerm)(s->key[i] ^ (s->key[i] >> 29))) & (cap - 1);
            while (key[h]) h = (h + 1) & (cap - 1);
            key[h] = s->key[i];
            count[h] = s->count[i];
        }
        free(s->key); free(s->count);
        s->key = key;
        s->count = count;
        s->key_cap = s->count_cap = cap;
    }
    size_t mask = s->key_cap - 1, h = row_hash((TcTerm)(o ^ (o >> 29))) & mask;
    for (; s->key[h]; h = (h + 1) & mask)
        if (s->key[h] == o) { s->count[h] += delta; return true; }
    s->key[h] = o;
    s->count[h] = delta;
    s->used++;
    return true;
}

static void occurrences_clear(Occurrences *s) {
    if (s->used) {
        memset(s->key, 0, s->key_cap * sizeof *s->key);
        memset(s->count, 0, s->count_cap * sizeof *s->count);
    }
    s->used = 0;
}

static void occurrences_free(Occurrences *s) { free(s->key); free(s->count); }

/* The work of one exploration.  It holds the program's term table for its
 * duration (tc_explore and tc_successors hand it over and take it back), a
 * view of the program and a copy of the interpretation, so that the matching
 * below reaches every array it uses through the explorer. */
typedef struct {
    TcTerms *terms;
    ProgramView program;
    TcInterpretation how;
    Solver solver;
    Trie trie;             /* the nodes of every map of rows the exploration holds */
    Places places;         /* the runs and indexes met                             */
    StateTable states;
    /* the store being matched; whether its rows mention no unknown; its state's
     * index (UINT32_MAX outside the exploration); the entry view kept while a
     * round's check reads the rows present partway through (section 6) */
    View view;
    bool ground;
    uint32_t at;
    View present;
    /* the bag being fitted (section 6): the occurrences its members take, and
     * those they read, counted per member */
    Occurrences taken, read;
    /* step one: the firings met, kept while `derive` (below); and whether a
     * shortcut, checked, ever found another successor than the full one */
    Fired fired;
    bool derive, disagreed;
    TcTerm *key; size_t key_cap;   /* scratch: the row terms of the match decided */
    /* the matches at it the discipline can use (match_from) */
    TcMember *member; size_t members, member_cap;
    Occurrence *selection; size_t selections, selection_cap;
    /* those matches by their rule and the row terms they select: member index
     * + 1 per slot, 0 empty (see known_selection) */
    uint32_t *known; size_t known_cap, known_used;
    /* scratch: the rows of a successor under construction */
    TcTerm *next; size_t next_len, next_cap;
    /* the selection being chosen: an occurrence for each pattern */
    Occurrence *pick; size_t pick_cap;
    /* the match being decided (the protocol tc_fire): its rule, as stored and
     * as an index, and the mark of the trail before its tells */
    TcStoredRule fire; uint32_t fire_rule; Mark fire_mark;
    RunBudget budget;   /* units of work: states expanded, bags examined, orders tried */
    bool out_of_memory;
    /* backend x86-flat: each rule's packed plan, the selected rows packed four
     * words a row, and whether the decider ever gave no verdict */
    struct FlatPlan *plans; size_t plans_len;
    uint32_t *flat_words; size_t flat_words_cap;
    bool flat_failed;
    uint32_t *batch_cursor; size_t batch_cursor_cap;
    uint32_t *batch_output; size_t batch_output_cap;
    uint64_t batch_used;
    /* the resident batch's rows: the runs its rule meets, each copy, and each
     * one's occurrence */
    uint32_t *batch_row; size_t batch_rows, batch_row_cap;
    Occurrence *batch_occurrence; size_t batch_occurrences, batch_occurrence_cap;
    /* tc_decide: the host's store the match is decided over (0 for the
     * explorer's own states), the selection, how the store answered (ROW
     * until a cursor or an import fails) and how the host's commit went */
    TcStoreOps store;
    bool has_store;
    const TcOccurrence *selected;
    TcStoreStatus store_status;
    TcCommitStatus commit;
    /* scratch of a successor: its rows with unknowns, their shapes, the
     * changes of its rows of fixed terms (row, copies added), the
     * occurrences gone, and rows listed */
    TcTerm *loose_next; size_t loose_nexts, loose_next_cap;
    TcTerm *shape_next; size_t shape_next_cap;
    uint32_t *change; size_t changes, change_cap;
    Occurrence *gone; size_t gones, gone_cap;
    TcTerm *listed; size_t listed_len, listed_cap;
    /* step every over a set store, of a program that propagates, from a
     * state of fixed terms (section 7, tcalc_delta.c): whether the run is
     * kept in one store, and the store; an admitted match then adds the rows
     * it makes to `next` */
    bool delta;
    DeltaStore delta_store;
    /* while a rule's plan runs: whether its steps decide its checks (the
     * protocol then asks none) */
    bool plan_checked;
    /* work counted, for growth gates (tcalc_core.h): rows a pattern was
     * unified with while matching, matches decided, and the rule patterns a
     * matching started from */
    uint64_t met, decided, tried;
} Explorer;

static inline Occurrence pick_at(const Explorer *x, uint32_t k) { return x->pick[k]; }
static inline TcMember member_at(const Explorer *x, size_t m) { return x->member[m]; }
static inline Occurrence selection_at(const Explorer *x, size_t k) { return x->selection[k]; }
static inline void pick_put(Explorer *x, uint32_t k, Occurrence o) { x->pick[k] = o; }
static inline TcTerm loose_at(const View *v, size_t i) { return v->loose[i]; }
static inline TcTerm shape_at(const View *v, size_t i) { return v->shape[i]; }

/* Room for a selection of `count` patterns. */
static bool ensure_pick(Explorer *x, size_t count) {
    if (count <= x->pick_cap) return true;
    Occurrence *grown = realloc(x->pick, count * sizeof *grown);
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

/* Room for `count` rows with unknowns in a view. */
static bool view_room(View *v, size_t count) {
    return reserve(&v->loose, &v->loose_cap, count) && reserve(&v->shape, &v->shape_cap, count);
}

/* The view of state q: its map, and its rows with unknowns copied out. */
static bool view_state(Explorer *x, uint32_t q) {
    TcState s = state_at(&x->states, q);
    View *v = &x->view;
    if (!view_room(v, s.looses)) return false;
    for (uint32_t i = 0; i < s.looses; i++) {
        v->loose[i] = x->states.loose[(size_t)s.loose + i];
        v->shape[i] = x->states.shape[(size_t)s.loose + i];
    }
    v->looses = s.looses;
    v->rows = s.rows;
    v->state = q;
    v->sum = s.sum;
    v->size = s.size;
    v->unknowns = s.unknowns;
    x->ground = s.looses == 0;
    x->at = q;
    return true;
}

/* ---- The rows a probe meets --------------------------------------------------
 * A plan's probe (tcalc_plan.c) meets the rows of its relation whose keys at
 * its index's first L paths are the probed term's terms there, walked: a row
 * of fixed terms holds a fixed term at such a place only when it is that
 * term, and a row with no item there meets no term that has one.  A key that
 * is not fixed when the probe runs (a local bound to a term with unknowns)
 * ends the keys at the level before it.  Over a state's view a probe reads,
 * in turn:
 *   - the rows with unknowns of its relation, and those with no symbol at
 *     their head (an unknown, or an expression headed by one, can meet a
 *     term of any head), less those holding another fixed term at a key's
 *     path;
 *   - the rows of fixed terms: its run's (level 0), or its index's rows under
 *     the key node of its keys.
 * A probe of no fixed relation reads, at level 0, the relation its term walks
 * to when it runs; a term walking to a variable meets every row.  Over the
 * one store of a run by delta a probe reads the store's own lists
 * (tcalc_delta.c); a store of no state (VIEW_BARE: the rows present partway
 * through a round) keeps no index, and its probes read their runs.  Each
 * copy of a row is an occurrence, its copies met one after another. */
typedef struct {
    TrieCursor ground;            /* the rows of fixed terms: one range of a map,
                                     or (every) the runs of the rows map in order   */
    bool open;                    /* the range is open                              */
    bool every;                   /* every run of the rows map, in order            */
    uint32_t order_at;            /* every: the next run in the order of runs       */
    TcTerm g;                     /* the row of fixed terms next (0: none left)     */
    uint32_t g_copies, g_copy;
    uint32_t lo[2], hi[2];        /* the rows with unknowns met: two ranges of the
                                     view's, in canonical order                     */
    uint32_t keys;                /* the keys met, for passing rows with unknowns   */
    uint32_t path[INDEX_PATHS];
    TcTerm key[INDEX_PATHS];
    Occurrence bound;             /* only the occurrences from it on, in canonical
                                     order (NO_OCCURRENCE: every one)               */
    TcTerm bound_shape;
} Candidates;

/* The next row of fixed terms of the cursor, in c->g (0 when none is left). */
static void cand_ground(const Explorer *x, Candidates *c) {
    uint64_t key = 0;
    uint32_t value = 0;
    for (;;) {
        if (c->open && trie_next(&x->trie, &c->ground, &key, &value)) {
            c->g = (TcTerm)key;
            c->g_copies = value;
            c->g_copy = 0;
            return;
        }
        c->open = false;
        if (!c->every || c->order_at >= x->places.orders) { c->g = 0; return; }
        uint32_t run = x->places.order[c->order_at++];
        trie_seek(&x->trie, &c->ground, x->view.rows, map_key(run, 1), map_key(run, UINT32_MAX));
        c->open = true;
    }
}

/* Whether the row with unknowns at position i is passed: it holds another
 * fixed term at a key's path. */
static bool cand_passed(const Explorer *x, const Candidates *c, uint32_t i) {
    for (uint32_t l = 0; l < c->keys; l++) {
        TcTerm at = 0;
        if (path_key(x->terms, loose_at(&x->view, i), c->path[l], &at) && term_node(x->terms, at).ground &&
            at != c->key[l])
            return true;
    }
    return false;
}

/* The canonical order of two rows of the view (by head and length, shape,
 * term): whether a (shape sa) comes before b (shape sb). */
static bool row_before(const TcTerms *t, TcTerm a, TcTerm sa, TcTerm b, TcTerm sb) {
    int c = head_length_order(t->node, a, b);
    if (c) return c < 0;
    return sa != sb ? sa < sb : a < b;
}

/* The copies of the row at position i that come before it. */
static uint32_t loose_copy(const View *v, uint32_t i) {
    uint32_t copy = 0;
    while (copy < i && loose_at(v, i - copy - 1) == loose_at(v, i)) copy++;
    return copy;
}

/* The shape of a row of the view: itself when it is fixed. */
static TcTerm view_shape(const Explorer *x, TcTerm row) {
    if (term_node(x->terms, row).ground) return row;
    for (size_t i = 0; i < x->view.looses; i++)
        if (loose_at(&x->view, i) == row) return shape_at(&x->view, i);
    return row;
}

/* The canonical order of two occurrences of the view (section 3): -1, 0 or 1. */
static int occurrence_order(const Explorer *x, Occurrence a, Occurrence b, TcTerm shape_a, TcTerm shape_b) {
    if (a == b) return 0;
    TcTerm ra = occurrence_row(a), rb = occurrence_row(b);
    if (ra == rb) return (a >> 32) < (b >> 32) ? -1 : 1;
    return row_before(x->terms, ra, shape_a, rb, shape_b) ? -1 : 1;
}

/* Whether the occurrence (its row's shape `shape`) comes before the cursor's
 * bound. */
static bool cand_before(const Explorer *x, const Candidates *c, Occurrence o, TcTerm shape) {
    return c->bound != NO_OCCURRENCE && occurrence_order(x, o, c->bound, shape, c->bound_shape) < 0;
}

/* The next occurrence of the cursor; NO_OCCURRENCE when none is left. */
static Occurrence cand_next(const Explorer *x, Candidates *c) {
    for (int part = 0; part < 2; part++)
        while (c->lo[part] < c->hi[part]) {
            uint32_t i = c->lo[part]++;
            Occurrence o = occurrence(loose_at(&x->view, i), loose_copy(&x->view, i));
            if (!cand_passed(x, c, i) && !cand_before(x, c, o, shape_at(&x->view, i))) return o;
        }
    Occurrence o = NO_OCCURRENCE;
    do {
        if (!c->g || c->g_copy >= c->g_copies) cand_ground(x, c);
        if (!c->g) return NO_OCCURRENCE;
        o = occurrence(c->g, c->g_copy++);
    } while (cand_before(x, c, o, c->g));
    return o;
}

/* The shape of the cursor's bound. */
static void cand_bound_shape(const Explorer *x, Candidates *c) {
    if (c->bound != NO_OCCURRENCE) c->bound_shape = view_shape(x, occurrence_row(c->bound));
}

/* The rows of the relation (head, length): its rows with unknowns, those with
 * no symbol at their head, and its run's rows of fixed terms.  Its run, or
 * UINT32_MAX when it has none. */
static uint32_t cand_relation(const Explorer *x, Candidates *c, uint32_t head, uint32_t length) {
    uint32_t looses = (uint32_t)x->view.looses;
    for (uint32_t i = 0; i < looses; i++) {   /* the rows with unknowns met, by head and length */
        uint32_t h = 0, l = 0;
        row_place(x->terms, loose_at(&x->view, i), &h, &l);
        if (head && h == 0) c->hi[0] = i + 1;
        if (h == head && l == length) {
            if (c->lo[1] == c->hi[1]) c->lo[1] = i;
            c->hi[1] = i + 1;
        }
    }
    uint32_t run = place_find(&x->places, head, length);
    if (run == UINT32_MAX) return run;
    c->open = true;
    trie_seek(&x->trie, &c->ground, x->view.rows, map_key(run, 1), map_key(run, UINT32_MAX));
    return run;
}

/* The rows `term` (at base) can meet at level 0: those of the relation it
 * walks to, or every row when it walks to a variable or to an expression
 * headed by no symbol. */
static void cand_open(const Explorer *x, TcTerm term, uint32_t base, Occurrence bound, Candidates *c) {
    const TcTerms *t = x->terms;
    *c = (Candidates){.bound = bound};
    cand_bound_shape(x, c);
    TcNode pn = term_node(t, walk(t, &x->solver, term, base));
    uint32_t head = 0, length = 0;
    bool every = pn.kind == TC_LOCAL || pn.kind == TC_UNKNOWN;
    if (pn.kind == TC_EXPRESSION && pn.length > 0) {
        TcTerm first = walk(t, &x->solver, term_item(t, pn.start), base);
        if (term_node(t, first).kind != TC_SYMBOL) every = true;
        head = first;
        length = pn.length;
    }
    if (every) {
        c->every = true;
        c->hi[1] = (uint32_t)x->view.looses;
        return;
    }
    (void)cand_relation(x, c, head, length);
}

/* The rows probe `probe` meets over the view, its terms walked at base.
 * False when memory ran out. */
static bool cand_probe(Explorer *x, uint32_t probe, uint32_t base, Occurrence bound, Candidates *c) {
    const ProgramView *v = &x->program;
    uint32_t head = probe_head(v, probe), length = probe_length(v, probe);
    *c = (Candidates){.bound = bound};
    if (head == PLAN_OPEN) { cand_open(x, probe_term(v, probe), base, bound, c); return true; }
    cand_bound_shape(x, c);
    uint32_t run = cand_relation(x, c, head, length), index = probe_index(v, probe);
    for (uint32_t l = 0; l < probe_level(v, probe); l++) {
        TcTerm key = walk(x->terms, &x->solver, probe_key(v, probe, l), base);
        if (!term_node(x->terms, key).ground) break;   /* the keys end at a term not fixed */
        c->path[l] = index_path_at(v, index, l);
        c->key[l] = key;
        c->keys++;
    }
    if (run == UINT32_MAX || !c->keys || x->view.state == VIEW_BARE) return true;   /* the run's rows */
    IndexShape shape = index_shape(v, index);
    uint32_t map = 0, level = shape_kept(&shape, c->keys);   /* a level the index keeps */
    if (!level) return true;   /* the run's rows */
    TcTerm split_key = 0;
    uint32_t split = index_split(v, index, &split_key), cut = 0;
    if (!place_index(&x->places, index, head, length, split, split_key, &cut) ||
        !state_index(&x->states, &x->trie, &x->places, x->terms, x->view.state, index, &shape, &map))
        return false;
    uint32_t node = keys_node(&x->places, index_root(&x->places, index), &shape, c->key, level);
    c->open = node != 0;   /* none: no row of fixed terms has these keys */
    if (node) trie_seek(&x->trie, &c->ground, map, map_key(node, 1), map_key(node, UINT32_MAX));
    return true;
}

/* The rows a probe meets over the one store of a run by delta, at positions
 * lo .. hi-1 (tcalc_delta.c). */
static bool delta_probe(Explorer *x, uint32_t probe, uint32_t base, uint32_t lo, uint32_t hi, DeltaCursor *c);
static uint32_t delta_next(const Explorer *x, DeltaCursor *c);
static inline TcTerm delta_row(const DeltaStore *s, size_t i) { return s->row[i]; }

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

/* ---- Guards --------------------------------------------------------------
 * A guard reads no row: it is decided on the match's values alone, at the
 * most general solution, which is the decision at every solution.  /= holds
 * when the two sides do not unify (two ground terms: when they are two
 * terms).  < and <= hold when both sides are integers in that order; a side
 * that is no integer (an unknown, which some solution makes a symbol, or any
 * other term) fails them.  A sum written (+ A B) is computed exactly: its
 * value, and how it left the 64-bit range (-1 below, 1 above, by less than
 * 2^64, its value then the sum wrapped), so that two such values compare as
 * the integers they are. */
typedef struct { int wrap; int64_t value; } GuardInteger;

/* The integer a term walks to: false when it is no integer. */
static bool guard_integer_term(const Explorer *x, TcTerm term, uint32_t base, int64_t *value) {
    TcNode n = term_node(x->terms, walk(x->terms, &x->solver, term, base));
    if (n.kind != TC_INTEGER) return false;
    *value = n.value;
    return true;
}

static bool guard_integer(const Explorer *x, TcTerm operand, uint32_t base, GuardInteger *out) {
    if (!written_sum(x->terms, operand)) {
        out->wrap = 0;
        return guard_integer_term(x, operand, base, &out->value);
    }
    TcNode n = term_node(x->terms, operand);
    int64_t a = 0, b = 0;
    if (!guard_integer_term(x, term_item(x->terms, n.start + 1), base, &a) ||
        !guard_integer_term(x, term_item(x->terms, n.start + 2), base, &b))
        return false;
    out->wrap = __builtin_add_overflow(a, b, &out->value) ? (a > 0 ? 1 : -1) : 0;
    return true;
}

static bool guard_holds(Explorer *x, TcCheck c, uint32_t base) {
    if (c.kind == TC_CHECK_DIFFERENT) {
        TcTerm left = walk(x->terms, &x->solver, c.left, base);
        TcTerm right = walk(x->terms, &x->solver, c.right, base);
        if (term_node(x->terms, left).ground && term_node(x->terms, right).ground) return left != right;
        Mark m = mark(&x->solver);
        bool meets = unify(x->terms, &x->solver, c.left, base, c.right, base);
        undo(&x->solver, m);
        return !meets && !x->solver.failed;
    }
    GuardInteger a = {0, 0}, b = {0, 0};
    if (!guard_integer(x, c.left, base, &a) || !guard_integer(x, c.right, base, &b)) return false;
    int order = a.wrap != b.wrap ? (a.wrap < b.wrap ? -1 : 1)
              : a.value < b.value ? -1 : a.value > b.value ? 1 : 0;
    return c.kind == TC_CHECK_LESS ? order < 0 : order <= 0;
}

/* A row an absence, a count or a census meets (pattern: its term resolved):
 * counted when it equals the pattern, the check failed (false) when it only
 * unifies with it, which some solution makes a match and another not. */
static bool check_row(Explorer *x, TcCheck c, uint32_t base, TcTerm pattern, TcTerm at, uint64_t *equal) {
    TcTerm row = resolve(x->terms, &x->solver, at, 0);
    if (c.kind != TC_CHECK_ABSENT && row == pattern) { (*equal)++; return true; }
    Mark m = mark(&x->solver);
    bool meets = unify(x->terms, &x->solver, c.left, base, row, 0);
    undo(&x->solver, m);
    return !meets;
}

/* Check i of rule r (at base): a guard or an equation on the match's values,
 * or the rows of the store matched that its pattern meets, read by the probe
 * its rule's plans give it (tcalc_plan.c). */
static bool check_holds(Explorer *x, const TcStoredRule *r, uint32_t i, uint32_t base) {
    TcCheck c = check_at(&x->program, r, i);
    if (check_guard(c.kind)) return guard_holds(x, c, base);
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
    uint32_t probe = plan_check_probe(&x->program, r->same, i);
    if (x->view.state == VIEW_ONE) {   /* the one store's rows */
        DeltaCursor rows;
        if (!delta_probe(x, probe, base, 0, (uint32_t)x->delta_store.rows, &rows)) {
            x->solver.failed = true;
            return false;
        }
        for (uint32_t at; (at = delta_next(x, &rows)) != UINT32_MAX;)
            if (!check_row(x, c, base, pattern, delta_row(&x->delta_store, at), &equal)) return false;
    } else {
        Candidates rows;
        if (!cand_probe(x, probe, base, NO_OCCURRENCE, &rows)) {
            x->solver.failed = true;
            return false;
        }
        for (Occurrence o; (o = cand_next(x, &rows)) != NO_OCCURRENCE;)
            if (!check_row(x, c, base, pattern, occurrence_row(o), &equal)) return false;
    }
    return c.kind != TC_CHECK_COUNT || equal == c.count;
}

static bool checks_hold(Explorer *x, const TcStoredRule *r, uint32_t base) {
    for (uint32_t i = 0; i < r->check_count; i++)
        if (!check_holds(x, r, i, base)) return false;
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

/* ---- The state a firing reaches ---------------------------------------------
 * The view's rows without the occurrences gone, with the made rows x->next,
 * all under the solver's present assignments (Core.lean: Firing.result: the
 * taken rows leave, the made rows arrive with new identities, and the
 * information becomes the knowledge).  The rows of fixed terms are the view's
 * map, changed along the paths of the rows that left and arrived (path
 * copying: every other node is shared), its hash sum by their hashes; the rows
 * with unknowns are resolved, those now fixed join the map, the rest are put
 * in canonical form.  With shortcuts checked the state is also computed in
 * full, every row listed, resolved and put in canonical form and a map made of
 * them, and the two compared. */

/* A row of fixed terms leaves (copies UINT32_MAX) or arrives (1) at the state
 * under construction; in a set store a row present arrives as no change.
 * False when memory ran out. */
static bool state_change(Explorer *x, TcState *s, TcTerm row, uint32_t copies) {
    uint32_t run = row_run(&x->places, x->terms, row, true);
    if (run == UINT32_MAX) return false;
    if (copies == 1 && x->how.store == TC_STORE_SET && trie_get(&x->trie, s->rows, map_key(run, row)))
        return true;
    if (!map_change(&x->trie, &s->rows, run, row, copies, copies)) return false;
    s->sum += copies == 1 ? row_hash(row) : 0 - row_hash(row);
    s->size += copies;
    return tc_push_u32(realloc, &x->change, &x->changes, &x->change_cap, row) &&
           tc_push_u32(realloc, &x->change, &x->changes, &x->change_cap, copies);
}

/* The changes, each row once with its copies added (none of 0 kept),
 * sorted by row.  False when memory ran out. */
static bool changes_merge(Explorer *x) {
    size_t n = x->changes / 2;
    for (size_t i = 1; i < n; i++)   /* insertion sort: a firing changes a few rows */
        for (size_t j = i; j > 0 && x->change[2 * (j - 1)] > x->change[2 * j]; j--)
            for (size_t w = 0; w < 2; w++) {
                uint32_t tmp = x->change[2 * j + w];
                x->change[2 * j + w] = x->change[2 * (j - 1) + w];
                x->change[2 * (j - 1) + w] = tmp;
            }
    size_t kept = 0;
    for (size_t i = 0; i < n; i++) {
        if (kept && x->change[2 * (kept - 1)] == x->change[2 * i]) {
            x->change[2 * (kept - 1) + 1] += x->change[2 * i + 1];
            if (!x->change[2 * (kept - 1) + 1]) kept--;
            continue;
        }
        x->change[2 * kept] = x->change[2 * i];
        x->change[2 * kept + 1] = x->change[2 * i + 1];
        kept++;
    }
    x->changes = 2 * kept;
    return true;
}

/* Whether the occurrence is among the n gone. */
static bool occurrence_gone(const Occurrence *gone, size_t n, Occurrence o) {
    for (size_t i = 0; i < n; i++) if (gone[i] == o) return true;
    return false;
}

/* A resolved row arrives: a fixed one at the map, one with unknowns among
 * x->loose_next.  False when memory ran out. */
static bool state_arrive(Explorer *x, TcState *s, TcTerm row) {
    if (!row) return false;
    if (term_node(x->terms, row).ground) return state_change(x, s, row, 1);
    return tc_push_u32(realloc, &x->loose_next, &x->loose_nexts, &x->loose_next_cap, row);
}

/* Enters the state of the map s.rows (held) and the rows with unknowns
 * x->loose_next (put in canonical form here), made from `parent` by the
 * changes x->change.  UINT32_MAX when memory ran out. */
static uint32_t state_close(Explorer *x, TcState s, uint32_t parent) {
    uint32_t n = (uint32_t)x->loose_nexts;
    bool ground = true;
    if (!reserve(&x->shape_next, &x->shape_next_cap, n ? n : 1) ||
        (n && !canonical_form(x->terms, x->loose_next, &n, x->how.store, &ground, x->shape_next))) {
        trie_release(&x->trie, s.rows);
        return UINT32_MAX;
    }
    s.looses = n;
    s.size += n;
    s.unknowns = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t m = max_unknown(x->terms, x->loose_next[i]);
        if (m > s.unknowns) s.unknowns = m;
    }
    s.parent = parent;
    if (parent == UINT32_MAX) x->changes = 0;
    else if (!changes_merge(x)) { trie_release(&x->trie, s.rows); return UINT32_MAX; }
    if (x->trie.failed) { trie_release(&x->trie, s.rows); return UINT32_MAX; }
    return state_enter(&x->states, &x->trie, s, x->loose_next, x->shape_next, x->change,
                       (uint32_t)(x->changes / 2));
}

/* The state of rows in canonical form (canonical_form), made from no other.
 * UINT32_MAX when memory ran out. */
static uint32_t state_of_rows(Explorer *x, const TcTerm *rows, uint32_t n) {
    TcState s = {.parent = UINT32_MAX};
    x->changes = 0;
    x->loose_nexts = 0;
    for (uint32_t i = 0; i < n; i++)
        if (!state_arrive(x, &s, rows[i])) { trie_release(&x->trie, s.rows); return UINT32_MAX; }
    return state_close(x, s, UINT32_MAX);
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
    if (canonical_form(x->terms, rows, &count, x->how.store, &ground, NULL))
        q = state_of_rows(x, rows, count);
    free(rows);
    return q;
}

/* The successor computed in full (above).  UINT32_MAX when memory ran out. */
static uint32_t full_successor(Explorer *x, const Occurrence *gone, size_t ngone) {
    x->listed_len = 0;
    if (!store_rows(&x->trie, &x->places, x->terms, x->view.rows, x->view.loose, x->view.shape,
                    (uint32_t)x->view.looses, &x->listed, &x->listed_len, &x->listed_cap))
        return UINT32_MAX;
    size_t kept = 0;
    for (size_t i = 0; i < x->listed_len; i++) {   /* listed rows: copies adjacent */
        uint32_t copy = 0;
        while (copy < i && x->listed[i - copy - 1] == x->listed[i]) copy++;
        if (occurrence_gone(gone, ngone, occurrence(x->listed[i], copy))) continue;
        TcTerm row = resolve(x->terms, &x->solver, x->listed[i], 0);
        if (!row) return UINT32_MAX;
        x->listed[kept++] = row;
    }
    x->listed_len = kept;
    for (size_t i = 0; i < x->next_len; i++) {
        TcTerm row = resolve(x->terms, &x->solver, x->next[i], 0);
        if (!row || !tc_push_u32(realloc, &x->listed, &x->listed_len, &x->listed_cap, row)) return UINT32_MAX;
    }
    uint32_t n = (uint32_t)x->listed_len;
    bool ground;
    if (x->solver.failed || x->terms->failed ||
        !canonical_form(x->terms, x->listed, &n, x->how.store, &ground, NULL))
        return UINT32_MAX;
    return state_of_rows(x, x->listed, n);
}

static uint32_t successor_state(Explorer *x, const Occurrence *gone, size_t ngone) {
    if (x->solver.failed || x->terms->failed) return UINT32_MAX;
    TcState s = {.rows = x->view.rows, .sum = x->view.sum,
                 .size = x->view.size - (uint32_t)x->view.looses};
    trie_retain(&x->trie, s.rows);
    x->changes = 0;
    x->loose_nexts = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < ngone; i++)
        if (term_node(x->terms, occurrence_row(gone[i])).ground)
            ok = state_change(x, &s, occurrence_row(gone[i]), UINT32_MAX);
    for (uint32_t i = 0; ok && i < x->view.looses; i++)
        if (!occurrence_gone(gone, ngone, occurrence(loose_at(&x->view, i), loose_copy(&x->view, i))))
            ok = state_arrive(x, &s, resolve(x->terms, &x->solver, loose_at(&x->view, i), 0));
    for (size_t i = 0; ok && i < x->next_len; i++)
        ok = state_arrive(x, &s, resolve(x->terms, &x->solver, x->next[i], 0));
    if (!ok || x->solver.failed || x->terms->failed) { trie_release(&x->trie, s.rows); return UINT32_MAX; }
    uint32_t parent = x->view.state < VIEW_ONE ? x->view.state : UINT32_MAX;
    uint32_t q = state_close(x, s, parent);
    if (x->how.shortcuts != TC_SHORTCUTS_CHECKED || q == UINT32_MAX) return q;
    uint32_t full = full_successor(x, gone, ngone);
    if (full != q) x->disagreed = true;
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
        if (!tc_push_u64(realloc, &x->selection, &x->selections, &x->selection_cap, x->pick[i]))
            return false;
    return tc_push_member(realloc, &x->member, &x->members, &x->member_cap, e);
}

/* Two matches of one rule that select the same row terms, though not the same
 * rows, are two members of the step (distinct occurrences), with one admission
 * and one successor: a state is a bag of terms.  The members met at a state are
 * kept by rule and row terms, so that a successor is computed once.  The table
 * is a shortcut: a member it cannot hold is computed again, to the same end. */
static uint64_t selection_hash(uint32_t rule, const Occurrence *pick, uint32_t total) {
    uint64_t h = (rule + 1) * UINT64_C(0x9E3779B97F4A7C15);
    for (uint32_t i = 0; i < total; i++) h = (h ^ occurrence_row(pick[i])) * UINT64_C(0x100000001B3);
    return h ^ (h >> 29);
}

/* Whether a member with rule and the row terms of x->pick was met at this
 * state; then *met is that member. */
static bool known_selection(const Explorer *x, uint32_t rule, uint32_t total, TcMember *met) {
    for (size_t i = x->known_cap ? selection_hash(rule, x->pick, total) & (x->known_cap - 1) : 0;
         x->known_cap && x->known[i]; i = (i + 1) & (x->known_cap - 1)) {
        const TcMember *e = &x->member[x->known[i] - 1];
        const Occurrence *other = x->selection + e->start;
        bool same = e->rule == rule;
        for (uint32_t j = 0; same && j < total; j++)
            same = occurrence_row(other[j]) == occurrence_row(x->pick[j]);
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
            size_t j = selection_hash(e->rule, x->selection + e->start, n) & (cap - 1);
            while (grown[j]) j = (j + 1) & (cap - 1);
            grown[j] = x->known[i];
        }
        free(x->known);
        x->known = grown;
        x->known_cap = cap;
    }
    const TcMember *e = &x->member[m];
    size_t j = selection_hash(e->rule, x->selection + e->start, total) & (x->known_cap - 1);
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
    x->next_len = 0;
    return append_made(x, r, 0) ? successor_state(x, x->pick, r->takes) : UINT32_MAX;
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
    for (uint32_t i = 0; i < total; i++) x->key[i] = occurrence_row(x->pick[i]);
    return true;
}

/* A positive rule (Scope.Positive): its checks are equations and guards,
 * each decided on the match's own values; it has no absence, count or census,
 * which read the rest of the state. */
static bool positive_rule(const ProgramView *v, uint32_t rule) {
    TcStoredRule r = rule_at(v, rule);
    for (uint32_t i = 0; i < r.check_count; i++)
        if (!check_binary(check_at(v, &r, i).kind)) return false;
    return true;
}

/* How many rows of state q, a state of fixed terms, are the term. */
static uint32_t state_count(const Explorer *x, uint32_t q, TcTerm term) {
    uint32_t head = 0, length = 0;
    row_place(x->terms, term, &head, &length);
    uint32_t run = place_find(&x->places, head, length);
    return run == UINT32_MAX ? 0 : trie_get(&x->trie, state_at(&x->states, q).rows, map_key(run, term));
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
        if (!earlier && takes + (read ? 1u : 0u) > state_count(x, q, t))
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
    if (state_at(&x->states, p).looses || !positive_rule(&x->program, x->fire_rule) ||
        !positive_rule(&x->program, a_rule) || !selection_key(x, total))
        return UINT32_MAX;
    uint32_t u = fired_find(f, p, x->fire_rule, x->key, total);   /* p --u--> p.u */
    if (u == UINT32_MAX) return UINT32_MAX;
    uint32_t far = fired_find(f, f->firing[4 * (size_t)u + 3], a_rule, a_terms,
                              fired_terms(f, a));                  /* p.u --a--> */
    if (far == UINT32_MAX) return UINT32_MAX;
    uint32_t target = f->firing[4 * (size_t)far + 3];
    if (state_at(&x->states, target).looses ||
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
/* The checks of the match: none left when the steps of its plan decided
 * them (tcalc_plan.c: placed checks, decided once their values are fixed). */
static bool tc_fire_checks(Explorer *x) {
    TcStoredRule r = x->fire;
    return x->plan_checked || checks_hold(x, &r, 0);
}
static bool tc_fire_checks_first(Explorer *x) { return tc_fire_checks(x); }
static bool tc_fire_loose(Explorer *x) { return loose_members(x->how.step); }

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

/* Whether a step reads a member's own successor: step one steps to it and
 * unique to its one member's; every other step reads bags of members, whose
 * results it computes, so a member's own successor is never asked for. */
static bool own_successors_read(TcStep step) { return step == TC_STEP_ONE || step == TC_STEP_UNIQUE; }

/* Admitted on its own: a member, with its successor when the step reads it,
 * looked up (the second order of an independent pair met before) or computed.
 * With shortcuts checked it is computed always, and compared. */
static bool tc_fire_admit(Explorer *x) {
    TcStoredRule r = x->fire;
    if (x->has_store) return store_admit(x, &r);
    if (x->delta) return append_made(x, &r, 0);   /* one store: its rows made */
    uint32_t total = r.takes + r.reads;
    if (!own_successors_read(x->how.step)) {   /* no successor (0), as for a loose member */
        if (!record_member(x, x->fire_rule, total, true, 0, UINT32_MAX)) return false;
        remember_selection(x, (uint32_t)x->members - 1, total);
        return true;
    }
    uint32_t succ = derived_successor(x, total);
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

/* The match of rule `rule` and the selection x->pick, its patterns unified
 * with its rows: decided, or the decision of the same row terms met before
 * reused. */
static bool decide_selection(Explorer *x, uint32_t rule) {
    TcStoredRule r = rule_at(&x->program, rule);
    uint32_t total = r.takes + r.reads;
    TcMember same = {0};
    x->decided++;
    if (known_selection(x, rule, total, &same))   /* the same row terms: the same decision */
        return record_member(x, rule, total, same.alone, same.successor, same.firing);
    x->fire = r;
    x->fire_rule = rule;
    return tc_fire(x) != tc_fire_engine_failure && !x->solver.failed;
}

/* ---- Running a plan ---------------------------------------------------------
 * A rule's matches are met by running one of its plans (tcalc_plan.c): each
 * probe's rows in turn, each unified with its pattern, each check decided at
 * its step, the match decided at the end.  This is the one way a rule is run:
 * over a state's view (the matches the discipline can use, each recorded as a
 * member), and over the one store of a run by delta (each match admitted adds
 * the rows it makes; tcalc_delta.c), where the plan entering by pattern d
 * meets d's rows from delta_lo on, the rows before delta_lo for the patterns
 * written before d, and every row for those written after it (the
 * semi-naive split).
 *
 * Over a view the matches are those the discipline can use: each one admitted
 * on its own, and for bag, synchronous and maximal also each one whose own
 * information (its matching and its tells) is solvable.  Its takes are
 * distinct rows and a read is never a taken row (Core.lean: Firing.Shapes and
 * Enabled), whatever order the plan meets them in.  Under step one a step is
 * one match and a state is a bag of terms, so two selections that differ only
 * in which copy of a row term they choose reach the same state: at each probe
 * one copy of each row term is tried (a row's copies are met one after
 * another).  The other steps count every copy, since their members are
 * occurrences. */
typedef struct {
    uint32_t rule, plan;
    uint32_t entry;       /* by delta: the pattern the delta's rows enter by; UINT32_MAX: none */
    uint32_t delta_lo, size;
    bool checks;          /* the plan's steps decide the checks */
} PlanRun;

static bool plan_from(Explorer *x, const PlanRun *run, uint32_t k);
static bool delta_decide(Explorer *x, uint32_t rule);

/* Whether the plan's probes before op k met pattern j. */
static bool plan_met(const ProgramView *v, uint32_t plan, uint32_t k, uint32_t j) {
    for (uint32_t i = 0; i < k; i++)
        if (plan_op(v, plan, i, 0) == PLAN_PROBE && plan_op(v, plan, i, 1) == j) return true;
    return false;
}

/* Whether occurrence o may not be met by pattern `pattern` at op k: a take
 * meets a row no pattern met before it holds, a read one no take met before
 * it holds. */
static bool plan_clash(const Explorer *x, const PlanRun *run, const TcStoredRule *r, uint32_t k, uint32_t pattern,
                       Occurrence o) {
    bool take = pattern < r->takes;
    for (uint32_t i = 0; i < k; i++) {
        if (plan_op(&x->program, run->plan, i, 0) != PLAN_PROBE) continue;
        uint32_t j = plan_op(&x->program, run->plan, i, 1);
        if ((take || j < r->takes) && pick_at(x, j) == o) return true;
    }
    return false;
}

/* A row the probe at op k meets, as pattern_k's: unified with its pattern
 * (the unify after the probe), the plan run on from op k + 2.  False when
 * memory ran out. */
static bool plan_row(Explorer *x, const PlanRun *run, uint32_t k, TcTerm pattern, uint32_t pattern_k,
                     Occurrence o) {
    Mark m = mark(&x->solver);
    x->met++;
    pick_put(x, pattern_k, o);
    bool ok = !unify(x->terms, &x->solver, pattern, 0, occurrence_row(o), 0) || plan_from(x, run, k + 2);
    undo(&x->solver, m);
    return ok && !x->solver.failed;
}

/* The probe at op k and the unify after it: each row the probe meets, unified
 * with its pattern, the plan run on from op k + 2. */
static bool plan_probe(Explorer *x, const PlanRun *run, uint32_t k) {
    TcStoredRule r = rule_at(&x->program, run->rule);
    uint32_t pattern_k = plan_op(&x->program, run->plan, k, 1);
    bool take = pattern_k < r.takes;
    TcTerm pattern = pattern_at(&x->program, &r, pattern_k);
    uint32_t lo = 0, hi = run->size;
    if (x->delta && run->entry != UINT32_MAX) {   /* the semi-naive split */
        if (pattern_k == run->entry) lo = run->delta_lo;
        else if (pattern_k < run->entry) hi = run->delta_lo;
        if (lo >= hi) return true;
    }
    Occurrence bound = NO_OCCURRENCE;
    if (!x->delta && x->how.matches == TC_MATCHES_SYMMETRIC) {
        uint32_t twin = identical_before(x, &r, pattern_k);   /* from the twin's row on (a take: past it) */
        if (twin != UINT32_MAX && plan_met(&x->program, run->plan, k, twin))
            bound = pick_at(x, twin) + (take ? (uint64_t)1 << 32 : 0);
    }
    uint32_t probe = plan_op(&x->program, run->plan, k, 2);
    if (x->delta) {   /* the one store's rows in the window */
        DeltaCursor rows;
        if (!delta_probe(x, probe, 0, lo, hi, &rows)) { x->solver.failed = true; return false; }
        for (uint32_t at; (at = delta_next(x, &rows)) != UINT32_MAX;)
            if (!plan_row(x, run, k, pattern, pattern_k, occurrence(delta_row(&x->delta_store, at), 0)))
                return false;
        return true;
    }
    Candidates rows;
    if (!cand_probe(x, probe, 0, bound, &rows)) { x->solver.failed = true; return false; }
    bool one = x->how.step == TC_STEP_ONE;
    TcTerm tried = 0;   /* the row term last tried at this probe */
    for (Occurrence o; (o = cand_next(x, &rows)) != NO_OCCURRENCE;) {
        if (plan_clash(x, run, &r, k, pattern_k, o)) continue;
        if (one) {
            if (occurrence_row(o) == tried) continue;   /* another copy of the term just tried */
            tried = occurrence_row(o);
        }
        if (!plan_row(x, run, k, pattern, pattern_k, o)) return false;
    }
    return true;
}

/* Runs the plan from op k: its probes, its checks (when the plan decides
 * them), and the match decided where the tells, takes and makes begin (the
 * protocol tc_fire's). */
static bool plan_from(Explorer *x, const PlanRun *run, uint32_t k) {
    TcStoredRule r = rule_at(&x->program, run->rule);
    for (; k < plan_ops(&x->program, run->plan); k++) {
        uint32_t op = plan_op(&x->program, run->plan, k, 0);
        if (op == PLAN_PROBE) return plan_probe(x, run, k);
        if (op != PLAN_CHECK && op != PLAN_ABSENT) break;
        if (run->checks && !check_holds(x, &r, plan_op(&x->program, run->plan, k, 1), 0))
            return !x->solver.failed && !x->terms->failed;
    }
    return x->delta ? delta_decide(x, run->rule) : decide_selection(x, run->rule);
}

/* Whether a plan's steps decide rule r's checks: the values each reads are
 * fixed at its step when the store matched holds fixed terms and the rule
 * tells nothing (tcalc_plan.c), and a match only the joint information of a
 * bag admits is a member though its own checks fail, so bag, synchronous and
 * maximal ask them at the end. */
static bool plan_checks(const Explorer *x, const TcStoredRule *r) {
    return x->ground && !x->has_store && r->tells == 0 && !loose_members(x->how.step);
}

/* Runs rule `rule`'s plan entering by pattern `entry` (UINT32_MAX: its plan
 * of no entry), over the one store's window (delta_lo, size) when the run is
 * by delta. */
static bool plan_run(Explorer *x, uint32_t rule, uint32_t entry, uint32_t delta_lo, uint32_t size) {
    TcStoredRule r = rule_at(&x->program, rule);
    uint32_t total = r.takes + r.reads;
    PlanRun run = {rule, plan_of(&x->program, rule, entry == UINT32_MAX ? total : entry), entry, delta_lo, size,
                   plan_checks(x, &r)};
    if (total) x->tried++;
    x->plan_checked = run.checks;
    bool ok = plan_from(x, &run, 0);
    x->plan_checked = false;
    return ok;
}

/* The order of two members: by rule, then each pattern's occurrence in the
 * canonical order of the view (section 3); -1, 0 or 1. */
static int member_order(const Explorer *x, TcMember a, TcMember b) {
    if (a.rule != b.rule) return a.rule < b.rule ? -1 : 1;
    TcStoredRule r = rule_at(&x->program, a.rule);
    for (uint32_t k = 0; k < r.takes + r.reads; k++) {
        Occurrence oa = selection_at(x, a.start + k), ob = selection_at(x, b.start + k);
        int c = occurrence_order(x, oa, ob, view_shape(x, occurrence_row(oa)), view_shape(x, occurrence_row(ob)));
        if (c) return c;
    }
    return 0;
}

/* The members in the order of their selections (member_order), so that a
 * state's matches, and so its steps, come in one order whatever order its
 * rules' plans meet them in.  False when memory ran out. */
static bool members_sort(Explorer *x) {
    size_t n = x->members;
    if (n < 2) return true;
    TcMember *sorted = malloc(n * sizeof *sorted), *tmp = malloc(n * sizeof *tmp);
    if (!sorted || !tmp) { free(sorted); free(tmp); return false; }
    for (size_t i = 0; i < n; i++) sorted[i] = member_at(x, i);
    for (size_t width = 1; width < n; width *= 2) {   /* merge sort, stable */
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                tmp[k++] = member_order(x, sorted[i], sorted[j]) <= 0 ? sorted[i++] : sorted[j++];
            while (i < mid) tmp[k++] = sorted[i++];
            while (j < hi) tmp[k++] = sorted[j++];
        }
        memcpy(sorted, tmp, n * sizeof *sorted);
    }
    for (size_t i = 0; i < n; i++) x->member[i] = sorted[i];
    free(sorted); free(tmp);
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
    if (x->how.backend == TC_BACKEND_X86_FLAT) return flat_collect(x) && members_sort(x);
    uint32_t state_unknowns = x->view.unknowns;   /* the state's unknowns are 0 .. state_unknowns-1 */
    x->ground = x->view.looses == 0;
    for (uint32_t rule = 0; rule < x->program.rules; rule++) {
        TcStoredRule r = rule_at(&x->program, rule);
        if (r.same != rule) continue;   /* that rule again: its matches are met there */
        if (!ensure_pick(x, (size_t)r.takes + r.reads + 1) ||
            !reserve_unknowns(&x->solver, state_unknowns) || !frame(x, 0, &r)) return false;
        if (x->how.step==TC_STEP_ONE && x->how.shortcuts!=TC_SHORTCUTS_NONE && x->ground && rule<x->program.prepared_count &&
            x->program.prepared[rule].guards!=UINT32_MAX && cetta_kernel_batch) {
            if (!resident_collect(x,rule,&x->program.prepared[rule])) return false;
        } else if (!plan_run(x, rule, UINT32_MAX, 0, 0)) return false;
    }
    return members_sort(x);
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

/* A bag of members; the occurrences they take and read are the explorer's
 * (x->taken, x->read), since one bag is fitted at a time. */
typedef struct {
    uint32_t *member; size_t len, cap;   /* member indices, in order of addition */
    uint32_t *base;   size_t blen, bcap; /* each member's frame base             */
    uint32_t next_base;
    bool snapshot;   /* each member enabled on its own (synchronous), not the
                        joint selection as one commit                         */
    bool tells;      /* a member's tells are solved as it joins; otherwise the
                        bag's checks are asked first (before-tells)            */
} Bag;

/* One proposed order of a bag's members, with their frame bases. */
typedef struct {
    uint32_t *member; size_t len, cap;
    uint32_t *base; size_t blen, bcap;
    uint32_t next_base;
} RunOrder;

static const Occurrence no_occurrences[1];

static const Occurrence *member_pick(const Explorer *x, const TcMember *e) {
    return x->selection ? x->selection + e->start : no_occurrences;
}

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
        Occurrence o = selection_at(x, e.start + i);
        if (occurrences_count(&x->taken, o) || (!b->snapshot && occurrences_count(&x->read, o))) return false;
    }
    if (!b->snapshot)
        for (uint32_t i = r.takes; i < r.takes + r.reads; i++)
            if (occurrences_count(&x->taken, selection_at(x, e.start + i))) return false;
    uint32_t base = b->next_base;
    if (!frame(x, base, &r)) return false;   /* memory ran out: the solver says so */
    for (uint32_t i = 0; i < r.takes + r.reads; i++)   /* the takes, then the reads */
        if (!unify(x->terms, &x->solver, pattern_at(&x->program, &r, i), base,
                   occurrence_row(selection_at(x, e.start + i)), 0))
            return false;
    if (b->tells && !tells_hold(x, &r, base)) return false;
    bool ok = true;
    for (uint32_t i = 0; ok && i < r.takes; i++) ok = occurrences_add(&x->taken, selection_at(x, e.start + i), 1);
    for (uint32_t i = r.takes; ok && i < r.takes + r.reads; i++)
        ok = occurrences_add(&x->read, selection_at(x, e.start + i), 1);
    if (!ok || !bag_push(b, m, base)) {
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
    const Occurrence *pick = member_pick(x, e);
    for (uint32_t i = 0; i < r->takes; i++) (void)occurrences_add(&x->taken, pick[i], UINT32_MAX);
    for (uint32_t i = r->takes; i < r->takes + r->reads; i++) (void)occurrences_add(&x->read, pick[i], UINT32_MAX);
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

/* The occurrences the members of the n members `member` take, in x->gone.
 * False when memory ran out. */
static bool members_taken(Explorer *x, const uint32_t *member, size_t n) {
    x->gones = 0;
    for (size_t k = 0; k < n; k++) {
        const TcMember *e = &x->member[member[k]];
        const Occurrence *pick = member_pick(x, e);
        for (uint32_t i = 0; i < x->program.rule[e->rule].takes; i++)
            if (!tc_push_u64(realloc, &x->gone, &x->gones, &x->gone_cap, pick[i])) return false;
    }
    return true;
}

/* The state the bag reaches: the entry rows it leaves, with the rows it made.
 * UINT32_MAX when memory ran out. */
static uint32_t bag_result(Explorer *x, const Bag *b) {
    x->next_len = 0;
    for (size_t k = 0; k < b->len; k++) {
        TcStoredRule r = rule_at(&x->program, member_at(x, bag_member(b, k)).rule);
        if (!append_made(x, &r, bag_base(b, k))) return UINT32_MAX;
    }
    if (!members_taken(x, b->member, b->len)) return UINT32_MAX;
    return successor_state(x, x->gone, x->gones);
}

/* Copies a view (its map stays its holder's). */
static bool view_copy(View *to, const View *from) {
    if (!view_room(to, from->looses)) return false;
    for (size_t i = 0; i < from->looses; i++) {
        to->loose[i] = from->loose[i];
        to->shape[i] = from->shape[i];
    }
    to->looses = from->looses;
    to->rows = from->rows;
    to->state = from->state;
    to->sum = from->sum;
    to->size = from->size;
    to->unknowns = from->unknowns;
    return true;
}

/* The rows present partway through a round become the view: the entry view's
 * rows (kept in x->present) without the occurrences gone (x->gone), with the
 * rows made so far (x->next), all resolved, its rows with unknowns in the
 * order of head, length and term.  The view holds its map; false when memory
 * ran out. */
static bool present_view(Explorer *x) {
    View *v = &x->view;
    uint32_t rows = x->present.rows;
    v->rows = 0;
    v->state = VIEW_BARE;
    v->looses = 0;
    trie_retain(&x->trie, rows);
    bool ok = true;
    for (size_t i = 0; ok && i < x->gones; i++) {
        TcTerm row = occurrence_row(x->gone[i]);
        if (term_node(x->terms, row).ground)
            ok = map_change(&x->trie, &rows, row_run(&x->places, x->terms, row, true), row, UINT32_MAX, UINT32_MAX);
    }
    size_t entry = x->present.looses;
    for (size_t i = 0; ok && i < entry + x->next_len; i++) {
        TcTerm row = i < entry ? loose_at(&x->present, i) : x->next[i - entry];
        if (i < entry && occurrence_gone(x->gone, x->gones, occurrence(row, loose_copy(&x->present, (uint32_t)i))))
            continue;
        row = resolve(x->terms, &x->solver, row, 0);
        if (!row) { ok = false; break; }
        if (term_node(x->terms, row).ground) {
            uint32_t run = row_run(&x->places, x->terms, row, true);
            ok = run != UINT32_MAX && map_change(&x->trie, &rows, run, row, 1, 1);
            continue;
        }
        ok = view_room(v, v->looses + 1);
        if (ok) { v->loose[v->looses] = row; v->shape[v->looses] = row; v->looses++; }
    }
    v->rows = rows;
    if (!ok) return false;
    const TcNode *nodes = x->terms->node;
    for (size_t i = 1; i < v->looses; i++)   /* by head, length and term (no shapes: a check reads no order) */
        for (size_t j = i; j > 0 && ground_order(nodes, v->loose[j], v->loose[j - 1]) < 0; j--) {
            TcTerm tmp = v->loose[j];
            v->loose[j] = v->loose[j - 1];
            v->loose[j - 1] = tmp;
            v->shape[j] = v->loose[j];
            v->shape[j - 1] = v->loose[j - 1];
        }
    return true;
}

/* Fires the members of a bag one at a time in the given order, each admitted
 * on its own at the state the ones before it left; returns the state reached,
 * or UINT32_MAX - 1 when some member is not admitted, UINT32_MAX on memory. */
static uint32_t run_in_order(Explorer *x, const RunOrder *b, const uint32_t *order) {
    x->gones = 0;
    x->next_len = 0;
    for (size_t k = 0; k < b->len; k++) {
        const TcMember *e = &x->member[b->member[order[k]]];
        TcStoredRule rule = x->program.rule[e->rule];
        const Occurrence *pick = member_pick(x, e);
        uint32_t base = b->base[order[k]];
        bool ok = true;
        for (uint32_t i = 0; ok && i < rule.takes + rule.reads; i++)
            ok = !occurrence_gone(x->gone, x->gones, pick[i]);
        for (uint32_t i = 0; ok && i < rule.takes + rule.reads; i++)   /* the takes, then the reads */
            ok = unify(x->terms, &x->solver, pattern_at(&x->program, &rule, i), base,
                       occurrence_row(pick[i]), 0);
        if (ok) {   /* the checks read the rows present now, the entry view kept aside */
            if (!view_copy(&x->present, &x->view)) return UINT32_MAX;
            bool built = present_view(x);
            ok = built && admitted(x, &rule, base);
            uint32_t present = x->view.rows;
            bool restored = view_copy(&x->view, &x->present);
            trie_release(&x->trie, present);
            if (!built || !restored) return UINT32_MAX;
        }
        if (!ok) return x->solver.failed ? UINT32_MAX : UINT32_MAX - 1;
        for (uint32_t i = 0; i < rule.takes; i++)
            if (!tc_push_u64(realloc, &x->gone, &x->gones, &x->gone_cap, pick[i])) return UINT32_MAX;
        for (uint32_t i = 0; i < rule.makes; i++) {
            TcTerm made = close_term(x->terms, &x->solver, make_at(&x->program, &rule, i), base);
            if (!made || !next_push(x, made)) return UINT32_MAX;
        }
    }
    return successor_state(x, x->gone, x->gones);
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

/* every: the state the bag of all the region's matches reaches, each match's
 * checks met on its own (*result; UINT32_MAX - 1 when the region has no match
 * or its matches do not fit together).  False when memory ran out. */
static bool every_result(Explorer *x, Bag *b, uint32_t *result) {
    b->len = b->blen = 0;
    b->next_base = 0;
    occurrences_clear(&x->taken);
    occurrences_clear(&x->read);
    Mark m = mark(&x->solver);
    size_t in = 0, fitted = 0;
    for (uint32_t k = 0; k < x->members; k++)
        if (in_region(x, x->member[k].rule)) {
            in++;
            if (!bag_add(x, b, k)) break;
            fitted++;
        }
    *result = UINT32_MAX - 1;
    if (in > 0 && fitted == in) *result = bag_result(x, b);
    undo(&x->solver, m);
    return *result != UINT32_MAX && !x->solver.failed;
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
    size_t bag_cap = x->members ? x->members : 1;
    b.member = malloc(bag_cap * sizeof *b.member);
    b.base = malloc(bag_cap * sizeof *b.base);
    b.cap = b.bcap = bag_cap;
    if (!b.member || !b.base) { free(b.member); free(b.base); return false; }
    occurrences_clear(&x->taken);
    occurrences_clear(&x->read);
    bool ok = true;
    if (ok && step == TC_STEP_EVERY) {
        uint32_t result = UINT32_MAX - 1;
        ok = every_result(x, &b, &result);
        if (ok && result != UINT32_MAX - 1) ok = add_successor(out, result);
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
                    .base = order_base, .bcap = order_cap};
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
    free(b.member); free(b.base);
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

/* ---- Programs that propagate ----------------------------------------------
 * A program propagates when its rules only read and make: no takes, no
 * tells, every check an equation or a guard, every local of a made row or a
 * check one of a read pattern (RuleSyntax/SetStoreSyntax: Propagates).  A
 * guard, like an equation, only filters matches: it is decided on the
 * match's values alone.  Then, at a state of fixed terms, whether a rule
 * matches some rows depends on those rows alone, and the rows it makes are
 * fixed terms.  Under step every over a set store, from a state of fixed
 * terms, every state of the run holds the one before: the run is kept in one
 * store, and each step after the first meets only the matches that read a
 * row the step before added (tcalc_delta.c).  Not when a rule takes (a row it
 * took can be made again), tells, or asks an absence, a count or a census
 * (they read the whole state); nor in a multiset store, where a firing that
 * makes a row again adds a copy of it; nor under another discipline, whose
 * step need not fire every match; nor from a state with unknowns, which a
 * read may decide, changing rows: those runs are explored state by state. */

/* Whether the rule propagates (above). */
static bool rule_propagates(const Explorer *x, uint32_t rule) {
    TcStoredRule r = rule_at(&x->program, rule);
    if (r.takes || r.tells) return false;
    bool *read = calloc(r.locals ? r.locals : 1, sizeof *read);
    if (!read) return false;
    bool ok = true;
    for (uint32_t k = 0; ok && k < r.reads; k++)
        ok = locals_in(x->terms, pattern_at(&x->program, &r, k), read, r.locals, true);
    for (uint32_t k = 0; ok && k < r.makes; k++)
        ok = locals_in(x->terms, make_at(&x->program, &r, k), read, r.locals, false);
    for (uint32_t k = 0; ok && k < r.check_count; k++) {
        TcCheck c = check_at(&x->program, &r, k);
        ok = check_binary(c.kind) && locals_in(x->terms, c.left, read, r.locals, false) &&
             locals_in(x->terms, c.right, read, r.locals, false);
    }
    free(read);
    return ok;
}

/* Whether the run is kept in one store (above): the program propagates, the
 * interpretation is step every over a set store on the C backend, and the
 * initial rows are fixed terms. */
static bool delta_admits(const Explorer *x, const TcTerm *initial, size_t initials) {
    if (x->how.step != TC_STEP_EVERY || x->how.store != TC_STORE_SET || x->how.backend != TC_BACKEND_C)
        return false;
    for (uint32_t rule = 0; rule < x->program.rules; rule++)
        if (!rule_propagates(x, rule)) return false;
    if (!initials) return true;
    if (!initial) return false;
    for (size_t i = 0; i < initials; i++)
        if (!term_node(x->terms, initial[i]).ground) return false;
    return true;
}

/* The successors of state q, matched at its view.  False when memory ran
 * out. */
static bool expand(Explorer *x, uint32_t q, Successors *next) {
    return view_state(x, q) && successors(x, next);
}

/* Lists state q of the table (list_state). */
static bool list_table_state(TcOutcomes *o, const Explorer *x, uint32_t q, bool ambiguous, uint64_t matches) {
    TcState s = state_at(&x->states, q);
    return list_state(o, &x->trie, &x->places, x->terms, s.rows, x->states.loose + s.loose,
                      x->states.shape + s.loose, s.looses, ambiguous, matches);
}

/* The structural order of states a and b (rows_order), their rows listed in
 * x->listed.  -2 when memory ran out. */
static int state_order(Explorer *x, uint32_t a, uint32_t b) {
    TcState sa = state_at(&x->states, a), sb = state_at(&x->states, b);
    x->listed_len = 0;
    if (!store_rows(&x->trie, &x->places, x->terms, sa.rows, x->states.loose + sa.loose,
                    x->states.shape + sa.loose, sa.looses, &x->listed, &x->listed_len, &x->listed_cap))
        return -2;
    size_t na = x->listed_len;
    if (!store_rows(&x->trie, &x->places, x->terms, sb.rows, x->states.loose + sb.loose,
                    x->states.shape + sb.loose, sb.looses, &x->listed, &x->listed_len, &x->listed_cap) ||
        !x->listed)
        return -2;
    return rows_order(x->terms, x->listed, (uint32_t)na, (uint32_t)(x->listed_len - na));
}

/* The k-th of n states in the structural order of states: the least by a scan
 * when k is 0 (the earlier of two equal), otherwise by sorting them; a run's
 * successors are few.  False when memory ran out. */
static bool nth_least_state(Explorer *x, uint32_t *states, size_t n, size_t k, uint32_t *out) {
    if (k == 0) {
        uint32_t best = states[0];
        for (size_t i = 1; i < n; i++) {
            int c = state_order(x, states[i], best);
            if (c == -2) return false;
            if (c < 0) best = states[i];
        }
        *out = best;
        return true;
    }
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0; j--) {
            int c = state_order(x, states[j], states[j - 1]);
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
    if (!spend(x)) { run_visits_free(&visits); return true; }
    int again = run_visit(&visits, q, turn);
    if (again < 0) { run_visits_free(&visits); return false; }
    if (again) {
        o->cyclic = true;
        *finished = true;
        run_visits_free(&visits);
        return true;
    }
    if (!list_table_state(o, x, q, false, 0)) {
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
            ok = list_table_state(o, x, q, true, next.ambiguous);
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
        if (!(ok = list_table_state(o, x, q, false, 0))) break;
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

/* Releases the exploration's storage; every map of rows goes with the pool. */
static void explorer_free(Explorer *x) {
    free(x->member); free(x->selection); free(x->next); free(x->known); free(x->pick);
    free(x->plans); free(x->flat_words); free(x->key);
    free(x->batch_cursor); free(x->batch_output); free(x->batch_row); free(x->batch_occurrence);
    free(x->view.loose); free(x->view.shape); free(x->present.loose); free(x->present.shape);
    free(x->loose_next); free(x->shape_next); free(x->change); free(x->gone); free(x->listed);
    occurrences_free(&x->taken); occurrences_free(&x->read);
    delta_free(&x->delta_store);
    fired_free(&x->fired);
    solver_free(&x->solver);
    states_free(&x->states);
    places_free(&x->places);
    trie_free(&x->trie);
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
    if (!expand(x, q, next)) return RUN_FAILED;
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
    if (ok && next->ambiguous) ok = list_table_state(e->o, x, q, true, next->ambiguous);
    else if (ok && next->len == 0) ok = list_table_state(e->o, x, q, false, 0);
    return ok ? RUN_EXPANDED : RUN_FAILED;
}

/* The work an exploration counted (tcalc_core.h), into its outcomes. */
static void count_work(TcOutcomes *o, const Explorer *x) {
    o->unifications = x->solver.unifications;
    o->rows_met = x->met;
    o->matches = x->decided;
    o->rows_stored = x->trie.held + x->states.looses + x->delta_store.rows;
    o->patterns_tried = x->tried;
    o->identity_work = x->trie.compared + x->states.compared;
    o->indexes = x->places.met;
}

/* ---- Step every over a set store, in one store ------------------------------
 * The run of a program that propagates, by delta (above), is tcalc_delta.c. */
#include "tcalc_delta.c"

TcOutcomes *tc_explore(TcProgram *program, const TcInterpretation *how) {
    TcOutcomes *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    *o = (TcOutcomes){0};
    o->memory_ok = true;
    if (how->backend==TC_BACKEND_C && how->step==TC_STEP_ONE && how->shortcuts!=TC_SHORTCUTS_NONE) resident_prepare(program);
    bool planned = plans_prepare(program);   /* the rules' plans, chosen once (tcalc_plan.c) */
    Explorer x = {.terms = program->terms, .program = program_view(program),
                  .how = *how, .budget = {.limit = how->budget}};
    bool ok = planned && explorer_start(&x), finished = false;
    if (ok && delta_admits(&x, program->row, program->rows)) {   /* one store, by delta */
        ok = delta_run(&x, program->row, program->rows, o, &finished);
        o->finished = ok && finished;
        o->disagreed = x.disagreed;
        o->work = x.budget.spent;
        count_work(o, &x);
        bool memory_ok = ok && !x.solver.failed && !x.terms->failed && !x.trie.failed;
        explorer_free(&x);   /* the term table is the program's again */
        o->memory_ok = memory_ok;
        return o;
    }
    if (!ok) {
        o->finished = false;
        o->decider_failed = x.flat_failed;
        o->disagreed = x.disagreed;
        o->work = x.budget.spent;
        count_work(o, &x);
        bool memory_ok = x.flat_failed && !x.solver.failed && !x.terms->failed && !x.trie.failed;
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
        count_work(o, &x);
        bool memory_ok = x.flat_failed && !x.solver.failed && !x.terms->failed && !x.trie.failed;
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
        count_work(o, &x);
        bool memory_ok = (ok || x.flat_failed) && !x.solver.failed && !x.terms->failed && !x.trie.failed;
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
        count_work(e.o, e.x);
        bool flat_failed = e.x->flat_failed;
        bool solver_failed = e.x->solver.failed;
        bool terms_failed = e.x->terms->failed || e.x->trie.failed;
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
    bool planned = plans_prepare(program);
    Explorer x = {.terms = program->terms, .program = program_view(program),
                  .how = *how, .budget = {.limit = how->budget}};
    Successors next = {0};
    bool ok = planned && explorer_start(&x);
    bool expanded = false;
#define TC_SUCCESSORS_RETURN() { \
        o->finished = ok && expanded && !next.budget_out; \
        o->decider_failed = x.flat_failed; \
        o->disagreed = x.disagreed; \
        o->states = x.states.states; \
        o->work = x.budget.spent; \
        count_work(o, &x); \
        free(next.state); \
        bool memory_ok = (ok || x.flat_failed) && !x.solver.failed && !x.terms->failed && !x.trie.failed; \
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
        ok = list_table_state(o, &x, q, true, next.ambiguous);
        TC_SUCCESSORS_RETURN();
    }
    if (!next.len) TC_SUCCESSORS_RETURN();
    if (!next.state) { ok = false; TC_SUCCESSORS_RETURN(); }
    size_t first = 0;
    while (first < next.len && x.how.store == TC_STORE_SET && next.state[first] == q) first++;
    if (first == next.len) TC_SUCCESSORS_RETURN();
    size_t i = first + 1;
    ok = list_table_state(o, &x, next.state[first], false, 0);
    if (!ok) TC_SUCCESSORS_RETURN();
    for (; i < next.len; i++)
        if (!(x.how.store == TC_STORE_SET && next.state[i] == q))
            if (!list_table_state(o, &x, next.state[i], false, 0)) {
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
    if (!canonical_form(terms, key, &m, store, &ground, NULL) || terms->failed) { free(key); return NULL; }
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

