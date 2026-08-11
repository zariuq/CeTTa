#include "regular_span_dfa_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RSDFA_V1_NO_STATE UINT32_MAX

typedef struct {
    uint32_t low;
    uint32_t high;
    uint32_t target;
    uint32_t annotation;
} RSDFAV1Transition;

typedef struct {
    uint64_t *subset;
    RSDFAV1Transition *transitions;
    uint32_t transition_len;
    uint32_t transition_cap;
    uint32_t *accept_tags;
    uint32_t accept_len;
    uint32_t *eof_accept_tags;
    uint32_t eof_accept_len;
} RSDFAV1State;

struct RSDFAV1PlanImpl {
    RSDFAV1State *states;
    uint32_t state_len;
    uint32_t state_cap;
    uint32_t subset_word_len;
    uint32_t start_state;
};

typedef struct {
    uint32_t low;
    uint32_t high;
} RSDFAV1Segment;

typedef struct {
    RSDFAV1Segment *data;
    uint32_t len;
    uint32_t cap;
} RSDFAV1SegmentVec;

typedef struct {
    uint32_t state;
    uint32_t start_scalar;
    uint32_t start_byte;
} RSDFAV1Run;

typedef struct {
    RSDFAV1Run *data;
    uint32_t len;
    uint32_t cap;
} RSDFAV1RunVec;

typedef struct {
    RSDFAV1Token *data;
    uint32_t len;
    uint32_t cap;
} RSDFAV1TokenVec;

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} RSDFAV1U32Vec;

typedef struct {
    const RSDFAV1Nfa *nfa;
    RSDFAV1PlanImpl *plan;
    uint32_t state_limit;
    uint32_t transition_limit;
    uint32_t transition_len;
    bool state_limit_hit;
    bool transition_limit_hit;
    bool annotation_conflict;
    bool failed;
    char *error_buf;
    size_t error_buf_size;
} RSDFAV1Build;

static void rsdfa_v1_set_error(char *buf,
                               size_t size,
                               const char *format,
                               ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool rsdfa_v1_grow(void **data,
                          uint32_t *cap,
                          uint32_t needed,
                          size_t item_size) {
    uint32_t next_cap;
    void *next;

    if (needed <= *cap)
        return true;
    next_cap = *cap ? *cap : 16u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / item_size)
        return false;
    next = realloc(*data, (size_t)next_cap * item_size);
    if (!next)
        return false;
    *data = next;
    *cap = next_cap;
    return true;
}

static bool rsdfa_v1_scalar_range_valid(uint32_t low, uint32_t high) {
    return low <= high && high <= UINT32_C(0x10ffff) &&
           !(low <= UINT32_C(0xdfff) && high >= UINT32_C(0xd800));
}

static bool rsdfa_v1_validate_nfa(const RSDFAV1Nfa *nfa,
                                  char *error_buf,
                                  size_t error_buf_size) {
    uint32_t index;

    if (!nfa || nfa->state_len == 0u || nfa->start_len == 0u ||
        nfa->accept_len == 0u || nfa->tag_len == 0u ||
        (nfa->start_len > 0u && !nfa->start_states) ||
        (nfa->edge_len > 0u && !nfa->edges) ||
        (nfa->accept_len > 0u && !nfa->accepts)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad regular-span NFA");
        return false;
    }
    for (index = 0u; index < nfa->start_len; index++) {
        if (nfa->start_states[index] >= nfa->state_len) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "regular-span NFA start state is invalid");
            return false;
        }
    }
    for (index = 0u; index < nfa->accept_len; index++) {
        if (nfa->accepts[index].state >= nfa->state_len ||
            nfa->accepts[index].tag >= nfa->tag_len) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "regular-span NFA accept is invalid");
            return false;
        }
    }
    for (index = 0u; index < nfa->edge_len; index++) {
        const RSDFAV1NfaEdge *edge = &nfa->edges[index];
        uint32_t range_index;
        if (edge->from >= nfa->state_len || edge->to >= nfa->state_len ||
            edge->kind > RSDFA_V1_NFA_EOF) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "regular-span NFA edge is invalid");
            return false;
        }
        if (edge->kind != RSDFA_V1_NFA_RANGES) {
            if (edge->range_len != 0u || edge->ranges) {
                rsdfa_v1_set_error(error_buf, error_buf_size,
                                   "zero-width NFA edge carries ranges");
                return false;
            }
            continue;
        }
        if (edge->range_len == 0u || !edge->ranges) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "consuming NFA edge has no ranges");
            return false;
        }
        for (range_index = 0u;
             range_index < edge->range_len; range_index++) {
            if (!rsdfa_v1_scalar_range_valid(
                    edge->ranges[range_index].low,
                    edge->ranges[range_index].high)) {
                rsdfa_v1_set_error(error_buf, error_buf_size,
                                   "NFA edge has a non-scalar Unicode range");
                return false;
            }
        }
    }
    return true;
}

static bool rsdfa_v1_bit_test(const uint64_t *bits, uint32_t index) {
    return (bits[index / 64u] &
            (UINT64_C(1) << (index % 64u))) != 0u;
}

static bool rsdfa_v1_bit_set(uint64_t *bits, uint32_t index) {
    uint64_t mask = UINT64_C(1) << (index % 64u);
    uint64_t *word = &bits[index / 64u];
    bool changed = (*word & mask) == 0u;
    *word |= mask;
    return changed;
}

static bool rsdfa_v1_bits_empty(const uint64_t *bits, uint32_t word_len) {
    uint32_t index;
    for (index = 0u; index < word_len; index++) {
        if (bits[index] != 0u)
            return false;
    }
    return true;
}

static void rsdfa_v1_zero_closure(const RSDFAV1Nfa *nfa,
                                  uint64_t *bits,
                                  bool include_eof) {
    bool changed = true;

    while (changed) {
        uint32_t index;
        changed = false;
        for (index = 0u; index < nfa->edge_len; index++) {
            const RSDFAV1NfaEdge *edge = &nfa->edges[index];
            if (edge->kind != RSDFA_V1_NFA_EPSILON &&
                !(include_eof && edge->kind == RSDFA_V1_NFA_EOF)) {
                continue;
            }
            if (rsdfa_v1_bit_test(bits, edge->from) &&
                rsdfa_v1_bit_set(bits, edge->to)) {
                changed = true;
            }
        }
    }
}

static int rsdfa_v1_u32_compare(const void *left, const void *right) {
    uint32_t lhs = *(const uint32_t *)left;
    uint32_t rhs = *(const uint32_t *)right;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

static bool rsdfa_v1_collect_accepts(const RSDFAV1Nfa *nfa,
                                     const uint64_t *bits,
                                     uint32_t **out_tags,
                                     uint32_t *out_len) {
    uint32_t *tags = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    uint32_t index;
    uint32_t write;

    *out_tags = NULL;
    *out_len = 0u;
    for (index = 0u; index < nfa->accept_len; index++) {
        if (!rsdfa_v1_bit_test(bits, nfa->accepts[index].state))
            continue;
        if (!rsdfa_v1_grow(
                (void **)&tags, &cap, len + 1u, sizeof(*tags))) {
            free(tags);
            return false;
        }
        tags[len++] = nfa->accepts[index].tag;
    }
    if (len > 1u)
        qsort(tags, len, sizeof(*tags), rsdfa_v1_u32_compare);
    write = 0u;
    for (index = 0u; index < len; index++) {
        if (write == 0u || tags[index] != tags[write - 1u])
            tags[write++] = tags[index];
    }
    *out_tags = tags;
    *out_len = write;
    return true;
}

static void rsdfa_v1_impl_free(RSDFAV1PlanImpl *impl) {
    uint32_t index;

    if (!impl)
        return;
    for (index = 0u; index < impl->state_len; index++) {
        free(impl->states[index].subset);
        free(impl->states[index].transitions);
        free(impl->states[index].accept_tags);
        free(impl->states[index].eof_accept_tags);
    }
    free(impl->states);
    free(impl);
}

void rsdfa_v1_plan_init(RSDFAV1Plan *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void rsdfa_v1_plan_free(RSDFAV1Plan *plan) {
    if (!plan)
        return;
    rsdfa_v1_impl_free(plan->impl);
    memset(plan, 0, sizeof(*plan));
}

void rsdfa_v1_program_init(RSDFAV1Program *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

void rsdfa_v1_program_free(RSDFAV1Program *program) {
    if (!program)
        return;
    free(program->states);
    free(program->transitions);
    free(program->accept_tags);
    free(program->eof_accept_tags);
    memset(program, 0, sizeof(*program));
}

void rsdfa_v1_subset_table_init(RSDFAV1SubsetTable *table) {
    if (table)
        memset(table, 0, sizeof(*table));
}

void rsdfa_v1_subset_table_free(RSDFAV1SubsetTable *table) {
    if (!table)
        return;
    free(table->words);
    memset(table, 0, sizeof(*table));
}

static bool rsdfa_v1_program_tag_slice_valid(
    const uint32_t *tags,
    uint32_t begin,
    uint32_t len,
    uint32_t total,
    uint32_t tag_len) {
    uint32_t index;

    if (begin > total || len > total - begin ||
        (len > 0u && !tags)) {
        return false;
    }
    for (index = 0u; index < len; index++) {
        uint32_t tag = tags[begin + index];
        if (tag >= tag_len ||
            (index > 0u && tags[begin + index - 1u] >= tag)) {
            return false;
        }
    }
    return true;
}

bool rsdfa_v1_program_validate(
    const RSDFAV1Program *program,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t state_index;
    uint32_t transition_end = 0u;
    uint32_t accept_end = 0u;
    uint32_t eof_accept_end = 0u;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || program->state_len == 0u || program->tag_len == 0u ||
        program->start_state >= program->state_len || !program->states ||
        (program->transition_len > 0u && !program->transitions) ||
        (program->accept_tag_len > 0u && !program->accept_tags) ||
        (program->eof_accept_tag_len > 0u &&
         !program->eof_accept_tags)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad regular-span DFA program");
        return false;
    }
    for (state_index = 0u; state_index < program->state_len;
         state_index++) {
        const RSDFAV1ProgramState *state = &program->states[state_index];
        uint32_t transition_index;

        if (transition_end > program->transition_len ||
            state->transition_begin != transition_end ||
            state->transition_len >
                program->transition_len - transition_end ||
            state->accept_begin != accept_end ||
            state->eof_accept_begin != eof_accept_end ||
            !rsdfa_v1_program_tag_slice_valid(
                program->accept_tags, state->accept_begin,
                state->accept_len, program->accept_tag_len,
                program->tag_len) ||
            !rsdfa_v1_program_tag_slice_valid(
                program->eof_accept_tags, state->eof_accept_begin,
                state->eof_accept_len, program->eof_accept_tag_len,
                program->tag_len)) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "regular-span DFA program state is malformed");
            return false;
        }
        for (transition_index = 0u;
             transition_index < state->transition_len;
             transition_index++) {
            const RSDFAV1ProgramTransition *transition =
                &program->transitions[
                    state->transition_begin + transition_index];
            if (!rsdfa_v1_scalar_range_valid(
                    transition->low, transition->high) ||
                transition->target >= program->state_len ||
                (transition_index > 0u &&
                 program->transitions[
                     state->transition_begin + transition_index - 1u]
                         .high >= transition->low)) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "regular-span DFA program transition is malformed");
                return false;
            }
        }
        transition_end += state->transition_len;
        accept_end += state->accept_len;
        eof_accept_end += state->eof_accept_len;
    }
    if (transition_end != program->transition_len ||
        accept_end != program->accept_tag_len ||
        eof_accept_end != program->eof_accept_tag_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "regular-span DFA program has trailing storage");
        return false;
    }
    return true;
}

void rsdfa_v1_ascii_transition_index_init(
    RSDFAV1AsciiTransitionIndex *index) {
    if (index)
        memset(index, 0, sizeof(*index));
}

void rsdfa_v1_ascii_transition_index_free(
    RSDFAV1AsciiTransitionIndex *index) {
    if (!index)
        return;
    free(index->targets);
    memset(index, 0, sizeof(*index));
}

static uint32_t rsdfa_v1_ascii_transition_target(
    const RSDFAV1Program *program,
    uint32_t state_index,
    uint32_t ascii) {
    const RSDFAV1ProgramState *state = &program->states[state_index];
    uint32_t low = 0u;
    uint32_t high = state->transition_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const RSDFAV1ProgramTransition *transition =
            &program->transitions[state->transition_begin + middle];

        if (ascii < transition->low)
            high = middle;
        else if (ascii > transition->high)
            low = middle + 1u;
        else
            return transition->target;
    }
    return RSDFA_V1_NO_STATE;
}

bool rsdfa_v1_ascii_transition_index_validate(
    const RSDFAV1Program *program,
    const RSDFAV1AsciiTransitionIndex *index,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t state;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!rsdfa_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !index || !index->targets || index->state_len != program->state_len) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            rsdfa_v1_set_error(
                error_buf, error_buf_size,
                "bad regular-span DFA ASCII transition index");
        }
        return false;
    }
    for (state = 0u; state < index->state_len; state++) {
        uint32_t ascii;

        for (ascii = 0u; ascii < RSDFA_V1_ASCII_CARDINALITY; ascii++) {
            if (index->targets[
                    (size_t)state * RSDFA_V1_ASCII_CARDINALITY + ascii] !=
                rsdfa_v1_ascii_transition_target(program, state, ascii)) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "regular-span DFA ASCII index disagrees with its transitions");
                return false;
            }
        }
    }
    return true;
}

bool rsdfa_v1_ascii_transition_index_build(
    const RSDFAV1Program *program,
    RSDFAV1AsciiTransitionIndex *out,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1AsciiTransitionIndex result;
    size_t target_len;
    uint32_t state;

    rsdfa_v1_ascii_transition_index_init(&result);
    if (!out || !rsdfa_v1_program_validate(
            program, error_buf, error_buf_size) ||
        (size_t)program->state_len >
            SIZE_MAX / RSDFA_V1_ASCII_CARDINALITY ||
        (target_len =
             (size_t)program->state_len * RSDFA_V1_ASCII_CARDINALITY) >
            SIZE_MAX / sizeof(*result.targets)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            rsdfa_v1_set_error(
                error_buf, error_buf_size,
                "bad regular-span DFA ASCII index build input");
        }
        return false;
    }
    result.targets = malloc(sizeof(*result.targets) * target_len);
    if (!result.targets) {
        rsdfa_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate regular-span DFA ASCII index");
        return false;
    }
    result.state_len = program->state_len;
    for (state = 0u; state < result.state_len; state++) {
        uint32_t ascii;

        for (ascii = 0u; ascii < RSDFA_V1_ASCII_CARDINALITY; ascii++) {
            result.targets[
                (size_t)state * RSDFA_V1_ASCII_CARDINALITY + ascii] =
                rsdfa_v1_ascii_transition_target(program, state, ascii);
        }
    }
    if (!rsdfa_v1_ascii_transition_index_validate(
            program, &result, error_buf, error_buf_size)) {
        rsdfa_v1_ascii_transition_index_free(&result);
        return false;
    }
    rsdfa_v1_ascii_transition_index_free(out);
    *out = result;
    return true;
}

bool rsdfa_v1_plan_export_program(
    const RSDFAV1Plan *plan,
    RSDFAV1Program *out,
    char *error_buf,
    size_t error_buf_size) {
    const RSDFAV1PlanImpl *impl = plan ? plan->impl : NULL;
    RSDFAV1Program result;
    uint32_t state_index;
    uint32_t transition_write = 0u;
    uint32_t accept_write = 0u;
    uint32_t eof_accept_write = 0u;
    bool ok = false;

    rsdfa_v1_program_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || !out || plan->state_len != impl->state_len ||
        plan->state_len == 0u || plan->tag_len == 0u) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad regular-span DFA program export");
        goto done;
    }
    result.state_len = plan->state_len;
    result.transition_len = plan->transition_len;
    result.start_state = impl->start_state;
    result.tag_len = plan->tag_len;
    for (state_index = 0u; state_index < impl->state_len; state_index++) {
        const RSDFAV1State *state = &impl->states[state_index];
        if (state->accept_len > UINT32_MAX - result.accept_tag_len ||
            state->eof_accept_len >
                UINT32_MAX - result.eof_accept_tag_len) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "regular-span DFA program size overflow");
            goto done;
        }
        result.accept_tag_len += state->accept_len;
        result.eof_accept_tag_len += state->eof_accept_len;
    }
    if ((size_t)result.state_len > SIZE_MAX / sizeof(*result.states) ||
        (size_t)result.transition_len >
            SIZE_MAX / sizeof(*result.transitions) ||
        (size_t)result.accept_tag_len >
            SIZE_MAX / sizeof(*result.accept_tags) ||
        (size_t)result.eof_accept_tag_len >
            SIZE_MAX / sizeof(*result.eof_accept_tags)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "regular-span DFA program allocation overflow");
        goto done;
    }
    result.states = calloc(result.state_len, sizeof(*result.states));
    result.transitions = calloc(
        result.transition_len ? result.transition_len : 1u,
        sizeof(*result.transitions));
    result.accept_tags = calloc(
        result.accept_tag_len ? result.accept_tag_len : 1u,
        sizeof(*result.accept_tags));
    result.eof_accept_tags = calloc(
        result.eof_accept_tag_len ? result.eof_accept_tag_len : 1u,
        sizeof(*result.eof_accept_tags));
    if (!result.states || !result.transitions || !result.accept_tags ||
        !result.eof_accept_tags) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate regular-span DFA program");
        goto done;
    }
    for (state_index = 0u; state_index < impl->state_len; state_index++) {
        const RSDFAV1State *source = &impl->states[state_index];
        RSDFAV1ProgramState *target = &result.states[state_index];
        uint32_t transition_index;

        target->transition_begin = transition_write;
        target->transition_len = source->transition_len;
        target->accept_begin = accept_write;
        target->accept_len = source->accept_len;
        target->eof_accept_begin = eof_accept_write;
        target->eof_accept_len = source->eof_accept_len;
        for (transition_index = 0u;
             transition_index < source->transition_len;
             transition_index++) {
            const RSDFAV1Transition *transition =
                &source->transitions[transition_index];
            result.transitions[transition_write++] =
                (RSDFAV1ProgramTransition){
                    .low = transition->low,
                    .high = transition->high,
                    .target = transition->target,
                };
        }
        if (source->accept_len > 0u) {
            memcpy(&result.accept_tags[accept_write], source->accept_tags,
                   sizeof(*result.accept_tags) * source->accept_len);
            accept_write += source->accept_len;
        }
        if (source->eof_accept_len > 0u) {
            memcpy(&result.eof_accept_tags[eof_accept_write],
                   source->eof_accept_tags,
                   sizeof(*result.eof_accept_tags) *
                       source->eof_accept_len);
            eof_accept_write += source->eof_accept_len;
        }
    }
    if (transition_write != result.transition_len ||
        accept_write != result.accept_tag_len ||
        eof_accept_write != result.eof_accept_tag_len ||
        !rsdfa_v1_program_validate(
            &result, error_buf, error_buf_size)) {
        goto done;
    }
    rsdfa_v1_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    rsdfa_v1_program_free(&result);
    return ok;
}

bool rsdfa_v1_plan_export_subsets(
    const RSDFAV1Plan *plan,
    RSDFAV1SubsetTable *out,
    char *error_buf,
    size_t error_buf_size) {
    const RSDFAV1PlanImpl *impl = plan ? plan->impl : NULL;
    RSDFAV1SubsetTable result;
    size_t row_bytes;
    size_t total_words;
    uint32_t state_index;

    rsdfa_v1_subset_table_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || !out || plan->state_len != impl->state_len ||
        plan->state_len == 0u || impl->subset_word_len == 0u) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad DFA subset-table export");
        return false;
    }
    if ((size_t)plan->state_len >
            SIZE_MAX / (size_t)impl->subset_word_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "DFA subset-table size overflow");
        return false;
    }
    total_words =
        (size_t)plan->state_len * (size_t)impl->subset_word_len;
    if (total_words > SIZE_MAX / sizeof(*result.words)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "DFA subset-table allocation overflow");
        return false;
    }
    result.words = calloc(total_words, sizeof(*result.words));
    if (!result.words) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate DFA subset table");
        return false;
    }
    result.state_len = plan->state_len;
    result.word_len = impl->subset_word_len;
    row_bytes = (size_t)result.word_len * sizeof(*result.words);
    for (state_index = 0u; state_index < result.state_len; state_index++) {
        if (!impl->states[state_index].subset) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "DFA state lost its NFA subset");
            rsdfa_v1_subset_table_free(&result);
            return false;
        }
        memcpy(&result.words[(size_t)state_index * result.word_len],
               impl->states[state_index].subset, row_bytes);
    }
    rsdfa_v1_subset_table_free(out);
    *out = result;
    return true;
}

bool rsdfa_v1_plan_export_transition_annotations(
    const RSDFAV1Plan *plan,
    uint32_t **out,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size) {
    const RSDFAV1PlanImpl *impl = plan ? plan->impl : NULL;
    uint32_t *annotations = NULL;
    uint32_t write = 0u;
    uint32_t state_index;

    if (out)
        *out = NULL;
    if (out_len)
        *out_len = 0u;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || !out || !out_len || plan->state_len != impl->state_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad DFA transition-annotation export");
        return false;
    }
    if ((size_t)plan->transition_len > SIZE_MAX / sizeof(*annotations)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "DFA transition-annotation allocation overflow");
        return false;
    }
    if (plan->transition_len > 0u)
        annotations = malloc(
            sizeof(*annotations) * (size_t)plan->transition_len);
    if (plan->transition_len > 0u && !annotations) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate DFA transition annotations");
        return false;
    }
    for (state_index = 0u; state_index < impl->state_len; state_index++) {
        const RSDFAV1State *state = &impl->states[state_index];
        uint32_t transition_index;
        for (transition_index = 0u;
             transition_index < state->transition_len;
             transition_index++) {
            if (write >= plan->transition_len) {
                free(annotations);
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "DFA transition annotations exceed their receipt");
                return false;
            }
            annotations[write++] =
                state->transitions[transition_index].annotation;
        }
    }
    if (write != plan->transition_len) {
        free(annotations);
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "DFA transition annotation count changed");
        return false;
    }
    *out = annotations;
    *out_len = write;
    return true;
}

void rsdfa_v1_tag_language_init(RSDFAV1TagLanguage *language) {
    if (language)
        memset(language, 0, sizeof(*language));
}

void rsdfa_v1_tag_language_free(RSDFAV1TagLanguage *language) {
    if (!language)
        return;
    free(language->first_ranges);
    memset(language, 0, sizeof(*language));
}

static bool rsdfa_v1_state_has_tag(const uint32_t *tags,
                                   uint32_t tag_len,
                                   uint32_t tag) {
    uint32_t low = 0u;
    uint32_t high = tag_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (tag < tags[middle])
            high = middle;
        else if (tag > tags[middle])
            low = middle + 1u;
        else
            return true;
    }
    return false;
}

typedef struct {
    uint32_t left;
    uint32_t right;
    uint32_t previous;
    uint32_t scalar;
} RSDFAV1InclusionPair;

typedef struct {
    RSDFAV1InclusionPair *data;
    uint32_t len;
    uint32_t cap;
    uint32_t *slots;
    uint32_t slot_cap;
} RSDFAV1InclusionPairVec;

void rsdfa_v1_inclusion_result_init(RSDFAV1InclusionResult *result) {
    if (result)
        memset(result, 0, sizeof(*result));
}

void rsdfa_v1_inclusion_result_free(RSDFAV1InclusionResult *result) {
    if (!result)
        return;
    free(result->counterexample);
    memset(result, 0, sizeof(*result));
}

static uint32_t rsdfa_v1_inclusion_hash(uint32_t left,
                                        uint32_t right) {
    uint64_t value = ((uint64_t)left << 32u) | (uint64_t)right;

    value ^= value >> 33u;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33u;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33u;
    return (uint32_t)value;
}

static bool rsdfa_v1_inclusion_pairs_rehash(
    RSDFAV1InclusionPairVec *pairs,
    uint32_t slot_cap) {
    uint32_t *slots;
    uint32_t index;

    if (slot_cap < 16u || (slot_cap & (slot_cap - 1u)) != 0u ||
        (size_t)slot_cap > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = calloc(slot_cap, sizeof(*slots));
    if (!slots)
        return false;
    for (index = 0u; index < pairs->len; index++) {
        const RSDFAV1InclusionPair *pair = &pairs->data[index];
        uint32_t slot = rsdfa_v1_inclusion_hash(
            pair->left, pair->right) & (slot_cap - 1u);
        while (slots[slot] != 0u)
            slot = (slot + 1u) & (slot_cap - 1u);
        slots[slot] = index + 1u;
    }
    free(pairs->slots);
    pairs->slots = slots;
    pairs->slot_cap = slot_cap;
    return true;
}

static bool rsdfa_v1_inclusion_pair_find_or_push(
    RSDFAV1InclusionPairVec *pairs,
    uint32_t pair_limit,
    uint32_t left,
    uint32_t right,
    uint32_t previous,
    uint32_t scalar,
    uint32_t *out_index,
    bool *out_new,
    bool *out_limit) {
    uint32_t slot;

    *out_new = false;
    *out_limit = false;
    if (pairs->slot_cap == 0u &&
        !rsdfa_v1_inclusion_pairs_rehash(pairs, 16u)) {
        return false;
    }
    slot = rsdfa_v1_inclusion_hash(left, right) &
        (pairs->slot_cap - 1u);
    while (pairs->slots[slot] != 0u) {
        uint32_t index = pairs->slots[slot] - 1u;
        const RSDFAV1InclusionPair *pair = &pairs->data[index];
        if (pair->left == left && pair->right == right) {
            *out_index = index;
            return true;
        }
        slot = (slot + 1u) & (pairs->slot_cap - 1u);
    }
    if (pairs->len >= pair_limit) {
        *out_limit = true;
        return true;
    }
    if (pairs->len + 1u >
        pairs->slot_cap - pairs->slot_cap / 4u) {
        if (pairs->slot_cap > UINT32_MAX / 2u ||
            !rsdfa_v1_inclusion_pairs_rehash(
                pairs, pairs->slot_cap * 2u)) {
            return false;
        }
        slot = rsdfa_v1_inclusion_hash(left, right) &
            (pairs->slot_cap - 1u);
        while (pairs->slots[slot] != 0u)
            slot = (slot + 1u) & (pairs->slot_cap - 1u);
    }
    if (!rsdfa_v1_grow(
            (void **)&pairs->data, &pairs->cap,
            pairs->len + 1u, sizeof(*pairs->data))) {
        return false;
    }
    *out_index = pairs->len;
    pairs->data[pairs->len++] = (RSDFAV1InclusionPair){
        .left = left,
        .right = right,
        .previous = previous,
        .scalar = scalar,
    };
    pairs->slots[slot] = *out_index + 1u;
    *out_new = true;
    return true;
}

static bool rsdfa_v1_program_state_accepts(
    const RSDFAV1Program *program,
    uint32_t state_index,
    uint32_t tag) {
    const RSDFAV1ProgramState *state;

    if (state_index == RSDFA_V1_NO_STATE)
        return false;
    state = &program->states[state_index];
    return rsdfa_v1_state_has_tag(
               state->accept_len > 0u
                   ? &program->accept_tags[state->accept_begin] : NULL,
               state->accept_len, tag) ||
           rsdfa_v1_state_has_tag(
               state->eof_accept_len > 0u
                   ? &program->eof_accept_tags[state->eof_accept_begin]
                   : NULL,
               state->eof_accept_len, tag);
}

static void rsdfa_v1_program_transition_segment(
    const RSDFAV1Program *program,
    uint32_t state_index,
    uint32_t scalar,
    uint32_t maximum,
    uint32_t *out_target,
    uint32_t *out_high) {
    const RSDFAV1ProgramState *state;
    uint32_t index;

    if (state_index == RSDFA_V1_NO_STATE) {
        *out_target = RSDFA_V1_NO_STATE;
        *out_high = maximum;
        return;
    }
    state = &program->states[state_index];
    for (index = 0u; index < state->transition_len; index++) {
        const RSDFAV1ProgramTransition *transition =
            &program->transitions[state->transition_begin + index];
        if (transition->high < scalar)
            continue;
        if (transition->low > scalar) {
            *out_target = RSDFA_V1_NO_STATE;
            *out_high = transition->low - 1u < maximum
                ? transition->low - 1u : maximum;
            return;
        }
        *out_target = transition->target;
        *out_high = transition->high < maximum
            ? transition->high : maximum;
        return;
    }
    *out_target = RSDFA_V1_NO_STATE;
    *out_high = maximum;
}

static bool rsdfa_v1_inclusion_counterexample(
    const RSDFAV1InclusionPairVec *pairs,
    uint32_t pair_index,
    RSDFAV1InclusionResult *result) {
    uint32_t length = 0u;
    uint32_t cursor = pair_index;

    while (pairs->data[cursor].previous != RSDFA_V1_NO_STATE) {
        length++;
        cursor = pairs->data[cursor].previous;
    }
    if (length > 0u) {
        result->counterexample = malloc(
            sizeof(*result->counterexample) * (size_t)length);
        if (!result->counterexample)
            return false;
    }
    result->counterexample_len = length;
    cursor = pair_index;
    while (length > 0u) {
        result->counterexample[--length] = pairs->data[cursor].scalar;
        cursor = pairs->data[cursor].previous;
    }
    return true;
}

bool rsdfa_v1_program_tag_inclusion(
    const RSDFAV1Program *left,
    uint32_t left_tag,
    const RSDFAV1Program *right,
    uint32_t right_tag,
    uint32_t pair_limit,
    RSDFAV1InclusionResult *out,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1InclusionPairVec pairs = {0};
    RSDFAV1InclusionResult result;
    uint32_t read = 0u;
    uint32_t ignored_index;
    bool ignored_new;
    bool limit_hit;
    bool ok = false;

    rsdfa_v1_inclusion_result_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || pair_limit == 0u || !left || !right ||
        left_tag >= left->tag_len || right_tag >= right->tag_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad DFA language-inclusion arguments");
        goto done;
    }
    if (!rsdfa_v1_program_validate(
            left, error_buf, error_buf_size) ||
        !rsdfa_v1_program_validate(
            right, error_buf, error_buf_size)) {
        goto done;
    }
    if (!rsdfa_v1_inclusion_pair_find_or_push(
            &pairs, pair_limit,
            left->start_state, right->start_state,
            RSDFA_V1_NO_STATE, 0u,
            &ignored_index, &ignored_new, &limit_hit)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate DFA inclusion product");
        goto done;
    }
    if (limit_hit) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "DFA inclusion limit rejected its start state");
        goto done;
    }
    while (read < pairs.len) {
        const RSDFAV1InclusionPair pair = pairs.data[read];
        const RSDFAV1ProgramState *left_state =
            &left->states[pair.left];
        uint32_t transition_index;

        if (rsdfa_v1_program_state_accepts(
                left, pair.left, left_tag) &&
            !rsdfa_v1_program_state_accepts(
                right, pair.right, right_tag)) {
            result.outcome = RSDFA_V1_INCLUSION_COMPLETED;
            result.included = false;
            result.explored_pair_len = pairs.len;
            if (!rsdfa_v1_inclusion_counterexample(
                    &pairs, read, &result)) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "failed to allocate DFA inclusion counterexample");
                goto done;
            }
            ok = true;
            goto publish;
        }
        for (transition_index = 0u;
             transition_index < left_state->transition_len;
             transition_index++) {
            const RSDFAV1ProgramTransition *transition =
                &left->transitions[
                    left_state->transition_begin + transition_index];
            uint32_t scalar = transition->low;

            for (;;) {
                uint32_t right_target;
                uint32_t segment_high;
                uint32_t pair_index;
                bool is_new;

                rsdfa_v1_program_transition_segment(
                    right, pair.right, scalar, transition->high,
                    &right_target, &segment_high);
                if (!rsdfa_v1_inclusion_pair_find_or_push(
                        &pairs, pair_limit,
                        transition->target, right_target,
                        read, scalar, &pair_index, &is_new,
                        &limit_hit)) {
                    rsdfa_v1_set_error(
                        error_buf, error_buf_size,
                        "failed to grow DFA inclusion product");
                    goto done;
                }
                if (limit_hit) {
                    result.outcome = RSDFA_V1_INCLUSION_PAIR_LIMIT;
                    result.included = false;
                    result.explored_pair_len = pairs.len;
                    ok = true;
                    goto publish;
                }
                (void)pair_index;
                (void)is_new;
                if (segment_high == transition->high)
                    break;
                scalar = segment_high + 1u;
            }
        }
        read++;
    }
    result.outcome = RSDFA_V1_INCLUSION_COMPLETED;
    result.included = true;
    result.explored_pair_len = pairs.len;
    ok = true;

publish:
    rsdfa_v1_inclusion_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));

done:
    free(pairs.data);
    free(pairs.slots);
    rsdfa_v1_inclusion_result_free(&result);
    return ok;
}

typedef struct {
    uint32_t left;
    uint32_t right;
    uint32_t previous;
    uint32_t scalar;
    uint32_t first_left_annotation;
    uint32_t first_right_annotation;
    bool divergent;
} RSDFAV1FunctionalityPair;

typedef struct {
    RSDFAV1FunctionalityPair *data;
    uint32_t len;
    uint32_t cap;
    uint32_t *slots;
    uint32_t slot_cap;
} RSDFAV1FunctionalityPairVec;

typedef struct {
    uint64_t *closures;
    uint32_t *outgoing_offsets;
    uint32_t *outgoing_edges;
    bool *accepts;
    uint32_t closure_word_len;
} RSDFAV1FunctionalityIndex;

void rsdfa_v1_functionality_result_init(
    RSDFAV1FunctionalityResult *result) {
    if (result)
        memset(result, 0, sizeof(*result));
}

void rsdfa_v1_functionality_result_free(
    RSDFAV1FunctionalityResult *result) {
    if (!result)
        return;
    free(result->counterexample);
    memset(result, 0, sizeof(*result));
}

static uint32_t rsdfa_v1_functionality_hash(
    uint32_t left,
    uint32_t right,
    bool divergent) {
    uint32_t hash = rsdfa_v1_inclusion_hash(left, right);
    if (divergent)
        hash ^= UINT32_C(0x9e3779b9);
    hash ^= hash >> 16u;
    hash *= UINT32_C(0x7feb352d);
    hash ^= hash >> 15u;
    return hash;
}

static bool rsdfa_v1_functionality_pairs_rehash(
    RSDFAV1FunctionalityPairVec *pairs,
    uint32_t slot_cap) {
    uint32_t *slots;
    uint32_t index;

    if (slot_cap < 16u || (slot_cap & (slot_cap - 1u)) != 0u ||
        (size_t)slot_cap > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = calloc(slot_cap, sizeof(*slots));
    if (!slots)
        return false;
    for (index = 0u; index < pairs->len; index++) {
        const RSDFAV1FunctionalityPair *pair = &pairs->data[index];
        uint32_t slot = rsdfa_v1_functionality_hash(
            pair->left, pair->right, pair->divergent) & (slot_cap - 1u);
        while (slots[slot] != 0u)
            slot = (slot + 1u) & (slot_cap - 1u);
        slots[slot] = index + 1u;
    }
    free(pairs->slots);
    pairs->slots = slots;
    pairs->slot_cap = slot_cap;
    return true;
}

static bool rsdfa_v1_functionality_pair_find_or_push(
    RSDFAV1FunctionalityPairVec *pairs,
    uint32_t pair_limit,
    uint32_t left,
    uint32_t right,
    bool divergent,
    uint32_t previous,
    uint32_t scalar,
    uint32_t first_left_annotation,
    uint32_t first_right_annotation,
    uint32_t *out_index,
    bool *out_new,
    bool *out_limit) {
    uint32_t slot;

    *out_new = false;
    *out_limit = false;
    if (left > right) {
        uint32_t swap = left;
        left = right;
        right = swap;
    }
    if (first_left_annotation > first_right_annotation) {
        uint32_t swap = first_left_annotation;
        first_left_annotation = first_right_annotation;
        first_right_annotation = swap;
    }
    if (pairs->slot_cap == 0u &&
        !rsdfa_v1_functionality_pairs_rehash(pairs, 16u)) {
        return false;
    }
    slot = rsdfa_v1_functionality_hash(left, right, divergent) &
        (pairs->slot_cap - 1u);
    while (pairs->slots[slot] != 0u) {
        uint32_t index = pairs->slots[slot] - 1u;
        const RSDFAV1FunctionalityPair *pair = &pairs->data[index];
        if (pair->left == left && pair->right == right &&
            pair->divergent == divergent) {
            *out_index = index;
            return true;
        }
        slot = (slot + 1u) & (pairs->slot_cap - 1u);
    }
    if (pairs->len >= pair_limit) {
        *out_limit = true;
        return true;
    }
    if (pairs->len + 1u > pairs->slot_cap - pairs->slot_cap / 4u) {
        if (pairs->slot_cap > UINT32_MAX / 2u ||
            !rsdfa_v1_functionality_pairs_rehash(
                pairs, pairs->slot_cap * 2u)) {
            return false;
        }
        slot = rsdfa_v1_functionality_hash(left, right, divergent) &
            (pairs->slot_cap - 1u);
        while (pairs->slots[slot] != 0u)
            slot = (slot + 1u) & (pairs->slot_cap - 1u);
    }
    if (!rsdfa_v1_grow(
            (void **)&pairs->data, &pairs->cap,
            pairs->len + 1u, sizeof(*pairs->data))) {
        return false;
    }
    *out_index = pairs->len;
    pairs->data[pairs->len++] = (RSDFAV1FunctionalityPair){
        .left = left,
        .right = right,
        .previous = previous,
        .scalar = scalar,
        .first_left_annotation = first_left_annotation,
        .first_right_annotation = first_right_annotation,
        .divergent = divergent,
    };
    pairs->slots[slot] = *out_index + 1u;
    *out_new = true;
    return true;
}

static void rsdfa_v1_functionality_index_free(
    RSDFAV1FunctionalityIndex *index) {
    free(index->closures);
    free(index->outgoing_offsets);
    free(index->outgoing_edges);
    free(index->accepts);
    memset(index, 0, sizeof(*index));
}

static bool rsdfa_v1_functionality_index_build(
    const RSDFAV1Nfa *nfa,
    uint32_t tag,
    RSDFAV1FunctionalityIndex *out,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1FunctionalityIndex index = {0};
    uint32_t *write_offsets = NULL;
    uint32_t *stack = NULL;
    size_t closure_word_total;
    uint32_t edge_index;
    uint32_t state;
    bool ok = false;

    index.closure_word_len =
        nfa->state_len / 64u + (nfa->state_len % 64u != 0u ? 1u : 0u);
    if (index.closure_word_len == 0u ||
        (size_t)nfa->state_len >
            SIZE_MAX / (size_t)index.closure_word_len) {
        goto allocation_failure;
    }
    closure_word_total =
        (size_t)nfa->state_len * (size_t)index.closure_word_len;
    if (closure_word_total > SIZE_MAX / sizeof(*index.closures) ||
        (size_t)nfa->state_len + 1u >
            SIZE_MAX / sizeof(*index.outgoing_offsets) ||
        (size_t)nfa->edge_len > SIZE_MAX / sizeof(*index.outgoing_edges)) {
        goto allocation_failure;
    }
    index.closures = calloc(closure_word_total, sizeof(*index.closures));
    index.outgoing_offsets = calloc(
        (size_t)nfa->state_len + 1u, sizeof(*index.outgoing_offsets));
    index.outgoing_edges = malloc(
        sizeof(*index.outgoing_edges) *
        (size_t)(nfa->edge_len ? nfa->edge_len : 1u));
    index.accepts = calloc(nfa->state_len, sizeof(*index.accepts));
    write_offsets = calloc(
        (size_t)nfa->state_len + 1u, sizeof(*write_offsets));
    stack = malloc(sizeof(*stack) * (size_t)nfa->state_len);
    if (!index.closures || !index.outgoing_offsets ||
        !index.outgoing_edges || !index.accepts ||
        !write_offsets || !stack) {
        goto allocation_failure;
    }
    for (edge_index = 0u; edge_index < nfa->edge_len; edge_index++) {
        if (index.outgoing_offsets[nfa->edges[edge_index].from + 1u] ==
                UINT32_MAX) {
            goto allocation_failure;
        }
        index.outgoing_offsets[nfa->edges[edge_index].from + 1u]++;
    }
    for (state = 0u; state < nfa->state_len; state++) {
        if (UINT32_MAX - index.outgoing_offsets[state] <
            index.outgoing_offsets[state + 1u]) {
            goto allocation_failure;
        }
        index.outgoing_offsets[state + 1u] +=
            index.outgoing_offsets[state];
        write_offsets[state] = index.outgoing_offsets[state];
    }
    write_offsets[nfa->state_len] = index.outgoing_offsets[nfa->state_len];
    for (edge_index = 0u; edge_index < nfa->edge_len; edge_index++) {
        uint32_t from = nfa->edges[edge_index].from;
        index.outgoing_edges[write_offsets[from]++] = edge_index;
    }
    for (state = 0u; state < nfa->state_len; state++) {
        uint64_t *closure = &index.closures[
            (size_t)state * (size_t)index.closure_word_len];
        uint32_t stack_len = 0u;
        rsdfa_v1_bit_set(closure, state);
        stack[stack_len++] = state;
        while (stack_len > 0u) {
            uint32_t current = stack[--stack_len];
            uint32_t begin = index.outgoing_offsets[current];
            uint32_t end = index.outgoing_offsets[current + 1u];
            uint32_t outgoing;
            for (outgoing = begin; outgoing < end; outgoing++) {
                const RSDFAV1NfaEdge *edge =
                    &nfa->edges[index.outgoing_edges[outgoing]];
                if (edge->kind == RSDFA_V1_NFA_EPSILON &&
                    rsdfa_v1_bit_set(closure, edge->to)) {
                    stack[stack_len++] = edge->to;
                }
            }
        }
        for (edge_index = 0u; edge_index < nfa->accept_len; edge_index++) {
            if (nfa->accepts[edge_index].tag == tag &&
                rsdfa_v1_bit_test(
                    closure, nfa->accepts[edge_index].state)) {
                index.accepts[state] = true;
                break;
            }
        }
    }
    *out = index;
    memset(&index, 0, sizeof(index));
    ok = true;
    goto done;

allocation_failure:
    rsdfa_v1_set_error(
        error_buf, error_buf_size,
        "failed to allocate annotated-NFA functionality index");

done:
    free(write_offsets);
    free(stack);
    rsdfa_v1_functionality_index_free(&index);
    return ok;
}

static uint32_t rsdfa_v1_functionality_consuming_edges(
    const RSDFAV1Nfa *nfa,
    const RSDFAV1FunctionalityIndex *index,
    uint32_t state,
    uint32_t *out_edges) {
    const uint64_t *closure = &index->closures[
        (size_t)state * (size_t)index->closure_word_len];
    uint32_t result_len = 0u;
    uint32_t closure_state;

    for (closure_state = 0u; closure_state < nfa->state_len;
         closure_state++) {
        uint32_t outgoing;
        if (!rsdfa_v1_bit_test(closure, closure_state))
            continue;
        for (outgoing = index->outgoing_offsets[closure_state];
             outgoing < index->outgoing_offsets[closure_state + 1u];
             outgoing++) {
            uint32_t edge_index = index->outgoing_edges[outgoing];
            if (nfa->edges[edge_index].kind == RSDFA_V1_NFA_RANGES)
                out_edges[result_len++] = edge_index;
        }
    }
    return result_len;
}

static bool rsdfa_v1_functionality_range_intersection(
    const RSDFAV1NfaEdge *left,
    const RSDFAV1NfaEdge *right,
    uint32_t *out_scalar) {
    uint32_t left_index;
    uint32_t right_index;
    bool found = false;
    uint32_t best = UINT32_MAX;

    for (left_index = 0u; left_index < left->range_len; left_index++) {
        for (right_index = 0u; right_index < right->range_len;
             right_index++) {
            uint32_t low = left->ranges[left_index].low >
                    right->ranges[right_index].low
                ? left->ranges[left_index].low
                : right->ranges[right_index].low;
            uint32_t high = left->ranges[left_index].high <
                    right->ranges[right_index].high
                ? left->ranges[left_index].high
                : right->ranges[right_index].high;
            if (low <= high && (!found || low < best)) {
                best = low;
                found = true;
            }
        }
    }
    if (found)
        *out_scalar = best;
    return found;
}

static bool rsdfa_v1_functionality_counterexample(
    const RSDFAV1FunctionalityPairVec *pairs,
    uint32_t pair_index,
    RSDFAV1FunctionalityResult *result) {
    uint32_t length = 0u;
    uint32_t cursor = pair_index;

    while (pairs->data[cursor].previous != RSDFA_V1_NO_STATE) {
        length++;
        cursor = pairs->data[cursor].previous;
    }
    if (length > 0u) {
        result->counterexample = malloc(
            sizeof(*result->counterexample) * (size_t)length);
        if (!result->counterexample)
            return false;
    }
    result->counterexample_len = length;
    cursor = pair_index;
    while (length > 0u) {
        result->counterexample[--length] = pairs->data[cursor].scalar;
        cursor = pairs->data[cursor].previous;
    }
    return true;
}

bool rsdfa_v1_annotated_nfa_functionality(
    const RSDFAV1Nfa *nfa,
    const uint32_t *edge_annotations,
    uint32_t no_annotation,
    uint32_t tag,
    uint32_t pair_limit,
    uint64_t work_limit,
    RSDFAV1FunctionalityResult *out,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1FunctionalityIndex index = {0};
    RSDFAV1FunctionalityPairVec pairs = {0};
    RSDFAV1FunctionalityResult result;
    uint32_t *left_edges = NULL;
    uint32_t *right_edges = NULL;
    uint32_t read = 0u;
    uint32_t start_left;
    bool ok = false;

    rsdfa_v1_functionality_result_init(&result);
    result.first_left_annotation = no_annotation;
    result.first_right_annotation = no_annotation;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || !nfa || !edge_annotations || pair_limit == 0u ||
        work_limit == 0u || tag >= nfa->tag_len ||
        !rsdfa_v1_validate_nfa(nfa, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            rsdfa_v1_set_error(
                error_buf, error_buf_size,
                "bad annotated-NFA functionality arguments");
        }
        goto done;
    }
    for (start_left = 0u; start_left < nfa->edge_len; start_left++) {
        bool consuming =
            nfa->edges[start_left].kind == RSDFA_V1_NFA_RANGES;
        if (nfa->edges[start_left].kind == RSDFA_V1_NFA_EOF) {
            rsdfa_v1_set_error(
                error_buf, error_buf_size,
                "annotated-NFA functionality v1 does not admit EOF edges");
            goto done;
        }
        if ((consuming && edge_annotations[start_left] == no_annotation) ||
            (!consuming && edge_annotations[start_left] != no_annotation)) {
            rsdfa_v1_set_error(
                error_buf, error_buf_size,
                "NFA edge annotation disagrees with edge width");
            goto done;
        }
    }
    if (!rsdfa_v1_functionality_index_build(
            nfa, tag, &index, error_buf, error_buf_size) ||
        (size_t)nfa->edge_len > SIZE_MAX / sizeof(*left_edges)) {
        goto done;
    }
    left_edges = malloc(
        sizeof(*left_edges) * (size_t)(nfa->edge_len ? nfa->edge_len : 1u));
    right_edges = malloc(
        sizeof(*right_edges) * (size_t)(nfa->edge_len ? nfa->edge_len : 1u));
    if (!left_edges || !right_edges) {
        rsdfa_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate annotated-NFA functionality worklist");
        goto done;
    }
    for (start_left = 0u; start_left < nfa->start_len; start_left++) {
        uint32_t start_right;
        for (start_right = start_left;
             start_right < nfa->start_len; start_right++) {
            uint32_t ignored_index;
            bool ignored_new;
            bool limit_hit;
            if (!rsdfa_v1_functionality_pair_find_or_push(
                    &pairs, pair_limit,
                    nfa->start_states[start_left],
                    nfa->start_states[start_right], false,
                    RSDFA_V1_NO_STATE, 0u,
                    no_annotation, no_annotation,
                    &ignored_index, &ignored_new, &limit_hit)) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "failed to allocate annotated-NFA functionality product");
                goto done;
            }
            if (limit_hit) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "functionality pair limit rejected a start pair");
                goto done;
            }
        }
    }
    while (read < pairs.len) {
        const RSDFAV1FunctionalityPair pair = pairs.data[read];
        uint32_t left_len;
        uint32_t right_len;
        uint32_t left_index;

        if (pair.divergent && index.accepts[pair.left] &&
            index.accepts[pair.right]) {
            result.outcome = RSDFA_V1_FUNCTIONALITY_COMPLETED;
            result.functional = false;
            result.first_left_annotation = pair.first_left_annotation;
            result.first_right_annotation = pair.first_right_annotation;
            result.explored_pair_len = pairs.len;
            if (!rsdfa_v1_functionality_counterexample(
                    &pairs, read, &result)) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "failed to allocate functionality counterexample");
                goto done;
            }
            ok = true;
            goto publish;
        }
        left_len = rsdfa_v1_functionality_consuming_edges(
            nfa, &index, pair.left, left_edges);
        right_len = rsdfa_v1_functionality_consuming_edges(
            nfa, &index, pair.right, right_edges);
        for (left_index = 0u; left_index < left_len; left_index++) {
            uint32_t right_index;
            uint32_t left_edge_index = left_edges[left_index];
            const RSDFAV1NfaEdge *left_edge =
                &nfa->edges[left_edge_index];
            for (right_index = 0u; right_index < right_len;
                 right_index++) {
                uint32_t right_edge_index = right_edges[right_index];
                const RSDFAV1NfaEdge *right_edge =
                    &nfa->edges[right_edge_index];
                uint32_t scalar;
                uint32_t pair_index;
                uint32_t first_left = pair.first_left_annotation;
                uint32_t first_right = pair.first_right_annotation;
                bool divergent = pair.divergent;
                bool is_new;
                bool limit_hit;

                if (result.work_item_len >= work_limit) {
                    result.outcome = RSDFA_V1_FUNCTIONALITY_WORK_LIMIT;
                    result.functional = false;
                    result.explored_pair_len = pairs.len;
                    ok = true;
                    goto publish;
                }
                result.work_item_len++;
                if (!rsdfa_v1_functionality_range_intersection(
                        left_edge, right_edge, &scalar)) {
                    continue;
                }
                if (!divergent &&
                    edge_annotations[left_edge_index] !=
                        edge_annotations[right_edge_index]) {
                    divergent = true;
                    first_left = edge_annotations[left_edge_index];
                    first_right = edge_annotations[right_edge_index];
                }
                if (!rsdfa_v1_functionality_pair_find_or_push(
                        &pairs, pair_limit,
                        left_edge->to, right_edge->to, divergent,
                        read, scalar, first_left, first_right,
                        &pair_index, &is_new, &limit_hit)) {
                    rsdfa_v1_set_error(
                        error_buf, error_buf_size,
                        "failed to grow annotated-NFA functionality product");
                    goto done;
                }
                if (limit_hit) {
                    result.outcome = RSDFA_V1_FUNCTIONALITY_PAIR_LIMIT;
                    result.functional = false;
                    result.explored_pair_len = pairs.len;
                    ok = true;
                    goto publish;
                }
                (void)pair_index;
                (void)is_new;
            }
        }
        read++;
    }
    result.outcome = RSDFA_V1_FUNCTIONALITY_COMPLETED;
    result.functional = true;
    result.explored_pair_len = pairs.len;
    ok = true;

publish:
    rsdfa_v1_functionality_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));

done:
    free(left_edges);
    free(right_edges);
    free(pairs.data);
    free(pairs.slots);
    rsdfa_v1_functionality_index_free(&index);
    rsdfa_v1_functionality_result_free(&result);
    return ok;
}

static bool rsdfa_v1_tag_can_accept(const RSDFAV1PlanImpl *impl,
                                    uint32_t tag,
                                    bool *can_accept) {
    bool changed = true;
    uint32_t state_index;

    for (state_index = 0u; state_index < impl->state_len; state_index++) {
        const RSDFAV1State *state = &impl->states[state_index];
        can_accept[state_index] =
            rsdfa_v1_state_has_tag(
                state->accept_tags, state->accept_len, tag) ||
            rsdfa_v1_state_has_tag(
                state->eof_accept_tags, state->eof_accept_len, tag);
    }
    while (changed) {
        changed = false;
        for (state_index = 0u;
             state_index < impl->state_len; state_index++) {
            const RSDFAV1State *state;
            uint32_t transition_index;
            if (can_accept[state_index])
                continue;
            state = &impl->states[state_index];
            for (transition_index = 0u;
                 transition_index < state->transition_len;
                 transition_index++) {
                if (can_accept[state->transitions[
                        transition_index].target]) {
                    can_accept[state_index] = true;
                    changed = true;
                    break;
                }
            }
        }
    }
    return can_accept[impl->start_state];
}

static bool rsdfa_v1_tag_language_first_ranges(
    const RSDFAV1PlanImpl *impl,
    const bool *can_accept,
    RSDFAV1TagLanguage *language) {
    const RSDFAV1State *start = &impl->states[impl->start_state];
    CettaLpNativeUnicodeRange *ranges = NULL;
    uint32_t range_len = 0u;
    uint32_t transition_index;

    if (start->transition_len > 0u) {
        ranges = malloc(
            sizeof(*ranges) * (size_t)start->transition_len);
        if (!ranges)
            return false;
    }
    for (transition_index = 0u;
         transition_index < start->transition_len; transition_index++) {
        const RSDFAV1Transition *transition =
            &start->transitions[transition_index];
        if (!can_accept[transition->target])
            continue;
        if (range_len > 0u &&
            ranges[range_len - 1u].high + 1u == transition->low) {
            ranges[range_len - 1u].high = transition->high;
        } else {
            ranges[range_len++] = (CettaLpNativeUnicodeRange){
                transition->low, transition->high,
            };
        }
    }
    if (range_len == 0u) {
        free(ranges);
        ranges = NULL;
    }
    language->first_ranges = ranges;
    language->first_range_len = range_len;
    return true;
}

static bool rsdfa_v1_tag_prefix_free(const RSDFAV1PlanImpl *impl,
                                     uint32_t tag,
                                     const bool *can_accept) {
    uint32_t state_index;

    for (state_index = 0u; state_index < impl->state_len; state_index++) {
        const RSDFAV1State *state = &impl->states[state_index];
        uint32_t transition_index;
        if (!rsdfa_v1_state_has_tag(
                state->accept_tags, state->accept_len, tag)) {
            continue;
        }
        for (transition_index = 0u;
             transition_index < state->transition_len;
             transition_index++) {
            if (can_accept[state->transitions[
                    transition_index].target]) {
                return false;
            }
        }
    }
    return true;
}

static bool rsdfa_v1_tag_one_scalar_or_eof(
    const RSDFAV1PlanImpl *impl,
    uint32_t tag,
    bool *reachable_one,
    bool *reachable_two) {
    const RSDFAV1State *start = &impl->states[impl->start_state];
    bool changed = true;
    bool has_match = false;
    uint32_t transition_index;
    uint32_t state_index;

    for (transition_index = 0u;
         transition_index < start->transition_len; transition_index++) {
        reachable_one[start->transitions[transition_index].target] = true;
    }
    for (state_index = 0u; state_index < impl->state_len; state_index++) {
        const RSDFAV1State *state;
        if (!reachable_one[state_index])
            continue;
        state = &impl->states[state_index];
        for (transition_index = 0u;
             transition_index < state->transition_len;
             transition_index++) {
            reachable_two[state->transitions[transition_index].target] = true;
        }
    }
    while (changed) {
        changed = false;
        for (state_index = 0u;
             state_index < impl->state_len; state_index++) {
            const RSDFAV1State *state;
            if (!reachable_two[state_index])
                continue;
            state = &impl->states[state_index];
            for (transition_index = 0u;
                 transition_index < state->transition_len;
                 transition_index++) {
                uint32_t target = state->transitions[
                    transition_index].target;
                if (!reachable_two[target]) {
                    reachable_two[target] = true;
                    changed = true;
                }
            }
        }
    }

    for (state_index = 0u; state_index < impl->state_len; state_index++) {
        const RSDFAV1State *state = &impl->states[state_index];
        bool ordinary = rsdfa_v1_state_has_tag(
            state->accept_tags, state->accept_len, tag);
        bool at_eof = rsdfa_v1_state_has_tag(
            state->eof_accept_tags, state->eof_accept_len, tag);
        if (ordinary) {
            has_match = true;
            if (state_index == impl->start_state ||
                !reachable_one[state_index] ||
                reachable_two[state_index]) {
                return false;
            }
        }
        /* EOF closure retains ordinary accepts.  Only an EOF membership that
         * is absent from the ordinary set denotes an EOF-required match. */
        if (at_eof && !ordinary) {
            has_match = true;
            if (state_index != impl->start_state)
                return false;
        }
    }
    return has_match;
}

bool rsdfa_v1_plan_tag_language(
    const RSDFAV1Plan *plan,
    uint32_t tag,
    RSDFAV1TagLanguage *out,
    char *error_buf,
    size_t error_buf_size) {
    const RSDFAV1PlanImpl *impl = plan ? plan->impl : NULL;
    RSDFAV1TagLanguage result;
    bool *can_accept = NULL;
    bool *reachable_one = NULL;
    bool *reachable_two = NULL;
    const RSDFAV1State *start;
    bool ok = false;

    rsdfa_v1_tag_language_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || !out || tag >= plan->tag_len ||
        impl->state_len == 0u || impl->state_len != plan->state_len ||
        impl->start_state >= impl->state_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad DFA tag-language arguments");
        goto done;
    }
    can_accept = calloc(impl->state_len, sizeof(*can_accept));
    reachable_one = calloc(impl->state_len, sizeof(*reachable_one));
    reachable_two = calloc(impl->state_len, sizeof(*reachable_two));
    if (!can_accept || !reachable_one || !reachable_two) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate DFA tag-language analysis");
        goto done;
    }
    if (!rsdfa_v1_tag_can_accept(impl, tag, can_accept)) {
        result.is_empty = true;
        rsdfa_v1_tag_language_free(out);
        *out = result;
        memset(&result, 0, sizeof(result));
        ok = true;
        goto done;
    }
    start = &impl->states[impl->start_state];
    result.accepts_empty = rsdfa_v1_state_has_tag(
        start->accept_tags, start->accept_len, tag);
    result.accepts_eof_empty = !result.accepts_empty &&
        rsdfa_v1_state_has_tag(
            start->eof_accept_tags, start->eof_accept_len, tag);
    result.prefix_free = rsdfa_v1_tag_prefix_free(
        impl, tag, can_accept);
    result.one_scalar_or_eof = rsdfa_v1_tag_one_scalar_or_eof(
        impl, tag, reachable_one, reachable_two);
    if (!rsdfa_v1_tag_language_first_ranges(
            impl, can_accept, &result)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate DFA first-scalar ranges");
        goto done;
    }
    rsdfa_v1_tag_language_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    free(can_accept);
    free(reachable_one);
    free(reachable_two);
    rsdfa_v1_tag_language_free(&result);
    return ok;
}

bool rsdfa_v1_plan_tags_prefix_overlap(
    const RSDFAV1Plan *plan,
    uint32_t left_tag,
    uint32_t right_tag,
    bool *out,
    char *error_buf,
    size_t error_buf_size) {
    const RSDFAV1PlanImpl *impl = plan ? plan->impl : NULL;
    bool *left_can_accept = NULL;
    bool *right_can_accept = NULL;
    uint32_t state_index;
    bool overlap = false;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || !out || left_tag >= plan->tag_len ||
        right_tag >= plan->tag_len || left_tag == right_tag ||
        impl->state_len == 0u || impl->state_len != plan->state_len ||
        impl->start_state >= impl->state_len) {
        rsdfa_v1_set_error(
            error_buf, error_buf_size,
            "bad DFA cross-tag prefix-overlap arguments");
        goto done;
    }
    left_can_accept = calloc(
        impl->state_len, sizeof(*left_can_accept));
    right_can_accept = calloc(
        impl->state_len, sizeof(*right_can_accept));
    if (!left_can_accept || !right_can_accept) {
        rsdfa_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate DFA cross-tag prefix analysis");
        goto done;
    }
    if (!rsdfa_v1_tag_can_accept(
            impl, left_tag, left_can_accept) ||
        !rsdfa_v1_tag_can_accept(
            impl, right_tag, right_can_accept)) {
        *out = false;
        ok = true;
        goto done;
    }
    for (state_index = 0u;
         state_index < impl->state_len; state_index++) {
        const RSDFAV1State *state = &impl->states[state_index];
        bool left_ordinary = rsdfa_v1_state_has_tag(
            state->accept_tags, state->accept_len, left_tag);
        bool right_ordinary = rsdfa_v1_state_has_tag(
            state->accept_tags, state->accept_len, right_tag);
        bool left_eof = rsdfa_v1_state_has_tag(
            state->eof_accept_tags, state->eof_accept_len, left_tag);
        bool right_eof = rsdfa_v1_state_has_tag(
            state->eof_accept_tags, state->eof_accept_len, right_tag);

        if ((left_ordinary && right_can_accept[state_index]) ||
            (right_ordinary && left_can_accept[state_index]) ||
            (left_eof && right_eof)) {
            overlap = true;
            break;
        }
    }
    *out = overlap;
    ok = true;

done:
    free(right_can_accept);
    free(left_can_accept);
    return ok;
}

static bool rsdfa_v1_ranges_overlap(
    uint32_t left_low,
    uint32_t left_high,
    const CettaLpNativeUnicodeRange *right,
    uint32_t right_len) {
    uint32_t index;
    for (index = 0u; index < right_len; index++) {
        if (left_low <= right[index].high &&
            right[index].low <= left_high) {
            return true;
        }
    }
    return false;
}

bool rsdfa_v1_plan_tag_stops_before(
    const RSDFAV1Plan *plan,
    uint32_t tag,
    const CettaLpNativeUnicodeRange *boundary_ranges,
    uint32_t boundary_range_len,
    bool *out,
    char *error_buf,
    size_t error_buf_size) {
    const RSDFAV1PlanImpl *impl = plan ? plan->impl : NULL;
    bool *can_accept = NULL;
    uint32_t state_index;
    uint32_t range_index;
    bool result = true;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || !out || tag >= plan->tag_len ||
        (boundary_range_len > 0u && !boundary_ranges) ||
        impl->state_len == 0u || impl->state_len != plan->state_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad DFA boundary-stop arguments");
        return false;
    }
    for (range_index = 0u;
         range_index < boundary_range_len; range_index++) {
        if (!rsdfa_v1_scalar_range_valid(
                boundary_ranges[range_index].low,
                boundary_ranges[range_index].high)) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "DFA boundary set contains an invalid range");
            return false;
        }
    }
    can_accept = calloc(impl->state_len, sizeof(*can_accept));
    if (!can_accept) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate DFA boundary-stop analysis");
        return false;
    }
    if (!rsdfa_v1_tag_can_accept(impl, tag, can_accept)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "DFA boundary-stop tag language is empty");
        goto done;
    }
    for (state_index = 0u; state_index < impl->state_len; state_index++) {
        const RSDFAV1State *state = &impl->states[state_index];
        uint32_t transition_index;
        if (!rsdfa_v1_state_has_tag(
                state->accept_tags, state->accept_len, tag)) {
            continue;
        }
        for (transition_index = 0u;
             transition_index < state->transition_len;
             transition_index++) {
            const RSDFAV1Transition *transition =
                &state->transitions[transition_index];
            if (can_accept[transition->target] &&
                rsdfa_v1_ranges_overlap(
                    transition->low, transition->high,
                    boundary_ranges, boundary_range_len)) {
                result = false;
                goto finished;
            }
        }
    }

finished:
    *out = result;
    ok = true;

done:
    free(can_accept);
    return ok;
}

static int32_t rsdfa_v1_state_find(const RSDFAV1PlanImpl *plan,
                                   const uint64_t *bits) {
    uint32_t index;
    size_t bytes = (size_t)plan->subset_word_len * sizeof(*bits);

    for (index = 0u; index < plan->state_len; index++) {
        if (memcmp(plan->states[index].subset, bits, bytes) == 0)
            return (int32_t)index;
    }
    return -1;
}

static uint32_t rsdfa_v1_state_get(RSDFAV1Build *build,
                                   const uint64_t *bits) {
    RSDFAV1PlanImpl *plan = build->plan;
    int32_t existing = rsdfa_v1_state_find(plan, bits);
    RSDFAV1State *state;
    size_t bytes;

    if (existing >= 0)
        return (uint32_t)existing;
    if (plan->state_len >= build->state_limit) {
        build->state_limit_hit = true;
        return RSDFA_V1_NO_STATE;
    }
    if (!rsdfa_v1_grow(
            (void **)&plan->states, &plan->state_cap,
            plan->state_len + 1u, sizeof(*plan->states))) {
        build->failed = true;
        rsdfa_v1_set_error(build->error_buf, build->error_buf_size,
                           "failed to allocate DFA states");
        return RSDFA_V1_NO_STATE;
    }
    state = &plan->states[plan->state_len];
    memset(state, 0, sizeof(*state));
    bytes = (size_t)plan->subset_word_len * sizeof(*bits);
    state->subset = malloc(bytes);
    if (!state->subset ||
        !rsdfa_v1_collect_accepts(
            build->nfa, bits, &state->accept_tags, &state->accept_len)) {
        free(state->subset);
        free(state->accept_tags);
        memset(state, 0, sizeof(*state));
        build->failed = true;
        rsdfa_v1_set_error(build->error_buf, build->error_buf_size,
                           "failed to allocate DFA state payload");
        return RSDFA_V1_NO_STATE;
    }
    memcpy(state->subset, bits, bytes);
    return plan->state_len++;
}

static bool rsdfa_v1_segment_push(RSDFAV1SegmentVec *segments,
                                  uint32_t low,
                                  uint32_t high) {
    if (!rsdfa_v1_grow(
            (void **)&segments->data, &segments->cap,
            segments->len + 1u, sizeof(*segments->data))) {
        return false;
    }
    segments->data[segments->len++] =
        (RSDFAV1Segment){.low = low, .high = high};
    return true;
}

static bool rsdfa_v1_build_segments(const RSDFAV1Nfa *nfa,
                                    RSDFAV1SegmentVec *segments,
                                    char *error_buf,
                                    size_t error_buf_size) {
    uint32_t *boundaries = NULL;
    uint32_t boundary_len = 0u;
    uint32_t boundary_cap = 0u;
    uint32_t edge_index;
    uint32_t index;
    uint32_t write;

    for (edge_index = 0u; edge_index < nfa->edge_len; edge_index++) {
        const RSDFAV1NfaEdge *edge = &nfa->edges[edge_index];
        uint32_t range_index;
        if (edge->kind != RSDFA_V1_NFA_RANGES)
            continue;
        for (range_index = 0u;
             range_index < edge->range_len; range_index++) {
            uint32_t low = edge->ranges[range_index].low;
            uint32_t after = edge->ranges[range_index].high + 1u;
            if (!rsdfa_v1_grow(
                    (void **)&boundaries, &boundary_cap,
                    boundary_len + 2u, sizeof(*boundaries))) {
                free(boundaries);
                rsdfa_v1_set_error(error_buf, error_buf_size,
                                   "failed to allocate Unicode partition");
                return false;
            }
            boundaries[boundary_len++] = low;
            boundaries[boundary_len++] = after;
        }
    }
    if (boundary_len == 0u)
        return true;
    qsort(boundaries, boundary_len, sizeof(*boundaries),
          rsdfa_v1_u32_compare);
    write = 0u;
    for (index = 0u; index < boundary_len; index++) {
        if (write == 0u || boundaries[index] != boundaries[write - 1u])
            boundaries[write++] = boundaries[index];
    }
    boundary_len = write;
    for (index = 0u; index + 1u < boundary_len; index++) {
        uint32_t low = boundaries[index];
        uint32_t high = boundaries[index + 1u] - 1u;
        if (low > high || low > UINT32_C(0x10ffff))
            continue;
        if (high > UINT32_C(0x10ffff))
            high = UINT32_C(0x10ffff);
        if (low <= UINT32_C(0xd7ff) && high >= UINT32_C(0xd800))
            high = UINT32_C(0xd7ff);
        if (low >= UINT32_C(0xd800) && low <= UINT32_C(0xdfff))
            low = UINT32_C(0xe000);
        if (low <= high &&
            !rsdfa_v1_segment_push(segments, low, high)) {
            free(boundaries);
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "failed to allocate Unicode segments");
            return false;
        }
    }
    free(boundaries);
    return true;
}

static bool rsdfa_v1_edge_matches(const RSDFAV1NfaEdge *edge,
                                  uint32_t scalar) {
    uint32_t index;
    for (index = 0u; index < edge->range_len; index++) {
        if (scalar >= edge->ranges[index].low &&
            scalar <= edge->ranges[index].high) {
            return true;
        }
    }
    return false;
}

static bool rsdfa_v1_transition_push(RSDFAV1Build *build,
                                     RSDFAV1State *state,
                                     uint32_t low,
                                     uint32_t high,
                                     uint32_t target,
                                     uint32_t annotation) {
    RSDFAV1Transition *previous;

    if (state->transition_len > 0u) {
        previous = &state->transitions[state->transition_len - 1u];
        if (previous->target == target &&
            previous->annotation == annotation &&
            previous->high != UINT32_MAX &&
            previous->high + 1u == low) {
            previous->high = high;
            return true;
        }
    }
    if (build->transition_len >= build->transition_limit) {
        build->transition_limit_hit = true;
        return false;
    }
    if (!rsdfa_v1_grow(
            (void **)&state->transitions, &state->transition_cap,
            state->transition_len + 1u,
            sizeof(*state->transitions))) {
        build->failed = true;
        rsdfa_v1_set_error(build->error_buf, build->error_buf_size,
                           "failed to allocate DFA transitions");
        return false;
    }
    state->transitions[state->transition_len++] =
        (RSDFAV1Transition){
            .low = low,
            .high = high,
            .target = target,
            .annotation = annotation,
        };
    build->transition_len++;
    return true;
}

static bool rsdfa_v1_plan_build_impl(
    const RSDFAV1Nfa *nfa,
    const uint32_t *edge_annotations,
    uint32_t no_annotation,
    uint32_t state_limit,
    uint32_t transition_limit,
    RSDFAV1Plan *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1PlanImpl *plan = NULL;
    RSDFAV1SegmentVec segments = {0};
    RSDFAV1Build build;
    uint64_t *initial = NULL;
    uint64_t *next = NULL;
    uint32_t state_index;
    bool ok = false;

    if (!out || !outcome || state_limit == 0u || transition_limit == 0u ||
        !rsdfa_v1_validate_nfa(nfa, error_buf, error_buf_size)) {
        return false;
    }
    if (edge_annotations) {
        for (state_index = 0u; state_index < nfa->edge_len; state_index++) {
            bool consuming =
                nfa->edges[state_index].kind == RSDFA_V1_NFA_RANGES;
            if ((consuming &&
                 edge_annotations[state_index] == no_annotation) ||
                (!consuming &&
                 edge_annotations[state_index] != no_annotation)) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "NFA edge annotation disagrees with edge width");
                return false;
            }
        }
    }
    rsdfa_v1_plan_free(out);
    *outcome = RSDFA_V1_BUILD_COMPLETED;
    plan = calloc(1u, sizeof(*plan));
    if (!plan) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate DFA plan");
        return false;
    }
    plan->subset_word_len = (nfa->state_len + 63u) / 64u;
    memset(&build, 0, sizeof(build));
    build.nfa = nfa;
    build.plan = plan;
    build.state_limit = state_limit;
    build.transition_limit = transition_limit;
    build.error_buf = error_buf;
    build.error_buf_size = error_buf_size;
    initial = calloc(plan->subset_word_len, sizeof(*initial));
    next = calloc(plan->subset_word_len, sizeof(*next));
    if (!initial || !next ||
        !rsdfa_v1_build_segments(
            nfa, &segments, error_buf, error_buf_size)) {
        if ((!initial || !next) && error_buf && error_buf_size > 0u)
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "failed to allocate DFA subset");
        goto done;
    }
    for (state_index = 0u; state_index < nfa->start_len; state_index++)
        (void)rsdfa_v1_bit_set(initial, nfa->start_states[state_index]);
    rsdfa_v1_zero_closure(nfa, initial, false);
    plan->start_state = rsdfa_v1_state_get(&build, initial);
    if (plan->start_state == RSDFA_V1_NO_STATE)
        goto resource_or_error;

    for (state_index = 0u; state_index < plan->state_len; state_index++) {
        uint32_t segment_index;
        for (segment_index = 0u;
             segment_index < segments.len; segment_index++) {
            uint32_t edge_index;
            uint32_t target;
            uint32_t annotation = no_annotation;
            uint32_t annotation_edge_index = UINT32_MAX;
            RSDFAV1State *state;
            bool have_annotation = false;

            memset(next, 0,
                   (size_t)plan->subset_word_len * sizeof(*next));
            for (edge_index = 0u; edge_index < nfa->edge_len; edge_index++) {
                const RSDFAV1NfaEdge *edge = &nfa->edges[edge_index];
                if (edge->kind == RSDFA_V1_NFA_RANGES &&
                    rsdfa_v1_bit_test(
                        plan->states[state_index].subset, edge->from) &&
                    rsdfa_v1_edge_matches(
                        edge, segments.data[segment_index].low)) {
                    uint32_t edge_annotation = edge_annotations
                        ? edge_annotations[edge_index] : no_annotation;
                    if (have_annotation && annotation != edge_annotation) {
                        build.annotation_conflict = true;
                        rsdfa_v1_set_error(
                            error_buf, error_buf_size,
                            "nonlocal NFA annotations at DFA state %u, "
                            "scalar U+%04X: edge %u has %u, edge %u has %u",
                            state_index,
                            segments.data[segment_index].low,
                            annotation_edge_index, annotation,
                            edge_index, edge_annotation);
                        goto resource_or_error;
                    }
                    annotation = edge_annotation;
                    annotation_edge_index = edge_index;
                    have_annotation = true;
                    (void)rsdfa_v1_bit_set(next, edge->to);
                }
            }
            if (rsdfa_v1_bits_empty(next, plan->subset_word_len))
                continue;
            rsdfa_v1_zero_closure(nfa, next, false);
            target = rsdfa_v1_state_get(&build, next);
            if (target == RSDFA_V1_NO_STATE)
                goto resource_or_error;
            state = &plan->states[state_index];
            if (!rsdfa_v1_transition_push(
                    &build, state,
                    segments.data[segment_index].low,
                    segments.data[segment_index].high,
                    target, annotation)) {
                goto resource_or_error;
            }
        }
    }

    for (state_index = 0u; state_index < plan->state_len; state_index++) {
        RSDFAV1State *state = &plan->states[state_index];
        memcpy(next, state->subset,
               (size_t)plan->subset_word_len * sizeof(*next));
        rsdfa_v1_zero_closure(nfa, next, true);
        if (!rsdfa_v1_collect_accepts(
                nfa, next,
                &state->eof_accept_tags,
                &state->eof_accept_len)) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "failed to allocate EOF accept tags");
            goto done;
        }
    }

    out->impl = plan;
    out->state_len = plan->state_len;
    out->transition_len = build.transition_len;
    out->tag_len = nfa->tag_len;
    plan = NULL;
    ok = true;
    goto done;

resource_or_error:
    if (build.failed)
        goto done;
    if (build.annotation_conflict) {
        *outcome = RSDFA_V1_BUILD_ANNOTATION_CONFLICT;
        ok = true;
        goto done;
    }
    if (build.state_limit_hit) {
        *outcome = RSDFA_V1_BUILD_STATE_LIMIT;
        ok = true;
        goto done;
    }
    if (build.transition_limit_hit) {
        *outcome = RSDFA_V1_BUILD_TRANSITION_LIMIT;
        ok = true;
        goto done;
    }
    rsdfa_v1_set_error(error_buf, error_buf_size,
                       "DFA construction stopped unexpectedly");

done:
    free(initial);
    free(next);
    free(segments.data);
    rsdfa_v1_impl_free(plan);
    return ok;
}

bool rsdfa_v1_plan_build(const RSDFAV1Nfa *nfa,
                         uint32_t state_limit,
                         uint32_t transition_limit,
                         RSDFAV1Plan *out,
                         RSDFAV1BuildOutcome *outcome,
                         char *error_buf,
                         size_t error_buf_size) {
    return rsdfa_v1_plan_build_impl(
        nfa, NULL, UINT32_MAX, state_limit, transition_limit,
        out, outcome, error_buf, error_buf_size);
}

bool rsdfa_v1_plan_build_annotated(
    const RSDFAV1Nfa *nfa,
    const uint32_t *edge_annotations,
    uint32_t no_annotation,
    uint32_t state_limit,
    uint32_t transition_limit,
    RSDFAV1Plan *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    if (!edge_annotations) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "annotated DFA build lacks edge annotations");
        return false;
    }
    return rsdfa_v1_plan_build_impl(
        nfa, edge_annotations, no_annotation,
        state_limit, transition_limit,
        out, outcome, error_buf, error_buf_size);
}

void rsdfa_v1_scan_result_init(RSDFAV1ScanResult *result) {
    if (result)
        memset(result, 0, sizeof(*result));
}

void rsdfa_v1_scan_result_free(RSDFAV1ScanResult *result) {
    if (!result)
        return;
    free(result->tokens);
    free(result->codepoints);
    free(result->byte_offsets);
    memset(result, 0, sizeof(*result));
}

static bool rsdfa_v1_decode_one(const uint8_t *bytes,
                                size_t len,
                                size_t position,
                                uint32_t *scalar,
                                uint32_t *width) {
    uint8_t first;
    uint32_t value;
    uint32_t count;
    uint32_t minimum;
    uint32_t index;

    if (position >= len)
        return false;
    first = bytes[position];
    if (first <= UINT8_C(0x7f)) {
        *scalar = first;
        *width = 1u;
        return true;
    }
    if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
        value = first & UINT8_C(0x1f);
        count = 2u;
        minimum = UINT32_C(0x80);
    } else if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
        value = first & UINT8_C(0x0f);
        count = 3u;
        minimum = UINT32_C(0x800);
    } else if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
        value = first & UINT8_C(0x07);
        count = 4u;
        minimum = UINT32_C(0x10000);
    } else {
        return false;
    }
    if ((size_t)count > len - position)
        return false;
    for (index = 1u; index < count; index++) {
        uint8_t next = bytes[position + index];
        if ((next & UINT8_C(0xc0)) != UINT8_C(0x80))
            return false;
        value = (value << 6u) | (next & UINT8_C(0x3f));
    }
    if (value < minimum || value > UINT32_C(0x10ffff) ||
        (value >= UINT32_C(0xd800) && value <= UINT32_C(0xdfff))) {
        return false;
    }
    *scalar = value;
    *width = count;
    return true;
}

static uint32_t rsdfa_v1_transition_find(const RSDFAV1State *state,
                                         uint32_t scalar) {
    uint32_t low = 0u;
    uint32_t high = state->transition_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const RSDFAV1Transition *transition =
            &state->transitions[middle];
        if (scalar < transition->low) {
            high = middle;
        } else if (scalar > transition->high) {
            low = middle + 1u;
        } else {
            return transition->target;
        }
    }
    return RSDFA_V1_NO_STATE;
}

static bool rsdfa_v1_run_push(RSDFAV1RunVec *runs,
                              uint32_t state,
                              uint32_t start_scalar,
                              uint32_t start_byte) {
    if (!rsdfa_v1_grow(
            (void **)&runs->data, &runs->cap,
            runs->len + 1u, sizeof(*runs->data))) {
        return false;
    }
    runs->data[runs->len++] = (RSDFAV1Run){
        .state = state,
        .start_scalar = start_scalar,
        .start_byte = start_byte,
    };
    return true;
}

static bool rsdfa_v1_u32_push(RSDFAV1U32Vec *values, uint32_t value) {
    if (values->len == UINT32_MAX)
        return false;
    if (!rsdfa_v1_grow(
            (void **)&values->data, &values->cap,
            values->len + 1u, sizeof(*values->data))) {
        return false;
    }
    values->data[values->len++] = value;
    return true;
}

static bool rsdfa_v1_token_push(RSDFAV1TokenVec *tokens,
                                uint32_t token_limit,
                                uint32_t tag,
                                const RSDFAV1Run *run,
                                uint32_t end_scalar,
                                uint32_t end_byte,
                                bool *limit_hit) {
    if (tokens->len >= token_limit) {
        *limit_hit = true;
        return true;
    }
    if (!rsdfa_v1_grow(
            (void **)&tokens->data, &tokens->cap,
            tokens->len + 1u, sizeof(*tokens->data))) {
        return false;
    }
    tokens->data[tokens->len++] = (RSDFAV1Token){
        .tag = tag,
        .start_scalar = run->start_scalar,
        .end_scalar = end_scalar,
        .start_byte = run->start_byte,
        .end_byte = end_byte,
    };
    return true;
}

static bool rsdfa_v1_emit_accepts(const RSDFAV1State *state,
                                  bool eof,
                                  RSDFAV1TokenVec *tokens,
                                  uint32_t token_limit,
                                  const RSDFAV1Run *run,
                                  uint32_t end_scalar,
                                  uint32_t end_byte,
                                  bool *limit_hit) {
    const uint32_t *tags = eof
        ? state->eof_accept_tags
        : state->accept_tags;
    uint32_t tag_len = eof
        ? state->eof_accept_len
        : state->accept_len;
    uint32_t index;

    for (index = 0u; index < tag_len; index++) {
        if (!rsdfa_v1_token_push(
                tokens, token_limit, tags[index], run,
                end_scalar, end_byte, limit_hit)) {
            return false;
        }
        if (*limit_hit)
            return true;
    }
    return true;
}

static int rsdfa_v1_token_compare(const void *left, const void *right) {
    const RSDFAV1Token *lhs = left;
    const RSDFAV1Token *rhs = right;

#define RSDFA_COMPARE_FIELD(field) \
    do { \
        if (lhs->field != rhs->field) \
            return lhs->field < rhs->field ? -1 : 1; \
    } while (0)
    RSDFA_COMPARE_FIELD(start_scalar);
    RSDFA_COMPARE_FIELD(end_scalar);
    RSDFA_COMPARE_FIELD(tag);
    RSDFA_COMPARE_FIELD(start_byte);
    RSDFA_COMPARE_FIELD(end_byte);
#undef RSDFA_COMPARE_FIELD
    return 0;
}

static bool rsdfa_v1_token_equal(const RSDFAV1Token *left,
                                 const RSDFAV1Token *right) {
    return left->tag == right->tag &&
           left->start_scalar == right->start_scalar &&
           left->end_scalar == right->end_scalar &&
           left->start_byte == right->start_byte &&
           left->end_byte == right->end_byte;
}

static void rsdfa_v1_tokens_canonicalize(RSDFAV1TokenVec *tokens) {
    uint32_t read;
    uint32_t write = 0u;

    if (tokens->len > 1u) {
        qsort(tokens->data, tokens->len,
              sizeof(*tokens->data), rsdfa_v1_token_compare);
    }
    for (read = 0u; read < tokens->len; read++) {
        if (write == 0u ||
            !rsdfa_v1_token_equal(
                &tokens->data[read], &tokens->data[write - 1u])) {
            tokens->data[write++] = tokens->data[read];
        }
    }
    tokens->len = write;
}

typedef enum {
    RSDFA_V1_SCAN_BYTES = 0,
    RSDFA_V1_SCAN_SCALAR_VIEW = 1
} RSDFAV1ScanInputKind;

typedef struct {
    RSDFAV1ScanInputKind kind;
    const uint8_t *bytes;
    uint32_t input_byte_len;
    const CettaLpNativeUtf8ScalarView *view;
} RSDFAV1ScanInput;

static bool rsdfa_v1_scan_has_next(const RSDFAV1ScanInput *input,
                                   uint32_t scalar_position,
                                   uint32_t byte_position) {
    if (input->kind == RSDFA_V1_SCAN_BYTES)
        return byte_position < input->input_byte_len;
    return scalar_position < input->view->scalar_len;
}

static bool rsdfa_v1_scan_next(const RSDFAV1ScanInput *input,
                               uint32_t scalar_position,
                               uint32_t byte_position,
                               uint32_t *scalar,
                               uint32_t *width) {
    if (input->kind == RSDFA_V1_SCAN_BYTES) {
        return rsdfa_v1_decode_one(
            input->bytes, input->input_byte_len, byte_position,
            scalar, width);
    }
    if (scalar_position >= input->view->scalar_len ||
        cetta_lp_native_utf8_scalar_view_byte_offset(
            input->view, scalar_position) != byte_position) {
        return false;
    }
    *scalar = cetta_lp_native_utf8_scalar_view_scalar_at(
        input->view, scalar_position);
    *width = cetta_lp_native_utf8_scalar_view_byte_offset(
        input->view, scalar_position + 1u) -
        cetta_lp_native_utf8_scalar_view_byte_offset(
            input->view, scalar_position);
    return true;
}

static bool rsdfa_v1_scan_input(const RSDFAV1Plan *plan,
                                const RSDFAV1ScanInput *input,
                                uint64_t work_limit,
                                uint32_t token_limit,
                                RSDFAV1ScanResult *out,
                                char *error_buf,
                                size_t error_buf_size) {
    RSDFAV1RunVec runs = {0};
    RSDFAV1TokenVec tokens = {0};
    RSDFAV1U32Vec codepoints = {0};
    RSDFAV1U32Vec byte_offsets = {0};
    RSDFAV1ScanResult result;
    uint32_t scalar_position = 0u;
    uint32_t byte_position = 0u;
    bool token_limit_hit = false;
    bool ok = false;

    memset(&result, 0, sizeof(result));
    if (!plan || !plan->impl || !input || !out || work_limit == 0u ||
        token_limit == 0u) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad regular-span DFA scan arguments");
        return false;
    }
    if (!rsdfa_v1_u32_push(&byte_offsets, 0u) ||
        !rsdfa_v1_run_push(
            &runs, plan->impl->start_state, 0u, 0u) ||
        !rsdfa_v1_emit_accepts(
            &plan->impl->states[plan->impl->start_state], false,
            &tokens, token_limit, &runs.data[0], 0u, 0u,
            &token_limit_hit)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to initialize DFA scan");
        goto done;
    }
    if (token_limit_hit)
        goto token_resource;

    while (rsdfa_v1_scan_has_next(
               input, scalar_position, byte_position)) {
        uint32_t scalar;
        uint32_t width;
        uint32_t read;
        uint32_t write = 0u;

        if ((uint64_t)runs.len > work_limit - result.work_item_len)
            goto work_resource;
        if (!rsdfa_v1_scan_next(
                input, scalar_position, byte_position, &scalar, &width)) {
            rsdfa_v1_set_error(
                error_buf, error_buf_size,
                input->kind == RSDFA_V1_SCAN_BYTES
                    ? "invalid UTF-8 at byte offset %u"
                    : "invalid UTF-8 scalar view at byte offset %u",
                byte_position);
            goto done;
        }
        for (read = 0u; read < runs.len; read++) {
            RSDFAV1Run run = runs.data[read];
            uint32_t target;
            result.work_item_len++;
            target = rsdfa_v1_transition_find(
                &plan->impl->states[run.state], scalar);
            if (target == RSDFA_V1_NO_STATE)
                continue;
            run.state = target;
            runs.data[write++] = run;
        }
        runs.len = write;
        byte_position += width;
        scalar_position++;
        if (!rsdfa_v1_u32_push(&codepoints, scalar) ||
            !rsdfa_v1_u32_push(&byte_offsets, byte_position)) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "failed to retain decoded DFA input");
            goto done;
        }
        for (read = 0u; read < runs.len; read++) {
            if (!rsdfa_v1_emit_accepts(
                    &plan->impl->states[runs.data[read].state], false,
                    &tokens, token_limit, &runs.data[read],
                    scalar_position, byte_position, &token_limit_hit)) {
                rsdfa_v1_set_error(error_buf, error_buf_size,
                                   "failed to emit DFA token");
                goto done;
            }
            if (token_limit_hit)
                goto token_resource;
        }
        if (!rsdfa_v1_run_push(
                &runs, plan->impl->start_state,
                scalar_position, byte_position)) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "failed to add DFA start position");
            goto done;
        }
        if (!rsdfa_v1_emit_accepts(
                &plan->impl->states[plan->impl->start_state], false,
                &tokens, token_limit, &runs.data[runs.len - 1u],
                scalar_position, byte_position, &token_limit_hit)) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "failed to emit nullable DFA token");
            goto done;
        }
        if (token_limit_hit)
            goto token_resource;
    }

    {
        uint32_t index;
        for (index = 0u; index < runs.len; index++) {
            if (!rsdfa_v1_emit_accepts(
                    &plan->impl->states[runs.data[index].state], true,
                    &tokens, token_limit, &runs.data[index],
                    scalar_position, byte_position, &token_limit_hit)) {
                rsdfa_v1_set_error(error_buf, error_buf_size,
                                   "failed to emit EOF DFA token");
                goto done;
            }
            if (token_limit_hit)
                goto token_resource;
        }
    }
    result.outcome = RSDFA_V1_SCAN_COMPLETED;
    goto finish;

work_resource:
    result.outcome = RSDFA_V1_SCAN_WORK_LIMIT;
    goto finish;

token_resource:
    result.outcome = RSDFA_V1_SCAN_TOKEN_LIMIT;

finish:
    rsdfa_v1_tokens_canonicalize(&tokens);
    result.tokens = tokens.data;
    result.token_len = tokens.len;
    result.codepoints = codepoints.data;
    result.byte_offsets = byte_offsets.data;
    result.tag_len = plan->tag_len;
    result.input_byte_len = input->input_byte_len;
    result.input_scalar_len = scalar_position;
    result.decoded_byte_len = input->kind == RSDFA_V1_SCAN_BYTES
        ? byte_position : input->view->decoded_byte_len;
    result.source_pass_count = input->kind == RSDFA_V1_SCAN_BYTES
        ? 1u : input->view->source_pass_count;
    tokens.data = NULL;
    codepoints.data = NULL;
    byte_offsets.data = NULL;
    rsdfa_v1_scan_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    free(runs.data);
    free(tokens.data);
    free(codepoints.data);
    free(byte_offsets.data);
    return ok;
}

bool rsdfa_v1_scan(const RSDFAV1Plan *plan,
                   const uint8_t *input,
                   size_t input_len,
                   uint64_t work_limit,
                   uint32_t token_limit,
                   RSDFAV1ScanResult *out,
                   char *error_buf,
                   size_t error_buf_size) {
    RSDFAV1ScanInput source;

    if (input_len > UINT32_MAX - 2u ||
        (input_len > 0u && !input)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad regular-span DFA byte input");
        return false;
    }
    source = (RSDFAV1ScanInput){
        .kind = RSDFA_V1_SCAN_BYTES,
        .bytes = input,
        .input_byte_len = (uint32_t)input_len,
        .view = NULL,
    };
    return rsdfa_v1_scan_input(
        plan, &source, work_limit, token_limit,
        out, error_buf, error_buf_size);
}

bool rsdfa_v1_scan_scalar_view(
    const RSDFAV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    uint32_t token_limit,
    RSDFAV1ScanResult *out,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1ScanInput source;

    if (!cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        return false;
    }
    source = (RSDFAV1ScanInput){
        .kind = RSDFA_V1_SCAN_SCALAR_VIEW,
        .bytes = NULL,
        .input_byte_len = view->input_byte_len,
        .view = view,
    };
    return rsdfa_v1_scan_input(
        plan, &source, work_limit, token_limit,
        out, error_buf, error_buf_size);
}

static void rsdfa_v1_cursor_record_accepts(
    const RSDFAV1State *state,
    bool eof,
    uint32_t start_scalar,
    uint32_t start_byte,
    uint32_t end_scalar,
    uint32_t end_byte,
    RSDFAV1Token *tokens) {
    const uint32_t *tags = eof
        ? state->eof_accept_tags : state->accept_tags;
    uint32_t tag_len = eof
        ? state->eof_accept_len : state->accept_len;
    uint32_t index;

    for (index = 0u; index < tag_len; index++) {
        uint32_t tag = tags[index];
        tokens[tag] = (RSDFAV1Token){
            .tag = tag,
            .start_scalar = start_scalar,
            .end_scalar = end_scalar,
            .start_byte = start_byte,
            .end_byte = end_byte,
        };
    }
}

static bool rsdfa_v1_scan_cursor_longest_impl(
    const RSDFAV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1CursorScanResult result;
    uint32_t state;
    uint32_t scalar_position;
    uint32_t byte_position;
    uint32_t index;
    uint32_t write = 0u;

    memset(&result, 0, sizeof(result));
    if (!plan || !plan->impl || !view || !out || work_limit == 0u ||
        start_scalar > view->scalar_len ||
        token_capacity < plan->tag_len ||
        (plan->tag_len > 0u && !tokens) ||
        !cetta_lp_native_utf8_scalar_view_has_storage(view)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad regular-span DFA cursor arguments");
        return false;
    }
    for (index = 0u; index < plan->tag_len; index++)
        tokens[index].tag = UINT32_MAX;

    result.outcome = RSDFA_V1_CURSOR_SCAN_COMPLETED;
    result.start_scalar = start_scalar;
    result.stop_scalar = start_scalar;
    result.start_byte = cetta_lp_native_utf8_scalar_view_byte_offset(
        view, start_scalar);
    result.stop_byte = result.start_byte;
    state = plan->impl->start_state;
    scalar_position = start_scalar;
    byte_position = result.start_byte;
    rsdfa_v1_cursor_record_accepts(
        &plan->impl->states[state], false,
        start_scalar, result.start_byte,
        scalar_position, byte_position, tokens);

    while (scalar_position < view->scalar_len) {
        uint32_t target;

        if (result.work_item_len >= work_limit) {
            result.outcome = RSDFA_V1_CURSOR_SCAN_WORK_LIMIT;
            goto finish;
        }
        result.work_item_len++;
        target = rsdfa_v1_transition_find(
            &plan->impl->states[state],
            cetta_lp_native_utf8_scalar_view_scalar_at(
                view, scalar_position));
        if (target == RSDFA_V1_NO_STATE)
            goto finish;
        state = target;
        scalar_position++;
        byte_position = cetta_lp_native_utf8_scalar_view_byte_offset(
            view, scalar_position);
        result.stop_scalar = scalar_position;
        result.stop_byte = byte_position;
        rsdfa_v1_cursor_record_accepts(
            &plan->impl->states[state], false,
            start_scalar, result.start_byte,
            scalar_position, byte_position, tokens);
    }

    result.reached_eof = true;
    rsdfa_v1_cursor_record_accepts(
        &plan->impl->states[state], true,
        start_scalar, result.start_byte,
        scalar_position, byte_position, tokens);

finish:
    for (index = 0u; index < plan->tag_len; index++) {
        if (tokens[index].tag != UINT32_MAX)
            tokens[write++] = tokens[index];
    }
    result.accept_len = write;
    *out = result;
    return true;
}

bool rsdfa_v1_scan_cursor_longest_prevalidated(
    const RSDFAV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size) {
    return rsdfa_v1_scan_cursor_longest_impl(
        plan, view, start_scalar, work_limit,
        tokens, token_capacity, out,
        error_buf, error_buf_size);
}

bool rsdfa_v1_scan_cursor_longest(
    const RSDFAV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        return false;
    }
    return rsdfa_v1_scan_cursor_longest_impl(
        plan, view, start_scalar, work_limit,
        tokens, token_capacity, out,
        error_buf, error_buf_size);
}

static uint32_t rsdfa_v1_program_transition_find(
    const RSDFAV1Program *program,
    uint32_t state_index,
    uint32_t scalar) {
    const RSDFAV1ProgramState *state = &program->states[state_index];
    uint32_t low = 0u;
    uint32_t high = state->transition_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const RSDFAV1ProgramTransition *transition =
            &program->transitions[state->transition_begin + middle];
        if (scalar < transition->low) {
            high = middle;
        } else if (scalar > transition->high) {
            low = middle + 1u;
        } else {
            return transition->target;
        }
    }
    return RSDFA_V1_NO_STATE;
}

static void rsdfa_v1_program_cursor_record_accepts(
    const RSDFAV1Program *program,
    uint32_t state_index,
    bool eof,
    uint32_t start_scalar,
    uint32_t start_byte,
    uint32_t end_scalar,
    uint32_t end_byte,
    RSDFAV1Token *tokens,
    uint64_t *small_tag_set) {
    const RSDFAV1ProgramState *state = &program->states[state_index];
    const uint32_t *tags = eof
        ? program->eof_accept_tags : program->accept_tags;
    uint32_t begin = eof
        ? state->eof_accept_begin : state->accept_begin;
    uint32_t len = eof ? state->eof_accept_len : state->accept_len;
    uint32_t index;

    for (index = 0u; index < len; index++) {
        uint32_t tag = tags[begin + index];
        if (small_tag_set)
            *small_tag_set |= UINT64_C(1) << tag;
        tokens[tag] = (RSDFAV1Token){
            .tag = tag,
            .start_scalar = start_scalar,
            .end_scalar = end_scalar,
            .start_byte = start_byte,
            .end_byte = end_byte,
        };
    }
}

static bool rsdfa_v1_program_scan_cursor_longest_impl(
    const RSDFAV1Program *program,
    const RSDFAV1AsciiTransitionIndex *ascii_index,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1CursorScanResult result;
    uint32_t state;
    uint32_t scalar_position;
    uint32_t byte_position;
    uint32_t index;
    uint32_t write = 0u;
    uint64_t small_tag_set = 0u;
    uint64_t *small_tag_set_ptr = NULL;

    memset(&result, 0, sizeof(result));
    if (!program || !program->states || program->state_len == 0u ||
        program->start_state >= program->state_len ||
        program->tag_len == 0u || !view || !out || work_limit == 0u ||
        start_scalar > view->scalar_len ||
        token_capacity < program->tag_len || !tokens ||
        (ascii_index &&
         (!ascii_index->targets ||
          ascii_index->state_len != program->state_len)) ||
        !cetta_lp_native_utf8_scalar_view_has_storage(view)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad regular-span DFA program cursor arguments");
        return false;
    }
    if (program->tag_len <= 64u) {
        small_tag_set_ptr = &small_tag_set;
    } else {
        for (index = 0u; index < program->tag_len; index++)
            tokens[index].tag = UINT32_MAX;
    }

    result.outcome = RSDFA_V1_CURSOR_SCAN_COMPLETED;
    result.start_scalar = start_scalar;
    result.stop_scalar = start_scalar;
    result.start_byte = cetta_lp_native_utf8_scalar_view_byte_offset(
        view, start_scalar);
    result.stop_byte = result.start_byte;
    state = program->start_state;
    scalar_position = start_scalar;
    byte_position = result.start_byte;
    rsdfa_v1_program_cursor_record_accepts(
        program, state, false, start_scalar, result.start_byte,
        scalar_position, byte_position, tokens, small_tag_set_ptr);

    while (scalar_position < view->scalar_len) {
        uint32_t target;

        if (result.work_item_len >= work_limit) {
            result.outcome = RSDFA_V1_CURSOR_SCAN_WORK_LIMIT;
            goto finish;
        }
        result.work_item_len++;
        if (view->ascii_bytes && ascii_index) {
            target = ascii_index->targets[
                (size_t)state * RSDFA_V1_ASCII_CARDINALITY +
                view->ascii_bytes[scalar_position]];
        } else {
            target = rsdfa_v1_program_transition_find(
                program, state,
                cetta_lp_native_utf8_scalar_view_scalar_at(
                    view, scalar_position));
        }
        if (target == RSDFA_V1_NO_STATE)
            goto finish;
        state = target;
        scalar_position++;
        byte_position = view->ascii_bytes
            ? scalar_position
            : cetta_lp_native_utf8_scalar_view_byte_offset(
                view, scalar_position);
        result.stop_scalar = scalar_position;
        result.stop_byte = byte_position;
        rsdfa_v1_program_cursor_record_accepts(
            program, state, false, start_scalar, result.start_byte,
            scalar_position, byte_position, tokens, small_tag_set_ptr);
    }

    result.reached_eof = true;
    rsdfa_v1_program_cursor_record_accepts(
        program, state, true, start_scalar, result.start_byte,
        scalar_position, byte_position, tokens, small_tag_set_ptr);

finish:
    if (small_tag_set_ptr) {
        while (small_tag_set != 0u) {
            uint32_t tag = (uint32_t)__builtin_ctzll(small_tag_set);
            tokens[write++] = tokens[tag];
            small_tag_set &= small_tag_set - 1u;
        }
    } else {
        for (index = 0u; index < program->tag_len; index++) {
            if (tokens[index].tag != UINT32_MAX)
                tokens[write++] = tokens[index];
        }
    }
    result.accept_len = write;
    *out = result;
    return true;
}

bool rsdfa_v1_program_scan_cursor_longest_prevalidated(
    const RSDFAV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size) {
    return rsdfa_v1_program_scan_cursor_longest_impl(
        program, NULL, view, start_scalar, work_limit,
        tokens, token_capacity, out,
        error_buf, error_buf_size);
}

bool rsdfa_v1_program_scan_cursor_longest_indexed_prevalidated(
    const RSDFAV1Program *program,
    const RSDFAV1AsciiTransitionIndex *ascii_index,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size) {
    return rsdfa_v1_program_scan_cursor_longest_impl(
        program, ascii_index, view, start_scalar, work_limit,
        tokens, token_capacity, out,
        error_buf, error_buf_size);
}

bool rsdfa_v1_program_scan_cursor_longest(
    const RSDFAV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!rsdfa_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        return false;
    }
    return rsdfa_v1_program_scan_cursor_longest_impl(
        program, NULL, view, start_scalar, work_limit,
        tokens, token_capacity, out,
        error_buf, error_buf_size);
}

void rsdfa_v1_parser_lattice_init(RSDFAV1ParserLattice *lattice) {
    if (lattice)
        memset(lattice, 0, sizeof(*lattice));
}

void rsdfa_v1_parser_lattice_free(RSDFAV1ParserLattice *lattice) {
    if (!lattice)
        return;
    free(lattice->terminal_ids);
    free(lattice->edges);
    free(lattice->start_offsets);
    free(lattice->owned_byte_offsets);
    memset(lattice, 0, sizeof(*lattice));
}

static uint32_t rsdfa_v1_scalar_width(uint32_t scalar) {
    if (scalar <= UINT32_C(0x7f))
        return 1u;
    if (scalar <= UINT32_C(0x7ff))
        return 2u;
    if (scalar <= UINT32_C(0xffff))
        return 3u;
    return 4u;
}

static int rsdfa_v1_lattice_edge_compare(const void *left,
                                         const void *right) {
    const CettaLpNativeUtf8LatticeEdge *lhs = left;
    const CettaLpNativeUtf8LatticeEdge *rhs = right;

#define RSDFA_LATTICE_COMPARE_FIELD(field) \
    do { \
        if (lhs->field != rhs->field) \
            return lhs->field < rhs->field ? -1 : 1; \
    } while (0)
    RSDFA_LATTICE_COMPARE_FIELD(scalar_left);
    RSDFA_LATTICE_COMPARE_FIELD(terminal_id);
    RSDFA_LATTICE_COMPARE_FIELD(scalar_right);
    RSDFA_LATTICE_COMPARE_FIELD(value_kind);
    RSDFA_LATTICE_COMPARE_FIELD(value);
    RSDFA_LATTICE_COMPARE_FIELD(byte_left);
    RSDFA_LATTICE_COMPARE_FIELD(byte_right);
#undef RSDFA_LATTICE_COMPARE_FIELD
    return 0;
}

static bool rsdfa_v1_base_terminal_valid(
    const CettaLpNativeUtf8Terminal *terminal,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if ((uint32_t)terminal->kind >
        (uint32_t)CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "composite lattice terminal kind is invalid");
        return false;
    }
    if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
        if (terminal->range_len == 0u || !terminal->ranges) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "composite lattice range terminal is empty");
            return false;
        }
        for (index = 0u; index < terminal->range_len; index++) {
            if (!rsdfa_v1_scalar_range_valid(
                    terminal->ranges[index].low,
                    terminal->ranges[index].high) ||
                (index > 0u &&
                 terminal->ranges[index - 1u].high >=
                     terminal->ranges[index].low)) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "composite lattice terminal ranges are invalid");
                return false;
            }
        }
        return true;
    }
    if (terminal->range_len != 0u || terminal->ranges) {
        rsdfa_v1_set_error(
            error_buf, error_buf_size,
            "non-range composite lattice terminal carries ranges");
        return false;
    }
    if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR &&
        !rsdfa_v1_scalar_range_valid(
            terminal->scalar, terminal->scalar)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "composite lattice terminal is not a scalar");
        return false;
    }
    return true;
}

static bool rsdfa_v1_base_terminal_matches(
    const CettaLpNativeUtf8Terminal *terminal,
    const RSDFAV1ScanResult *source,
    uint32_t position) {
    uint32_t scalar;
    uint32_t low;
    uint32_t high;

    if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_EOF)
        return position == source->input_scalar_len;
    if (position >= source->input_scalar_len)
        return false;
    scalar = source->codepoints[position];
    if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_ANY)
        return true;
    if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR)
        return scalar == terminal->scalar;
    low = 0u;
    high = terminal->range_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (scalar < terminal->ranges[middle].low) {
            high = middle;
        } else if (scalar > terminal->ranges[middle].high) {
            low = middle + 1u;
        } else {
            return true;
        }
    }
    return false;
}

static bool rsdfa_v1_scan_receipt_valid(
    const RSDFAV1ScanResult *source) {
    return (source->source_pass_count == 0u &&
            source->decoded_byte_len == 0u) ||
        (source->source_pass_count == 1u &&
         source->decoded_byte_len == source->input_byte_len);
}

bool rsdfa_v1_composite_parser_lattice_build(
    const RSDFAV1ScanResult *source,
    const CettaLpNativeUtf8Terminal *base_terminals,
    uint32_t base_terminal_len,
    const uint32_t *tag_terminal_ids,
    uint32_t tag_len,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1ParserLattice result;
    uint32_t mapped_tag_count = 0u;
    uint32_t declared_terminal_len = 0u;
    uint32_t edge_len = 0u;
    uint32_t index;
    bool ok = false;

    memset(&result, 0, sizeof(result));
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!source || !out || tag_len != source->tag_len ||
        (base_terminal_len > 0u && !base_terminals) ||
        (tag_len > 0u && !tag_terminal_ids) ||
        source->outcome != RSDFA_V1_SCAN_COMPLETED ||
        !rsdfa_v1_scan_receipt_valid(source) ||
        source->input_scalar_len > UINT32_MAX - 2u ||
        (source->input_scalar_len > 0u && !source->codepoints) ||
        !source->byte_offsets ||
        (source->token_len > 0u && !source->tokens)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "bad completed DFA scan for parser lattice");
        goto done;
    }
    if (source->byte_offsets[0] != 0u ||
        source->byte_offsets[source->input_scalar_len] !=
            source->input_byte_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "DFA scan has an invalid decoded byte extent");
        goto done;
    }
    for (index = 0u; index < source->input_scalar_len; index++) {
        uint32_t scalar = source->codepoints[index];
        uint32_t left = source->byte_offsets[index];
        uint32_t right = source->byte_offsets[index + 1u];
        if (!rsdfa_v1_scalar_range_valid(scalar, scalar) ||
            right < left ||
            right - left != rsdfa_v1_scalar_width(scalar)) {
            rsdfa_v1_set_error(
                error_buf, error_buf_size,
                "DFA scan has an invalid scalar/byte index");
            goto done;
        }
    }
    for (index = 0u; index < base_terminal_len; index++) {
        uint32_t other;
        if (!rsdfa_v1_base_terminal_valid(
                &base_terminals[index], error_buf, error_buf_size)) {
            goto done;
        }
        for (other = 0u; other < index; other++) {
            if (base_terminals[other].terminal_id ==
                base_terminals[index].terminal_id) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "composite lattice base terminal ID is duplicated");
                goto done;
            }
        }
        for (other = 0u; other < tag_len; other++) {
            if (tag_terminal_ids[other] != UINT32_MAX &&
                tag_terminal_ids[other] ==
                    base_terminals[index].terminal_id) {
                rsdfa_v1_set_error(
                    error_buf, error_buf_size,
                    "span and scalar terminal IDs collide");
                goto done;
            }
        }
    }
    for (index = 0u; index < tag_len; index++) {
        if (tag_terminal_ids[index] != UINT32_MAX)
            mapped_tag_count++;
    }
    if (mapped_tag_count > UINT32_MAX - base_terminal_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "composite lattice terminal count overflow");
        goto done;
    }
    declared_terminal_len = mapped_tag_count + base_terminal_len;
    if (declared_terminal_len > 0u) {
        if ((size_t)declared_terminal_len >
            SIZE_MAX / sizeof(*result.terminal_ids)) {
            goto done;
        }
        result.terminal_ids = malloc(
            sizeof(*result.terminal_ids) * declared_terminal_len);
        if (!result.terminal_ids)
            goto done;
        declared_terminal_len = 0u;
        for (index = 0u; index < base_terminal_len; index++) {
            result.terminal_ids[declared_terminal_len++] =
                base_terminals[index].terminal_id;
        }
        for (index = 0u; index < tag_len; index++) {
            if (tag_terminal_ids[index] != UINT32_MAX) {
                result.terminal_ids[declared_terminal_len++] =
                    tag_terminal_ids[index];
            }
        }
        qsort(result.terminal_ids, declared_terminal_len,
              sizeof(*result.terminal_ids), rsdfa_v1_u32_compare);
        {
            uint32_t read;
            uint32_t write = 0u;
            for (read = 0u; read < declared_terminal_len; read++) {
                if (write == 0u ||
                    result.terminal_ids[read] !=
                        result.terminal_ids[write - 1u]) {
                    result.terminal_ids[write++] =
                        result.terminal_ids[read];
                }
            }
            declared_terminal_len = write;
        }
    }

    for (index = 0u; index < source->token_len; index++) {
        const RSDFAV1Token *token = &source->tokens[index];
        if (token->tag >= source->tag_len ||
            token->start_scalar > token->end_scalar ||
            token->end_scalar > source->input_scalar_len ||
            token->start_byte !=
                source->byte_offsets[token->start_scalar] ||
            token->end_byte !=
                source->byte_offsets[token->end_scalar]) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "DFA token is inconsistent with decoded input");
            goto done;
        }
        if (tag_terminal_ids[token->tag] != UINT32_MAX) {
            if (edge_len == UINT32_MAX) {
                rsdfa_v1_set_error(error_buf, error_buf_size,
                                   "composite lattice edge count overflow");
                goto done;
            }
            edge_len++;
        }
    }
    {
        uint32_t position;
        for (position = 0u;
             position <= source->input_scalar_len; position++) {
            uint32_t terminal_index;
            for (terminal_index = 0u;
                 terminal_index < base_terminal_len; terminal_index++) {
                if (rsdfa_v1_base_terminal_matches(
                        &base_terminals[terminal_index],
                        source, position)) {
                    if (edge_len == UINT32_MAX) {
                        rsdfa_v1_set_error(
                            error_buf, error_buf_size,
                            "composite lattice edge count overflow");
                        goto done;
                    }
                    edge_len++;
                }
            }
        }
    }
    if (edge_len > 0u) {
        uint32_t write = 0u;
        if ((size_t)edge_len >
            SIZE_MAX / sizeof(*result.edges)) {
            goto done;
        }
        result.edges = malloc(sizeof(*result.edges) * edge_len);
        if (!result.edges)
            goto done;
        for (index = 0u; index < source->token_len; index++) {
            const RSDFAV1Token *token = &source->tokens[index];
            uint32_t terminal_id = tag_terminal_ids[token->tag];
            if (terminal_id == UINT32_MAX)
                continue;
            result.edges[write++] = (CettaLpNativeUtf8LatticeEdge){
                .terminal_id = terminal_id,
                .scalar_left = token->start_scalar,
                .scalar_right = token->end_scalar,
                .byte_left = token->start_byte,
                .byte_right = token->end_byte,
                .value_kind =
                    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS,
                .value = index,
            };
        }
        {
            uint32_t position;
            for (position = 0u;
                 position <= source->input_scalar_len; position++) {
                uint32_t terminal_index;
                for (terminal_index = 0u;
                     terminal_index < base_terminal_len; terminal_index++) {
                    const CettaLpNativeUtf8Terminal *terminal =
                        &base_terminals[terminal_index];
                    bool eof;
                    if (!rsdfa_v1_base_terminal_matches(
                            terminal, source, position)) {
                        continue;
                    }
                    eof = terminal->kind ==
                        CETTA_LP_NATIVE_UTF8_TERMINAL_EOF;
                    result.edges[write++] =
                        (CettaLpNativeUtf8LatticeEdge){
                            .terminal_id = terminal->terminal_id,
                            .scalar_left = position,
                            .scalar_right = eof ? position : position + 1u,
                            .byte_left = source->byte_offsets[position],
                            .byte_right = eof
                                ? source->byte_offsets[position]
                                : source->byte_offsets[position + 1u],
                            .value_kind = eof
                                ? CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF
                                : CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR,
                            .value = eof ? 0u : source->codepoints[position],
                        };
                }
            }
        }
        if (write != edge_len) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "composite lattice edge count changed");
            goto done;
        }
        qsort(result.edges, edge_len, sizeof(*result.edges),
              rsdfa_v1_lattice_edge_compare);
    }
    result.start_offsets = calloc(
        (size_t)source->input_scalar_len + 2u,
        sizeof(*result.start_offsets));
    if (!result.start_offsets)
        goto done;
    for (index = 0u; index < edge_len; index++) {
        uint32_t offset_index = result.edges[index].scalar_left + 1u;
        if (result.start_offsets[offset_index] == UINT32_MAX) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "DFA parser lattice position count overflow");
            goto done;
        }
        result.start_offsets[offset_index]++;
    }
    for (index = 1u; index <= source->input_scalar_len + 1u; index++) {
        if (result.start_offsets[index] >
            UINT32_MAX - result.start_offsets[index - 1u]) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "DFA parser lattice offset overflow");
            goto done;
        }
        result.start_offsets[index] += result.start_offsets[index - 1u];
    }
    result.lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = result.terminal_ids,
        .terminal_len = declared_terminal_len,
        .edges = result.edges,
        .edge_len = edge_len,
        .start_offsets = result.start_offsets,
        .start_offset_len = source->input_scalar_len + 2u,
        .codepoints = source->codepoints,
        .byte_offsets = source->byte_offsets,
        .scalar_len = source->input_scalar_len,
        .input_byte_len = source->input_byte_len,
        .decoded_byte_len = source->decoded_byte_len,
        .source_pass_count = source->source_pass_count,
    };
    rsdfa_v1_parser_lattice_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    rsdfa_v1_parser_lattice_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to build DFA parser lattice");
    }
    return ok;
}

bool rsdfa_v1_parser_lattice_build(
    const RSDFAV1ScanResult *source,
    const uint32_t *tag_terminal_ids,
    uint32_t tag_len,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size) {
    return rsdfa_v1_composite_parser_lattice_build(
        source, NULL, 0u, tag_terminal_ids, tag_len,
        out, error_buf, error_buf_size);
}

static bool rsdfa_v1_parser_lattice_slice_impl(
    const CettaLpNativeUtf8Lattice *source,
    uint32_t scalar_left,
    uint32_t scalar_right,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size,
    bool source_prevalidated) {
    RSDFAV1ParserLattice result;
    uint32_t scalar_len;
    uint32_t byte_left;
    uint32_t edge_len = 0u;
    uint32_t position;
    uint32_t write = 0u;
    bool ok = false;

    memset(&result, 0, sizeof(result));
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!source || !out || scalar_left > scalar_right ||
        scalar_right > (source ? source->scalar_len : 0u) ||
        (!source_prevalidated &&
         !cetta_lp_native_utf8_lattice_validate(
             source, error_buf, error_buf_size))) {
        if (!error_buf || error_buf[0] == '\0') {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "bad parser lattice slice interval");
        }
        goto done;
    }
    scalar_len = scalar_right - scalar_left;
    byte_left = source->byte_offsets[scalar_left];
    if ((source->terminal_len > 0u &&
         (size_t)source->terminal_len >
             SIZE_MAX / sizeof(*result.terminal_ids)) ||
        (size_t)scalar_len + 1u >
            SIZE_MAX / sizeof(*result.owned_byte_offsets) ||
        (size_t)scalar_len + 2u >
            SIZE_MAX / sizeof(*result.start_offsets)) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "parser lattice slice allocation overflow");
        goto done;
    }
    if (source->terminal_len > 0u) {
        result.terminal_ids = malloc(
            sizeof(*result.terminal_ids) *
            (size_t)source->terminal_len);
        if (!result.terminal_ids)
            goto done;
        memcpy(result.terminal_ids, source->terminal_ids,
               sizeof(*result.terminal_ids) *
               (size_t)source->terminal_len);
    }
    result.owned_byte_offsets = malloc(
        sizeof(*result.owned_byte_offsets) *
        ((size_t)scalar_len + 1u));
    result.start_offsets = calloc(
        (size_t)scalar_len + 2u, sizeof(*result.start_offsets));
    if (!result.owned_byte_offsets || !result.start_offsets)
        goto done;
    for (position = 0u; position <= scalar_len; position++) {
        result.owned_byte_offsets[position] =
            source->byte_offsets[scalar_left + position] - byte_left;
    }
    for (position = scalar_left; position <= scalar_right; position++) {
        uint32_t edge_index;
        for (edge_index = source->start_offsets[position];
             edge_index < source->start_offsets[position + 1u];
             edge_index++) {
            if (source->edges[edge_index].scalar_right <= scalar_right) {
                if (edge_len == UINT32_MAX) {
                    rsdfa_v1_set_error(
                        error_buf, error_buf_size,
                        "parser lattice slice edge count overflow");
                    goto done;
                }
                edge_len++;
            }
        }
    }
    if (edge_len > 0u) {
        if ((size_t)edge_len > SIZE_MAX / sizeof(*result.edges)) {
            rsdfa_v1_set_error(error_buf, error_buf_size,
                               "parser lattice slice edge allocation overflow");
            goto done;
        }
        result.edges = malloc(sizeof(*result.edges) * (size_t)edge_len);
        if (!result.edges)
            goto done;
    }
    for (position = scalar_left; position <= scalar_right; position++) {
        uint32_t edge_index;
        for (edge_index = source->start_offsets[position];
             edge_index < source->start_offsets[position + 1u];
             edge_index++) {
            CettaLpNativeUtf8LatticeEdge edge =
                source->edges[edge_index];
            if (edge.scalar_right > scalar_right)
                continue;
            edge.scalar_left -= scalar_left;
            edge.scalar_right -= scalar_left;
            edge.byte_left -= byte_left;
            edge.byte_right -= byte_left;
            result.edges[write++] = edge;
            result.start_offsets[edge.scalar_left + 1u]++;
        }
    }
    if (write != edge_len) {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "parser lattice slice edge count changed");
        goto done;
    }
    for (position = 1u; position <= scalar_len + 1u; position++) {
        result.start_offsets[position] +=
            result.start_offsets[position - 1u];
    }
    result.lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = result.terminal_ids,
        .terminal_len = source->terminal_len,
        .edges = result.edges,
        .edge_len = edge_len,
        .start_offsets = result.start_offsets,
        .start_offset_len = scalar_len + 2u,
        .codepoints = scalar_len > 0u
            ? source->codepoints + scalar_left
            : NULL,
        .byte_offsets = result.owned_byte_offsets,
        .scalar_len = scalar_len,
        .input_byte_len =
            source->byte_offsets[scalar_right] - byte_left,
        .decoded_byte_len = 0u,
        .source_pass_count = 0u,
    };
    if (!cetta_lp_native_utf8_lattice_validate(
            &result.lattice, error_buf, error_buf_size)) {
        goto done;
    }
    rsdfa_v1_parser_lattice_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    rsdfa_v1_parser_lattice_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        rsdfa_v1_set_error(error_buf, error_buf_size,
                           "failed to slice parser lattice");
    }
    return ok;
}

bool rsdfa_v1_parser_lattice_slice(
    const CettaLpNativeUtf8Lattice *source,
    uint32_t scalar_left,
    uint32_t scalar_right,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size) {
    return rsdfa_v1_parser_lattice_slice_impl(
        source, scalar_left, scalar_right, out,
        error_buf, error_buf_size, false);
}

bool rsdfa_v1_parser_lattice_slice_prevalidated(
    const CettaLpNativeUtf8Lattice *source,
    uint32_t scalar_left,
    uint32_t scalar_right,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size) {
    return rsdfa_v1_parser_lattice_slice_impl(
        source, scalar_left, scalar_right, out,
        error_buf, error_buf_size, true);
}
