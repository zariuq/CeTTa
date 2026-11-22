/* rc_feed.c - see rc_feed.h.  Included in the translation unit of the module that is fed, after the
 * runtime, the components and the generated code (with its ids header). */
#include "rc_feed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* records per stop of the reader: the reader reads at most this many records past the last one the
 * module asked for */
#ifndef RC_FEED_BATCH
#define RC_FEED_BATCH 256
#endif

/* a record taken from the reader: its opcode and its operands (rf_ops[at .. at + n)) */
typedef struct { uint16_t op; uint16_t n; uint32_t at; } RfRec;
/* an operand: a word (v), or n bytes, n words or n / 2 pairs of words at rf_pool + v */
enum { RF_WORD, RF_BYTES, RF_WORDS, RF_PAIRS };
typedef struct { uint8_t kind; uint32_t n; uint64_t v; } RfOp;

/* this module's side of the record table: per opcode, the constructor and how each child is built
 * (an immediate natural, an interned byte string and a symbolic list or record directly; any other
 * representation through the generated code) */
enum { RB_IMM, RB_NUMBER, RB_IBYTES, RB_LIST1, RB_LIST2 };
typedef struct { int ctor; int n; bool symbolic; uint8_t how[8]; uint16_t sort[8]; int nil[8], cons[8]; bool imm[8]; } RbRow;

static struct {
    const RcFeed *f;
    const RcInstanceApi *R;
    const RcFeed *ready_for;      /* the description the ids below were resolved for */
    /* the reader's constructors */
    int k_entry, k_wrapper, s_record;
    int k_record, k_ocons, k_onil, k_word, k_bytes, k_words, k_wcons, k_wnil, k_pairs, k_pcons, k_pnil, k_pair;
    /* this module's rows, by opcode (a byte; -1: none) */
    int16_t row_of[256];
    RbRow *rows;
    int stream_sort;
    /* the run */
    bool active, stopped, ended;
    uint64_t gen;                 /* the run's number, in its promise */
    size_t base;                  /* the reader's frames above it hold records */
    T *final;                     /* the reader's value once it has ended */
    RfRec *recs; size_t nrecs, reccap;
    RfOp *ops; size_t nops, opcap;
    uint8_t *pool; size_t pool_len, pool_cap;
    RcItem *items; size_t items_cap;
    /* the terms of the records taken at the last stop (records bbase ..), built while their operands
     * are at hand; roots of this module's collections (rc_root_slots) */
    T *bterm[RC_FEED_BATCH + 1];
    uint64_t bbase;
    size_t bn;
    RcFeedStats stats;
} rf;

uint64_t rc_record_count;

static uint64_t rf_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static void *rf_grow_to(void *p, size_t *cap, size_t need, size_t size, size_t first) {
    size_t c = *cap ? *cap : first;
    while (c < need) c *= 2;
    void *q = realloc(p, c * size);
    if (!q) { fputs("rc_feed: out of memory\n", stderr); exit(3); }
    *cap = c;
    return q;
}
static inline void *rf_grow(void *p, size_t *cap, size_t need, size_t size, size_t first) {
    return need <= *cap ? p : rf_grow_to(p, cap, need, size, first);
}
static size_t rf_pool_add(const void *p, size_t n) {
    size_t at = (rf.pool_len + 7u) & ~(size_t)7u;
    rf.pool = rf_grow(rf.pool, &rf.pool_cap, at + n, 1, (size_t)1 << 16);
    if (n) memcpy(rf.pool + at, p, n);
    rf.pool_len = at + n;
    return at;
}

/* ---------------------------------------------------------------- set-up */
static int rf_reader_ctor(const char *name) {
    int c = rf.R->ctor_named(name);
    if (c < 0) { fprintf(stderr, "rc_feed: the reader %s has no constructor %s\n", rf.R->module, name); exit(3); }
    return c;
}
/* how a child of sort s holds an operand: a number, a byte string, or a list of one or two numbers */
static void rf_child(RbRow *w, int i, int s) {
    w->sort[i] = (uint16_t)s;
    w->nil[i] = w->cons[i] = -1;
    int r = rc_sort_rep[s];
    if (r == RC_REP_NAT || r == RC_REP_UNARY) { w->how[i] = RB_IMM; return; }        /* immediates */
    if (r == RC_REP_WORD || r == RC_REP_BINARY) { w->how[i] = RB_NUMBER; return; }
    if (r == RC_REP_BYTES || r == RC_REP_SLICES) { w->how[i] = RB_IBYTES; return; }  /* interned */
    for (int c = 0; c < rc_nctors; c++) {
        if (rc_ctor_judgment[c] || rc_ctor_sort[c] != s) continue;
        int n = rc_ctor_arity[c];
        /* exactly one constructor without children and one cell; a second of either refuses */
        if (!n) { w->nil[i] = w->nil[i] == -1 ? c : -2; continue; }
        const uint16_t *as = &rc_ctor_asort[rc_ctor_asort_off[c]];
        bool numbers = (n == 2 || n == 3) && as[n - 1] == s;
        for (int j = 0; j + 1 < n && numbers; j++) {
            int rj = rc_sort_rep[as[j]];
            numbers = rj == RC_REP_NAT || rj == RC_REP_UNARY || rj == RC_REP_WORD || rj == RC_REP_BINARY;
        }
        if (!numbers || w->cons[i] != -1) { w->cons[i] = -2; continue; }
        w->cons[i] = c;
    }
    if (w->nil[i] < 0 || w->cons[i] < 0) {
        fprintf(stderr, "rc_feed: child %d of %s, of sort %s, holds no operand (a number, a byte string or a list of numbers)\n",
                i, rc_ctor_name[w->ctor], rc_sort_name[s]);
        exit(3);
    }
    w->how[i] = rc_ctor_arity[w->cons[i]] == 2 ? RB_LIST1 : RB_LIST2;
    /* a symbolic list of immediate naturals is built directly */
    const uint16_t *as = &rc_ctor_asort[rc_ctor_asort_off[w->cons[i]]];
    w->imm[i] = r == RC_REP_SYMBOLIC && (rc_sort_rep[as[0]] == RC_REP_NAT || rc_sort_rep[as[0]] == RC_REP_UNARY) &&
                (rc_ctor_arity[w->cons[i]] == 2 || rc_sort_rep[as[1]] == RC_REP_NAT || rc_sort_rep[as[1]] == RC_REP_UNARY);
}
static void rf_setup(const RcFeed *f) {
    rf.f = f;
    rf.R = f->reader;
    if (rf.ready_for == f) return;
    rf.R->init();
    rf.k_entry = rf_reader_ctor(f->entry);
    rf.k_wrapper = rf_reader_ctor(f->wrapper);
    rf.s_record = rf.R->ctor_arg_sort(rf.k_wrapper, f->element);
    if (rf.s_record < 0) { fprintf(stderr, "rc_feed: %s has no argument %d\n", f->wrapper, f->element); exit(3); }
    rf.k_record = rf_reader_ctor("BRecord"); rf.k_ocons = rf_reader_ctor("OperandsCons"); rf.k_onil = rf_reader_ctor("OperandsNil");
    rf.k_word = rf_reader_ctor("BWord"); rf.k_bytes = rf_reader_ctor("BBytes"); rf.k_words = rf_reader_ctor("BWords");
    rf.k_wcons = rf_reader_ctor("WordsCons"); rf.k_wnil = rf_reader_ctor("WordsNil"); rf.k_pairs = rf_reader_ctor("BPairs");
    rf.k_pcons = rf_reader_ctor("PairsCons"); rf.k_pnil = rf_reader_ctor("PairsNil"); rf.k_pair = rf_reader_ctor("BPair");
    rf.stream_sort = -1;
    for (int s = 0; s < rc_nsorts; s++) if (!strcmp(rc_sort_name[s], f->stream_sort)) rf.stream_sort = s;
    if (rf.stream_sort < 0) { fprintf(stderr, "rc_feed: no sort %s\n", f->stream_sort); exit(3); }
    for (size_t i = 0; i < sizeof rf.row_of / sizeof rf.row_of[0]; i++) rf.row_of[i] = -1;
    free(rf.rows);
    rf.rows = calloc(f->nrecords ? f->nrecords : 1, sizeof *rf.rows);
    if (!rf.rows) { fputs("rc_feed: out of memory\n", stderr); exit(3); }
    for (size_t i = 0; i < f->nrecords; i++) {
        const RcFeedRecord *x = &f->records[i];
        if (x->opcode >= sizeof rf.row_of / sizeof rf.row_of[0] || rf.row_of[x->opcode] >= 0 || x->ctor >= rc_nctors ||
            rc_ctor_judgment[x->ctor] || rc_ctor_arity[x->ctor] > 8) {
            fprintf(stderr, "rc_feed: record table row %zu (opcode %u): a repeated or unknown opcode, or not a data constructor of at most 8 children\n",
                    i, (unsigned)x->opcode);
            exit(3);
        }
        RbRow *w = &rf.rows[i];
        w->ctor = x->ctor;
        w->n = rc_ctor_arity[x->ctor];
        w->symbolic = rc_sort_rep[rc_ctor_sort[x->ctor]] == RC_REP_SYMBOLIC;
        for (int j = 0; j < w->n; j++) rf_child(w, j, rc_ctor_asort[rc_ctor_asort_off[x->ctor] + j]);
        rf.row_of[x->opcode] = (int16_t)i;
    }
    rf.ready_for = f;
}

/* ---------------------------------------------------------------- taking records */
static _Noreturn void rf_bad(const char *what) { rc_refinement_fail(what); }
/* one record from the items at *pi (the byte-record library's shape) */
static void rf_take_record(const RcItem *it, size_t n, size_t *pi) {
    size_t i = *pi;
    #define ITEM(c) (i < n && it[i].ctor == (c))
    if (!ITEM(rf.k_record)) rf_bad("the reader's record is not a BRecord");
    i++;
    if (!ITEM(-1) || it[i].v >= sizeof rf.row_of / sizeof rf.row_of[0]) rf_bad("the reader's record has no opcode");
    rf.recs = rf_grow(rf.recs, &rf.reccap, rf.nrecs + 1, sizeof *rf.recs, 1024);
    RfRec *x = &rf.recs[rf.nrecs];
    x->op = (uint16_t)it[i++].v;
    x->n = 0;
    x->at = (uint32_t)rf.nops;
    if (rf.nops > UINT32_MAX - 64) rf_bad("too many operands in one stream");
    while (!ITEM(rf.k_onil)) {
        if (!ITEM(rf.k_ocons)) rf_bad("the reader's operands are not a list");
        i++;
        rf.ops = rf_grow(rf.ops, &rf.opcap, rf.nops + 1, sizeof *rf.ops, 4096);
        RfOp *o = &rf.ops[rf.nops];
        if (ITEM(rf.k_word)) {
            i++;
            if (!ITEM(-1)) rf_bad("a BWord operand is not a number");
            *o = (RfOp){RF_WORD, 0, it[i++].v};
        } else if (ITEM(rf.k_bytes)) {
            i++;
            if (!ITEM(-2)) rf_bad("a BBytes operand is not a byte string");
            *o = (RfOp){RF_BYTES, it[i].n, rf_pool_add(it[i].p, it[i].n)};
            i++;
        } else if (ITEM(rf.k_words) || ITEM(rf.k_pairs)) {
            bool pairs = ITEM(rf.k_pairs);
            i++;
            /* the numbers in the pool, read in place */
            size_t at = (rf.pool_len + 7u) & ~(size_t)7u, m = 0;
            for (;;) {
                if (pairs ? ITEM(rf.k_pnil) : ITEM(rf.k_wnil)) { i++; break; }
                if (!(pairs ? ITEM(rf.k_pcons) : ITEM(rf.k_wcons))) rf_bad("a list operand is not a list");
                i++;
                if (pairs) { if (!ITEM(rf.k_pair)) rf_bad("a pair operand is not a BPair"); i++; }
                for (int e = 0; e < (pairs ? 2 : 1); e++) {
                    if (!ITEM(-1)) rf_bad("a list operand holds something else than numbers");
                    rf.pool = rf_grow(rf.pool, &rf.pool_cap, at + (m + 1) * sizeof(uint64_t), 1, (size_t)1 << 16);
                    memcpy(rf.pool + at + m * sizeof(uint64_t), &it[i++].v, sizeof(uint64_t));
                    m++;
                }
            }
            rf.pool_len = at + m * sizeof(uint64_t);
            if (m > UINT32_MAX) rf_bad("a list operand too long");
            *o = (RfOp){pairs ? RF_PAIRS : RF_WORDS, (uint32_t)m, at};
        } else rf_bad("an operand of an unknown kind");
        rf.nops++;
        x->n++;
    }
    i++;
    #undef ITEM
    rf.nrecs++;
    *pi = i;
}
static T *rf_build(uint64_t i);
/* the records waiting in the reader's frames above rf.base, the oldest first; their frames dropped */
static void rf_take(void) {
    size_t depth = *rf.R->depth;
    const RcPend *frames = *rf.R->frames;
    for (size_t fr = rf.base; fr < depth; fr++) {
        const RcPend *pf = &frames[fr];
        if (pf->w != rf.k_wrapper || pf->k == rf.f->element || rf.f->element >= pf->n)
            rf_bad("a frame above the stream's records is not the record wrapper's");
        T *rec = pf->a[rf.f->element];
        for (;;) {
            size_t n = rf.R->value_items(rf.s_record, rec, rf.items, rf.items_cap);
            if (n != (size_t)-1) { size_t i = 0; rf_take_record(rf.items, n, &i); if (i != n) rf_bad("a record with items after it"); break; }
            if (rf.items_cap >= ((size_t)1 << 24)) rf_bad("a record too large to read");
            rf.items = rf_grow(rf.items, &rf.items_cap, rf.items_cap ? 2 * rf.items_cap : 256, sizeof *rf.items, 256);
        }
    }
    rf.R->pend_drop(rf.base);
    rf.stats.records = rf.nrecs;
    rc_record_count = rf.nrecs;
    /* this batch's terms (the operands just written are at hand); the last batch's are dropped */
    size_t taken = depth - rf.base;
    rf.bn = 0;
    rf.bbase = rf.nrecs - taken;
    rc_root_slots = rf.bterm;
    rc_root_nslots = 0;
    if (taken > RC_FEED_BATCH) return;   /* more than a stop takes: built when reached */
    for (uint64_t i = rf.bbase; i < rf.nrecs; i++) { rf.bterm[rf.bn] = rf_build(i); rf.bn++; rc_root_nslots = rf.bn; }
}

/* the reader stops at its safepoint once a batch of records waits in frames */
static bool rf_stop(void) {
    return *rf.R->depth - rf.base >= RC_FEED_BATCH;
}
/* the records' frames lie above the frames the reader's entry left below its loop (none in a reader
 * whose entry reaches the loop by tail calls) */
static void rf_find_base(void) {
    size_t depth = *rf.R->depth;
    const RcPend *frames = *rf.R->frames;
    rf.base = rf.R->stop_mark();
    while (rf.base < depth && frames[rf.base].w != rf.k_wrapper) rf.base++;
}
/* the reader has stopped (v == RC_STOPPED_VALUE) or ended with v */
static void rf_after(T *v) {
    if (v == RC_STOPPED_VALUE) { rf.stats.stops++; rf_take(); return; }
    rf_take();
    rf.stopped = false;
    rf.ended = true;
    /* the frames below the records (none in a reader whose records start its loop) */
    v = rf.R->pend_apply(rf.R->stop_mark(), v);
    if (!v) rf_bad("the reader gives no value for the stream");
    rf.final = v;
}
static void rf_continue(void) {
    uint64_t t0 = rf_now();
    rf_after(rf.R->resume());
    rf.stats.read_ns += rf_now() - t0;
}

/* the cursor: record i exists once the reader has read it */
bool rc_record_more(uint64_t i) {
    while (i >= rf.nrecs) {
        if (!rf.active || !rf.stopped) return false;
        rf_continue();
    }
    return true;
}

/* record i as this module's term */
#define RF_IMM_MAX (UINT64_MAX >> 3)
static T *rf_build(uint64_t i) {
    const RfRec *x = &rf.recs[i];
    int r = rf.row_of[x->op];
    if (r < 0) rf_bad("a record whose opcode the record table does not name");
    const RbRow *w = &rf.rows[r];
    if (x->n != w->n) rf_bad("a record whose operands are not its constructor's children");
    T *kids[8];
    const RfOp *o = &rf.ops[x->at];
    for (int j = 0; j < w->n; j++, o++) {
        T *k = NULL;
        switch (w->how[j]) {
        case RB_IMM:
            if (o->kind == RF_WORD && o->v <= RF_IMM_MAX) k = RC_IMM(o->v);
            break;
        case RB_NUMBER:
            if (o->kind == RF_WORD) k = rc_value_of_u64(w->sort[j], o->v);
            break;
        case RB_IBYTES:
            if (o->kind == RF_BYTES) k = rc_intern_bytes(rf.pool + o->v, o->n);
            break;
        default: {
            uint32_t per = w->how[j] == RB_LIST1 ? 1 : 2;
            if (o->kind != (per == 1 ? RF_WORDS : RF_PAIRS) || o->n % per) break;
            const uint64_t *p = (const uint64_t *)(rf.pool + o->v);
            size_t m = o->n / per;
            T *list = rc_construct(w->nil[j], NULL);
            if (w->imm[j] && m) {
                /* a symbolic list of immediates: its cells in one allocation, the last one first */
                size_t cell = sizeof(TH) + (per + 1) * sizeof(T *);
                char *mem = (char *)rc_alloc_bytes(cell * m);
                for (size_t e = m; e-- > 0;) {
                    T *t = (T *)(mem + e * cell);
                    t->tag = (uint16_t)w->cons[j]; t->space = RC_SPACE_YOUNG; t->flags = 0; t->n = per + 1;
                    for (uint32_t q = 0; q < per; q++) {
                        uint64_t v = p[e * per + q];
                        if (v > RF_IMM_MAX) rf_bad("a number of a list operand that its sort cannot hold");
                        t->k[q] = RC_IMM(v);
                    }
                    t->k[per] = list;
                    list = t;
                }
            } else {
                const uint16_t *as = &rc_ctor_asort[rc_ctor_asort_off[w->cons[j]]];
                for (size_t e = m; e-- > 0;) {
                    T *ck[3];
                    for (uint32_t q = 0; q < per; q++)
                        if (!(ck[q] = rc_value_of_u64(as[q], p[e * per + q]))) rf_bad("a number of a list operand that its sort cannot hold");
                    ck[per] = list;
                    list = rc_construct(w->cons[j], ck);
                }
            }
            k = list;
        }
        }
        if (!k) rf_bad("an operand that does not fit its constructor's child");
        kids[j] = k;
    }
    T *t;
    uint16_t c = (uint16_t)w->ctor;
    if (!w->symbolic || !w->n) t = rc_construct(w->ctor, kids);
    else switch (w->n) {
        case 1: t = rc_mk1(c, kids[0]); break;
        case 2: t = rc_mk2(c, kids[0], kids[1]); break;
        case 3: t = rc_mk3(c, kids[0], kids[1], kids[2]); break;
        case 4: t = rc_mk4(c, kids[0], kids[1], kids[2], kids[3]); break;
        default: t = rc_mkv(c, (uint32_t)w->n, kids); break;
    }
    if (!t) rf_bad("a record's constructor the representation cannot build");
    return t;
}
T *rc_record_term(uint64_t i) {
    if (i >= rf.nrecs) rf_bad("a record the cursor has not reached");
    if (i - rf.bbase < rf.bn) return rf.bterm[i - rf.bbase];
    return rf_build(i);
}

/* the promise of the stream's end: forcing it reads the stream to its end */
static T *rf_promise(uint64_t id) {
    if (!rf.active || id != rf.gen) return NULL;
    while (rf.stopped) rf_continue();
    return rf.f->end(rf.R, rf.final);
}

/* ---------------------------------------------------------------- a run */
static void rf_reader_violation(const char *what) {
    char m[256];
    snprintf(m, sizeof m, "the reader %s: %s", rf.R->module, what);
    rc_refinement_fail(m);
}
T *rc_feed_begin(const RcFeed *f, const uint8_t *bytes, size_t len, const char **why) {
    if (rf.active) rc_feed_end(false);
    rf_setup(f);
    rf.active = true;
    rf.stopped = rf.ended = false;
    rf.gen++;
    rf.base = 0;
    rf.final = NULL;
    rf.nrecs = rf.nops = 0;
    rf.pool_len = 0;
    rf.bn = 0;
    rc_record_count = 0;
    memset(&rf.stats, 0, sizeof rf.stats);
    rc_promise_hook = rf_promise;
    rf.R->run_begin();
    rf.R->set_fail_hook(rf_reader_violation);
    T *in = rf.R->bytes_input(bytes, len);
    if (!in) { *why = "the stream is too long for the reader's input"; rc_feed_end(false); return NULL; }
    uint64_t t0 = rf_now();
    rf.base = *rf.R->depth;
    rf.R->set_stop_hook(rf_stop);
    /* the reader runs until a batch of records waits (its header is then an argument of its loop) or
     * to its end (a short stream, or one refused before its loop) */
    T *v = rf.R->start(rf.k_entry, &in);
    rf_find_base();
    T *header = NULL;
    if (v == RC_STOPPED_VALUE) {
        rf.stopped = true;
        header = rf.R->root(f->header_root);
        rf.stats.stops++;
        rf_take();
    } else {
        rf_after(v);
    }
    rf.stats.read_ns += rf_now() - t0;
    return f->stream(rf.R, header, rf.final, RC_IMM(0), RC_PROMISE(rf.gen));
}
void rc_feed_finish(void) {
    while (rf.active && rf.stopped) rf_continue();
}
void rc_feed_end(bool stopped) {
    if (!rf.active) return;
    rf.active = rf.stopped = false;
    rf.bn = 0;
    rc_root_nslots = 0;
    rc_promise_hook = NULL;
    rc_record_count = 0;
    rf.R->set_stop_hook(NULL);
    rf.R->set_fail_hook(NULL);
    if (stopped) rf.R->free_store();   /* a run stopped inside the reader leaves no state to keep */
    rf.R->run_end();
}
RcFeedStats rc_feed_stats(void) { return rf.stats; }
