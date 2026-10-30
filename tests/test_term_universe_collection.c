#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "parser.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

/* Reclamation of a term universe, against a reachability oracle.
 *
 * Each round makes new terms over the live ones, chooses roots, lets a
 * counted holder take and drop references, and collects.  The collector
 * must find live exactly the entries a plain recursive walk from the roots
 * and the holder's ids reaches.  Every live entry must keep its term, read
 * back through its record wherever the record now lies, and a lookup of
 * that term must still find it.  Every reclaimed term must be gone from
 * every lookup, and storing it again must give an equal live term, under
 * any id: freed slots are reused.  The build checks every collection
 * besides: it recounts the references and traces from the roots. */

typedef struct {
    AtomId *ids;
    size_t len, cap;
} TestIds;

static void test_ids_push(TestIds *ids, AtomId id) {
    if (ids->len == ids->cap) {
        ids->cap = ids->cap ? ids->cap * 2u : 64u;
        ids->ids = realloc(ids->ids, ids->cap * sizeof(*ids->ids));
        assert(ids->ids);
    }
    ids->ids[ids->len++] = id;
}

static void test_list_roots(const TermUniverse *universe,
                            TermUniverseRootVisit visit,
                            void *visit_context, void *ctx) {
    const TestIds *roots = ctx;
    (void)universe;
    for (size_t i = 0u; i < roots->len; i++)
        visit(visit_context, roots->ids[i]);
}

/* A counted holder, as a space is: a reference a held id, taken and dropped
 * through the universe, and its ids listed for the check. */
typedef struct {
    TermUniverse *universe;
    TestIds ids;
} TestHolder;

static void test_holder_take(TestHolder *holder, AtomId id) {
    test_ids_push(&holder->ids, id);
    term_universe_hold(holder->universe, id);
}

static void test_holder_drop(TestHolder *holder, size_t at) {
    term_universe_release(holder->universe, holder->ids.ids[at]);
    holder->ids.ids[at] = holder->ids.ids[--holder->ids.len];
}

static void test_list_held(const TermUniverse *universe,
                           TermUniverseRootVisit visit, void *visit_context,
                           void *ctx) {
    test_list_roots(universe, visit, visit_context, &((TestHolder *)ctx)->ids);
}

static uint64_t g_rng = UINT64_C(0x9e3779b97f4a7c15);

static uint64_t test_rand(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static size_t test_below(size_t bound) {
    return (size_t)(test_rand() % bound);
}

static void init_test_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

/* The oracle: the entries a recursive walk from the roots reaches. */
static void oracle_reach(const TermUniverse *universe, AtomId id,
                         bool *reached) {
    if (id == CETTA_ATOM_ID_NONE || reached[id])
        return;
    reached[id] = true;
    switch (tu_kind(universe, id)) {
    case ATOM_EXPR:
        for (CettaExprIndex i = 0u; i < tu_arity(universe, id); i++)
            oracle_reach(universe, tu_child(universe, id, i), reached);
        break;
    case ATOM_VAR:
        oracle_reach(universe, tu_var_name_key_id(universe, id), reached);
        break;
    default:
        break;
    }
}

/* The term ID names, read through its record, equals TERM. */
static bool record_equals(const TermUniverse *universe, AtomId id,
                          const Atom *term) {
    if (!term || tu_kind(universe, id) != term->kind)
        return false;
    switch (term->kind) {
    case ATOM_SYMBOL:
        return tu_sym(universe, id) == term->sym_id;
    case ATOM_VAR:
        return var_id_eq(tu_var_id(universe, id), term->var_id);
    case ATOM_GROUNDED:
        return term->ground.gkind == GV_INT &&
               tu_ground_kind(universe, id) == GV_INT &&
               tu_int(universe, id) == term->ground.ival;
    case ATOM_EXPR:
        if (tu_arity(universe, id) != term->expr.len)
            return false;
        for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
            if (!record_equals(universe, tu_child(universe, id, i),
                               term->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

/* New terms over the live ones: symbols, integers, variables, a named
 * variable now and then, and expressions of up to four children. */
static void make_terms(TermUniverse *universe, TestIds *live,
                       const SymbolId *heads, size_t head_count,
                       size_t count) {
    for (size_t made = 0u; made < count; made++) {
        AtomId id = CETTA_ATOM_ID_NONE;
        size_t kind = test_below(10u);
        if (kind == 0u) {
            id = tu_intern_symbol(universe, heads[test_below(head_count)]);
        } else if (kind <= 2u) {
            id = tu_intern_int(universe, (int64_t)test_below(2000u));
        } else if (kind == 3u) {
            id = tu_intern_var(universe, heads[test_below(head_count)],
                               var_id_from_base(1u + test_below(40u)));
        } else if (kind == 4u) {
            AtomId key = tu_intern_symbol(
                universe, heads[test_below(head_count)]);
            id = tu_intern_named_var(universe, key,
                                     var_id_from_base(1u + test_below(40u)));
        } else if (live->len != 0u) {
            AtomId children[5];
            CettaExprLen arity = 1u + test_below(4u);
            children[0] = tu_intern_symbol(
                universe, heads[test_below(head_count)]);
            for (CettaExprLen i = 1u; i < arity; i++)
                children[i] = live->ids[test_below(live->len)];
            id = tu_expr_from_ids(universe, children, arity);
        }
        if (id != CETTA_ATOM_ID_NONE)
            test_ids_push(live, id);
    }
}

static void test_collection_matches_reachability(bool wide) {
    const char *names[] = {"A", "lt", "succ", "pair", "f", "g", "h", "k"};
    SymbolId heads[8];
    for (size_t i = 0u; i < 8u; i++)
        heads[i] = symbol_intern_cstr(g_symbols, names[i]);
    Arena persistent, snapshots;
    arena_init(&persistent);
    arena_init(&snapshots);
    TermUniverse universe;
    assert(term_universe_init_with_store_format(
        &universe, wide ? TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1
                        : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1));
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    TestIds roots = {0}, live = {0};
    TestHolder holder = {.universe = &universe};
    assert(term_universe_add_root_provider(&universe, test_list_roots,
                                           &roots));
    assert(term_universe_add_counted_root_provider(&universe, test_list_held,
                                                   &holder));
    uint64_t reclaimed_total = 0u, kept_total = 0u;

    for (unsigned round = 0u; round < 16u; round++) {
        make_terms(&universe, &live, heads, 8u, 600u);
        /* Decode some terms, so the pointer table holds their atoms. */
        for (size_t i = 0u; i < live.len; i += 3u)
            assert(term_universe_get_atom(&universe, live.ids[i]));
        roots.len = 0u;
        for (size_t i = 0u; i < live.len; i++) {
            if (test_below(6u) == 0u)
                test_ids_push(&roots, live.ids[i]);
            /* The holder takes some ids, a few more than once. */
            if (test_below(12u) == 0u)
                test_holder_take(&holder, live.ids[i]);
            if (test_below(40u) == 0u)
                test_holder_take(&holder, live.ids[i]);
        }
        /* And drops about a third of what it held. */
        for (size_t i = 0u; i < holder.ids.len;) {
            if (test_below(3u) == 0u)
                test_holder_drop(&holder, i);
            else
                i++;
        }

        size_t len = universe.len;
        bool *reached = calloc(len, sizeof(*reached));
        Atom **terms = calloc(len, sizeof(*terms));
        Atom **decoded = calloc(len, sizeof(*decoded));
        assert(reached && terms && decoded);
        for (size_t i = 0u; i < roots.len; i++)
            oracle_reach(&universe, roots.ids[i], reached);
        for (size_t i = 0u; i < holder.ids.len; i++)
            oracle_reach(&universe, holder.ids.ids[i], reached);
        for (size_t i = 0u; i < live.len; i++) {
            AtomId id = live.ids[i];
            if (!terms[id]) {
                terms[id] = term_universe_copy_atom(&universe, &snapshots, id);
                decoded[id] = term_universe_decoded_atom_at(&universe, id);
            }
            assert(terms[id]);
        }

        TermUniverseCollectionReport report;
        assert(term_universe_collect(&universe, &report));
        uint64_t expect_live = 0u;
        for (size_t id = 0u; id < len; id++)
            expect_live += reached[id];
        assert(report.live == expect_live);
        assert(report.entries == len);

        TestIds next = {0}, restore = {0};
        bool *seen = calloc(len, sizeof(*seen));
        assert(seen);
        for (size_t i = 0u; i < live.len; i++) {
            AtomId id = live.ids[i];
            if (seen[id])
                continue;
            seen[id] = true;
            if (reached[id]) {
                assert(record_equals(&universe, id, terms[id]));
                assert(term_universe_lookup_atom_id(&universe, terms[id]) ==
                       id);
                if (decoded[id])
                    assert(term_universe_lookup_atom_id(&universe,
                                                        decoded[id]) == id);
                test_ids_push(&next, id);
                kept_total++;
                continue;
            }
            /* Reclaimed: no lookup returns it, by structure or by the atom it
             * decoded to.  A ground term is then found nowhere; a term with
             * variables may meet a live entry its lookup takes as equal, one
             * spelling its variables differently.  Stored again, it is an
             * equal term, newly named. */
            AtomId found = term_universe_lookup_atom_id(&universe, terms[id]);
            assert(found != id);
            if (!atom_has_vars(terms[id]))
                assert(found == CETTA_ATOM_ID_NONE);
            else if (found != CETTA_ATOM_ID_NONE)
                assert(found < len && reached[found]);
            if (decoded[id]) {
                found = term_universe_lookup_atom_id(&universe, decoded[id]);
                assert(found != id);
                if (!atom_has_vars(decoded[id]))
                    assert(found == CETTA_ATOM_ID_NONE);
            }
            reclaimed_total++;
            if (test_below(4u) == 0u)
                test_ids_push(&restore, id);
        }
        /* Store some reclaimed terms again, once every lookup is checked:
         * storing one also makes its reclaimed subterms anew. */
        for (size_t i = 0u; i < restore.len; i++) {
            Atom *term = terms[restore.ids[i]];
            AtomId again =
                term_universe_store_atom_id(&universe, &persistent, term);
            assert(again != CETTA_ATOM_ID_NONE);
            assert(record_equals(&universe, again, term));
            test_ids_push(&next, again);
        }
        free(restore.ids);
        free(live.ids);
        live = next;
        free(seen);
        free(reached);
        free(terms);
        free(decoded);
    }
    assert(reclaimed_total > 1000u && kept_total > 1000u);
    printf("PASS: %s collection keeps what the roots reach and reclaims the "
           "rest (%" PRIu64 " kept, %" PRIu64 " reclaimed)\n",
           wide ? "wide" : "compact", kept_total, reclaimed_total);
    term_universe_remove_root_provider(&universe, test_list_roots, &roots);
    term_universe_remove_root_provider(&universe, test_list_held, &holder);
    term_universe_free(&universe);
    arena_free(&persistent);
    arena_free(&snapshots);
    free(roots.ids);
    free(holder.ids.ids);
    free(live.ids);
}

/* A count saturates at its largest value, and its entry is kept, with what
 * it holds, whatever is released later; an entry held as often, short of
 * that, is freed when its holders let go. */
static void test_saturated_counts_keep_their_entries(void) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    /* Counting starts at the first collection; the holds below are bare,
     * listed by no provider, so they must come after it. */
    assert(term_universe_collect(&universe, NULL) && universe.counting);
    SymbolId f = symbol_intern_cstr(g_symbols, "f");
    AtomId kids[2] = {tu_intern_symbol(&universe, f),
                      tu_intern_int(&universe, 70000)};
    AtomId shared = tu_expr_from_ids(&universe, kids, 2u);
    kids[1] = tu_intern_int(&universe, 60000);
    AtomId often = tu_expr_from_ids(&universe, kids, 2u);
    for (unsigned i = 0u; i < 70000u; i++)
        term_universe_hold(&universe, shared);
    for (unsigned i = 0u; i < 60000u; i++)
        term_universe_hold(&universe, often);
    assert(universe.saturated == 1u);
    for (unsigned i = 0u; i < 70000u; i++)
        term_universe_release(&universe, shared);
    for (unsigned i = 0u; i < 60000u; i++)
        term_universe_release(&universe, often);
    TermUniverseCollectionReport report;
    assert(term_universe_collect(&universe, &report));
    assert(tu_kind(&universe, shared) == ATOM_EXPR &&
           tu_int(&universe, tu_child(&universe, shared, 1u)) == 70000);
    assert(term_universe_lookup_atom_id(
               &universe, term_universe_copy_atom(&universe, &persistent,
                                                  shared)) == shared);
    /* OFTEN and its integer go; the symbol is held by SHARED. */
    assert(report.reclaimed == 2u);
    printf("PASS: a saturated count keeps its entry and what it holds\n");
    term_universe_free(&universe);
    arena_free(&persistent);
}

/* Freed entry slots are reused once their quarantine has passed: the
 * entries grow no further, every new term reads back and is found under
 * its reused id, and each reused slot counts a generation. */
static void test_freed_slots_are_reused(void) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    TestIds roots = {0};
    assert(term_universe_add_root_provider(&universe, test_list_roots,
                                           &roots));
    SymbolId h = symbol_intern_cstr(g_symbols, "h");
    AtomId kids[2] = {tu_intern_symbol(&universe, h), CETTA_ATOM_ID_NONE};
    test_ids_push(&roots, kids[0]);
    for (int64_t i = 0; i < 500; i++) {
        kids[1] = tu_intern_int(&universe, i);
        (void)tu_expr_from_ids(&universe, kids, 2u);
    }
    /* Three collections let the freed slots past any quarantine. */
    for (int round = 0; round < 3; round++)
        assert(term_universe_collect(&universe, NULL));
    size_t len = universe.len;
    AtomId made[400];
    for (int64_t i = 0; i < 400; i++) {
        kids[1] = tu_intern_int(&universe, 1000 + i);
        made[i] = tu_expr_from_ids(&universe, kids, 2u);
        assert(made[i] != CETTA_ATOM_ID_NONE && made[i] < len);
    }
    assert(universe.len == len);
    for (int64_t i = 0; i < 400; i++) {
        test_ids_push(&roots, made[i]);
        assert(tu_child(&universe, made[i], 0u) == kids[0]);
        assert(tu_int(&universe, tu_child(&universe, made[i], 1u)) ==
               1000 + i);
        Atom *term = term_universe_copy_atom(&universe, &persistent, made[i]);
        assert(term && term_universe_lookup_atom_id(&universe, term) ==
                           made[i]);
#if CETTA_BUILD_WITH_TERM_UNIVERSE_COLLECTION_CHECKS
        assert(universe.generations && universe.generations[made[i]] != 0u);
#endif
    }
    /* The reused entries collect and are kept like any other. */
    assert(term_universe_collect(&universe, NULL));
    for (int64_t i = 0; i < 400; i++)
        assert(tu_int(&universe, tu_child(&universe, made[i], 1u)) ==
               1000 + i);
    printf("PASS: freed slots are reused past their quarantine\n");
    term_universe_remove_root_provider(&universe, test_list_roots, &roots);
    term_universe_free(&universe);
    arena_free(&persistent);
    free(roots.ids);
}

/* Freed records are reused by size, and records too large for the size
 * lists leave holes until the records are compacted; either way every live
 * record reads back.  Compaction runs once the loose bytes reach the
 * pool's half. */
static void test_freed_records_are_reused_and_compacted(void) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    TestIds roots = {0};
    assert(term_universe_add_root_provider(&universe, test_list_roots,
                                           &roots));
    SymbolId g = symbol_intern_cstr(g_symbols, "g");
    AtomId head = tu_intern_symbol(&universe, g);
    test_ids_push(&roots, head);
    /* Small records, each kept or not; the freed ones are reused. */
    AtomId kids[96];
    kids[0] = head;
    for (int64_t i = 0; i < 1000; i++) {
        kids[1] = tu_intern_int(&universe, i);
        AtomId id = tu_expr_from_ids(&universe, kids, 2u);
        if (i % 10 == 0)
            test_ids_push(&roots, id);
    }
    TermUniverseCollectionReport report;
    assert(term_universe_collect(&universe, &report));
    assert(universe.record_free_bytes != 0u);
    size_t pool = universe.blob_len;
    /* Fewer than were freed, all of the freed size: the pool keeps its
     * length. */
    for (int64_t i = 2000; i < 2800; i++) {
        kids[1] = tu_intern_int(&universe, i);
        (void)tu_expr_from_ids(&universe, kids, 2u);
    }
    assert(universe.blob_len == pool);
    assert(term_universe_collect(&universe, &report));
    /* Large records leave holes, and the holes bring on compaction. */
    for (int64_t i = 0; i < 600; i++) {
        for (unsigned k = 1u; k < 96u; k++)
            kids[k] = tu_intern_int(&universe, (int64_t)k * 100000 + i);
        AtomId id = tu_expr_from_ids(&universe, kids, 96u);
        if (i % 10 == 0)
            test_ids_push(&roots, id);
    }
    assert(term_universe_collect(&universe, &report));
    assert(universe.record_free_bytes == 0u &&
           universe.record_hole_bytes == 0u);
    for (size_t i = 0u; i < roots.len; i++) {
        AtomId id = roots.ids[i];
        if (tu_kind(&universe, id) != ATOM_EXPR)
            continue;
        assert(tu_child(&universe, id, 0u) == head);
        Atom *term = term_universe_copy_atom(&universe, &persistent, id);
        assert(term && term_universe_lookup_atom_id(&universe, term) == id);
    }
    printf("PASS: freed records are reused, and holes are compacted away\n");
    term_universe_remove_root_provider(&universe, test_list_roots, &roots);
    term_universe_free(&universe);
    arena_free(&persistent);
    free(roots.ids);
}

/* Integers live in pages by value rather than in the intern table.  A
 * reclaimed integer leaves its page, and the page's other integers stay
 * found, through the page's promotion from sparse to dense. */
/* Pages of integers go with their values: rooted values keep exactly their
 * pages, unrooted ones none, and rounds of ever-new integers leave nothing
 * behind and no larger table. */
static void test_integer_pages_go_with_their_values(void) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    TestIds roots = {0};
    assert(term_universe_add_root_provider(&universe, test_list_roots,
                                           &roots));
    enum { VALUES = 64 * 512, EVERY = 97 };
    static bool kept_page[64];
    size_t kept_pages = 0u;
    for (int64_t value = 0; value < VALUES; value++) {
        AtomId id = tu_intern_int(&universe, value);
        if (value % EVERY == 0) {
            test_ids_push(&roots, id);
            if (!kept_page[value / 512]) {
                kept_page[value / 512] = true;
                kept_pages++;
            }
        }
    }
    TermUniverseCollectionReport report;
    assert(term_universe_collect(&universe, &report));
    assert(universe.integer_used == kept_pages);
    for (size_t i = 0u; i < roots.len; i++) {
        int64_t value = tu_int(&universe, roots.ids[i]);
        assert(value % EVERY == 0);
        assert(tu_intern_int(&universe, value) == roots.ids[i]);
    }
    for (int64_t value = 1; value < VALUES; value += EVERY)
        assert(tu_int(&universe, tu_intern_int(&universe, value)) == value);
    roots.len = 0u;
    assert(term_universe_collect(&universe, &report));
    assert(universe.integer_used == 0u);
    size_t mask = universe.integer_mask;
    for (int64_t round = 0; round < 64; round++) {
        for (int64_t value = 0; value < 4096; value++)
            assert(tu_intern_int(&universe, VALUES + round * 4096 + value) !=
                   CETTA_ATOM_ID_NONE);
        assert(term_universe_collect(&universe, &report));
        assert(universe.integer_used == 0u);
    }
    assert(universe.integer_mask == mask);
    printf("PASS: integer pages go with their values (%zu pages kept for "
           "%d rooted, none after; 64 rounds of fresh values leave none)\n",
           kept_pages, (VALUES + EVERY - 1) / EVERY);
    term_universe_remove_root_provider(&universe, test_list_roots, &roots);
    free(roots.ids);
    term_universe_free(&universe);
    arena_free(&persistent);
}

static void test_integer_pages_release_reclaimed_values(void) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    TestIds roots = {0};
    assert(term_universe_add_root_provider(&universe, test_list_roots,
                                           &roots));
    AtomId ids[400];
    for (int64_t value = 0; value < 400; value++) {
        ids[value] = tu_intern_int(&universe, value);
        if (value % 3 == 0)
            test_ids_push(&roots, ids[value]);
    }
    TermUniverseCollectionReport report;
    assert(term_universe_collect(&universe, &report));
    for (int64_t value = 0; value < 400; value++) {
        AtomId again = tu_intern_int(&universe, value);
        if (value % 3 == 0)
            assert(again == ids[value]);
        assert(tu_int(&universe, again) == value);
    }
    printf("PASS: integer pages keep rooted values and release the rest\n");
    term_universe_remove_root_provider(&universe, test_list_roots, &roots);
    term_universe_free(&universe);
    arena_free(&persistent);
    free(roots.ids);
}

/* A region that holds ids no provider lists holds collection off. */
static void test_inhibited_safe_point_collects_nothing(void) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    AtomId held = tu_intern_int(&universe, 12345);
    term_universe_collection_inhibit_enter(&universe);
    assert(!term_universe_collect(&universe, NULL));
    term_universe_collection_inhibit_leave(&universe);
    assert(tu_int(&universe, held) == 12345);
    TermUniverseCollectionReport report;
    assert(term_universe_collect(&universe, &report));
    assert(report.live == 0u && report.dead == 1u);
    printf("PASS: an open region holds collection off\n");
    term_universe_free(&universe);
    arena_free(&persistent);
}

#if CETTA_BUILD_WITH_TERM_UNIVERSE_COLLECTION_CHECKS
/* A build with collection checks stops at a read of a reclaimed entry. */
static void test_read_of_reclaimed_entry_traps(void) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        int devnull = open("/dev/null", 1);
        if (devnull >= 0)
            dup2(devnull, 2);
        Arena persistent;
        arena_init(&persistent);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &persistent);
        term_universe_set_collection_mode(&universe,
                                          TERM_UNIVERSE_COLLECTION_RECLAIM);
        AtomId lost = tu_intern_int(&universe, 777);
        (void)term_universe_collect(&universe, NULL);
        (void)tu_int(&universe, lost);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    printf("PASS: a read of a reclaimed entry traps\n");
}
#endif

/* Holders the registry test registers: two functions of each kind, so two
 * holders may share a context. */
static void registry_provider_a(const TermUniverse *universe,
                                TermUniverseRootVisit visit,
                                void *visit_context, void *provider_context) {
    (void)universe; (void)visit; (void)visit_context; (void)provider_context;
}

static void registry_provider_b(const TermUniverse *universe,
                                TermUniverseRootVisit visit,
                                void *visit_context, void *provider_context) {
    (void)universe; (void)visit; (void)visit_context; (void)provider_context;
}

static bool registry_observer_a(TermUniverse *universe,
                                TermUniverseStoreFormat old_format,
                                TermUniverseStoreFormat new_format, void *ctx) {
    (void)universe; (void)old_format; (void)new_format; (void)ctx;
    return true;
}

static bool registry_observer_b(TermUniverse *universe,
                                TermUniverseStoreFormat old_format,
                                TermUniverseStoreFormat new_format, void *ctx) {
    (void)universe; (void)old_format; (void)new_format; (void)ctx;
    return true;
}

enum { REGISTRY_TEST_HOLDERS = 600u };

/* Every slot holds a held key, each held key exactly once. */
static void registry_check_providers(const TermUniverse *universe,
                                     bool held[][2], size_t held_count,
                                     const int *contexts,
                                     TermUniverseRootProvider fns[2]) {
    static bool seen[REGISTRY_TEST_HOLDERS][2];
    memset(seen, 0, sizeof(seen));
    assert(universe->root_provider_len == held_count);
    for (size_t i = 0u; i < universe->root_provider_len; i++) {
        const TermUniverseRootProviderEntry *entry = &universe->root_providers[i];
        size_t h = (size_t)((const int *)entry->ctx - contexts);
        unsigned f = entry->fn == fns[0] ? 0u : 1u;
        assert(h < REGISTRY_TEST_HOLDERS && entry->fn == fns[f]);
        assert(held[h][f] && !seen[h][f]);
        seen[h][f] = true;
    }
}

static void registry_check_observers(const TermUniverse *universe,
                                     bool held[][2], size_t held_count,
                                     const int *contexts,
                                     TermUniverseStoreFormatObserver fns[2]) {
    static bool seen[REGISTRY_TEST_HOLDERS][2];
    memset(seen, 0, sizeof(seen));
    assert(universe->observer_len == held_count);
    for (size_t i = 0u; i < universe->observer_len; i++) {
        const TermUniverseStoreFormatObserverEntry *entry = &universe->observers[i];
        size_t h = (size_t)((const int *)entry->ctx - contexts);
        unsigned f = entry->fn == fns[0] ? 0u : 1u;
        assert(h < REGISTRY_TEST_HOLDERS && entry->fn == fns[f]);
        assert(held[h][f] && !seen[h][f]);
        seen[h][f] = true;
    }
}

/* A universe's registries keep exactly the holders a plain set keeps, each
 * in one slot: random adds, duplicate adds and removals, growing past and
 * shrinking below the size where the index starts, in fresh universes and
 * in ones whose index outlives the shrink. */
static void test_registries_keep_their_holders(void) {
    static int contexts[REGISTRY_TEST_HOLDERS];
    static bool providers[REGISTRY_TEST_HOLDERS][2];
    static bool observers[REGISTRY_TEST_HOLDERS][2];
    TermUniverseRootProvider provider_fns[2] = {registry_provider_a,
                                                registry_provider_b};
    TermUniverseStoreFormatObserver observer_fns[2] = {registry_observer_a,
                                                       registry_observer_b};
    uint64_t state = UINT64_C(0x2545f4914f6cdd1d);
    unsigned checks = 0u;
    for (unsigned round = 0u; round < 6u; round++) {
        TermUniverse universe;
        term_universe_init(&universe);
        memset(providers, 0, sizeof(providers));
        memset(observers, 0, sizeof(observers));
        size_t provider_count = 0u, observer_count = 0u;
        /* Small rounds stay near the index's threshold; large ones cross it
         * both ways, phases favouring adds, then removals. */
        size_t span = round < 2u ? 24u : REGISTRY_TEST_HOLDERS;
        for (unsigned step = 0u; step < 40000u; step++) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            size_t h = (size_t)(state % span);
            unsigned f = (unsigned)(state >> 40) & 1u;
            bool favour_add = (step / 2500u) % 2u == 0u;
            bool add = ((state >> 48) & 3u) != 0u ? favour_add : !favour_add;
            bool observer = ((state >> 52) & 1u) != 0u;
            if (observer) {
                if (add) {
                    assert(term_universe_add_store_format_observer(
                        &universe, observer_fns[f], &contexts[h]));
                    if (!observers[h][f]) {
                        observers[h][f] = true;
                        observer_count++;
                    }
                } else {
                    term_universe_remove_store_format_observer(
                        &universe, observer_fns[f], &contexts[h]);
                    if (observers[h][f]) {
                        observers[h][f] = false;
                        observer_count--;
                    }
                }
                assert(universe.observer_len == observer_count);
            } else {
                if (add) {
                    assert(term_universe_add_root_provider(
                        &universe, provider_fns[f], &contexts[h]));
                    if (!providers[h][f]) {
                        providers[h][f] = true;
                        provider_count++;
                    }
                } else {
                    term_universe_remove_root_provider(
                        &universe, provider_fns[f], &contexts[h]);
                    if (providers[h][f]) {
                        providers[h][f] = false;
                        provider_count--;
                    }
                }
                assert(universe.root_provider_len == provider_count);
            }
            if (step % 61u == 0u) {
                registry_check_providers(&universe, providers, provider_count,
                                         contexts, provider_fns);
                registry_check_observers(&universe, observers, observer_count,
                                         contexts, observer_fns);
                checks++;
            }
        }
        registry_check_providers(&universe, providers, provider_count,
                                 contexts, provider_fns);
        registry_check_observers(&universe, observers, observer_count,
                                 contexts, observer_fns);
        term_universe_free(&universe);
    }
    printf("PASS: registries keep exactly their holders through %u checks, "
           "indexed and scanned\n", checks);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable var_intern;
    init_test_symbols(&symbols);
    var_intern_init(&var_intern);
    g_var_intern = &var_intern;
    test_collection_matches_reachability(false);
    test_collection_matches_reachability(true);
    test_integer_pages_release_reclaimed_values();
    test_integer_pages_go_with_their_values();
    test_inhibited_safe_point_collects_nothing();
    test_saturated_counts_keep_their_entries();
    test_freed_records_are_reused_and_compacted();
    test_freed_slots_are_reused();
    test_registries_keep_their_holders();
#if CETTA_BUILD_WITH_TERM_UNIVERSE_COLLECTION_CHECKS
    test_read_of_reclaimed_entry_traps();
#endif
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);
    return 0;
}
