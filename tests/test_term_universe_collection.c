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
 * Each round makes new terms over the live ones, chooses roots, and
 * collects.  The collector must find live exactly the entries a plain
 * recursive walk from the roots reaches.  Every live entry must keep its
 * term, read back through its record after the records were compacted,
 * and a lookup of that term must still find it.  Every reclaimed term must
 * be gone from every lookup, and storing it again must give an equal term
 * under a new id. */

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
    assert(term_universe_add_root_provider(&universe, test_list_roots,
                                           &roots));
    uint64_t reclaimed_total = 0u, kept_total = 0u;

    for (unsigned round = 0u; round < 16u; round++) {
        make_terms(&universe, &live, heads, 8u, 600u);
        /* Decode some terms, so the pointer table holds their atoms. */
        for (size_t i = 0u; i < live.len; i += 3u)
            assert(term_universe_get_atom(&universe, live.ids[i]));
        roots.len = 0u;
        for (size_t i = 0u; i < live.len; i++) {
            if (test_below(5u) == 0u)
                test_ids_push(&roots, live.ids[i]);
        }

        size_t len = universe.len;
        bool *reached = calloc(len, sizeof(*reached));
        Atom **terms = calloc(len, sizeof(*terms));
        Atom **decoded = calloc(len, sizeof(*decoded));
        assert(reached && terms && decoded);
        for (size_t i = 0u; i < roots.len; i++)
            oracle_reach(&universe, roots.ids[i], reached);
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
            assert(again != CETTA_ATOM_ID_NONE && again >= len);
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
    term_universe_free(&universe);
    arena_free(&persistent);
    arena_free(&snapshots);
    free(roots.ids);
    free(live.ids);
}

/* Integers live in pages by value rather than in the intern table.  A
 * reclaimed integer leaves its page, and the page's other integers stay
 * found, through the page's promotion from sparse to dense. */
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
        else
            assert(again != ids[value] && again >= 400u);
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

int main(void) {
    SymbolTable symbols;
    VarInternTable var_intern;
    init_test_symbols(&symbols);
    var_intern_init(&var_intern);
    g_var_intern = &var_intern;
    test_collection_matches_reachability(false);
    test_collection_matches_reachability(true);
    test_integer_pages_release_reclaimed_values();
    test_inhibited_safe_point_collects_nothing();
#if CETTA_BUILD_WITH_TERM_UNIVERSE_COLLECTION_CHECKS
    test_read_of_reclaimed_entry_traps();
#endif
    return 0;
}
