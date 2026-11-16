#include "petta_compiled_reader.h"
#include "generated/petta_reader_direct_v1.generated.h"
#include "generated/petta_extended_reader_direct_v1.generated.h"
#include "parser.h"
#include "term_domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

/* Only reader allocations are faulted. Projection callbacks temporarily leave
 * the fault scope so a refused syntax frame cannot fault the term interner. */
static bool fault_scope;
static size_t allocation, fail_at;
void *__real_cetta_malloc(size_t);
void *__real_cetta_realloc(void *, size_t);
static bool refuse(void) {
    return fault_scope && ++allocation == fail_at;
}
void *__wrap_cetta_malloc(size_t n) { return refuse() ? NULL : __real_cetta_malloc(n); }
void *__wrap_cetta_realloc(void *p, size_t n) { return refuse() ? NULL : __real_cetta_realloc(p, n); }

typedef struct {
    TermUniverse *universe;
    PeTTaGroundingV1 *grounding;
} Context;

static bool begin(void *raw) {
    Context *c = raw;
    bool saved = fault_scope; fault_scope = false;
    bool ok = petta_grounding_v1_begin_form(c->grounding);
    fault_scope = saved; return ok;
}
static AtomId word(void *raw, const uint8_t *b, size_t n) {
    Context *c = raw;
    bool saved = fault_scope; fault_scope = false;
    AtomId id = petta_grounding_v1_word(c->grounding, b, n);
    fault_scope = saved; return id;
}
static AtomId variable(void *raw, const uint8_t *b, size_t n, bool anonymous) {
    Context *c = raw;
    bool saved = fault_scope; fault_scope = false;
    AtomId id = petta_grounding_v1_variable(c->grounding,
        anonymous ? (const uint8_t *)"_" : b, anonymous ? 1u : n);
    fault_scope = saved; return id;
}
static AtomId string(void *raw, const uint8_t *b, size_t n) {
    Context *c = raw;
    bool saved = fault_scope; fault_scope = false;
    AtomId id = petta_grounding_v1_string(c->grounding, b, n);
    fault_scope = saved; return id;
}
static AtomId dollar(void *raw) { return word(raw, (const uint8_t *)"$", 1u); }
static AtomId expression(void *raw, const AtomId *ids, size_t n) {
    Context *c = raw;
    bool saved = fault_scope; fault_scope = false;
    AtomId id = petta_grounding_v1_expression(c->grounding, ids, n);
    fault_scope = saved; return id;
}
static AtomId list(void *raw, const AtomId *ids, size_t n, AtomId rest) {
    Context *c = raw;
    bool saved = fault_scope; fault_scope = false;
    AtomId id = tu_list_from_ids(c->universe, ids, n, rest);
    fault_scope = saved; return id;
}
static bool finish(void *raw, bool runnable, AtomId id, AtomId out[2], uint32_t *n) {
    (void)raw;
    assert(!runnable);
    out[0] = id; *n = 1u;
    return id != CETTA_ATOM_ID_NONE;
}

static char *nested(size_t depth, bool lists) {
    const char *leaf = "(Pair $x $x)";
    const char *open = lists ? "[" : "(node ";
    size_t width = strlen(open);
    char *source = malloc(depth * (width + 1u) + strlen(leaf) + 1u);
    assert(source);
    char *at = source;
    for (size_t i = 0u; i < depth; ++i) { memcpy(at, open, width); at += width; }
    memcpy(at, leaf, strlen(leaf)); at += strlen(leaf);
    for (size_t i = 0u; i < depth; ++i) *at++ = lists ? ']' : ')';
    *at = '\0'; return source;
}

static void inspect(TermUniverse *universe, AtomId id, size_t depth, bool lists) {
    Atom *term = term_universe_get_atom(universe, id);
    assert(term);
    for (size_t i = 0u; i < depth; ++i) {
        if (lists) {
            assert(atom_is_list(term) && atom_list_len(term) == 1u);
            term = atom_list_elems(term)[0];
        } else {
            assert(term->kind == ATOM_EXPR && term->expr.len == 2u);
            assert(atom_is_symbol(term->expr.elems[0], "node"));
            term = term->expr.elems[1];
        }
    }
    assert(term->kind == ATOM_EXPR && term->expr.len == 3u);
    assert(atom_is_symbol(term->expr.elems[0], "Pair"));
    assert(term->expr.elems[1]->kind == ATOM_VAR &&
        var_id_eq(term->expr.elems[1]->var_id, term->expr.elems[2]->var_id));
}

int main(void) {
    CettaTermDomain *domain = cetta_term_domain_new_default(false);
    CettaTermDomainScope scope = {0};
    assert(domain && cetta_term_domain_enter(domain, &scope));
    TermUniverse *universe = cetta_term_domain_universe(domain);
    Context context = {universe, petta_grounding_v1_new(universe)};
    assert(context.grounding);
    GSLTDirectPeTTaProjectionV1 projection = {
        .context = &context, .begin_form = begin, .token_bytes = word,
        .variable_bytes = variable, .string_bytes = string, .dollar_symbol = dollar,
        .expression = expression, .list = list, .finish_form = finish};
    char error[512];
    for (unsigned lists = 0u; lists < 2u; ++lists) {
        const GSLTDirectPeTTaReaderV1Plan *plan = lists
            ? &petta_extended_reader_direct_v1_plan : &petta_reader_direct_v1_plan;
        assert(plan->depth_limit == GSLT_DIRECT_PETTA_DEPTH_UNBOUNDED_V1);
        assert(gslt_direct_petta_reader_v1_plan_validate(plan, error, sizeof error));
        char *source = nested(6000u, lists != 0u);
        AtomId id = CETTA_ATOM_ID_NONE;
        assert(gslt_direct_petta_reader_v1_parse_term_bytes_id(plan,
            (const uint8_t *)source, strlen(source), &projection, &id, error, sizeof error) == 0);
        inspect(universe, id, 6000u, lists != 0u);
        if (!lists) {
            PeTTaCompiledReaderV1 *reader = petta_compiled_reader_v1_new();
            assert(reader && petta_compiled_reader_v1_prepare(reader, error, sizeof error));
            PeTTaCompiledReaderV1Receipt receipt;
            AtomId *ids = NULL;
            assert(petta_compiled_reader_v1_parse_text_ids(reader, source, universe,
                &ids, &receipt, error, sizeof error) == 1 && ids);
            inspect(universe, ids[0], 6000u, false);
            assert(receipt.form_len == 1u && receipt.token_len == 12004u &&
                receipt.reduce_len == 12005u);
            free(ids); petta_compiled_reader_v1_free(reader);
        }
        GSLTDirectPeTTaReaderV1Plan bounded = *plan;
        bounded.depth_limit = 64u;
        id = CETTA_ATOM_ID_NONE;
        assert(gslt_direct_petta_reader_v1_parse_term_bytes_id(&bounded,
            (const uint8_t *)source, strlen(source), &projection, &id, error, sizeof error) < 0 &&
            id == CETTA_ATOM_ID_NONE);
        free(source);
        source = nested(20u, lists != 0u);
        assert(gslt_direct_petta_reader_v1_parse_term_bytes_id(&bounded,
            (const uint8_t *)source, strlen(source), &projection, &id, error, sizeof error) == 0);
        free(source);
        bounded.depth_limit = 0u;
        assert(!gslt_direct_petta_reader_v1_plan_validate(&bounded, error, sizeof error));
    }
    puts("deep expressions and lists retain shape and aliases; explicit allowances still apply");

    const char *token_data[] = {"(a ] )", "[|a]"};
    for (size_t i = 0u; i < sizeof token_data / sizeof token_data[0]; ++i) {
        AtomId id = CETTA_ATOM_ID_NONE;
        assert(gslt_direct_petta_reader_v1_parse_term_bytes_id(
            &petta_extended_reader_direct_v1_plan, (const uint8_t *)token_data[i],
            strlen(token_data[i]), &projection, &id, error, sizeof error) == 0 &&
            id != CETTA_ATOM_ID_NONE);
    }
    const char *invalid[] = {"(a (b)", "(a (b)) extra", "[a |]", "[a | b c]", "[a (b]", "(a [b)"};
    for (size_t i = 0u; i < sizeof invalid / sizeof invalid[0]; ++i) {
        AtomId id = CETTA_ATOM_ID_NONE;
        int result = gslt_direct_petta_reader_v1_parse_term_bytes_id(
            &petta_extended_reader_direct_v1_plan, (const uint8_t *)invalid[i],
            strlen(invalid[i]), &projection, &id, error, sizeof error);
        if (result == 0) fprintf(stderr, "unexpected accepted input: %s\n", invalid[i]);
        assert(result < 0 && id == CETTA_ATOM_ID_NONE);
    }
    puts("malformed children and list rests cannot reuse the preceding child's value");

    char *source = nested(80u, false);
    AtomId *ids = NULL;
    GSLTDirectPeTTaReaderV1Receipt receipt;
    allocation = 0u; fail_at = SIZE_MAX; fault_scope = true;
    int count = gslt_direct_petta_reader_v1_parse_bytes_ids(&petta_reader_direct_v1_plan,
        (const uint8_t *)source, strlen(source), &projection, &ids, &receipt, error, sizeof error);
    fault_scope = false;
    assert(count == 1 && ids);
    size_t allocations = allocation;
    assert(allocations > 80u);
    free(ids);
    for (size_t fault = 1u; fault <= allocations; ++fault) {
        ids = NULL; allocation = 0u; fail_at = fault; fault_scope = true;
        count = gslt_direct_petta_reader_v1_parse_bytes_ids(&petta_reader_direct_v1_plan,
            (const uint8_t *)source, strlen(source), &projection, &ids, &receipt, error, sizeof error);
        fault_scope = false;
        assert(count < 0 && !ids && error[0]);
        assert(gslt_direct_petta_reader_v1_parse_bytes_ids(&petta_reader_direct_v1_plan,
            (const uint8_t *)source, strlen(source), &projection, &ids, &receipt, error, sizeof error) == 1);
        inspect(universe, ids[0], 80u, false);
        free(ids);
    }
    printf("all %zu reader allocation refusals are explicit; replay retains the source\n", allocations);
    free(source);
    source = malloc(10003u); assert(source);
    source[0] = '(';
    for (size_t i = 0u; i < 5000u; ++i) { source[1u + i * 2u] = 'a'; source[2u + i * 2u] = ' '; }
    source[10001u] = ')'; source[10002u] = '\0';
    allocation = 0u; fail_at = SIZE_MAX; fault_scope = true;
    count = gslt_direct_petta_reader_v1_parse_bytes_ids(&petta_reader_direct_v1_plan,
        (const uint8_t *)source, strlen(source), &projection, &ids, &receipt, error, sizeof error);
    fault_scope = false;
    assert(count == 1 && ids && tu_arity(universe, ids[0]) == 5000u);
    AtomId expected = ids[0]; free(ids);
    allocations = allocation;
    assert(allocations > 8u);
    for (size_t fault = 1u; fault <= allocations; ++fault) {
        ids = NULL; allocation = 0u; fail_at = fault; fault_scope = true;
        count = gslt_direct_petta_reader_v1_parse_bytes_ids(&petta_reader_direct_v1_plan,
            (const uint8_t *)source, strlen(source), &projection, &ids, &receipt, error, sizeof error);
        fault_scope = false;
        assert(count < 0 && !ids && error[0]);
        assert(gslt_direct_petta_reader_v1_parse_bytes_ids(&petta_reader_direct_v1_plan,
            (const uint8_t *)source, strlen(source), &projection, &ids, &receipt, error, sizeof error) == 1);
        assert(ids[0] == expected); free(ids);
    }
    printf("all %zu wide-container allocation refusals are explicit; all 5000 children survive replay\n", allocations);
    free(source);
    petta_grounding_v1_free(context.grounding);
    cetta_term_domain_leave(&scope); cetta_term_domain_release(domain);
    puts("PASS: compiled PeTTa reader container worklist");
    return 0;
}
