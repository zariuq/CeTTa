/* Randomized differential test: arena_retain_owner with marks and resets
 * against a reference model of acquisition-ordered (owner, release) holds. */
#include "atom.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static size_t refuse_after,allocation_calls;
static bool refusal_hit;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
static bool allocation_refused(void) {
    if(!refuse_after)return false;
    allocation_calls++;
    if(--refuse_after)return false;
    refusal_hit=true;errno=ENOMEM;return true;
}
void *__wrap_malloc(size_t size) {
    return allocation_refused()?NULL:__real_malloc(size);
}
void *__wrap_calloc(size_t count,size_t width) {
    return allocation_refused()?NULL:__real_calloc(count,width);
}
void *__wrap_realloc(void *pointer,size_t size) {
    return allocation_refused()?NULL:__real_realloc(pointer,size);
}

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

static unsigned unexpected_storage_calls;
static void owner_storage(const void *owner,CettaStorageCensus *census) {
    (void)owner;(void)census;unexpected_storage_calls++;
}
typedef struct {
    const Owner *known;
    unsigned visited;
    unsigned stop_after;
    bool accept_unknown;
} OwnerWalk;
static bool inspect_owner(const void *owner,void (*drop)(void *),
        CettaStorageVisitor storage,void *raw) {
    OwnerWalk *walk=raw;walk->visited++;
    if(walk->stop_after && walk->visited>=walk->stop_after) return false;
    if(owner==walk->known)
        return drop==owner_release && storage==owner_storage;
    return walk->accept_unknown;
}
typedef struct {CettaForeignHold hold;unsigned references;} DirectForeign;
static void direct_retain(void *raw) {((DirectForeign *)raw)->references++;}
static void direct_release(void *raw) {
    DirectForeign *value=raw;assert(value->references);value->references--;
}
static const CettaForeignHold direct_hold={.retain=direct_retain,.release=direct_release};
static bool inspect_direct(const void *owner,void (*drop)(void *),
        CettaStorageVisitor storage,void *raw) {
    return owner==raw && drop==direct_release && storage==NULL;
}
static void retained_owner_walk_controls(void) {
    Arena arena;arena_init_detached(&arena);
    Owner x={0},y={0};OwnerWalk walk={.known=&x};
    assert(arena_retained_owners_satisfy(&arena,inspect_owner,&walk) && !walk.visited);
    assert(!arena_retained_owners_satisfy(NULL,inspect_owner,&walk));
    assert(!arena_retained_owners_satisfy(&arena,NULL,&walk));
    ArenaMark empty=arena_mark(&arena);
    assert(arena_retain_owner_with_storage(&arena,&x,owner_retain,owner_release,owner_storage));
    assert(arena_retained_owners_satisfy(&arena,inspect_owner,&walk) && walk.visited==1u);
    assert(x.holds==1u && x.retains==1u && !x.releases && !unexpected_storage_calls);
    assert(arena_retain_owner_with_storage(&arena,&x,owner_retain,owner_release,owner_storage));
    assert(arena_retain_owner(&arena,&y,owner_retain,other_release));
    walk.visited=0u;
    assert(!arena_retained_owners_satisfy(&arena,inspect_owner,&walk) && walk.visited==2u);
    walk=(OwnerWalk){.known=&x,.accept_unknown=true};
    assert(arena_retained_owners_satisfy(&arena,inspect_owner,&walk) && walk.visited==2u);
    walk=(OwnerWalk){.known=&x,.accept_unknown=true,.stop_after=1u};
    assert(!arena_retained_owners_satisfy(&arena,inspect_owner,&walk) && walk.visited==1u);
    assert(x.holds==1u && y.holds==1u && !x.releases && !y.other_releases && !unexpected_storage_calls);
    arena_reset(&arena,empty);walk=(OwnerWalk){.known=&x};
    assert(arena_retained_owners_satisfy(&arena,inspect_owner,&walk) && !walk.visited);
    assert(!x.holds && !y.holds && x.releases==1u && y.other_releases==1u);
    DirectForeign foreign={.hold=direct_hold};
    assert(atom_foreign(&arena,(CettaForeignValue *)&foreign));
    assert(foreign.references==1u);
    /* The deduplicated ledger is empty; the direct finalizer is still live. */
    assert(arena_retained_owners_satisfy(&arena,inspect_owner,&walk) && !walk.visited);
    assert(arena_finalizers_satisfy(&arena,inspect_direct,&foreign));
    assert(!arena_finalizers_satisfy(&arena,inspect_owner,&walk));
    assert(foreign.references==1u && !unexpected_storage_calls);
    arena_free(&arena);
    assert(!foreign.references);
    puts("PASS: complete retained-owner walk preserves identity, holds and stopped predicates");
}

static void allocation_refusal_controls(void) {
    const unsigned prefixes[]={0u,4u,8u};
    const size_t requests[]={5u,2u,4u};
    size_t refusals=0u;
    for(size_t p=0u;p<sizeof(prefixes)/sizeof(*prefixes);p++) {
        unsigned prefix=prefixes[p];
        for(size_t fault=0u;fault<=requests[p];fault++) {
            Arena a;arena_init_detached(&a);Owner owners[10]={0};
            for(unsigned i=0u;i<prefix;i++)
                assert(arena_retain_owner(&a,&owners[i],owner_retain,owner_release));
            ArenaMark mark=arena_mark(&a);
            const void *finalizers=a.finalizers;
            size_t external=a.external_bytes;
            allocation_calls=0u;refusal_hit=false;
            refuse_after=fault?fault:SIZE_MAX;
            bool acquired=arena_retain_owner(&a,&owners[prefix],owner_retain,owner_release);
            refuse_after=0u;
            if(!fault) {
                assert(acquired&&!refusal_hit&&allocation_calls==requests[p]);
            } else {
                assert(!acquired&&refusal_hit&&allocation_calls==fault);
                assert(!owners[prefix].holds&&!owners[prefix].retains&&
                    a.finalizers==finalizers&&a.external_bytes==external);
                assert(!arena_retains_owner(&a,&owners[prefix],owner_release));
                for(unsigned i=0u;i<prefix;i++) {
                    assert(owners[i].holds==1u&&owners[i].retains==1u);
                    assert(arena_retains_owner(&a,&owners[i],owner_release));
                    assert(arena_retained_owner_at(&a,owner_release,i)==&owners[prefix-1u-i]);
                }
                assert(!arena_retained_owner_at(&a,owner_release,prefix));
                arena_reset(&a,mark);
                assert(arena_at_mark(&a,mark));
                assert(arena_retain_owner(&a,&owners[prefix],owner_retain,owner_release));
                refusals++;
            }
            refusal_hit=false;allocation_calls=0u;refuse_after=1u;
            assert(arena_retain_owner(&a,&owners[prefix],owner_retain,owner_release));
            refuse_after=0u;
            assert(!refusal_hit&&!allocation_calls&&owners[prefix].holds==1u);
            arena_reset(&a,mark);
            assert(!owners[prefix].holds&&arena_at_mark(&a,mark));
            for(unsigned i=0u;i<prefix;i++)assert(owners[i].holds==1u);
            arena_free(&a);assert(charge(&a)==0u);
            for(size_t i=0u;i<sizeof(owners)/sizeof(*owners);i++)
                assert(!owners[i].holds&&owners[i].retains==owners[i].releases);
        }
    }
    assert(refusals==11u);
    puts("PASS: all 11 owner-acquisition allocation refusals preserve holds, order, rollback and retry");
}

static void live_span_controls(void) {
    Arena a, other; arena_init_detached(&a); arena_init_detached(&other);
    assert(!arena_has_live_span(NULL, &a, sizeof(a)) &&
        !arena_has_live_span(&a, NULL, 1u));
    unsigned char *prefix = arena_alloc(&a, 64u);
    void *foreign = arena_alloc(&other, 64u);
    assert(prefix && foreign && arena_has_live_span(&a, prefix, 64u));
    assert(arena_has_live_span(&a, prefix + 63u, 1u) &&
        !arena_has_live_span(&a, prefix + 63u, 2u) &&
        !arena_has_live_span(&a, prefix, 65u) &&
        !arena_has_live_span(&a, prefix, 0u) &&
        !arena_has_live_span(&a, foreign, 64u) &&
        !arena_has_live_span(&a, prefix, SIZE_MAX));
    ArenaMark mark = arena_mark(&a);
    void *removed = arena_alloc(&a, 64u);
    assert(removed && arena_has_live_span(&a, removed, 64u));
    arena_reset(&a, mark);
    assert(arena_owns_ptr(&a, removed) && !arena_has_live_span(&a, removed, 1u) &&
        arena_has_live_span(&a, prefix, 64u));
    size_t large = a.head->capacity * 2u;
    void *spare = arena_alloc(&a, large);
    assert(spare && arena_has_live_span(&a, spare, large));
    arena_reset(&a, mark);
    assert(arena_owns_ptr(&a, spare) && !arena_has_live_span(&a, spare, 1u));
    arena_free(&a); arena_free(&other);
    puts("PASS: live spans exclude foreign storage, spare capacity, truncated prefixes and overflow");
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
    retained_owner_walk_controls();
    allocation_refusal_controls();
    live_span_controls();
    owner_cursor_controls();
    ownership_controls();
    accounting_controls();
    sized_owner_controls();
    resource_owner_controls();
    randomized_ownership();
    return 0;
}
