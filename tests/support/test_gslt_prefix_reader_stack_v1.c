/* Stack-lifecycle tests for the generated prefix reader, independent of the
 * host atom representation. The real host projection has separate parity
 * tests; these callbacks count events and inject projection failures. */
#include "generated/prime_reader_direct_v1.generated.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t next; uint32_t fail_at; } Projection;

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

static void check(const char *source, size_t length, uint32_t depth,
                  uint32_t fail_at, bool accepted, uint32_t prefixes) {
    Projection state = {0, fail_at};
    GSLTDirectPrefixProjectionV1 projection = {
        &state, begin, bytes, bytes, atom, bytes, expression, prefix};
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
    } else {
        assert(count == -1 && !ids && error[0]);
        assert(receipt.source_pass_count == 0);
    }
    free(ids);
}

int main(void) {
    const size_t nesting = 65536;
    char *source = malloc(4 * nesting + 2);
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
    free(source);
    puts("(GSLTPrefixReaderStackV1Summary cases=11 depth=65536)");
    return 0;
}
