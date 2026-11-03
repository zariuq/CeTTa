#include "proof_batches.h"
#include "term_domain.h"
#include "parser.h"
#include "fallible_alloc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void *watched_domain;
static unsigned domain_frees;
void __real_free(void *);
void __wrap_free(void *p) {
    if (p && p == watched_domain) { domain_frees++; watched_domain = NULL; }
    __real_free(p);
}
static bool converting;
static unsigned constructor_calls, refuse_constructor, malloc_calls, refuse_malloc;
static bool refuse_promotion_payload;
AtomId __real_tu_expr_from_ids(TermUniverse *, const AtomId *, CettaExprLen);
AtomId __wrap_tu_expr_from_ids(TermUniverse *u, const AtomId *ids, CettaExprLen n) {
    if (converting) {
        assert(u->collection_inhibit && !term_universe_collect(u, NULL));
        if (++constructor_calls == refuse_constructor) return CETTA_ATOM_ID_NONE;
    }
    return __real_tu_expr_from_ids(u, ids, n);
}
void *__real_cetta_fallible_malloc(size_t);
void *__wrap_cetta_fallible_malloc(size_t n) {
    if (converting && ++malloc_calls == refuse_malloc) return NULL;
    return __real_cetta_fallible_malloc(n);
}
void *__real_cetta_fallible_calloc(size_t, size_t);
void *__wrap_cetta_fallible_calloc(size_t n, size_t width) {
    if (refuse_promotion_payload) { refuse_promotion_payload = false; return NULL; }
    return __real_cetta_fallible_calloc(n, width);
}
static Atom *parse(Arena *a, const char *text) {
    Atom **forms = NULL; assert(parse_metta_text(text, a, &forms) == 1);
    Atom *result = forms[0]; free(forms); return result;
}
static AtomId key(TermUniverse *u, Arena *a, const char *text) {
    AtomId id = term_universe_store_alpha_key(u, parse(a, text));
    assert(id != CETTA_ATOM_ID_NONE); return id;
}
static const char *const expected[] = {
    "(proofs 4 2 (C (pair $a $a) I))",
    "(proofs 3 1 (C (pair $a $b) (M I)))",
    "(proofs 3 1 (C (pair $a $b) (1 I)))",
    "(proofs 2 0 (C (pair $b $a) (3 (M I))))",
    "(proofs 2 0 (C (pair $b $a) (3 (1 I))))"
};
typedef struct {
    CettaTermDomain *domain;
    CettaTermDomainScope scope;
    TermUniverse *universe;
    Arena syntax;
    Space space;
    CettaProofRowLayout layout;
    AtomId root, seed;
} Fixture;
static void init(Fixture *f) {
    *f = (Fixture){0};
    f->domain = cetta_term_domain_new(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1, TERM_UNIVERSE_COLLECTION_RECLAIM, false);
    assert(f->domain && cetta_term_domain_enter(f->domain, &f->scope));
    f->universe = cetta_term_domain_universe(f->domain);
    arena_init_detached(&f->syntax);
    f->layout = (CettaProofRowLayout){.proof = {.length = 2u, .child = {3u, 2u}},
        .arity = {4u, 3u}, .head = {parse(&f->syntax, "proofs")->sym_id, parse(&f->syntax, "C")->sym_id},
        .universe_instance = f->universe->instance_id, .storage_epoch = f->universe->storage_epoch};
    f->root = key(f->universe, &f->syntax, "(proofs 4 2 (C (pair $a $a)))");
    f->seed = key(f->universe, &f->syntax, "I");
    space_init_with_universe(&f->space, f->universe); f->space.kind = SPACE_KIND_SET;
    space_add(&f->space, parse(&f->syntax, expected[0])); assert(space_length64(&f->space) == 1u);
}
static SpaceProofImageStatus promote(Fixture *f) {
    return space_promote_proof_seed(&f->space, &f->layout, f->root, f->seed,
        (CettaProofLimits){4096u, 4096u, 8u, 100000u, 100000u},
        (CettaProofBatchLifetime){f->domain, cetta_term_domain_retain, cetta_term_domain_release});
}
static void cleanup(Fixture *f) {
    space_free(&f->space); arena_free(&f->syntax);
    cetta_term_domain_leave(&f->scope); cetta_term_domain_release(f->domain);
    assert(cetta_proof_batch_diagnostics().bytes == 0u);
}
static CettaProofBatchStore *store(Fixture *f) {
    CettaProofBatchStore *s = space_proof_batch_store_borrow(&f->space, f->layout.head[0]); assert(s); return s;
}
static void populate(Fixture *f) {
    uint64_t revision = f->space.revision;
    assert(promote(f) == SPACE_PROOF_IMAGE_READY);
    assert(!space_dense_head_rows_absent(&f->space, f->layout.head[0]));
    assert(f->space.revision == revision && f->space.native.len == 1u && space_length64(&f->space) == 1u);
    CettaProofBatchStore *s = store(f); CettaProofBatchView *parent = NULL;
    assert(cetta_proof_batch_seal_rank(s, 0u, &parent) == CETTA_PROOF_BATCH_OK);
    CettaProofBatchTransaction *t = NULL; CettaProofBatchOffer offered; CettaProofBatchResult result;
    AtomId child = key(f->universe, &f->syntax, "(proofs 3 1 (C (pair $a $b)))");
    AtomId m = key(f->universe, &f->syntax, "M"), one = key(f->universe, &f->syntax, "1");
    assert(cetta_proof_batch_begin(s, &t) == CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_offer(t, parent, 0u, m, child, &offered) == CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_offer(t, parent, 0u, one, child, &offered) == CETTA_PROOF_BATCH_OK);
    assert(space_publish_proof_batch(&f->space, f->layout.head[0], t, &result) == CETTA_PROOF_BATCH_OK);
    assert(result.appended_rows == 2u && f->space.revision == revision + 1u);
    cetta_proof_batch_transaction_release(t); t = NULL;
    assert(cetta_proof_batch_begin(s, &t) == CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_offer(t, parent, 0u, m, child, &offered) == CETTA_PROOF_BATCH_DUPLICATE);
    assert(space_publish_proof_batch(&f->space, f->layout.head[0], t, &result) == CETTA_PROOF_BATCH_OK);
    assert(!result.appended_rows && result.producer_occurrences == 1u && f->space.revision == revision + 1u);
    cetta_proof_batch_transaction_release(t); t = NULL; cetta_proof_batch_view_release(parent); parent = NULL;
    assert(cetta_proof_batch_seal_rank(s, 1u, &parent) == CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_begin(s, &t) == CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_offer(t, parent, 0u, key(f->universe, &f->syntax, "3"),
        key(f->universe, &f->syntax, "(proofs 2 0 (C (pair $b $a)))"), &offered) == CETTA_PROOF_BATCH_OK);
    assert(space_publish_proof_batch(&f->space, f->layout.head[0], t, &result) == CETTA_PROOF_BATCH_OK);
    assert(result.appended_rows == 2u && result.producer_occurrences == 2u &&
        f->space.revision == revision + 2u && f->space.native.len == 1u && space_length64(&f->space) == 5u);
    cetta_proof_batch_transaction_release(t); cetta_proof_batch_view_release(parent);
}
static void bag(Fixture *f, bool unrelated) {
    assert(space_length64(&f->space) == 5u + unrelated);
    unsigned seen[5] = {0};
    for (CettaIndex i = 0u; i < space_length64(&f->space); i++) {
        Atom *row = space_get_at64(&f->space, i); assert(row);
        if (unrelated && atom_alpha_eq(row, parse(&f->syntax, "(unrelated 9)"))) continue;
        bool found = false;
        for (unsigned j = 0u; j < 5u; j++) if (atom_alpha_eq(row, parse(&f->syntax, expected[j]))) {
            seen[j]++; found = true; break;
        }
        assert(found);
    }
    for (unsigned j = 0u; j < 5u; j++) assert(seen[j] == 1u);
}
static void ordinary_observers(void) {
    Fixture f; init(&f); populate(&f);
    uint64_t revision = f.space.revision, equation = f.space.equation_revision;
    Atom *pattern = parse(&f.syntax, "(proofs $a $b $c)"); CettaCount count = 0u;
    assert(space_dense_count_columns(&f.space, pattern->expr.elems, pattern->expr.len, &count) && count == 4u);
    assert(f.space.native.len == 1u && f.space.native.dense_facts == 4u && f.space.has_proof_image);
    assert(term_universe_collect(f.universe, NULL));
    assert(space_dense_count_columns(&f.space, pattern->expr.elems, pattern->expr.len, &count) && count == 4u);
    AtomId unrelated = key(f.universe, &f.syntax, "(unrelated 9)");
    assert(space_add_alpha_keys_batch(&f.space, &unrelated, 1u));
    assert(f.space.has_proof_image && f.space.native.len == 2u && f.space.revision == revision + 1u);
    assert(space_prepare_proof_read(&f.space, parse(&f.syntax, "(unrelated $x)")) && f.space.has_proof_image);
    revision = f.space.revision;
    bag(&f, true);
    assert(!f.space.has_proof_image && f.space.revision == revision && f.space.equation_revision == equation);
    assert(space_contains_for_add_nodup(&f.space, parse(&f.syntax, expected[3])));
    assert(space_contains_exact(&f.space, parse(&f.syntax, "(unrelated 9)")));
    assert(!space_contains_exact(&f.space, parse(&f.syntax, "(unrelated 10)")));
    space_add(&f.space, parse(&f.syntax, expected[3])); assert(space_length64(&f.space) == 6u);
    assert(space_remove(&f.space, parse(&f.syntax, expected[3])) && space_length64(&f.space) == 5u);
    cleanup(&f);
    puts("PASS: actual Space seed promotion, incremental producer/storage counts, native wildcard count, disjoint append and complete alias/proof bags");
}
static void refusal_retry(void) {
    unsigned last_malloc = 0u;
    for (unsigned variant = 0u; variant < 3u; variant++) {
        Fixture f; init(&f); populate(&f); uint64_t revision = f.space.revision;
        constructor_calls = malloc_calls = 0u; refuse_constructor = variant == 1u ? 2u : 0u;
        refuse_malloc = variant == 2u ? last_malloc : 0u; converting = true;
        SpaceProofImageStatus status = space_settle_proof_images(&f.space); converting = false;
        if (!variant) { assert(status == SPACE_PROOF_IMAGE_READY); last_malloc = malloc_calls; assert(last_malloc); }
        else {
            assert(status == SPACE_PROOF_IMAGE_CAPACITY && f.space.has_proof_image &&
                f.space.native.len == 1u && f.space.native.dense_facts == 4u &&
                space_length64(&f.space) == 5u && f.space.revision == revision);
            refuse_constructor = refuse_malloc = 0u;
            assert(space_settle_proof_images(&f.space) == SPACE_PROOF_IMAGE_READY);
        }
        assert(f.space.revision == revision); bag(&f, false); cleanup(&f);
    }
    puts("PASS: actual constructor and final destination-buffer refusal preserve compressed rows/revisions and retry without replay");
}
static void promotion_and_publish_controls(void) {
    Fixture f; init(&f); uint64_t revision = f.space.revision;
    refuse_promotion_payload = true; assert(promote(&f) == SPACE_PROOF_IMAGE_CAPACITY);
    assert(!f.space.has_proof_image && space_length64(&f.space) == 1u && f.space.revision == revision);
    assert(promote(&f) == SPACE_PROOF_IMAGE_READY);
    CettaProofBatchStore *s = store(&f); CettaProofBatchView *parent = NULL;
    CettaProofBatchStore *alien = NULL;
    assert(cetta_proof_batch_store_new(f.universe, &f.layout, f.root, f.seed,
        (CettaProofLimits){8u,8u,4u,100u,100u},
        (CettaProofBatchLifetime){f.domain,cetta_term_domain_retain,cetta_term_domain_release}, &alien) == CETTA_PROOF_BATCH_OK);
    CettaProofBatchView *alien_parent = NULL;
    assert(cetta_proof_batch_seal_rank(alien, 0u, &alien_parent) == CETTA_PROOF_BATCH_OK);
    CettaProofBatchTransaction *alien_tx = NULL; CettaProofBatchOffer alien_offer;
    assert(cetta_proof_batch_begin(alien, &alien_tx) == CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_offer(alien_tx, alien_parent, 0u,
        key(f.universe,&f.syntax,"M"),key(f.universe,&f.syntax,"(proofs 3 1 (C a))"), &alien_offer) == CETTA_PROOF_BATCH_OK);
    assert(space_publish_proof_batch(&f.space, f.layout.head[0], alien_tx, NULL) == CETTA_PROOF_BATCH_DECLINED);
    assert(space_length64(&f.space) == 1u && f.space.revision == revision &&
        cetta_proof_batch_store_stats(alien).stored_rows == 1u);
    cetta_proof_batch_transaction_release(alien_tx); cetta_proof_batch_view_release(alien_parent);
    cetta_proof_batch_store_release(alien);

    assert(cetta_proof_batch_seal_rank(s, 0u, &parent) == CETTA_PROOF_BATCH_OK);
    CettaProofBatchTransaction *t = NULL; CettaProofBatchOffer offer;
    assert(cetta_proof_batch_begin(s, &t) == CETTA_PROOF_BATCH_OK);
    AtomId child = key(f.universe, &f.syntax, "(proofs 3 1 (C (pair $a $b)))");
    assert(cetta_proof_batch_offer(t, parent, 0u, key(f.universe, &f.syntax, "M"), child, &offer) == CETTA_PROOF_BATCH_OK);
    CettaProofBatchView *empty = NULL;
    assert(cetta_proof_batch_seal_rank(s, 1u, &empty) == CETTA_PROOF_BATCH_OK);
    assert(space_publish_proof_batch(&f.space, f.layout.head[0], t, NULL) == CETTA_PROOF_BATCH_STALE);
    assert(f.space.revision == revision && space_length64(&f.space) == 1u);
    cetta_proof_batch_transaction_release(t); cetta_proof_batch_view_release(empty);
    cetta_proof_batch_view_release(parent);
    assert(space_settle_proof_images(&f.space) == SPACE_PROOF_IMAGE_READY && !f.space.has_proof_image);
    assert(space_length64(&f.space) == 1u && atom_alpha_eq(space_get_at64(&f.space, 0u), parse(&f.syntax, expected[0])));
    cleanup(&f);
    init(&f);
    f.root = key(f.universe, &f.syntax, "(proofs 4 2 (C (pair $a $b)))");
    assert(promote(&f) == SPACE_PROOF_IMAGE_DECLINED && !f.space.has_proof_image);
    cleanup(&f);
    puts("PASS: promotion refusal/mismatched alias seed and stale publication leave actual Space unchanged; seed-only fallback neither loses nor duplicates row");
}
static void captured_ordinary_seed(void) {
    Fixture f; init(&f); Space overlay; space_init_overlay(&overlay, &f.space);
    uint64_t prefix = f.space.prefix_epoch;
    populate(&f);
    assert(space_length64(&overlay) == 1u && f.space.prefix_epoch == prefix);
    uint64_t revision = f.space.revision;
    assert(atom_alpha_eq(space_get_at64(&overlay, 0u), parse(&f.syntax, expected[0])));
    assert(space_length64(&overlay) == 1u && f.space.revision == revision);
    space_free(&overlay); bag(&f, false); cleanup(&f);
    puts("PASS: ordinary overlay captured before promotion keeps its sole seed occurrence and prefix through later proof appends/settlement");
}
static void captured_prefix_lifetime(void) {
    Fixture f; init(&f); assert(promote(&f) == SPACE_PROOF_IMAGE_READY);
    CettaProofBatchStore *s = store(&f); CettaProofBatchView *old = NULL, *parent = NULL;
    assert(cetta_proof_batch_capture(s, &old) == CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_seal_rank(s, 0u, &parent) == CETTA_PROOF_BATCH_OK);
    AtomId later = CETTA_ATOM_ID_NONE;
    for (unsigned i = 0u; i < 1024u; i++) {
        char child[96], label[32];
        snprintf(child, sizeof(child), "(proofs 3 %u (C (later %u)))", i, i);
        snprintf(label, sizeof(label), "Label%u", i);
        CettaProofBatchTransaction *t = NULL; CettaProofBatchOffer offer;
        assert(cetta_proof_batch_begin(s, &t) == CETTA_PROOF_BATCH_OK);
        later = key(f.universe, &f.syntax, child);
        assert(cetta_proof_batch_offer(t, parent, 0u, key(f.universe, &f.syntax, label), later, &offer) == CETTA_PROOF_BATCH_OK);
        assert(space_publish_proof_batch(&f.space, f.layout.head[0], t, NULL) == CETTA_PROOF_BATCH_OK);
        cetta_proof_batch_transaction_release(t);
    }
    assert(space_length64(&f.space) == 1025u && f.space.native.len == 1u);
    cetta_proof_batch_view_release(parent);
    watched_domain = f.domain; unsigned before = domain_frees;
    space_free(&f.space); arena_free(&f.syntax);
    cetta_term_domain_leave(&f.scope); cetta_term_domain_release(f.domain);
    CettaProofBatchDiagnostics d = cetta_proof_batch_diagnostics();
    assert(d.stores == 0u && d.transactions == 0u && d.views == 1u &&
        d.chunks == 1u && d.languages == 1u && d.roots == 2u && d.bytes == 2272u);
    assert(domain_frees == before);
    CettaTermDomainScope read = {0}; assert(cetta_term_domain_enter(f.domain, &read));
    assert(term_universe_collect(f.universe, NULL));
    assert(tu_hdr(f.universe, later) == NULL && cetta_proof_batch_view_current(old));
    CettaProofBatchCursor *cursor = NULL; assert(cetta_proof_batch_cursor_new(old, CETTA_PROOF_BATCH_ALL_STATES, &cursor) == CETTA_PROOF_BATCH_OK);
    CettaProofBudget budget = {64u, 0u}; CettaProofBatchWord pending;
    assert(cetta_proof_batch_cursor_peek(cursor, &budget, &pending) == CETTA_PROOF_BATCH_ROW &&
        pending.has_ordinal && pending.ordinal == 0u && pending.word.label_count == 0u && pending.word.state_key == f.root);
    assert(cetta_proof_batch_cursor_commit(cursor, pending.word.token));
    budget = (CettaProofBudget){64u, 0u};
    assert(cetta_proof_batch_cursor_peek(cursor, &budget, &pending) == CETTA_PROOF_BATCH_END);
    cetta_proof_batch_cursor_release(cursor); cetta_term_domain_leave(&read);
    cetta_proof_batch_view_release(old);
    assert(domain_frees == before + 1u && cetta_proof_batch_diagnostics().bytes == 0u);
    puts("PASS: actual Space destruction drops1024 later batches; tiny old capture retains2272 bytes and real term/symbol domain until its final release");
}
int main(void) {
    ordinary_observers(); refusal_retry(); promotion_and_publish_controls(); captured_ordinary_seed(); captured_prefix_lifetime();
    return 0;
}
