#include "foreign_region.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void require(bool condition, const char *what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        exit(1);
    }
}

static void load(const char *path) {
    fid_t frame = PL_open_foreign_frame();
    term_t input = PL_new_term_ref();
    require(input && PL_put_atom_chars(input, path), "fixture path");
    require(PL_call_predicate(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
        PL_predicate("consult", 1, "system"), input), "load fixture");
    PL_discard_foreign_frame(frame);
}

static void start(CettaForeignRegion *region, const char *name, int arity,
                  term_t args) {
    require(cetta_foreign_region_start(region, NULL,
        PL_predicate(name, arity, "foreign_region_fixture"), args)
        == CETTA_FOREIGN_OK, "start goal");
}

static bool solved(CettaForeignRegion *region) {
    int status = cetta_foreign_region_next(region);
    return status == PL_S_TRUE || status == PL_S_LAST;
}

static bool unary(CettaForeignRegion *region, const char *name, uint64_t key) {
    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK, "begin unary");
    term_t var = cetta_foreign_region_variable(region, key);
    require(var != 0, "unary variable");
    start(region, name, 1, var);
    bool answer = solved(region);
    require(cetta_foreign_region_finish(region, answer, NULL) == CETTA_FOREIGN_OK,
            "finish unary");
    return answer;
}

static bool bind_integer(CettaForeignRegion *region, uint64_t key, int64_t n) {
    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK, "begin binding");
    term_t var = cetta_foreign_region_variable(region, key);
    term_t args = PL_new_term_refs(2);
    require(var && args && PL_put_term(args, var) && PL_put_int64(args + 1, n),
            "binding arguments");
    start(region, "bind", 2, args);
    bool answer = solved(region);
    require(cetta_foreign_region_finish(region, answer, NULL) == CETTA_FOREIGN_OK,
            "finish binding");
    return answer;
}

static void link(CettaForeignRegion *region, uint64_t x, uint64_t y,
                 const char *name) {
    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK, "begin link");
    term_t left = cetta_foreign_region_variable(region, x);
    term_t right = cetta_foreign_region_variable(region, y);
    term_t args = PL_new_term_refs(2);
    require(left && right && args && PL_put_term(args, left) &&
            PL_put_term(args + 1, right), "link arguments");
    start(region, name, 2, args);
    require(solved(region), "link answer");
    require(cetta_foreign_region_finish(region, true, NULL) == CETTA_FOREIGN_OK,
            "commit link");
}

static void observe_integer(CettaForeignRegion *region, uint64_t key, int64_t n) {
    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK, "begin observation");
    term_t var = cetta_foreign_region_variable(region, key);
    int64_t value = 0;
    require(var && PL_get_int64(var, &value) && value == n, "observe retained integer");
    require(cetta_foreign_region_finish(region, false, NULL) == CETTA_FOREIGN_OK,
            "release observation");
}

static void observe_variable(CettaForeignRegion *region, uint64_t key) {
    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK, "begin free observation");
    term_t var = cetta_foreign_region_variable(region, key);
    require(var && PL_is_variable(var), "observe unbound variable");
    require(cetta_foreign_region_finish(region, false, NULL) == CETTA_FOREIGN_OK,
            "release free observation");
}

static void require_fault(record_t fault, const char *expected) {
    fid_t frame = PL_open_foreign_frame();
    term_t term = PL_new_term_ref();
    char *actual = NULL;
    require(fault && term && PL_recorded(fault, term) &&
            PL_get_atom_chars(term, &actual) && !strcmp(actual, expected),
            "exact fault payload");
    PL_discard_foreign_frame(frame);
    PL_erase(fault);
}

static void retained_attributes_and_history(void) {
    CettaForeignRegion *parent = cetta_foreign_region_open();
    require(parent && unary(parent, "domain", 77), "create retained domain");
    require(cetta_foreign_region_checkpoint(parent, 4) == CETTA_FOREIGN_OK,
            "first native checkpoint");
    require(bind_integer(parent, 77, 7), "narrow first branch");
    observe_integer(parent, 77, 7);
    require(cetta_foreign_region_rollback(parent, 4) == CETTA_FOREIGN_OK &&
            unary(parent, "original", 77), "rollback restores attributes");
    require(cetta_foreign_region_checkpoint(parent, 5) == CETTA_FOREIGN_OK &&
            !bind_integer(parent, 77, 99) && unary(parent, "original", 77),
            "failed operation restores attributes");
    link(parent, UINT64_MAX, 77, "alias");
    require(cetta_foreign_region_commit(parent) == CETTA_FOREIGN_OK,
            "commit roots created below checkpoint");
    CettaForeignSnapshot *snapshot = cetta_foreign_region_snapshot(parent);
    require(snapshot != NULL, "snapshot attributed graph");
    CettaForeignRegion *child = cetta_foreign_region_restore(snapshot);
    require(child && unary(child, "original", 77), "record restores attributes");
    require(cetta_foreign_region_begin(parent) == CETTA_FOREIGN_INVALID,
            "reject mutation of non-innermost parent");
    require(bind_integer(child, UINT64_MAX, 3), "child narrows alias");
    observe_integer(child, 77, 3);
    require(cetta_foreign_region_free(child) == CETTA_FOREIGN_OK &&
            unary(parent, "original", 77), "child has no mutable parent handles");
    CettaForeignRegion *sibling = cetta_foreign_region_restore(snapshot);
    require(sibling && bind_integer(sibling, 77, 8), "independent restored sibling");
    observe_integer(sibling, UINT64_MAX, 8);
    require(cetta_foreign_region_free(sibling) == CETTA_FOREIGN_OK,
            "release sibling");
    cetta_foreign_snapshot_free(snapshot);
    require(cetta_foreign_region_free(parent) == CETTA_FOREIGN_OK, "release parent");
    puts("PASS: retained attributes, aliasing, checkpoint commit, rollback and sibling isolation");
}

static void catalogue_and_cycles(void) {
    CettaForeignRegion *region = cetta_foreign_region_open();
    require(region != NULL, "catalogue region");
    for (uint64_t key = 0; key < 1050; key++)
        require(bind_integer(region, key, (int64_t)key), "populate radix boundaries");
    require(cetta_foreign_region_checkpoint(region, 3) == CETTA_FOREIGN_OK,
            "catalogue checkpoint");
    require(bind_integer(region, 999999, 13), "new branch variable");
    require(cetta_foreign_region_variable_count(region) == 1051,
            "catalogue extends");
    require(cetta_foreign_region_rollback(region, 3) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_variable_count(region) == 1050,
            "rollback restores catalogue");
    observe_variable(region, 999999);
    require(cetta_foreign_region_variable_count(region) == 1050,
            "aborted observation retains no new slot");
    for (uint64_t key = 0; key < 1050; key++)
        observe_integer(region, key, (int64_t)key);
    require(unary(region, "cyclic", UINT64_MAX), "create retained cycle");
    CettaForeignSnapshot *snapshot = cetta_foreign_region_snapshot(region);
    CettaForeignRegion *copy = cetta_foreign_region_restore(snapshot);
    require(copy && cetta_foreign_region_begin(copy) == CETTA_FOREIGN_OK,
            "restore cycle");
    term_t cycle = cetta_foreign_region_variable(copy, UINT64_MAX);
    require(cycle && !PL_is_acyclic(cycle), "snapshot preserves cycle");
    require(cetta_foreign_region_finish(copy, false, NULL) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_free(copy) == CETTA_FOREIGN_OK,
            "release cycle copy");
    cetta_foreign_snapshot_free(snapshot);
    require(cetta_foreign_region_free(region) == CETTA_FOREIGN_OK, "release catalogue");
    puts("PASS: stable slots across radix boundaries, catalogue rollback and cyclic snapshots");
}

static void checkpoint_frontier(void) {
    CettaForeignRegion *region = cetta_foreign_region_open();
    require(region != NULL, "frontier region");
    for (uint32_t mark = 1u; mark <= 4u; mark++)
        require(cetta_foreign_region_checkpoint(region, mark) == CETTA_FOREIGN_OK &&
                bind_integer(region, mark * 100u, mark * 10u), "frontier history");
    const uint32_t invalid[] = {4u, 2u}, kept[] = {2u, 4u};
    require(cetta_foreign_region_frontier(region, invalid, 2u) == CETTA_FOREIGN_INVALID,
            "unordered frontier is refused without pruning");
    require(cetta_foreign_region_frontier(region, kept, 2u) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_frontier(region, kept, 2u) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_variable_count(region) == 4u,
            "frontier is idempotent and retains current graph");
    require(cetta_foreign_region_rollback(region, 4u) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_variable_count(region) == 3u,
            "frontier preserves absolute last rollback mark");
    observe_integer(region, 300u, 30);
    require(cetta_foreign_region_rollback(region, 2u) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_variable_count(region) == 1u,
            "same horizon restores earliest operation, not latest");
    observe_integer(region, 100u, 10);
    require(cetta_foreign_region_checkpoint(region, 9u) == CETTA_FOREIGN_OK &&
            bind_integer(region, 200u, 55) &&
            cetta_foreign_region_frontier(region, NULL, 0u) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_rollback(region, 9u) == CETTA_FOREIGN_OK,
            "empty live frontier commits graph without copying");
    observe_integer(region, 200u, 55);
    require(cetta_foreign_region_free(region) == CETTA_FOREIGN_OK, "release frontier region");
    puts("PASS: live absolute horizons preserve rollback, catalogue and current state");
}

static void checkpoint_compaction(void) {
    CettaForeignRegion *region = cetta_foreign_region_open();
    require(region != NULL, "compaction region");
    for (uint32_t mark = 1; mark <= 4; mark++) {
        require(cetta_foreign_region_checkpoint(region, mark) == CETTA_FOREIGN_OK &&
                bind_integer(region, mark * 100, mark * 10), "compaction history");
    }
    const uint32_t invalid[] = {4, 2};
    require(cetta_foreign_region_rebase(region, invalid, 2) == CETTA_FOREIGN_INVALID,
            "reject unordered checkpoint map without changing history");
    const uint32_t kept[] = {2, 4};
    require(cetta_foreign_region_rebase(region, kept, 2) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_rollback(region, 1) == CETTA_FOREIGN_OK,
            "rollback compacted last checkpoint");
    require(cetta_foreign_region_variable_count(region) == 3,
            "last rollback restores key catalogue");
    observe_integer(region, 300, 30);
    require(cetta_foreign_region_rollback(region, 0) == CETTA_FOREIGN_OK &&
            cetta_foreign_region_variable_count(region) == 1,
            "rollback first kept checkpoint closes unreachable anchors");
    observe_integer(region, 100, 10);
    require(cetta_foreign_region_checkpoint(region, 0) == CETTA_FOREIGN_OK &&
            bind_integer(region, 200, 55) &&
            cetta_foreign_region_rebase(region, NULL, 0) == CETTA_FOREIGN_OK,
            "empty checkpoint map commits history without copying roots");
    observe_integer(region, 200, 55);
    require(cetta_foreign_region_rollback(region, 0) == CETTA_FOREIGN_OK,
            "committed history cannot be rewound");
    observe_integer(region, 200, 55);
    qid_t nested = PL_open_query(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
        PL_predicate("true", 0, "system"), PL_new_term_ref());
    require(nested && cetta_foreign_region_free(region) == CETTA_FOREIGN_NOT_INNER &&
            cetta_foreign_region_rollback(region, 0) == CETTA_FOREIGN_NOT_INNER,
            "unmanaged younger query prevents frame destruction");
    require(PL_close_query(nested) &&
            cetta_foreign_region_free(region) == CETTA_FOREIGN_OK,
            "release compaction region in valid order");
    puts("PASS: binding-history compaction preserves rollback and committed roots");
}

static void query_lifecycle(void) {
    CettaForeignRegion *region = cetta_foreign_region_open();
    require(region && unary(region, "watch", 1), "install effectful freeze");
    require(cetta_foreign_region_checkpoint(region, 1) == CETTA_FOREIGN_OK &&
            bind_integer(region, 1, 7), "wake once");
    require(cetta_foreign_region_rollback(region, 1) == CETTA_FOREIGN_OK &&
            bind_integer(region, 1, 8), "restored freeze wakes in another branch");
    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK,
            "observe persistent effects");
    term_t count = PL_new_term_ref();
    start(region, "event_count", 1, count);
    int64_t events = 0;
    require(solved(region) && PL_get_int64(count, &events) && events == 2,
            "rollback does not undo or replay database effects");
    require(cetta_foreign_region_finish(region, false, NULL) == CETTA_FOREIGN_OK,
            "release effect observation");
    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK,
            "open alternatives");
    term_t value = cetta_foreign_region_variable(region, 2);
    start(region, "choices", 1, value);
    CettaForeignSnapshot *selected = NULL;
    for (int64_t expected = 1; expected <= 3; expected++) {
        int64_t got = 0;
        require(solved(region) && PL_get_int64(value, &got) && got == expected,
                "resume alternatives without replay");
        if (expected == 1) {
            selected = cetta_foreign_region_snapshot(region);
            CettaForeignRegion *branch = cetta_foreign_region_restore(selected);
            require(branch && !bind_integer(branch, 2, 2),
                    "snapshot selected answer while cursor remains live");
            observe_integer(branch, 2, 1);
            require(cetta_foreign_region_free(branch) == CETTA_FOREIGN_OK,
                    "release selected child before advancing parent cursor");
        }
    }
    require(cetta_foreign_region_finish(region, true, NULL) == CETTA_FOREIGN_OK,
            "cut selected solution");
    observe_integer(region, 2, 3);
    CettaForeignRegion *saved = cetta_foreign_region_restore(selected);
    require(saved != NULL, "restore prior answer after cursor cut");
    observe_integer(saved, 2, 1);
    require(cetta_foreign_region_free(saved) == CETTA_FOREIGN_OK,
            "release saved prior answer");
    cetta_foreign_snapshot_free(selected);

    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK,
            "body fault operation");
    value = cetta_foreign_region_variable(region, 3);
    start(region, "body_fault", 1, value);
    require(cetta_foreign_region_next(region) == PL_S_EXCEPTION, "body raises");
    record_t fault = 0;
    require(cetta_foreign_region_finish(region, false, &fault) == CETTA_FOREIGN_FAULT,
            "release body fault");
    require_fault(fault, "body_failure");
    observe_variable(region, 3);

    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK,
            "cleanup fault operation");
    value = cetta_foreign_region_variable(region, 4);
    start(region, "release_fault", 1, value);
    require(solved(region), "cleanup protected answer");
    require(cetta_foreign_region_finish(region, true, &fault) == CETTA_FOREIGN_FAULT,
            "cleanup fault aborts tentative commit");
    require_fault(fault, "release_failure");
    require(PL_exception(0) == 0, "cleanup fault does not poison later query");
    observe_variable(region, 4);

    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK,
            "nested release control");
    value = cetta_foreign_region_variable(region, 5);
    start(region, "choices", 1, value);
    require(solved(region), "outer answer");
    qid_t nested = PL_open_query(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
        PL_predicate("true", 0, "system"), PL_new_term_ref());
    require(nested != 0, "nested query");
    require(cetta_foreign_region_finish(region, true, NULL) == CETTA_FOREIGN_NOT_INNER,
            "refused release leaves operation live");
    require(PL_close_query(nested) &&
            cetta_foreign_region_finish(region, true, NULL) == CETTA_FOREIGN_OK,
            "retry release in valid order");
    observe_integer(region, 5, 1);
    require(cetta_foreign_region_free(region) == CETTA_FOREIGN_OK, "release lifecycle");
    puts("PASS: effects, alternatives, cut, exact body and cleanup faults, refused release");
}

static void network(size_t size) {
    clock_t began = clock();
    CettaForeignRegion *region = cetta_foreign_region_open();
    require(region != NULL, "network region");
    for (size_t index = 0; index < size; index++)
        link(region, index, index + 1, "post");
    require(bind_integer(region, size, 0), "wake retained network");
    observe_integer(region, 0, (int64_t)size);
    require(cetta_foreign_region_begin(region) == CETTA_FOREIGN_OK,
            "garbage collection operation");
    term_t arg = PL_new_term_ref();
    require(cetta_foreign_region_start(region, NULL,
        PL_predicate("garbage_collect", 0, "system"), arg) == CETTA_FOREIGN_OK &&
        solved(region) &&
        cetta_foreign_region_finish(region, true, NULL) == CETTA_FOREIGN_OK,
        "collect while retaining roots");
    observe_integer(region, 0, (int64_t)size);
    CettaForeignRegionStats stats = cetta_foreign_region_stats(region);
    require(stats.snapshots == 0 && stats.snapshot_slots == 0 &&
            stats.slot_steps <= (2 * size + 4) *
                ((sizeof(size_t) * CHAR_BIT + 4) / 5),
            "linear posts transfer no full-network snapshots");
    printf("PASS: retained network size=%zu operations=%llu slot_steps=%llu snapshots=%llu cpu=%.6f\n",
        size, (unsigned long long)stats.operations,
        (unsigned long long)stats.slot_steps, (unsigned long long)stats.snapshots,
        (double)(clock() - began) / CLOCKS_PER_SEC);
    require(cetta_foreign_region_free(region) == CETTA_FOREIGN_OK, "release network");
}

typedef struct {
    CettaForeignRegion *parent;
    const CettaForeignSnapshot *snapshot;
    PL_engine_t engine;
} Transfer;

static void *transfer_worker(void *context) {
    Transfer *transfer = context;
    require(PL_set_engine(transfer->engine, NULL) == PL_ENGINE_SET,
            "claim migrated engine");
    require(cetta_foreign_region_begin(transfer->parent) == CETTA_FOREIGN_INVALID &&
            cetta_foreign_region_open() == NULL,
            "another thread cannot borrow or shadow a live region");
    require(cetta_foreign_region_adopt(transfer->parent) == CETTA_FOREIGN_OK &&
            unary(transfer->parent, "original", 77),
            "explicit adoption reuses the attached engine's attributed graph");
    require(cetta_foreign_region_checkpoint(transfer->parent, 8) == CETTA_FOREIGN_OK &&
            bind_integer(transfer->parent, 77, 5),
            "adopted owner can refine its graph");
    require(cetta_foreign_region_rollback(transfer->parent, 8) == CETTA_FOREIGN_OK &&
            unary(transfer->parent, "original", 77),
            "adopted owner can roll back without replay");
    require(PL_set_engine(NULL, NULL) == PL_ENGINE_SET, "release migrated engine");
    require(PL_thread_attach_engine(NULL) >= 0, "attach independent worker engine");
    CettaForeignRegion *child = cetta_foreign_region_restore(transfer->snapshot);
    require(child && unary(child, "original", 77) && bind_integer(child, 77, 6),
            "restore attributed snapshot on another engine");
    observe_integer(child, 77, 6);
    require(cetta_foreign_region_free(child) == CETTA_FOREIGN_OK,
            "release worker-owned region");
    require(PL_thread_destroy_engine(), "destroy released worker engine");
    return NULL;
}

static void cross_thread_transfer(void) {
    CettaForeignRegion *parent = cetta_foreign_region_open();
    require(parent && unary(parent, "domain", 77), "transfer parent");
    CettaForeignSnapshot *snapshot = cetta_foreign_region_snapshot(parent);
    require(snapshot != NULL, "transfer snapshot");
    Transfer transfer = {parent, snapshot, PL_current_engine()};
    require(PL_set_engine(NULL, NULL) == PL_ENGINE_SET, "detach owner engine");
    pthread_t worker;
    require(!pthread_create(&worker, NULL, transfer_worker, &transfer) &&
            !pthread_join(worker, NULL), "complete worker transfer");
    require(PL_set_engine(transfer.engine, NULL) == PL_ENGINE_SET,
            "reattach original owner engine");
    require(cetta_foreign_region_begin(parent) == CETTA_FOREIGN_INVALID &&
            cetta_foreign_region_adopt(parent) == CETTA_FOREIGN_OK,
            "reattached owner must explicitly reclaim the lease");
    require(unary(parent, "original", 77), "worker transfer leaves parent unchanged");
    cetta_foreign_snapshot_free(snapshot);
    require(cetta_foreign_region_free(parent) == CETTA_FOREIGN_OK,
            "release transfer parent");
    puts("PASS: explicit engine lease transfer, rollback and independent attributed snapshot");
}

int main(int argc, char **argv) {
    require(argc == 2, "fixture path required");
    char *initial[] = {"foreign-region-gate", "-q", "--nosignals", NULL};
    require(PL_initialise(3, initial), "initialize SWI");
    load(argv[1]);
    retained_attributes_and_history();
    catalogue_and_cycles();
    checkpoint_compaction();
    checkpoint_frontier();
    query_lifecycle();
    network(100);
    network(1100);
    network(4400);
    cross_thread_transfer();
    require(PL_cleanup(0) == PL_CLEANUP_SUCCESS, "clean final engine release");
    return 0;
}
