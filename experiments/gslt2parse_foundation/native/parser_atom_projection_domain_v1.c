#include "parser_atom_projection_domain_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t state_len;
    uint32_t state_limit;
    uint32_t transition_limit;
    uint32_t *starts;
    uint32_t start_len;
    uint32_t start_cap;
    RSDFAV1NfaEdge *edges;
    uint32_t edge_len;
    uint32_t edge_cap;
    RSDFAV1NfaAccept *accepts;
    uint32_t accept_len;
    uint32_t accept_cap;
    bool state_limit_hit;
    bool transition_limit_hit;
} PPAtomProjectionDomainV1Builder;

static void ppdomain_v1_set_error(char *buf,
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

static bool ppdomain_v1_grow(void **data,
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

static void ppdomain_v1_builder_free(
    PPAtomProjectionDomainV1Builder *builder) {
    uint32_t index;

    if (!builder)
        return;
    for (index = 0u; index < builder->edge_len; index++)
        free((void *)builder->edges[index].ranges);
    free(builder->starts);
    free(builder->edges);
    free(builder->accepts);
    memset(builder, 0, sizeof(*builder));
}

static bool ppdomain_v1_state(
    PPAtomProjectionDomainV1Builder *builder,
    uint32_t *out) {
    if (builder->state_len >= builder->state_limit) {
        builder->state_limit_hit = true;
        return false;
    }
    *out = builder->state_len++;
    return true;
}

static bool ppdomain_v1_start(
    PPAtomProjectionDomainV1Builder *builder,
    uint32_t state) {
    if (!ppdomain_v1_grow(
            (void **)&builder->starts, &builder->start_cap,
            builder->start_len + 1u, sizeof(*builder->starts))) {
        return false;
    }
    builder->starts[builder->start_len++] = state;
    return true;
}

static bool ppdomain_v1_accept(
    PPAtomProjectionDomainV1Builder *builder,
    uint32_t state) {
    if (!ppdomain_v1_grow(
            (void **)&builder->accepts, &builder->accept_cap,
            builder->accept_len + 1u, sizeof(*builder->accepts))) {
        return false;
    }
    builder->accepts[builder->accept_len++] =
        (RSDFAV1NfaAccept){.state = state, .tag = 0u};
    return true;
}

static bool ppdomain_v1_edge(
    PPAtomProjectionDomainV1Builder *builder,
    uint32_t from,
    uint32_t to,
    const CettaLpNativeUnicodeRange *ranges,
    uint32_t range_len) {
    CettaLpNativeUnicodeRange *copy;

    if (builder->edge_len >= builder->transition_limit) {
        builder->transition_limit_hit = true;
        return false;
    }
    if (range_len == 0u || !ranges ||
        (size_t)range_len > SIZE_MAX / sizeof(*copy)) {
        return false;
    }
    copy = malloc(sizeof(*copy) * (size_t)range_len);
    if (!copy)
        return false;
    memcpy(copy, ranges, sizeof(*copy) * (size_t)range_len);
    if (!ppdomain_v1_grow(
            (void **)&builder->edges, &builder->edge_cap,
            builder->edge_len + 1u, sizeof(*builder->edges))) {
        free(copy);
        return false;
    }
    builder->edges[builder->edge_len++] = (RSDFAV1NfaEdge){
        .from = from,
        .to = to,
        .kind = RSDFA_V1_NFA_RANGES,
        .ranges = copy,
        .range_len = range_len,
    };
    return true;
}

static uint64_t ppdomain_v1_pow16(uint32_t exponent) {
    return UINT64_C(1) << (4u * exponent);
}

static bool ppdomain_v1_hex_digit_edge(
    PPAtomProjectionDomainV1Builder *builder,
    uint32_t from,
    uint32_t to,
    uint32_t digit) {
    CettaLpNativeUnicodeRange ranges[2];
    uint32_t range_len;

    if (digit < 10u) {
        ranges[0] = (CettaLpNativeUnicodeRange){
            (uint32_t)'0' + digit, (uint32_t)'0' + digit,
        };
        range_len = 1u;
    } else {
        ranges[0] = (CettaLpNativeUnicodeRange){
            (uint32_t)'A' + digit - 10u,
            (uint32_t)'A' + digit - 10u,
        };
        ranges[1] = (CettaLpNativeUnicodeRange){
            (uint32_t)'a' + digit - 10u,
            (uint32_t)'a' + digit - 10u,
        };
        range_len = 2u;
    }
    return ppdomain_v1_edge(builder, from, to, ranges, range_len);
}

static bool ppdomain_v1_hex_any_edge(
    PPAtomProjectionDomainV1Builder *builder,
    uint32_t from,
    uint32_t to) {
    static const CettaLpNativeUnicodeRange ranges[] = {
        {(uint32_t)'0', (uint32_t)'9'},
        {(uint32_t)'A', (uint32_t)'F'},
        {(uint32_t)'a', (uint32_t)'f'},
    };
    return ppdomain_v1_edge(
        builder, from, to, ranges,
        (uint32_t)(sizeof(ranges) / sizeof(ranges[0])));
}

static bool ppdomain_v1_hex_interval(
    PPAtomProjectionDomainV1Builder *builder,
    uint32_t from,
    uint32_t remaining,
    uint64_t low,
    uint64_t high) {
    uint64_t maximum = ppdomain_v1_pow16(remaining) - 1u;

    if (low > high || high > maximum)
        return false;
    if (remaining == 0u)
        return ppdomain_v1_accept(builder, from);
    if (low == 0u && high == maximum) {
        uint32_t current = from;
        uint32_t position;
        for (position = 0u; position < remaining; position++) {
            uint32_t next;
            if (!ppdomain_v1_state(builder, &next) ||
                !ppdomain_v1_hex_any_edge(builder, current, next)) {
                return false;
            }
            current = next;
        }
        return ppdomain_v1_accept(builder, current);
    }
    {
        uint64_t block = ppdomain_v1_pow16(remaining - 1u);
        uint32_t first = (uint32_t)(low / block);
        uint32_t last = (uint32_t)(high / block);
        uint32_t digit;

        for (digit = first; digit <= last; digit++) {
            uint64_t sub_low = digit == first ? low % block : 0u;
            uint64_t sub_high = digit == last
                ? high % block : block - 1u;
            uint32_t next;
            if (!ppdomain_v1_state(builder, &next) ||
                !ppdomain_v1_hex_digit_edge(
                    builder, from, next, digit) ||
                !ppdomain_v1_hex_interval(
                    builder, next, remaining - 1u,
                    sub_low, sub_high)) {
                return false;
            }
        }
    }
    return true;
}

static bool ppdomain_v1_hex_interval_root(
    PPAtomProjectionDomainV1Builder *builder,
    uint32_t digits,
    uint64_t low,
    uint64_t high) {
    uint32_t start;

    return ppdomain_v1_state(builder, &start) &&
        ppdomain_v1_start(builder, start) &&
        ppdomain_v1_hex_interval(builder, start, digits, low, high);
}

static bool ppdomain_v1_build_hex(
    PPAtomProjectionDomainV1Builder *builder,
    const PPAtomProjectionV1WrapperView *wrapper) {
    uint32_t digits;

    for (digits = wrapper->min_digits;
         digits <= wrapper->max_digits; digits++) {
        uint64_t capacity = ppdomain_v1_pow16(digits) - 1u;
        uint64_t high = wrapper->max_value < capacity
            ? wrapper->max_value : capacity;
        if (high <= UINT64_C(0xd7ff)) {
            if (!ppdomain_v1_hex_interval_root(
                    builder, digits, 0u, high)) {
                return false;
            }
        } else {
            if (!ppdomain_v1_hex_interval_root(
                    builder, digits, 0u, UINT64_C(0xd7ff))) {
                return false;
            }
            if (high >= UINT64_C(0xe000) &&
                !ppdomain_v1_hex_interval_root(
                    builder, digits, UINT64_C(0xe000), high)) {
                return false;
            }
        }
        if (digits == UINT32_MAX)
            break;
    }
    return true;
}

static int ppdomain_v1_u32_compare(const void *left, const void *right) {
    uint32_t lhs = *(const uint32_t *)left;
    uint32_t rhs = *(const uint32_t *)right;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

static bool ppdomain_v1_build_map(
    PPAtomProjectionDomainV1Builder *builder,
    const PPAtomProjectionV1WrapperView *wrapper) {
    uint32_t start;
    uint32_t accept;

    if (!ppdomain_v1_state(builder, &start) ||
        !ppdomain_v1_state(builder, &accept) ||
        !ppdomain_v1_start(builder, start) ||
        !ppdomain_v1_accept(builder, accept)) {
        return false;
    }
    if (wrapper->map_default == PPATOM_PROJECTION_V1_MAP_IDENTITY) {
        static const CettaLpNativeUnicodeRange ranges[] = {
            {0u, UINT32_C(0xd7ff)},
            {UINT32_C(0xe000), UINT32_C(0x10ffff)},
        };
        return ppdomain_v1_edge(
            builder, start, accept, ranges,
            (uint32_t)(sizeof(ranges) / sizeof(ranges[0])));
    }
    {
        uint32_t *values;
        CettaLpNativeUnicodeRange *ranges;
        uint32_t range_len = 0u;
        uint32_t index;
        bool ok;

        if (wrapper->mapping_len == 0u || !wrapper->mappings ||
            (size_t)wrapper->mapping_len > SIZE_MAX / sizeof(*values) ||
            (size_t)wrapper->mapping_len > SIZE_MAX / sizeof(*ranges)) {
            return false;
        }
        values = malloc(sizeof(*values) * (size_t)wrapper->mapping_len);
        ranges = malloc(sizeof(*ranges) * (size_t)wrapper->mapping_len);
        if (!values || !ranges) {
            free(values);
            free(ranges);
            return false;
        }
        for (index = 0u; index < wrapper->mapping_len; index++)
            values[index] = wrapper->mappings[index].source;
        qsort(values, wrapper->mapping_len, sizeof(*values),
              ppdomain_v1_u32_compare);
        for (index = 0u; index < wrapper->mapping_len; index++) {
            if (range_len > 0u &&
                ranges[range_len - 1u].high + 1u == values[index]) {
                ranges[range_len - 1u].high = values[index];
            } else {
                ranges[range_len++] = (CettaLpNativeUnicodeRange){
                    values[index], values[index],
                };
            }
        }
        ok = ppdomain_v1_edge(
            builder, start, accept, ranges, range_len);
        free(values);
        free(ranges);
        return ok;
    }
}

bool ppatom_projection_domain_v1_program(
    const PPAtomProjectionV1Plan *plan,
    uint32_t wrapper_index,
    uint32_t state_limit,
    uint32_t transition_limit,
    RSDFAV1Program *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomProjectionV1WrapperView wrapper;
    PPAtomProjectionDomainV1Builder builder = {0};
    RSDFAV1Nfa nfa;
    RSDFAV1Plan dfa;
    RSDFAV1Program result;
    RSDFAV1BuildOutcome build_outcome = RSDFA_V1_BUILD_COMPLETED;
    bool built;
    bool ok = false;

    rsdfa_v1_plan_init(&dfa);
    rsdfa_v1_program_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || !outcome || state_limit == 0u || transition_limit == 0u ||
        !ppatom_projection_v1_plan_wrapper(
            plan, wrapper_index, &wrapper,
            error_buf, error_buf_size)) {
        if (!error_buf || !error_buf[0]) {
            ppdomain_v1_set_error(error_buf, error_buf_size,
                                  "bad projection-domain arguments");
        }
        goto done;
    }
    builder.state_limit = state_limit;
    builder.transition_limit = transition_limit;
    built = wrapper.kind == PPATOM_PROJECTION_V1_WRAPPER_MAP
        ? ppdomain_v1_build_map(&builder, &wrapper)
        : ppdomain_v1_build_hex(&builder, &wrapper);
    if (!built) {
        if (builder.state_limit_hit || builder.transition_limit_hit) {
            *outcome = builder.state_limit_hit
                ? RSDFA_V1_BUILD_STATE_LIMIT
                : RSDFA_V1_BUILD_TRANSITION_LIMIT;
            ok = true;
            goto done;
        }
        ppdomain_v1_set_error(error_buf, error_buf_size,
                              "failed to construct projection-domain NFA");
        goto done;
    }
    nfa = (RSDFAV1Nfa){
        .state_len = builder.state_len,
        .start_states = builder.starts,
        .start_len = builder.start_len,
        .edges = builder.edges,
        .edge_len = builder.edge_len,
        .accepts = builder.accepts,
        .accept_len = builder.accept_len,
        .tag_len = 1u,
    };
    if (!rsdfa_v1_plan_build(
            &nfa, state_limit, transition_limit,
            &dfa, &build_outcome, error_buf, error_buf_size)) {
        goto done;
    }
    if (build_outcome != RSDFA_V1_BUILD_COMPLETED) {
        *outcome = build_outcome;
        ok = true;
        goto done;
    }
    if (!rsdfa_v1_plan_export_program(
            &dfa, &result, error_buf, error_buf_size)) {
        goto done;
    }
    rsdfa_v1_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    *outcome = RSDFA_V1_BUILD_COMPLETED;
    ok = true;

done:
    rsdfa_v1_program_free(&result);
    rsdfa_v1_plan_free(&dfa);
    ppdomain_v1_builder_free(&builder);
    return ok;
}
