/* rcm_cetta.c - a rule module compiled by rulec, as a CeTTa native module: its judgments called on
 * atoms.  Generic over the module; one instance per compiled module (a translation unit that includes
 * the runtime, the components, the generated code and this file, with the instance's names; see
 * native/megalodon_module/mgm_instance.c; the generated code and its ids header come first), with
 *     RCM_DISPATCH   the name of the dispatch function (the instance's only global symbol)
 *     RCM_OPS        the operations' prefix, as a string ("__cetta_lib_mgm_")
 *     RCM_TAG        the prefix of the result atoms, as a string ("mgm")
 * Operations:
 *     OPS call TERM               TERM = (JUDGMENT ARG ...); each argument is module data in the
 *         module's constructors: (CTOR KID ...), (CTOR) or CTOR for a nullary constructor, checked
 *         against the judgment's argument sorts.  Result (TAG:value V) with V in the same form
 *         (nullary constructors as (CTOR)), (TAG:none) when no rule applies, or (TAG:error "message")
 *         when the call cannot be made (an unknown constructor, a sort mismatch, a representation
 *         that cannot hold a value).
 *     OPS hold TERM               TERM is module data of a sort whose values outlive a call (interned,
 *         immediate or nullary-only); it is built once in the module's store and kept there:
 *         (TAG:handle N).  Wherever module data is expected in a call or a hold (arguments and inside
 *         them) (TAG:handle N) stands for the kept value; relation rows hold data only.  N is never
 *         reused.
 *     OPS call-hold TERM          as call, but the value (of such a sort) is kept and returned as a new
 *         handle: (TAG:value (TAG:handle M)).
 *     OPS view HANDLE             (TAG:value V): the kept value as atoms.
 *     OPS release HANDLE          the value is no longer kept; the handle is refused from now on.
 *     OPS reset                   every handle is refused from now on, and the store is freed.
 *     A handle is refused ("stale handle N") once released, or once the store that keeps its value
 *     has been freed (a reset, or a call stopped by a refinement violation); a value is never
 *     replaced under a handle, so a changed or extended value always has a new handle.
 *     OPS stats                   (TAG:stats CALLS READ-NS BUILD-NS MODULE-NS VIEW-NS NODES-READ
 *         NODES-VIEWED HOLDS HELD) since the process started: the time spent reading atoms into module
 *         values (calls and holds), beginning runs (the store, the relation rows), in the module, and
 *         writing results as atoms; NODES-READ the atoms read (an atom the arguments share counts
 *         once), HOLDS the holds made, HELD the values kept now.
 *     OPS set-relation NAME ROWS  ROWS = ((A ...) ...): the rows of the module's relation NAME, as
 *         module data, for the calls that follow (a relation premise with every argument bound holds
 *         when its arguments equal a row); () empties it.  Result (TAG:rows N).
 * An instance whose module reads its input through another compiled module (rc_feed.h) defines
 *     RCM_FEED       the feed's description (an RcFeed)
 * and has two more operations:
 *     OPS feed PATH TERM          the file PATH read by the feed's reader while the call TERM runs:
 *         TERM is a call as above, one of whose arguments is (TAG:stream), the value standing for the
 *         stream (of the feed's stream sort).  The reader stops when the module no longer reads, so
 *         a stream the module refuses early is not read further.  Result as for call.
 *     OPS feed-measure PATH TERM  the same run, measured: (TAG:measure RECORDS READ-NS MODULE-NS
 *         YOUNG-BYTES PROMOTED-BYTES OLD-PEAK-BYTES), RECORDS the records read, READ-NS the time in the
 *         reader (and in taking its records), MODULE-NS the rest of the run.
 * A module with a data constructor of more than 16 children is refused by every operation (the glue's
 * child arrays hold 16).  Calls are serialised.  A call's atoms are read on the caller's thread (where
 * they were just made) straight into module values in the module's representation (a subterm equal
 * to one read before in the call is read once); the call then runs on the instance's worker thread,
 * made at the first call with a large stack (RCM_STACK_BYTES, 256 MiB unless the instance sets it: the
 * rules recurse on the depth of terms).
 * With RCM_CAPTURE=DIR in the environment every call is also written to DIR/<module>.<pid>.cap, for
 * replay outside CeTTa (a differential between builds of the module, profiles, check mode, traces):
 * lines
 *     n ID CTOR KID-ID ...           a node of module data (each distinct node once)
 *     c JUDGMENT ARG-ID ... = R      a call and its result: a node id, none, or error
 *     r RELATION ARITY ID ...        the rows a relation has from now on (ARITY ids per row)
 * (a kept value is written as its nodes, once).  Interned sorts keep their store from call to call
 * (collected at the end of a call when it has grown, the kept values among the roots); everything else
 * of a call is freed when it returns.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "atom.h"
#include "space.h"
#include "../rule_runtime/rc_rt.h"
#include "../rule_components/rc_intern.h"
#include "../rule_components/rc_table.h"
#ifdef RCM_FEED
#include "rc_feed.h"
#endif

#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaLibraryContext;
Atom *RCM_DISPATCH(struct CettaLibraryContext *ctx, Space *space, Arena *a, Atom *head, Atom **args, uint32_t nargs);

#ifndef RCM_STACK_BYTES
#define RCM_STACK_BYTES ((size_t)256 << 20)
#endif

static pthread_mutex_t rcm_gate = PTHREAD_MUTEX_INITIALIZER;

/* the boundary's counters (OPS stats) */
static uint64_t rcm_ncalls, rcm_nholds, rcm_read_ns, rcm_build_ns, rcm_module_ns, rcm_view_ns, rcm_nodes_read, rcm_nodes_viewed;
static uint64_t rcm_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
static bool rcm_ready, rcm_interned, rcm_tables;
static const char *rcm_too_wide;   /* a data constructor of more than 16 children: the glue refuses */

/* kept values: an entry per handle, never reused */
typedef struct { T *value; int sort; uint64_t epoch; bool live; int cap_id; } RcmHeld;
static RcmHeld *rcm_held; static size_t rcm_nheld, rcm_heldcap;
static bool *rcm_outlives;   /* per sort: its values outlive a call (identity sorts) */
static bool rcm_held_valid(size_t i) {
    return i < rcm_nheld && rcm_held[i].live && rcm_held[i].epoch == rc_intern_epoch();
}
static size_t rcm_held_roots(void (*mark)(T *v)) {
    size_t n = 0;
    for (size_t i = 0; i < rcm_nheld; i++)
        if (rcm_held_valid(i)) { if (mark) mark(rcm_held[i].value); n++; }
    return n;
}
static size_t rcm_hold(T *v, int sort) {
    if (rcm_nheld == rcm_heldcap) { rcm_heldcap = rcm_heldcap ? 2 * rcm_heldcap : 64; rcm_held = realloc(rcm_held, rcm_heldcap * sizeof *rcm_held); }
    rcm_held[rcm_nheld] = (RcmHeld){v, sort, rc_intern_epoch(), true, 0};
    return rcm_nheld++;
}

/* constructor names: an open-addressing table built once */
/* constructors by the symbol that names them (CeTTa's interned SymbolId): an open-addressing table
 * from symbol to constructor id; each constructor's canonical symbol atom for writing values */
static SymbolId *rcm_ctor_sym;
static Atom **rcm_ctor_atom;
static SymbolId *rcm_sym_keys; static int *rcm_sym_ctor; static size_t rcm_sym_cap;
static SymbolId rcm_handle_sym, rcm_stream_sym;
static size_t rcm_sym_slot(SymbolId id) { return ((size_t)id * 0x9e3779b97f4a7c15ull >> 20) & (rcm_sym_cap - 1); }
static void rcm_init(void) {
    rcm_sym_cap = 1;
    while (rcm_sym_cap < 4u * (size_t)rc_nctors) rcm_sym_cap <<= 1;
    rcm_sym_keys = malloc(rcm_sym_cap * sizeof *rcm_sym_keys);
    rcm_sym_ctor = malloc(rcm_sym_cap * sizeof *rcm_sym_ctor);
    rcm_ctor_sym = malloc((size_t)rc_nctors * sizeof *rcm_ctor_sym);
    rcm_ctor_atom = calloc((size_t)rc_nctors, sizeof *rcm_ctor_atom);
    for (size_t i = 0; i < rcm_sym_cap; i++) rcm_sym_ctor[i] = -1;
    for (int c = 0; c < rc_nctors; c++) {
        SymbolId id = atom_symbol_literal_id(rc_ctor_name[c]);
        rcm_ctor_sym[c] = id;
        size_t h = rcm_sym_slot(id);
        while (rcm_sym_ctor[h] >= 0) h = (h + 1) & (rcm_sym_cap - 1);
        rcm_sym_keys[h] = id;
        rcm_sym_ctor[h] = c;
    }
    /* the glue reads and writes constructors of at most 16 children (its child arrays) */
    for (int c = 0; c < rc_nctors && !rcm_too_wide; c++)
        if (!rc_ctor_judgment[c] && rc_ctor_arity[c] > 16) rcm_too_wide = rc_ctor_name[c];
    rcm_handle_sym = atom_symbol_literal_id(RCM_TAG ":handle");
    rcm_stream_sym = atom_symbol_literal_id(RCM_TAG ":stream");
    rcm_outlives = calloc((size_t)rc_nsorts + 1, sizeof(bool));
    for (int s = 0; s < rc_nsorts; s++) {
        int r = rc_sort_rep[s];
        /* an interning store: interned sorts and byte strings, binary naturals' large values, byte
         * strings not cut from an input */
        if (r == RC_REP_INTERN || r == RC_REP_BYTES || r == RC_REP_BINARY || r == RC_REP_SLICES) rcm_interned = true;
        if (r == RC_REP_TABLE) rcm_tables = true;
        bool nullary_only = r == RC_REP_SYMBOLIC;
        for (int c = 0; c < rc_nctors && nullary_only; c++)
            if (!rc_ctor_judgment[c] && rc_ctor_sort[c] == s && rc_ctor_arity[c]) nullary_only = false;
        /* interned values and byte strings, immediate naturals, words, binary naturals (immediates or
         * interned magnitudes); never slices (cut from one call's input) */
        rcm_outlives[s] = r == RC_REP_INTERN || r == RC_REP_BYTES || r == RC_REP_NAT || r == RC_REP_UNARY ||
                          r == RC_REP_WORD || r == RC_REP_BINARY || nullary_only;
    }
    rc_runtime_init(0);
    rc_components_init();
    rc_intern_held_hook = rcm_held_roots;
}
static int rcm_ctor_of(SymbolId id) {
    for (size_t h = rcm_sym_slot(id); rcm_sym_ctor[h] >= 0; h = (h + 1) & (rcm_sym_cap - 1))
        if (rcm_sym_keys[h] == id) return rcm_sym_ctor[h];
    return -1;
}
static Atom *rcm_ctor_symbol(int c) {
    if (!rcm_ctor_atom[c]) rcm_ctor_atom[c] = atom_symbol_canonical(rcm_ctor_sym[c]);
    return rcm_ctor_atom[c];
}

/* relation rows read from atoms, kept outside the module's heap (rows outlive a call); held: a
 * handle's index + 1, else 0 */
typedef struct RcmTree { int ctor; int n; size_t held; struct RcmTree *k[]; } RcmTree;
static void rcm_tree_free(RcmTree *t) {
    if (!t) return;
    for (int i = 0; i < t->n; i++) rcm_tree_free(t->k[i]);
    free(t);
}
/* (TAG:handle N): its index, else -1 */
static long rcm_handle_index(Atom *x) {
    if (!x || x->kind != ATOM_EXPR || x->expr.len != 2 || x->expr.elems[0]->kind != ATOM_SYMBOL ||
        x->expr.elems[0]->sym_id != rcm_handle_sym) return -1;
    Atom *n = x->expr.elems[1];
    if (n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT || n->ground.ival < 0) return -2;
    return (long)n->ground.ival;
}
static Atom *rcm_handle_atom(Arena *a, size_t i) {
    Atom *e[2] = {atom_symbol(a, RCM_TAG ":handle"), atom_int(a, (int64_t)i)};
    return atom_expr(a, e, 2);
}
/* an atom as data of sort `sort` (or of any sort when sort < 0, for the judgment itself) */
static RcmTree *rcm_tree(Atom *x, int sort, char *err, size_t errcap) {
    const char *name = NULL;
    Atom **kids = NULL;
    uint32_t n = 0;
    long h = rcm_handle_index(x);
    if (h == -2) { snprintf(err, errcap, "a handle names a natural number"); return NULL; }
    if (h >= 0) {
        if (!rcm_held_valid((size_t)h)) { snprintf(err, errcap, "stale handle %ld", h); return NULL; }
        if (sort >= 0 && rcm_held[h].sort != sort) {
            snprintf(err, errcap, "handle %ld holds a value of sort %s, not %s", h, rc_sort_name[rcm_held[h].sort], rc_sort_name[sort]);
            return NULL;
        }
        RcmTree *t = calloc(1, sizeof *t);
        t->ctor = -1; t->held = (size_t)h + 1;
        return t;
    }
    Atom *head = NULL;
    if (x && x->kind == ATOM_SYMBOL) head = x;
    else if (x && x->kind == ATOM_EXPR && x->expr.len >= 1 && x->expr.elems[0]->kind == ATOM_SYMBOL) {
        head = x->expr.elems[0];
        kids = x->expr.elems + 1;
        n = (uint32_t)(x->expr.len - 1);
    }
    if (!head) { snprintf(err, errcap, "not module data (a constructor application expected)"); return NULL; }
    int c = rcm_ctor_of(head->sym_id);
    name = c >= 0 ? rc_ctor_name[c] : atom_name_cstr(head);
    if (c < 0) { snprintf(err, errcap, "unknown constructor %s", name); return NULL; }
    if (sort >= 0 && (rc_ctor_judgment[c] || rc_ctor_sort[c] != sort)) {
        snprintf(err, errcap, "%s is not a constructor of sort %s", name, rc_sort_name[sort]);
        return NULL;
    }
    if (n != rc_ctor_arity[c]) { snprintf(err, errcap, "%s takes %u arguments, given %u", name, (unsigned)rc_ctor_arity[c], (unsigned)n); return NULL; }
    RcmTree *t = calloc(1, sizeof *t + (size_t)n * sizeof(RcmTree *));
    rcm_nodes_read++;
    t->ctor = c; t->n = (int)n;
    for (uint32_t i = 0; i < n; i++) {
        t->k[i] = rcm_tree(kids[i], rc_ctor_asort[rc_ctor_asort_off[c] + i], err, errcap);
        if (!t->k[i]) { rcm_tree_free(t); return NULL; }
    }
    return t;
}
/* the tree in the module's representation (inside a call) */
static T *rcm_build(const RcmTree *t) {
    T *k[16];
    if (t->held) return rcm_held[t->held - 1].value;
    if (t->n > 16) rc_refinement_fail("constructor arity above 16");
    for (int i = 0; i < t->n; i++) k[i] = rcm_build(t->k[i]);
    T *v = rc_construct(t->ctor, k);
    if (!v) rc_refinement_fail("a constructor the representation cannot build");
    return v;
}
/* A call's arguments are read from their atoms straight into module values, on the module's thread
 * inside the run (relation rows, which outlive calls, are trees).  A subterm the atoms share (one
 * atom reached twice) is read once.  An atom that is not module data of the expected sort stops the
 * reading (the store is kept) and makes the call's error. */
typedef struct RcmRun RcmRun;
static jmp_buf rcm_input_jump;
static char *rcm_input_msg;
static _Noreturn __attribute__((format(printf, 1, 2))) void rcm_input_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rcm_input_msg, 256, fmt, ap);
    va_end(ap);
    longjmp(rcm_input_jump, 1);
}
/* The subterms read in the current call, by structure: the arguments' atoms are hashed once (CeTTa
 * keeps an atom's structural hash; hashing a term hashes and keeps its subterms'), and a subterm equal
 * to one read before in the call (an equal atom, of the same sort) is that value, its atoms not read
 * again.  An open-addressing table whose entries are valid in the generation they were made in, so a
 * call starts with an empty table at no cost. */
typedef struct { Atom *x; uint32_t h; int sort; uint32_t gen; T *v; } RcmSeen;
static RcmSeen *rcm_seen; static size_t rcm_seen_cap, rcm_seen_n;
static uint32_t rcm_seen_gen = 1;
static size_t rcm_seen_slot(uint32_t h, int sort) {
    return (size_t)(((uint64_t)h ^ ((uint64_t)(uint32_t)sort << 32)) * 0x9e3779b97f4a7c15ull >> 17) & (rcm_seen_cap - 1);
}
static void rcm_seen_reset(void) {
    rcm_seen_n = 0;
    if (++rcm_seen_gen == 0) { if (rcm_seen) memset(rcm_seen, 0, rcm_seen_cap * sizeof *rcm_seen); rcm_seen_gen = 1; }
}
static void rcm_seen_put(Atom *x, uint32_t h, int sort, T *v) {
    if (2 * (rcm_seen_n + 1) > rcm_seen_cap) {
        size_t ncap = rcm_seen_cap ? 2 * rcm_seen_cap : 1024;
        RcmSeen *old = rcm_seen; size_t ocap = rcm_seen_cap;
        rcm_seen = calloc(ncap, sizeof *rcm_seen);
        if (!rcm_seen) rc_refinement_fail("out of memory reading atoms");
        rcm_seen_cap = ncap;
        for (size_t i = 0; i < ocap; i++) {
            if (old[i].gen != rcm_seen_gen) continue;
            size_t j = rcm_seen_slot(old[i].h, old[i].sort);
            while (rcm_seen[j].gen == rcm_seen_gen) j = (j + 1) & (ncap - 1);
            rcm_seen[j] = old[i];
        }
        free(old);
    }
    size_t j = rcm_seen_slot(h, sort);
    while (rcm_seen[j].gen == rcm_seen_gen) j = (j + 1) & (rcm_seen_cap - 1);
    rcm_seen[j] = (RcmSeen){x, h, sort, rcm_seen_gen, v};
    rcm_seen_n++;
}
/* two atoms read alike as module data: the same symbols and numbers in the same shape (two subterms
 * whose kept hashes differ differ); anything else falls back to CeTTa's equality */
static bool rcm_same(Atom *a, Atom *b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case ATOM_SYMBOL: return a->sym_id == b->sym_id;
    case ATOM_EXPR: {
        if (a->expr.len != b->expr.len) return false;
        uint32_t ha = atom_hash_cache_load(a), hb = atom_hash_cache_load(b);
        if (ha && hb && ha != hb) return false;
        for (CettaExprLen i = 0; i < a->expr.len; i++)
            if (!rcm_same(a->expr.elems[i], b->expr.elems[i])) return false;
        return true;
    }
    case ATOM_GROUNDED:
        if (a->ground.gkind == GV_INT && b->ground.gkind == GV_INT) return a->ground.ival == b->ground.ival;
        return atom_eq(a, b);
    default:
        return atom_eq(a, b);
    }
}
static T *rcm_seen_get(Atom *x, uint32_t h, int sort) {
    if (!rcm_seen_n) return NULL;
    for (size_t j = rcm_seen_slot(h, sort); rcm_seen[j].gen == rcm_seen_gen; j = (j + 1) & (rcm_seen_cap - 1))
        if (rcm_seen[j].h == h && rcm_seen[j].sort == sort && rcm_same(rcm_seen[j].x, x)) return rcm_seen[j].v;
    return NULL;
}
/* an atom as a value of sort `sort` */
static T *rcm_value(Atom *x, int sort) {
    long h = rcm_handle_index(x);
    if (h == -2) rcm_input_error("a handle names a natural number");
    if (h >= 0) {
        if (!rcm_held_valid((size_t)h)) rcm_input_error("stale handle %ld", h);
        if (rcm_held[h].sort != sort)
            rcm_input_error("handle %ld holds a value of sort %s, not %s", h, rc_sort_name[rcm_held[h].sort], rc_sort_name[sort]);
        return rcm_held[h].value;
    }
    Atom *head = NULL, **kids = NULL;
    uint32_t n = 0;
    if (x && x->kind == ATOM_SYMBOL) head = x;
    else if (x && x->kind == ATOM_EXPR && x->expr.len >= 1 && x->expr.elems[0]->kind == ATOM_SYMBOL) {
        head = x->expr.elems[0];
        kids = x->expr.elems + 1;
        n = (uint32_t)(x->expr.len - 1);
    }
    if (!head) rcm_input_error("not module data (a constructor application expected)");
    int c = rcm_ctor_of(head->sym_id);
    if (c < 0) rcm_input_error("unknown constructor %s", atom_name_cstr(head));
    if (rc_ctor_judgment[c] || rc_ctor_sort[c] != sort) rcm_input_error("%s is not a constructor of sort %s", rc_ctor_name[c], rc_sort_name[sort]);
    if (n != rc_ctor_arity[c]) rcm_input_error("%s takes %u arguments, given %u", rc_ctor_name[c], (unsigned)rc_ctor_arity[c], (unsigned)n);
    if (!n) return rc_construct(c, NULL);
    /* the structural hash CeTTa keeps (0: none kept, the atom is not reused) */
    uint32_t hx = atom_hash_cache_load(x);
    T *v = hx ? rcm_seen_get(x, hx, sort) : NULL;
    if (v) return v;
    if (n > 16) rc_refinement_fail("constructor arity above 16");
    T *k[16];
    for (uint32_t i = 0; i < n; i++) k[i] = rcm_value(kids[i], rc_ctor_asort[rc_ctor_asort_off[c] + i]);
    v = rc_construct(c, k);
    if (!v) rc_refinement_fail("a constructor the representation cannot build");
    rcm_nodes_read++;
    if (hx) rcm_seen_put(x, hx, sort, v);
    return v;
}

/* The values written in the current result: an interned value is its own identity (equal values are
 * one node), so a value written before in the result is the same atom (the atoms share it). */
typedef struct { T *v; uint32_t gen; Atom *x; } RcmShown;
static RcmShown *rcm_shown; static size_t rcm_shown_cap, rcm_shown_n;
static uint32_t rcm_shown_gen = 1;
static size_t rcm_shown_slot(T *v) { return (size_t)(((uintptr_t)v >> 3) * 0x9e3779b97f4a7c15ull >> 17) & (rcm_shown_cap - 1); }
static void rcm_shown_reset(void) {
    rcm_shown_n = 0;
    if (++rcm_shown_gen == 0) { if (rcm_shown) memset(rcm_shown, 0, rcm_shown_cap * sizeof *rcm_shown); rcm_shown_gen = 1; }
}
static Atom *rcm_shown_get(T *v) {
    if (!rcm_shown_n) return NULL;
    for (size_t j = rcm_shown_slot(v); rcm_shown[j].gen == rcm_shown_gen; j = (j + 1) & (rcm_shown_cap - 1))
        if (rcm_shown[j].v == v) return rcm_shown[j].x;
    return NULL;
}
static void rcm_shown_put(T *v, Atom *x) {
    if (2 * (rcm_shown_n + 1) > rcm_shown_cap) {
        size_t ncap = rcm_shown_cap ? 2 * rcm_shown_cap : 1024;
        RcmShown *old = rcm_shown; size_t ocap = rcm_shown_cap;
        rcm_shown = calloc(ncap, sizeof *rcm_shown);
        if (!rcm_shown) { rcm_shown = old; return; }   /* no room: the atom is not shared */
        rcm_shown_cap = ncap;
        for (size_t i = 0; i < ocap; i++) {
            if (old[i].gen != rcm_shown_gen) continue;
            size_t j = rcm_shown_slot(old[i].v);
            while (rcm_shown[j].gen == rcm_shown_gen) j = (j + 1) & (ncap - 1);
            rcm_shown[j] = old[i];
        }
        free(old);
    }
    size_t j = rcm_shown_slot(v);
    while (rcm_shown[j].gen == rcm_shown_gen) j = (j + 1) & (rcm_shown_cap - 1);
    rcm_shown[j] = (RcmShown){v, rcm_shown_gen, x};
    rcm_shown_n++;
}
/* a value as an atom: (CTOR KID ...), a nullary constructor as (CTOR) */
static Atom *rcm_atom_of(Arena *a, T *t, int sort) {
    bool interned = rc_sort_rep[sort] == RC_REP_INTERN && RC_IS_FOREIGN(t);
    if (interned) { Atom *x = rcm_shown_get(t); if (x) return x; }
    T *kids[16];
    int c = rc_view(sort, t, kids);
    if (c < 0) return atom_symbol(a, RCM_TAG ":unreadable");
    uint32_t n = rc_ctor_arity[c];
    rcm_nodes_viewed++;
    Atom *e[17];
    e[0] = rcm_ctor_symbol(c);
    for (uint32_t i = 0; i < n && i < 16; i++) e[i + 1] = rcm_atom_of(a, kids[i], rc_ctor_asort[rc_ctor_asort_off[c] + i]);
    Atom *x = atom_expr(a, e, (CettaExprLen)(n + 1));
    if (interned && n) rcm_shown_put(t, x);
    return x;
}
static Atom *rcm_atom(Arena *a, T *t, int sort) {
    rcm_shown_reset();
    Atom *x = rcm_atom_of(a, t, sort);
    rcm_shown_reset();
    return x;
}

/* the capture: nodes shared through a table of (constructor, children's ids) */
static FILE *rcm_cap;
static bool rcm_cap_tried;
typedef struct { uint64_t h; int id, ctor, n, off; } RcmCapSlot;
static RcmCapSlot *rcm_cap_tab; static size_t rcm_cap_size, rcm_cap_used;
static int *rcm_cap_kids; static size_t rcm_cap_nkids, rcm_cap_kidcap;
static int rcm_cap_next = 1;
static bool rcm_capturing(void) {
    if (!rcm_cap_tried) {
        rcm_cap_tried = true;
        const char *dir = getenv("RCM_CAPTURE");
        if (dir && *dir) {
            char path[4096];
            snprintf(path, sizeof path, "%s/%s.%ld.cap", dir, RCM_TAG, (long)getpid());
            rcm_cap = fopen(path, "w");
        }
    }
    return rcm_cap != NULL;
}
static int rcm_cap_node(int ctor, int n, const int *k) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t)ctor;
    for (int i = 0; i < n; i++) { h ^= (uint64_t)k[i]; h *= 1099511628211ull; h ^= h >> 29; }
    if (2 * (rcm_cap_used + 1) > rcm_cap_size) {
        size_t nsize = rcm_cap_size ? 2 * rcm_cap_size : 1u << 16;
        RcmCapSlot *nt = calloc(nsize, sizeof *nt);
        for (size_t i = 0; i < rcm_cap_size; i++) {
            if (!rcm_cap_tab[i].id) continue;
            size_t j = rcm_cap_tab[i].h & (nsize - 1);
            while (nt[j].id) j = (j + 1) & (nsize - 1);
            nt[j] = rcm_cap_tab[i];
        }
        free(rcm_cap_tab);
        rcm_cap_tab = nt; rcm_cap_size = nsize;
    }
    size_t j = h & (rcm_cap_size - 1);
    for (; rcm_cap_tab[j].id; j = (j + 1) & (rcm_cap_size - 1)) {
        RcmCapSlot *e = &rcm_cap_tab[j];
        if (e->h == h && e->ctor == ctor && e->n == n && !memcmp(rcm_cap_kids + e->off, k, (size_t)n * sizeof(int))) return e->id;
    }
    if (rcm_cap_nkids + (size_t)n > rcm_cap_kidcap) { rcm_cap_kidcap = 2 * rcm_cap_kidcap + (size_t)n + 1024; rcm_cap_kids = realloc(rcm_cap_kids, rcm_cap_kidcap * sizeof(int)); }
    memcpy(rcm_cap_kids + rcm_cap_nkids, k, (size_t)n * sizeof(int));
    rcm_cap_tab[j] = (RcmCapSlot){h, rcm_cap_next, ctor, n, (int)rcm_cap_nkids};
    rcm_cap_nkids += (size_t)n;
    rcm_cap_used++;
    fprintf(rcm_cap, "n %d %s", rcm_cap_next, rc_ctor_name[ctor]);
    for (int i = 0; i < n; i++) fprintf(rcm_cap, " %d", k[i]);
    fputc('\n', rcm_cap);
    return rcm_cap_next++;
}
static int rcm_cap_value(T *t, int sort) {
    T *kids[16];
    int c = rc_view(sort, t, kids), k[16];
    if (c < 0) return 0;
    for (int i = 0; i < rc_ctor_arity[c]; i++) k[i] = rcm_cap_value(kids[i], rc_ctor_asort[rc_ctor_asort_off[c] + i]);
    return rcm_cap_node(c, rc_ctor_arity[c], k);
}
static int rcm_cap_tree(const RcmTree *t) {
    if (t->held) {
        RcmHeld *h = &rcm_held[t->held - 1];
        if (!h->cap_id) h->cap_id = rcm_cap_value(h->value, h->sort);
        return h->cap_id;
    }
    int k[16];
    for (int i = 0; i < t->n; i++) k[i] = rcm_cap_tree(t->k[i]);
    return rcm_cap_node(t->ctor, t->n, k);
}

/* relation rows */
typedef struct { int nrows, cap, arity; RcmTree **rows; T **built; } RcmRelation;
static RcmRelation rcm_rel[64];
static bool rcm_relation(int rel, int n, T *const *args) {
    if (rel < 0 || rel >= rc_nrelations) return false;
    const RcmRelation *r = &rcm_rel[rel];
    if (r->arity != n) return false;
    for (int i = 0; i < r->nrows; i++) {
        bool all = true;
        for (int j = 0; j < n && all; j++) all = rc_term_equal(r->built[i * n + j], args[j]);
        if (all) return true;
    }
    return false;
}

/* a run: call a judgment on the arguments read from argv (judgment >= 0), or read the value of
 * argv[0] of sort hold_sort (judgment < 0); a feed's run reads bytes through the feed, its stream the
 * argument at position stream */
struct RcmRun {
    int judgment;
    Atom *const *argv; int nargs; int hold_sort; int stream;
    T *args[16];
    T *result;
    char violation[256];   /* a refinement violation stopped the run */
    char input[256];       /* the atoms are not module data of the expected sorts */
    bool feed, feed_begun; const uint8_t *bytes; size_t len; uint64_t t0, total_ns;
};
static jmp_buf rcm_jump;
static RcmRun *rcm_current;
static _Noreturn void rcm_violation(const char *what) {
    snprintf(rcm_current->violation, sizeof rcm_current->violation, "refinement violation: %s", what);
    longjmp(rcm_jump, 1);
}
#ifdef RCM_FEED
static bool rcm_feed_settle;   /* the call's values may hold the stream's end: read to it in the run */
#endif
/* a run stopped by a refinement violation: the store is not reused */
static void rcm_stopped(RcmRun *r) {
    rc_fail_hook = NULL;
    r->result = NULL;
#ifdef RCM_FEED
    if (r->feed_begun) rc_feed_end(true);
    r->feed_begun = false;
#endif
    if (rcm_interned) rc_intern_free();
}
/* The run's beginning, on the caller's thread (where its atoms were just made): the store, the
 * relation rows, the arguments read from their atoms (a hold: its value), and a feed's stream read to
 * its first stop.  False when nothing is left to run on the module's thread. */
static bool rcm_prepare(RcmRun *r) {
    rcm_current = r;
    rc_fail_hook = rcm_violation;
    if (setjmp(rcm_jump)) { rcm_stopped(r); return false; }
    uint64_t tr = rcm_now();
    rc_runtime_reset();
    if (rcm_interned) { rc_intern_begin(); rc_foreign_hook = rc_intern_safepoint; }
    if (rcm_tables) { rc_table_reset(); rc_extra_roots_hook = rc_table_roots; }
    for (int i = 0; i < rc_nrelations; i++) {
        RcmRelation *rel = &rcm_rel[i];
        free(rel->built);
        rel->built = calloc((size_t)(rel->nrows * rel->arity) + 1, sizeof(T *));
        for (int j = 0; j < rel->nrows * rel->arity; j++) rel->built[j] = rcm_build(rel->rows[j]);
    }
    rc_relation_hook = rcm_relation;
    uint64_t t0 = rcm_now();
    rcm_build_ns += t0 - tr;
    r->t0 = t0;
    /* the atoms read into module values */
    rcm_input_msg = r->input;
    if (setjmp(rcm_input_jump)) {
        rcm_seen_reset();
        rcm_read_ns += rcm_now() - t0;
        rc_fail_hook = NULL;
        r->result = NULL;
        return false;
    }
    rcm_seen_reset();
    if (r->judgment < 0) {
        (void)atom_hash(r->argv[0]);
        r->result = rcm_value(r->argv[0], r->hold_sort);
        rcm_seen_reset();
        rcm_read_ns += rcm_now() - t0;
        rc_fail_hook = NULL;
        return false;
    }
    for (int i = 0; i < r->nargs; i++)
        if (i != r->stream) { (void)atom_hash(r->argv[i]); r->args[i] = rcm_value(r->argv[i], rc_ctor_asort[rc_ctor_asort_off[r->judgment] + i]); }
    rcm_seen_reset();
#ifdef RCM_FEED
    if (r->feed) {
        const char *why = NULL;
        T *stream = rc_feed_begin(&RCM_FEED, r->bytes, r->len, &why);
        if (!stream) {
            snprintf(r->input, sizeof r->input, "%s", why ? why : "the stream cannot be read");
            rcm_read_ns += rcm_now() - t0;
            rc_fail_hook = NULL;
            return false;
        }
        r->feed_begun = true;
        r->args[r->stream] = stream;
    }
#endif
    rcm_read_ns += rcm_now() - t0;
    rc_fail_hook = NULL;
    return true;
}
/* the call, on the module's thread */
static void rcm_run(RcmRun *r) {
    rcm_current = r;
    rc_fail_hook = rcm_violation;
    if (setjmp(rcm_jump)) { rcm_stopped(r); return; }
    uint64_t t1 = rcm_now();
    r->result = rc_call(r->judgment, r->args);
#ifdef RCM_FEED
    /* the stream's end read now, on the module's thread, when the call's value may hold it */
    if (r->feed && r->result && rcm_feed_settle) rc_feed_finish();
#endif
    uint64_t t2 = rcm_now();
    rcm_module_ns += t2 - t1;
    r->total_ns = t2 - r->t0;
    rc_fail_hook = NULL;
}
/* the worker: one run at a time, posted under rcm_gate (held by the caller throughout) */
static pthread_mutex_t rcm_post = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rcm_posted = PTHREAD_COND_INITIALIZER, rcm_finished = PTHREAD_COND_INITIALIZER;
static RcmRun *rcm_pending;
static bool rcm_done, rcm_worker_up;
static void *rcm_worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&rcm_post);
    for (;;) {
        while (!rcm_pending) pthread_cond_wait(&rcm_posted, &rcm_post);
        RcmRun *r = rcm_pending;
        pthread_mutex_unlock(&rcm_post);
        rcm_run(r);
        pthread_mutex_lock(&rcm_post);
        rcm_pending = NULL;
        rcm_done = true;
        pthread_cond_signal(&rcm_finished);
    }
    return NULL;
}
static bool rcm_execute(RcmRun *r) {
    if (!rcm_prepare(r)) return true;
    if (!rcm_worker_up) {
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, RCM_STACK_BYTES);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_t th;
        bool ok = pthread_create(&th, &at, rcm_worker, NULL) == 0;
        pthread_attr_destroy(&at);
        if (!ok) return false;
        rcm_worker_up = true;
    }
    pthread_mutex_lock(&rcm_post);
    rcm_done = false;
    rcm_pending = r;
    pthread_cond_signal(&rcm_posted);
    while (!rcm_done) pthread_cond_wait(&rcm_finished, &rcm_post);
    pthread_mutex_unlock(&rcm_post);
    return true;
}

static Atom *rcm_error(Arena *a, const char *message) {
    Atom *e[2] = {atom_symbol(a, RCM_TAG ":error"), atom_string(a, message)};
    return atom_expr(a, e, 2);
}

/* a call's head: (J ARG ...) or J, J a judgment of the module with as many arguments; -1 with
 * err set when it is not */
static int rcm_call_head(Atom *term, Atom *const **kids, uint32_t *n, char *err, size_t errcap) {
    *kids = NULL; *n = 0;
    Atom *head = NULL;
    if (rcm_handle_index(term) != -1) { snprintf(err, errcap, "the head is a data constructor, not a judgment"); return -1; }
    if (term && term->kind == ATOM_SYMBOL) head = term;
    else if (term && term->kind == ATOM_EXPR && term->expr.len >= 1 && term->expr.elems[0]->kind == ATOM_SYMBOL) {
        head = term->expr.elems[0];
        *kids = term->expr.elems + 1;
        *n = (uint32_t)(term->expr.len - 1);
    }
    if (!head) { snprintf(err, errcap, "not module data (a constructor application expected)"); return -1; }
    int c = rcm_ctor_of(head->sym_id);
    if (c < 0) { snprintf(err, errcap, "unknown constructor %s", atom_name_cstr(head)); return -1; }
    if (!rc_ctor_judgment[c]) { snprintf(err, errcap, "the head is a data constructor, not a judgment"); return -1; }
    if (*n != rc_ctor_arity[c]) { snprintf(err, errcap, "%s takes %u arguments, given %u", rc_ctor_name[c], (unsigned)rc_ctor_arity[c], (unsigned)*n); return -1; }
    if (*n > 16) { snprintf(err, errcap, "%s has more than 16 arguments", rc_ctor_name[c]); return -1; }
    return c;
}
/* the capture of a call: its arguments' nodes and its result */
static void rcm_capture_call(const RcmRun *run) {
    int k[16];
    int j = run->judgment;
    for (int i = 0; i < run->nargs; i++) k[i] = rcm_cap_value(run->args[i], rc_ctor_asort[rc_ctor_asort_off[j] + i]);
    int rid = !run->violation[0] && run->result ? rcm_cap_value(run->result, rc_ctor_sort[j]) : 0;
    fprintf(rcm_cap, "c %s", rc_ctor_name[j]);
    for (int i = 0; i < run->nargs; i++) fprintf(rcm_cap, " %d", k[i]);
    if (run->violation[0]) fputs(" = error\n", rcm_cap);
    else if (!run->result) fputs(" = none\n", rcm_cap);
    else fprintf(rcm_cap, " = %d\n", rid);
    fflush(rcm_cap);
}

static Atom *rcm_call(Arena *a, Atom *term, bool keep) {
    char err[256];
    rcm_ncalls++;
    Atom *const *kids;
    uint32_t n;
    int j = rcm_call_head(term, &kids, &n, err, sizeof err);
    if (j < 0) return rcm_error(a, err);
    if (keep && !rcm_outlives[rc_ctor_sort[j]])
        return rcm_error(a, "the judgment's values do not outlive a call (their sort is not interned, immediate or nullary)");
    RcmRun run = {.judgment = j, .argv = kids, .nargs = (int)n, .hold_sort = -1, .stream = -1};
    Atom *result;
    if (!rcm_execute(&run)) {
        result = rcm_error(a, "cannot start the module's thread");
    } else if (run.input[0]) {
        result = rcm_error(a, run.input);
    } else {
        if (rcm_capturing()) rcm_capture_call(&run);
        if (run.violation[0]) result = rcm_error(a, run.violation);
        else if (!run.result) result = atom_expr(a, (Atom *[]){atom_symbol(a, RCM_TAG ":none")}, 1);
        else if (keep) {
            /* kept before the store's end of run, so that a collection there keeps it */
            Atom *e[2] = {atom_symbol(a, RCM_TAG ":value"), rcm_handle_atom(a, rcm_hold(run.result, rc_ctor_sort[j]))};
            result = atom_expr(a, e, 2);
        } else {
            uint64_t t1 = rcm_now();
            Atom *e[2] = {atom_symbol(a, RCM_TAG ":value"), rcm_atom(a, run.result, rc_ctor_sort[j])};
            result = atom_expr(a, e, 2);
            rcm_view_ns += rcm_now() - t1;
        }
    }
    if (rcm_interned && !run.violation[0]) rc_intern_end();
    if (rcm_tables) rc_table_reset();
    rc_runtime_reset();
    return result;
}

static bool rcm_text(Atom *x, const char **out) {
    if (x && x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING) { *out = x->ground.sval; return true; }
    if (x && x->kind == ATOM_SYMBOL) { *out = atom_name_cstr(x); return true; }
    return false;
}

static Atom *rcm_set_relation(Arena *a, Atom *name_atom, Atom *rows) {
    const char *name;
    if (!rcm_text(name_atom, &name)) return rcm_error(a, "expected a relation name");
    int rel = -1;
    for (int i = 0; i < rc_nrelations; i++) if (!strcmp(rc_relation_names[i], name)) rel = i;
    if (rel < 0) return rcm_error(a, "the module's rules query no relation of that name");
    if (!rows || rows->kind != ATOM_EXPR) return rcm_error(a, "expected the rows as an expression ((A ...) ...)");
    RcmRelation fresh = {0, 0, -1, NULL, NULL};
    char err[256];
    for (CettaExprLen i = 0; i < rows->expr.len; i++) {
        Atom *row = rows->expr.elems[i];
        if (!row || row->kind != ATOM_EXPR) { snprintf(err, sizeof err, "row %llu is not an expression", (unsigned long long)i); goto bad; }
        int n = (int)row->expr.len;
        if (fresh.arity < 0) fresh.arity = n;
        if (n != fresh.arity) { snprintf(err, sizeof err, "rows of different lengths"); goto bad; }
        for (int j = 0; j < n; j++) {
            /* a relation's arguments have no declared sorts: any data constructor */
            RcmTree *t = rcm_tree(row->expr.elems[j], -1, err, sizeof err);
            if (!t) goto bad;
            /* rows outlive calls and handles: they hold module data, never handles */
            if (t->held) { rcm_tree_free(t); snprintf(err, sizeof err, "a relation row holds module data, not a handle"); goto bad; }
            if (rc_ctor_judgment[t->ctor]) { rcm_tree_free(t); snprintf(err, sizeof err, "a judgment in a row"); goto bad; }
            if (fresh.nrows * n + j >= fresh.cap) { fresh.cap = 2 * fresh.cap + 8; fresh.rows = realloc(fresh.rows, (size_t)fresh.cap * sizeof(RcmTree *)); }
            fresh.rows[fresh.nrows * n + j] = t;
        }
        fresh.nrows++;
    }
    {
        RcmRelation *r = &rcm_rel[rel];
        for (int j = 0; j < r->nrows * r->arity; j++) rcm_tree_free(r->rows[j]);
        free(r->rows); free(r->built);
        *r = fresh;
        if (r->arity < 0) r->arity = 0;
        if (rcm_capturing()) {
            int *ids = malloc(((size_t)(r->nrows * r->arity) + 1) * sizeof(int));
            for (int j = 0; j < r->nrows * r->arity; j++) ids[j] = rcm_cap_tree(r->rows[j]);
            fprintf(rcm_cap, "r %s %d", name, r->arity);
            for (int j = 0; j < r->nrows * r->arity; j++) fprintf(rcm_cap, " %d", ids[j]);
            fputc('\n', rcm_cap);
            fflush(rcm_cap);
            free(ids);
        }
    }
    return atom_expr(a, (Atom *[]){atom_symbol(a, RCM_TAG ":rows"), atom_int(a, (int64_t)fresh.nrows)}, 2);
bad:
    for (int j = 0; j < fresh.nrows * (fresh.arity > 0 ? fresh.arity : 0); j++) rcm_tree_free(fresh.rows[j]);
    free(fresh.rows);
    return rcm_error(a, err);
}

#ifdef RCM_FEED
/* sorts whose values may hold a promise (the stream's end): reachable through data constructors */
static bool *rcm_holds_promise;
static void rcm_promise_sorts(void) {
    rcm_holds_promise = calloc((size_t)rc_nsorts + 1, sizeof(bool));
    for (int s = 0; s < rc_nsorts; s++) rcm_holds_promise[s] = rc_sort_rep[s] == RC_REP_PROMISE;
    for (bool changed = true; changed;) {
        changed = false;
        for (int c = 0; c < rc_nctors; c++) {
            if (rc_ctor_judgment[c] || rcm_holds_promise[rc_ctor_sort[c]]) continue;
            for (int i = 0; i < rc_ctor_arity[c]; i++)
                if (rcm_holds_promise[rc_ctor_asort[rc_ctor_asort_off[c] + i]]) { rcm_holds_promise[rc_ctor_sort[c]] = changed = true; break; }
        }
    }
}
static uint8_t *rcm_slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    uint8_t *b = malloc(cap);
    while (b) {
        if (n == cap) { uint8_t *g = realloc(b, cap * 2); if (!g) { free(b); b = NULL; break; } b = g; cap *= 2; }
        size_t got = fread(b + n, 1, cap - n, f);
        n += got;
        if (!got) break;
    }
    fclose(f);
    *len = n;
    return b;
}
/* OPS feed PATH TERM and OPS feed-measure PATH TERM */
static Atom *rcm_feed_call(Arena *a, Atom *path_atom, Atom *term, bool measure) {
    char err[256];
    const char *path;
    if (!rcm_text(path_atom, &path)) return rcm_error(a, "expected the path of a stream");
    Atom *const *kids;
    uint32_t n;
    int j = rcm_call_head(term, &kids, &n, err, sizeof err);
    if (j < 0) return rcm_error(a, err);
    int stream_sort = -1, stream = -1, streams = 0;
    for (int s = 0; s < rc_nsorts; s++) if (!strcmp(rc_sort_name[s], RCM_FEED.stream_sort)) stream_sort = s;
    for (uint32_t i = 0; i < n; i++) {
        Atom *x = kids[i];
        if (!(x && x->kind == ATOM_EXPR && x->expr.len == 1 && x->expr.elems[0]->kind == ATOM_SYMBOL && x->expr.elems[0]->sym_id == rcm_stream_sym)) continue;
        if (rc_ctor_asort[rc_ctor_asort_off[j] + i] != stream_sort) {
            snprintf(err, sizeof err, "argument %u of %s is not of the stream's sort %s", i + 1, rc_ctor_name[j], RCM_FEED.stream_sort);
            return rcm_error(a, err);
        }
        stream = (int)i;
        streams++;
    }
    if (streams != 1) { snprintf(err, sizeof err, "the call names the stream (" RCM_TAG ":stream) %d times, not once", streams); return rcm_error(a, err); }
    rcm_ncalls++;
    size_t len = 0;
    uint8_t *bytes = rcm_slurp(path, &len);
    if (!bytes) return rcm_error(a, "cannot read the stream");
    if (!rcm_holds_promise) rcm_promise_sorts();
    rcm_feed_settle = rcm_holds_promise[rc_ctor_sort[j]];
    memset(&rc_stats, 0, sizeof rc_stats);
    RcmRun run = {.judgment = j, .argv = kids, .nargs = (int)n, .hold_sort = -1, .stream = stream, .feed = true, .bytes = bytes, .len = len};
    Atom *result;
    if (!rcm_execute(&run)) result = rcm_error(a, "cannot start the module's thread");
    else if (run.input[0]) result = rcm_error(a, run.input);
    else if (run.violation[0]) result = rcm_error(a, run.violation);
    else if (measure) {
        RcFeedStats fs = rc_feed_stats();
        uint64_t mod = run.total_ns > fs.read_ns ? run.total_ns - fs.read_ns : 0;
        Atom *e[7] = {atom_symbol(a, RCM_TAG ":measure"), atom_int(a, (int64_t)fs.records), atom_int(a, (int64_t)fs.read_ns),
                      atom_int(a, (int64_t)mod), atom_int(a, (int64_t)rc_alloc_total()),
                      atom_int(a, (int64_t)rc_stats.promoted_bytes), atom_int(a, (int64_t)rc_stats.old_live_peak)};
        result = atom_expr(a, e, 7);
    } else if (!run.result) result = atom_expr(a, (Atom *[]){atom_symbol(a, RCM_TAG ":none")}, 1);
    else {
        uint64_t t1 = rcm_now();
        Atom *e[2] = {atom_symbol(a, RCM_TAG ":value"), rcm_atom(a, run.result, rc_ctor_sort[j])};
        result = atom_expr(a, e, 2);
        rcm_view_ns += rcm_now() - t1;
    }
    /* the reader's run ends after the result was read (a stopped run ended it already; a run whose
     * atoms were refused never began it) */
    if (run.feed_begun) rc_feed_end(false);
    if (rcm_interned && !run.violation[0]) rc_intern_end();
    if (rcm_tables) rc_table_reset();
    rc_runtime_reset();
    free(bytes);
    return result;
}
#endif

/* OPS hold TERM: the value built once in the store and kept */
static Atom *rcm_hold_term(Arena *a, Atom *term) {
    rcm_nholds++;
    int sort;
    long h = rcm_handle_index(term);
    if (h == -2) return rcm_error(a, "a handle names a natural number");
    if (h >= 0) {
        if (!rcm_held_valid((size_t)h)) { char m[64]; snprintf(m, sizeof m, "stale handle %ld", h); return rcm_error(a, m); }
        sort = rcm_held[h].sort;
    } else {
        Atom *head = term && term->kind == ATOM_SYMBOL ? term
            : term && term->kind == ATOM_EXPR && term->expr.len >= 1 && term->expr.elems[0]->kind == ATOM_SYMBOL ? term->expr.elems[0] : NULL;
        if (!head) return rcm_error(a, "not module data (a constructor application expected)");
        int c = rcm_ctor_of(head->sym_id);
        if (c < 0) { char m[256]; snprintf(m, sizeof m, "unknown constructor %s", atom_name_cstr(head)); return rcm_error(a, m); }
        if (rc_ctor_judgment[c]) return rcm_error(a, "a judgment is not module data (call-hold keeps a judgment's value)");
        sort = rc_ctor_sort[c];
    }
    if (!rcm_outlives[sort]) return rcm_error(a, "values of this sort do not outlive a call (not interned, immediate or nullary)");
    RcmRun run = {.judgment = -1, .argv = &term, .nargs = 1, .hold_sort = sort, .stream = -1};
    Atom *result;
    if (!rcm_execute(&run)) result = rcm_error(a, "cannot start the module's thread");
    else if (run.input[0]) result = rcm_error(a, run.input);
    else if (run.violation[0]) result = rcm_error(a, run.violation);
    else result = rcm_handle_atom(a, rcm_hold(run.result, sort));
    if (rcm_interned && !run.violation[0]) rc_intern_end();
    if (rcm_tables) rc_table_reset();
    rc_runtime_reset();
    return result;
}
static Atom *rcm_handle_op(Arena *a, Atom *x, const char *op) {
    long h = rcm_handle_index(x);
    if (h < 0) return rcm_error(a, "expected a handle");
    if (!rcm_held_valid((size_t)h)) { char m[64]; snprintf(m, sizeof m, "stale handle %ld", h); return rcm_error(a, m); }
    if (!strcmp(op, "release")) {
        rcm_held[h].live = false;
        Atom *e[2] = {atom_symbol(a, RCM_TAG ":released"), atom_int(a, (int64_t)h)};
        return atom_expr(a, e, 2);
    }
    uint64_t t1 = rcm_now();
    Atom *e[2] = {atom_symbol(a, RCM_TAG ":value"), rcm_atom(a, rcm_held[h].value, rcm_held[h].sort)};
    rcm_view_ns += rcm_now() - t1;
    return atom_expr(a, e, 2);
}

Atom *RCM_DISPATCH(struct CettaLibraryContext *ctx, Space *space, Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    (void)ctx; (void)space;
    static const char prefix[] = RCM_OPS;
    if (!head || head->kind != ATOM_SYMBOL || strncmp(atom_name_cstr(head), prefix, sizeof prefix - 1)) return NULL;
    const char *op = atom_name_cstr(head) + sizeof prefix - 1;
    Atom *result;
    pthread_mutex_lock(&rcm_gate);
    if (!rcm_ready) { rcm_init(); rcm_ready = true; }
    if (rcm_too_wide) {
        char m[256];
        snprintf(m, sizeof m, "constructor %s has more than 16 children, more than this glue reads and writes", rcm_too_wide);
        result = rcm_error(a, m);
    }
    else if (!strcmp(op, "call")) result = nargs == 1 ? rcm_call(a, args[0], false) : rcm_error(a, "expected one term");
    else if (!strcmp(op, "call-hold")) result = nargs == 1 ? rcm_call(a, args[0], true) : rcm_error(a, "expected one term");
    else if (!strcmp(op, "hold")) result = nargs == 1 ? rcm_hold_term(a, args[0]) : rcm_error(a, "expected one term");
    else if (!strcmp(op, "view") || !strcmp(op, "release")) result = nargs == 1 ? rcm_handle_op(a, args[0], op) : rcm_error(a, "expected a handle");
    else if (!strcmp(op, "reset")) {
        /* every handle is refused from now on, whether or not the module keeps an interning store */
        for (size_t i = 0; i < rcm_nheld; i++) { rcm_held[i].live = false; rcm_held[i].value = NULL; }
        if (rcm_interned) rc_intern_free();
        result = atom_expr(a, (Atom *[]){atom_symbol(a, RCM_TAG ":reset")}, 1);
    }
    else if (!strcmp(op, "set-relation")) result = nargs == 2 ? rcm_set_relation(a, args[0], args[1]) : rcm_error(a, "expected a name and rows");
#ifdef RCM_FEED
    else if (!strcmp(op, "feed") || !strcmp(op, "feed-measure"))
        result = nargs == 2 ? rcm_feed_call(a, args[0], args[1], op[4] == '-') : rcm_error(a, "expected a stream's path and a call");
#endif
    else if (!strcmp(op, "stats")) {
        Atom *e[10] = {atom_symbol(a, RCM_TAG ":stats"), atom_int(a, (int64_t)rcm_ncalls), atom_int(a, (int64_t)rcm_read_ns),
                      atom_int(a, (int64_t)rcm_build_ns), atom_int(a, (int64_t)rcm_module_ns), atom_int(a, (int64_t)rcm_view_ns),
                      atom_int(a, (int64_t)rcm_nodes_read), atom_int(a, (int64_t)rcm_nodes_viewed),
                      atom_int(a, (int64_t)rcm_nholds), atom_int(a, (int64_t)rcm_held_roots(NULL))};
        result = atom_expr(a, e, 10);
    }
    else result = NULL;
    pthread_mutex_unlock(&rcm_gate);
    return result;
}
