#include "native/regex_selection_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP(name, next, second, arg) {CETTA_RE_V1_##name, next, second, arg}
static unsigned passed, failed;
static void check(bool value, const char *reason) {
    if (value) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", reason); }
}
typedef struct { uint64_t calls, stop; } Cancellation;
static bool cancelled(void *context) {
    Cancellation *control = context;
    ++control->calls;
    return control->stop && control->calls >= control->stop;
}
static CettaReProgramV1 *program(const CettaReInstructionV1 *ops, size_t len,
                               uint32_t groups, const uint32_t *ranks) {
    CettaReProgramSpecV1 spec = {.instructions = ops, .instruction_len = (uint32_t)len,
        .group_len = groups, .ranks = ranks};
    CettaReProgramV1 *result = NULL; char error[256] = {0};
    bool ok = cetta_re_v1_program(&spec, &result, error, sizeof(error));
    check(ok, "owned policy-control graph admission");
    if (!ok) fprintf(stderr, "%s\n", error);
    return result;
}
static CettaReResultV1 find(CettaReProgramV1 *p, const char *input, CettaReModeV1 mode,
                          Cancellation *control, uint64_t budget) {
    CettaReExecutionPolicyV1 policy = {.work_budget = budget, .cancelled = cancelled, .context = control};
    return cetta_re_v1_find_policy_valid_utf8(p, (const uint8_t *)input, strlen(input), 0u, mode, &policy);
}
static bool span(CettaReResultV1 result, size_t start, size_t end) {
    return result.outcome == CETTA_RE_V1_FOUND && result.groups && result.group_len &&
        result.groups[0].participated && result.groups[0].start == start && result.groups[0].end == end;
}
static bool same_cursor(CettaReCursorV1 a, CettaReCursorV1 b) {
    return a.bytes == b.bytes && a.byte_len == b.byte_len && a.cursor == b.cursor &&
        a.last_end == b.last_end && a.has_previous == b.has_previous && a.finished == b.finished;
}
static void sweep(CettaReProgramV1 *p, const char *input, CettaReModeV1 mode,
                  CettaReOutcomeV1 expected, const char *reason) {
    if (!p) return;
    Cancellation control = {0};
    CettaReResultV1 baseline = find(p, input, mode, &control, 0u);
    check(baseline.outcome == expected && control.calls > 0u, reason);
    uint64_t calls = control.calls;
    for (uint64_t stop = 1u; stop <= calls; ++stop) {
        control = (Cancellation){.stop = stop};
        CettaReResultV1 interrupted = find(p, input, mode, &control, 0u);
        check(interrupted.outcome == CETTA_RE_V1_CANCELLED && !interrupted.groups && !interrupted.group_len,
            "every cancellation checkpoint remains distinct from a match or completed absence");
        check(control.calls == stop, "cancellation callback is not polled again after interruption");
        cetta_re_v1_result_free(&interrupted);
    }
    control = (Cancellation){0};
    CettaReResultV1 reused = find(p, input, mode, &control, 0u);
    check(reused.outcome == baseline.outcome && reused.group_len == baseline.group_len,
        "cancelled calls leave the owned program reusable");
    if (baseline.groups && reused.groups)
        for (uint32_t i = 0u; i < baseline.group_len; ++i)
            check(baseline.groups[i].participated == reused.groups[i].participated &&
                baseline.groups[i].start == reused.groups[i].start && baseline.groups[i].end == reused.groups[i].end,
                "reuse preserves participation and original byte spans");
    cetta_re_v1_result_free(&baseline); cetta_re_v1_result_free(&reused);
}
static CettaReProgramV1 *star(bool extended) {
    const CettaReInstructionV1 ops[] = {
        OP(SPLIT,1,2,0), OP(CHAR,0,0,'a'), OP(MATCH,0,0,0), OP(BACKREF,2,0,1)};
    return program(ops, extended ? 4u : 3u, extended ? 2u : 1u, NULL);
}
static void provisional_regular(void) {
    CettaReProgramV1 *p = star(false);
    if (!p) return;
    Cancellation control = {.stop = 7u};
    CettaReResultV1 r = find(p, "aaa", CETTA_RE_V1_PREFIX, &control, 0u);
    check(r.outcome == CETTA_RE_V1_CANCELLED && r.state_visits == 6u && !r.groups,
        "a provisional lower-priority acceptance is not published after cancellation");
    cetta_re_v1_result_free(&r);
    control = (Cancellation){0};
    r = find(p, "aaa", CETTA_RE_V1_PREFIX, &control, 6u);
    check(r.outcome == CETTA_RE_V1_INCOMPLETE && !r.groups,
        "the same provisional acceptance is not published after a work cutoff");
    cetta_re_v1_result_free(&r);
    sweep(p, "aaa", CETTA_RE_V1_PREFIX, CETTA_RE_V1_FOUND, "regular matching with a non-cancelling policy completes");
    sweep(p, "b", CETTA_RE_V1_FULL, CETTA_RE_V1_NO_MATCH, "closed regular absence remains absence");
    cetta_re_v1_release(p);
}
static void negative_assertion(void) {
    const CettaReInstructionV1 ops[] = {
        OP(LOOKAHEAD,1,3,1), OP(ANY,2,0,0), OP(MATCH,0,0,0),
        OP(CHAR,4,0,'a'), OP(SPLIT,3,5,0), OP(CHAR,6,0,'z'), OP(MATCH,0,0,0)};
    const uint32_t ranks[] = {1,1,1,0,0,0,0};
    CettaReProgramV1 *p = program(ops, 7u, 1u, ranks);
    sweep(p, "aaaaX", CETTA_RE_V1_PREFIX, CETTA_RE_V1_FOUND,
        "a completed negative body can license its outer match");
    sweep(p, "aaaaz", CETTA_RE_V1_PREFIX, CETTA_RE_V1_NO_MATCH,
        "a successful negative body rejects the outer path");
    if (p) {
        Cancellation control = {.stop = 5u};
        CettaReResultV1 r = find(p, "aaaaX", CETTA_RE_V1_PREFIX, &control, 0u);
        check(r.outcome == CETTA_RE_V1_CANCELLED && !r.groups,
            "cancellation inside negative lookahead is not evidence that its body is absent");
        cetta_re_v1_result_free(&r);
        control = (Cancellation){0};
        r = find(p, "aaaaX", CETTA_RE_V1_PREFIX, &control, 4u);
        check(r.outcome == CETTA_RE_V1_INCOMPLETE && !r.groups,
            "resource exhaustion inside negative lookahead is not negative success");
        cetta_re_v1_result_free(&r);
    }
    cetta_re_v1_release(p);
}
static void backreference_comparison(void) {
    enum { REPEATS = 12, COUNT = REPEATS + 4 };
    CettaReInstructionV1 ops[COUNT];
    ops[0] = (CettaReInstructionV1)OP(SAVE,1,0,2);
    for (uint32_t i = 0u; i < REPEATS; ++i) ops[i + 1u] = (CettaReInstructionV1)OP(CHAR,i + 2u,0,0x3bbu);
    ops[REPEATS + 1u] = (CettaReInstructionV1)OP(SAVE,REPEATS + 2u,0,3);
    ops[REPEATS + 2u] = (CettaReInstructionV1)OP(BACKREF,REPEATS + 3u,0,1);
    ops[REPEATS + 3u] = (CettaReInstructionV1)OP(MATCH,0,0,0);
    char input[4u * REPEATS + 1u];
    for (size_t i = 0u; i < 2u * REPEATS; ++i) { input[2u * i] = (char)0xceu; input[2u * i + 1u] = (char)0xbbu; }
    input[4u * REPEATS] = 0;
    CettaReProgramV1 *p = program(ops, COUNT, 2u, NULL);
    if (!p) return;
    Cancellation control = {.stop = REPEATS + 5u};
    CettaReResultV1 r = find(p, input, CETTA_RE_V1_FULL, &control, 0u);
    check(r.outcome == CETTA_RE_V1_CANCELLED && r.state_visits == REPEATS + 4u,
        "backreference scalar comparison polls cancellation inside the remembered text");
    cetta_re_v1_result_free(&r);
    sweep(p, input, CETTA_RE_V1_FULL, CETTA_RE_V1_FOUND, "Unicode remembered text completes without cancellation");
    control = (Cancellation){0}; r = find(p, input, CETTA_RE_V1_FULL, &control, 0u);
    check(span(r, 0u, 4u * REPEATS) && r.groups[1].participated && r.groups[1].end == 2u * REPEATS,
        "comparison does not replace original UTF-8 capture byte spans");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
}
static void cursor_atomic(bool extended) {
    CettaReProgramV1 *p = star(extended);
    if (!p) return;
    CettaReCursorV1 cursor; check(cetta_re_v1_cursor(&cursor, (const uint8_t *)"ab", 2u), "cursor input admission");
    CettaReResultV1 first = cetta_re_v1_next(p, &cursor, 0u);
    check(span(first, 0u, 1u), "first cursor match establishes adjacent-empty suppression");
    cetta_re_v1_result_free(&first);
    CettaReCursorV1 before = cursor;
    Cancellation control = {.stop = 4u};
    CettaReExecutionPolicyV1 policy = {.cancelled = cancelled, .context = &control};
    CettaReResultV1 r = cetta_re_v1_next_policy(p, &cursor, &policy);
    check(r.outcome == CETTA_RE_V1_CANCELLED, "cancellation can arrive after suppressing a provisional adjacent empty match");
    check(same_cursor(cursor, before), "cancelled iteration publishes no cursor advancement or completion");
    cetta_re_v1_result_free(&r);
    cursor = before; policy = (CettaReExecutionPolicyV1){.work_budget = 3u};
    r = cetta_re_v1_next_policy(p, &cursor, &policy);
    check(r.outcome == CETTA_RE_V1_INCOMPLETE, "cursor work cutoff remains distinct from cancellation");
    check(same_cursor(cursor, before), "incomplete iteration publishes no cursor advancement");
    cetta_re_v1_result_free(&r);
    cursor = before; r = cetta_re_v1_next(p, &cursor, 0u);
    check(span(r, 2u, 2u), "retry after interruption returns the same next observable empty match");
    cetta_re_v1_result_free(&r);
    r = cetta_re_v1_next(p, &cursor, 0u);
    check(r.outcome == CETTA_RE_V1_NO_MATCH && cursor.finished, "completed cursor absence is stable");
    cetta_re_v1_result_free(&r);
    for (unsigned i = 0u; i < 32u; ++i) {
        control = (Cancellation){.stop = 1u};
        r = find(p, "a", CETTA_RE_V1_FULL, &control, 0u);
        check(r.outcome == CETTA_RE_V1_CANCELLED && !r.groups, "repeated interrupted invocation cleanup");
        cetta_re_v1_result_free(&r);
    }
    cetta_re_v1_release(p);
}
int main(void) {
    provisional_regular(); negative_assertion(); backreference_comparison();
    cursor_atomic(false); cursor_atomic(true);
    printf("RegexExecutionPolicyV1Summary %u %u\n", passed, failed);
    return failed ? 1 : 0;
}
