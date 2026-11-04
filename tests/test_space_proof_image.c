#include "proof_row.h"
#include "parser.h"
#include "symbol.h"
#include "fallible_alloc.h"
#include "shared_transition.h"
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Refuse the real fallible boundaries, leaving ordinary fixture allocation
 * alone. Each failed conversion is retried through the public Space API. */
static bool conversion;
static unsigned constructors, refuse_constructor, mallocs, refuse_malloc;
static bool refuse_payload, refuse_materialization;
static unsigned materializations;
CettaProofRowStatus __real_cetta_proof_row_materialize(
    TermUniverse *, const CettaProofRowLayout *, const CettaProofWord *,
    CettaProofRowRetain, void *, AtomId *, CettaProofRowWork *);
CettaProofRowStatus __wrap_cetta_proof_row_materialize(
    TermUniverse *u, const CettaProofRowLayout *layout, const CettaProofWord *word,
    CettaProofRowRetain retain, void *context, AtomId *out, CettaProofRowWork *work) {
    if (refuse_materialization) { materializations++; return CETTA_PROOF_ROW_CAPACITY; }
    return __real_cetta_proof_row_materialize(u, layout, word, retain, context, out, work);
}
static TermUniverse *collect_at_commit;
AtomId __real_tu_expr_from_ids(TermUniverse *, const AtomId *, CettaExprLen);
AtomId __wrap_tu_expr_from_ids(TermUniverse *u, const AtomId *ids, CettaExprLen n) {
    if (conversion) {
        assert(u->collection_inhibit && !term_universe_collect(u, NULL));
        if (++constructors == refuse_constructor) return CETTA_ATOM_ID_NONE;
    }
    return __real_tu_expr_from_ids(u, ids, n);
}
void *__real_cetta_fallible_malloc(size_t);
void *__wrap_cetta_fallible_malloc(size_t n) {
    if (conversion) {
        ++mallocs;
        if (mallocs == 4u && collect_at_commit)
            assert(term_universe_collect(collect_at_commit, NULL));
        if (mallocs == refuse_malloc) return NULL;
    }
    return __real_cetta_fallible_malloc(n);
}
void *__real_cetta_fallible_calloc(size_t, size_t);
void *__wrap_cetta_fallible_calloc(size_t n, size_t width) {
    if (refuse_payload) { refuse_payload = false; return NULL; }
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
static CettaProofRowLayout layout(TermUniverse *u, Arena *a) {
    return (CettaProofRowLayout){.proof = {.length = 2u, .child = {3u, 2u}},
        .arity = {4u, 3u}, .head = {parse(a, "proofs")->sym_id, parse(a, "C")->sym_id},
        .universe_instance = u->instance_id, .storage_epoch = u->storage_epoch};
}
static CettaProofImage *image(TermUniverse *u, Arena *a) {
    AtomId seed = key(u, a, "I"), one = key(u, a, "1");
    AtomId m = key(u, a, "M"), three = key(u, a, "3");
    AtomId root = key(u, a, "(proofs 4 2 (C (pair $a $a)))");
    AtomId k1 = key(u, a, "(proofs 3 1 (C (pair $a $b)))");
    AtomId k2 = key(u, a, "(proofs 2 0 (C (pair $b $a)))");
    CettaProofBuilder *b = NULL; CettaProofImage *im = NULL;
    assert(cetta_proof_builder_new(u, root, seed,
        (CettaProofLimits){8u, 8u, 4u, 100u, 100u}, &b) == CETTA_PROOF_OK);
    uint32_t n1, n2; CettaProofOffer offer;
    assert(cetta_proof_builder_state(b, k1, &n1) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_offer(b, 0u, one, n1, &offer) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_offer(b, 0u, m, n1, &offer) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_advance(b) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_state(b, k2, &n2) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_offer(b, n1, three, n2, &offer) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_seal(b, &im) == CETTA_PROOF_OK);
    cetta_proof_builder_free(b); return im;
}
static const char *expected[] = {
    "(proofs 4 2 (C (pair $a $a) I))",
    "(proofs 3 1 (C (pair $a $b) (1 I)))",
    "(proofs 3 1 (C (pair $a $b) (M I)))",
    "(proofs 2 0 (C (pair $b $a) (3 (1 I))))",
    "(proofs 2 0 (C (pair $b $a) (3 (M I))))"
};
static void bag(Space *s, Arena *a, bool prefix) {
    unsigned seen[5] = {0};
    assert(space_length64(s) == 5u + prefix);
    for (CettaIndex i = 0u; i < space_length64(s); i++) {
        Atom *row = space_get_at64(s, i); assert(row);
        if (prefix && atom_alpha_eq(row, parse(a, "(unrelated 9)"))) continue;
        bool found = false;
        for (unsigned j = 0; j < 5u; j++)
            if (atom_alpha_eq(row, parse(a, expected[j]))) { seen[j]++; found = true; break; }
        assert(found);
    }
    for (unsigned j = 0u; j < 5u; j++) assert(seen[j] == 1u);
}
static void attach(Space *s, TermUniverse *u, Arena *a, CettaProofImage *im,
                   CettaProofRowLayout *l) {
    space_init_with_universe(s, u); s->kind = SPACE_KIND_SET;
    uint64_t revision = s->revision, equation = s->equation_revision;
    assert(space_attach_proof_image(s, l->head[0], l, im) == SPACE_PROOF_IMAGE_READY);
    assert(s->revision == revision + 1u && s->equation_revision == equation);
    assert(s->native.has_proof_image && s->has_proof_image);
    assert(space_has_proof_image(s, l->head[0]) && space_length64(s) == 5u);
    (void)a;
}
static void marker_lifecycle(TermUniverse *u, CettaProofImage *im,
                             CettaProofRowLayout *l) {
    Space s = {.native.has_proof_image = true};
    space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
    assert(!s.native.has_proof_image && !s.has_proof_image);
    assert(space_attach_proof_image(&s, l->head[0], l, im) == SPACE_PROOF_IMAGE_READY);
    assert(s.native.has_proof_image && s.has_proof_image);
    space_free(&s);
    assert(!s.native.has_proof_image && !s.has_proof_image);
    puts("PASS: cached proof presence resets on init/free and tracks actual attachment");
}
static void observation_controls(TermUniverse *u, Arena *a, CettaProofImage *im,
                                 CettaProofRowLayout *l) {
    Space s; space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
    Atom *unrelated = parse(a, "(unrelated 9)"); space_add(&s, unrelated);
    uint64_t revision = s.revision;
    assert(space_attach_proof_image(&s, l->head[0], l, im) == SPACE_PROOF_IMAGE_READY);
    assert(s.revision == revision + 1u && s.native.len == 1u);
    revision = s.revision;
    uint64_t equation = s.equation_revision, declaration = s.declaration_revision, prefix = s.prefix_epoch;
    Atom *pattern = parse(a, "(proofs $rank $hyp $payload)");
    uint64_t count = 0u; CettaIndex examined = 99u;
    conversion = true; constructors = 0u;
    assert(space_match_count_flat_linear64(&s, a, pattern, &count, &examined));
    conversion = false;
    assert(count == 5u && constructors == 0u && s.native.len == 1u);
    assert(space_has_proof_image(&s, l->head[0]));
    u->storage_epoch++;
    assert(!space_match_count_flat_linear64(&s, a, pattern, &count, &examined));
    assert(space_match_backend_last_error_code() == SPACE_MATCH_BACKEND_ERROR_STORAGE_INVALIDATED);
    assert(space_has_proof_image(&s, l->head[0]) && s.native.len == 1u);
    u->storage_epoch--;
    space_match_backend_clear_error();
    assert(space_match_count_flat_linear64(&s, a, pattern, &count, &examined) && count == 5u);
    assert(!space_dense_patterns_miss(&s, &pattern, 1u));
    assert(!space_contains_only_exact_atoms(&s));
    assert(space_contains_exact(&s, unrelated));
    assert(space_has_proof_image(&s, l->head[0]));
    SpaceOccurrenceCursor reader; space_occurrence_cursor_init_empty(&reader);
    assert(space_occurrence_cursor_init(&s, unrelated, &reader));
    assert(space_has_proof_image(&s, l->head[0]));
    uint64_t before_fault = s.revision;
    collect_at_commit = u;
    constructors = mallocs = 0u; refuse_malloc = 4u; conversion = true;
    assert(space_settle_proof_images(&s) == SPACE_PROOF_IMAGE_CAPACITY);
    conversion = false; refuse_malloc = 0u; collect_at_commit = NULL;
    assert(s.revision == before_fault && s.native.len == 1u && space_length64(&s) == 6u);
    assert(space_has_proof_image(&s, l->head[0]));
    /* Another Space's dense borrow cannot forbid this independent image's
     * conversion: PROOF has no borrowed tuple/skeleton pointers. */
    space_dense_reading_begin();
    assert(space_get_at64(&s, 1u));
    space_dense_reading_end();
    assert(!s.native.has_proof_image && !s.has_proof_image);
    assert(!space_has_proof_image(&s, l->head[0])); bag(&s, a, true);
    CettaIndex captured_index = 99u; Atom *captured = NULL;
    assert(space_occurrence_cursor_next(&reader, &captured_index, &captured) == SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(atom_alpha_eq(captured, unrelated));
    assert(space_occurrence_cursor_next(&reader, &captured_index, &captured) == SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&reader);
    assert(s.revision == revision && s.equation_revision == equation &&
        s.declaration_revision == declaration && s.prefix_epoch == prefix);
    assert(space_contains_for_add_nodup(&s, parse(a, expected[0])));
    space_add(&s, parse(a, expected[0])); assert(space_length64(&s) == 6u);
    assert(space_remove(&s, parse(a, expected[0]))); assert(space_length64(&s) == 5u);
    space_add(&s, parse(a, expected[0])); bag(&s, a, true);
    space_free(&s);
    puts("PASS: proof image length/count, unrelated readers, complete alias bags and same-bag revision boundaries");
}
static SpaceProofImageStatus settle(Space *s, unsigned constructor, unsigned allocation) {
    constructors = mallocs = 0u; refuse_constructor = constructor; refuse_malloc = allocation;
    conversion = true; SpaceProofImageStatus result = space_settle_proof_images(s); conversion = false;
    refuse_constructor = refuse_malloc = 0u; return result;
}
static void refusal_controls(TermUniverse *u, Arena *a, CettaProofImage *im,
                             CettaProofRowLayout *l) {
    Space s; attach(&s, u, a, im, l);
    assert(settle(&s, 0u, 0u) == SPACE_PROOF_IMAGE_READY);
    unsigned total = constructors, allocations = mallocs;
    assert(total == 16u && allocations == 4u); bag(&s, a, false); space_free(&s);
    for (unsigned n = 1u; n <= total; n++) {
        attach(&s, u, a, im, l); uint64_t revision = s.revision;
        assert(settle(&s, n, 0u) == SPACE_PROOF_IMAGE_CAPACITY);
        assert(space_match_backend_last_error_code() == SPACE_MATCH_BACKEND_ERROR_ALLOCATION_FAILED);
        assert(space_has_proof_image(&s, l->head[0]) && s.native.len == 0u &&
            space_length64(&s) == 5u && s.revision == revision);
        assert(settle(&s, 0u, 0u) == SPACE_PROOF_IMAGE_READY); bag(&s, a, false);
        assert(s.revision == revision); space_free(&s);
    }
    for (unsigned n = 1u; n <= allocations; n++) {
        attach(&s, u, a, im, l); uint64_t revision = s.revision;
        assert(settle(&s, 0u, n) == SPACE_PROOF_IMAGE_CAPACITY);
        assert(space_has_proof_image(&s, l->head[0]) && s.native.len == 0u && s.revision == revision);
        assert(settle(&s, 0u, 0u) == SPACE_PROOF_IMAGE_READY); bag(&s, a, false); space_free(&s);
    }
    attach(&s, u, a, im, l); Atom *pattern = parse(a, "(proofs $a $b $c)");
    CettaNativeQuery q = {.source = &s, .patterns = &pattern, .pattern_count = 1u,
        .template_atom = pattern, .consumer = CETTA_NATIVE_QUERY_COLLECT};
    CettaNativeQueryResult result;
    constructors = 0u; refuse_constructor = 1u; conversion = true;
    assert(cetta_native_query_run(&q, a, &result) == CETTA_NATIVE_QUERY_FAILED);
    conversion = false; refuse_constructor = 0u;
    assert(space_has_proof_image(&s, l->head[0]) && result.answers == 0u);
    /* Explicit rows name their own observation source. The Space supplies
     * the TU only, so its unrelated stored image cannot refuse this query. */
    q.rows = parse(a, "((proofs 99 8 value))");
    refuse_materialization = true; materializations = 0u;
    assert(cetta_native_query_run(&q, a, &result) == CETTA_NATIVE_QUERY_DONE);
    assert(result.answers == 1u && result.collection && result.collection->kind == ATOM_EXPR &&
        result.collection->expr.len == 1u &&
        atom_alpha_eq(result.collection->expr.elems[0], parse(a, "(proofs 99 8 value)")));
    assert(materializations == 0u && space_has_proof_image(&s, l->head[0]) && s.native.len == 0u);
    q.rows = NULL;
    assert(cetta_native_query_run(&q, a, &result) == CETTA_NATIVE_QUERY_FAILED);
    assert(materializations == 1u && space_has_proof_image(&s, l->head[0]));
    refuse_materialization = false;
    BindingSet matches; binding_set_init(&matches);
    space_query_conjunction(&s, a, &pattern, 1u, NULL, &matches);
    assert(matches.len == 5u); binding_set_free(&matches); space_free(&s);
    space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
    uint64_t revision = s.revision; refuse_payload = true;
    assert(space_attach_proof_image(&s, l->head[0], l, im) == SPACE_PROOF_IMAGE_CAPACITY);
    assert(space_length64(&s) == 0u && s.revision == revision);
    assert(space_attach_proof_image(&s, l->head[0], l, im) == SPACE_PROOF_IMAGE_READY);
    space_free(&s);
    printf("PASS: %u constructor and %u destination/commit refusal boundaries retain image and retry; native failure is not empty success\n", total, allocations);
}
static CettaProofImage *single(TermUniverse *u, AtomId row, AtomId seed, unsigned depth, AtomId label) {
    CettaProofBuilder *b = NULL; CettaProofImage *im = NULL;
    assert(cetta_proof_builder_new(u, row, seed,
        (CettaProofLimits){128u, 128u, 128u, 1000u, 1000u}, &b) == CETTA_PROOF_OK);
    uint32_t parent = 0u;
    for (unsigned i = 0u; i < depth; i++) {
        uint32_t child; CettaProofOffer offer;
        assert(cetta_proof_builder_state(b, row, &child) == CETTA_PROOF_OK);
        assert(cetta_proof_builder_offer(b, parent, label, child, &offer) == CETTA_PROOF_OK);
        if (i + 1u < depth) assert(cetta_proof_builder_advance(b) == CETTA_PROOF_OK);
        parent = child;
    }
    assert(cetta_proof_builder_seal(b, &im) == CETTA_PROOF_OK);
    cetta_proof_builder_free(b); return im;
}
static void decoder_boundaries(TermUniverse *u, Arena *a) {
    AtomId seed = key(u, a, "I"), label = key(u, a, "M");
    SymbolId head = parse(a, "wide")->sym_id;
    CettaProofRowLayout l = {.proof = {.length = 1u, .child = {1u}},
        .arity = {2u}, .head = {head}, .universe_instance = u->instance_id,
        .storage_epoch = u->storage_epoch};
    AtomId head_id = key(u, a, "wide"), erased = tu_expr_from_ids(u, &head_id, 1u);
    for (unsigned depth = 64u; depth <= 65u; depth++) {
        CettaProofImage *im = single(u, erased, seed, depth, label);
        Space s; space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
        SpaceProofImageStatus status = space_attach_proof_image(&s, head, &l, im);
        assert(status == (depth == 64u ? SPACE_PROOF_IMAGE_READY : SPACE_PROOF_IMAGE_DECLINED));
        if (depth == 64u) {
            assert(space_length64(&s) == 65u);
            assert(space_settle_proof_images(&s) == SPACE_PROOF_IMAGE_READY);
            assert(tu_arity(u, space_get_atom_id_at64(&s, 64u)) == 2u);
        } else assert(space_length64(&s) == 0u);
        space_free(&s); cetta_proof_image_release(im);
    }
    /* A small graph can denote a bag whose rooted-row realization cannot
     * even be addressed. Admission must reject it before publication. */
    CettaProofBuilder *huge_builder = NULL; CettaProofImage *huge = NULL;
    assert(cetta_proof_builder_new(u, erased, seed,
        (CettaProofLimits){128u, 128u, 128u, UINT64_MAX, UINT64_MAX}, &huge_builder) == CETTA_PROOF_OK);
    uint32_t parent = 0u;
    for (unsigned i = 0u; i < 61u; i++) {
        uint32_t child; CettaProofOffer offer;
        assert(cetta_proof_builder_state(huge_builder, erased, &child) == CETTA_PROOF_OK);
        assert(cetta_proof_builder_offer(huge_builder, parent, seed, child, &offer) == CETTA_PROOF_OK);
        assert(cetta_proof_builder_offer(huge_builder, parent, label, child, &offer) == CETTA_PROOF_OK);
        if (i < 60u) assert(cetta_proof_builder_advance(huge_builder) == CETTA_PROOF_OK);
        parent = child;
    }
    assert(cetta_proof_builder_seal(huge_builder, &huge) == CETTA_PROOF_OK);
    cetta_proof_builder_free(huge_builder);
    assert(cetta_proof_image_stats(huge).support > SIZE_MAX / sizeof(AtomId));
    Space huge_space; space_init_with_universe(&huge_space, u); huge_space.kind = SPACE_KIND_SET;
    assert(space_attach_proof_image(&huge_space, head, &l, huge) == SPACE_PROOF_IMAGE_DECLINED);
    assert(space_length64(&huge_space) == 0u); space_free(&huge_space); cetta_proof_image_release(huge);
    AtomId *kids = malloc(4096u * sizeof(*kids)); assert(kids);
    kids[0] = head_id;
    for (unsigned i = 1u; i < 4096u; i++) kids[i] = seed;
    for (unsigned arity = 4096u; arity <= 4097u; arity++) {
        l.arity[0] = arity; l.proof.child[0] = arity - 1u;
        erased = tu_expr_from_ids(u, kids, arity - 1u); assert(erased != CETTA_ATOM_ID_NONE);
        CettaProofImage *im = single(u, erased, seed, 0u, label);
        Space s; space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
        assert(space_attach_proof_image(&s, head, &l, im) ==
            (arity == 4096u ? SPACE_PROOF_IMAGE_READY : SPACE_PROOF_IMAGE_DECLINED));
        if (arity == 4096u) {
            assert(space_settle_proof_images(&s) == SPACE_PROOF_IMAGE_READY);
            AtomId row = space_get_atom_id_at64(&s, 0u);
            assert(tu_arity(u, row) == arity && tu_child(u, row, arity - 1u) == seed);
        } else assert(space_length64(&s) == 0u);
        space_free(&s); cetta_proof_image_release(im);
    }
    free(kids);
    SymbolId roles[] = {g_builtin_syms.equals, g_builtin_syms.equals_percent,
        g_builtin_syms.colon, g_builtin_syms.subtype};
    for (unsigned i = 0u; i < sizeof(roles) / sizeof(*roles); i++) {
        Atom *symbol = atom_symbol(a, symbol_bytes(g_symbols, roles[i]));
        AtomId parts[] = {term_universe_store_alpha_key(u, symbol), seed};
        erased = tu_expr_from_ids(u, parts, 2u);
        CettaProofImage *im = single(u, erased, seed, 0u, label);
        l.head[0] = roles[i]; l.arity[0] = 3u; l.proof.child[0] = 2u;
        Space s; space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
        assert(space_attach_proof_image(&s, roles[i], &l, im) == SPACE_PROOF_IMAGE_DECLINED);
        assert(space_length64(&s) == 0u); space_free(&s); cetta_proof_image_release(im);
    }
    puts("PASS: decoder rank64/65, child-slot4096/4097 and equation/declaration role boundaries");
}
static void fallback_controls(TermUniverse *u, Arena *a, CettaProofImage *im,
                              CettaProofRowLayout *l) {
    Space s; attach(&s, u, a, im, l);
    Atom *repeated = parse(a, "(proofs $same $same $value)"); CettaCount count = 99u;
    assert(!space_dense_count_columns(&s, repeated->expr.elems, repeated->expr.len, &count));
    Space *sum = NULL;
    assert(space_heap_additive_copy(u, &s, &s, &sum) == SPACE_ADDITIVE_COPY_UNSUPPORTED && !sum);
    assert(space_has_proof_image(&s, l->head[0]));
    Space *clone = space_heap_clone_shallow(&s); assert(clone); bag(clone, a, false);
    assert(!space_has_proof_image(&s, l->head[0])); space_free(clone); free(clone); space_free(&s);
    attach(&s, u, a, im, l);
    collect_at_commit = u; assert(settle(&s, 0u, 0u) == SPACE_PROOF_IMAGE_READY); collect_at_commit = NULL;
    bag(&s, a, false); space_free(&s);
    attach(&s, u, a, im, l);
    uint64_t revision = s.revision;
    conversion = true; constructors = 0u; refuse_constructor = 1u;
    assert(!space_truncate64(&s, 0u)); conversion = false; refuse_constructor = 0u;
    assert(s.revision == revision && space_length64(&s) == 5u && space_has_proof_image(&s, l->head[0]));
    assert(space_truncate64(&s, 0u)); assert(space_length64(&s) == 0u); space_free(&s);
    attach(&s, u, a, im, l);
    Space overlay; space_init_overlay(&overlay, &s);
    Atom *pattern = parse(a, "(proofs $r $h $p)"); BindingSet answers; binding_set_init(&answers);
    space_query_conjunction(&overlay, a, &pattern, 1u, NULL, &answers);
    assert(answers.len == 5u); binding_set_free(&answers);
    space_free(&overlay); space_free(&s);
    puts("PASS: repeated-variable decline, copy/overlay/mutation fallbacks and actual GC before commit");
}
static void admission_controls(TermUniverse *u, Arena *a, CettaProofImage *im,
                               CettaProofRowLayout l) {
    Space s; space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
    CettaProofRowLayout wrong = l; wrong.head[1] = parse(a, "wrong")->sym_id;
    assert(space_attach_proof_image(&s, l.head[0], &wrong, im) == SPACE_PROOF_IMAGE_DECLINED);
    wrong = l; wrong.storage_epoch++;
    assert(space_attach_proof_image(&s, l.head[0], &wrong, im) == SPACE_PROOF_IMAGE_DECLINED);
    space_add(&s, parse(a, expected[0]));
    assert(space_attach_proof_image(&s, l.head[0], &l, im) == SPACE_PROOF_IMAGE_DECLINED);
    space_free(&s);
    TermUniverse other; term_universe_init(&other);
    space_init_with_universe(&s, &other); s.kind = SPACE_KIND_SET;
    wrong = l; wrong.universe_instance = other.instance_id; wrong.storage_epoch = other.storage_epoch;
    assert(space_attach_proof_image(&s, l.head[0], &wrong, im) == SPACE_PROOF_IMAGE_DECLINED);
    space_free(&s); term_universe_free(&other);
    puts("PASS: stale/foreign universe, wrong layout and nonempty head decline before publication");
}
static struct {
    unsigned calls;
    bool install;
    CettaProofImage *image;
    CettaProofRowLayout *layout;
} proof_store_callback;
static bool install_then_decline_id(Space *s, AtomId id, Atom *atom) {
    (void)id; (void)atom; proof_store_callback.calls++;
    if (proof_store_callback.install) {
        proof_store_callback.install = false;
        assert(space_attach_proof_image(s, proof_store_callback.layout->head[0],
            proof_store_callback.layout, proof_store_callback.image) == SPACE_PROOF_IMAGE_READY);
    }
    return false;
}
static bool install_then_decline_atom(Space *s, Atom *atom) {
    return install_then_decline_id(s, CETTA_ATOM_ID_NONE, atom);
}
static bool observe_then_decline_atom(Space *s, Atom *atom) {
    (void)s; (void)atom; proof_store_callback.calls++; return false;
}
static void selected_storage_controls(TermUniverse *u, Arena *a, CettaProofImage *im,
                                      CettaProofRowLayout *l) {
    Space s, view; attach(&s, u, a, im, l);
    assert(space_init_module_own_view(&view, &s));
    Atom *fresh = parse(a, "(fresh 1)"); uint64_t revision = s.revision, viewed = view.revision;
    refuse_materialization = true;
    space_add(&view, fresh);
    assert(space_has_proof_image(&s, l->head[0]) && space_length64(&s) == 5u &&
        space_length64(&view) == 5u && s.revision == revision && view.revision == viewed);
    refuse_materialization = false;
    space_add(&view, fresh);
    assert(space_length64(&s) == 6u && space_length64(&view) == 6u &&
        space_contains_exact(&s, fresh));
    space_free(&view); space_free(&s);

    attach(&s, u, a, im, l);
    SpaceMatchBackendOps ops = *s.match_backend.ops;
    ops.store_atom_direct = observe_then_decline_atom; s.match_backend.ops = &ops;
    proof_store_callback.calls = 0u; refuse_materialization = true;
    assert(!space_admit_atom(&s, NULL, fresh) && proof_store_callback.calls == 0u);
    assert(space_length64(&s) == 5u && space_has_proof_image(&s, l->head[0]));
    refuse_materialization = false;
    assert(space_admit_atom(&s, NULL, fresh) && proof_store_callback.calls == 1u);
    assert(space_length64(&s) == 6u);
    assert(space_admit_atom(&s, NULL, fresh)); /* accepted duplicate, not a new row */
    assert(space_length64(&s) == 6u); space_free(&s);

    space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
    ops = *s.match_backend.ops; ops.store_atom_direct = install_then_decline_atom;
    s.match_backend.ops = &ops;
    proof_store_callback.calls = 0u; proof_store_callback.install = true;
    proof_store_callback.image = im; proof_store_callback.layout = l;
    refuse_materialization = true;
    assert(!space_admit_atom(&s, NULL, fresh));
    assert(proof_store_callback.calls == 1u && space_length64(&s) == 5u &&
        space_has_proof_image(&s, l->head[0]));
    refuse_materialization = false;
    assert(space_admit_atom(&s, NULL, fresh));
    assert(proof_store_callback.calls == 2u && space_length64(&s) == 6u);
    space_free(&s);

    space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
    ops = *s.match_backend.ops; ops.store_atom_id_direct = install_then_decline_id;
    s.match_backend.ops = &ops;
    AtomId id = key(u, a, "(fresh 1)");
    proof_store_callback.calls = 0u; proof_store_callback.install = true;
    proof_store_callback.image = im; proof_store_callback.layout = l;
    revision = s.revision; refuse_materialization = true;
    space_add_atom_id(&s, id);
    assert(proof_store_callback.calls == 1u && space_has_proof_image(&s, l->head[0]) &&
        space_length64(&s) == 5u && s.native.len == 0u && s.revision == revision + 1u);
    refuse_materialization = false;
    space_add_atom_id(&s, id);
    assert(proof_store_callback.calls == 2u && !space_has_proof_image(&s, l->head[0]) &&
        space_length64(&s) == 6u && space_contains_exact(&s, fresh));
    space_free(&s);
    puts("PASS: selected module storage and admission preflight preserve callback effects without replay");
}
static unsigned order_materializations;
static bool refuse_order_materialization(Space *s, Arena *arena) {
    (void)s; (void)arena; order_materializations++; return false;
}
static void absent_store_order_controls(TermUniverse *u, Arena *a) {
    Atom *equation = parse(a, "(= (f $x) $x)");
    AtomId id = term_universe_store_alpha_key(u, equation); assert(id != CETTA_ATOM_ID_NONE);
    for (unsigned direct_id = 0u; direct_id < 2u; direct_id++) {
        Space s; space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
        SpaceMatchBackendOps ops = *s.match_backend.ops;
        assert(!ops.store_atom_direct && !ops.store_atom_id_direct);
        ops.materialize_native_storage = refuse_order_materialization;
        s.match_backend.ops = &ops; s.match_backend.kind = SPACE_ENGINE_PATHMAP;
        order_materializations = 0u;
        if (direct_id) space_add_atom_id(&s, id);
        else space_add(&s, equation);
        assert(order_materializations == 1u && s.native.len == 0u &&
            !s.match_backend.pathmap.bridge.preserve_logical_order);
        /* The controlled backend declined before adopting any storage. */
        s.match_backend.kind = SPACE_ENGINE_NATIVE;
        space_free(&s);
    }
    puts("PASS: absent store slots preserve PathMap authored-order handoff and failed materialization");
}
typedef struct { Space *space; _Atomic bool start; } ConcurrentProofRead;
static void *settle_concurrently(void *context) {
    ConcurrentProofRead *read = context;
    while (!atomic_load_explicit(&read->start, memory_order_acquire)) sched_yield();
    cetta_shared_transition_scope_enter();
    assert(space_settle_proof_images(read->space) == SPACE_PROOF_IMAGE_READY);
    cetta_shared_transition_scope_leave();
    return NULL;
}
static void concurrent_marker_control(TermUniverse *u, Arena *a, CettaProofImage *im,
                                      CettaProofRowLayout *l) {
    Space s; space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
    cetta_shared_transition_scope_enter();
    assert(space_attach_proof_image(&s, l->head[0], l, im) == SPACE_PROOF_IMAGE_DECLINED);
    cetta_shared_transition_scope_leave();
    assert(space_length64(&s) == 0u);
    assert(space_attach_proof_image(&s, l->head[0], l, im) == SPACE_PROOF_IMAGE_READY);
    ConcurrentProofRead read = {.space = &s}; atomic_init(&read.start, false);
    pthread_t writer; assert(!pthread_create(&writer, NULL, settle_concurrently, &read));
    cetta_shared_transition_scope_enter();
    atomic_store_explicit(&read.start, true, memory_order_release);
    for (unsigned i = 0u; i < 512u; i++) {
        /* Only the representation marker is observed outside the physical
         * guard. Ordinary rows/TU access retains its existing serialization. */
        (void)space_has_proof_image(&s, SYMBOL_ID_NONE);
        CETTA_SCOPED_SHARED_TRANSITION(physical_read);
        assert(space_length64(&s) == 5u);
        AtomId row = space_get_atom_id_at64(&s, 0u);
        assert(row != CETTA_ATOM_ID_NONE && tu_hdr(u, row));
    }
    cetta_shared_transition_scope_leave(); assert(!pthread_join(writer, NULL));
    assert(!atomic_load_explicit(&s.native.has_proof_image, memory_order_acquire));
    bag(&s, a, false); space_free(&s);
    Space src, dst; attach(&src, u, a, im, l);
    space_init_with_universe(&dst, u); dst.kind = SPACE_KIND_SET;
    space_add(&dst, parse(a, "(old 7)"));
    space_replace_contents(&dst, &src);
    assert(!atomic_load_explicit(&src.native.has_proof_image, memory_order_acquire));
    assert(!atomic_load_explicit(&dst.native.has_proof_image, memory_order_acquire));
    assert(space_length64(&src) == 0u); bag(&dst, a, false);
    space_free(&src); space_free(&dst);
    puts("PASS: atomic marker publication during serialized settlement and native storage transfer");
}
static void reset_ownership_control(Arena *a) {
    Arena before, after; arena_init_detached(&before); arena_init_detached(&after);
    TermUniverse u; term_universe_init(&u); term_universe_set_persistent_arena(&u, &before);
    term_universe_set_collection_mode(&u, TERM_UNIVERSE_COLLECTION_RECLAIM);
    CettaProofImage *im = image(&u, a); CettaProofRowLayout l = layout(&u, a);
    CettaProofStateView state; assert(cetta_proof_image_state(im, 0u, &state));
    AtomId borrowed_head = tu_child(&u, state.key, 0u);
    Space old_space; attach(&old_space, &u, a, im, &l); cetta_proof_image_release(im);
    uint64_t old_epoch = u.storage_epoch;
    term_universe_set_persistent_arena(&u, &after);
    assert(u.storage_epoch != old_epoch);
    while (u.len <= borrowed_head) {
        char name[80]; snprintf(name, sizeof(name), "replacement_%zu", u.len);
        AtomId id = tu_intern_symbol(&u, symbol_intern_cstr(g_symbols, name));
        assert(id != CETTA_ATOM_ID_NONE);
    }
    Space replacement; space_init_with_universe(&replacement, &u);
    space_add_atom_id(&replacement, borrowed_head);
    Atom *pattern = parse(a, "(proofs $a $b $c)"); uint64_t count = 99u; CettaIndex examined = 99u;
    assert(!space_match_count_flat_linear64(&old_space, a, pattern, &count, &examined));
    assert(space_match_backend_last_error_code() == SPACE_MATCH_BACKEND_ERROR_STORAGE_INVALIDATED);
    assert(term_universe_collect(&u, NULL));
    assert(u.counting && u.counts[borrowed_head] == 1u);
    space_free(&old_space);
    assert(u.counts[borrowed_head] == 1u);
    assert(term_universe_collect(&u, NULL) && tu_hdr(&u, borrowed_head));
    assert(space_get_atom_id_at64(&replacement, 0u) == borrowed_head);
    space_free(&replacement); term_universe_free(&u); arena_free(&before); arena_free(&after);
    puts("PASS: actual TU storage reset neither lists nor drops a reused borrowed PROOF head coordinate");
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols; g_hashcons = NULL;
    VarInternTable vars; var_intern_init(&vars); g_var_intern = &vars;
    Arena arena, persistent; arena_init_detached(&arena); arena_init_detached(&persistent);
    TermUniverse u; term_universe_init(&u); term_universe_set_persistent_arena(&u, &persistent);
    term_universe_set_collection_mode(&u, TERM_UNIVERSE_COLLECTION_RECLAIM);
    CettaProofRowLayout l = layout(&u, &arena); CettaProofImage *im = image(&u, &arena);
    marker_lifecycle(&u, im, &l);
    observation_controls(&u, &arena, im, &l);
    refusal_controls(&u, &arena, im, &l);
    admission_controls(&u, &arena, im, l);
    fallback_controls(&u, &arena, im, &l);
    decoder_boundaries(&u, &arena);
    selected_storage_controls(&u, &arena, im, &l);
    absent_store_order_controls(&u, &arena);
    concurrent_marker_control(&u, &arena, im, &l);
    Space retained; attach(&retained, &u, &arena, im, &l);
    cetta_proof_image_release(im); im = NULL;
    assert(term_universe_collect(&u, NULL));
    assert(space_length64(&retained) == 5u && space_settle_proof_images(&retained) == SPACE_PROOF_IMAGE_READY);
    bag(&retained, &arena, false); space_free(&retained);
    assert(term_universe_collect(&u, NULL));
    term_universe_free(&u);
    reset_ownership_control(&arena);
    arena_free(&arena); arena_free(&persistent);
    g_var_intern = NULL; var_intern_free(&vars); g_symbols = NULL; symbol_table_free(&symbols);
    puts("PASS: native Space proof storage owns roots through caller release and actual reclamation");
    return 0;
}
