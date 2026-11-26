/*
 * Threaded rho runs (src/rhocalc_core.c): memory held by task regions, in
 * the manner of region-based memory management (Tofte and Talpin), so a
 * run holds what its live tasks need; continuations placed by their
 * parties, so positions and names do not depend on scheduling; and names
 * drawn from shares in the style of interval tree clocks (Almeida, Baquero
 * and Fonte 2008), so no name is drawn twice.
 *
 *   Memory.  The duplicator loop x!(D(x)) | D(x) run for 10^3 to 10^6 COMMs
 *   on 1, 2 and 4 workers ends with its two processes and what their
 *   regions hold, the same bytes whatever the number of COMMs, with no more
 *   regions made, no more scratch, and a heap and a peak RSS that a run of
 *   10^6 COMMs leaves where one of 10^5 left them.  A
 *   burst of 4000 live tasks before the loop makes a region for each, and
 *   once the live tasks fall the run gives them back.
 *   Placement.  A spawn's components draw from the balanced pieces of the
 *   spawner's supply and a COMM's continuation from the join of its
 *   parties'; on two loops side by side, on one channel and on two, and on
 *   two relays whose every task draws a name, the sorted positions and
 *   names are the same on every run and any number of workers.  Placing
 *   continuations by the party that found the match
 *   instead (a test hook) changes them.  A spawn of 640 components lies
 *   within 11 halvings, and 320 COMMs among them on one channel join their
 *   parties' pieces in the word: no block is made.
 *   Names.  Every task of the duplicator loop drawing a name for 10^6 COMMs
 *   draws in the root block, no name twice, and the supply that ran the
 *   loop gets its place back at the latest clock.
 */
#define CETTA_TEST_HOOKS 1
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "atom.h"
#include "rhocalc_core.h"
#include "rhocalc_syntax.h"
#include "symbol.h"

/* A sanitizer keeps memory of its own: AddressSanitizer quarantines what is
   freed, and ThreadSanitizer shadows what is touched and keeps state for
   each thread.  Under one the peak RSS measures the sanitizer, not the run,
   so it is checked in builds without one; the census checks hold in every
   build. */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define RSS_MEASURES_THE_RUN 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define RSS_MEASURES_THE_RUN 0
#endif
#endif
#ifndef RSS_MEASURES_THE_RUN
#define RSS_MEASURES_THE_RUN 1
#endif

static int g_failures;

#define CHECK(condition, ...)                                         \
    do {                                                              \
        if (!(condition)) {                                           \
            g_failures++;                                             \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                    \
            fputc('\n', stderr);                                      \
        }                                                             \
    } while (0)

static Arena g_arena;

static Atom *parse(const char *text) {
    Atom **atoms = NULL;
    int n = rhocalc_parse_text(text, RHOCALC_SEMANTIC_PROFILE_STRICT_CORE,
                               CETTA_SYNTAX_MRHO, &g_arena, &atoms);
    Atom *atom = n == 1 ? atoms[0] : NULL;
    free(atoms);
    if (!atom)
        fprintf(stderr, "FAIL: could not parse %s: %s\n", text,
                rhocalc_last_parse_error() ? rhocalc_last_parse_error() : "");
    return atom;
}

static bool run(Atom *proc, uint32_t threads, uint32_t limit,
                RhoReductionResult *out) {
    RhoRuntimeProfile profile = {
        .scheduler_policy = RUN_SCHEDULE_CANONICAL,
        .reduction_limit = limit,
        .thread_count = threads,
        .threaded = true,
    };
    *out = (RhoReductionResult){0};
    bool ok = rhocalc_reduce_to_quiescence(
        &g_arena, proc, RHOCALC_SEMANTIC_PROFILE_STRICT_CORE, &profile, NULL, out);
    if (!ok)
        fprintf(stderr, "run failed: %s\n",
                rhocalc_last_validation_error() ? rhocalc_last_validation_error() : "");
    return ok;
}

/* D(x) = for (y <- x) (*y | x!(*y)) on X, and the loop x!(D(x)) | D(x). */
static char *loop_on(const char *x) {
    char dup[512], *loop = malloc(1200);
    snprintf(dup, sizeof(dup),
             "(rho:recv %s $y (rho:par (rho:drop $y) (rho:send %s (rho:drop $y))))",
             x, x);
    snprintf(loop, 1200, "(rho:par %s (rho:send %s %s))", dup, x, dup);
    return loop;
}

/* ── What a run leaves ───────────────────────────────────────────────── */

/* An entry: "live", a position (a numeral) and "next" a spelling, or
   "name" and a spelling. */
#define ENTRY_MAX (CETTA_GENERATED_NAME_NUMERAL_MAX + CETTA_GENERATED_NAME_SPELLING_MAX + 16u)

typedef struct {
    char text[ENTRY_MAX];
} Entry;

typedef struct {
    Entry *items;
    size_t len, cap;
    pthread_mutex_t mutex;
    /* Draws into names, with an index. */
    CettaGeneratedName *names;
    _Atomic size_t names_len;
    size_t names_cap;
    _Atomic size_t blocked;   /* drawn names beyond the root block */
    size_t live_blocked;      /* live tasks beyond the root block */
    unsigned live_deepest;    /* the deepest live task's interval */
} Observed;

static void observed_add(Observed *o, const char *text) {
    pthread_mutex_lock(&o->mutex);
    if (o->len == o->cap) {
        o->cap = o->cap ? 2u * o->cap : 64u;
        o->items = realloc(o->items, o->cap * sizeof(*o->items));
    }
    snprintf(o->items[o->len++].text, sizeof(o->items[0].text), "%s", text);
    pthread_mutex_unlock(&o->mutex);
}

static void spell_name(CettaGeneratedName name, char *out) {
    if (!cetta_generated_name_spell(CETTA_NAME_KIND_SPACE, name, out,
                                    CETTA_GENERATED_NAME_SPELLING_MAX))
        strcpy(out, "<unspelled>");
}

/* A position: the share of a supply, behind its block's key. */
static void spell_position(const CettaNameSupply *supply, char *out) {
    size_t length = 0u;
    if (supply->block) {
        length = (size_t)sprintf(out, "%016llx%016llx.",
                                 (unsigned long long)supply->block->key_hi,
                                 (unsigned long long)supply->block->key_lo);
    }
    size_t share = cetta_name_share_spell(supply->share, out + length);
    if (share == 0u)
        strcpy(out + length, "<none>");
    else
        out[length + share] = '\0';
}

static void on_drawn(void *context, CettaGeneratedName name) {
    Observed *o = context;
    if (cetta_generated_name_block(name))
        atomic_fetch_add_explicit(&o->blocked, 1u, memory_order_relaxed);
    if (o->names) {
        size_t at = atomic_fetch_add_explicit(&o->names_len, 1u,
                                              memory_order_relaxed);
        if (at < o->names_cap)
            o->names[at] = name;
    } else {
        char text[CETTA_GENERATED_NAME_SPELLING_MAX];
        spell_name(name, text);
        char entry[ENTRY_MAX];
        snprintf(entry, sizeof(entry), "name %s", text);
        observed_add(o, entry);
    }
}

static void on_live(void *context, const CettaNameSupply *supply, Atom *proc) {
    (void)proc;
    Observed *o = context;
    if (supply->block)
        o->live_blocked++;
    else if (!(supply->share & CETTA_NAME_SHARE_CODED) && supply->share &&
             63u - (unsigned)__builtin_clzll(supply->share) > o->live_deepest)
        o->live_deepest = 63u - (unsigned)__builtin_clzll(supply->share);
    char position[CETTA_GENERATED_NAME_NUMERAL_MAX], next[CETTA_GENERATED_NAME_SPELLING_MAX];
    spell_position(supply, position);
    /* The name of the task's place and clock: its next counted name. */
    CettaGeneratedName name = {supply->share,
                               supply->drawn[CETTA_NAME_KIND_SPACE] + 1u,
                               (uintptr_t)supply->block | CETTA_NAME_FORM_COUNTED};
    spell_name(name, next);
    char entry[ENTRY_MAX];
    snprintf(entry, sizeof(entry), "live %s next %s", position, next);
    observed_add(o, entry);
}

static int compare_entries(const void *left, const void *right) {
    return strcmp(((const Entry *)left)->text, ((const Entry *)right)->text);
}

/* The sorted multiset of what a run leaves, as one text. */
static char *outcome_of(Observed *o) {
    qsort(o->items, o->len, sizeof(*o->items), compare_entries);
    size_t size = 1u;
    for (size_t i = 0u; i < o->len; i++)
        size += strlen(o->items[i].text) + 1u;
    char *text = malloc(size), *at = text;
    for (size_t i = 0u; i < o->len; i++)
        at += sprintf(at, "%s\n", o->items[i].text);
    *at = '\0';
    return text;
}

static void observed_reset(Observed *o) {
    o->len = 0u;
    o->live_blocked = 0u;
    o->live_deepest = 0u;
    atomic_store(&o->names_len, 0u);
    atomic_store(&o->blocked, 0u);
}

/* One run of PROC watched, from a root supply of its own, as a run of the
   program has: its outcome as text, NULL when it failed. */
static char *watched_run(Atom *proc, uint32_t threads, uint32_t limit,
                         bool finder_placement, bool draws, Observed *o) {
    observed_reset(o);
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    CettaNameSupply *previous = cetta_name_supply_install(&root);
    RhocalcThreadedTestHooks hooks = {
        .finder_placement = finder_placement,
        .draw_each_task = draws,
        .drawn = on_drawn,
        .live = on_live,
        .context = o,
    };
    rhocalc_threaded_test_hooks_set(&hooks);
    RhoReductionResult result;
    bool ok = run(proc, threads, limit, &result);
    rhocalc_threaded_test_hooks_set(NULL);
    cetta_name_supply_install(previous);
    cetta_name_supply_dispose(&root);
    return ok ? outcome_of(o) : NULL;
}

static size_t heap_in_use(void) {
#if defined(__GLIBC__)
    return mallinfo2().uordblks;
#else
    return 0u;
#endif
}

static long peak_rss_kib(void) {
    struct rusage usage;
    return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss : 0;
}

/* ── Memory ──────────────────────────────────────────────────────────── */

static void test_memory(Atom *loop) {
    static const uint32_t workers[3] = {1u, 2u, 4u};
    static const uint32_t limits[4] = {1000u, 10000u, 100000u, 1000000u};
    RhocalcThreadedCensus census[3][4];
    long rss_small = 0, rss_large = 0;
    size_t heap_small = 0u, heap_large = 0u;
    for (unsigned w = 0u; w < 3u; w++) {
        for (unsigned l = 0u; l < 4u; l++) {
            /* The heap and the peak RSS before and after the last run, 10^5
               and then 10^6 COMMs on 4 workers: the same threads, their
               allocator caches warm, so the difference is what the COMMs
               add.  (A persistent worker's cache keeps some chunks it
               freed, which the heap counts as in use.) */
            if (l == 3u && w == 2u) {
                rss_small = peak_rss_kib();
                heap_small = heap_in_use();
            }
            RhoReductionResult result;
            bool ok = run(loop, workers[w], limits[l], &result);
            rhocalc_threaded_last_census(&census[w][l]);
            CHECK(ok && result.status == RHOCALC_REDUCTION_LIMIT_EXHAUSTED &&
                      result.reductions_taken == limits[l],
                  "the loop runs %u COMMs on %u workers", limits[l], workers[w]);
        }
    }
    rss_large = peak_rss_kib();
    heap_large = heap_in_use();
    bool live_ok = true, bytes_ok = true, regions_ok = true, scratch_ok = true;
    for (unsigned w = 0u; w < 3u; w++)
        for (unsigned l = 0u; l < 4u; l++) {
            const RhocalcThreadedCensus *c = &census[w][l];
            live_ok = live_ok && c->live_tasks == 2u;
            bytes_ok = bytes_ok && c->live_task_bytes == census[0][0].live_task_bytes;
            /* Regions are made for the tasks live at once (three, between
               a COMM and its spawn) and the regions kept at hand (16 by
               each worker, and a stock's worth in the run's pool), never
               per COMM. */
            uint64_t need = 3u + 16u * (workers[w] + 1u);
            regions_ok = regions_ok &&
                         c->regions_made - c->regions_given_back <= need &&
                         c->regions_made <= 2u * need;
            scratch_ok = scratch_ok && c->scratch_reserved == census[w][0].scratch_reserved;
        }
    CHECK(live_ok, "every run ends with the loop's two processes");
    CHECK(bytes_ok, "their regions hold %llu bytes after every run, whatever "
                    "the COMMs and workers", (unsigned long long)census[0][0].live_task_bytes);
    CHECK(regions_ok, "no run makes or holds more regions than its live "
                      "tasks and stocks need (%llu at 10^3, %llu at 10^6 on "
                      "4 workers)",
          (unsigned long long)census[2][0].regions_made,
          (unsigned long long)census[2][3].regions_made);
    CHECK(scratch_ok, "the workers' scratch does not grow with the COMMs");
    CHECK(heap_large <= heap_small + 64u * 1024u,
          "a run of 10^6 COMMs leaves the heap where 10^5 left it (%zu -> "
          "%zu bytes)", heap_small, heap_large);
    CHECK(!RSS_MEASURES_THE_RUN || rss_large <= rss_small + 4096,
          "a run of 10^6 COMMs leaves the peak RSS where 10^5 left it "
          "(%ld -> %ld KiB)", rss_small, rss_large);
    CHECK(cetta_name_supply_live_blocks() == 0u,
          "the loop's supplies never leave the root block");
    printf("PASS: the duplicator loop on 1, 2 and 4 workers, 10^3 to 10^6 "
           "COMMs: it ends with 2 tasks holding %llu bytes in their regions "
           "(%llu reserved), %llu-%llu regions made, %llu scratch bytes on 4 "
           "workers; from 10^5 to 10^6 COMMs on 4 workers heap %zu -> %zu "
           "bytes, peak RSS %ld -> %ld KiB\n",
           (unsigned long long)census[0][0].live_task_bytes,
           (unsigned long long)census[0][0].live_task_reserved,
           (unsigned long long)census[0][0].regions_made,
           (unsigned long long)census[2][3].regions_made,
           (unsigned long long)census[2][3].scratch_reserved,
           heap_small, heap_large, rss_small, rss_large);
}

/* A burst of 2000 sends and 2000 receives on one channel beside the loop:
   4000 tasks live at once, each in a region of its own, then two.  The run
   ends holding only what the loop's live tasks, the workers' stocks and a
   pool that can refill them need: the burst's regions were given back once
   no task needed them. */
static void test_memory_peak_falls(void) {
    static const uint32_t workers[3] = {1u, 2u, 4u};
    enum { BURST = 2000 };
    const char *dup =
        "(rho:recv (rho:quote rho:nil) $y (rho:par (rho:drop $y) "
        "(rho:send (rho:quote rho:nil) (rho:drop $y))))";
    size_t cap = 64u + BURST * 48u + 3u * strlen(dup);
    char *text = malloc(cap);
    size_t len = (size_t)snprintf(text, cap, "(rho:par");
    for (unsigned i = 0u; i < BURST; i++)
        len += (size_t)snprintf(text + len, cap - len,
                                " (rho:send $b rho:nil) (rho:recv $b $x rho:nil)");
    snprintf(text + len, cap - len, " %s (rho:send (rho:quote rho:nil) %s))",
             dup, dup);
    Atom *proc = parse(text);
    free(text);
    if (!proc)
        return;
    bool ok = true, made_ok = true, held_ok = true;
    RhocalcThreadedCensus census[3];
    for (unsigned w = 0u; w < 3u; w++) {
        RhoReductionResult result;
        ok = ok && run(proc, workers[w], 100000u, &result) &&
             result.reductions_taken == 100000u;
        rhocalc_threaded_last_census(&census[w]);
        const RhocalcThreadedCensus *c = &census[w];
        made_ok = made_ok && c->regions_made >= 2u * BURST && c->live_tasks == 2u;
        held_ok = held_ok && c->regions_made - c->regions_given_back <=
                                 3u + 16u * (2u * workers[w] + 1u);
    }
    CHECK(ok, "the burst and the loop run 10^5 COMMs on 1, 2 and 4 workers");
    CHECK(made_ok, "the burst's 4000 tasks, live at once, each get a region "
                   "(%llu made on 4 workers)",
          (unsigned long long)census[2].regions_made);
    CHECK(held_ok, "the run ends holding what the loop's live tasks and the "
                   "stocks need, the burst's regions given back (%llu of %llu "
                   "on 4 workers)",
          (unsigned long long)census[2].regions_given_back,
          (unsigned long long)census[2].regions_made);
    printf("PASS: a burst of %u live tasks before the loop: %llu-%llu regions "
           "made, all but %llu-%llu given back once the live tasks fell\n",
           2u * BURST, (unsigned long long)census[0].regions_made,
           (unsigned long long)census[2].regions_made,
           (unsigned long long)(census[0].regions_made - census[0].regions_given_back),
           (unsigned long long)(census[2].regions_made - census[2].regions_given_back));
}

/* ── Placement ───────────────────────────────────────────────────────── */

/* Spawn pieces and the join of a COMM's parties, by hand: the first task
   takes the right half of the caller's supply (word 3). */
static void test_placement_by_hand(Observed *o) {
    Atom *three = parse("(rho:par (rho:drop $u) (rho:drop $v) (rho:drop $w))");
    char *outcome = three ? watched_run(three, 2u, 1000u, false, false, o) : NULL;
    /* The balanced pieces of 3: the first two share the left half, the
       third takes the right half. */
    CHECK(outcome && strstr(outcome, "live 2022100 ") &&
              strstr(outcome, "live 2022010 ") &&
              strstr(outcome, "live 20201 "),
          "a spawn's three components draw from the balanced pieces of its "
          "supply: %s", outcome ? outcome : "(failed)");
    free(outcome);
    Atom *comm = parse("(rho:par (rho:send $x rho:nil) (rho:recv $x $y (rho:drop $z)))");
    char *joined = comm ? watched_run(comm, 2u, 1000u, false, false, o) : NULL;
    CHECK(joined && strcmp(joined, "live 201 next &space(201.1)\n") == 0,
          "a COMM's continuation draws from the join of its parties' "
          "supplies, the share they were split from: %s",
          joined ? joined : "(failed)");
    free(joined);
    bool found_other = false;
    for (unsigned i = 0u; comm && i < 8u; i++) {
        char *by_finder = watched_run(comm, 2u, 1000u, true, false, o);
        found_other = found_other ||
                      (by_finder && strcmp(by_finder, "live 201 next &space(201.1)\n") != 0);
        free(by_finder);
    }
    CHECK(found_other, "placed by the finder, the continuation draws "
                       "elsewhere (the negative control)");
}

typedef struct {
    const char *label;
    Atom *proc;
    uint32_t limit;
    bool draws;
} PlacementCase;

/* Runs of a program on 1, 2 and 4 workers, RUNS times each: canonical
   placement leaves one outcome; placement by the finder leaves another,
   and usually several. */
static void test_placement_case(const PlacementCase *c, Observed *o) {
    enum { RUNS = 12 };
    static const uint32_t workers[3] = {1u, 2u, 4u};
    char *canonical = NULL;
    bool same = true;
    size_t runs = 0u;
    for (unsigned w = 0u; w < 3u; w++)
        for (unsigned r = 0u; r < RUNS; r++) {
            char *outcome = watched_run(c->proc, workers[w], c->limit, false,
                                        c->draws, o);
            runs++;
            if (!outcome) { same = false; continue; }
            if (!canonical) canonical = outcome;
            else { same = same && strcmp(canonical, outcome) == 0; free(outcome); }
        }
    size_t lines = 0u;
    for (const char *p = canonical; p && *p; p++) lines += *p == '\n';
    CHECK(same && canonical, "%s: %zu runs on 1, 2 and 4 workers leave the "
                             "same sorted positions and names", c->label, runs);
    /* The negative control. */
    char *first_finder = NULL;
    bool differs_from_canonical = false, finder_varies = false;
    for (unsigned w = 0u; w < 3u; w++)
        for (unsigned r = 0u; r < RUNS; r++) {
            char *outcome = watched_run(c->proc, workers[w], c->limit, true,
                                        c->draws, o);
            if (!outcome) continue;
            differs_from_canonical = differs_from_canonical ||
                                     (canonical && strcmp(canonical, outcome) != 0);
            if (!first_finder) first_finder = outcome;
            else { finder_varies = finder_varies || strcmp(first_finder, outcome) != 0; free(outcome); }
        }
    CHECK(differs_from_canonical, "%s: placed by the finder, positions and "
                                  "names differ (the negative control)", c->label);
    printf("PASS: %s: %zu runs, one outcome of %zu entries; placed by the "
           "finder the outcome differs%s\n",
           c->label, runs, lines,
           finder_varies ? " and changes from run to run" : "");
    free(first_finder);
    free(canonical);
}

/* Two relays of LENGTH COMMs side by side, on channels a0.. and b0..: each
   channel has one send and one receive, so every run has the same COMMs. */
static char *two_relays(unsigned length) {
    size_t size = 256u + 2u * length * 96u;
    char *text = malloc(size), *at = text;
    at += sprintf(at, "(rho:par");
    for (unsigned chain = 0u; chain < 2u; chain++) {
        char p = chain ? 'b' : 'a';
        at += sprintf(at, " (rho:send $%c0 rho:nil)", p);
        for (unsigned i = 0u; i < length; i++)
            at += sprintf(at, " (rho:recv $%c%u $x (rho:send $%c%u (rho:drop $x)))",
                          p, i, p, i + 1u);
    }
    sprintf(at, ")");
    return text;
}

/* A spawn as wide as hot_channel's, of stuck processes: every piece within
   the first task's half and 10 halvings, in the root block; and 320
   sends meeting 320 receives on one channel
   among 640 components: every continuation draws from the union of two
   pieces, which fits the word, so no block is made. */
static void test_wide_spawn(Observed *o) {
    enum { WIDE = 640u };
    char *text = malloc(64u * WIDE + 64u), *at = text;
    at += sprintf(at, "(rho:par");
    for (unsigned i = 0u; i < WIDE; i++)
        at += sprintf(at, " (rho:drop $v%u)", i);
    sprintf(at, ")");
    Atom *wide = parse(text);
    char *outcome = wide ? watched_run(wide, 4u, 1000u, false, false, o) : NULL;
    size_t lines = 0u;
    for (const char *p = outcome; p && *p; p++) lines += *p == '\n';
    CHECK(outcome && lines == WIDE && o->live_blocked == 0u &&
              o->live_deepest <= 11u && cetta_name_supply_live_blocks() == 0u,
          "a spawn of 640 lies in the root block within 11 halvings (%zu "
          "tasks, %zu beyond the root block, the deepest %u)",
          lines, o->live_blocked, o->live_deepest);
    unsigned deepest = o->live_deepest;
    free(outcome);
    at = text;
    at += sprintf(at, "(rho:par");
    for (unsigned i = 0u; i < WIDE / 2u; i++)
        at += sprintf(at, " (rho:recv $hot $x (rho:send $out%u (rho:drop $x)))"
                          " (rho:send $hot (rho:send $p%u rho:nil))", i, i);
    sprintf(at, ")");
    Atom *hot = parse(text);
    bool same = true;
    size_t comm_lines = 0u, blocked = 0u;
    for (unsigned w = 1u; hot && w <= 4u; w *= 2u) {
        char *comms = watched_run(hot, w, 1000000u, false, false, o);
        size_t n = 0u;
        for (const char *p = comms; p && *p; p++) n += *p == '\n';
        same = same && comms && n == WIDE / 2u;
        comm_lines = n;
        blocked += o->live_blocked;
        free(comms);
    }
    CHECK(same && blocked == 0u && cetta_name_supply_live_blocks() == 0u,
          "320 COMMs among a spawn of 640 join their parties' pieces in the "
          "word: %zu continuations, %zu beyond the root block", comm_lines,
          blocked);
    printf("PASS: a spawn of 640 components lies in the root block within %u "
           "halvings, and its 320 COMMs on one channel make no block\n",
           deepest);
    free(text);
}

static void test_placement(Observed *o) {
    test_placement_by_hand(o);
    test_wide_spawn(o);
    char *one = loop_on("(rho:quote rho:nil)");
    char *both = malloc(2600);
    snprintf(both, 2600, "(rho:par %s %s)", one, one);
    char *a = loop_on("$a"), *b = loop_on("$b");
    char *apart = malloc(2600);
    snprintf(apart, 2600, "(rho:par %s %s)", a, b);
    char *relays = two_relays(40u);
    PlacementCase cases[] = {
        {"two loops on one channel", parse(both), 2000u, false},
        {"two loops on two channels", parse(apart), 2000u, false},
        {"two relays of 40 COMMs, every task drawing a name", parse(relays),
         1000000u, true},
    };
    for (unsigned i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++)
        if (cases[i].proc)
            test_placement_case(&cases[i], o);
    free(one); free(both); free(a); free(b); free(apart); free(relays);
}

/* ── Names ───────────────────────────────────────────────────────────── */

static int compare_names(const void *left, const void *right) {
    const CettaGeneratedName *a = left, *b = right;
    if (a->tag != b->tag) return a->tag < b->tag ? -1 : 1;
    if (a->hi != b->hi) return a->hi < b->hi ? -1 : 1;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return 0;
}

static void test_names(Atom *loop, Observed *o) {
    enum { COMMS = 1000000u };
    o->names_cap = 3u * COMMS + 8u;
    o->names = malloc(o->names_cap * sizeof(*o->names));
    CettaNameSupply *root = cetta_name_supply_current();
    uint64_t before = root->drawn[CETTA_NAME_KIND_SPACE];
    observed_reset(o);
    RhocalcThreadedTestHooks hooks = {
        .draw_each_task = true, .drawn = on_drawn, .live = on_live, .context = o,
    };
    rhocalc_threaded_test_hooks_set(&hooks);
    RhoReductionResult result;
    char *outcome = run(loop, 4u, COMMS, &result) ? outcome_of(o) : NULL;
    rhocalc_threaded_test_hooks_set(NULL);
    size_t count = atomic_load(&o->names_len);
    CHECK(outcome && count >= 3u * COMMS && count <= o->names_cap,
          "every task of the loop draws a name (%zu)", count);
    CHECK(atomic_load(&o->blocked) == 0u && cetta_name_supply_live_blocks() == 0u,
          "every name of 10^6 COMMs is in the root block: no block was made");
    qsort(o->names, count, sizeof(*o->names), compare_names);
    size_t repeats = 0u;
    for (size_t i = 1u; i < count; i++)
        repeats += compare_names(&o->names[i - 1u], &o->names[i]) == 0;
    CHECK(repeats == 0u, "no name of the loop repeats (%zu of %zu)", repeats, count);
    CHECK(root->share == CETTA_NAME_SHARE_WHOLE && root->block == NULL &&
              root->drawn[CETTA_NAME_KIND_SPACE] > before,
          "the supply that ran the loop has its place back, at a later clock");
    CettaGeneratedName after = cetta_name_fresh(CETTA_NAME_KIND_SPACE);
    bool fresh = bsearch(&after, o->names, count, sizeof(*o->names),
                         compare_names) == NULL;
    CHECK(fresh, "and its next name is none of the loop's");
    printf("PASS: names on the duplicator loop: %zu names of 10^6 COMMs on 4 "
           "workers, all in the root block, none twice; the caller's supply "
           "goes on at clock %llu\n",
           count, (unsigned long long)root->drawn[CETTA_NAME_KIND_SPACE]);
    free(outcome);
    free(o->names);
    o->names = NULL;
    o->names_cap = 0u;
}

int main(void) {
    SymbolTable symbols;
    VarInternTable vars;
    HashConsTable hashcons;
    char error[200];
    if (!cetta_name_supply_configure(NULL, NULL, error, sizeof(error))) {
        fprintf(stderr, "FAIL: the environment's name mode reads: %s\n", error);
        return 1;
    }
    arena_init(&g_arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&vars);
    hashcons_init(&hashcons);
    g_symbols = &symbols;
    g_var_intern = &vars;
    g_hashcons = &hashcons;
    arena_set_hashcons(&g_arena, &hashcons);

    Observed observed = {0};
    pthread_mutex_init(&observed.mutex, NULL);
    char *loop_text = loop_on("(rho:quote rho:nil)");
    Atom *loop = parse(loop_text);
    if (loop) {
        test_memory(loop);
        test_memory_peak_falls();
        test_placement(&observed);
        test_names(loop, &observed);
    }
    free(loop_text);
    free(observed.items);
    pthread_mutex_destroy(&observed.mutex);
    if (g_failures) {
        fprintf(stderr, "FAIL: %d threaded rho checks\n", g_failures);
        return 1;
    }
    printf("PASS: threaded rho runs hold what their live tasks need, place "
           "tasks by their parties, and draw exact names\n");
    return 0;
}
