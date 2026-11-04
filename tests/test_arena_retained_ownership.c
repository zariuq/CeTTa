/* Randomized differential test: arena_retain_owner with marks and resets
 * against a reference model of acquisition-ordered (owner, release) holds. */
#include "atom.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum { OWNERS = 300, OPS = 400000, MAXMARKS = 64 };
static int holds_a[OWNERS], holds_b[OWNERS];
static int base[OWNERS];
static int idx(void *p) { return (int)((int *)p - base); }
static void ra(void *p) { holds_a[idx(p)]++; }
static void la(void *p) { assert(holds_a[idx(p)] > 0); holds_a[idx(p)]--; }
static void rb(void *p) { holds_b[idx(p)]++; }
static void lb(void *p) { assert(holds_b[idx(p)] > 0); holds_b[idx(p)]--; }

/* model: list of (owner, which) in acquisition order */
static int model_owner[2 * OWNERS], model_which[2 * OWNERS], model_len;
static int model_has(int o, int w) {
    for (int i = 0; i < model_len; i++)
        if (model_owner[i] == o && model_which[i] == w) return 1;
    return 0;
}

static void randomized_ownership(void) {
    Arena a; arena_init_detached(&a);
    ArenaOwnerCursor cursor;
    assert(arena_owner_cursor_begin(&a, &cursor));
    int cursor_at = 0;
    size_t cursor_visits = 0u;
    ArenaMark marks[MAXMARKS]; int mark_len[MAXMARKS]; int nmarks = 0;
    srand(12345);
    long resets = 0, dedups = 0, adds = 0;
    for (int op = 0; op < OPS; op++) {
        int r = rand() % 100;
        if (r < 85) {
            int o = rand() % OWNERS, w = rand() % 2;
            int had = model_has(o, w);
            assert(w ? arena_retain_owner(&a, &base[o], rb, lb)
                     : arena_retain_resource_sized(&a, &base[o], ra, la,
                                                   100u + o, 100u + o));
            if (!had) { model_owner[model_len] = o; model_which[model_len] = w; model_len++; adds++; }
            else dedups++;
        } else if (r < 93 && nmarks < MAXMARKS) {
            marks[nmarks] = arena_mark(&a); mark_len[nmarks] = model_len; nmarks++;
        } else if (nmarks > 0) {
            int k = rand() % nmarks;
            arena_reset(&a, marks[k]);
            model_len = mark_len[k];
            nmarks = k + 1;   /* marks after k are invalid now; k stays valid */
            resets++;
            ArenaOwnerView invalid = {0};
            assert(arena_owner_cursor_peek(&a, &cursor, &invalid) ==
                   ARENA_OWNER_CURSOR_INVALIDATED);
            assert(!arena_owner_cursor_accept(&a, &cursor));
            assert(arena_owner_cursor_begin(&a, &cursor));
            cursor_at = 0;
        }
        /* Each consumer offer has its own bounded allowance. A reset
         * requires an explicit new prefix; otherwise only new owners appear. */
        for (unsigned step = 0u; step < (unsigned)op % 4u; step++) {
            ArenaOwnerView view = {0};
            ArenaOwnerCursorStep next = arena_owner_cursor_peek(&a, &cursor, &view);
            if (cursor_at == model_len) {
                assert(next == ARENA_OWNER_CURSOR_END);
                break;
            }
            assert(next == ARENA_OWNER_CURSOR_ITEM &&
                   view.owner == &base[model_owner[cursor_at]] &&
                   view.release == (model_which[cursor_at] ? lb : la));
            assert(arena_owner_cursor_accept(&a, &cursor));
            cursor_at++;
            cursor_visits++;
        }
        if (op % 997 == 0 || op == OPS - 1) {
            int ca[OWNERS] = {0}, cb[OWNERS] = {0};
            size_t resources = 0u;
            for (int i = 0; i < model_len; i++) (model_which[i] ? cb : ca)[model_owner[i]]++;
            for (int i = 0; i < model_len; i++)
                if (model_which[i] == 0)
                    resources += 100u + (unsigned)model_owner[i];
            assert(a.retained_resource_bytes == resources);
            assert(a.retained_resource_bytes <= a.external_bytes);
            for (int o = 0; o < OWNERS; o++) {
                if (ca[o] != holds_a[o] || cb[o] != holds_b[o]) {
                    fprintf(stderr, "MISMATCH op %d owner %d: model %d/%d real %d/%d\n",
                            op, o, ca[o], cb[o], holds_a[o], holds_b[o]);
                    abort();
                }
            }
        }
    }
    while (cursor_at < model_len) {
        ArenaOwnerView view;
        assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_ITEM);
        assert(view.owner == &base[model_owner[cursor_at]] &&
               view.release == (model_which[cursor_at] ? lb : la));
        assert(arena_owner_cursor_accept(&a, &cursor));
        cursor_at++;
        cursor_visits++;
    }
    ArenaOwnerView end = {0};
    assert(arena_owner_cursor_peek(&a, &cursor, &end) == ARENA_OWNER_CURSOR_END);
    arena_free(&a);
    assert(arena_owner_cursor_peek(&a, &cursor, &end) == ARENA_OWNER_CURSOR_INVALIDATED);
    assert(a.retained_resource_bytes == 0u);
    for (int o = 0; o < OWNERS; o++) assert(!holds_a[o] && !holds_b[o]);
    printf("PASS: %d ops, %ld adds, %ld dedups, %ld resets; holds match the model\n", OPS, adds, dedups, resets);
    printf("PASS: %zu ordered cursor visits match the independent reset-aware model\n",
           cursor_visits);
}


static unsigned held;
static void retain(void *p) { (void)p; held++; }
static void release(void *p) { (void)p; assert(held); held--; }

/* What the arena is charged beyond its blocks: transient finalizer storage
 * (external) and the index capacity that survives a reset (persistent). */
static size_t charge(const Arena *a) {
    return a->external_bytes + a->persistent_bytes;
}

static void accounting_controls(void) {
    Arena a;
    int owners[512];
    arena_init_detached(&a);
    ArenaMark empty = arena_mark(&a);
    for (unsigned i = 0; i < 512; i++)
        assert(arena_retain_owner(&a, &owners[i], retain, release));
    size_t populated = charge(&a);
    arena_reset(&a, empty);
    size_t retained = charge(&a);
    printf("owner populated=%zu after-reset=%zu\n", populated, retained);
    assert(held == 0 && arena_at_mark(&a, empty));
    assert(retained > 0 && retained < populated);
    ArenaMark warm = arena_mark(&a);
    assert(arena_mark_accounted_live_bytes(warm) == arena_accounted_live_bytes(&a));
    for (unsigned i = 0; i < 512; i++)
        assert(arena_retain_owner(&a, &owners[i], retain, release));
    assert(charge(&a) == populated);
    arena_reset(&a, warm);
    assert(charge(&a) == retained && arena_at_mark(&a, warm));
    arena_reset(&a, empty);
    assert(charge(&a) == retained && arena_at_mark(&a, empty));
    arena_free(&a);
    assert(!held && charge(&a) == 0);

    arena_init_detached(&a);
    empty = arena_mark(&a);
    CettaFrameIdentity id;
    assert(cetta_frame_identity_acquire(&id));
    assert(arena_retain_frame_identity(&a, id));
    cetta_frame_identity_release(id);
    populated = charge(&a);
    arena_reset(&a, empty);
    printf("frame populated=%zu after-reset=%zu\n", populated, charge(&a));
    assert(populated > 0 && charge(&a) == populated);
    assert(arena_at_mark(&a, empty));
    assert(!cetta_frame_identity_retain(id));
    arena_free(&a);
    assert(charge(&a) == 0);
    puts("PASS: reset releases holds and preserves charges for retained index capacity");
}

typedef struct { unsigned holds, retains, releases, other_releases; } Owner;
static void owner_retain(void *p) { Owner *o=p; o->holds++; o->retains++; }
static void owner_release(void *p) { Owner *o=p; assert(o->holds); o->holds--; o->releases++; }
static void other_release(void *p) { Owner *o=p; assert(o->holds); o->holds--; o->other_releases++; }

static bool same_cursor(ArenaOwnerCursor left, ArenaOwnerCursor right) {
    return left.identity == right.identity && left.position == right.position &&
        left.reset_epoch == right.reset_epoch && left.initialized == right.initialized;
}

static void owner_cursor_controls(void) {
    enum { PREFIX = 2048, EXTRA = 8 };
    Owner owners[PREFIX + EXTRA];
    for (unsigned i = 0u; i < PREFIX + EXTRA; i++)
        owners[i] = (Owner){0};
    Arena a, other;
    arena_init_detached(&a);
    arena_init_detached(&other);
    ArenaOwnerCursor cursor = {0};
    ArenaOwnerView view = {.owner = &owners[0], .release = other_release};
    ArenaOwnerView sentinel = view;
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_INVALIDATED);
    assert(view.owner == sentinel.owner && view.release == sentinel.release);
    assert(arena_owner_cursor_begin(&a, &cursor));
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_END);
    assert(view.owner == sentinel.owner && view.release == sentinel.release);
    assert(!arena_owner_cursor_accept(&a, &cursor));
    size_t visits = 0u;
    for (unsigned i = 0u; i < PREFIX; i++) {
        assert(arena_retain_owner(&a, &owners[i], owner_retain, owner_release));
        ArenaOwnerCursor pending = cursor;
        assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_ITEM);
        assert(view.owner == &owners[i] && view.release == owner_release);
        assert(same_cursor(cursor, pending));
        /* A refused downstream registration leaves this item unacknowledged. */
        ArenaOwnerView retry;
        assert(arena_owner_cursor_peek(&a, &cursor, &retry) == ARENA_OWNER_CURSOR_ITEM);
        assert(retry.owner == view.owner && retry.release == view.release);
        assert(arena_owner_cursor_accept(&a, &cursor));
        visits++;
        assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_END);
    }
    assert(visits == PREFIX && cursor.position == PREFIX);
    for (unsigned repeat = 0u; repeat < 10000u; repeat++)
        assert(arena_retain_owner(&a, &owners[repeat % PREFIX], owner_retain, owner_release));
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_END);
    assert(cursor.position == PREFIX);
    /* Crossing the power-of-two prefix grows the records and hash index
     * between a reader's peek and accept; no vector pointer may be cached. */
    ArenaOwnerCursor prefix_reader;
    assert(arena_owner_cursor_begin(&a, &prefix_reader));
    assert(arena_owner_cursor_peek(&a, &prefix_reader, &view) == ARENA_OWNER_CURSOR_ITEM);
    assert(view.owner == &owners[0] && view.release == owner_release);
    /* A release callback is part of a key, even when the owner is shared. */
    assert(arena_retain_owner(&a, &owners[0], owner_retain, other_release));
    assert(arena_owner_cursor_accept(&a, &prefix_reader));
    assert(arena_owner_cursor_peek(&a, &prefix_reader, &view) == ARENA_OWNER_CURSOR_ITEM);
    assert(view.owner == &owners[1] && view.release == owner_release);
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_ITEM);
    assert(view.owner == &owners[0] && view.release == other_release);
    assert(arena_owner_cursor_accept(&a, &cursor));

    ArenaOwnerCursor before_reset = cursor;
    ArenaMark prefix = arena_mark(&a);
    for (unsigned i = PREFIX; i < PREFIX + EXTRA; i++)
        assert(arena_retain_owner(&a, &owners[i], owner_retain, owner_release));
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_ITEM);
    assert(view.owner == &owners[PREFIX]);
    arena_reset(&a, prefix);
    view = sentinel;
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_INVALIDATED);
    assert(view.owner == sentinel.owner && view.release == sentinel.release);
    assert(!arena_owner_cursor_accept(&a, &cursor) && same_cursor(cursor, before_reset));
    assert(arena_owner_cursor_begin(&a, &cursor));
    /* Wrong Arena, invalid offset and absent output cannot consume anything. */
    assert(arena_owner_cursor_peek(&other, &cursor, &view) == ARENA_OWNER_CURSOR_INVALIDATED);
    assert(!arena_owner_cursor_accept(&other, &cursor));
    assert(arena_owner_cursor_peek(&a, &cursor, NULL) == ARENA_OWNER_CURSOR_INVALIDATED);
    ArenaOwnerCursor invalid = cursor;
    invalid.position = UINT32_MAX;
    assert(arena_owner_cursor_peek(&a, &invalid, &view) == ARENA_OWNER_CURSOR_INVALIDATED);
    assert(!arena_owner_cursor_accept(&a, &invalid));
    /* Another append can occur between peek and accept after a rebase. */
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_ITEM);
    assert(view.owner == &owners[0]);
    assert(arena_retain_owner(&a, &owners[PREFIX], owner_retain, owner_release));
    assert(arena_owner_cursor_accept(&a, &cursor));
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_ITEM);
    assert(view.owner == &owners[1]);
    ArenaMark unchanged = arena_mark(&a);
    arena_reset(&a, unchanged);
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_INVALIDATED);
    assert(!arena_owner_cursor_accept(&a, &cursor));
    assert(arena_owner_cursor_begin(&a, &cursor));
    ArenaOwnerCursor incarnation = cursor;
    arena_free(&a);
    assert(arena_owner_cursor_peek(&a, &incarnation, &view) == ARENA_OWNER_CURSOR_INVALIDATED);
    assert(!arena_owner_cursor_begin(&a, &cursor));
    arena_init_detached(&a);
    assert(arena_owner_cursor_peek(&a, &incarnation, &view) == ARENA_OWNER_CURSOR_INVALIDATED);
    assert(arena_owner_cursor_begin(&a, &cursor));
    assert(arena_owner_cursor_peek(&a, &cursor, &view) == ARENA_OWNER_CURSOR_END);
    arena_free(&a);
    arena_free(&other);
    for (unsigned i = 0u; i < PREFIX + EXTRA; i++) {
        assert(owners[i].holds == 0u);
        assert(owners[i].retains == owners[i].releases + owners[i].other_releases);
    }
    puts("PASS: owner cursor visits only new acquisitions, retries pending work and rejects reset/reincarnation");
}
static void ownership_controls(void) {
    Arena a, b;
    arena_init_detached(&a); arena_init_detached(&b);
    Owner x={0}, y={0};
    ArenaMark empty=arena_mark(&a);
    assert(!arena_retain_owner(&a,NULL,owner_retain,owner_release));
    assert(arena_retain_owner(&a,&x,owner_retain,owner_release));
    ArenaMark kept=arena_mark(&a);
    for (unsigned i=0;i<100;i++) assert(arena_retain_owner(&a,&x,owner_retain,owner_release));
    assert(x.retains==1 && x.holds==1);
    assert(arena_retain_owner(&a,&x,owner_retain,other_release));
    assert(arena_retain_owner(&a,&y,owner_retain,owner_release));
    assert(x.holds==2 && y.holds==1);
    arena_reset(&a,kept);
    assert(x.holds==1 && x.releases==0 && x.other_releases==1);
    assert(y.holds==0 && y.releases==1);
    /* A rolled-back key is eligible again; an older live key stays deduped. */
    assert(arena_retain_owner(&a,&x,owner_retain,owner_release));
    assert(x.retains==2);
    assert(arena_retain_owner(&a,&y,owner_retain,owner_release));
    assert(y.retains==2 && y.holds==1);
    assert(arena_retain_owner(&b,&x,owner_retain,owner_release));
    assert(x.holds==2);
    arena_reset(&a,empty);
    assert(x.holds==1 && y.holds==0);
    assert(arena_retain_owner(&a,&x,owner_retain,owner_release));
    assert(x.holds==2);
    arena_free(&a); arena_free(&b);
    assert(x.holds==0 && y.holds==0);
    assert(x.retains==x.releases+x.other_releases && y.retains==y.releases);
}

static void sized_owner_controls(void) {
    Arena charged, control;
    Owner x = {0}, y = {0};
    arena_init_detached(&charged);
    arena_init_detached(&control);
    assert(!arena_retain_owner_sized(NULL, &x, owner_retain, owner_release, 4096));
    assert(arena_retain_owner_sized(&charged, &x, owner_retain, owner_release, 4096));
    assert(arena_retain_owner(&control, &x, owner_retain, owner_release));
    assert(charged.external_bytes == control.external_bytes + 4096);
    ArenaMark charged_mark = arena_mark(&charged);
    ArenaMark control_mark = arena_mark(&control);
    for (unsigned i = 0; i < 100; i++)
        assert(arena_retain_owner_sized(&charged, &x, owner_retain, owner_release, 4096));
    assert(charged.external_bytes == charged_mark.external_bytes);
    assert(arena_retain_owner_sized(&charged, &y, owner_retain, owner_release, 8192));
    assert(arena_retain_owner(&control, &y, owner_retain, owner_release));
    assert(charged.external_bytes == control.external_bytes + 4096 + 8192);
    arena_reset(&charged, charged_mark);
    arena_reset(&control, control_mark);
    assert(charged.external_bytes == control.external_bytes + 4096);
    assert(x.holds == 2 && y.holds == 0);
    arena_free(&charged);
    arena_free(&control);
    assert(x.holds == 0 && y.holds == 0);
    puts("PASS: external owner bytes are charged once and reset with their hold");
}

static void resource_owner_controls(void) {
    Arena a;
    Owner metadata = {0}, resource = {0}, later = {0};
    arena_init_detached(&a);
    ArenaMark empty = arena_mark(&a);
    assert(!arena_retain_resource_sized(&a, &resource, owner_retain,
        owner_release, 100u, 101u));
    assert(resource.holds == 0u && a.retained_resource_bytes == 0u);
    assert(arena_retain_owner_sized(&a, &metadata, owner_retain,
        owner_release, 4096u));
    assert(a.retained_resource_bytes == 0u);
    assert(arena_retain_resource_sized(&a, &resource, owner_retain,
        owner_release, 4096u, 3072u));
    assert(a.retained_resource_bytes == 3072u);
    ArenaMark retained = arena_mark(&a);
    for (unsigned i = 0u; i < 100u; i++)
        assert(arena_retain_resource_sized(&a, &resource, owner_retain,
            owner_release, 4096u, 3072u));
    assert(resource.holds == 1u && a.retained_resource_bytes == 3072u);
    assert(a.external_bytes == retained.external_bytes);
    assert(arena_retain_resource_sized(&a, &later, owner_retain,
        owner_release, 8192u, 8192u));
    assert(a.retained_resource_bytes == 11264u);
    assert(a.retained_resource_bytes <= a.external_bytes);
    arena_reset(&a, retained);
    assert(arena_at_mark(&a, retained));
    assert(later.holds == 0u && resource.holds == 1u);
    assert(a.retained_resource_bytes == 3072u);
    arena_reset(&a, empty);
    assert(a.retained_resource_bytes == 0u);
    assert(metadata.holds == 0u && resource.holds == 0u);
    arena_free(&a);
    assert(a.retained_resource_bytes == 0u);
    puts("PASS: typed resource charges exclude metadata, deduplicate aliases and follow partial resets");
}


int main(void) {
    owner_cursor_controls();
    ownership_controls();
    accounting_controls();
    sized_owner_controls();
    resource_owner_controls();
    randomized_ownership();
    return 0;
}
