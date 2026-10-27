#include "native/regex_selection_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include the changed matcher at the allocation seam, without adding a
 * production allocator hook or a second runtime configuration. */
static size_t allocations, live_allocations, refuse_allocation;
static bool track_allocations;
static void *controlled_malloc(size_t size) {
    if (track_allocations && ++allocations == refuse_allocation) return NULL;
    void *p = malloc(size);
    if (track_allocations && p) ++live_allocations;
    return p;
}
static void *controlled_calloc(size_t n, size_t size) {
    if (track_allocations && ++allocations == refuse_allocation) return NULL;
    void *p = calloc(n, size);
    if (track_allocations && p) ++live_allocations;
    return p;
}
static void controlled_free(void *p) {
    if (track_allocations && p) --live_allocations;
    free(p);
}
#define malloc controlled_malloc
#define calloc controlled_calloc
#define free controlled_free
#include "native/regex_selection_v1.c"
#undef malloc
#undef calloc
#undef free

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
static bool same_result(CettaReResultV1 a, CettaReResultV1 b) {
    if (a.outcome != b.outcome || a.group_len != b.group_len || a.state_visits != b.state_visits) return false;
    for (uint32_t i = 0; i < a.group_len; ++i)
        if (a.groups[i].participated != b.groups[i].participated ||
            a.groups[i].start != b.groups[i].start || a.groups[i].end != b.groups[i].end) return false;
    return true;
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
static void reused_workspace(void) {
    const CettaReInstructionV1 ops[] = {
        OP(SPLIT,1,4,0), OP(SAVE,2,0,2), OP(CHAR,3,0,0x3bbu),
        OP(SAVE,5,0,3), OP(CHAR,5,0,0), OP(MATCH,0,0,0)};
    CettaReProgramV1 *owners[] = {program(ops, 6u, 2u, NULL), star(false), star(true)};
    const uint8_t input[] = {'a', 0xce, 0xbb, 0, 'b'};
    CettaReWorkspaceV1 workspace = {0};
    /* Reuse across different shapes, failed searches, interruption points and
     * independent cursors. Compare work accounting as well as answers. */
    for (unsigned pass = 0; pass < 2u; ++pass) for (size_t p = 0; p < 3u; ++p) {
        if (!owners[p]) continue;
        for (uint64_t limit = 0; limit < 24u; ++limit) {
            for (unsigned cancelling = 0; cancelling < 2u; ++cancelling) {
                CettaReCursorV1 fresh = {.bytes = input, .byte_len = sizeof(input)}, shared = fresh;
                Cancellation a = {.stop = cancelling ? limit : 0u}, b = a;
                CettaReExecutionPolicyV1 policy = {.work_budget = cancelling ? 0u : limit,
                    .cancelled = cancelled, .context = &a};
                for (unsigned step = 0; step < 8u; ++step) {
                    policy.context = &a;
                    CettaReResultV1 x = cetta_re_v1_next_policy(owners[p], &fresh, &policy);
                    policy.context = &b;
                    CettaReResultV1 y = cetta_re_v1_next_workspace(owners[p], &shared, &policy, &workspace);
                    check(same_result(x, y) && same_cursor(fresh, shared) && a.calls == b.calls,
                        "fresh and reused storage preserve captures, byte spans, cursor, work and cancellation schedule");
                    bool done = x.outcome != CETTA_RE_V1_FOUND;
                    cetta_re_v1_result_free(&x); cetta_re_v1_result_free(&y);
                    if (done) break;
                }
            }
        }
    }
    cetta_re_v1_workspace_free(&workspace);
    if (owners[0]) {
        CettaReCursorV1 cursor = {.bytes = input, .byte_len = sizeof(input)};
        track_allocations = true; allocations = 0u;
        CettaReResultV1 first = cetta_re_v1_next_workspace(owners[0], &cursor, NULL, &workspace);
        size_t first_allocations = allocations;
        CettaReResultV1 second = cetta_re_v1_next_workspace(owners[0], &cursor, NULL, &workspace);
        check(first_allocations == 11u && allocations == 12u,
            "ten scratch buffers are allocated once; subsequent matches allocate only their result");
        check(span(first, 1u, 3u) && first.groups[1].participated && span(second, 3u, 4u) &&
            !second.groups[1].participated && first.groups != second.groups,
            "later searches do not alter earlier results or leak capture participation");
        cetta_re_v1_workspace_free(&workspace);
        check(span(first, 1u, 3u) && first.groups[1].end == 3u,
            "match results outlive workspace disposal");
        cetta_re_v1_result_free(&first); cetta_re_v1_result_free(&second);
        check(live_allocations == 0u, "successful operation releases every owned allocation");
        track_allocations = false;
        for (size_t failure = 1u; failure <= 11u; ++failure) {
            cursor = (CettaReCursorV1){.bytes = input, .byte_len = sizeof(input)};
            CettaReCursorV1 before = cursor;
            allocations = 0; refuse_allocation = failure; track_allocations = true;
            CettaReResultV1 r = cetta_re_v1_next_workspace(owners[0], &cursor, NULL, &workspace);
            check(r.outcome == CETTA_RE_V1_ALLOCATION_FAILURE && !r.groups && same_cursor(cursor, before),
                "scratch/result allocation refusal publishes no result or cursor mutation");
            cetta_re_v1_result_free(&r); refuse_allocation = 0;
            r = cetta_re_v1_next_workspace(owners[0], &cursor, NULL, &workspace);
            check(span(r, 1u, 3u), "workspace retries safely after every allocation refusal");
            cetta_re_v1_result_free(&r); cetta_re_v1_workspace_free(&workspace);
            check(live_allocations == 0u, "allocation failure and retry release all scratch and results");
            track_allocations = false;
        }
    }
    for (size_t p = 0; p < 3u; ++p) cetta_re_v1_release(owners[p]);
}
static void first_scalar_filter(void) {
    const CettaReInstructionV1 ops[] = {
        OP(SAVE,1,0,2), OP(CHAR,2,0,'a'), OP(SAVE,3,0,3), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(ops, 4u, 2u, NULL);
    if (!p) return;
    check(p->first_ascii_valid && (p->first_ascii['a' / 64u] & (UINT64_C(1) << ('a' % 64u))),
        "first-scalar analysis follows capture saves to a consuming site");
    for (unsigned byte = 0; byte < 128u; ++byte) {
        uint8_t input[] = {(uint8_t)byte, 'a'};
        CettaReResultV1 x = cetta_re_v1_find_policy_valid_utf8(p, input, sizeof(input), 0u, CETTA_RE_V1_SEARCH, NULL);
        CettaReResultV1 y = cetta_re_v1_find_reference_valid_utf8(p, input, sizeof(input), 0u, CETTA_RE_V1_SEARCH, NULL);
        x.state_visits = y.state_visits;
        check(same_result(x, y), "all ASCII starts, including NUL, preserve the selected captures");
        cetta_re_v1_result_free(&x); cetta_re_v1_result_free(&y);
        for (uint64_t budget = 1; budget < 8u; ++budget) {
            CettaReExecutionPolicyV1 policy = {.work_budget = budget};
            x = cetta_re_v1_find_policy_valid_utf8(p, input, sizeof(input), 0u, CETTA_RE_V1_SEARCH, &policy);
            y = cetta_re_v1_find_reference_valid_utf8(p, input, sizeof(input), 0u, CETTA_RE_V1_SEARCH, &policy);
            check(same_result(x, y), "bounded search retains the exact unfiltered work schedule");
            cetta_re_v1_result_free(&x); cetta_re_v1_result_free(&y);
        }
    }
    uint8_t absent[4096]; memset(absent, 'x', sizeof(absent));
    Cancellation control = {.stop = 100u};
    CettaReExecutionPolicyV1 policy = {.cancelled = cancelled, .context = &control};
    CettaReResultV1 r = cetta_re_v1_find_policy_valid_utf8(p, absent, sizeof(absent), 0u, CETTA_RE_V1_SEARCH, &policy);
    check(r.outcome == CETTA_RE_V1_CANCELLED && control.calls == 100u,
        "a long skipped run still polls cooperative cancellation");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
    const CettaReInstructionV1 nullable[] = {OP(ASSERT,1,0,CETTA_RE_V1_TEXT_START), OP(MATCH,0,0,0)};
    p = program(nullable, 2u, 1u, NULL);
    check(p && !p->first_ascii_valid, "assertions cannot disguise a possibly nullable path");
    cetta_re_v1_release(p);
}
int main(void) {
    provisional_regular(); negative_assertion(); backreference_comparison();
    cursor_atomic(false); cursor_atomic(true);
    reused_workspace();
    first_scalar_filter();
    printf("RegexExecutionPolicyV1Summary %u %u\n", passed, failed);
    return failed ? 1 : 0;
}
