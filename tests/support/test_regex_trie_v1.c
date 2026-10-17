#define _POSIX_C_SOURCE 200809L
#include "native/regex_trie_v1.h"
#include "symbol.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef CETTA_TRIE_ALLOCATION_TEST
static size_t allocation_allowance = SIZE_MAX;
void *cetta_test_trie_malloc(size_t n) {
    if (!allocation_allowance) return NULL;
    if (allocation_allowance != SIZE_MAX) --allocation_allowance;
    return malloc(n);
}
void *cetta_test_trie_calloc(size_t n, size_t width) {
    if (!allocation_allowance) return NULL;
    if (allocation_allowance != SIZE_MAX) --allocation_allowance;
    return calloc(n, width);
}
void *cetta_test_trie_realloc(void *p, size_t n) {
    if (!allocation_allowance) return NULL;
    if (allocation_allowance != SIZE_MAX) --allocation_allowance;
    return realloc(p, n);
}
#endif

#define OP(kind, next, second, arg) {CETTA_RE_V1_##kind, next, second, arg}
static unsigned passed, failed;
static void check(bool ok, const char *name) {
    if (ok) ++passed; else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
}
static CettaReProgramV1 *program(const CettaReInstructionV1 *ops, uint32_t len,
                                uint32_t groups, const CettaReRangeV1 *ranges,
                                uint32_t range_len) {
    static const CettaReRangeV1 word[] = {{48,57},{65,90},{95,95},{97,122}};
    CettaReProgramSpecV1 spec = {.instructions = ops, .instruction_len = len,
        .group_len = groups, .ranges = ranges, .range_len = range_len,
        .word_ranges = word, .word_range_len = 4u};
    CettaReProgramV1 *p = NULL; char error[256];
    check(cetta_re_v1_program(&spec, &p, error, sizeof(error)), "program ingress"); return p;
}
static bool same(const CettaReResultV1 *a, const CettaReResultV1 *b) {
    if (a->outcome != b->outcome || a->group_len != b->group_len) return false;
    for (uint32_t i = 0; i < a->group_len; ++i)
        if (a->groups[i].participated != b->groups[i].participated ||
            (a->groups[i].participated && (a->groups[i].start != b->groups[i].start ||
                                         a->groups[i].end != b->groups[i].end))) return false;
    return true;
}
static CettaReProgramV1 *literals(const char *const *words, unsigned count) {
    size_t n = count; /* count-1 splits and one shared MATCH. */
    for (unsigned i = 0; i < count; ++i) n += strlen(words[i]);
    CettaReInstructionV1 *ops = calloc(n, sizeof(*ops));
    uint32_t *starts = calloc(count, sizeof(*starts));
    uint32_t at = count - 1u, match = (uint32_t)n - 1u;
    for (unsigned i = 0; i < count; ++i) {
        starts[i] = strlen(words[i]) ? at : match;
        for (size_t j = 0; j < strlen(words[i]); ++j) {
            ops[at] = (CettaReInstructionV1)OP(CHAR, j + 1u < strlen(words[i]) ? at + 1u : match,
                                             0u, (unsigned char)words[i][j]); ++at;
        }
    }
    for (unsigned i = 0; i + 1u < count; ++i)
        ops[i] = (CettaReInstructionV1)OP(SPLIT, starts[i], i + 2u < count ? i + 1u : starts[i + 1u], 0u);
    ops[match] = (CettaReInstructionV1)OP(MATCH,0u,0u,0u);
    CettaReProgramV1 *p = program(ops, (uint32_t)n, 1u, NULL, 0u);
    free(ops); free(starts); return p;
}
static void compare_input(CettaReProgramV1 *p, const uint8_t *s, size_t n) {
    for (unsigned mode = 0; mode < 3u; ++mode) {
        for (size_t start = 0; start <= n; ++start) {
            if ((mode == CETTA_RE_V1_FULL && start) || (start < n && (s[start] & 0xc0u) == 0x80u)) continue;
            CettaReResultV1 a = cetta_re_v1_find(p, s, n, start, (CettaReModeV1)mode, 0u);
            CettaReResultV1 b = cetta_re_v1_find_reference_valid_utf8(p, s, n, start,
                (CettaReModeV1)mode, NULL);
            if (!same(&a, &b)) fprintf(stderr, "DIFF text=%.*s mode=%u start=%zu a=%u %zu..%zu b=%u %zu..%zu\n",
                (int)n, (const char *)s, mode, start, a.outcome,
                a.group_len ? a.groups[0].start : 0u, a.group_len ? a.groups[0].end : 0u,
                b.outcome, b.group_len ? b.groups[0].start : 0u, b.group_len ? b.groups[0].end : 0u);
            check(same(&a, &b), "literal trie preserves selected observation");
            cetta_re_v1_result_free(&a); cetta_re_v1_result_free(&b);
        }
    }
}
static void literal_differential(void) {
    static const char *const words[] = {"", "a", "b", "aa", "ab", "ba", "bb", "aaa", "aab", "aba", "abb", "baa", "bab", "bba", "bbb"};
    for (unsigned a = 0; a < 15u; ++a) for (unsigned b = 0; b < 15u; ++b) {
        const char *pair[] = {words[a], words[b]}; CettaReProgramV1 *p = literals(pair, 2u);
        check(cetta_re_v1_literal_accelerated(p), "pure literal alternatives are admitted");
        for (unsigned text = 0; text < 15u; ++text)
            compare_input(p, (const uint8_t *)words[text], strlen(words[text]));
        cetta_re_v1_release(p);
    }
    const char *priority[] = {"bc", "abcd", "b", "a", "abcd"};
    CettaReProgramV1 *p = literals(priority, 5u); compare_input(p, (const uint8_t *)"xxabcdabc", 9u);
    uint8_t *image = NULL; size_t len = 0; char error[256]; CettaReProgramV1 *restored = NULL;
    check(cetta_re_v1_store(p, &image, &len, error, sizeof(error)) &&
          cetta_re_v1_restore(image, len, &restored, error, sizeof(error)) &&
          !cetta_re_v1_literal_accelerated(restored), "CRP2 restore never rebuilds an unstored accelerator");
    compare_input(restored, (const uint8_t *)"xxabcdabc", 9u);
    cetta_re_v1_release(restored); free(image); cetta_re_v1_release(p);
    const CettaReInstructionV1 unicode[] = {
        OP(SPLIT,1,3,0), OP(CHAR,2,0,0x3bb), OP(CHAR,4,0,'a'), OP(CHAR,4,0,0x3bb), OP(MATCH,0,0,0)};
    p = program(unicode, 5u, 1u, NULL, 0u);
    check(cetta_re_v1_literal_accelerated(p), "Unicode literals share scalar trie");
    const uint8_t input[] = {'x',0xce,0xbb,'a',0,'x'}; compare_input(p, input, sizeof(input));
    cetta_re_v1_release(p);
    const CettaReInstructionV1 captured[] = {OP(SAVE,1,0,2),OP(CHAR,2,0,'a'),OP(SAVE,3,0,3),OP(MATCH,0,0,0)};
    p = program(captured, 4u, 2u, NULL, 0u);
    check(!cetta_re_v1_literal_accelerated(p), "capture-sensitive shape falls back unchanged");
    compare_input(p, (const uint8_t *)"aa", 2u); cetta_re_v1_release(p);
    const CettaReInstructionV1 sequence_choice[] = {OP(CHAR,1,0,'a'),OP(SPLIT,2,3,0),OP(CHAR,4,0,'b'),OP(CHAR,4,0,'c'),OP(MATCH,0,0,0)};
    p = program(sequence_choice, 5u, 1u, NULL, 0u);
    check(!cetta_re_v1_literal_accelerated(p), "post-consumption branching is not silently admitted");
    compare_input(p, (const uint8_t *)"xac", 3u); cetta_re_v1_release(p);
}

static void check_query(SymbolTable *symbols, CettaReProgramV1 *p, CettaReModeV1 mode) {
    CettaReNameAutomatonV1 *a = NULL;
    check(cetta_re_v1_name_automaton(p, mode, NULL, &a), "regular name automaton prepares");
    if (!a) return;
    SymbolNameQueryResult result = {0};
    check(symbol_table_query_names(symbols, cetta_re_v1_name_automaton_view(a),
          cetta_re_v1_name_automaton_initial(a), &result), "symbol index product query succeeds");
    size_t at = 0; uint32_t invalid = 0u; bool equal = true;
    for (SymbolId id = 1u; id < result.snapshot_count; ++id) {
        SymbolSpelling s = symbol_spelling(symbols, id);
        if (!cetta_re_v1_valid_utf8((const uint8_t *)s.bytes, s.len)) { ++invalid; continue; }
        CettaReResultV1 reference = cetta_re_v1_find_reference_valid_utf8(p,
            (const uint8_t *)s.bytes, s.len, 0u, mode, NULL);
        if (reference.outcome == CETTA_RE_V1_FOUND) {
            equal = equal && at < result.len && result.ids[at] == id; ++at;
        } else equal = equal && reference.outcome == CETTA_RE_V1_NO_MATCH;
        cetta_re_v1_result_free(&reference);
    }
    check(equal && at == result.len && invalid == result.invalid_utf8_count,
          "product query equals independent filtering, snapshot/order/opaque names included");
    symbol_name_query_free(&result); cetta_re_v1_name_automaton_free(a);
}
static uint32_t random_state = 91u;
static uint32_t random_word(void) { random_state = random_state * 1664525u + 1013904223u; return random_state; }
static void query_differential(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    char word[8];
    for (unsigned n = 1u; n <= 4u; ++n) for (unsigned value = 0; value < (1u << n); ++value) {
        for (unsigned i = 0; i < n; ++i) word[i] = (value & (1u << i)) ? 'a' : 'b';
        word[n] = 0; symbol_intern_cstr(&symbols, word);
    }
    const uint8_t nul[] = {'a',0,'b'}, unicode[] = {0xce,0xbb,'a'}, bad[] = {'a',0xff,'b'};
    symbol_intern_bytes(&symbols, nul, sizeof(nul)); symbol_intern_bytes(&symbols, unicode, sizeof(unicode));
    symbol_intern_bytes(&symbols, bad, sizeof(bad)); symbol_intern_cstr(&symbols, "a\nb");
    const CettaReRangeV1 range[] = {{'a','b'}};
    for (unsigned trial = 0; trial < 256u; ++trial) {
        CettaReInstructionV1 ops[12];
        for (uint32_t pc = 0; pc < 11u; ++pc) {
            uint32_t next = 1u + random_word() % 11u, second = 1u + random_word() % 11u;
            switch (random_word() % 9u) {
            case 0: ops[pc] = (CettaReInstructionV1)OP(FAIL,0,0,0); break;
            case 1: ops[pc] = (CettaReInstructionV1)OP(CHAR,next,0,'a'); break;
            case 2: ops[pc] = (CettaReInstructionV1)OP(CHAR,next,0,'b'); break;
            case 3: ops[pc] = (CettaReInstructionV1)OP(ANY,next,0,0); break;
            case 4: ops[pc] = (CettaReInstructionV1)OP(CLASS,next,1,0); break;
            case 5: ops[pc] = (CettaReInstructionV1)OP(SPLIT,next,second,0); break;
            case 6: ops[pc] = (CettaReInstructionV1)OP(JUMP,next,0,0); break;
            case 7: ops[pc] = (CettaReInstructionV1)OP(JUMP,next,0,0); break;
            default: ops[pc] = (CettaReInstructionV1)OP(ASSERT,next,0,random_word() % 8u); break;
            }
        }
        ops[11] = (CettaReInstructionV1)OP(MATCH,0,0,0);
        CettaReProgramV1 *p = program(ops, 12u, 2u, range, 1u);
        for (unsigned mode = 0; mode < 3u; ++mode) check_query(&symbols, p, (CettaReModeV1)mode);
        cetta_re_v1_release(p);
    }
    SymbolNameQueryResult snapshot = {0};
    check(symbol_table_query_names(&symbols, NULL, NULL, &snapshot) && snapshot.invalid_utf8_count == 1u,
        "fallback snapshot exposes opaque spelling count");
    uint32_t before = snapshot.snapshot_count; symbol_intern_cstr(&symbols, "after-snapshot");
    check(snapshot.snapshot_count == before && snapshot.ids[snapshot.len - 1u] < before,
        "captured result does not change under later interning");
    symbol_name_query_free(&snapshot);
    const CettaReInstructionV1 later[] = {OP(ANY,1,0,0),OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(later, 2u, 1u, NULL, 0u);
    check_query(&symbols, p, CETTA_RE_V1_SEARCH); cetta_re_v1_release(p);
    const CettaReInstructionV1 bad_capture[] = {
        OP(SAVE,1,0,3),OP(CHAR,2,0,'a'),OP(SAVE,3,0,2),OP(MATCH,0,0,0)};
    p = program(bad_capture, 4u, 2u, NULL, 0u);
    CettaReNameAutomatonV1 *a = NULL;
    check(!cetta_re_v1_name_automaton(p, CETTA_RE_V1_SEARCH, NULL, &a),
        "uncertified capture flow cannot enter erased name automaton");
    CettaReResultV1 fault = cetta_re_v1_find(p, (const uint8_t *)"a", 1u, 0u, CETTA_RE_V1_SEARCH, 0u);
    check(fault.outcome == CETTA_RE_V1_INVALID_ARGUMENT,
        "selected fallback retains reversed-capture fault rather than invented match");
    cetta_re_v1_result_free(&fault);
    uint8_t *image = NULL; size_t image_len; char error[256]; CettaReProgramV1 *restored = NULL;
    check(cetta_re_v1_store(p, &image, &image_len, error, sizeof(error)) &&
        cetta_re_v1_restore(image, image_len, &restored, error, sizeof(error)) &&
        !cetta_re_v1_name_automaton(restored, CETTA_RE_V1_SEARCH, NULL, &a),
        "restoring raw capture graph cannot bypass name-query admission");
    free(image); cetta_re_v1_release(restored); cetta_re_v1_release(p);
    symbol_table_free(&symbols);
}
typedef struct { SymbolTable *symbols; atomic_bool done; } Writer;
static void *write_names(void *context) {
    Writer *writer = context; char name[64];
    for (unsigned i = 0; i < 500u; ++i) {
        snprintf(name, sizeof(name), "thread-name-%u", i); symbol_intern_cstr(writer->symbols, name);
    }
    atomic_store_explicit(&writer->done, true, memory_order_release); return NULL;
}
static bool always_cancel(void *unused) { (void)unused; return true; }
static void policies_and_concurrency(void) {
    SymbolTable symbols; symbol_table_init(&symbols); symbol_intern_cstr(&symbols, "initial");
    const CettaReInstructionV1 ops[] = {OP(CHAR,1,0,'t'),OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(ops, 2u, 1u, NULL, 0u);
    Writer writer = {.symbols = &symbols}; atomic_init(&writer.done, false); pthread_t thread;
    check(pthread_create(&thread, NULL, write_names, &writer) == 0, "interner writer starts");
    for (unsigned i = 0; i < 16u; ++i) check_query(&symbols, p, CETTA_RE_V1_PREFIX);
    pthread_join(thread, NULL); check_query(&symbols, p, CETTA_RE_V1_PREFIX);
    SymbolNameQueryResult old = {0}; check(symbol_table_query_names(&symbols, NULL, NULL, &old), "old query owner");
    SymbolId *saved = old.ids; size_t old_len = old.len;
    CettaReExecutionPolicyV1 policy = {.cancelled = always_cancel}; CettaReNameAutomatonV1 *a = NULL;
    check(cetta_re_v1_name_automaton(p, CETTA_RE_V1_SEARCH, &policy, &a) &&
        !symbol_table_query_names(&symbols, cetta_re_v1_name_automaton_view(a),
                                  cetta_re_v1_name_automaton_initial(a), &old) &&
        cetta_re_v1_name_automaton_outcome(a) == CETTA_RE_V1_CANCELLED &&
        old.ids == saved && old.len == old_len, "cancelled query preserves previous owner, not empty success");
    cetta_re_v1_name_automaton_free(a); a = NULL; policy = (CettaReExecutionPolicyV1){.work_budget = 1u};
    check(cetta_re_v1_name_automaton(p, CETTA_RE_V1_SEARCH, &policy, &a) &&
        !symbol_table_query_names(&symbols, cetta_re_v1_name_automaton_view(a),
                                  cetta_re_v1_name_automaton_initial(a), &old) &&
        cetta_re_v1_name_automaton_outcome(a) == CETTA_RE_V1_INCOMPLETE && old.ids == saved,
        "query work exhaustion is incomplete and atomic");
    cetta_re_v1_name_automaton_free(a); symbol_name_query_free(&old);
    CettaReResultV1 cancelled = cetta_re_v1_find_policy_valid_utf8(p, (const uint8_t *)"t", 1u,
        0u, CETTA_RE_V1_SEARCH, &(CettaReExecutionPolicyV1){.cancelled = always_cancel});
    check(cancelled.outcome == CETTA_RE_V1_CANCELLED && !cancelled.groups,
        "literal accelerator preserves cancellation outcome"); cetta_re_v1_result_free(&cancelled);
    cetta_re_v1_release(p); symbol_table_free(&symbols);
}
static void allocation_controls(void) {
#ifdef CETTA_TRIE_ALLOCATION_TEST
    bool reached_success = false;
    for (size_t allowance = 0; allowance < 128u && !reached_success; ++allowance) {
        allocation_allowance = allowance;
        CettaScalarTrie *t = cetta_scalar_trie_new();
        bool ok = t && cetta_scalar_trie_insert_utf8(t, (const uint8_t *)"alphabet", 8u, 1u) &&
            cetta_scalar_trie_insert_utf8(t, (const uint8_t *)"alpine", 6u, 2u) && cetta_scalar_trie_prepare(t);
        cetta_scalar_trie_free(t); allocation_allowance = SIZE_MAX;
        if (ok) reached_success = true;
    }
    check(reached_success, "every fallible trie-construction allocation unwinds cleanly");
    SymbolTable symbols; symbol_table_init(&symbols); symbol_intern_cstr(&symbols, "before");
    const CettaReInstructionV1 ops[] = {OP(ANY,1,0,0),OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(ops, 2u, 1u, NULL, 0u); CettaReNameAutomatonV1 *a = NULL;
    check(cetta_re_v1_name_automaton(p, CETTA_RE_V1_SEARCH, NULL, &a), "allocation-test automaton");
    SymbolNameQueryResult result = {0}; symbol_table_query_names(&symbols, NULL, NULL, &result);
    SymbolId *old = result.ids; allocation_allowance = 0;
    bool queried = symbol_table_query_names(&symbols, cetta_re_v1_name_automaton_view(a),
        cetta_re_v1_name_automaton_initial(a), &result);
    allocation_allowance = SIZE_MAX;
    check(!queried && result.ids == old && result.len == 1u,
        "failed lazy-index allocation preserves previous result owner");
    check(symbol_table_query_names(&symbols, cetta_re_v1_name_automaton_view(a),
        cetta_re_v1_name_automaton_initial(a), &result), "failed index can subsequently rebuild");
    allocation_allowance = 0; SymbolId added = symbol_intern_cstr(&symbols, "after");
    allocation_allowance = SIZE_MAX;
    check(added != SYMBOL_ID_NONE && symbol_table_query_names(&symbols,
        cetta_re_v1_name_automaton_view(a), cetta_re_v1_name_automaton_initial(a), &result) &&
        result.len == 2u, "optional index growth failure never loses interned names");
    /* Fail destination-state allocation on each traversal prefix. */
    reached_success = false;
    for (size_t allowance = 0; allowance < 128u && !reached_success; ++allowance) {
        allocation_allowance = allowance;
        reached_success = symbol_table_query_names(&symbols, cetta_re_v1_name_automaton_view(a),
            cetta_re_v1_name_automaton_initial(a), &result);
        allocation_allowance = SIZE_MAX;
    }
    check(reached_success && result.len == 2u, "all partial product-walk states release on allocation refusal");
    symbol_name_query_free(&result); cetta_re_v1_name_automaton_free(a);
    cetta_re_v1_release(p); symbol_table_free(&symbols);
#endif
}
static void deep_controls(void) {
    size_t depth = 100000u;
    CettaReInstructionV1 *ops = calloc(depth + 1u, sizeof(*ops));
    uint8_t *text = malloc(depth);
    if (!ops || !text) { check(false, "deep-control fixture allocation"); free(ops); free(text); return; }
    memset(text, 'a', depth);
    for (uint32_t i = 0; i < depth; ++i) ops[i] = (CettaReInstructionV1)OP(CHAR,i+1u,0,'a');
    ops[depth] = (CettaReInstructionV1)OP(MATCH,0,0,0);
    CettaReProgramV1 *p = program(ops, (uint32_t)depth + 1u, 1u, NULL, 0u);
    check(cetta_re_v1_literal_accelerated(p), "deep literal preparation has no recursive traversal or default depth cap");
    CettaReResultV1 result = cetta_re_v1_find(p, text, depth, 0u, CETTA_RE_V1_FULL, 0u);
    check(result.outcome == CETTA_RE_V1_FOUND && result.groups[0].start == 0u && result.groups[0].end == depth,
        "deep literal selection retains exact entire-input span");
    cetta_re_v1_result_free(&result); cetta_re_v1_release(p); free(ops);
    SymbolTable symbols; symbol_table_init(&symbols); SymbolId id = symbol_intern_bytes(&symbols, text, (uint32_t)depth);
    const CettaReInstructionV1 any[] = {OP(ANY,1,0,0),OP(MATCH,0,0,0)};
    p = program(any, 2u, 1u, NULL, 0u); CettaReNameAutomatonV1 *a = NULL; SymbolNameQueryResult names = {0};
    check(cetta_re_v1_name_automaton(p, CETTA_RE_V1_PREFIX, NULL, &a) &&
        symbol_table_query_names(&symbols, cetta_re_v1_name_automaton_view(a),
            cetta_re_v1_name_automaton_initial(a), &names) && names.len == 1u && names.ids[0] == id,
        "deep trie product walk uses owned explicit traversal frames");
    symbol_name_query_free(&names); cetta_re_v1_name_automaton_free(a);
    cetta_re_v1_release(p); symbol_table_free(&symbols); free(text);
}
static uint64_t nanoseconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void benchmark(void) {
    size_t count = 10000u; char **words = calloc(count, sizeof(*words));
    for (size_t i = 0; i < count; ++i) {
        words[i] = malloc(32u); snprintf(words[i], 32u, "keyword%05zu", i);
    }
    uint64_t begin = nanoseconds(); CettaReProgramV1 *p = literals((const char *const *)words, (unsigned)count);
    uint64_t prepare = nanoseconds() - begin;
    const uint8_t input[] = "miss keyword09876"; size_t n = sizeof(input) - 1u;
    CettaReResultV1 fast = cetta_re_v1_find(p, input, n, 0u, CETTA_RE_V1_SEARCH, 0u);
    CettaReResultV1 reference = cetta_re_v1_find_reference_valid_utf8(p, input, n, 0u, CETTA_RE_V1_SEARCH, NULL);
    check(same(&fast, &reference), "benchmark input is semantically matched");
    uint64_t fast_work = fast.state_visits, reference_work = reference.state_visits;
    cetta_re_v1_result_free(&fast); cetta_re_v1_result_free(&reference);
    begin = nanoseconds();
    for (unsigned i = 0; i < 1000u; ++i) {
        fast = cetta_re_v1_find(p, input, n, 0u, CETTA_RE_V1_SEARCH, 0u); cetta_re_v1_result_free(&fast);
    }
    uint64_t fast_ns = (nanoseconds() - begin) / 1000u;
    begin = nanoseconds();
    for (unsigned i = 0; i < 10u; ++i) {
        reference = cetta_re_v1_find_reference_valid_utf8(p, input, n, 0u, CETTA_RE_V1_SEARCH, NULL);
        cetta_re_v1_result_free(&reference);
    }
    uint64_t reference_ns = (nanoseconds() - begin) / 10u;
    printf("{\"literal_count\":%zu,\"graph_and_trie_prepare_ns\":%llu,\"fast_find_ns\":%llu,"
           "\"reference_find_ns\":%llu,\"fast_visits\":%llu,\"reference_visits\":%llu}\n", count,
        (unsigned long long)prepare, (unsigned long long)fast_ns, (unsigned long long)reference_ns,
        (unsigned long long)fast_work, (unsigned long long)reference_work);
    for (size_t i = 0; i < count; ++i) free(words[i]);
    free(words); cetta_re_v1_release(p);
}
int main(int argc, char **argv) {
    literal_differential(); query_differential(); policies_and_concurrency();
    allocation_controls(); deep_controls();
    if (argc == 2 && !strcmp(argv[1], "--benchmark") && !failed) benchmark();
    printf("(RegexTrieV1Summary %u %u)\n", passed, failed); return failed ? 1 : 0;
}
