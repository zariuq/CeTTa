#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abt.h"
#include "atom.h"
#include "atom_blob.h"
#include "abt_default_signatures_blob.h"
#include "symbol.h"

#ifndef CETTA_ABT_MUTATION
#define CETTA_ABT_MUTATION 0
#endif

enum {
    ABT_DEEP_TERM_DEPTH = CETTA_ABT_MUTATION == 0 ? 100000 : 1000,
    ABT_EXPECTED_CHECKS = 114,
};

static unsigned failures = 0;
static unsigned checks = 0;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                           \
            failures++;                                                       \
        }                                                                      \
    } while (0)

static Atom *node1(Arena *arena, const char *head, Atom *a) {
    return atom_expr2(arena, atom_symbol(arena, head), a);
}

static Atom *node2(Arena *arena, const char *head, Atom *a, Atom *b) {
    return atom_expr3(arena, atom_symbol(arena, head), a, b);
}

static Atom *node3(Arena *arena, const char *head,
                   Atom *a, Atom *b, Atom *c) {
    Atom *elems[] = {atom_symbol(arena, head), a, b, c};
    return atom_expr(arena, elems, 4u);
}

static Atom *node4(Arena *arena, const char *head,
                   Atom *a, Atom *b, Atom *c, Atom *d) {
    Atom *elems[] = {atom_symbol(arena, head), a, b, c, d};
    return atom_expr(arena, elems, 5u);
}

static Atom *var(Arena *arena, int64_t index) {
    return node1(arena, "idx", atom_int(arena, index));
}

static void test_default_signature_blob(Arena *arena) {
    AtomBlobStatus status = ATOM_BLOB_OK;
    Atom *equation = atom_blob_decode_single_ground(
        arena, ABT_DEFAULT_SIGNATURES_BLOB,
        ABT_DEFAULT_SIGNATURES_BLOB_LEN, &status);
    CHECK(status == ATOM_BLOB_OK && equation &&
              equation->kind == ATOM_EXPR && equation->expr.len == 3u &&
              atom_is_symbol(equation->expr.elems[0], "="),
          "default signature source decodes as one equation");

    Atom *decoded_set = equation && equation->kind == ATOM_EXPR &&
                        equation->expr.len == 3u
        ? equation->expr.elems[2] : NULL;
    Atom *native_set = abt_default_signature_set(arena);
    CHECK(decoded_set && native_set && atom_eq(decoded_set, native_set),
          "native default signature set is the decoded source atom");

    Atom *head = atom_symbol(arena, "__cetta_abt_default_signatures");
    Atom *capability = atom_symbol(arena, "AbtDefaultsV1");
    Atom *args[] = {capability};
    Atom *dispatched = abt_grounded_dispatch(arena, head, args, 1u);
    CHECK(dispatched && native_set && atom_eq(dispatched, native_set),
          "versioned native capability returns the decoded source atom");

    Atom *bad_capability = atom_symbol(arena, "AbtDefaultsV2");
    Atom *bad_args[] = {bad_capability};
    Atom *bad_call = atom_expr2(arena, head, bad_capability);
    Atom *bad_result = abt_grounded_dispatch(arena, head, bad_args, 1u);
    CHECK(bad_result && bad_result->kind == ATOM_EXPR &&
              bad_result->expr.len == 3u &&
              atom_is_symbol(bad_result->expr.elems[0], "Error") &&
              atom_eq(bad_result->expr.elems[1], bad_call) &&
              atom_is_symbol(bad_result->expr.elems[2],
                             "ABTInvalidDefaultSignatureCapability"),
          "wrong default-signature capability preserves culprit and context");

    Atom *arity_result = abt_grounded_dispatch(arena, head, NULL, 0u);
    Atom *arity_elems[] = {head};
    Atom *arity_call = atom_expr(arena, arity_elems, 1u);
    CHECK(arity_result && arity_result->kind == ATOM_EXPR &&
              arity_result->expr.len == 3u &&
              atom_is_symbol(arity_result->expr.elems[0], "Error") &&
              atom_eq(arity_result->expr.elems[1], arity_call) &&
              atom_is_symbol(arity_result->expr.elems[2],
                             "ABTIncorrectArity"),
          "default-signature arity error preserves culprit and context");

    unsigned char *corrupt = cetta_malloc(
        (size_t)ABT_DEFAULT_SIGNATURES_BLOB_LEN);
    memcpy(corrupt, ABT_DEFAULT_SIGNATURES_BLOB,
           (size_t)ABT_DEFAULT_SIGNATURES_BLOB_LEN);
    corrupt[0] ^= 0xffu;
    CHECK(atom_blob_decode_single_ground(
              arena, corrupt, ABT_DEFAULT_SIGNATURES_BLOB_LEN, &status) ==
              NULL && status == ATOM_BLOB_BAD_MAGIC,
          "corrupt default signature blob magic fails closed");
    free(corrupt);

    CHECK(atom_blob_decode_single_ground(
              arena, ABT_DEFAULT_SIGNATURES_BLOB,
              ABT_DEFAULT_SIGNATURES_BLOB_LEN - 1u, &status) == NULL &&
              status == ATOM_BLOB_TRUNCATED,
          "truncated default signature blob fails closed");
}

static void check_atom_eq(const char *label, Atom *actual, Atom *expected) {
    checks++;
    if (!actual || !expected || !atom_eq(actual, expected)) {
        fprintf(stderr, "FAIL: %s\n", label);
        failures++;
    }
}

static Atom *make_signature_decl(Arena *arena, const char *head,
                                 Atom *fields, Atom *binding_decl) {
    return node3(arena, "AbtSignature", atom_symbol(arena, head),
                 fields, binding_decl);
}

static void test_signature_admission(Arena *arena) {
    AbtSignature signature;
    abt_signature_init(&signature);
    CHECK(abt_signature_add_defaults(&signature, arena),
          "default ABT signature admitted");

    const AbtSignatureEntry *pi = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "Pi"), 2u);
    const AbtSignatureEntry *lam = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "Lam"), 2u);
    const AbtSignatureEntry *app = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "App"), 2u);
    const AbtSignatureEntry *chain = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "ABTChainV1"), 2u);
    const AbtSignatureEntry *pattern_var = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "ABTPatternVarV1"), 1u);
    const AbtSignatureEntry *let_scope = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "ABTLetScopeV1"), 1u);
    const AbtSignatureEntry *let_term = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "ABTLetV1"), 3u);
    CHECK(pi && pi->depths[0] == 0u && pi->depths[1] == 1u,
          "Pi codomain binds once");
    CHECK(lam && lam->depths[0] == 0u && lam->depths[1] == 1u,
          "Lam body binds once");
    CHECK(app && app->depths[0] == 0u && app->depths[1] == 0u,
          "App fields do not bind");
    CHECK(chain && chain->depths[0] == 0u && chain->depths[1] == 1u,
          "Prime canonical chain body binds once");
    CHECK(pattern_var && pattern_var->depths[0] == 0u,
          "Prime canonical let pattern slots do not bind");
    CHECK(let_scope && let_scope->depths[0] == 1u,
          "Prime canonical let scope binds once");
    CHECK(let_term && let_term->depths[0] == 0u &&
              let_term->depths[1] == 0u && let_term->depths[2] == 0u,
          "Prime canonical let delegates binding to unary scope nodes");

    Atom *canonical_let = node3(
        arena, "ABTLetV1",
        node1(arena, "ABTPatternVarV1", atom_int(arena, 0)),
        atom_symbol(arena, "source"),
        node1(arena, "ABTLetScopeV1", var(arena, 0)));
    CHECK(abt_scope_check(&signature, 0u, canonical_let),
          "Prime canonical let is closed through its declared unary scope");
    abt_signature_free(&signature);

    Atom *fields = node2(arena, "Fields", atom_symbol(arena, "type"),
                         atom_symbol(arena, "body"));
    Atom *dependent_decl = make_signature_decl(
        arena, "Dependent", fields,
        node2(arena, "BindPi", atom_symbol(arena, "type"),
              atom_symbol(arena, "body")));
    abt_signature_init(&signature);
    CHECK(abt_signature_add_decl(&signature, dependent_decl),
          "package BindPi signature admitted");
    const AbtSignatureEntry *dependent = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "Dependent"), 2u);
    CHECK(dependent && dependent->depths[0] == 0u &&
              dependent->depths[1] == 1u,
          "package BindPi preserves domain and codomain depths");
    abt_signature_free(&signature);

    Atom *unknown_bind1_decl = make_signature_decl(
        arena, "BadBind1", fields,
        node1(arena, "Bind1", atom_symbol(arena, "missing")));
    abt_signature_init(&signature);
    CHECK(!abt_signature_add_decl(&signature, unknown_bind1_decl) &&
              signature.len == 0u,
          "Bind1 undeclared field fails closed");
    abt_signature_free(&signature);

    Atom *decl = make_signature_decl(
        arena, "Forall", fields,
        node1(arena, "Bind1", atom_symbol(arena, "body")));
    Atom *set = node1(arena, "AbtSignatures", decl);
    abt_signature_init(&signature);
    CHECK(abt_signature_add_set(&signature, set),
          "package Bind1 signature admitted");
    const AbtSignatureEntry *forall_entry = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "Forall"), 2u);
    CHECK(forall_entry && forall_entry->depths[0] == 0u &&
              forall_entry->depths[1] == 1u,
          "package Bind1 has declared depth");
    abt_signature_free(&signature);

    Atom *generated_field0 = node1(arena, "Hole", atom_int(arena, 0));
    Atom *generated_field1 = node1(arena, "Hole", atom_int(arena, 1));
    Atom *generated_fields = node2(
        arena, "Fields", generated_field0, generated_field1);
    Atom *generated_bfield0 = node2(
        arena, "BField", generated_field0, atom_int(arena, 0));
    Atom *generated_bfield1 = node2(
        arena, "BField", generated_field1, atom_int(arena, 2));
    Atom *generated_bfields = node2(
        arena, "BCons", generated_bfield0,
        node2(arena, "BCons", generated_bfield1,
              atom_symbol(arena, "BNil")));
    Atom *generated_decl = make_signature_decl(
        arena, "ScopedPair", generated_fields,
        node1(arena, "BindFields", generated_bfields));
    abt_signature_init(&signature);
    CHECK(abt_signature_add_decl(&signature, generated_decl),
          "structured generated fields admitted");
    const AbtSignatureEntry *scoped_pair = abt_signature_lookup(
        &signature, symbol_intern_cstr(g_symbols, "ScopedPair"), 2u);
    CHECK(scoped_pair && scoped_pair->depths[0] == 0u &&
              scoped_pair->depths[1] == 2u,
          "structured fields preserve generated depths without invented names");
    abt_signature_free(&signature);

    Atom *bad_generated_bfield = node2(
        arena, "BField", generated_field0, atom_int(arena, -1));
    Atom *bad_generated_bfields = node2(
        arena, "BCons", bad_generated_bfield, atom_symbol(arena, "BNil"));
    Atom *bad_generated_decl = make_signature_decl(
        arena, "BadDepth", node1(arena, "Fields", generated_field0),
        node1(arena, "BindFields", bad_generated_bfields));
    abt_signature_init(&signature);
    CHECK(!abt_signature_add_decl(&signature, bad_generated_decl) &&
              signature.len == 0u,
          "negative generated field depth fails closed");
    abt_signature_free(&signature);

    Atom *duplicate_fields = node2(
        arena, "Fields", atom_symbol(arena, "body"),
        atom_symbol(arena, "body"));
    Atom *duplicate_decl = make_signature_decl(
        arena, "BadDuplicate", duplicate_fields, atom_symbol(arena, "Bind0"));
    abt_signature_init(&signature);
    CHECK(!abt_signature_add_decl(&signature, duplicate_decl) &&
              signature.len == 0u,
          "duplicate fields fail closed");
    abt_signature_free(&signature);

    Atom *bfield = node2(arena, "BField", atom_symbol(arena, "type"),
                         atom_int(arena, 0));
    Atom *blist = node2(arena, "BCons", bfield, atom_symbol(arena, "BNil"));
    Atom *incomplete_decl = make_signature_decl(
        arena, "BadIncomplete", fields, node1(arena, "BindFields", blist));
    abt_signature_init(&signature);
    CHECK(!abt_signature_add_decl(&signature, incomplete_decl) &&
              signature.len == 0u,
          "incomplete BindFields fails closed");
    abt_signature_free(&signature);

    Atom *decl_a = make_signature_decl(
        arena, "DuplicateHead", fields, atom_symbol(arena, "Bind0"));
    Atom *decl_b = make_signature_decl(
        arena, "DuplicateHead", fields, atom_symbol(arena, "Bind0"));
    Atom *duplicate_set = node2(arena, "AbtSignatures", decl_a, decl_b);
    abt_signature_init(&signature);
    CHECK(!abt_signature_add_set(&signature, duplicate_set) &&
              signature.len == 0u,
          "signature-set failure rolls back all declarations");
    abt_signature_free(&signature);
}

static void test_shift_and_substitution(Arena *arena,
                                        const AbtSignature *signature) {
    Atom *type = atom_symbol(arena, "A");

    check_atom_eq(
        "shift at cutoff",
        abt_shift(signature, arena, 1, 0u, var(arena, 0)),
        var(arena, 1));

    Atom *lam_term = node2(
        arena, "Lam", type,
        node2(arena, "App", var(arena, 1), var(arena, 0)));
    Atom *lam_shifted = node2(
        arena, "Lam", type,
        node2(arena, "App", var(arena, 2), var(arena, 0)));
    check_atom_eq("shift respects Lam binder",
                  abt_shift(signature, arena, 1, 0u, lam_term), lam_shifted);

    Atom *pi_term = node2(arena, "Pi", type, var(arena, 0));
    check_atom_eq("shift respects Pi binder",
                  abt_shift(signature, arena, 1, 0u, pi_term), pi_term);
    CHECK(abt_shift(signature, arena, 1, 0u, pi_term) == pi_term,
          "no-op shift preserves the original closed term pointer");

    Atom *below_cutoff = var(arena, 0);
    CHECK(abt_shift(signature, arena, 1, 1u, below_cutoff) == below_cutoff,
          "shift below cutoff preserves the original variable pointer");
    CHECK(abt_shift(signature, arena, 0, 0u, lam_term) == lam_term,
          "zero shift preserves the original term pointer");

    check_atom_eq(
        "negative shift removes one unused outer level",
        abt_shift(signature, arena, -1, 0u, var(arena, 1)),
        var(arena, 0));
    CHECK(abt_shift(signature, arena, -1, 0u, var(arena, 0)) == NULL,
          "negative shift rejects a referenced removed level");
    CHECK(abt_shift(
              signature, arena, -1, 0u,
              node2(arena, "Lam", type, var(arena, 1))) == NULL,
          "negative shift rejects capture beneath a binder");
    check_atom_eq(
        "negative shift preserves a remaining outer level beneath a binder",
        abt_shift(
            signature, arena, -1, 0u,
            node2(arena, "Lam", type, var(arena, 2))),
        node2(arena, "Lam", type, var(arena, 1)));

    Atom *app_term = node2(
        arena, "App", atom_symbol(arena, "f"), var(arena, 0));
    Atom *app_shifted = node2(
        arena, "App", atom_symbol(arena, "f"), var(arena, 1));
    check_atom_eq("App argument remains nonbinding",
                  abt_shift(signature, arena, 1, 0u, app_term), app_shifted);

    Atom *args_term = node2(
        arena, "ArgsCons", var(arena, 0), atom_symbol(arena, "ArgsNil"));
    Atom *args_shifted = node2(
        arena, "ArgsCons", var(arena, 1), atom_symbol(arena, "ArgsNil"));
    check_atom_eq("ArgsCons child remains nonbinding",
                  abt_shift(signature, arena, 1, 0u, args_term), args_shifted);

    Atom *case_term = node2(
        arena, "Case", atom_symbol(arena, "C"), var(arena, 0));
    Atom *case_shifted = node2(
        arena, "Case", atom_symbol(arena, "C"), var(arena, 1));
    check_atom_eq("Case body follows waist depth zero",
                  abt_shift(signature, arena, 1, 0u, case_term), case_shifted);

    Atom *subst_capture_probe = node2(
        arena, "Lam", type,
        node2(arena, "App", var(arena, 1), var(arena, 0)));
    check_atom_eq(
        "substitution lifts replacement under binder",
        abt_subst(signature, arena, 0u, var(arena, 0), subst_capture_probe),
        subst_capture_probe);

    check_atom_eq(
        "substitution decrements higher indices",
        abt_subst(signature, arena, 0u, atom_symbol(arena, "z"),
                  var(arena, 2)),
        var(arena, 1));

    Atom *shared = node2(
        arena, "App", var(arena, 0), atom_symbol(arena, "tail"));
    Atom *shared_twice = node2(arena, "App", shared, shared);
    Atom *shared_result = abt_subst(
        signature, arena, 0u, atom_symbol(arena, "z"), shared_twice);
    CHECK(shared_result && shared_result->kind == ATOM_EXPR &&
              shared_result->expr.elems[1] == shared_result->expr.elems[2],
          "changed shared subterms are rebuilt once per depth");

    Atom *depth_shared = var(arena, 0);
    Atom *depth_sensitive = node2(
        arena, "App", depth_shared,
        node2(arena, "Lam", type, depth_shared));
    Atom *depth_expected = node2(
        arena, "App", var(arena, 1),
        node2(arena, "Lam", type, var(arena, 0)));
    check_atom_eq(
        "shared subterms at different binder depths are not conflated",
        abt_shift(signature, arena, 1, 0u, depth_sensitive),
        depth_expected);
}

static void test_locally_nameless_seams(Arena *arena,
                                        const AbtSignature *signature) {
    Atom *type = atom_symbol(arena, "A");
    Atom *x = atom_symbol(arena, "x");
    Atom *y = atom_symbol(arena, "y");
    Atom *x_body = node2(
        arena, "App", x, node2(arena, "Lam", type, x));
    Atom *y_body = node2(
        arena, "App", y, node2(arena, "Lam", type, y));
    Atom *closed = node2(
        arena, "App", var(arena, 0),
        node2(arena, "Lam", type, var(arena, 1)));

    Atom *closed_x = abt_close(signature, arena, x, x_body);
    Atom *closed_y = abt_close(signature, arena, y, y_body);
    check_atom_eq("close accounts for nested binders", closed_x, closed);
    CHECK(abt_alpha_eq(closed_x, closed_y),
          "alpha-renamed named syntax forms close identically");
    check_atom_eq("open inverts close on locally closed body",
                  abt_open(signature, arena, x, closed_x), x_body);
    CHECK(abt_open(signature, arena, x, x_body) == x_body,
          "open without a matching index preserves the original pointer");
    CHECK(abt_open(
              signature, arena, var(arena, 0),
              node2(arena, "Lam", type, var(arena, 1))) == NULL,
          "open rejects a loose replacement that would be captured");
    CHECK(abt_close(signature, arena, var(arena, 0), x_body) == NULL,
          "close rejects a loose canonical name");

    Atom *free_x = node1(arena, "Free", x);
    Atom *free_body = node2(
        arena, "App", free_x, node2(arena, "Lam", type, free_x));
    Atom *structured_closed = abt_close(
        signature, arena, free_x, free_body);
    check_atom_eq("close accepts a structured free-name wrapper",
                  structured_closed, closed);
    check_atom_eq("open restores a structured free-name wrapper",
                  abt_open(signature, arena, free_x, structured_closed),
                  free_body);

    Atom *named_x = node1(arena, "quote", atom_string(arena, "x"));
    Atom *named_y = node1(arena, "quote", atom_string(arena, "y"));
    Atom *named_body = node2(
        arena, "App", named_x, node2(arena, "Lam", type, named_x));
    check_atom_eq("bind admits an explicit quoted-string name",
                  abt_bind(signature, arena, named_x, named_body), closed);
    check_atom_eq("open restores an explicit quoted-string name",
                  abt_open(signature, arena, named_x, closed), named_body);
    check_atom_eq("generated name spellings erase alpha-invariantly",
                  abt_bind(signature, arena, named_y,
                           node2(arena, "App", named_y,
                                 node2(arena, "Lam", type, named_y))),
                  closed);
    check_atom_eq("bind shifts pre-existing loose indices before closing",
                  abt_bind(signature, arena, named_x,
                           node2(arena, "App", var(arena, 0), named_x)),
                  node2(arena, "App", var(arena, 1), var(arena, 0)));
    check_atom_eq("bind preserves a different generated syntax name",
                  abt_bind(signature, arena, named_x,
                           node2(arena, "App", named_y, named_x)),
                  node2(arena, "App", named_y, var(arena, 0)));

    Atom *mm_ph = node1(arena, "quote",
                        node1(arena, "mm-var", atom_string(arena, "ph")));
    Atom *quoted_body = node2(
        arena, "App", mm_ph, node2(arena, "Lam", type, mm_ph));
    check_atom_eq("bind admits an explicit quoted structural name",
                  abt_bind(signature, arena, mm_ph, quoted_body), closed);
    check_atom_eq("open restores an explicit quoted structural name",
                  abt_open(signature, arena, mm_ph, closed), quoted_body);

    Atom *quoted_loose = node1(arena, "quote", var(arena, 99));
    CHECK(abt_shift(signature, arena, 1, 0u, quoted_loose) == quoted_loose,
          "shift is opaque beneath quotation");
    CHECK(abt_subst(signature, arena, 0u, atom_symbol(arena, "z"),
                    quoted_loose) == quoted_loose,
          "substitution is opaque beneath quotation");
    CHECK(abt_scope_check(signature, 0u, quoted_loose),
          "quoted syntax is closed with respect to outer object binders");
    Atom *quoted_matcher_var = node1(arena, "quote", atom_var(arena, "x"));
    CHECK(abt_shift(signature, arena, 1, 0u, quoted_matcher_var) ==
              quoted_matcher_var,
          "ordinary open quoted code is opaque to object-variable shifting");
    CHECK(abt_scope_check(signature, 0u, quoted_matcher_var),
          "matcher variables inside ordinary quoted code do not become object indices");
    CHECK(abt_alpha_eq(quoted_matcher_var, quoted_matcher_var),
          "ordinary open quoted code remains structurally comparable");
    CHECK(abt_bind(signature, arena, quoted_matcher_var, x_body) == NULL,
          "an open quote remains inadmissible as a persistent binder key");
    CHECK(abt_scope_check(signature, 0u, named_x),
          "quoted-string data is closed canonical ABT");
    CHECK(abt_bind(signature, arena, var(arena, 0), x_body) == NULL,
          "bind rejects a loose canonical index as a name");

    Atom *mixed = node2(arena, "App", x, var(arena, 0));
    Atom *mixed_closed = node2(arena, "App", var(arena, 0), var(arena, 0));
    check_atom_eq("close distinguishes a free spelling from a bound index",
                  abt_close(signature, arena, x, mixed), mixed_closed);

    CHECK(abt_scope_check(
              signature, 0u, node2(arena, "Lam", type, var(arena, 0))),
          "scope check accepts a bound variable");
    CHECK(!abt_scope_check(
              signature, 0u, node2(arena, "Lam", type, var(arena, 1))),
          "scope check rejects a loose variable");
    CHECK(!abt_scope_check(signature, 0u,
                           node1(arena, "idx", atom_symbol(arena, "bad"))),
          "scope check rejects malformed idx syntax");
    CHECK(!abt_alpha_eq(
              node1(arena, "idx", atom_symbol(arena, "bad")),
              node1(arena, "idx", atom_symbol(arena, "bad"))),
          "alpha equality rejects malformed idx syntax");

    /* Canonical counterpart of rho capture avoidance: substituting an outer
       receive variable beneath an inner receive must not capture a free name
       carried by the payload.  The nameful rho implementation remains
       independently covered by its reduction golden. */
    AbtSignature rho_signature;
    abt_signature_init(&rho_signature);
    Atom *rho_fields = node2(
        arena, "Fields", atom_symbol(arena, "channel"),
        atom_symbol(arena, "body"));
    Atom *rho_decl = make_signature_decl(
        arena, "RhoRecv", rho_fields,
        node1(arena, "Bind1", atom_symbol(arena, "body")));
    CHECK(abt_signature_add_decl(&rho_signature, rho_decl),
          "rho receive binding signature admitted");
    Atom *channel = node1(arena, "RhoQuote", atom_symbol(arena, "RhoNil"));
    Atom *rho_body = node2(
        arena, "RhoRecv", channel, node1(arena, "RhoDrop", var(arena, 1)));
    Atom *free_payload = node1(arena, "Free", atom_symbol(arena, "x"));
    Atom *rho_expected = node2(
        arena, "RhoRecv", channel, node1(
            arena, "RhoDrop", node1(arena, "Free", atom_symbol(arena, "x"))));
    check_atom_eq(
        "rho canonical substitution preserves a free same-spelling name",
        abt_subst(&rho_signature, arena, 0u, free_payload, rho_body),
        rho_expected);
    abt_signature_free(&rho_signature);
}

static void test_named_readback(Arena *arena,
                                const AbtSignature *signature) {
    Atom *type = atom_symbol(arena, "A");
    Atom *a0 = atom_symbol(arena, "a0");
    Atom *a1 = atom_symbol(arena, "a1");
    Atom *simple = node2(arena, "Lam", type, var(arena, 0));
    Atom *simple_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "Lam"),
        node1(arena, "Binders", a0), type, a0);
    check_atom_eq("named readback prints a canonical lambda",
                  abt_print(signature, arena, simple), simple_syntax);
    check_atom_eq("named parsing inverts a canonical lambda readback",
                  abt_parse(signature, arena, simple_syntax), simple);

    Atom *nested = node2(
        arena, "Lam", type,
        node2(arena, "Lam", type,
              node2(arena, "App", var(arena, 1), var(arena, 0))));
    Atom *nested_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "Lam"),
        node1(arena, "Binders", a0), type,
        node4(arena, "ABTBind", atom_symbol(arena, "Lam"),
              node1(arena, "Binders", a1), type,
              node2(arena, "App", a0, a1)));
    check_atom_eq("named readback converts indices to absolute levels",
                  abt_print(signature, arena, nested), nested_syntax);
    check_atom_eq("nested named readback round-trips",
                  abt_parse(signature, arena, nested_syntax), nested);

    Atom *collision = node2(
        arena, "Lam", type, node2(arena, "App", var(arena, 0), a0));
    Atom *collision_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "Lam"),
        node1(arena, "Binders", a1), type,
        node2(arena, "App", a1, a0));
    check_atom_eq("named readback avoids capturing a free generated name",
                  abt_print(signature, arena, collision), collision_syntax);
    check_atom_eq("capture-free generated names round-trip",
                  abt_parse(signature, arena, collision_syntax), collision);

    Atom *x = atom_symbol(arena, "x");
    Atom *y = atom_symbol(arena, "y");
    Atom *alpha_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "Lam"),
        node1(arena, "Binders", x), type, x);
    check_atom_eq("named parsing quotients alpha-renamed binders",
                  abt_parse(signature, arena, alpha_syntax), simple);
    Atom *shadow_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "Lam"),
        node1(arena, "Binders", x), type,
        node4(arena, "ABTBind", atom_symbol(arena, "Lam"),
              node1(arena, "Binders", y), type,
              node2(arena, "App", x, y)));
    check_atom_eq("named parsing resolves lexical scope innermost-first",
                  abt_parse(signature, arena, shadow_syntax), nested);

    Atom *named_x = node1(arena, "quote", atom_string(arena, "arg-1"));
    Atom *named_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "Lam"),
        node1(arena, "Binders", named_x), type, named_x);
    check_atom_eq("named parsing accepts quoted-string identities",
                  abt_parse(signature, arena, named_syntax), simple);

    Atom *mm_ph = node1(arena, "quote",
                        node1(arena, "mm-var", atom_string(arena, "ph")));
    Atom *quoted_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "Lam"),
        node1(arena, "Binders", mm_ph), type, mm_ph);
    check_atom_eq("named parsing accepts quoted structural identities",
                  abt_parse(signature, arena, quoted_syntax), simple);
    Atom *mm_ps = node1(arena, "quote",
                        node1(arena, "mm-var", atom_string(arena, "ps")));
    Atom *quoted_distinct = node4(
        arena, "ABTBind", atom_symbol(arena, "Lam"),
        node1(arena, "Binders", mm_ph), type, mm_ps);
    check_atom_eq("distinct quoted names remain distinct constants",
                  abt_parse(signature, arena, quoted_distinct),
                  node2(arena, "Lam", type, mm_ps));
    Atom *raw_compound = node1(
        arena, "mm-var", atom_string(arena, "ph"));
    CHECK(abt_parse(
              signature, arena,
              node4(arena, "ABTBind", atom_symbol(arena, "Lam"),
                    node1(arena, "Binders", raw_compound), type,
                    raw_compound)) == NULL,
          "unquoted compound binder names fail closed");
    check_atom_eq("unbound quoted-string data crosses canonicalization unchanged",
                  abt_parse(signature, arena, named_x), named_x);

    AbtSignature scoped_signature;
    abt_signature_init(&scoped_signature);
    CHECK(abt_signature_add_defaults(&scoped_signature, arena),
          "readback test defaults admitted");
    Atom *field0 = node1(arena, "Hole", atom_int(arena, 0));
    Atom *field1 = node1(arena, "Hole", atom_int(arena, 1));
    Atom *fields = node2(arena, "Fields", field0, field1);
    Atom *bindings = node2(
        arena, "BCons", node2(arena, "BField", field0, atom_int(arena, 0)),
        node2(arena, "BCons",
              node2(arena, "BField", field1, atom_int(arena, 2)),
              atom_symbol(arena, "BNil")));
    CHECK(abt_signature_add_decl(
              &scoped_signature,
              make_signature_decl(
                  arena, "ScopedPair", fields,
                  node1(arena, "BindFields", bindings))),
          "depth-two readback signature admitted");
    Atom *scoped = node2(
        arena, "ScopedPair", atom_symbol(arena, "payload"),
        node2(arena, "App", var(arena, 1), var(arena, 0)));
    Atom *scoped_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "ScopedPair"),
        node2(arena, "Binders", a0, a1),
        atom_symbol(arena, "payload"), node2(arena, "App", a0, a1));
    check_atom_eq("named readback preserves arbitrary declared depths",
                  abt_print(&scoped_signature, arena, scoped), scoped_syntax);
    check_atom_eq("depth-two named readback round-trips",
                  abt_parse(&scoped_signature, arena, scoped_syntax), scoped);
    Atom *duplicate_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "ScopedPair"),
        node2(arena, "Binders", x, x), atom_symbol(arena, "payload"),
        node2(arena, "App", x, x));
    CHECK(abt_parse(&scoped_signature, arena, duplicate_syntax) == NULL,
          "duplicate binders fail closed");
    Atom *duplicate_quoted_syntax = node4(
        arena, "ABTBind", atom_symbol(arena, "ScopedPair"),
        node2(arena, "Binders", mm_ph, mm_ph),
        atom_symbol(arena, "payload"), node2(arena, "App", mm_ph, mm_ph));
    CHECK(abt_parse(
              &scoped_signature, arena, duplicate_quoted_syntax) == NULL,
          "structurally duplicate quoted binders fail closed");
    abt_signature_free(&scoped_signature);

    CHECK(abt_parse(signature, arena, node2(arena, "Lam", type, x)) == NULL,
          "known binding constructors require an explicit syntax binder");
    CHECK(abt_parse(signature, arena, var(arena, 0)) == NULL,
          "named parsing rejects embedded canonical indices");
    Atom *plain = node2(
        arena, "App", atom_symbol(arena, "f"), atom_symbol(arena, "z"));
    CHECK(abt_print(signature, arena, plain) == plain,
          "readback preserves an unchanged nonbinding pointer");
    CHECK(abt_parse(signature, arena, plain) == plain,
          "named parsing preserves an unchanged nonbinding pointer");
    CHECK(abt_print(signature, arena, var(arena, 0)) == NULL,
          "readback rejects a loose canonical variable");
    CHECK(abt_parse(
              signature, arena,
              node3(arena, "ABTBind", atom_symbol(arena, "Missing"),
                    node1(arena, "Binders", x), x)) == NULL,
          "unknown binding presentations fail closed");
    CHECK(abt_print(
              signature, arena,
              node3(arena, "ABTBind", atom_symbol(arena, "Missing"),
                    node1(arena, "Binders", x), x)) == NULL,
          "readback rejects its reserved presentation marker");
    Atom *cycle = node1(arena, "Loop", atom_symbol(arena, "seed"));
    cycle->expr.elems[1] = cycle;
    CHECK(abt_parse(signature, arena, cycle) == NULL,
          "named parsing rejects a cyclic atom graph");
}

static Atom *make_deep_lam(Arena *arena, Atom *type) {
    Atom *term = var(arena, 0);
    for (uint32_t i = 0; i < ABT_DEEP_TERM_DEPTH; i++)
        term = node2(arena, "Lam", type, term);
    return term;
}

static bool deep_lam_has_depth(Atom *term) {
    uint32_t depth = 0;
    while (term && term->kind == ATOM_EXPR && term->expr.len == 3u &&
           atom_is_symbol(term->expr.elems[0], "Lam")) {
        depth++;
        term = term->expr.elems[2];
    }
    return depth == ABT_DEEP_TERM_DEPTH && term && term->kind == ATOM_EXPR &&
           term->expr.len == 2u && atom_is_symbol(term->expr.elems[0], "idx") &&
           term->expr.elems[1]->kind == ATOM_GROUNDED &&
           term->expr.elems[1]->ground.gkind == GV_INT &&
           term->expr.elems[1]->ground.ival == 0;
}

static bool deep_named_lam_has_depth(Atom *term) {
    uint32_t depth = 0u;
    while (term && term->kind == ATOM_EXPR && term->expr.len == 5u &&
           atom_is_symbol(term->expr.elems[0], "ABTBind") &&
           atom_is_symbol(term->expr.elems[1], "Lam")) {
        depth++;
        term = term->expr.elems[4];
    }
    if (depth != ABT_DEEP_TERM_DEPTH || !term ||
        term->kind != ATOM_SYMBOL)
        return false;
    char expected[32];
    int written = snprintf(expected, sizeof(expected), "a%u",
                           ABT_DEEP_TERM_DEPTH - 1u);
    return written > 0 && (size_t)written < sizeof(expected) &&
           atom_is_symbol(term, expected);
}

static void test_deep_and_cyclic_inputs(Arena *arena,
                                        const AbtSignature *signature) {
    Atom *deep = make_deep_lam(arena, atom_symbol(arena, "A"));
    CHECK(abt_scope_check(signature, 0u, deep),
          "scope traversal is iterative at depth 100000");
    Atom *deep_shifted = abt_shift(signature, arena, 1, 0u, deep);
    CHECK(deep_lam_has_depth(deep_shifted),
          "shift traversal is iterative at depth 100000");
    CHECK(abt_alpha_eq(deep, deep_shifted),
          "alpha equality is iterative at depth 100000");
    Atom *deep_syntax = abt_print(signature, arena, deep);
    CHECK(deep_named_lam_has_depth(deep_syntax),
          "named readback is iterative at depth 100000");
    CHECK(abt_alpha_eq(abt_parse(signature, arena, deep_syntax), deep),
          "named parsing is iterative and round-trips at depth 100000");

    Atom *cycle = node1(arena, "Loop", atom_symbol(arena, "seed"));
    cycle->expr.elems[1] = cycle;
    CHECK(abt_shift(signature, arena, 1, 0u, cycle) == NULL,
          "shift rejects a cyclic atom graph");
    CHECK(!abt_scope_check(signature, 0u, cycle),
          "scope check rejects a cyclic atom graph");
    CHECK(!abt_alpha_eq(cycle, cycle),
          "alpha equality rejects a cyclic atom graph");
    CHECK(abt_subst(signature, arena, 0u, cycle, var(arena, 0)) == NULL,
          "substitution rejects a cyclic replacement");
    CHECK(abt_open(signature, arena, cycle, var(arena, 0)) == NULL,
          "open rejects a cyclic replacement");
}

static void test_grounded_dispatch_symbol_table_lifetime(
        SymbolTable *symbols, Arena *arena) {
    arena_init(arena);
    symbol_table_init(symbols);
    (void)symbol_intern_cstr(symbols, "__lifetime_offset");
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;

    Atom *head = atom_symbol(arena, "__cetta_abt_default_signatures");
    Atom *capability = atom_symbol(arena, "AbtDefaultsV1");
    Atom *args[] = {capability};
    Atom *expected = abt_default_signature_set(arena);
    Atom *actual = abt_grounded_dispatch(arena, head, args, 1u);
    CHECK(actual && expected && atom_eq(actual, expected),
          "grounded dispatch survives same-address symbol-table reuse");
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    AbtSignature signature;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    test_default_signature_blob(&arena);
    test_signature_admission(&arena);
    abt_signature_init(&signature);
    CHECK(abt_signature_add_defaults(&signature, &arena),
          "operational default signature admitted");
    test_shift_and_substitution(&arena, &signature);
    test_locally_nameless_seams(&arena, &signature);
    test_named_readback(&arena, &signature);
    test_deep_and_cyclic_inputs(&arena, &signature);

    abt_signature_free(&signature);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;

    test_grounded_dispatch_symbol_table_lifetime(&symbols, &arena);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;

    if (checks != ABT_EXPECTED_CHECKS) {
        fprintf(stderr, "FAIL: ABT core executed %u/%u checks\n",
                checks, ABT_EXPECTED_CHECKS);
        failures++;
    }
    printf("(ABTCoreSummary %u %u %u)\n", checks,
           checks >= failures ? checks - failures : 0u, failures);
    if (failures != 0u) {
        fprintf(stderr, "ABT core: %u failure(s)\n", failures);
        return 1;
    }
    puts("PASS: iterative capture-avoiding ABT core");
    return 0;
}
