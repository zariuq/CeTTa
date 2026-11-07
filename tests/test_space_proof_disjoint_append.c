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
        .arity = {4u, 3u}, .head = {atom_symbol_current_id(parse(a, "proofs")), atom_symbol_current_id(parse(a, "C"))},
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

#include "u64_index.h"

/* Refuse real index allocations without touching unrelated fixture/TU
 * allocation. The index's production resource-return path remains in use. */
static bool inside_index;
static unsigned index_allocations, fail_index_allocation;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t n) {
    if (inside_index && ++index_allocations == fail_index_allocation) return NULL;
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t w) {
    if (inside_index && ++index_allocations == fail_index_allocation) return NULL;
    return __real_calloc(n, w);
}
void *__wrap_realloc(void *p, size_t n) {
    if (inside_index && ++index_allocations == fail_index_allocation) return NULL;
    return __real_realloc(p, n);
}
CettaGsltU32IndexInsertResultV1 __real_cetta_u64_index_insert(CettaU64Index *, uint64_t, uint32_t);
CettaGsltU32IndexInsertResultV1 __wrap_cetta_u64_index_insert(CettaU64Index *i, uint64_t k, uint32_t v) {
    bool outer = inside_index; inside_index = true;
    CettaGsltU32IndexInsertResultV1 result = __real_cetta_u64_index_insert(i, k, v);
    inside_index = outer; return result;
}

static bool refuse_capture_registration;
static unsigned capture_registration_refusals;
bool __real_term_universe_add_root_provider(TermUniverse *,TermUniverseRootProvider,void *);
bool __wrap_term_universe_add_root_provider(TermUniverse *u,TermUniverseRootProvider fn,void *ctx){
    if(refuse_capture_registration){refuse_capture_registration=false;capture_registration_refusals++;return false;}
    return __real_term_universe_add_root_provider(u,fn,ctx);
}

static void cursor_values(SpaceOccurrenceCursor *cursor, Arena *a,
                           const char *const *expected_rows, size_t n) {
    bool seen[16] = {0}; size_t count = 0u;
    Atom *pattern = parse(a, expected_rows[0]);
    CettaIndex ceiling = cursor->ceiling;
    for (;;) {
        Atom *row = NULL; CettaIndex ordinal = UINT64_MAX;
        SpaceOccurrenceCursorStep step = space_occurrence_cursor_next(cursor, &ordinal, &row);
        if (step == SPACE_OCCURRENCE_CURSOR_END) break;
        assert(step == SPACE_OCCURRENCE_CURSOR_ITEM && row && ordinal < ceiling);
        /* Occurrence cursors return candidates. This fixture's patterns are
         * literal heads over independent variables, so head filtering is exact. */
        if (row->kind != ATOM_EXPR || row->expr.len == 0u ||
            !atom_eq(row->expr.elems[0], pattern->expr.elems[0])) continue;
        assert(count++ < n);
        bool found = false;
        for (size_t j = 0u; j < n; j++)
            if (atom_alpha_eq(row, parse(a, expected_rows[j]))) {
                assert(!seen[j]); seen[j] = true; found = true; break;
            }
        if (!found) { fputs("unexpected captured row: ", stderr); atom_print(row,stderr); fprintf(stderr," (expected %s, ordinal %llu)\n",expected_rows[0],(unsigned long long)ordinal); }
        assert(found);
    }
    assert(count == n);
}
static void check_words(TermUniverse *u, CettaProofCursor *cursor, Arena *a) {
    CettaProofBudget budget = {1000u, 0u}; unsigned seen[5] = {0};
    unsigned count = 0u;
    const char *states[] = {"(proofs 4 2 (C (pair $a $a)))",
        "(proofs 3 1 (C (pair $a $b)))", "(proofs 2 0 (C (pair $b $a)))"};
    for (;;) {
        CettaProofWord word; CettaProofNext next = cetta_proof_cursor_peek(cursor, &budget, &word);
        if (next == CETTA_PROOF_END) break;
        assert(next == CETTA_PROOF_ROW && count++ < 5u && word.state < 3u);
        assert(word.state_key == key(u, a, states[word.state]));
        AtomId labels[2] = {0};
        if (word.label_count) labels[0] = word.outer_labels[0];
        if (word.label_count == 2u) labels[1] = word.outer_labels[1];
        unsigned which = word.state == 0u ? 0u : word.state == 1u
            ? (tu_kind(u, labels[0]) == ATOM_GROUNDED && tu_int(u, labels[0]) == 1 ? 1u : 2u)
            : (tu_kind(u, labels[1]) == ATOM_GROUNDED && tu_int(u, labels[1]) == 1 ? 3u : 4u);
        assert(which < 5u && seen[which]++ == 0u);
        if (which == 0u) assert(word.label_count == 0u);
        else if (which <= 2u) assert(word.label_count == 1u);
        else assert(word.label_count == 2u && tu_kind(u, labels[0]) == ATOM_GROUNDED && tu_int(u, labels[0]) == 3);
        if (which == 2u || which == 4u) assert(tu_kind(u, labels[which == 2u ? 0u : 1u]) == ATOM_SYMBOL &&
            symbol_eq_cstr(g_symbols, tu_sym(u, labels[which == 2u ? 0u : 1u]), "M"));
        assert(tu_kind(u, word.seed) == ATOM_SYMBOL && symbol_eq_cstr(g_symbols, tu_sym(u, word.seed), "I"));
        assert(cetta_proof_cursor_commit(cursor, word.token));
    }
    assert(count == 5u);
}
static void proof_count(Space *s, Arena *a, CettaProofRowLayout *l, uint64_t total) {
    assert(space_has_proof_image(s, l->head[0]));
    Atom *pattern = parse(a, "(proofs $r $h $p)"); uint64_t count = 0u; CettaIndex work = 0u;
    assert(space_match_count_flat_linear64(s, a, pattern, &count, &work) && count == 5u);
    assert(space_length64(s) == total && space_has_proof_image(s, l->head[0]));
}
static void append_views(TermUniverse *u, Arena *a, CettaProofImage *im, CettaProofRowLayout *l) {
    Space s; space_init_with_universe(&s, u); s.kind = SPACE_KIND_SET;
    space_add(&s, parse(a, "(unrelated 9)"));
    assert(space_attach_proof_image(&s, l->head[0], l, im) == SPACE_PROOF_IMAGE_READY);
    SpaceOccurrenceCursor old; space_occurrence_cursor_init_empty(&old);
    assert(space_occurrence_cursor_init(&s, parse(a, "(unrelated $x)"), &old));
    CettaProofCursor *proof = NULL; assert(cetta_proof_cursor_new(im, CETTA_PROOF_ALL_STATES, &proof) == CETTA_PROOF_OK);
    CettaProofBudget budget = {100u, 0u}; CettaProofWord pending;
    assert(cetta_proof_cursor_peek(proof, &budget, &pending) == CETTA_PROOF_ROW);
    uint64_t pending_token = pending.token;
    SpaceReadToken before = space_read_token(&s); SpaceProgramToken program = space_program_token(&s);
    AtomId first[] = {key(u,a,"(final 1)"),key(u,a,"(final 2)"),key(u,a,"(final 1)")};
    refuse_materialization = true; materializations = 0u;
    assert(space_add_alpha_keys_batch(&s, first, 3u));
    assert(materializations == 0u && s.native.len == 3u);
    assert(!space_read_token_prefix_intact(before) && space_program_token_matches_live_space(program, &s));
    proof_count(&s, a, l, 8u);
    assert(term_universe_collect(u, NULL));
    const char *old_rows[] = {"(unrelated 9)"}; cursor_values(&old, a, old_rows, 1u);
    space_occurrence_cursor_release(&old);
    SpaceOccurrenceCursor mid; space_occurrence_cursor_init_empty(&mid);
    assert(space_occurrence_cursor_init(&s, parse(a,"(final $x)"), &mid));
    AtomId second[] = {key(u,a,"(other 7)"), key(u,a,"(final 3)")};
    assert(space_add_alpha_keys_batch(&s, second, 2u) && materializations == 0u);
    assert(s.native.len == 5u); proof_count(&s, a, l, 10u);
    assert(term_universe_collect(u, NULL));
    assert(cetta_proof_cursor_peek(proof, &budget, &pending) == CETTA_PROOF_ROW && pending.token == pending_token);
    const char *mid_rows[] = {"(final 1)","(final 2)"}; cursor_values(&mid, a, mid_rows, 2u);
    space_occurrence_cursor_release(&mid);
    SpaceOccurrenceCursor now; space_occurrence_cursor_init_empty(&now);
    assert(space_occurrence_cursor_init(&s,parse(a,"(final $x)"),&now));
    const char *new_rows[] = {"(final 1)","(final 2)","(final 3)"}; cursor_values(&now,a,new_rows,3u);
    space_occurrence_cursor_release(&now);
    check_words(u, proof, a); cetta_proof_cursor_free(proof);
    refuse_materialization = false;
    assert(space_settle_proof_images(&s) == SPACE_PROOF_IMAGE_READY && s.native.len == 10u);
    unsigned seen[5] = {0};
    for (CettaIndex i = 0u; i < space_length64(&s); i++) {
        Atom *row = space_get_at64(&s,i); assert(row);
        for (unsigned j = 0u; j < 5u; j++) if (atom_alpha_eq(row,parse(a,expected[j]))) seen[j]++;
    }
    for (unsigned j=0u;j<5u;j++) assert(seen[j]==1u);
    space_free(&s);
    puts("PASS: disjoint batches retain five proof words, old/new ordinary captures, pending proof cursor and truthful prefix stamps");
}
static void captured_retirement(TermUniverse *u,Arena *a,CettaProofImage *im,CettaProofRowLayout *l){
    Space s;space_init_with_universe(&s,u);s.kind=SPACE_KIND_SET;
    Atom *row=parse(a,"(unrelated-retired 999)");space_add(&s,row);
    assert(space_attach_proof_image(&s,l->head[0],l,im)==SPACE_PROOF_IMAGE_READY);
    SpaceOccurrenceCursor old;space_occurrence_cursor_init_empty(&old);
    assert(space_occurrence_cursor_init(&s,parse(a,"(unrelated-retired $x)"),&old));
    CettaProofCursor *proof=NULL;assert(cetta_proof_cursor_new(im,CETTA_PROOF_ALL_STATES,&proof)==CETTA_PROOF_OK);
    AtomId fresh=key(u,a,"(after-retired 1000)");
    refuse_materialization=true;materializations=0u;
    assert(space_add_alpha_keys_batch(&s,&fresh,1u)&&materializations==0u);
    assert(space_has_proof_image(&s,l->head[0]));
    space_free(&s);assert(term_universe_collect(u,NULL));
    assert(term_universe_lookup_atom_id(u,parse(a,"(after-retired 1000)"))==CETTA_ATOM_ID_NONE);
    const char *rows[]={"(unrelated-retired 999)"};cursor_values(&old,a,rows,1u);
    check_words(u,proof,a);cetta_proof_cursor_free(proof);space_occurrence_cursor_release(&old);
    assert(term_universe_collect(u,NULL));
    assert(term_universe_lookup_atom_id(u,row)==CETTA_ATOM_ID_NONE);
    refuse_materialization=false;
    puts("PASS: detached ordinary capture and proof cursor outlive source retirement and collection; later-only prefix rows retire");
}
typedef struct{TermUniverse*u;Arena*a;Space*s;unsigned refs,calls;}AppendFinalizer;
static void append_finalizer_retain(void *p){((AppendFinalizer*)p)->refs++;}
static void append_finalizer_release(void *p){
    AppendFinalizer *f=p;assert(f->refs);if(--f->refs)return;f->calls++;
    assert(f->u->counted_sweep_active&&f->u->collection_inhibit==1u);
    AtomId ids[]={key(f->u,f->a,"(callback-added 73)"),key(f->u,f->a,"(callback-added 74)")};
    assert(space_add_alpha_keys_batch(f->s,ids,2u));
    assert(f->u->collection_inhibit==1u);
}
static void collection_callback(TermUniverse*u,Arena*a,CettaProofImage*im,CettaProofRowLayout*l){
    Space s;space_init_with_universe(&s,u);s.kind=SPACE_KIND_SET;space_add(&s,parse(a,"(callback-old 72)"));
    assert(space_attach_proof_image(&s,l->head[0],l,im)==SPACE_PROOF_IMAGE_READY);
    SpaceOccurrenceCursor old;space_occurrence_cursor_init_empty(&old);
    assert(space_occurrence_cursor_init(&s,parse(a,"(callback-old $x)"),&old));
    assert(term_universe_collect(u,NULL));
    AppendFinalizer finalizer={u,a,&s,0u,0u};Arena source;arena_init_detached(&source);
    Atom*h=atom_native_handle_identifier(&source,91,&finalizer,append_finalizer_retain,append_finalizer_release);
    assert(h&&finalizer.refs==1u&&term_universe_store_atom_id(u,&source,h)!=CETTA_ATOM_ID_NONE);
    assert(finalizer.refs==2u);arena_free(&source);assert(finalizer.refs==1u);
    refuse_materialization=true;materializations=0u;
    assert(term_universe_collect(u,NULL)&&finalizer.calls==1u&&finalizer.refs==0u);
    assert(materializations==0u&&s.native.len==3u);proof_count(&s,a,l,8u);
    const char *rows[]={"(callback-old 72)"};cursor_values(&old,a,rows,1u);space_occurrence_cursor_release(&old);
    SpaceOccurrenceCursor now;space_occurrence_cursor_init_empty(&now);
    assert(space_occurrence_cursor_init(&s,parse(a,"(callback-added $x)"),&now));
    const char *added[]={"(callback-added 73)","(callback-added 74)"};cursor_values(&now,a,added,2u);
    space_occurrence_cursor_release(&now);refuse_materialization=false;space_free(&s);
    puts("PASS: actual opaque finalizer appends disjoint rows during counted sweep and transfers a captured prefix without materialization");
}
static void prefix_key_roots(TermUniverse*u,Arena*a,CettaProofImage*im,CettaProofRowLayout*l){
    Space s;space_init_with_universe(&s,u);s.kind=SPACE_KIND_SET;
    Atom *original=parse(a,"(open-prefix $captured $captured)");space_add(&s,original);
    AtomId stored=space_get_atom_id_at64(&s,0u);
    assert(space_attach_proof_image(&s,l->head[0],l,im)==SPACE_PROOF_IMAGE_READY);
    assert(term_universe_collect(u,NULL)&&u->counting);
    AtomId canonical=term_universe_alpha_key_id(u,stored);assert(canonical!=CETTA_ATOM_ID_NONE);
    AtomId added=key(u,a,"(ordinary 17)");
    refuse_materialization=true;materializations=0u;
    assert(space_add_alpha_keys_batch(&s,&added,1u)&&materializations==0u);
    assert(canonical!=stored);
    assert(term_universe_collect(u,NULL)&&tu_hdr(u,canonical));
    uint64_t revision=s.revision;
    AtomId variant=key(u,a,"(open-prefix $other $other)");assert(variant==canonical);
    assert(space_add_alpha_keys_batch(&s,&variant,1u)&&s.revision==revision&&s.native.len==2u);
    proof_count(&s,a,l,7u);
    space_free(&s);assert(term_universe_collect(u,NULL));
    bool declined=false;
    assert(term_universe_alpha_key_lookup(u,original,&declined)==CETTA_ATOM_ID_NONE&&!declined);
    refuse_materialization=false;
    puts("PASS: incremental prefix presence owns a distinct alpha key across warm GC and releases it with its Space");
}
static unsigned fault_case(TermUniverse *u, Arena *a, CettaProofImage *im, CettaProofRowLayout *l,
                            unsigned fail_malloc, unsigned fail_index, bool fail_provider) {
    Space s; space_init_with_universe(&s,u); s.kind=SPACE_KIND_SET;
    space_add(&s,parse(a,"(unrelated 9)"));
    for(unsigned i=0u;i<63u;i++){
        char text[64];snprintf(text,sizeof(text),"(padding %u)",i);space_add(&s,parse(a,text));
    }
    assert(s.native.len==s.native.cap&&s.native.len==64u);
    assert(space_attach_proof_image(&s,l->head[0],l,im)==SPACE_PROOF_IMAGE_READY);
    CettaCount prefix_rows=s.native.len;
    SpaceOccurrenceCursor old;space_occurrence_cursor_init_empty(&old);
    assert(space_occurrence_cursor_init(&s,parse(a,"(unrelated $x)"),&old));
    AtomId ids[]={key(u,a,"(fresh $x $x)"),key(u,a,"(fresh $y $y)"),key(u,a,"(fresh $x $y)")};
    uint64_t rev=s.revision, prefix=s.prefix_epoch;
    conversion=true;mallocs=0u;refuse_malloc=fail_malloc;
    index_allocations=0u;fail_index_allocation=fail_index;
    refuse_materialization=true;materializations=0u;
    refuse_capture_registration=fail_provider;capture_registration_refusals=0u;
    bool success=space_add_alpha_keys_batch(&s,ids,3u);
    assert(!refuse_capture_registration&&capture_registration_refusals==(fail_provider?1u:0u));
    unsigned allocations=index_allocations;
    conversion=false;refuse_malloc=0u;fail_index_allocation=0u;
    assert(materializations==0u);
    if(fail_malloc||fail_index||fail_provider){
        assert(!success&&s.revision==rev&&s.prefix_epoch==prefix&&s.native.len==prefix_rows);
        proof_count(&s,a,l,prefix_rows+5u);
        assert(space_match_backend_last_error_code()==SPACE_MATCH_BACKEND_ERROR_ALLOCATION_FAILED);
        space_match_backend_clear_error();
        assert(space_add_alpha_keys_batch(&s,ids,3u));
    }else assert(success);
    assert(s.native.len==prefix_rows+2u);proof_count(&s,a,l,prefix_rows+7u);
    rev=s.revision;
    assert(space_add_alpha_keys_batch(&s,ids,3u)&&s.native.len==prefix_rows+2u&&s.revision==rev);
    const char *old_rows[]={"(unrelated 9)"};cursor_values(&old,a,old_rows,1u);
    space_occurrence_cursor_release(&old);
    SpaceOccurrenceCursor fresh;space_occurrence_cursor_init_empty(&fresh);
    assert(space_occurrence_cursor_init(&s,parse(a,"(fresh $x $y)"),&fresh));
    const char *fresh_rows[]={"(fresh $a $a)","(fresh $a $b)"};cursor_values(&fresh,a,fresh_rows,2u);
    space_occurrence_cursor_release(&fresh);
    refuse_materialization=false;space_free(&s);return allocations;
}
static unsigned store_callback_calls;
static bool observe_store_id(Space *s,AtomId id,Atom *atom){
    (void)s;(void)id;(void)atom;store_callback_calls++;return false;
}
static void fallbacks(TermUniverse *u,Arena *a,CettaProofImage *im,CettaProofRowLayout *l){
    Space s;attach(&s,u,a,im,l);
    AtomId mixed[]={key(u,a,"(unrelated 7)"),key(u,a,expected[0])};
    uint64_t revision=s.revision;
    refuse_materialization=true;materializations=0u;
    assert(!space_add_alpha_keys_batch(&s,mixed,2u));
    assert(materializations==1u&&s.revision==revision&&s.native.len==0u);
    proof_count(&s,a,l,5u);
    space_match_backend_clear_error();refuse_materialization=false;
    assert(space_add_alpha_keys_batch(&s,mixed,2u)&&space_length64(&s)==6u);
    assert(!space_has_proof_image(&s,l->head[0]));space_free(&s);
    attach(&s,u,a,im,l);
    Space overlay;space_init_overlay(&overlay,&s);
    AtomId fresh=key(u,a,"(new 42)");
    assert(space_add_alpha_keys_batch(&s,&fresh,1u));
    assert(space_length64(&s)==6u&&space_length64(&overlay)==5u);
    bag(&overlay,a,false);
    assert(!space_contains_exact(&overlay,parse(a,"(new 42)")));
    assert(!space_has_proof_image(&s,l->head[0]));
    space_free(&overlay);space_free(&s);
    attach(&s,u,a,im,l);
    SpaceMatchBackendOps ops=*s.match_backend.ops;ops.store_atom_id_direct=observe_store_id;
    s.match_backend.ops=&ops;store_callback_calls=0u;
    refuse_materialization=true;
    assert(!space_add_alpha_keys_batch(&s,&fresh,1u)&&store_callback_calls==0u);
    refuse_materialization=false;space_match_backend_clear_error();
    assert(space_add_alpha_keys_batch(&s,&fresh,1u)&&store_callback_calls==1u);
    assert(space_length64(&s)==6u&&!space_has_proof_image(&s,l->head[0]));space_free(&s);
    puts("PASS: mixed-head refusal is atomic; frozen overlay suffix and custom store callbacks use exact settlement fallback");
}
int main(void){
    assert(setvbuf(stdout,NULL,_IOLBF,0)==0);
    SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);
    g_symbols=&symbols;g_hashcons=NULL;VarInternTable vars;var_intern_init(&vars);g_var_intern=&vars;
    for(unsigned format=0u;format<2u;format++){
        Arena a,persistent;arena_init_detached(&a);arena_init_detached(&persistent);
        TermUniverse u;assert(term_universe_init_with_store_format(&u,format?TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1));
        term_universe_set_persistent_arena(&u,&persistent);term_universe_set_collection_mode(&u,TERM_UNIVERSE_COLLECTION_RECLAIM);
        CettaProofRowLayout l=layout(&u,&a);CettaProofImage*im=image(&u,&a);
        append_views(&u,&a,im,&l);
        captured_retirement(&u,&a,im,&l);
        collection_callback(&u,&a,im,&l);
        prefix_key_roots(&u,&a,im,&l);
        unsigned allocations=fault_case(&u,&a,im,&l,0u,0u,false);assert(allocations>0u);
        for(unsigned i=1u;i<=3u;i++)fault_case(&u,&a,im,&l,i,0u,false);
        for(unsigned i=1u;i<=allocations;i++)fault_case(&u,&a,im,&l,0u,i,false);
        fault_case(&u,&a,im,&l,0u,0u,true);
        printf("PASS: %s three staging, %u real index-allocation and capture-provider refusals preserve captures and retry without duplicate rows\n",format?"wide":"compact",allocations);
        fallbacks(&u,&a,im,&l);
        cetta_proof_image_release(im);assert(term_universe_collect(&u,NULL));
        term_universe_free(&u);arena_free(&a);arena_free(&persistent);
    }
    g_var_intern=NULL;var_intern_free(&vars);g_symbols=NULL;symbol_table_free(&symbols);
    return 0;
}
