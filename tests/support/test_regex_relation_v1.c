#include "native/regex_selection_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP(name, next, second, arg) {CETTA_RE_V1_##name, next, second, arg}
static unsigned passed, failed;
static void check(bool success, const char *message) {
    if (success) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", message); }
}

static CettaReProgramV1 *program(const CettaReInstructionV1 *ops, uint32_t len,
                                uint32_t groups, const uint32_t *ranks) {
    CettaReProgramSpecV1 spec = {.instructions = ops, .instruction_len = len,
        .group_len = groups, .ranks = ranks};
    CettaReProgramV1 *out = NULL; char error[256];
    bool valid = cetta_re_v1_program(&spec, &out, error, sizeof(error));
    check(valid, "program ingress");
    if (!valid) fprintf(stderr, "%s\n", error);
    return out;
}

typedef struct { size_t start, end, left, right; bool captured; } Expected;
static bool same(const CettaReResultV1 *actual, Expected expected) {
    return actual->outcome == CETTA_RE_V1_FOUND && actual->group_len &&
        actual->groups[0].participated && actual->groups[0].start == expected.start &&
        actual->groups[0].end == expected.end &&
        (actual->group_len == 1u || (actual->groups[1].participated == expected.captured &&
        (!expected.captured || (actual->groups[1].start == expected.left &&
                               actual->groups[1].end == expected.right))));
}

static void enumerate(CettaReProgramV1 *p, const uint8_t *bytes, size_t len,
    size_t start, CettaReModeV1 mode, const Expected *expected, size_t count,
    uint64_t budget, const char *message) {
    CettaReRelationV1 *cursor = NULL; CettaReOutcomeV1 error;
    bool opened = cetta_re_v1_relation_open(p, bytes, len, start, mode, &cursor, &error);
    check(opened && error == CETTA_RE_V1_FOUND, "owned relation opens");
    if (!opened) return;
    CettaReExecutionPolicyV1 policy = {.work_budget = budget};
    size_t at = 0u, steps = 0u; bool okay = true, complete = false;
    while (++steps < 100000u) {
        CettaReResultV1 r = cetta_re_v1_relation_next(cursor, &policy);
        if (r.outcome == CETTA_RE_V1_INCOMPLETE) {
            okay = okay && budget && r.state_visits == budget;
        } else if (r.outcome == CETTA_RE_V1_NO_MATCH) {
            complete = true; cetta_re_v1_result_free(&r); break;
        } else {
            okay = okay && at < count && same(&r, expected[at < count ? at : 0u]);
            ++at;
            if (r.outcome != CETTA_RE_V1_FOUND) { cetta_re_v1_result_free(&r); break; }
        }
        cetta_re_v1_result_free(&r);
    }
    check(okay && complete && at == count, message);
    CettaReResultV1 end = cetta_re_v1_relation_next(cursor, NULL);
    check(end.outcome == CETTA_RE_V1_NO_MATCH && !end.groups && !end.state_visits,
        "completed absence is stable and does not explore again");
    cetta_re_v1_result_free(&end); cetta_re_v1_relation_release(cursor);
}

static void alternatives_and_overlap(void) {
    const CettaReInstructionV1 choice[] = {
        OP(SPLIT,1,2,0), OP(CHAR,4,0,'a'), OP(CHAR,3,0,'a'),
        OP(CHAR,4,0,'a'), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(choice, 5u, 1u, NULL);
    const Expected all[] = {{0,1,0,0,false}, {0,2,0,0,false}, {1,2,0,0,false}};
    enumerate(p, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_SEARCH, all, 3u, 0u,
        "relation retains a shorter alternative and overlapping later start");
    enumerate(p, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_SEARCH, all, 3u, 1u,
        "one-transition slices retain every occurrence without replay");
    enumerate(p, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_PREFIX, all, 2u, 1u,
        "prefix relation fixes only the start, not the selected endpoint");
    enumerate(p, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_FULL, all + 1u, 1u, 1u,
        "full relation tests end eligibility before emitting an answer");
    enumerate(p, (const uint8_t *)"bb", 2u, 0u, CETTA_RE_V1_SEARCH, all, 0u, 1u,
        "exhaustion is not an invented alternative");
    CettaReResultV1 selected = cetta_re_v1_find(p, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_SEARCH, 0u);
    check(same(&selected, all[0]), "existing selected search remains first-priority");
    cetta_re_v1_result_free(&selected); cetta_re_v1_release(p);

    const CettaReInstructionV1 duplicates[] = {
        OP(SPLIT,1,1,0), OP(SAVE,2,0,2), OP(CHAR,3,0,'a'), OP(SAVE,4,0,3), OP(MATCH,0,0,0)};
    p = program(duplicates, 5u, 2u, NULL);
    const Expected twins[] = {{0,1,0,1,true}, {0,1,0,1,true}};
    enumerate(p, (const uint8_t *)"a", 1u, 0u, CETTA_RE_V1_FULL, twins, 2u, 1u,
        "equal graph alternatives retain occurrence multiplicity");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 captures[] = {
        OP(SPLIT,1,4,0), OP(SAVE,2,0,2), OP(CHAR,3,0,'a'), OP(SAVE,5,0,3),
        OP(CHAR,5,0,'a'), OP(MATCH,0,0,0)};
    p = program(captures, 6u, 2u, NULL);
    const Expected participation[] = {{0,1,0,1,true}, {0,1,0,0,false}};
    enumerate(p, (const uint8_t *)"a", 1u, 0u, CETTA_RE_V1_FULL, participation, 2u, 1u,
        "same whole span retains participating and unmatched capture alternatives");
    cetta_re_v1_release(p);
}

static void cycles_utf8_and_context(void) {
    const CettaReInstructionV1 loop[] = {OP(SPLIT,1,2,0), OP(JUMP,0,0,0), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(loop, 3u, 1u, NULL);
    const uint8_t input[] = {'a',0xce,0xbb};
    const Expected boundaries[] = {{0,0,0,0,false}, {1,1,0,0,false}, {3,3,0,0,false}};
    enumerate(p, input, sizeof(input), 0u, CETTA_RE_V1_SEARCH, boundaries, 3u, 1u,
        "nullable cycles are finite and starts skip UTF-8 continuation bytes");
    CettaReRelationV1 *cursor = NULL; CettaReOutcomeV1 error;
    check(!cetta_re_v1_relation_open(p, input, sizeof(input), 2u, CETTA_RE_V1_SEARCH, &cursor, &error) &&
        error == CETTA_RE_V1_INVALID_ARGUMENT, "non-scalar start is refused");
    const uint8_t malformed[] = {0xc0,0x80};
    check(!cetta_re_v1_relation_open(p, malformed, sizeof(malformed), 0u, CETTA_RE_V1_SEARCH, &cursor, &error) &&
        error == CETTA_RE_V1_INVALID_UTF8, "malformed UTF-8 is not logical absence");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 context[] = {
        OP(ASSERT,1,0,CETTA_RE_V1_TEXT_START), OP(CHAR,2,0,'a'), OP(MATCH,0,0,0)};
    p = program(context, 3u, 1u, NULL);
    enumerate(p, (const uint8_t *)"ba", 2u, 1u, CETTA_RE_V1_PREFIX, boundaries, 0u, 1u,
        "a restricted start does not redefine the original text-start assertion");
    cetta_re_v1_release(p);
}

static bool cancelled(void *context) { return *(bool *)context; }
static void owned_residual_and_backreference(void) {
    const CettaReInstructionV1 ops[] = {
        OP(SAVE,1,0,2), OP(CHAR,2,0,'a'), OP(SPLIT,1,3,0), OP(SAVE,4,0,3),
        OP(CHAR,5,0,'-'), OP(BACKREF,6,0,1), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(ops, 7u, 2u, NULL);
    const Expected expected[] = {{0,17,0,8,true}};
    enumerate(p, (const uint8_t *)"aaaaaaaa-aaaaaaaa", 17u, 0u, CETTA_RE_V1_FULL, expected, 1u, 1u,
        "a backreference longer than each work slice resumes scalar by scalar");
    CettaReRelationV1 *cursor = NULL; CettaReOutcomeV1 error;
    uint8_t *input = malloc(17u); memcpy(input, "aaaaaaaa-aaaaaaaa", 17u);
    check(cetta_re_v1_relation_open(p, input, 17u, 0u, CETTA_RE_V1_FULL, &cursor, &error),
        "owned input relation opens");
    memset(input, 'x', 17u); free(input); cetta_re_v1_release(p);
    bool stop = true;
    CettaReExecutionPolicyV1 policy = {.cancelled = cancelled, .context = &stop};
    CettaReResultV1 r = cetta_re_v1_relation_next(cursor, &policy);
    check(r.outcome == CETTA_RE_V1_CANCELLED && !r.groups && !r.state_visits,
        "cancellation does not consume the residual or claim zero answers");
    cetta_re_v1_result_free(&r);
    stop = false; r = cetta_re_v1_relation_next(cursor, &policy);
    check(same(&r, expected[0]), "resumption owns both input and program independently of their caller");
    cetta_re_v1_result_free(&r);
    stop = true; r = cetta_re_v1_relation_next(cursor, &policy);
    check(r.outcome == CETTA_RE_V1_CANCELLED, "cancellation after publication retains only remaining work");
    cetta_re_v1_result_free(&r);
    stop = false; r = cetta_re_v1_relation_next(cursor, &policy);
    check(r.outcome == CETTA_RE_V1_NO_MATCH, "resume after publication does not repeat the accepted answer");
    cetta_re_v1_result_free(&r); cetta_re_v1_relation_release(cursor);
}

static void atomic_observations(void) {
    const CettaReInstructionV1 atomic[] = {
        OP(LOOKAHEAD,1,3,0), OP(BACKREF,2,0,1), OP(MATCH,0,0,0),
        OP(SAVE,4,0,2), OP(SPLIT,5,7,0), OP(CHAR,6,0,'a'), OP(SAVE,9,0,3),
        OP(CHAR,8,0,'a'), OP(CHAR,10,0,'a'), OP(MATCH,0,0,0), OP(SAVE,9,0,3)};
    const uint32_t ranks[] = {1,1,1,0,0,0,0,0,0,0,0};
    CettaReProgramV1 *p = program(atomic, 11u, 2u, ranks);
    const Expected expected[] = {{0,1,0,1,true}};
    enumerate(p, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_PREFIX, expected, 1u, 1u,
        "atomic lookahead exports its local winner, not every body alternative");
    enumerate(p, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_FULL, expected, 0u, 1u,
        "failed continuation cannot reopen the discarded atomic-body alternative");
    uint8_t *artifact = NULL; size_t artifact_len = 0u; char text[256];
    CettaReProgramV1 *restored = NULL;
    check(cetta_re_v1_store(p, &artifact, &artifact_len, text, sizeof(text)) &&
        !memcmp(artifact, "CRP3", 4u) && cetta_re_v1_restore(artifact, artifact_len,
            &restored, text, sizeof(text)), "extended relation uses an actual restored CRP3 graph");
    free(artifact);
    enumerate(restored, (const uint8_t *)"aa", 2u, 0u, CETTA_RE_V1_PREFIX, expected, 1u, 1u,
        "restored extended relation preserves atomic captures without authoring preparation");
    cetta_re_v1_release(restored);
    cetta_re_v1_release(p);

    const CettaReInstructionV1 negative[] = {
        OP(LOOKAHEAD,1,2,1), OP(MATCH,0,0,0), OP(CHAR,3,0,'a'), OP(MATCH,0,0,0)};
    const uint32_t negative_ranks[] = {1,1,0,0};
    p = program(negative, 4u, 1u, negative_ranks);
    const Expected empty[] = {{0,0,0,0,false}};
    enumerate(p, (const uint8_t *)"b", 1u, 0u, CETTA_RE_V1_PREFIX, empty, 1u, 1u,
        "negative observation succeeds only after complete body absence");
    enumerate(p, (const uint8_t *)"a", 1u, 0u, CETTA_RE_V1_PREFIX, empty, 0u, 1u,
        "a paused negative observation cannot invent a successful outer match");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 negative_capture[] = {
        OP(LOOKAHEAD,1,2,1), OP(MATCH,0,0,0), OP(SAVE,3,0,2),
        OP(CHAR,4,0,'a'), OP(CHAR,5,0,'b'), OP(SAVE,6,0,3), OP(MATCH,0,0,0)};
    const uint32_t negative_capture_ranks[] = {1,1,0,0,0,0,0};
    p = program(negative_capture, 7u, 2u, negative_capture_ranks);
    enumerate(p, (const uint8_t *)"a", 1u, 0u, CETTA_RE_V1_PREFIX, empty, 1u, 1u,
        "a failed negative body does not leak its provisional capture writes");
    cetta_re_v1_release(p);

    const CettaReInstructionV1 reverse[] = {
        OP(LOOKBEHIND,1,2,2), OP(MATCH,0,0,0), OP(SAVE,3,0,2),
        OP(CHAR,4,0,0x3bb), OP(SAVE,5,0,3), OP(MATCH,0,0,0)};
    const uint32_t reverse_ranks[] = {1,1,0,0,0,0};
    p = program(reverse, 6u, 2u, reverse_ranks);
    const uint8_t lambda[] = {0xce,0xbb};
    const Expected reversed[] = {{2,2,0,2,true}};
    enumerate(p, lambda, 2u, 0u, CETTA_RE_V1_SEARCH, reversed, 1u, 1u,
        "reverse observation retains original UTF-8 byte captures without consuming the outer match");
    cetta_re_v1_release(p);
}

typedef struct {
    const CettaReInstructionV1 *ops;
    const uint8_t *bytes;
    size_t len, start;
    CettaReModeV1 mode;
    bool ancestors[8][4];
    Expected answers[512];
    size_t count;
    bool overflow;
} Reference;

/* Independent finite point-simple path relation for the generated regular
 * fragment. Both SPLIT uses are visited even when their targets coincide. */
static void reference_visit(Reference *r, uint32_t pc, size_t pos) {
    if (r->ancestors[pc][pos]) return;
    r->ancestors[pc][pos] = true;
    CettaReInstructionV1 op = r->ops[pc];
    if (op.opcode == CETTA_RE_V1_MATCH) {
        if (r->mode != CETTA_RE_V1_FULL || pos == r->len) {
            if (r->count == sizeof(r->answers) / sizeof(*r->answers)) r->overflow = true;
            else r->answers[r->count++] = (Expected){r->start, pos, 0u, 0u, false};
        }
    } else if (op.opcode == CETTA_RE_V1_SPLIT) {
        reference_visit(r, op.next, pos); reference_visit(r, op.second, pos);
    } else if (op.opcode == CETTA_RE_V1_JUMP) reference_visit(r, op.next, pos);
    else if (op.opcode == CETTA_RE_V1_ASSERT) {
        if ((op.argument == CETTA_RE_V1_TEXT_START && !pos) ||
            (op.argument == CETTA_RE_V1_TEXT_END && pos == r->len)) reference_visit(r, op.next, pos);
    } else if (pos < r->len && (op.opcode == CETTA_RE_V1_ANY ||
        (op.opcode == CETTA_RE_V1_CHAR && r->bytes[pos] == op.argument)))
        reference_visit(r, op.next, pos + 1u);
    r->ancestors[pc][pos] = false;
}

static uint32_t random_state = 0x632be59bu;
static uint32_t random_number(void) {
    random_state ^= random_state << 13u; random_state ^= random_state >> 17u;
    random_state ^= random_state << 5u; return random_state;
}

static void generated_independent_relation(void) {
    static const uint32_t kinds[] = {CETTA_RE_V1_FAIL, CETTA_RE_V1_MATCH,
        CETTA_RE_V1_CHAR, CETTA_RE_V1_ANY, CETTA_RE_V1_JUMP,
        CETTA_RE_V1_SPLIT, CETTA_RE_V1_ASSERT};
    for (unsigned graph = 0u; graph < 512u; ++graph) {
        CettaReInstructionV1 ops[7];
        for (uint32_t i = 0u; i < 6u; ++i) {
            uint32_t opcode = kinds[random_number() % (sizeof(kinds) / sizeof(*kinds))];
            uint32_t first = i + 1u + random_number() % (6u - i);
            uint32_t second = i + 1u + random_number() % (6u - i);
            /* Self jumps and split-to-self exercise path-local cycle cuts,
             * without imposing a depth cutoff on the reference relation. */
            if (opcode == CETTA_RE_V1_JUMP && !(graph % 3u)) first = i;
            if (opcode == CETTA_RE_V1_SPLIT && !(graph % 5u)) second = i;
            ops[i] = (CettaReInstructionV1){opcode, first, second,
                opcode == CETTA_RE_V1_ASSERT ? (random_number() & 1u) : 'a' + (random_number() & 1u)};
        }
        ops[6] = (CettaReInstructionV1)OP(MATCH,0,0,0);
        CettaReProgramV1 *p = program(ops, 7u, 1u, NULL);
        if (!p) continue;
        for (size_t len = 0u; len <= 3u; ++len) {
            for (unsigned word = 0u; word < (1u << len); ++word) {
                uint8_t bytes[3];
                for (size_t i = 0u; i < len; ++i) bytes[i] = (uint8_t)('a' + ((word >> i) & 1u));
                for (unsigned mode = 0u; mode < 3u; ++mode) {
                    Reference ref = {.ops = ops, .bytes = bytes, .len = len, .mode = (CettaReModeV1)mode};
                    for (size_t start = 0u; start <= len; ++start) {
                        ref.start = start; reference_visit(&ref, 0u, start);
                        if (mode != CETTA_RE_V1_SEARCH) break;
                    }
                    check(!ref.overflow, "independent finite generated relation fits its observation buffer");
                    enumerate(p, bytes, len, 0u, (CettaReModeV1)mode, ref.answers, ref.count,
                        graph % 2u ? 1u : 0u, "generated cursor equals independent accepting-path relation");
                }
            }
        }
        cetta_re_v1_release(p);
    }
}

typedef struct { uint32_t pc; size_t pos, left, right; bool open; } CaptureState;
typedef struct {
    Reference observations;
    CaptureState ancestors[16];
    size_t depth;
} CaptureReference;

static void capture_reference_visit(CaptureReference *r, CaptureState state) {
    for (size_t i = 0u; i < r->depth; ++i) {
        CaptureState prior = r->ancestors[i];
        if (prior.pc == state.pc && prior.pos == state.pos && prior.left == state.left &&
            prior.right == state.right && prior.open == state.open) return;
    }
    if (r->depth == sizeof(r->ancestors) / sizeof(*r->ancestors)) {
        r->observations.overflow = true; return;
    }
    r->ancestors[r->depth++] = state;
    Reference *out = &r->observations;
    CettaReInstructionV1 op = out->ops[state.pc];
    if (op.opcode == CETTA_RE_V1_MATCH) {
        if (out->mode != CETTA_RE_V1_FULL || state.pos == out->len) {
            if (out->count == sizeof(out->answers) / sizeof(*out->answers)) out->overflow = true;
            else out->answers[out->count++] = (Expected){out->start, state.pos, state.left, state.right,
                !state.open && state.left != SIZE_MAX && state.right != SIZE_MAX};
        }
    } else if (op.opcode == CETTA_RE_V1_SPLIT) {
        CaptureState branch = state; branch.pc = op.next; capture_reference_visit(r, branch);
        branch.pc = op.second; capture_reference_visit(r, branch);
    } else if (op.opcode == CETTA_RE_V1_SAVE) {
        if (op.argument == 2u) { state.left = state.pos; state.right = SIZE_MAX; state.open = true; }
        else if (state.open) { state.right = state.pos; state.open = false; }
        state.pc = op.next; capture_reference_visit(r, state);
    } else if (op.opcode == CETTA_RE_V1_BACKREF) {
        if (!state.open && state.left != SIZE_MAX && state.right != SIZE_MAX) {
            size_t width = state.right - state.left;
            if (width <= out->len - state.pos &&
                !memcmp(out->bytes + state.left, out->bytes + state.pos, width)) {
                state.pc = op.next; state.pos += width; capture_reference_visit(r, state);
            }
        }
    } else if (op.opcode == CETTA_RE_V1_JUMP) {
        state.pc = op.next; capture_reference_visit(r, state);
    } else if (state.pos < out->len && (op.opcode == CETTA_RE_V1_ANY ||
        (op.opcode == CETTA_RE_V1_CHAR && out->bytes[state.pos] == op.argument))) {
        state.pc = op.next; ++state.pos; capture_reference_visit(r, state);
    }
    --r->depth;
}

static void generated_capture_relation(void) {
    static const uint32_t kinds[] = {CETTA_RE_V1_FAIL, CETTA_RE_V1_MATCH, CETTA_RE_V1_CHAR,
        CETTA_RE_V1_ANY, CETTA_RE_V1_JUMP, CETTA_RE_V1_SPLIT, CETTA_RE_V1_SAVE, CETTA_RE_V1_BACKREF};
    for (unsigned graph = 0u; graph < 256u; ++graph) {
        CettaReInstructionV1 ops[9];
        for (uint32_t i = 0u; i < 7u; ++i) {
            uint32_t opcode = kinds[random_number() % (sizeof(kinds) / sizeof(*kinds))];
            uint32_t first = i + 1u + random_number() % (7u - i);
            uint32_t second = opcode == CETTA_RE_V1_BACKREF ? 0u :
                i + 1u + random_number() % (7u - i);
            if (opcode == CETTA_RE_V1_SPLIT && !(graph % 5u)) second = i;
            if (opcode == CETTA_RE_V1_JUMP && !(graph % 3u)) first = i;
            uint32_t argument = opcode == CETTA_RE_V1_SAVE ? 2u + (random_number() & 1u) :
                opcode == CETTA_RE_V1_BACKREF ? 1u : 'a' + (random_number() & 1u);
            ops[i] = (CettaReInstructionV1){opcode, first, second, argument};
        }
        ops[7] = (CettaReInstructionV1)OP(MATCH,0,0,0);
        /* The profile is program-wide, even when this dependent operation
         * is unreachable from this particular entry. */
        ops[8] = (CettaReInstructionV1)OP(BACKREF,7,0,1);
        CettaReProgramV1 *p = program(ops, 9u, 2u, NULL);
        if (!p) continue;
        for (size_t len = 0u; len <= 3u; ++len) {
            for (unsigned word = 0u; word < (1u << len); ++word) {
                uint8_t bytes[3];
                for (size_t i = 0u; i < len; ++i) bytes[i] = (uint8_t)('a' + ((word >> i) & 1u));
                for (unsigned mode = 0u; mode < 3u; ++mode) {
                    CaptureReference ref = {.observations = {.ops = ops, .bytes = bytes, .len = len,
                        .mode = (CettaReModeV1)mode}};
                    for (size_t start = 0u; start <= len; ++start) {
                        ref.observations.start = start;
                        capture_reference_visit(&ref, (CaptureState){0u,start,SIZE_MAX,SIZE_MAX,false});
                        if (mode != CETTA_RE_V1_SEARCH) break;
                    }
                    check(!ref.observations.overflow, "independent generated full-configuration relation stays finite");
                    enumerate(p, bytes, len, 0u, (CettaReModeV1)mode,
                        ref.observations.answers, ref.observations.count, graph % 2u ? 1u : 0u,
                        "generated capture/reference cursor equals independent path relation");
                }
            }
        }
        cetta_re_v1_release(p);
    }
}

static void demand_bounded_prefix(void) {
    const uint32_t branches = 256u;
    CettaReInstructionV1 *ops = malloc((branches + 1u) * sizeof(*ops));
    for (uint32_t i = 0u; i < branches; ++i) ops[i] = (CettaReInstructionV1)OP(SPLIT,i+1u,i+1u,0);
    ops[branches] = (CettaReInstructionV1)OP(MATCH,0,0,0);
    CettaReProgramV1 *p = program(ops, branches + 1u, 1u, NULL);
    free(ops);
    CettaReRelationV1 *cursor = NULL; CettaReOutcomeV1 error;
    check(cetta_re_v1_relation_open(p, NULL, 0u, 0u, CETTA_RE_V1_FULL, &cursor, &error),
        "large multiplicity relation opens without materializing its alternatives");
    CettaReExecutionPolicyV1 policy = {.work_budget = 300u};
    CettaReResultV1 first = cetta_re_v1_relation_next(cursor, &policy);
    Expected empty = {0,0,0,0,false};
    check(same(&first, empty) && first.state_visits <= policy.work_budget,
        "one requested occurrence takes one path through exponentially many alternatives");
    cetta_re_v1_result_free(&first);
    CettaReResultV1 second = cetta_re_v1_relation_next(cursor, &policy);
    check(same(&second, empty) && second.state_visits < branches / 4u,
        "next occurrence reuses its stored frontier instead of restarting the first path");
    cetta_re_v1_result_free(&second); cetta_re_v1_relation_release(cursor); cetta_re_v1_release(p);
}

static void deep_retained_observations(void) {
    uint32_t depth = 1024u, count = depth + 1u;
    CettaReInstructionV1 *ops = malloc((size_t)count * sizeof(*ops));
    uint32_t *ranks = malloc((size_t)count * sizeof(*ranks));
    for (uint32_t i = 0u; i < depth; ++i) {
        ops[i] = (CettaReInstructionV1)OP(LOOKAHEAD,depth,i+1u,0);
        ranks[i] = depth - i;
    }
    ops[depth] = (CettaReInstructionV1)OP(MATCH,0,0,0); ranks[depth] = 0u;
    CettaReProgramV1 *p = program(ops, count, 1u, ranks);
    free(ops); free(ranks);
    const Expected empty[] = {{0,0,0,0,false}};
    enumerate(p, NULL, 0u, 0u, CETTA_RE_V1_FULL, empty, 1u, 1u,
        "deep observation tasks pause and resume without C recursion");
    cetta_re_v1_release(p);
}

static void profile_and_artifact_ownership(void) {
    const CettaReInstructionV1 regular[] = {
        OP(SAVE,1,0,2), OP(CHAR,2,0,'a'), OP(SAVE,3,0,3), OP(SAVE,4,0,2), OP(MATCH,0,0,0)};
    CettaReProgramV1 *p = program(regular, 5u, 2u, NULL);
    const Expected regular_capture[] = {{0,1,1,1,true}};
    enumerate(p, (const uint8_t *)"a", 1u, 0u, CETTA_RE_V1_FULL, regular_capture, 1u, 1u,
        "regular relation preserves Pike's raw SAVE last-write profile");
    CettaReResultV1 selected = cetta_re_v1_find(p, (const uint8_t *)"a", 1u, 0u, CETTA_RE_V1_FULL, 0u);
    check(same(&selected, regular_capture[0]), "raw SAVE control agrees with unchanged regular selected engine");
    cetta_re_v1_result_free(&selected);
    uint8_t *bytes = NULL; size_t len = 0u; char text[256];
    CettaReProgramV1 *restored = NULL;
    check(cetta_re_v1_store(p, &bytes, &len, text, sizeof(text)) && !memcmp(bytes, "CRP2", 4u) &&
        cetta_re_v1_restore(bytes, len, &restored, text, sizeof(text)), "regular artifact remains CRP2");
    free(bytes); cetta_re_v1_release(p);
    enumerate(restored, (const uint8_t *)"a", 1u, 0u, CETTA_RE_V1_FULL, regular_capture, 1u, 1u,
        "restored program owns the same relation without rebuilding its graph");
    CettaReRelationV1 *cursor = NULL; CettaReOutcomeV1 error;
    check(cetta_re_v1_relation_open(restored, (const uint8_t *)"a", 1u, 0u,
        CETTA_RE_V1_FULL, &cursor, &error), "replacement control opens");
    CettaReRelationV1 *saved = cursor;
    check(!cetta_re_v1_relation_open(restored, NULL, 1u, 0u, CETTA_RE_V1_FULL, &cursor, &error) &&
        cursor == saved && error == CETTA_RE_V1_INVALID_ARGUMENT,
        "failed relation replacement preserves its existing owner and residual");
    CettaReResultV1 result = cetta_re_v1_relation_next(cursor, NULL);
    check(same(&result, regular_capture[0]), "preserved owner still produces its original answer");
    cetta_re_v1_result_free(&result); cetta_re_v1_relation_release(cursor); cetta_re_v1_release(restored);
}

int main(void) {
    alternatives_and_overlap(); cycles_utf8_and_context();
    owned_residual_and_backreference(); atomic_observations();
    profile_and_artifact_ownership(); generated_independent_relation(); generated_capture_relation();
    demand_bounded_prefix(); deep_retained_observations();
    printf("(RegexRelationV1Summary %u %u)\n", passed, failed);
    return failed ? 1 : 0;
}
