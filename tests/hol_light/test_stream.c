/* test_stream.c - laws of HOLR replay (include/hol_replay.h):
 *   - every truncation, every single-bit flip and random garbage is rejected
 *     without crashing; a proper prefix fed as non-final is never malformed;
 *   - any split of the input bytes and any split of the record budget reach
 *     the same committed history, each record committed exactly once;
 *   - resource failures (allocation, fuel) are retryable and invisible in
 *     the committed history; so are collection and total hash collision;
 *   - clones continue independently and their post-checkpoint theories are
 *     incompatible branches;
 *   - probe mode, dependent failures and the validation order;
 *   - END count/checksum, trailing bytes, header fields, limits, policy;
 *   - EMIT delivery occurrences, LABEL, EXPECT, EXPECT_ALPHA, EXPECT_AXIOMS,
 *     DROP. */
#include "hol_replay.h"
#include "test_util.h"

#include <stdint.h>

/* ---- A small assembler over hol_writer ---------------------------------- */

typedef struct {
    HolWriter *w;
    uint64_t n, y, t, h; /* next id per space */
} Asm;

static void asm_init(Asm *a, uint32_t flags, HolAxiomPolicy policy) {
    memset(a, 0, sizeof *a);
    a->w = hol_writer_new(flags, policy);
    if (!a->w) { fprintf(stderr, "no writer\n"); exit(2); }
}
static void op(Asm *a, uint8_t o) { hol_writer_op(a->w, o); }
static void uv(Asm *a, uint64_t v) { hol_writer_uv(a->w, v); }

static uint64_t NAME(Asm *a, const char *s) {
    op(a, HOLR_NAME);
    uv(a, strlen(s));
    hol_writer_bytes(a->w, s, strlen(s));
    return a->n++;
}
static uint64_t TYVAR(Asm *a, uint64_t n) { op(a, HOLR_TYVAR); uv(a, n); return a->y++; }
static uint64_t TYAPP(Asm *a, uint64_t n, unsigned k, const uint64_t *args) {
    op(a, HOLR_TYAPP);
    uv(a, n);
    uv(a, k);
    for (unsigned i = 0; i < k; i++) uv(a, args[i]);
    return a->y++;
}
static uint64_t FUN(Asm *a, uint64_t nfun, uint64_t x, uint64_t y) {
    uint64_t args[2] = {x, y};
    return TYAPP(a, nfun, 2, args);
}
static uint64_t T2(Asm *a, uint8_t o, uint64_t x, uint64_t y) { op(a, o); uv(a, x); uv(a, y); return a->t++; }
static uint64_t H1(Asm *a, uint8_t o, uint64_t x) { op(a, o); uv(a, x); return a->h++; }
static uint64_t H2(Asm *a, uint8_t o, uint64_t x, uint64_t y) { op(a, o); uv(a, x); uv(a, y); return a->h++; }
static uint64_t INSTR(Asm *a, uint8_t o, unsigned np, const uint64_t *pairs, uint64_t th) {
    op(a, o);
    uv(a, np);
    for (unsigned i = 0; i < 2 * np; i++) uv(a, pairs[i]);
    uv(a, th);
    return a->h++;
}
static void EXPECT(Asm *a, uint8_t o, uint64_t th, unsigned nh, const uint64_t *hyps, uint64_t concl) {
    op(a, o);
    uv(a, th);
    uv(a, nh);
    for (unsigned i = 0; i < nh; i++) uv(a, hyps[i]);
    uv(a, concl);
}
static void EXPECT_AXIOMS(Asm *a, uint64_t th, unsigned n, const uint64_t *ids) {
    op(a, HOLR_EXPECT_AXIOMS);
    uv(a, th);
    uv(a, n);
    for (unsigned i = 0; i < n; i++) uv(a, ids[i]);
}
static void REC1(Asm *a, uint8_t o, uint64_t x) { op(a, o); uv(a, x); }

/* Finished stream bytes, owned by the caller. */
static uint8_t *asm_finish(Asm *a, size_t *len) {
    const uint8_t *p = hol_writer_finish(a->w, len);
    uint8_t *out = (uint8_t *)malloc(*len);
    memcpy(out, p, *len);
    hol_writer_free(a->w);
    return out;
}

/* Ids of interest in the sample stream. */
static uint64_t S_H13, S_TYPEDEF1, S_AX, S_AXUSE, S_RECORDS, S_H13_RECORD;

/* A stream using every record kind except POLICY and the failure records. */
static uint8_t *sample_stream(size_t *len) {
    Asm a;
    asm_init(&a, 0, HOL_AXIOMS_OPEN);
    uint64_t nA = NAME(&a, "A"), nbool = NAME(&a, "bool"), nfun = NAME(&a, "fun");
    uint64_t nx = NAME(&a, "x"), ny = NAME(&a, "y"), nf = NAME(&a, "f"), np = NAME(&a, "p");
    uint64_t neq = NAME(&a, "="), nT = NAME(&a, "T"), nq = NAME(&a, "q");
    uint64_t none = NAME(&a, "one"), nmk = NAME(&a, "mk_one"), ndest = NAME(&a, "dest_one");
    uint64_t nlab = NAME(&a, "TRANS_EXAMPLE"), nB = NAME(&a, "B");
    uint64_t yA = TYVAR(&a, nA), yB = TYVAR(&a, nB);
    uint64_t yb = TYAPP(&a, nbool, 0, NULL);
    uint64_t yAA = FUN(&a, nfun, yA, yA), yAb = FUN(&a, nfun, yA, yb), yAAb = FUN(&a, nfun, yA, yAb);
    uint64_t ybb = FUN(&a, nfun, yb, yb), ybbb = FUN(&a, nfun, yb, ybb);
    uint64_t ybbB = FUN(&a, nfun, ybb, yb), yEq2 = FUN(&a, nfun, ybb, ybbB);
    uint64_t x = T2(&a, HOLR_VAR, nx, yA), y = T2(&a, HOLR_VAR, ny, yA), f = T2(&a, HOLR_VAR, nf, yAA);
    uint64_t fx = T2(&a, HOLR_COMB, f, x), lx = T2(&a, HOLR_ABS, x, fx);
    uint64_t lxx = T2(&a, HOLR_COMB, lx, x), lxy = T2(&a, HOLR_COMB, lx, y), fy = T2(&a, HOLR_COMB, f, y);
    uint64_t p = T2(&a, HOLR_VAR, np, yb), idp = T2(&a, HOLR_ABS, p, p);
    uint64_t eq2 = T2(&a, HOLR_CONST, neq, yEq2);
    uint64_t e2 = T2(&a, HOLR_COMB, T2(&a, HOLR_COMB, eq2, idp), idp); /* (\p.p) = (\p.p) */
    uint64_t eqb = T2(&a, HOLR_CONST, neq, ybbb);
    uint64_t Tv = T2(&a, HOLR_VAR, nT, yb);
    uint64_t defn = T2(&a, HOLR_COMB, T2(&a, HOLR_COMB, eqb, Tv), e2);
    uint64_t eqa = T2(&a, HOLR_CONST, neq, yAAb);
    uint64_t c13 = T2(&a, HOLR_COMB, T2(&a, HOLR_COMB, eqa, lxy), fy); /* (\x. f x) y = f y */
    uint64_t H0 = H1(&a, HOLR_BETA, lxx);                    /* |- (\x. f x) x = f x */
    uint64_t pr[2] = {y, x};
    uint64_t Hi = INSTR(&a, HOLR_INST, 1, pr, H0);           /* |- (\x. f x) y = f y */
    uint64_t H2_ = H1(&a, HOLR_REFL, lx);
    uint64_t H3 = H1(&a, HOLR_ASSUME, e2);
    uint64_t H4 = H2(&a, HOLR_DEDUCT, H3, H3);               /* |- e2 = e2 */
    uint64_t H5 = H2(&a, HOLR_EQ_MP, H4, H3);                /* {e2} |- e2 */
    uint64_t Hdef = H1(&a, HOLR_NEW_DEFINITION, defn);
    uint64_t Tc = T2(&a, HOLR_CONST, nT, yb);
    uint64_t H7 = H1(&a, HOLR_REFL, Tc);                     /* |- T = T */
    op(&a, HOLR_NEW_TYPEDEF);
    uv(&a, none);
    uv(&a, nmk);
    uv(&a, ndest);
    uv(&a, H7);
    S_TYPEDEF1 = a.h;
    a.h += 2;
    uint64_t H11 = H1(&a, HOLR_REFL, y);
    uint64_t H12 = H2(&a, HOLR_MK_COMB, H2_, H11);           /* |- lx y = lx y */
    S_H13_RECORD = hol_writer_records(a.w);
    uint64_t H13 = H2(&a, HOLR_TRANS, H12, Hi);              /* |- lx y = f y */
    uint64_t H14 = H2(&a, HOLR_ABS_RULE, y, H13);
    uint64_t ty[2] = {yB, yA};
    uint64_t H15 = INSTR(&a, HOLR_INST_TYPE, 1, ty, H14);
    EXPECT(&a, HOLR_EXPECT, H13, 0, NULL, c13);
    uint64_t hy[1] = {e2};
    EXPECT(&a, HOLR_EXPECT, H5, 1, hy, e2);
    EXPECT(&a, HOLR_EXPECT_ALPHA, H5, 1, hy, e2);
    op(&a, HOLR_LABEL);
    uv(&a, H13);
    uv(&a, nlab);
    REC1(&a, HOLR_EMIT, H13);
    REC1(&a, HOLR_EMIT, H13);
    REC1(&a, HOLR_EMIT, H15);
    uint64_t q = T2(&a, HOLR_VAR, nq, yb);
    uint64_t Hax = H1(&a, HOLR_NEW_AXIOM, q);
    uint64_t Hq = H1(&a, HOLR_ASSUME, q);
    uint64_t Huse = H2(&a, HOLR_DEDUCT, Hax, Hq);            /* |- q = q, uses the axiom */
    uint64_t axl[1] = {Hax};
    EXPECT_AXIOMS(&a, Huse, 1, axl);
    EXPECT_AXIOMS(&a, Hax, 1, axl);
    EXPECT_AXIOMS(&a, H13, 0, NULL);
    REC1(&a, HOLR_DROP_THM, H0);
    REC1(&a, HOLR_DROP_TERM, lxx);
    REC1(&a, HOLR_DROP_TYPE, ybbB);
    uint64_t nnat2 = NAME(&a, "nat2"), nc0 = NAME(&a, "c0");
    op(&a, HOLR_NEW_TYPE);
    uv(&a, nnat2);
    uv(&a, 0);
    op(&a, HOLR_NEW_CONSTANT);
    uv(&a, nc0);
    uv(&a, yb);
    (void)Hdef;
    S_H13 = H13;
    S_AX = Hax;
    S_AXUSE = Huse;
    S_RECORDS = hol_writer_records(a.w);
    return asm_finish(&a, len);
}

/* With HOLR_FIXTURES_DIR set, every stream built here is also written out,
 * so that tools/fixture_diff.sh can replay it with the OCaml oracle. */
static void save_fixture(const char *fmt, int a, int b, const uint8_t *bytes, size_t n) {
    const char *dir = getenv("HOLR_FIXTURES_DIR");
    if (!dir) return;
    char name[256], path[1024];
    snprintf(name, sizeof name, fmt, a, b);
    snprintf(path, sizeof path, "%s/%s.hrs", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(bytes, 1, n, f);
    fclose(f);
}

/* ---- Replay driver with a history log ------------------------------------ */

typedef struct {
    HolReplayRecord *v;
    size_t n, cap;
} Log;

static void log_obs(void *ctx, const HolReplayRecord *rec) {
    Log *l = (Log *)ctx;
    if (l->n == l->cap) {
        l->cap = l->cap ? 2 * l->cap : 256;
        l->v = (HolReplayRecord *)realloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = *rec;
}

static bool log_equal(const Log *a, const Log *b, size_t from) {
    if (b->n + from != a->n) return false;
    for (size_t i = 0; i < b->n; i++) {
        const HolReplayRecord *x = &a->v[from + i], *y = &b->v[i];
        if (x->index != y->index || x->opcode != y->opcode || x->outcome != y->outcome ||
            x->status != y->status || x->reason != y->reason || x->space != y->space ||
            x->first_id != y->first_id || x->count != y->count || x->digest[0] != y->digest[0] ||
            x->digest[1] != y->digest[1] || x->alpha[0] != y->alpha[0] || x->alpha[1] != y->alpha[1])
            return false;
    }
    return true;
}

typedef struct {
    HolKernel *k;
    HolReplay *r;
    Log log;
} Run;

static void run_open(Run *u, const HolConfig *cfg, HolAxiomPolicy pol, const HolReplayLimits *lim) {
    memset(u, 0, sizeof *u);
    u->k = hol_kernel_new(cfg);
    u->r = hol_replay_new(u->k, lim, pol);
    hol_replay_set_observer(u->r, log_obs, &u->log);
}

static void run_close(Run *u) {
    hol_replay_free(u->r);
    hol_kernel_free(u->k);
    free(u->log.v);
}

/* Feeds everything at once and runs to a terminal state. */
static HolReplayState run_all(Run *u, const uint8_t *b, size_t n) {
    HolReplayState st = hol_replay_feed(u->r, b, n, true);
    if (st == HOL_REPLAY_MALFORMED) return st;
    return hol_replay_resume(u->r, 0);
}

static HolReplayState replay_bytes(const uint8_t *b, size_t n, HolReason *why) {
    Run u;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    HolReplayState st = run_all(&u, b, n);
    if (why) *why = hol_replay_reason(u.r);
    run_close(&u);
    return st;
}

static uint64_t xorshift(uint64_t *s) {
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

/* ------------------------------------------------------------------------ */

static uint8_t *SAMPLE;
static size_t SAMPLE_LEN;
static Log REF;

static void test_reference(void) {
    SECTION("reference");
    Run u;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    CHECK(run_all(&u, SAMPLE, SAMPLE_LEN) == HOL_REPLAY_ACCEPTED);
    HolReplayInfo info;
    hol_replay_info(u.r, &info);
    CHECK(info.records == S_RECORDS && info.offset == SAMPLE_LEN);
    CHECK(info.failed == 0 && info.expectations_checked == 6 && info.emitted == 3);
    CHECK(u.log.n == S_RECORDS);
    for (size_t i = 0; i < u.log.n; i++) CHECK(u.log.v[i].index == i && u.log.v[i].outcome != HOLR_OUT_REFUSED);
    /* label */
    CHECK(hol_replay_nlabels(u.r) == 1);
    uint64_t h;
    const char *nm;
    size_t len;
    CHECK(hol_replay_label(u.r, 0, &h, &nm, &len) && h == S_H13 && len == 13 && !memcmp(nm, "TRANS_EXAMPLE", 13));
    /* deliveries: H13 twice as two occurrences, then H15 */
    HolRef d[3];
    uint64_t ord, ev[3];
    for (int i = 0; i < 3; i++) {
        CHECK(hol_replay_next(u.r, &d[i], &ord) == HOL_OK && ord == (uint64_t)i);
        hol_thm_event(u.k, d[i], &ev[i]);
    }
    HolRef none;
    CHECK(hol_replay_next(u.r, &none, &ord) == HOL_STALE);
    bool same;
    hol_thm_same_payload(u.k, d[0], d[1], &same);
    CHECK(same && ev[0] != ev[1]);
    hol_thm_same_payload(u.k, d[0], d[2], &same);
    CHECK(!same);
    /* a dropped id is no longer available; the axiom dependency is visible */
    HolRef t;
    CHECK(hol_replay_thm(u.r, 0, &t) == HOL_STALE);
    CHECK(hol_replay_thm(u.r, S_AXUSE, &t) == HOL_OK);
    bool std = true;
    hol_thm_standard(u.k, t, &std);
    CHECK(!std);
    REF = u.log;
    u.log.v = NULL;
    run_close(&u);
}

static void test_truncation_and_prefixes(void) {
    SECTION("truncation");
    unsigned bad = 0;
    for (size_t cut = 0; cut < SAMPLE_LEN; cut++) {
        if (cut % 5 == 0) save_fixture("trunc-%d", (int)cut, 0, SAMPLE, cut);
        /* as final input: always malformed (truncated or missing END) */
        HolReason why;
        HolReplayState st = replay_bytes(SAMPLE, cut, &why);
        if (st != HOL_REPLAY_MALFORMED) bad++;
        /* as a non-final prefix: never malformed; completing it gives the
         * reference history */
        Run u;
        run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
        hol_replay_feed(u.r, SAMPLE, cut, false);
        st = hol_replay_resume(u.r, 0);
        if (st != HOL_REPLAY_NEED_INPUT) bad++;
        hol_replay_feed(u.r, SAMPLE + cut, SAMPLE_LEN - cut, true);
        st = hol_replay_resume(u.r, 0);
        if (st != HOL_REPLAY_ACCEPTED || !log_equal(&REF, &u.log, 0)) bad++;
        run_close(&u);
    }
    CHECK(bad == 0);
    printf("  truncation: %zu cut points, each rejected when final and resumable when not\n", SAMPLE_LEN);
}

static void test_bit_flips(void) {
    SECTION("bit-flips");
    uint8_t *m = (uint8_t *)malloc(SAMPLE_LEN);
    unsigned accepted = 0, total = 0;
    unsigned by_state[6] = {0};
    for (size_t i = 0; i < SAMPLE_LEN; i++)
        for (int b = 0; b < 8; b++) {
            memcpy(m, SAMPLE, SAMPLE_LEN);
            m[i] ^= (uint8_t)(1u << b);
            if ((i * 8 + (size_t)b) % 3 == 0) save_fixture("flip-%d-%d", (int)i, b, m, SAMPLE_LEN);
            HolReplayState st = replay_bytes(m, SAMPLE_LEN, NULL);
            by_state[st]++;
            if (st == HOL_REPLAY_ACCEPTED) accepted++;
            total++;
        }
    CHECK(accepted == 0); /* the END checksum covers every byte */
    printf("  bit flips: %u single-bit mutants: %u malformed, %u refused, %u accepted\n", total,
           by_state[HOL_REPLAY_MALFORMED], by_state[HOL_REPLAY_REFUSED], accepted);
    free(m);
}

static void test_garbage(void) {
    SECTION("garbage");
    uint64_t seed = 0x9e3779b97f4a7c15ULL;
    size_t hdr = 44; /* magic + sha + four one-byte varints */
    uint8_t buf[512];
    unsigned crashes_avoided = 0;
    for (int it = 0; it < 20000; it++) {
        size_t n = hdr + xorshift(&seed) % (sizeof buf - hdr);
        memcpy(buf, SAMPLE, hdr);
        for (size_t i = hdr; i < n; i++) buf[i] = (uint8_t)xorshift(&seed);
        /* bias towards valid opcodes */
        if (n > hdr && (it & 1)) buf[hdr] = (uint8_t)(xorshift(&seed) % 0x43);
        if (it % 50 == 0) save_fixture("garbage-%d", it, 0, buf, n);
        HolReplayState st = replay_bytes(buf, n, NULL);
        CHECK(st != HOL_REPLAY_ACCEPTED && st != HOL_REPLAY_RUNNING && st != HOL_REPLAY_NEED_INPUT);
        crashes_avoided++;
    }
    /* garbage spliced into the middle of the valid sample */
    for (int it = 0; it < 5000; it++) {
        size_t at = hdr + xorshift(&seed) % (SAMPLE_LEN - hdr), k = 1 + xorshift(&seed) % 6;
        uint8_t *m = (uint8_t *)malloc(SAMPLE_LEN + k);
        memcpy(m, SAMPLE, at);
        for (size_t i = 0; i < k; i++) m[at + i] = (uint8_t)xorshift(&seed);
        memcpy(m + at + k, SAMPLE + at, SAMPLE_LEN - at);
        if (it % 25 == 0) save_fixture("splice-%d", it, 0, m, SAMPLE_LEN + k);
        CHECK(replay_bytes(m, SAMPLE_LEN + k, NULL) != HOL_REPLAY_ACCEPTED);
        free(m);
    }
    printf("  garbage: %u random streams and 5000 splices rejected\n", crashes_avoided);
}

static void test_splits(void) {
    SECTION("splits");
    size_t chunks[] = {1, 2, 3, 5, 7, 16, 64, 333};
    unsigned bad = 0;
    for (size_t c = 0; c < sizeof chunks / sizeof chunks[0]; c++)
        for (uint64_t budget = 0; budget <= 3; budget++) {
            Run u;
            run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
            size_t fed = 0;
            HolReplayState st = HOL_REPLAY_NEED_INPUT;
            for (int guard = 0; guard < 1 << 20; guard++) {
                if (st == HOL_REPLAY_NEED_INPUT) {
                    size_t n = SAMPLE_LEN - fed < chunks[c] ? SAMPLE_LEN - fed : chunks[c];
                    st = hol_replay_feed(u.r, SAMPLE + fed, n, fed + n == SAMPLE_LEN);
                    fed += n;
                }
                st = hol_replay_resume(u.r, budget);
                if (st != HOL_REPLAY_RUNNING && st != HOL_REPLAY_NEED_INPUT) break;
            }
            if (st != HOL_REPLAY_ACCEPTED || !log_equal(&REF, &u.log, 0)) bad++;
            run_close(&u);
        }
    /* random splits of both */
    uint64_t seed = 12345;
    for (int it = 0; it < 300; it++) {
        Run u;
        run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
        size_t fed = 0;
        HolReplayState st = HOL_REPLAY_NEED_INPUT;
        for (int guard = 0; guard < 1 << 20; guard++) {
            if (st == HOL_REPLAY_NEED_INPUT || (xorshift(&seed) & 3) == 0) {
                if (fed < SAMPLE_LEN) {
                    size_t n = 1 + xorshift(&seed) % 40;
                    if (n > SAMPLE_LEN - fed) n = SAMPLE_LEN - fed;
                    st = hol_replay_feed(u.r, SAMPLE + fed, n, fed + n == SAMPLE_LEN);
                    fed += n;
                }
            }
            st = hol_replay_resume(u.r, xorshift(&seed) % 4);
            if (st != HOL_REPLAY_RUNNING && st != HOL_REPLAY_NEED_INPUT) break;
        }
        if (st != HOL_REPLAY_ACCEPTED || !log_equal(&REF, &u.log, 0)) bad++;
        run_close(&u);
    }
    CHECK(bad == 0);
}

static void test_resource_retry(void) {
    SECTION("resource-retry");
    /* count kernel allocations of a clean run */
    Run u;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    uint64_t a0 = hol_test_allocations(u.k);
    run_all(&u, SAMPLE, SAMPLE_LEN);
    uint64_t total = hol_test_allocations(u.k) - a0;
    run_close(&u);
    unsigned bad = 0, hit = 0;
    for (uint64_t n = 1; n <= total + 3; n++) {
        run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
        hol_test_fail_allocation(u.k, n, false);
        hol_replay_feed(u.r, SAMPLE, SAMPLE_LEN, true);
        HolReplayState st = hol_replay_resume(u.r, 0);
        if (st == HOL_REPLAY_RESOURCE) {
            hit++;
            if (hol_replay_status(u.r) != HOL_NOMEM) bad++;
            st = hol_replay_resume(u.r, 0); /* the failure was one-shot */
        }
        if (st != HOL_REPLAY_ACCEPTED || !log_equal(&REF, &u.log, 0)) bad++;
        run_close(&u);
    }
    CHECK(bad == 0);
    CHECK(hit > 20);
    /* sticky failure: stays RESOURCE, commits nothing more, then recovers */
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    hol_replay_feed(u.r, SAMPLE, SAMPLE_LEN, true);
    hol_replay_resume(u.r, 40);
    size_t before = u.log.n;
    hol_test_fail_allocation(u.k, 1, true);
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_RESOURCE);
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_RESOURCE);
    CHECK(u.log.n <= before + 40);
    hol_test_fail_allocation(u.k, 0, false);
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_ACCEPTED && log_equal(&REF, &u.log, 0));
    run_close(&u);
    /* fuel exhaustion and interrupts are retryable resource failures too;
     * they are checked every 1024 work units, so use one heavy record */
    Asm a;
    asm_init(&a, 0, HOL_AXIOMS_OPEN);
    uint64_t nA = NAME(&a, "A"), nfun = NAME(&a, "fun"), nx = NAME(&a, "x"), ny = NAME(&a, "y"),
             nf = NAME(&a, "f");
    uint64_t yA = TYVAR(&a, nA), yAA = FUN(&a, nfun, yA, yA);
    uint64_t x = T2(&a, HOLR_VAR, nx, yA), y = T2(&a, HOLR_VAR, ny, yA), f = T2(&a, HOLR_VAR, nf, yAA);
    uint64_t t = x;
    for (int i = 0; i < 5000; i++) t = T2(&a, HOLR_COMB, f, t);
    uint64_t th = H1(&a, HOLR_REFL, t);
    uint64_t pr[2] = {y, x};
    INSTR(&a, HOLR_INST, 1, pr, th);
    size_t hlen;
    uint8_t *heavy = asm_finish(&a, &hlen);
    save_fixture("heavy", 0, 0, heavy, hlen);
    Log ref2 = {0};
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    CHECK(run_all(&u, heavy, hlen) == HOL_REPLAY_ACCEPTED);
    ref2 = u.log;
    u.log.v = NULL;
    run_close(&u);
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    hol_set_fuel(u.k, 2000);
    hol_replay_feed(u.r, heavy, hlen, true);
    HolReplayState st = hol_replay_resume(u.r, 0);
    CHECK(st == HOL_REPLAY_RESOURCE && hol_replay_status(u.r) == HOL_INTERRUPTED &&
          hol_replay_reason(u.r) == HOL_R_FUEL);
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_RESOURCE); /* still not enough */
    hol_set_fuel(u.k, 0);
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_ACCEPTED && log_equal(&ref2, &u.log, 0));
    run_close(&u);
    volatile int flag = 1;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    hol_set_interrupt(u.k, &flag);
    hol_replay_feed(u.r, heavy, hlen, true);
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_RESOURCE && hol_replay_reason(u.r) == HOL_R_INTERRUPT);
    flag = 0;
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_ACCEPTED && log_equal(&ref2, &u.log, 0));
    run_close(&u);
    free(ref2.v);
    free(heavy);
    printf("  resource retry: %u injected allocation failures retried to the reference history\n", hit);
}

static void test_gc_and_collisions(void) {
    SECTION("gc-collisions");
    /* a full collection at every record boundary */
    Run u;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    hol_replay_feed(u.r, SAMPLE, SAMPLE_LEN, true);
    HolReplayState st;
    do {
        st = hol_replay_resume(u.r, 1);
        hol_gc(u.k);
    } while (st == HOL_REPLAY_RUNNING);
    CHECK(st == HOL_REPLAY_ACCEPTED && log_equal(&REF, &u.log, 0));
    HolStats s;
    hol_stats(u.k, &s);
    CHECK(s.gc_runs >= S_RECORDS);
    /* delivered references survive collections */
    HolRef d;
    uint64_t ord;
    CHECK(hol_replay_next(u.r, &d, &ord) == HOL_OK);
    hol_gc(u.k);
    uint64_t dg = 0;
    CHECK(hol_digest(u.k, d, &dg) == HOL_OK && dg == REF.v[S_H13_RECORD].digest[0]);
    run_close(&u);
    HolConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.use_hash_mask = true;
    cfg.hash_mask = 0;
    run_open(&u, &cfg, HOL_AXIOMS_OPEN, NULL);
    CHECK(run_all(&u, SAMPLE, SAMPLE_LEN) == HOL_REPLAY_ACCEPTED && log_equal(&REF, &u.log, 0));
    run_close(&u);
}

static void test_clones(void) {
    SECTION("clones");
    unsigned bad = 0;
    for (uint64_t k = 0; k <= S_RECORDS; k++) {
        Run u;
        run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
        hol_replay_feed(u.r, SAMPLE, SAMPLE_LEN, true);
        if (k) hol_replay_resume(u.r, k);
        HolReplay *c = hol_replay_clone(u.r);
        Log cl = {0};
        hol_replay_set_observer(c, log_obs, &cl);
        HolReplayState s1 = hol_replay_resume(u.r, 0), s2 = hol_replay_resume(c, 0);
        if (s1 != HOL_REPLAY_ACCEPTED || s2 != HOL_REPLAY_ACCEPTED) bad++;
        if (!log_equal(&REF, &u.log, 0) || !log_equal(&REF, &cl, (size_t)k)) bad++;
        /* the type definition theorems: shared when the checkpoint is after
         * the definition, separate incompatible branches otherwise */
        HolRef t1, t2, mixed;
        hol_replay_thm(u.r, S_TYPEDEF1, &t1);
        hol_replay_thm(c, S_TYPEDEF1, &t2);
        bool same = false;
        hol_thm_same_payload(u.k, t1, t2, &same);
        HolStatus ms = hol_DEDUCT_ANTISYM_RULE(u.k, t1, t2, &mixed);
        bool after = false;
        for (size_t i = 0; i < REF.n && i < k; i++)
            if (REF.v[i].opcode == HOLR_NEW_TYPEDEF) after = true;
        if (after ? !(same && ms == HOL_OK) : !(!same && ms == HOL_FOREIGN)) bad++;
        hol_replay_free(c);
        free(cl.v);
        run_close(&u);
    }
    CHECK(bad == 0);
}

/* A probe-mode stream with refusals and dependent failures. */
static uint8_t *probe_stream(size_t *len, uint32_t flags) {
    Asm a;
    asm_init(&a, flags, HOL_AXIOMS_OPEN);
    uint64_t nA = NAME(&a, "A"), nx = NAME(&a, "x"), ny = NAME(&a, "y");
    uint64_t yA = TYVAR(&a, nA);
    uint64_t x = T2(&a, HOLR_VAR, nx, yA), y = T2(&a, HOLR_VAR, ny, yA);
    uint64_t rx = H1(&a, HOLR_REFL, x), ry = H1(&a, HOLR_REFL, y);
    uint64_t bad = H2(&a, HOLR_TRANS, rx, ry); /* refused: x <> y */
    op(&a, HOLR_EXPECT_REFUSED);
    uint64_t dep = H2(&a, HOLR_TRANS, bad, bad); /* dependent failure */
    op(&a, HOLR_EXPECT_REFUSED);
    uint64_t ok = H2(&a, HOLR_TRANS, rx, rx);
    (void)dep;
    op(&a, HOLR_EXPECT_REFUSED); /* check fails: the last record succeeded */
    T2(&a, HOLR_COMB, x, x);     /* term refused: x is not a function */
    op(&a, HOLR_EXPECT_REFUSED);
    REC1(&a, HOLR_DROP_THM, bad); /* dropping a FAILED id is allowed */
    REC1(&a, HOLR_EMIT, ok);
    return asm_finish(&a, len);
}

static void test_probe(void) {
    SECTION("probe");
    size_t len;
    uint8_t *b = probe_stream(&len, HOLR_FLAG_PROBE);
    save_fixture("probe", 0, 0, b, len);
    Run u;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    CHECK(run_all(&u, b, len) == HOL_REPLAY_ACCEPTED);
    HolReplayInfo info;
    hol_replay_info(u.r, &info);
    CHECK(info.failed == 4 && info.expectations_checked == 4);
    int refused = 0, dep = 0, failed_checks = 0;
    for (size_t i = 0; i < u.log.n; i++) {
        if (u.log.v[i].outcome == HOLR_OUT_REFUSED) refused++;
        if (u.log.v[i].outcome == HOLR_OUT_DEPFAIL) dep++;
        if (u.log.v[i].outcome == HOLR_OUT_CHECK && u.log.v[i].status == HOL_REFUSED) failed_checks++;
    }
    CHECK(refused == 2 && dep == 1 && failed_checks == 1);
    run_close(&u);
    free(b);
    /* strict mode: absorbing at the first refusal */
    b = probe_stream(&len, 0);
    save_fixture("probe-strict", 0, 0, b, len);
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    CHECK(run_all(&u, b, len) == HOL_REPLAY_REFUSED);
    CHECK(hol_replay_status(u.r) == HOL_REFUSED && hol_replay_reason(u.r) == HOL_R_TRANS);
    hol_replay_info(u.r, &info);
    CHECK(info.records == 8); /* NAME x3, TYVAR, VAR x2, REFL x2; TRANS does not commit */
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_REFUSED);
    CHECK(hol_replay_feed(u.r, b, 1, true) == HOL_REPLAY_REFUSED);
    run_close(&u);
    free(b);
}

/* Validation order: every id is checked before anything executes, so an
 * undefined id makes a record malformed even when an earlier operand is a
 * FAILED placeholder; with no bad id, a FAILED operand gives DEPFAIL. */
static void test_validation_order(void) {
    SECTION("validation-order");
    for (int variant = 0; variant < 4; variant++) {
        Asm a;
        asm_init(&a, HOLR_FLAG_PROBE, HOL_AXIOMS_OPEN);
        uint64_t nA = NAME(&a, "A"), nx = NAME(&a, "x"), ny = NAME(&a, "y");
        uint64_t yA = TYVAR(&a, nA);
        uint64_t x = T2(&a, HOLR_VAR, nx, yA), y = T2(&a, HOLR_VAR, ny, yA);
        uint64_t rx = H1(&a, HOLR_REFL, x), ry = H1(&a, HOLR_REFL, y);
        uint64_t bad = H2(&a, HOLR_TRANS, rx, ry);
        uint64_t future = 1000;
        switch (variant) {
        case 0: H2(&a, HOLR_TRANS, bad, future); break;
        case 1: H2(&a, HOLR_TRANS, future, bad); break;
        case 2: H2(&a, HOLR_TRANS, bad, rx); break;
        case 3: REC1(&a, HOLR_DROP_THM, rx); H2(&a, HOLR_TRANS, bad, rx); break;
        }
        size_t len;
        uint8_t *b = asm_finish(&a, &len);
        save_fixture("valorder-%d", variant, 0, b, len);
        HolReason why;
        HolReplayState st = replay_bytes(b, len, &why);
        if (variant == 2) CHECK(st == HOL_REPLAY_ACCEPTED);
        else if (variant == 3) CHECK(st == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_DROPPED_REF);
        else CHECK(st == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_FUTURE_REF);
        free(b);
    }
}

static void test_end_and_header(void) {
    SECTION("end-header");
    HolReason why;
    /* trailing byte after END */
    uint8_t *m = (uint8_t *)malloc(SAMPLE_LEN + 1);
    memcpy(m, SAMPLE, SAMPLE_LEN);
    m[SAMPLE_LEN] = 0;
    save_fixture("trailing", 0, 0, m, SAMPLE_LEN + 1);
    CHECK(replay_bytes(m, SAMPLE_LEN + 1, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_TRAILING);
    /* bytes after END in a later feed */
    Run u;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    hol_replay_feed(u.r, SAMPLE, SAMPLE_LEN, false);
    CHECK(hol_replay_resume(u.r, 0) == HOL_REPLAY_ACCEPTED);
    CHECK(hol_replay_feed(u.r, m, 1, true) == HOL_REPLAY_MALFORMED &&
          hol_replay_reason(u.r) == HOL_R_STREAM_AFTER_END);
    run_close(&u);
    free(m);
    /* wrong count / checksum in END: rebuild the END record by hand */
    Asm a;
    asm_init(&a, 0, HOL_AXIOMS_OPEN);
    NAME(&a, "A");
    size_t dl;
    const uint8_t *data = hol_writer_data(a.w, &dl);
    uint8_t buf[256];
    memcpy(buf, data, dl);
    hol_writer_free(a.w);
    /* compute the proper checksum with a writer, then corrupt fields */
    asm_init(&a, 0, HOL_AXIOMS_OPEN);
    NAME(&a, "A");
    size_t full;
    uint8_t *good = asm_finish(&a, &full);
    CHECK(replay_bytes(good, full, NULL) == HOL_REPLAY_ACCEPTED);
    memcpy(buf, good, full);
    buf[dl + 1] = 2; /* record count 1 -> 2 */
    save_fixture("bad-count", 0, 0, buf, full);
    CHECK(replay_bytes(buf, full, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_COUNT);
    memcpy(buf, good, full);
    buf[full - 1] ^= 0x80; /* checksum */
    save_fixture("bad-checksum", 0, 0, buf, full);
    CHECK(replay_bytes(buf, full, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_CHECKSUM);
    /* missing END */
    save_fixture("missing-end", 0, 0, good, dl);
    CHECK(replay_bytes(good, dl, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_MISSING_END);
    /* header fields: magic, kernel id, version, profile, policy, flags */
    struct { size_t at; uint8_t val; HolReason why; } h[] = {
        {0, 'X', HOL_R_STREAM_MAGIC},   {8, 0x00, HOL_R_STREAM_HEADER},
        {40, 2, HOL_R_STREAM_VERSION},  {41, 1, HOL_R_STREAM_HEADER},
        {42, 3, HOL_R_STREAM_HEADER},   {43, 2, HOL_R_STREAM_HEADER},
    };
    for (size_t i = 0; i < sizeof h / sizeof h[0]; i++) {
        memcpy(buf, good, full);
        buf[h[i].at] = h[i].val;
        if (h[i].at != 42) save_fixture("header-%d", (int)h[i].at, 0, buf, full); /* policy: native only */
        CHECK(replay_bytes(buf, full, &why) == HOL_REPLAY_MALFORMED && why == h[i].why);
    }
    /* integers: at most 9 bytes, minimal, below 2^62 */
    memcpy(buf, good, 44);
    buf[44] = HOLR_NAME;
    for (int i = 0; i < 9; i++) buf[45 + i] = 0xff;
    save_fixture("int-%d", 0, 0, buf, 54);
    CHECK(replay_bytes(buf, 54, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_INTEGER);
    buf[45] = 0x81;
    buf[46] = 0x00; /* 1 encoded in two bytes */
    save_fixture("int-%d", 1, 0, buf, 47);
    CHECK(replay_bytes(buf, 47, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_INTEGER);
    for (int i = 0; i < 8; i++) buf[45 + i] = 0x80;
    buf[53] = 0x40; /* 2^62 */
    save_fixture("int-%d", 2, 0, buf, 54);
    CHECK(replay_bytes(buf, 54, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_INTEGER);
    buf[53] = 0x3f; /* 2^62 - 2^56: a well-formed integer, but too long a name */
    CHECK(replay_bytes(buf, 54, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_LIMIT);
    /* unknown opcode */
    memcpy(buf, good, 44);
    buf[44] = 0x50;
    save_fixture("bad-opcode", 0, 0, buf, 45);
    CHECK(replay_bytes(buf, 45, &why) == HOL_REPLAY_MALFORMED && why == HOL_R_STREAM_OPCODE);
    free(good);
}

static void test_limits(void) {
    SECTION("limits");
    HolReplayLimits lim;
    Run u;
    memset(&lim, 0, sizeof lim);
    lim.max_buffered_bytes = 100;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, &lim);
    CHECK(hol_replay_feed(u.r, SAMPLE, SAMPLE_LEN, true) == HOL_REPLAY_MALFORMED &&
          hol_replay_status(u.r) == HOL_LIMIT);
    run_close(&u);
    /* the buffer limit bounds unconsumed input only: a stream larger than
     * the limit streams through in small chunks */
    lim.max_buffered_bytes = 200;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, &lim);
    size_t fed = 0;
    HolReplayState st = HOL_REPLAY_NEED_INPUT;
    while (fed < SAMPLE_LEN && (st == HOL_REPLAY_NEED_INPUT || st == HOL_REPLAY_RUNNING)) {
        size_t n = SAMPLE_LEN - fed < 50 ? SAMPLE_LEN - fed : 50;
        st = hol_replay_feed(u.r, SAMPLE + fed, n, fed + n == SAMPLE_LEN);
        fed += n;
        if (st == HOL_REPLAY_NEED_INPUT || st == HOL_REPLAY_RUNNING) st = hol_replay_resume(u.r, 0);
    }
    CHECK(st == HOL_REPLAY_ACCEPTED && log_equal(&REF, &u.log, 0));
    run_close(&u);
    struct { int which; uint64_t v; } cases[] = {{0, 10}, {1, 3}, {2, 1}, {3, 1}};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memset(&lim, 0, sizeof lim);
        if (cases[i].which == 0) lim.max_records = cases[i].v;
        if (cases[i].which == 1) lim.max_ids = cases[i].v;
        if (cases[i].which == 2) lim.max_name_bytes = (uint32_t)cases[i].v;
        if (cases[i].which == 3) lim.max_operands = (uint32_t)cases[i].v;
        run_open(&u, NULL, HOL_AXIOMS_OPEN, &lim);
        CHECK(run_all(&u, SAMPLE, SAMPLE_LEN) == HOL_REPLAY_MALFORMED && hol_replay_reason(u.r) == HOL_R_STREAM_LIMIT);
        run_close(&u);
    }
}

static void test_policy(void) {
    SECTION("policy");
    /* the sample has a user axiom: refused under a STANDARD host policy */
    Run u;
    run_open(&u, NULL, HOL_AXIOMS_STANDARD, NULL);
    CHECK(run_all(&u, SAMPLE, SAMPLE_LEN) == HOL_REPLAY_REFUSED && hol_replay_status(u.r) == HOL_POLICY);
    run_close(&u);
    /* a stream declaring SEALED in its header */
    Asm a;
    asm_init(&a, 0, HOL_AXIOMS_SEALED);
    uint64_t nb = NAME(&a, "bool"), nq = NAME(&a, "q");
    uint64_t yb = TYAPP(&a, nb, 0, NULL);
    uint64_t q = T2(&a, HOLR_VAR, nq, yb);
    H1(&a, HOLR_NEW_AXIOM, q);
    size_t len;
    uint8_t *b = asm_finish(&a, &len);
    CHECK(replay_bytes(b, len, NULL) == HOL_REPLAY_REFUSED);
    free(b);
    /* a POLICY record cannot loosen; tightening mid-stream applies */
    asm_init(&a, HOLR_FLAG_PROBE, HOL_AXIOMS_OPEN);
    nb = NAME(&a, "bool");
    nq = NAME(&a, "q");
    yb = TYAPP(&a, nb, 0, NULL);
    q = T2(&a, HOLR_VAR, nq, yb);
    H1(&a, HOLR_NEW_AXIOM, q);       /* ok */
    REC1(&a, HOLR_POLICY, HOL_AXIOMS_SEALED);
    REC1(&a, HOLR_POLICY, HOL_AXIOMS_OPEN); /* no-op: already tighter */
    H1(&a, HOLR_NEW_AXIOM, q);       /* refused */
    op(&a, HOLR_EXPECT_REFUSED);
    b = asm_finish(&a, &len);
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    CHECK(run_all(&u, b, len) == HOL_REPLAY_ACCEPTED);
    HolReplayInfo info;
    hol_replay_info(u.r, &info);
    CHECK(info.failed == 1);
    HolRef env;
    HolAxiomPolicy p;
    hol_replay_env(u.r, &env);
    CHECK(hol_env_policy(u.k, env, &p) == HOL_OK && p == HOL_AXIOMS_SEALED);
    run_close(&u);
    free(b);
}

static void test_expect_axioms_exactness(void) {
    SECTION("expect-axioms");
    /* EXPECT_AXIOMS lists NEW_AXIOM output ids as a set: order is free,
     * repetition fails, and a theorem with the same payload as an axiom is
     * not the axiom's id. */
    for (int variant = 0; variant < 5; variant++) {
        Asm a;
        asm_init(&a, HOLR_FLAG_PROBE, HOL_AXIOMS_OPEN);
        uint64_t nb = NAME(&a, "bool"), np = NAME(&a, "p"), nq = NAME(&a, "q");
        uint64_t yb = TYAPP(&a, nb, 0, NULL);
        uint64_t p = T2(&a, HOLR_VAR, np, yb), q = T2(&a, HOLR_VAR, nq, yb);
        uint64_t ap = H1(&a, HOLR_NEW_AXIOM, p), aq = H1(&a, HOLR_NEW_AXIOM, q);
        uint64_t both = H2(&a, HOLR_DEDUCT, ap, aq); /* |- p = q from both axioms */
        uint64_t rp = H1(&a, HOLR_REFL, p);
        uint64_t again = H2(&a, HOLR_EQ_MP, rp, ap); /* |- p: same payload as ap */
        uint64_t l[2];
        switch (variant) {
        case 0: l[0] = ap; l[1] = aq; EXPECT_AXIOMS(&a, both, 2, l); break;
        case 1: l[0] = aq; l[1] = ap; EXPECT_AXIOMS(&a, both, 2, l); break;
        case 2: l[0] = ap; l[1] = ap; EXPECT_AXIOMS(&a, both, 2, l); break;
        case 3: l[0] = again; EXPECT_AXIOMS(&a, again, 1, l); break;
        case 4: l[0] = ap; EXPECT_AXIOMS(&a, again, 1, l); break;
        }
        size_t len;
        uint8_t *b = asm_finish(&a, &len);
        save_fixture("axioms-%d", variant, 0, b, len);
        Run u;
        run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
        CHECK(run_all(&u, b, len) == HOL_REPLAY_ACCEPTED);
        HolReplayInfo info;
        hol_replay_info(u.r, &info);
        bool expect_ok = variant == 0 || variant == 1 || variant == 4;
        CHECK(info.failed == (expect_ok ? 0u : 1u));
        run_close(&u);
        free(b);
    }
}

static void test_streaming_memory(void) {
    SECTION("streaming");
    /* replay keeps only unconsumed bytes: offset keeps counting */
    Run u;
    run_open(&u, NULL, HOL_AXIOMS_OPEN, NULL);
    for (size_t fed = 0; fed < SAMPLE_LEN; fed += 10) {
        size_t n = SAMPLE_LEN - fed < 10 ? SAMPLE_LEN - fed : 10;
        hol_replay_feed(u.r, SAMPLE + fed, n, fed + n == SAMPLE_LEN);
        hol_replay_resume(u.r, 0);
    }
    HolReplayInfo info;
    hol_replay_info(u.r, &info);
    CHECK(hol_replay_state(u.r) == HOL_REPLAY_ACCEPTED && info.offset == SAMPLE_LEN);
    run_close(&u);
}

int main(void) {
    SAMPLE = sample_stream(&SAMPLE_LEN);
    printf("  sample stream: %zu bytes, %llu records\n", SAMPLE_LEN, (unsigned long long)S_RECORDS);
    save_fixture("sample", 0, 0, SAMPLE, SAMPLE_LEN);
    test_reference();
    test_truncation_and_prefixes();
    test_bit_flips();
    test_garbage();
    test_splits();
    test_resource_retry();
    test_gc_and_collisions();
    test_clones();
    test_probe();
    test_validation_order();
    test_end_and_header();
    test_limits();
    test_policy();
    test_expect_axioms_exactness();
    test_streaming_memory();
    free(SAMPLE);
    free(REF.v);
    return test_summary("test-stream");
}
