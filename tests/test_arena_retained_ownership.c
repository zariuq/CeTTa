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
    ArenaMark marks[MAXMARKS]; int mark_len[MAXMARKS]; int nmarks = 0;
    srand(12345);
    long resets = 0, dedups = 0, adds = 0;
    for (int op = 0; op < OPS; op++) {
        int r = rand() % 100;
        if (r < 85) {
            int o = rand() % OWNERS, w = rand() % 2;
            int had = model_has(o, w);
            assert(arena_retain_owner(&a, &base[o], w ? rb : ra, w ? lb : la));
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
        }
        if (op % 997 == 0 || op == OPS - 1) {
            int ca[OWNERS] = {0}, cb[OWNERS] = {0};
            for (int i = 0; i < model_len; i++) (model_which[i] ? cb : ca)[model_owner[i]]++;
            for (int o = 0; o < OWNERS; o++) {
                if (ca[o] != holds_a[o] || cb[o] != holds_b[o]) {
                    fprintf(stderr, "MISMATCH op %d owner %d: model %d/%d real %d/%d\n",
                            op, o, ca[o], cb[o], holds_a[o], holds_b[o]);
                    abort();
                }
            }
        }
    }
    arena_free(&a);
    for (int o = 0; o < OWNERS; o++) assert(!holds_a[o] && !holds_b[o]);
    printf("PASS: %d ops, %ld adds, %ld dedups, %ld resets; holds match the model\n", OPS, adds, dedups, resets);
}


static unsigned held;
static void retain(void *p) { (void)p; held++; }
static void release(void *p) { (void)p; assert(held); held--; }

static void accounting_controls(void) {
    Arena a;
    int owners[512];
    arena_init_detached(&a);
    ArenaMark empty = arena_mark(&a);
    for (unsigned i = 0; i < 512; i++)
        assert(arena_retain_owner(&a, &owners[i], retain, release));
    size_t populated = a.external_bytes;
    arena_reset(&a, empty);
    size_t retained = a.external_bytes;
    printf("owner populated=%zu after-reset=%zu\n", populated, retained);
    assert(held == 0 && arena_at_mark(&a, empty));
    assert(retained > 0 && retained < populated);
    ArenaMark warm = arena_mark(&a);
    assert(arena_mark_accounted_live_bytes(warm) == arena_accounted_live_bytes(&a));
    for (unsigned i = 0; i < 512; i++)
        assert(arena_retain_owner(&a, &owners[i], retain, release));
    assert(a.external_bytes == populated);
    arena_reset(&a, warm);
    assert(a.external_bytes == retained && arena_at_mark(&a, warm));
    arena_reset(&a, empty);
    assert(a.external_bytes == retained && arena_at_mark(&a, empty));
    arena_free(&a);
    assert(!held && a.external_bytes == 0);

    arena_init_detached(&a);
    empty = arena_mark(&a);
    CettaFrameIdentity id;
    assert(cetta_frame_identity_acquire(&id));
    assert(arena_retain_frame_identity(&a, id));
    cetta_frame_identity_release(id);
    populated = a.external_bytes;
    arena_reset(&a, empty);
    printf("frame populated=%zu after-reset=%zu\n", populated, a.external_bytes);
    assert(populated > 0 && a.external_bytes == populated);
    assert(arena_at_mark(&a, empty));
    assert(!cetta_frame_identity_retain(id));
    arena_free(&a);
    assert(a.external_bytes == 0);
    puts("PASS: reset releases holds and preserves charges for retained index capacity");
}

typedef struct { unsigned holds, retains, releases, other_releases; } Owner;
static void owner_retain(void *p) { Owner *o=p; o->holds++; o->retains++; }
static void owner_release(void *p) { Owner *o=p; assert(o->holds); o->holds--; o->releases++; }
static void other_release(void *p) { Owner *o=p; assert(o->holds); o->holds--; o->other_releases++; }
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


int main(void) {
    ownership_controls();
    accounting_controls();
    randomized_ownership();
    return 0;
}
