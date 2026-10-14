/* Stack-lifecycle tests for the generated prefix reader, independent of the
 * host atom representation. The real host projection has separate parity
 * tests; these callbacks count events and inject projection failures. */
#include "generated/prime_reader_direct_v1.generated.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t next; uint32_t fail_at; uint32_t patterns; } Projection;

void *cetta_realloc(void *pointer, size_t size) {
    void *result = realloc(pointer, size);
    if (!result && size) abort();
    return result;
}

static bool begin(void *context) { return context != NULL; }
static AtomId atom(void *context) {
    Projection *state = context;
    if (++state->next == state->fail_at) return CETTA_ATOM_ID_NONE;
    return state->next;
}
static AtomId bytes(void *context, const uint8_t *value, size_t length) {
    (void)value; (void)length;
    return atom(context);
}
static AtomId expression(void *context, const AtomId *children, size_t length) {
    for (size_t i = 0; i < length; ++i) assert(children[i] != CETTA_ATOM_ID_NONE);
    return atom(context);
}
static AtomId prefix(void *context, GSLTDirectPrefixRoleV1 role, AtomId payload) {
    assert(role != GSLT_DIRECT_PREFIX_ROLE_INVALID && payload != CETTA_ATOM_ID_NONE);
    return atom(context);
}
static AtomId meta(void *context, AtomId term, AtomId argument) {
    assert(term != CETTA_ATOM_ID_NONE && argument != CETTA_ATOM_ID_NONE);
    return atom(context);
}
static AtomId braces(void *context, const AtomId *children, size_t length) {
    for (size_t i = 0; i < length; ++i) assert(children[i] != CETTA_ATOM_ID_NONE);
    return atom(context);
}
static AtomId list(void *context, const AtomId *elems, size_t length, AtomId rest) {
    Projection *state = context;
    for (size_t i = 0; i < length; ++i) assert(elems[i] != CETTA_ATOM_ID_NONE);
    if (rest != CETTA_ATOM_ID_NONE) {
        assert(length > 0);
        state->patterns++;
    }
    return atom(context);
}

static void check_lists(const char *source, size_t length, uint32_t depth,
                        uint32_t fail_at, bool accepted, uint32_t prefixes,
                        uint32_t patterns) {
    Projection state = {0, fail_at, 0};
    GSLTDirectPrefixProjectionV1 projection = {
        &state, begin, bytes, bytes, atom, bytes, expression, prefix, list,
        meta, braces};
    GSLTDirectPrefixReaderV1Plan plan = prime_reader_direct_v1_plan;
    GSLTDirectPrefixReaderV1Receipt receipt;
    AtomId *ids = NULL;
    char error[256];
    plan.depth_limit = depth;
    int count = gslt_direct_prefix_reader_v1_parse_bytes_ids(
        &plan, (const uint8_t *)source, length, &projection, &ids,
        &receipt, error, sizeof(error));
    assert((count == 1) == accepted);
    if (accepted) {
        assert(ids && ids[0] != CETTA_ATOM_ID_NONE);
        assert(receipt.input_byte_len == length);
        assert(receipt.prefix_len == prefixes);
        assert(receipt.token_len == state.next && receipt.reduce_len == state.next);
        assert(state.patterns == patterns);
    } else {
        assert(count == -1 && !ids && error[0]);
        assert(receipt.source_pass_count == 0);
    }
    free(ids);
}

static void check(const char *source, size_t length, uint32_t depth,
                  uint32_t fail_at, bool accepted, uint32_t prefixes) {
    check_lists(source, length, depth, fail_at, accepted, prefixes, 0);
}

int main(void) {
    const size_t nesting = 65536;
    char *source = malloc(6 * nesting + 2);
    assert(source);
    for (size_t i = 0; i < nesting; ++i) memcpy(source + 3 * i, "(a ", 3);
    source[3 * nesting] = 'b';
    memset(source + 3 * nesting + 1, ')', nesting);
    size_t length = 4 * nesting + 1;
    source[length] = '\0';
    check(source, length, UINT32_MAX, 0, true, 0);
    check(source, length - 1, UINT32_MAX, 0, false, 0);
    check(source, length, UINT32_MAX, (uint32_t)nesting + 1, false, 0);
    check(source, length, 512, 0, false, 0);
    memset(source, '@', nesting);
    source[nesting] = 'a';
    check(source, nesting + 1, UINT32_MAX, 0, true, (uint32_t)nesting);
    check(source, nesting, UINT32_MAX, 0, false, 0);
    check(source, nesting + 1, UINT32_MAX, 2, false, 0);
    check("@a", 2, 1, 0, false, 0);
    check("@a", 2, 2, 0, true, 1);
    check("(a)", 3, 1, 0, false, 0);
    check("(a)", 3, 2, 0, true, 0);
    /* Lists are frames of the same explicit stack. */
    for (size_t i = 0; i < nesting; ++i) memcpy(source + 3 * i, "[a ", 3);
    source[3 * nesting] = 'b';
    memset(source + 3 * nesting + 1, ']', nesting);
    source[length] = '\0';
    check(source, length, UINT32_MAX, 0, true, 0);
    check(source, length - 1, UINT32_MAX, 0, false, 0);
    check(source, length, UINT32_MAX, (uint32_t)nesting + 1, false, 0);
    check(source, length, 512, 0, false, 0);
    /* A rest may itself be a list pattern, to any depth. */
    for (size_t i = 0; i < nesting; ++i) memcpy(source + 5 * i, "[a | ", 5);
    source[5 * nesting] = 'b';
    memset(source + 5 * nesting + 1, ']', nesting);
    size_t rest_length = 6 * nesting + 1;
    source[rest_length] = '\0';
    check_lists(source, rest_length, UINT32_MAX, 0, true, 0, (uint32_t)nesting);
    check(source, rest_length - 1, UINT32_MAX, 0, false, 0);
    check(source, rest_length, 512, 0, false, 0);
    check_lists("[a b | c]", 9, UINT32_MAX, 0, true, 0, 1);
    check("[]", 2, 1, 0, true, 0);
    check("[a]", 3, 1, 0, false, 0);
    check("[a]", 3, 2, 0, true, 0);
    check("[a]", 3, 2, 2, false, 0);
    check("[a | b]", 7, UINT32_MAX, 3, false, 0);
    check("[| a]", 5, UINT32_MAX, 0, false, 0);
    check("[a | b c]", 9, UINT32_MAX, 0, false, 0);
    check("[@a]", 4, UINT32_MAX, 0, true, 1);
    check("([a (b [c])])", 13, UINT32_MAX, 0, true, 0);
    /* A bracket touching a term, a meta-argument, is a frame of the same
     * stack: brackets in a row, and an element holding one, to any depth. */
    {
        char *chain = malloc(8 * nesting + 2);
        assert(chain);
        chain[0] = 'a';
        for (size_t i = 0; i < nesting; ++i) memcpy(chain + 1 + 8 * i, "[x := y]", 8);
        chain[1 + 8 * nesting] = '\0';
        check(chain, 1 + 8 * nesting, UINT32_MAX, 0, true, 0);
        check(chain, 8 * nesting, UINT32_MAX, 0, false, 0);
        free(chain);
    }
    {
        char *nested = malloc(8 * nesting + 2);
        assert(nested);
        nested[0] = 'a';
        for (size_t i = 0; i < nesting; ++i) memcpy(nested + 1 + 7 * i, "[x := y", 7);
        memset(nested + 1 + 7 * nesting, ']', nesting);
        size_t nested_length = 1 + 8 * nesting;
        nested[nested_length] = '\0';
        check(nested, nested_length, UINT32_MAX, 0, true, 0);
        check(nested, nested_length - 1, UINT32_MAX, 0, false, 0);
        check(nested, nested_length, 512, 0, false, 0);
        free(nested);
    }
    check("a[x := y]", 9, UINT32_MAX, 0, true, 0);
    check("a[x := y]", 9, 1, 0, false, 0);
    check("@a[x := y]", 10, UINT32_MAX, 0, true, 1);
    check("a[x := y]", 9, UINT32_MAX, 4, false, 0);
    check("(a [x y])", 9, UINT32_MAX, 0, true, 0);
    check("a[x y]", 6, UINT32_MAX, 0, true, 0);
    check("a[x :=]", 7, UINT32_MAX, 0, true, 0);
    check("a[]", 3, UINT32_MAX, 0, true, 0);
    check("a[x | y]", 8, UINT32_MAX, 0, false, 0);
    check("[x := y]", 8, UINT32_MAX, 0, false, 0);
    /* Braces: a braces node, and braces touching a term, frames of the same
     * stack, to any depth. */
    {
        char *chain = malloc(5 * nesting + 2);
        assert(chain);
        chain[0] = 'a';
        for (size_t i = 0; i < nesting; ++i) memcpy(chain + 1 + 5 * i, "{x y}", 5);
        chain[1 + 5 * nesting] = '\0';
        check(chain, 1 + 5 * nesting, UINT32_MAX, 0, true, 0);
        check(chain, 5 * nesting, UINT32_MAX, 0, false, 0);
        free(chain);
    }
    for (size_t i = 0; i < nesting; ++i) memcpy(source + 3 * i, "{a ", 3);
    source[3 * nesting] = 'b';
    memset(source + 3 * nesting + 1, '}', nesting);
    source[length] = '\0';
    check(source, length, UINT32_MAX, 0, true, 0);
    check(source, length - 1, UINT32_MAX, 0, false, 0);
    check(source, length, 512, 0, false, 0);
    check("a{x}", 4, UINT32_MAX, 0, true, 0);
    check("a{}", 3, UINT32_MAX, 0, true, 0);
    check("{}", 2, UINT32_MAX, 0, true, 0);
    check("@a{x}", 5, UINT32_MAX, 0, true, 1);
    check("a{x}[y := z]", 12, UINT32_MAX, 0, true, 0);
    check("a{x", 3, UINT32_MAX, 0, false, 0);
    check("a{x}", 4, UINT32_MAX, 3, false, 0);
    check("}", 1, UINT32_MAX, 0, false, 0);
    free(source);
    puts("(GSLTPrefixReaderStackV1Summary cases=56 depth=65536)");
    return 0;
}
