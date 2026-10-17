#include "native/regex_selection_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed, failed;
static void check(bool ok, const char *message) {
    if (ok) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", message); }
}

#define OP(name, next, second, arg) {CETTA_RE_V1_##name, next, second, arg}
static CettaReProgramV1 *program(const CettaReInstructionV1 *ops, size_t len, uint32_t groups,
    const uint32_t *ranks, const CettaReRangeV1 *ranges, size_t range_len,
    const CettaReFoldPairV1 *folds, size_t fold_len) {
    CettaReProgramSpecV1 spec = {.instructions = ops, .instruction_len = (uint32_t)len,
        .group_len = groups, .ranks = ranks, .ranges = ranges, .range_len = (uint32_t)range_len,
        .fold_pairs = folds, .fold_pair_len = (uint32_t)fold_len};
    CettaReProgramV1 *p = NULL; char error[256] = {0};
    bool ok = cetta_re_v1_program(&spec, &p, error, sizeof(error));
    check(ok, "validated graph ingress");
    if (!ok) fprintf(stderr, "%s\n", error);
    return p;
}

static void observation(CettaReProgramV1 *p, const char *input, CettaReModeV1 mode,
    CettaReOutcomeV1 outcome, size_t start, size_t end, int capture,
    size_t capture_start, size_t capture_end, const char *message) {
    if (!p) return;
    uint8_t *bytes = NULL; size_t len = 0; char error[256];
    CettaReProgramV1 *restored = NULL;
    check(cetta_re_v1_store(p, &bytes, &len, error, sizeof(error)) &&
          cetta_re_v1_restore(bytes, len, &restored, error, sizeof(error)), "owned artifact restoration");
    if (bytes) check(!memcmp(bytes, "CRP3", 4u), "extended profile is distinct from CRP2");
    for (unsigned copy = 0; copy < 2u; ++copy) {
        CettaReResultV1 r = cetta_re_v1_find(copy ? restored : p,
            (const uint8_t *)input, strlen(input), 0u, mode, 0u);
        bool ok = r.outcome == outcome;
        if (ok && outcome == CETTA_RE_V1_FOUND) {
            ok = r.group_len && r.groups[0].start == start && r.groups[0].end == end;
            if (capture > 0) ok = ok && (uint32_t)capture < r.group_len &&
                r.groups[capture].participated && r.groups[capture].start == capture_start &&
                r.groups[capture].end == capture_end;
            if (capture < 0) ok = ok && (uint32_t)-capture < r.group_len && !r.groups[-capture].participated;
        }
        check(ok, message);
        if (!ok) {
            fprintf(stderr, "  %s %zu..%zu", cetta_re_v1_outcome_name(r.outcome),
                r.group_len ? r.groups[0].start : 0u, r.group_len ? r.groups[0].end : 0u);
            for (uint32_t i = 1; i < r.group_len; ++i)
                fprintf(stderr, " g%u=%d:%zu..%zu", i, r.groups[i].participated, r.groups[i].start, r.groups[i].end);
            fputc('\n', stderr);
        }
        if (outcome == CETTA_RE_V1_FOUND || outcome == CETTA_RE_V1_NO_MATCH) {
            CettaReResultV1 stopped = cetta_re_v1_find(copy ? restored : p,
                (const uint8_t *)input, strlen(input), 0u, mode, 1u);
            check(stopped.outcome == CETTA_RE_V1_INCOMPLETE ||
                (r.state_visits <= 1u && stopped.outcome == r.outcome),
                "caller cutoff is not absence unless observation actually completes");
            cetta_re_v1_result_free(&stopped);
        }
        cetta_re_v1_result_free(&r);
    }
    CettaReProgramV1 *owner = restored;
    check(!cetta_re_v1_restore(bytes, len ? len - 1u : 0u, &restored, error, sizeof(error)) &&
        restored == owner, "truncated extended artifact preserves the existing owner");
    if (bytes) bytes[0] ^= 1u;
    check(!cetta_re_v1_restore(bytes, len, &restored, error, sizeof(error)) && restored == owner,
        "wrong extended profile preserves the existing owner");
    free(bytes); cetta_re_v1_release(restored);
}

static void backreferences(void) {
    const CettaReInstructionV1 repeated[] = {
        OP(ASSERT,1,0,CETTA_RE_V1_TEXT_START), OP(SAVE,2,0,2), OP(CHAR,3,0,'a'),
        OP(SPLIT,2,4,0), OP(SAVE,5,0,3), OP(CHAR,6,0,'-'), OP(BACKREF,7,0,1),
        OP(ASSERT,8,0,CETTA_RE_V1_TEXT_END), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(repeated, 9u, 2u, NULL, NULL, 0u, NULL, 0u);
    observation(p, "aaa-aaa", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,7,1,0,3, "remembered text repeats exactly");
    observation(p, "a-a", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,3,1,0,1, "single remembered character");
    observation(p, "aa-a", CETTA_RE_V1_FULL, CETTA_RE_V1_NO_MATCH, 0,0,0,0,0, "different remembered text is rejected");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 distinct[] = {
        OP(SPLIT,1,4,0), OP(SAVE,2,0,2), OP(CHAR,3,0,'a'), OP(SAVE,7,0,3),
        OP(CHAR,5,0,'a'), OP(SAVE,6,0,2), OP(SAVE,7,0,3), OP(BACKREF,8,0,1), OP(MATCH,0,0,0)};
    p = program(distinct, 9u, 2u, NULL, NULL, 0u, NULL, 0u);
    observation(p, "a", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,1,1,1,1,
        "same point with different captures must not be merged");
    observation(p, "aa", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,2,1,0,1,
        "preferred nonempty capture survives when viable");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 absent[] = {OP(BACKREF,1,0,1), OP(MATCH,0,0,0)};
    p = program(absent, 2u, 2u, NULL, NULL, 0u, NULL, 0u);
    observation(p, "", CETTA_RE_V1_FULL, CETTA_RE_V1_NO_MATCH, 0,0,0,0,0, "unmatched is not an empty capture");
    cetta_re_v1_release(p);
    const CettaReInstructionV1 open[] = {
        OP(SAVE,1,0,2), OP(CHAR,2,0,'a'), OP(BACKREF,3,0,1), OP(MATCH,0,0,0)};
    p = program(open, 4u, 2u, NULL, NULL, 0u, NULL, 0u);
    observation(p, "aa", CETTA_RE_V1_FULL, CETTA_RE_V1_NO_MATCH, 0,0,0,0,0, "currently open capture cannot be referenced");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 already_closed[] = {
        OP(SAVE,1,0,2), OP(CHAR,2,0,'a'), OP(SAVE,3,0,3), OP(CHAR,4,0,'b'),
        OP(SAVE,5,0,3), OP(BACKREF,6,0,1), OP(MATCH,0,0,0)};
    p = program(already_closed, 7u, 2u, NULL, NULL, 0u, NULL, 0u);
    observation(p, "aba", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,3,1,0,1,
        "closing an already-closed group does not alter its captured text");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 unicode[] = {
        OP(SAVE,1,0,2), OP(CHAR,2,0,0x212au), OP(SAVE,3,0,3), OP(BACKREF,4,2,1), OP(MATCH,0,0,0)};
    const CettaReFoldPairV1 folds[] = {{'K','K'}, {'k','K'}, {0x212au,'K'}};
    p = program(unicode, 5u, 2u, NULL, NULL, 0u, folds, 3u);
    observation(p, "\xe2\x84\xaa" "k", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,4,1,0,3,
        "Unicode backreference compares scalars without changing original spans");
    observation(p, "\xe2\x84\xaa" "K", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,4,1,0,3,
        "owned folding provider survives restoration");
    observation(p, "\xe2\x84\xaa" "x", CETTA_RE_V1_FULL, CETTA_RE_V1_NO_MATCH, 0,0,0,0,0,
        "folding is not arbitrary text equality");
    cetta_re_v1_release(p);
}

static void lookahead(void) {
    const CettaReInstructionV1 positive[] = {
        OP(LOOKAHEAD,1,4,0), OP(CHAR,2,0,'a'), OP(BACKREF,3,0,1), OP(MATCH,0,0,0),
        OP(SAVE,5,0,2), OP(CHAR,6,0,'a'), OP(SAVE,7,0,3), OP(MATCH,0,0,0)};
    const uint32_t ranks[] = {1,1,1,1,0,0,0,0};
    CettaReProgramV1 *p = program(positive, 8u, 2u, ranks, NULL, 0u, NULL, 0u);
    observation(p, "aa", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,2,1,0,1,
        "positive lookahead commits captures but not its input cursor");
    observation(p, "xaa", CETTA_RE_V1_SEARCH, CETTA_RE_V1_FOUND, 1,3,1,1,2,
        "lookahead uses original absolute capture positions");
    observation(p, "ab", CETTA_RE_V1_FULL, CETTA_RE_V1_NO_MATCH, 0,0,0,0,0,
        "later mismatch does not invent an assertion answer");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 negative[] = {
        OP(LOOKAHEAD,1,3,1), OP(CHAR,2,0,'b'), OP(MATCH,0,0,0),
        OP(SAVE,4,0,2), OP(CHAR,5,0,'a'), OP(SAVE,6,0,3), OP(MATCH,0,0,0)};
    const uint32_t nranks[] = {1,1,1,0,0,0,0};
    p = program(negative, 7u, 2u, nranks, NULL, 0u, NULL, 0u);
    observation(p, "b", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,1,-1,0,0,
        "negative assertion requires absence and publishes no captures");
    observation(p, "xb", CETTA_RE_V1_SEARCH, CETTA_RE_V1_FOUND, 1,2,-1,0,0,
        "negative assertion can succeed after an earlier failed start");
    observation(p, "ab", CETTA_RE_V1_PREFIX, CETTA_RE_V1_NO_MATCH, 0,0,0,0,0,
        "successful negative body rejects the outer path");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 atomic[] = {
        OP(LOOKAHEAD,1,4,0), OP(BACKREF,2,0,1), OP(CHAR,3,0,'b'), OP(MATCH,0,0,0),
        OP(SAVE,5,0,2), OP(SPLIT,6,8,0), OP(CHAR,7,0,'a'), OP(SAVE,10,0,3),
        OP(CHAR,9,0,'a'), OP(CHAR,7,0,'a'), OP(MATCH,0,0,0)};
    const uint32_t aranks[] = {1,1,1,1,0,0,0,0,0,0,0};
    p = program(atomic, 11u, 2u, aranks, NULL, 0u, NULL, 0u);
    observation(p, "ab", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,2,1,0,1, "atomic preferred assertion is usable");
    observation(p, "aab", CETTA_RE_V1_FULL, CETTA_RE_V1_NO_MATCH, 0,0,0,0,0,
        "outer failure does not reopen a positive assertion's discarded branch");
    cetta_re_v1_release(p);
}

static void lookbehind(void) {
    const CettaReRangeV1 ranges[] = {{'a','b'}, {'b','c'}};
    const CettaReInstructionV1 forward[] = {
        OP(LOOKBEHIND,1,2,0), OP(MATCH,0,0,0), OP(SAVE,3,0,2), OP(CLASS,4,1,0),
        OP(SPLIT,3,5,0), OP(SAVE,6,0,3), OP(SAVE,7,0,4), OP(CLASS,8,1,1),
        OP(SPLIT,7,9,0), OP(SAVE,10,0,5), OP(MATCH,0,0,0)};
    const uint32_t ranks[] = {1,1,0,0,0,0,0,0,0,0,0};
    CettaReProgramV1 *p = program(forward, 11u, 3u, ranks, ranges, 2u, NULL, 0u);
    CettaReResultV1 r = cetta_re_v1_find(p, (const uint8_t *)"abc", 3u, 3u, CETTA_RE_V1_PREFIX, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND && r.groups[1].start == 0u && r.groups[1].end == 2u &&
        r.groups[2].start == 2u && r.groups[2].end == 3u, "forward written priority chooses ab then c");
    cetta_re_v1_result_free(&r);
    r = cetta_re_v1_find(p, (const uint8_t *)"abc!", 4u, 3u, CETTA_RE_V1_PREFIX, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND && r.groups[0].start == 3u && r.groups[0].end == 3u,
        "lookbehind must end at assertion cursor, not original haystack end");
    cetta_re_v1_result_free(&r);
    r = cetta_re_v1_find(p, (const uint8_t *)"a", 1u, 1u, CETTA_RE_V1_PREFIX, 0u);
    check(r.outcome == CETTA_RE_V1_NO_MATCH, "lookbehind cannot borrow absent preceding text");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);

    const CettaReInstructionV1 reverse[] = {
        OP(LOOKBEHIND,1,2,2), OP(MATCH,0,0,0), OP(SAVE,3,0,4), OP(CLASS,4,1,1),
        OP(SPLIT,3,5,0), OP(SAVE,6,0,5), OP(SAVE,7,0,2), OP(CLASS,8,1,0),
        OP(SPLIT,7,9,0), OP(SAVE,10,0,3), OP(MATCH,0,0,0)};
    p = program(reverse, 11u, 3u, ranks, ranges, 2u, NULL, 0u);
    r = cetta_re_v1_find(p, (const uint8_t *)"abc", 3u, 3u, CETTA_RE_V1_PREFIX, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND && r.groups[1].start == 0u && r.groups[1].end == 1u &&
        r.groups[2].start == 1u && r.groups[2].end == 3u,
        "explicit reverse profile chooses a then bc with normalized captures");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);

    const CettaReInstructionV1 context[] = {
        OP(LOOKBEHIND,1,2,0), OP(MATCH,0,0,0), OP(ASSERT,3,0,CETTA_RE_V1_TEXT_START),
        OP(CHAR,4,0,'a'), OP(MATCH,0,0,0)};
    const uint32_t cranks[] = {1,1,0,0,0};
    p = program(context, 5u, 1u, cranks, NULL, 0u, NULL, 0u);
    r = cetta_re_v1_find(p, (const uint8_t *)"xa", 2u, 2u, CETTA_RE_V1_PREFIX, 0u);
    check(r.outcome == CETTA_RE_V1_NO_MATCH, "lookbehind never slices away original anchor context");
    cetta_re_v1_result_free(&r);
    r = cetta_re_v1_find(p, (const uint8_t *)"ab", 2u, 1u, CETTA_RE_V1_PREFIX, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND, "original context permits anchored preceding a");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
}

static void cycles_and_certificates(void) {
    const CettaReInstructionV1 cyclic[] = {
        OP(SPLIT,1,4,0), OP(SAVE,2,0,2), OP(SAVE,3,0,3), OP(JUMP,0,0,0),
        OP(BACKREF,5,0,1), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(cyclic, 6u, 2u, NULL, NULL, 0u, NULL, 0u);
    observation(p, "", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0,0,1,0,0,
        "nullable loops close only identical complete configurations");
    cetta_re_v1_release(p); p = NULL;
    const CettaReInstructionV1 ops[] = {OP(LOOKAHEAD,1,0,0), OP(MATCH,0,0,0)};
    const uint32_t bad[] = {0u,0u};
    CettaReProgramSpecV1 spec = {.instructions = ops, .instruction_len = 2u, .group_len = 1u, .ranks = bad};
    char error[256];
    check(!cetta_re_v1_program(&spec, &p, error, sizeof(error)), "recursive assertion rank is rejected at ingress");
    spec.ranks = NULL;
    check(!cetta_re_v1_program(&spec, &p, error, sizeof(error)), "look observations require a checked rank certificate");
    const CettaReInstructionV1 regular[] = {OP(CHAR,1,0,'a'), OP(MATCH,0,0,0)};
    p = program(regular, 2u, 1u, NULL, NULL, 0u, NULL, 0u);
    uint8_t *bytes = NULL; size_t len;
    check(cetta_re_v1_store(p, &bytes, &len, error, sizeof(error)) && !memcmp(bytes, "CRP2", 4u),
        "ordinary programs retain the existing artifact profile");
    free(bytes); cetta_re_v1_release(p);
}

static void deep_observations(void) {
    const uint32_t depth = 4096u, len = 2u * depth + 1u;
    CettaReInstructionV1 *ops = calloc(len, sizeof(*ops));
    uint32_t *ranks = calloc(len, sizeof(*ranks));
    check(ops && ranks, "deep observation fixture allocation");
    if (!ops || !ranks) { free(ops); free(ranks); return; }
    for (uint32_t i = 0; i < depth; ++i) {
        ops[2u * i] = (CettaReInstructionV1)OP(LOOKAHEAD,2u*i+1u,2u*i+2u,0u);
        ops[2u * i + 1u] = (CettaReInstructionV1)OP(MATCH,0,0,0);
        ranks[2u * i] = ranks[2u * i + 1u] = depth - i;
    }
    ops[len - 1u] = (CettaReInstructionV1)OP(MATCH,0,0,0);
    CettaReProgramV1 *p = program(ops, len, 1u, ranks, NULL, 0u, NULL, 0u);
    CettaReResultV1 r = cetta_re_v1_find(p, NULL, 0u, 0u, CETTA_RE_V1_FULL, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND && r.groups[0].start == 0u && r.groups[0].end == 0u,
        "nested observations use an explicit task stack, not the C stack");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
    ops[0].argument = 1u;
    p = program(ops, len, 1u, ranks, NULL, 0u, NULL, 0u);
    r = cetta_re_v1_find(p, NULL, 0u, 0u, CETTA_RE_V1_FULL, 0u);
    check(r.outcome == CETTA_RE_V1_NO_MATCH, "completed nested positive witness refutes a negative observation");
    cetta_re_v1_result_free(&r);
    r = cetta_re_v1_find(p, NULL, 0u, 0u, CETTA_RE_V1_FULL, depth);
    check(r.outcome == CETTA_RE_V1_INCOMPLETE && r.state_visits == depth,
        "incomplete nested negative observation never becomes completed absence");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
    free(ops); free(ranks);
}

static void backwards_reference_and_capture_lifetimes(void) {
    const CettaReInstructionV1 reverse[] = {
        OP(LOOKBEHIND,1,2,2), OP(MATCH,0,0,0), OP(SAVE,3,0,2), OP(CHAR,4,0,955),
        OP(SAVE,5,0,3), OP(BACKREF,6,0,1), OP(MATCH,0,0,0)};
    const uint32_t ranks[] = {1,1,0,0,0,0,0};
    CettaReProgramV1 *p = program(reverse, 7u, 2u, ranks, NULL, 0u, NULL, 0u);
    const char *text = "\xce\xbb\xce\xbb";
    CettaReResultV1 r = cetta_re_v1_find(p, (const uint8_t *)text, 4u, 4u, CETTA_RE_V1_PREFIX, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND && r.groups[0].start == 4u && r.groups[0].end == 4u &&
        r.groups[1].participated && r.groups[1].start == 2u && r.groups[1].end == 4u,
        "reverse backreference preserves original UTF8 capture orientation");
    cetta_re_v1_result_free(&r);
    r = cetta_re_v1_find(p, (const uint8_t *)"x\xce\xbb", 3u, 3u, CETTA_RE_V1_PREFIX, 0u);
    check(r.outcome == CETTA_RE_V1_NO_MATCH, "reverse backreference compares rather than merely consuming width");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);

    const CettaReInstructionV1 branch[] = {
        OP(SPLIT,1,7,0), OP(LOOKAHEAD,2,3,0), OP(CHAR,9,0,'z'),
        OP(SAVE,4,0,2), OP(CHAR,5,0,'a'), OP(SAVE,6,0,3), OP(MATCH,0,0,0),
        OP(CHAR,8,0,'a'), OP(LOOKAHEAD,9,10,1), OP(MATCH,0,0,0), OP(CHAR,11,0,'z'), OP(MATCH,0,0,0)};
    const uint32_t branks[] = {1,1,1,0,0,0,0,1,1,1,0,0};
    p = program(branch, 12u, 2u, branks, NULL, 0u, NULL, 0u);
    observation(p, "a", CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, 0u,1u,-1,0u,0u,
        "positive assertion captures on a discarded outer branch do not leak");
    cetta_re_v1_release(p);
}

static void declared_nullable_profiles(void) {
    const CettaReInstructionV1 regular[] = {
        OP(SPLIT,1,4,0), OP(SAVE,2,0,2), OP(SAVE,3,0,3), OP(JUMP,0,0,0),
        OP(JUMP,5,0,0), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(regular, 6u, 2u, NULL, NULL, 0u, NULL, 0u);
    CettaReResultV1 r = cetta_re_v1_find(p, NULL, 0u, 0u, CETTA_RE_V1_FULL, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND && !r.groups[1].participated,
        "ordinary point-simple nullable profile remains unchanged");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
    const CettaReInstructionV1 extended[] = {
        OP(LOOKAHEAD,1,7,0), OP(SPLIT,2,5,0), OP(SAVE,3,0,2), OP(SAVE,4,0,3),
        OP(JUMP,1,0,0), OP(JUMP,6,0,0), OP(MATCH,0,0,0), OP(MATCH,0,0,0)};
    const uint32_t ranks[] = {1,1,1,1,1,1,1,0};
    p = program(extended, 8u, 2u, ranks, NULL, 0u, NULL, 0u);
    r = cetta_re_v1_find(p, NULL, 0u, 0u, CETTA_RE_V1_FULL, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND && r.groups[1].participated &&
        r.groups[1].start == 0u && r.groups[1].end == 0u,
        "declared full-configuration-simple extended profile retains a changed empty capture");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
}

static void independent_word_relations(void) {
    const CettaReRangeV1 range = {'a','b'};
    /* The oracle is the independent word equation w = u ++ u, not a
     * second traversal of the instruction graph. */
    const CettaReInstructionV1 doubled[] = {
        OP(SAVE,1,0,2), OP(CLASS,2,1,0), OP(SPLIT,1,3,0), OP(SAVE,4,0,3),
        OP(BACKREF,5,0,1), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(doubled, 6u, 2u, NULL, &range, 1u, NULL, 0u);
    for (size_t length = 0; length <= 10u; ++length) {
        for (unsigned word = 0u; word < (1u << length); ++word) {
            uint8_t input[10];
            for (size_t i = 0; i < length; ++i) input[i] = (word & (1u << i)) ? 'a' : 'b';
            bool expected = length && !(length & 1u) && !memcmp(input, input + length / 2u, length / 2u);
            CettaReResultV1 r = cetta_re_v1_find(p, input, length, 0u, CETTA_RE_V1_FULL, 0u);
            check(r.outcome == (expected ? CETTA_RE_V1_FOUND : CETTA_RE_V1_NO_MATCH) &&
                (!expected || (r.groups[1].participated && !r.groups[1].start && r.groups[1].end == length / 2u)),
                "backreference agrees with independent doubled-word relation");
            cetta_re_v1_result_free(&r);
        }
    }
    cetta_re_v1_release(p);

    const CettaReInstructionV1 suffix[] = {
        OP(LOOKBEHIND,1,2,0), OP(MATCH,0,0,0), OP(SAVE,3,0,2), OP(CLASS,4,1,0),
        OP(SPLIT,3,5,0), OP(SAVE,6,0,3), OP(MATCH,0,0,0)};
    const uint32_t ranks[] = {1,1,0,0,0,0,0};
    p = program(suffix, 7u, 2u, ranks, &range, 1u, NULL, 0u);
    for (size_t length = 0; length <= 8u; ++length) {
        for (unsigned word = 0u; word < (1u << length); ++word) {
            uint8_t input[8];
            for (size_t i = 0; i < length; ++i) input[i] = (word & (1u << i)) ? 'a' : 'x';
            size_t begin = length;
            while (begin && input[begin - 1u] == 'a') --begin;
            bool expected = begin != length;
            CettaReResultV1 r = cetta_re_v1_find(p, input, length, length, CETTA_RE_V1_PREFIX, 0u);
            check(r.outcome == (expected ? CETTA_RE_V1_FOUND : CETTA_RE_V1_NO_MATCH) &&
                (!expected || (r.groups[1].participated && r.groups[1].start == begin && r.groups[1].end == length)),
                "greedy lookbehind agrees with independent maximal-suffix relation");
            cetta_re_v1_result_free(&r);
        }
    }
    cetta_re_v1_release(p);
}

/* A finite authored-language oracle. It enumerates source outcomes before
 * selection, independently of instruction scheduling or VM cycle keys. */
typedef enum { RX_EPS, RX_ZERO, RX_CHAR, RX_SEQ, RX_ALT, RX_CAPTURE, RX_REF,
    RX_AHEAD, RX_NOT_AHEAD, RX_BEHIND, RX_NOT_BEHIND } RefKind;
typedef struct { RefKind kind; unsigned left, right, argument, reverse; } RefExpr;
typedef struct {
    size_t pos, captures[6];
    unsigned open;
    uint8_t choices[64]; size_t choice_len;
} RefState;
typedef struct { RefState *items; size_t len, cap; } RefAnswers;
typedef struct { RefExpr nodes[32]; unsigned len; } RefSource;

static void ref_add(RefAnswers *out, RefState state) {
    if (out->len == out->cap) {
        size_t cap = out->cap ? out->cap * 2u : 8u;
        void *items = realloc(out->items, cap * sizeof(*out->items));
        if (!items) { fputs("reference fixture allocation failed\n", stderr); exit(2); }
        out->items = items; out->cap = cap;
    }
    out->items[out->len++] = state;
}

static int ref_order(const RefState *a, const RefState *b) {
    size_t n = a->choice_len < b->choice_len ? a->choice_len : b->choice_len;
    int order = memcmp(a->choices, b->choices, n);
    if (order) return order;
    return a->choice_len < b->choice_len ? -1 : a->choice_len != b->choice_len;
}

static RefAnswers ref_eval(const RefSource *source, unsigned root, const uint8_t *input,
    size_t length, RefState state, bool reverse) {
    const RefExpr node = source->nodes[root];
    RefAnswers out = {0};
    if (node.kind == RX_EPS) ref_add(&out, state);
    else if (node.kind == RX_CHAR) {
        bool available = reverse ? state.pos != 0u : state.pos != length;
        size_t at = reverse ? state.pos - (available ? 1u : 0u) : state.pos;
        if (available && input[at] == node.argument) { state.pos = reverse ? at : at + 1u; ref_add(&out, state); }
    } else if (node.kind == RX_REF) {
        size_t left = state.captures[2u * node.argument], right = state.captures[2u * node.argument + 1u];
        if (!(state.open & (1u << node.argument)) && left != SIZE_MAX && right != SIZE_MAX) {
            if (left > right) { size_t swap = left; left = right; right = swap; }
            size_t n = right - left;
            bool available = reverse ? n <= state.pos : n <= length - state.pos;
            size_t at = reverse && available ? state.pos - n : state.pos;
            if (available && !memcmp(input + left, input + at, n)) {
                state.pos = reverse ? at : at + n; ref_add(&out, state);
            }
        }
    } else if (node.kind == RX_SEQ) {
        RefAnswers first = ref_eval(source, reverse ? node.right : node.left, input, length, state, reverse);
        for (size_t i = 0; i < first.len; ++i) {
            RefAnswers second = ref_eval(source, reverse ? node.left : node.right, input, length, first.items[i], reverse);
            for (size_t j = 0; j < second.len; ++j) ref_add(&out, second.items[j]);
            free(second.items);
        }
        free(first.items);
    } else if (node.kind == RX_ALT) {
        for (unsigned side = 0; side < 2u; ++side) {
            RefState branch = state; branch.choices[branch.choice_len++] = (uint8_t)side;
            RefAnswers answers = ref_eval(source, side ? node.right : node.left, input, length, branch, reverse);
            for (size_t i = 0; i < answers.len; ++i) ref_add(&out, answers.items[i]);
            free(answers.items);
        }
    } else if (node.kind == RX_CAPTURE) {
        state.captures[2u * node.argument] = state.pos;
        state.captures[2u * node.argument + 1u] = SIZE_MAX;
        state.open |= 1u << node.argument;
        RefAnswers answers = ref_eval(source, node.left, input, length, state, reverse);
        for (size_t i = 0; i < answers.len; ++i) {
            if (answers.items[i].open & (1u << node.argument)) {
                size_t start = answers.items[i].captures[2u * node.argument], stop = answers.items[i].pos;
                answers.items[i].captures[2u * node.argument] = start < stop ? start : stop;
                answers.items[i].captures[2u * node.argument + 1u] = start < stop ? stop : start;
            }
            answers.items[i].open &= ~(1u << node.argument);
            ref_add(&out, answers.items[i]);
        }
        free(answers.items);
    } else if (node.kind != RX_ZERO) {
        bool behind = node.kind == RX_BEHIND || node.kind == RX_NOT_BEHIND;
        bool negative = node.kind == RX_NOT_AHEAD || node.kind == RX_NOT_BEHIND;
        bool backwards = behind && node.reverse;
        bool found = false; RefState best = state; size_t best_start = 0u;
        for (size_t start = 0; start <= state.pos; ++start) {
            if (!behind || backwards) start = state.pos;
            RefState inner = state; inner.pos = start;
            RefAnswers answers = ref_eval(source, node.left, input, length, inner, backwards);
            for (size_t i = 0; i < answers.len; ++i) {
                RefState candidate = answers.items[i];
                if (behind && !backwards && candidate.pos != state.pos) continue;
                int order = found ? ref_order(&candidate, &best) : -1;
                if (!found || order < 0 || (!order && start > best_start)) {
                    best = candidate; best_start = start;
                }
                found = true;
            }
            free(answers.items);
            if (!behind || backwards) break;
        }
        if (negative ? !found : found) {
            RefState answer = negative ? state : best; answer.pos = state.pos; ref_add(&out, answer);
        }
    }
    return out;
}

typedef struct { CettaReInstructionV1 ops[256]; uint32_t ranks[256]; uint32_t len; } RefCode;
static uint32_t ref_emit(RefCode *code, CettaReInstructionV1 op, uint32_t rank) {
    if (code->len == 256u) { fputs("reference fixture code overflow\n", stderr); exit(2); }
    uint32_t at = code->len++; code->ops[at] = op; code->ranks[at] = rank; return at;
}

static uint32_t ref_compile(const RefSource *source, unsigned root, RefCode *code,
    uint32_t continuation, uint32_t rank, bool reverse) {
    RefExpr node = source->nodes[root];
    if (node.kind == RX_EPS) return continuation;
    if (node.kind == RX_ZERO) return ref_emit(code, (CettaReInstructionV1)OP(FAIL,0,0,0), rank);
    if (node.kind == RX_CHAR)
        return ref_emit(code, (CettaReInstructionV1)OP(CHAR,continuation,0,node.argument), rank);
    if (node.kind == RX_REF)
        return ref_emit(code, (CettaReInstructionV1)OP(BACKREF,continuation,0,node.argument), rank);
    if (node.kind == RX_SEQ) {
        uint32_t tail = ref_compile(source, reverse ? node.left : node.right, code, continuation, rank, reverse);
        return ref_compile(source, reverse ? node.right : node.left, code, tail, rank, reverse);
    }
    if (node.kind == RX_ALT) {
        uint32_t left = ref_compile(source, node.left, code, continuation, rank, reverse);
        uint32_t right = ref_compile(source, node.right, code, continuation, rank, reverse);
        return ref_emit(code, (CettaReInstructionV1)OP(SPLIT,left,right,0), rank);
    }
    if (node.kind == RX_CAPTURE) {
        uint32_t close = ref_emit(code, (CettaReInstructionV1)OP(SAVE,continuation,0,2u*node.argument+1u), rank);
        uint32_t body = ref_compile(source, node.left, code, close, rank, reverse);
        return ref_emit(code, (CettaReInstructionV1)OP(SAVE,body,0,2u*node.argument), rank);
    }
    bool behind = node.kind == RX_BEHIND || node.kind == RX_NOT_BEHIND;
    bool negative = node.kind == RX_NOT_AHEAD || node.kind == RX_NOT_BEHIND;
    uint32_t terminal = ref_emit(code, (CettaReInstructionV1)OP(MATCH,0,0,0), rank - 1u);
    uint32_t body = ref_compile(source, node.left, code, terminal, rank - 1u, behind && node.reverse);
    CettaReInstructionV1 op = {behind ? CETTA_RE_V1_LOOKBEHIND : CETTA_RE_V1_LOOKAHEAD,
        continuation, body, (negative ? 1u : 0u) | (behind && node.reverse ? 2u : 0u)};
    return ref_emit(code, op, rank);
}

static uint32_t random_state = UINT32_C(0x51dc0ffe);
static uint32_t random_word(void) {
    random_state ^= random_state << 13u; random_state ^= random_state >> 17u;
    random_state ^= random_state << 5u; return random_state;
}

static unsigned ref_generate(RefSource *source, unsigned depth) {
    unsigned at = source->len++;
    RefExpr *node = &source->nodes[at];
    if (!depth) {
        unsigned which = random_word() % 5u;
        node->kind = which == 0u ? RX_EPS : which == 1u ? RX_ZERO : which == 2u ? RX_REF : RX_CHAR;
    } else node->kind = (RefKind)(random_word() % 11u);
    node->argument = node->kind == RX_REF || node->kind == RX_CAPTURE ? 1u + random_word() % 2u :
        'a' + random_word() % 2u;
    node->reverse = random_word() & 1u;
    if (node->kind == RX_SEQ || node->kind == RX_ALT) {
        node->left = ref_generate(source, depth - 1u); node->right = ref_generate(source, depth - 1u);
    } else if (node->kind == RX_CAPTURE || node->kind >= RX_AHEAD)
        node->left = ref_generate(source, depth - 1u);
    return at;
}

static void generated_source_observations(void) {
    for (unsigned trial = 0u; trial < 160u; ++trial) {
        RefSource source = {0}; unsigned root = ref_generate(&source, 3u);
        /* Force the extended realization, without adding choices or writes. */
        unsigned epsilon = source.len++; source.nodes[epsilon] = (RefExpr){.kind = RX_EPS};
        unsigned observe = source.len++; source.nodes[observe] = (RefExpr){.kind = RX_AHEAD, .left = epsilon};
        unsigned wrapped = source.len++; source.nodes[wrapped] = (RefExpr){.kind = RX_SEQ, .left = observe, .right = root};
        RefCode code = {0}; uint32_t terminal = ref_emit(&code, (CettaReInstructionV1)OP(MATCH,0,0,0), 8u);
        uint32_t entry = ref_compile(&source, wrapped, &code, terminal, 8u, false);
        CettaReProgramSpecV1 spec = {.instructions = code.ops, .instruction_len = code.len,
            .entry = entry, .group_len = 3u, .ranks = code.ranks};
        CettaReProgramV1 *p = NULL; char error[256];
        check(cetta_re_v1_program(&spec, &p, error, sizeof(error)), "generated authored fixture admits its rank certificate");
        if (!p) continue;
        for (size_t length = 0u; length <= 4u; ++length) for (unsigned word = 0u; word < (1u << length); ++word) {
            uint8_t input[4];
            for (size_t i = 0; i < length; ++i) input[i] = (word & (1u << i)) ? 'a' : 'b';
            for (unsigned mode = CETTA_RE_V1_SEARCH; mode <= CETTA_RE_V1_FULL; ++mode) {
                bool found = false; RefState best = {0};
                for (size_t start = 0; start <= length && !found; ++start) {
                    if (mode != CETTA_RE_V1_SEARCH && start) break;
                    RefState initial = {.pos = start};
                    for (unsigned i = 0; i < 6u; ++i) initial.captures[i] = SIZE_MAX;
                    initial.captures[0] = start;
                    RefAnswers answers = ref_eval(&source, wrapped, input, length, initial, false);
                    for (size_t i = 0; i < answers.len; ++i) {
                        if (mode == CETTA_RE_V1_FULL && answers.items[i].pos != length) continue;
                        if (!found || ref_order(&answers.items[i], &best) < 0) best = answers.items[i];
                        found = true;
                    }
                    free(answers.items);
                }
                CettaReResultV1 result = cetta_re_v1_find(p, input, length, 0u, (CettaReModeV1)mode, 0u);
                bool ok = result.outcome == (found ? CETTA_RE_V1_FOUND : CETTA_RE_V1_NO_MATCH);
                if (ok && found) {
                    best.captures[1] = best.pos;
                    for (unsigned g = 0; g < 3u; ++g) {
                        size_t left = best.captures[2u*g], right = best.captures[2u*g+1u];
                        bool closed = !(best.open & (1u << g)) && left != SIZE_MAX && right != SIZE_MAX;
                        if (left > right) { size_t swap = left; left = right; right = swap; }
                        ok = ok && result.groups[g].participated == closed &&
                            (!closed || (result.groups[g].start == left && result.groups[g].end == right));
                    }
                }
                check(ok, "generated acyclic graph agrees with independent source outcome selection");
                if (!ok) fprintf(stderr, "  generated trial %u length %zu word %u mode %u\n", trial, length, word, mode);
                if (result.state_visits > 1u) {
                    CettaReResultV1 stopped = cetta_re_v1_find(p, input, length, 0u, (CettaReModeV1)mode,
                        result.state_visits - 1u);
                    check(stopped.outcome == CETTA_RE_V1_INCOMPLETE,
                        "generated nested observation cutoff preserves incompleteness");
                    cetta_re_v1_result_free(&stopped);
                }
                cetta_re_v1_result_free(&result);
            }
        }
        cetta_re_v1_release(p);
    }
}

static void reused_capture_source_case(RefSource source, unsigned root, const char *input,
    bool expected, size_t capture_start, size_t capture_end, const char *message) {
    /* A trivial observation selects the capture-sensitive realization without
     * altering this source's choices, writes, or consuming behavior. */
    unsigned epsilon = source.len++; source.nodes[epsilon] = (RefExpr){.kind = RX_EPS};
    unsigned observe = source.len++; source.nodes[observe] = (RefExpr){.kind = RX_AHEAD, .left = epsilon};
    unsigned wrapped = source.len++; source.nodes[wrapped] = (RefExpr){.kind = RX_SEQ, .left = observe, .right = root};
    RefCode code = {0}; uint32_t terminal = ref_emit(&code, (CettaReInstructionV1)OP(MATCH,0,0,0), 8u);
    uint32_t entry = ref_compile(&source, wrapped, &code, terminal, 8u, false);
    CettaReProgramSpecV1 spec = {.instructions = code.ops, .instruction_len = code.len,
        .entry = entry, .group_len = 3u, .ranks = code.ranks};
    CettaReProgramV1 *p = NULL; char error[256];
    check(cetta_re_v1_program(&spec, &p, error, sizeof(error)), "reused source capture IDs admit the checked graph");
    if (!p) return;
    size_t length = strlen(input);
    RefState initial = {0};
    for (unsigned i = 0u; i < 6u; ++i) initial.captures[i] = SIZE_MAX;
    initial.captures[0] = 0u;
    RefAnswers answers = ref_eval(&source, wrapped, (const uint8_t *)input, length, initial, false);
    bool found = false; RefState best = {0};
    for (size_t i = 0u; i < answers.len; ++i) if (answers.items[i].pos == length) {
        if (!found || ref_order(&answers.items[i], &best) < 0) best = answers.items[i];
        found = true;
    }
    check(found == expected, "independent source fixture has its stated word observation");
    if (found) check(!(best.open & 2u) && best.captures[2] == capture_start && best.captures[3] == capture_end,
        "independent source closes the reused ID to the intended interval immediately");
    CettaReResultV1 r = cetta_re_v1_find(p, (const uint8_t *)input, length, 0u, CETTA_RE_V1_FULL, 0u);
    bool ok = r.outcome == (found ? CETTA_RE_V1_FOUND : CETTA_RE_V1_NO_MATCH);
    if (ok && found) {
        best.captures[1] = best.pos;
        for (unsigned group = 0u; group < 3u; ++group) {
            size_t left = best.captures[2u * group], right = best.captures[2u * group + 1u];
            bool closed = !(best.open & (1u << group)) && left != SIZE_MAX && right != SIZE_MAX;
            ok = ok && r.groups[group].participated == closed &&
                (!closed || (r.groups[group].start == left && r.groups[group].end == right));
        }
    }
    check(ok, message);
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p); free(answers.items);
}

static void reused_capture_id_observations(void) {
    /* Source expression: capture_1(a); capture_1(b); reference_1. */
    RefSource sequential = {.nodes = {
        {.kind = RX_CHAR, .argument = 'a'}, {.kind = RX_CHAR, .argument = 'b'},
        {.kind = RX_CAPTURE, .left = 0u, .argument = 1u},
        {.kind = RX_CAPTURE, .left = 1u, .argument = 1u},
        {.kind = RX_REF, .argument = 1u}, {.kind = RX_SEQ, .left = 3u, .right = 4u},
        {.kind = RX_SEQ, .left = 2u, .right = 5u}}, .len = 7u};
    reused_capture_source_case(sequential, 6u, "abb", true, 1u, 2u,
        "forward re-opening overwrites the old capture before a backreference");
    reused_capture_source_case(sequential, 6u, "aba", false, 0u, 0u,
        "overwritten capture does not retain the first captured word");

    /* Source: a; b; reverse-behind(capture_1(a; capture_1(b))); reference_1.
     * The outer close is already closed by the inner occurrence. */
    RefSource nested_reverse = {.nodes = {
        {.kind = RX_CHAR, .argument = 'a'}, {.kind = RX_CHAR, .argument = 'b'},
        {.kind = RX_CAPTURE, .left = 1u, .argument = 1u},
        {.kind = RX_SEQ, .left = 0u, .right = 2u},
        {.kind = RX_CAPTURE, .left = 3u, .argument = 1u},
        {.kind = RX_BEHIND, .left = 4u, .reverse = 1u},
        {.kind = RX_REF, .argument = 1u}, {.kind = RX_SEQ, .left = 5u, .right = 6u},
        {.kind = RX_SEQ, .left = 1u, .right = 7u},
        {.kind = RX_SEQ, .left = 0u, .right = 8u}}, .len = 10u};
    reused_capture_source_case(nested_reverse, 9u, "abb", true, 1u, 2u,
        "reverse nested reuse keeps the inner normalized capture when the outer close is redundant");
    reused_capture_source_case(nested_reverse, 9u, "aba", false, 0u, 0u,
        "reverse reused capture is content-sensitive rather than a remembered width");

    /* A forward observation and a reverse observation overwrite the same ID;
     * neither observation consumes the outer cursor. */
    RefSource mixed = {.nodes = {
        {.kind = RX_CHAR, .argument = 'a'}, {.kind = RX_CHAR, .argument = 'b'},
        {.kind = RX_CAPTURE, .left = 0u, .argument = 1u},
        {.kind = RX_CAPTURE, .left = 1u, .argument = 1u},
        {.kind = RX_BEHIND, .left = 2u, .reverse = 1u},
        {.kind = RX_AHEAD, .left = 3u}, {.kind = RX_REF, .argument = 1u},
        {.kind = RX_SEQ, .left = 5u, .right = 6u},
        {.kind = RX_SEQ, .left = 4u, .right = 7u},
        {.kind = RX_SEQ, .left = 2u, .right = 8u}}, .len = 10u};
    reused_capture_source_case(mixed, 9u, "ab", true, 1u, 2u,
        "forward observation replaces the closed capture inherited from reverse observation");
}

typedef struct { size_t start, end; bool open; } ClosedCaptureModel;
static ClosedCaptureModel close_model(ClosedCaptureModel before, size_t stop) {
    if (!before.open) return before;
    return (ClosedCaptureModel){before.start < stop ? before.start : stop,
        before.start < stop ? stop : before.start, false};
}
static bool capture_model_equal(ClosedCaptureModel left, ClosedCaptureModel right) {
    return left.start == right.start && left.end == right.end && left.open == right.open;
}
static void canonical_capture_keys_and_nullable_loop(void) {
    ClosedCaptureModel forward = close_model((ClosedCaptureModel){0u, SIZE_MAX, true}, 1u);
    ClosedCaptureModel reverse = close_model((ClosedCaptureModel){1u, SIZE_MAX, true}, 0u);
    check(capture_model_equal(forward, reverse),
        "independent finite closure model gives one key to the same forward/reverse closed interval");
    ClosedCaptureModel raw_reverse = {1u, 0u, false};
    check(!capture_model_equal(forward, raw_reverse),
        "delayed orientation normalization would create a spurious second closed-state key");
    check(capture_model_equal(close_model(reverse, 7u), reverse),
        "closing an already-closed reused ID preserves its established interval");

    /* After consuming a, reverse lookbehind writes group 1. A nullable loop
     * writes empty group 2 and the same group 1 through forward lookbehind.
     * The independent state model has one meaningful change (group 2 starts
     * participating), then a fixed point. No VM visit count is prescribed. */
    const CettaReInstructionV1 ops[] = {
        OP(CHAR,1,0,'a'), OP(LOOKBEHIND,2,8,2), OP(SPLIT,3,7,0),
        OP(SAVE,4,0,4), OP(SAVE,5,0,5), OP(LOOKBEHIND,6,12,0), OP(JUMP,2,0,0),
        OP(BACKREF,16,0,1), OP(SAVE,9,0,2), OP(CHAR,10,0,'a'), OP(SAVE,11,0,3), OP(MATCH,0,0,0),
        OP(SAVE,13,0,2), OP(CHAR,14,0,'a'), OP(SAVE,15,0,3), OP(MATCH,0,0,0), OP(MATCH,0,0,0)};
    const uint32_t ranks[] = {1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1};
    CettaReProgramV1 *p = program(ops, 17u, 3u, ranks, NULL, 0u, NULL, 0u);
    if (!p) return;
    CettaReResultV1 r = cetta_re_v1_find(p, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_FULL, 0u);
    check(r.outcome == CETTA_RE_V1_FOUND && r.group_len == 3u &&
        r.groups[1].participated && r.groups[1].start == forward.start && r.groups[1].end == forward.end &&
        r.groups[2].participated && r.groups[2].start == 1u && r.groups[2].end == 1u,
        "capture-sensitive nullable loop retains its meaningful empty capture and canonical remembered text");
    cetta_re_v1_result_free(&r);
    r = cetta_re_v1_find(p, (const uint8_t *)"ab", 2u, 0u, CETTA_RE_V1_FULL, 0u);
    check(r.outcome == CETTA_RE_V1_NO_MATCH,
        "nullable re-observation cannot invent a match for different remembered text");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
}

int main(void) {
    backreferences(); lookahead(); lookbehind(); cycles_and_certificates();
    deep_observations(); backwards_reference_and_capture_lifetimes();
    declared_nullable_profiles();
    independent_word_relations();
    generated_source_observations();
    reused_capture_id_observations(); canonical_capture_keys_and_nullable_loop();
    printf("(RegexExtendedV1Summary %u %u)\n", passed, failed);
    return failed ? 1 : 0;
}
