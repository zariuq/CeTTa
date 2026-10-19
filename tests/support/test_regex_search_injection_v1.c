#include "native/regex_selection_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Qualify injected search starts against ordered PREFIX calls on the same
 * original immutable input. These share the native instruction executor,
 * not a Rust oracle or an independent C refinement proof. */
static unsigned passed, failed;

static void check(bool correct, const char *name) {
    if (correct) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
}

static CettaReResultV1 ordered_scan(const CettaReProgramV1 *program,
                                  const uint8_t *input, size_t length,
                                  size_t start, uint64_t budget) {
    size_t position = start;
    for (;;) {
        CettaReResultV1 result = cetta_re_v1_find(program, input, length,
            position, CETTA_RE_V1_PREFIX, budget);
        if (result.outcome != CETTA_RE_V1_NO_MATCH || position == length)
            return result;
        cetta_re_v1_result_free(&result);
        /* The public PREFIX call has established valid UTF-8 and start. */
        ++position;
        while (position < length && (input[position] & 0xc0u) == 0x80u) ++position;
    }
}

static bool same_result(const CettaReResultV1 *first, const CettaReResultV1 *second) {
    if (first->outcome != second->outcome || first->group_len != second->group_len)
        return false;
    for (uint32_t group = 0; group < first->group_len; ++group) {
        const CettaReCaptureV1 a = first->groups[group], b = second->groups[group];
        if (a.participated != b.participated || a.start != b.start || a.end != b.end)
            return false;
    }
    /* Visit counts measure different schedules, not match observations. */
    return true;
}

#define RX_MATCH { .opcode = CETTA_RE_V1_MATCH }
#define RX_FAIL { .opcode = CETTA_RE_V1_FAIL }
#define RX_CHAR(N,C) { .opcode = CETTA_RE_V1_CHAR, .next = (N), .argument = (C) }
#define RX_ANY(N) { .opcode = CETTA_RE_V1_ANY, .next = (N) }
#define RX_JUMP(N) { .opcode = CETTA_RE_V1_JUMP, .next = (N) }
#define RX_SPLIT(A,B) { .opcode = CETTA_RE_V1_SPLIT, .next = (A), .second = (B) }
#define RX_SAVE(N,S) { .opcode = CETTA_RE_V1_SAVE, .next = (N), .argument = (S) }
#define RX_ASSERT(N,A) { .opcode = CETTA_RE_V1_ASSERT, .next = (N), .argument = (A) }

static const CettaReRangeV1 words[] = {
    {48,57}, {65,90}, {95,95}, {97,122}, {769,769}, {955,955}, {1632,1641}
};
static const CettaReRangeV1 letters[] = {{97,122}, {955,955}};
static const CettaReInstructionV1 literal_a[] = {RX_CHAR(1,97), RX_MATCH};
static const CettaReInstructionV1 alternatives[] = {
    RX_SPLIT(1,2), RX_CHAR(4,97), RX_CHAR(3,97), RX_CHAR(4,98), RX_MATCH
};
static const CettaReInstructionV1 later_longer[] = {
    RX_SPLIT(1,2), RX_CHAR(5,97), RX_CHAR(3,98), RX_CHAR(4,98), RX_CHAR(5,98), RX_MATCH
};
static const CettaReInstructionV1 earlier_lower_branch[] = {
    RX_SPLIT(1,2), RX_CHAR(4,98), RX_CHAR(3,97), RX_CHAR(4,98), RX_MATCH
};
static const CettaReInstructionV1 earlier_waiting[] = {
    RX_SPLIT(1,5), RX_CHAR(2,97), RX_SPLIT(3,4), RX_ANY(2), RX_CHAR(6,122),
    RX_CHAR(6,98), RX_MATCH
};
static const CettaReInstructionV1 greedy_capture[] = {
    RX_SPLIT(1,5), RX_SAVE(2,2), RX_CHAR(3,97), RX_SAVE(4,3), RX_SPLIT(1,5), RX_MATCH
};
static const CettaReInstructionV1 lazy_capture[] = {
    RX_SPLIT(5,1), RX_SAVE(2,2), RX_CHAR(3,97), RX_SAVE(4,3), RX_SPLIT(5,1), RX_MATCH
};
static const CettaReInstructionV1 nullable_capture[] = {
    RX_SPLIT(1,7), RX_SAVE(2,2), RX_SPLIT(3,4), RX_CHAR(5,97), RX_JUMP(5),
    RX_SAVE(6,3), RX_SPLIT(1,7), RX_MATCH
};
static const CettaReInstructionV1 failed_capture[] = {
    RX_SPLIT(1,5), RX_SAVE(2,2), RX_CHAR(3,97), RX_SAVE(4,3), RX_CHAR(7,99),
    RX_CHAR(6,97), RX_CHAR(7,98), RX_MATCH
};
static const CettaReInstructionV1 shared_capture[] = {
    RX_SPLIT(1,2), RX_SAVE(3,2), RX_JUMP(3), RX_ANY(4), RX_SAVE(5,3), RX_MATCH
};
static const CettaReInstructionV1 unmatched_capture[] = {
    RX_SPLIT(2,1), RX_SAVE(3,2), RX_JUMP(3), RX_ANY(4), RX_SAVE(5,3), RX_MATCH
};
static const CettaReInstructionV1 text_start[] = {
    RX_ASSERT(1,CETTA_RE_V1_TEXT_START), RX_CHAR(2,97), RX_MATCH
};
static const CettaReInstructionV1 text_end[] = {
    RX_CHAR(1,97), RX_ASSERT(2,CETTA_RE_V1_TEXT_END), RX_MATCH
};
static const CettaReInstructionV1 line_start[] = {
    RX_ASSERT(1,CETTA_RE_V1_LINE_START), RX_CHAR(2,98), RX_MATCH
};
static const CettaReInstructionV1 line_end[] = {
    RX_CHAR(1,97), RX_ASSERT(2,CETTA_RE_V1_LINE_END), RX_MATCH
};
static const CettaReInstructionV1 boundary[] = {
    RX_ASSERT(1,CETTA_RE_V1_WORD_BOUNDARY), RX_CHAR(2,97), RX_MATCH
};
static const CettaReInstructionV1 ascii_boundary[] = {
    RX_ASSERT(1,CETTA_RE_V1_ASCII_WORD_BOUNDARY), RX_CHAR(2,97), RX_MATCH
};
static const CettaReInstructionV1 nonboundary[] = {
    RX_ASSERT(1,CETTA_RE_V1_NOT_WORD_BOUNDARY), RX_CHAR(2,97), RX_MATCH
};
static const CettaReInstructionV1 greek[] = {
    RX_SAVE(1,2), RX_CHAR(2,955), RX_SAVE(3,3), RX_MATCH
};
static const CettaReInstructionV1 class_run[] = {
    {.opcode = CETTA_RE_V1_CLASS, .next = 1, .argument = 0, .second = 2},
    RX_SPLIT(0,2), RX_MATCH
};
static const CettaReInstructionV1 empty[] = {RX_MATCH};
static const CettaReInstructionV1 dead[] = {RX_FAIL};
static const CettaReInstructionV1 epsilon_cycle[] = {RX_SPLIT(0,1), RX_MATCH};
static const CettaReInstructionV1 nul[] = {RX_CHAR(1,0), RX_MATCH};

typedef struct {
    const char *name;
    const CettaReInstructionV1 *instructions;
    uint32_t count, groups;
} Program;

#define PROGRAM(N,A,G) { (N), (A), (uint32_t)(sizeof(A) / sizeof(*(A))), (G) }
static const Program programs[] = {
    PROGRAM("literal",literal_a,1), PROGRAM("alternative",alternatives,1),
    PROGRAM("later-longer",later_longer,1), PROGRAM("earlier-lower-branch",earlier_lower_branch,1),
    PROGRAM("earlier-waiting",earlier_waiting,1), PROGRAM("greedy-capture",greedy_capture,2),
    PROGRAM("lazy-capture",lazy_capture,2), PROGRAM("nullable-capture",nullable_capture,2),
    PROGRAM("failed-capture",failed_capture,2), PROGRAM("shared-capture",shared_capture,2),
    PROGRAM("unmatched-capture",unmatched_capture,2), PROGRAM("text-start",text_start,1),
    PROGRAM("text-end",text_end,1), PROGRAM("line-start",line_start,1),
    PROGRAM("line-end",line_end,1), PROGRAM("boundary",boundary,1),
    PROGRAM("ascii-boundary",ascii_boundary,1), PROGRAM("nonboundary",nonboundary,1),
    PROGRAM("greek",greek,2), PROGRAM("class-run",class_run,1),
    PROGRAM("empty",empty,1), PROGRAM("dead",dead,1),
    PROGRAM("epsilon-cycle",epsilon_cycle,1), PROGRAM("NUL",nul,1)
};

static CettaReProgramV1 *prepare(const Program *description) {
    CettaReProgramV1 *program = NULL;
    CettaReProgramSpecV1 spec = {
        .instructions = description->instructions, .instruction_len = description->count,
        .group_len = description->groups, .ranges = letters, .range_len = 2,
        .word_ranges = words, .word_range_len = (uint32_t)(sizeof(words) / sizeof(*words))
    };
    char reason[256] = {0};
    bool accepted = cetta_re_v1_program(&spec, &program, reason, sizeof(reason));
    if (!accepted) fprintf(stderr, "%s: %s\n", description->name, reason);
    check(accepted, description->name);
    return program;
}

static void compare(const CettaReProgramV1 *program, const char *name,
                    const uint8_t *input, size_t length, size_t start, uint64_t budget) {
    CettaReResultV1 injected = cetta_re_v1_find(program, input, length, start, CETTA_RE_V1_SEARCH, budget);
    CettaReResultV1 scanned = ordered_scan(program, input, length, start, budget);
    if (!same_result(&injected, &scanned)) {
        fprintf(stderr, "%s start=%zu: injected=%s scanned=%s\n", name, start,
            cetta_re_v1_outcome_name(injected.outcome), cetta_re_v1_outcome_name(scanned.outcome));
    }
    check(same_result(&injected, &scanned), name);
    cetta_re_v1_result_free(&injected);
    cetta_re_v1_result_free(&scanned);
}

static bool whole(const CettaReResultV1 *result, size_t start, size_t end) {
    return result->outcome == CETTA_RE_V1_FOUND && result->group_len &&
        result->groups[0].participated && result->groups[0].start == start &&
        result->groups[0].end == end;
}

static void qualify_scans(void) {
    static const char *const inputs[] = {
        "", "a", "b", "aa", "ab", "abbb", "abz", "xabc", "a\nb", " a", "ba",
        "a\xce\xbb" "b", "\xce\xbb" "a", "\xce\xbb a", "a\xcc\x81" "a"
    };
    static const uint8_t with_nul[] = {'a',0,'b'};
    static const uint8_t invalid[] = {'a',0xff,'b'};
    for (size_t index = 0; index < sizeof(programs) / sizeof(*programs); ++index) {
        const Program *description = &programs[index];
        CettaReProgramV1 *program = prepare(description);
        if (!program) continue;
        for (size_t sample = 0; sample < sizeof(inputs) / sizeof(*inputs); ++sample) {
            const uint8_t *input = (const uint8_t *)inputs[sample];
            size_t length = strlen(inputs[sample]);
            for (size_t start = 0; start <= length; ++start) {
                if (start < length && (input[start] & 0xc0u) == 0x80u) continue;
                compare(program, description->name, input, length, start, 0);
            }
        }
        compare(program, description->name, with_nul, sizeof(with_nul), 0, 0);
        compare(program, description->name, invalid, sizeof(invalid), 0, 0);
        compare(program, description->name, (const uint8_t *)"\xce\xbb", 2, 1, 0);
        compare(program, description->name, (const uint8_t *)"a", 1, 2, 0);
        cetta_re_v1_release(program);
    }
}

static void qualify_observations(void) {
    CettaReProgramV1 *program = prepare(&programs[2]);
    CettaReResultV1 early = cetta_re_v1_find(program, (const uint8_t *)"abbb", 4, 0, CETTA_RE_V1_SEARCH, 0);
    CettaReResultV1 late = cetta_re_v1_find(program, (const uint8_t *)"abbb", 4, 1, CETTA_RE_V1_PREFIX, 0);
    check(whole(&early,0,1) && whole(&late,1,4) && !same_result(&early,&late),
        "later longer match cannot displace an earlier start");
    cetta_re_v1_result_free(&early); cetta_re_v1_result_free(&late); cetta_re_v1_release(program);

    program = prepare(&programs[3]);
    early = cetta_re_v1_find(program, (const uint8_t *)"ab", 2, 0, CETTA_RE_V1_SEARCH, 0);
    check(whole(&early,0,2), "earlier start precedes a later higher-priority source branch");
    cetta_re_v1_result_free(&early); cetta_re_v1_release(program);

    program = prepare(&programs[4]);
    early = cetta_re_v1_find(program, (const uint8_t *)"abz", 3, 0, CETTA_RE_V1_SEARCH, 0);
    check(whole(&early,0,3), "earlier unfinished route survives a later accepting route");
    cetta_re_v1_result_free(&early); cetta_re_v1_release(program);

    program = prepare(&programs[1]);
    early = cetta_re_v1_find(program, (const uint8_t *)"ab", 2, 0, CETTA_RE_V1_PREFIX, 0);
    late = cetta_re_v1_find(program, (const uint8_t *)"ab", 2, 0, CETTA_RE_V1_FULL, 0);
    check(whole(&early,0,1) && whole(&late,0,2), "full eligibility constrains paths before selection");
    check(early.group_len && early.groups[0].end != 2 && whole(&late,0,2),
        "selecting a prefix then rejecting its remaining suffix is not full matching");
    cetta_re_v1_result_free(&early); cetta_re_v1_result_free(&late); cetta_re_v1_release(program);

    program = prepare(&programs[11]);
    static const uint8_t haystack[] = {'b','a'};
    early = cetta_re_v1_find(program, haystack, sizeof(haystack), 0, CETTA_RE_V1_SEARCH, 0);
    late = cetta_re_v1_find(program, haystack + 1, 1, 0, CETTA_RE_V1_PREFIX, 0);
    check(early.outcome == CETTA_RE_V1_NO_MATCH && whole(&late,0,1),
        "slicing a later start would fabricate a text-start match");
    cetta_re_v1_result_free(&early); cetta_re_v1_result_free(&late); cetta_re_v1_release(program);

    program = prepare(&programs[15]);
    const uint8_t unicode_word[] = {0xce,0xbb,'a'};
    early = cetta_re_v1_find(program, unicode_word, sizeof(unicode_word), 0, CETTA_RE_V1_SEARCH, 0);
    check(early.outcome == CETTA_RE_V1_NO_MATCH, "Unicode word boundary retains its left original scalar");
    cetta_re_v1_result_free(&early); cetta_re_v1_release(program);
    program = prepare(&programs[16]);
    early = cetta_re_v1_find(program, unicode_word, sizeof(unicode_word), 0, CETTA_RE_V1_SEARCH, 0);
    check(whole(&early,2,3), "ASCII boundary remains an explicitly different interpretation");
    cetta_re_v1_result_free(&early); cetta_re_v1_release(program);

    for (size_t index = 8; index <= 10; ++index) {
        program = prepare(&programs[index]);
        early = cetta_re_v1_find(program, (const uint8_t *)"ab", 2, 0, CETTA_RE_V1_SEARCH, 0);
        bool participates = index == 9;
        check(early.outcome == CETTA_RE_V1_FOUND && early.group_len == 2 &&
            early.groups[1].participated == participates &&
            (!participates || (early.groups[1].start == 0 && early.groups[1].end == 1)),
            "failed branches and discarded arrivals cannot leak capture snapshots");
        cetta_re_v1_result_free(&early); cetta_re_v1_release(program);
    }

    program = prepare(&programs[18]);
    const uint8_t unicode_capture[] = {'a',0xce,0xbb,'b'};
    early = cetta_re_v1_find(program, unicode_capture, sizeof(unicode_capture), 0, CETTA_RE_V1_SEARCH, 0);
    check(whole(&early,1,3) && early.group_len == 2 && early.groups[1].participated &&
        early.groups[1].start == 1 && early.groups[1].end == 3,
        "captured Unicode scalar retains original UTF-8 byte bounds");
    cetta_re_v1_result_free(&early); cetta_re_v1_release(program);

    program = prepare(&programs[7]);
    early = cetta_re_v1_find(program, (const uint8_t *)"aa", 2, 0, CETTA_RE_V1_SEARCH, 0);
    check(whole(&early,0,2) && early.groups[1].participated &&
        early.groups[1].start == 1 && early.groups[1].end == 2,
        "nullable repetition retains the declared last consuming capture");
    cetta_re_v1_result_free(&early);
    early = cetta_re_v1_find(program, (const uint8_t *)"b", 1, 0, CETTA_RE_V1_SEARCH, 1);
    check(early.outcome == CETTA_RE_V1_INCOMPLETE && !early.groups && !early.group_len,
        "explicit cutoff does not become an empty answer or completed no-match");
    cetta_re_v1_result_free(&early);
    compare(program, "explicit cutoff before first closure completes", (const uint8_t *)"b", 1, 0, 1);
    cetta_re_v1_release(program);
}

int main(void) {
    qualify_scans();
    qualify_observations();
    printf("(RegexSearchInjectionV1Summary %u %u)\n", passed, failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
