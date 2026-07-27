#include "gslt_direct_reader_v1.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *input;
    size_t input_len;
    size_t pos;
    uint32_t shifts;
} GSLTDirectCursorV1;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} GSLTDirectBytesV1;

typedef struct {
    const GSLTDirectReaderV1Plan *plan;
    GSLTDirectCursorV1 cursor;
    const GSLTDirectAtomProjectionV1 *projection;
    uint32_t tokens;
    uint32_t reductions;
    char *error_buf;
    size_t error_buf_size;
    /* Precomputed per-plan ASCII node-kind dispatch (0=none, else GSLT_DISP_*).
     * Collapses the per-node codepoint comparisons to one table lookup on the
     * common ASCII path; built once per plan and cached (see ascii_dispatch). */
    const uint8_t *ascii_dispatch;
} GSLTDirectStateV1;

enum {
    GSLT_DISP_NONE = 0,
    GSLT_DISP_STRING,
    GSLT_DISP_VAR,
    GSLT_DISP_EXPR,
    GSLT_DISP_WORD
};

static void gslt_direct_reader_v1_error(char *buf, size_t size,
                                        const char *format, ...) {
    va_list arguments;
    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool gslt_direct_reader_v1_cp_in(const uint32_t *points,
                                        uint32_t point_len,
                                        uint32_t codepoint) {
    uint32_t lo = 0u;
    uint32_t hi = point_len;
    while (lo < hi) {
        uint32_t middle = lo + (hi - lo) / 2u;
        if (points[middle] < codepoint)
            lo = middle + 1u;
        else
            hi = middle;
    }
    return lo < point_len && points[lo] == codepoint;
}

static inline bool gslt_direct_reader_v1_class_contains(
    const GSLTDirectScalarClassV1 *scalar_class, uint32_t codepoint) {
    bool found;
    if (!scalar_class || codepoint > UINT32_C(0x10ffff))
        return false;
    if (codepoint < 256u && scalar_class->ascii)
        return scalar_class->ascii[codepoint] != 0u;
    found = gslt_direct_reader_v1_cp_in(
        scalar_class->points, scalar_class->point_len, codepoint);
    return scalar_class->complement ? !found : found;
}

static bool gslt_direct_reader_v1_decode(
    const uint8_t *input, size_t input_len, size_t pos,
    uint32_t *codepoint_out, size_t *width_out) {
    uint8_t first;
    uint32_t codepoint;
    size_t width;

    if (!input || pos >= input_len || !codepoint_out || !width_out)
        return false;
    first = input[pos];
    if (first < UINT8_C(0x80)) {
        *codepoint_out = first;
        *width_out = 1u;
        return true;
    }
    if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
        width = 2u;
        codepoint = first & UINT32_C(0x1f);
    } else if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
        width = 3u;
        codepoint = first & UINT32_C(0x0f);
    } else if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
        width = 4u;
        codepoint = first & UINT32_C(0x07);
    } else {
        return false;
    }
    if (width > input_len - pos)
        return false;
    for (size_t index = 1u; index < width; index++) {
        uint8_t byte = input[pos + index];
        if ((byte & UINT8_C(0xc0)) != UINT8_C(0x80))
            return false;
        codepoint = (codepoint << 6u) | (byte & UINT32_C(0x3f));
    }
    if ((width == 2u && codepoint < UINT32_C(0x80)) ||
        (width == 3u && codepoint < UINT32_C(0x800)) ||
        (width == 4u && codepoint < UINT32_C(0x10000)) ||
        codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) &&
         codepoint <= UINT32_C(0xdfff))) {
        return false;
    }
    *codepoint_out = codepoint;
    *width_out = width;
    return true;
}

static bool gslt_direct_reader_v1_peek(
    const GSLTDirectCursorV1 *cursor, uint32_t *codepoint_out,
    size_t *width_out) {
    return cursor && cursor->pos < cursor->input_len &&
           gslt_direct_reader_v1_decode(
               cursor->input, cursor->input_len, cursor->pos,
               codepoint_out, width_out);
}

static bool gslt_direct_reader_v1_next(
    GSLTDirectCursorV1 *cursor, uint32_t *codepoint_out,
    size_t *width_out) {
    size_t width;
    uint32_t codepoint;
    if (!gslt_direct_reader_v1_peek(cursor, &codepoint, &width))
        return false;
    cursor->pos += width;
    cursor->shifts++;
    if (codepoint_out)
        *codepoint_out = codepoint;
    if (width_out)
        *width_out = width;
    return true;
}

static inline void gslt_direct_reader_v1_consume_width(
    GSLTDirectCursorV1 *cursor, size_t width) {
    cursor->pos += width;
    cursor->shifts++;
}

static bool gslt_direct_reader_v1_literal_contains(
    const uint32_t *values, uint32_t value_len, uint32_t codepoint) {
    for (uint32_t index = 0u; index < value_len; index++)
        if (values[index] == codepoint)
            return true;
    return false;
}

static bool gslt_direct_reader_v1_boundary(
    const GSLTDirectBoundaryV1 *boundary,
    const GSLTDirectCursorV1 *cursor) {
    uint32_t codepoint;
    size_t width;
    if (!boundary || !cursor)
        return false;
    if (cursor->pos == cursor->input_len)
        return boundary->allow_eof;
    if (cursor->input[cursor->pos] < UINT8_C(0x80)) {
        codepoint = cursor->input[cursor->pos];
        width = 1u;
    } else if (!gslt_direct_reader_v1_peek(cursor, &codepoint, &width)) {
        return false;
    }
    (void)width;
    if (gslt_direct_reader_v1_literal_contains(
            boundary->literals, boundary->literal_len, codepoint)) {
        return true;
    }
    for (uint32_t index = 0u; index < boundary->class_len; index++)
        if (gslt_direct_reader_v1_class_contains(
                boundary->classes[index], codepoint)) {
            return true;
        }
    return false;
}

static bool gslt_direct_reader_v1_match_literal(
    GSLTDirectCursorV1 *cursor, const uint32_t *literal,
    uint32_t literal_len) {
    GSLTDirectCursorV1 probe;
    if (!cursor || (literal_len > 0u && !literal))
        return false;
    probe = *cursor;
    for (uint32_t index = 0u; index < literal_len; index++) {
        uint32_t codepoint;
        if (!gslt_direct_reader_v1_next(&probe, &codepoint, NULL) ||
            codepoint != literal[index]) {
            return false;
        }
    }
    *cursor = probe;
    return true;
}

static bool gslt_direct_reader_v1_bytes_reserve(
    GSLTDirectBytesV1 *bytes, size_t extra) {
    size_t needed;
    size_t cap;
    if (!bytes || extra > SIZE_MAX - bytes->len)
        return false;
    needed = bytes->len + extra;
    if (needed <= bytes->cap)
        return true;
    cap = bytes->cap ? bytes->cap : 32u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }
    bytes->data = cetta_realloc(bytes->data, cap);
    bytes->cap = cap;
    return true;
}

static bool gslt_direct_reader_v1_bytes_append(
    GSLTDirectBytesV1 *bytes, const uint8_t *source, size_t source_len) {
    if (!gslt_direct_reader_v1_bytes_reserve(bytes, source_len))
        return false;
    if (source_len > 0u)
        memcpy(bytes->data + bytes->len, source, source_len);
    bytes->len += source_len;
    return true;
}

static bool gslt_direct_reader_v1_bytes_append_codepoint(
    GSLTDirectBytesV1 *bytes, uint32_t codepoint) {
    uint8_t encoded[4];
    size_t len;
    if (codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) &&
         codepoint <= UINT32_C(0xdfff))) {
        return false;
    }
    if (codepoint <= UINT32_C(0x7f)) {
        encoded[0] = (uint8_t)codepoint;
        len = 1u;
    } else if (codepoint <= UINT32_C(0x7ff)) {
        encoded[0] = (uint8_t)(UINT32_C(0xc0) | (codepoint >> 6u));
        encoded[1] = (uint8_t)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
        len = 2u;
    } else if (codepoint <= UINT32_C(0xffff)) {
        encoded[0] = (uint8_t)(UINT32_C(0xe0) | (codepoint >> 12u));
        encoded[1] = (uint8_t)(UINT32_C(0x80) |
                               ((codepoint >> 6u) & UINT32_C(0x3f)));
        encoded[2] = (uint8_t)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
        len = 3u;
    } else {
        encoded[0] = (uint8_t)(UINT32_C(0xf0) | (codepoint >> 18u));
        encoded[1] = (uint8_t)(UINT32_C(0x80) |
                               ((codepoint >> 12u) & UINT32_C(0x3f)));
        encoded[2] = (uint8_t)(UINT32_C(0x80) |
                               ((codepoint >> 6u) & UINT32_C(0x3f)));
        encoded[3] = (uint8_t)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
        len = 4u;
    }
    return gslt_direct_reader_v1_bytes_append(bytes, encoded, len);
}

static int gslt_direct_reader_v1_digit(uint32_t codepoint,
                                       uint32_t radix) {
    uint32_t value;
    if (codepoint >= (uint32_t)'0' && codepoint <= (uint32_t)'9')
        value = codepoint - (uint32_t)'0';
    else if (codepoint >= (uint32_t)'a' && codepoint <= (uint32_t)'z')
        value = codepoint - (uint32_t)'a' + 10u;
    else if (codepoint >= (uint32_t)'A' && codepoint <= (uint32_t)'Z')
        value = codepoint - (uint32_t)'A' + 10u;
    else
        return -1;
    return value < radix ? (int)value : -1;
}

static bool gslt_direct_reader_v1_simple_target(
    const GSLTDirectReaderV1Plan *plan, uint32_t source,
    uint32_t *target_out) {
    for (uint32_t index = 0u; index < plan->simple_map_len; index++) {
        if (plan->simple_map[index].source == source) {
            if (target_out)
                *target_out = plan->simple_map[index].target;
            return true;
        }
    }
    return false;
}

static bool gslt_direct_reader_v1_skip(GSLTDirectStateV1 *state) {
    const GSLTDirectReaderV1Plan *plan = state->plan;
    for (;;) {
        uint32_t codepoint;
        size_t width;
        if (state->cursor.pos == state->cursor.input_len)
            return true;
        if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
            codepoint = state->cursor.input[state->cursor.pos];
            width = 1u;
        } else if (!gslt_direct_reader_v1_peek(
                       &state->cursor, &codepoint, &width)) {
            gslt_direct_reader_v1_error(
                state->error_buf, state->error_buf_size,
                "compiled reader encountered malformed UTF-8");
            return false;
        }
        if (gslt_direct_reader_v1_class_contains(
                plan->whitespace, codepoint)) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            continue;
        }
        if (codepoint != plan->comment_marker)
            return true;
        gslt_direct_reader_v1_consume_width(&state->cursor, width);
        while (state->cursor.pos < state->cursor.input_len) {
            if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
                codepoint = state->cursor.input[state->cursor.pos];
                width = 1u;
            } else if (!gslt_direct_reader_v1_peek(
                           &state->cursor, &codepoint, &width)) {
                gslt_direct_reader_v1_error(
                    state->error_buf, state->error_buf_size,
                    "compiled reader encountered malformed UTF-8");
                return false;
            }
            if (!gslt_direct_reader_v1_class_contains(
                    plan->comment_body, codepoint)) {
                break;
            }
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
        }
        if (!gslt_direct_reader_v1_boundary(
                &plan->comment_boundary, &state->cursor)) {
            gslt_direct_reader_v1_error(
                state->error_buf, state->error_buf_size,
                "compiled reader comment boundary rejected the source");
            return false;
        }
    }
}

static AtomId gslt_direct_reader_v1_parse_atom(
    GSLTDirectStateV1 *state, uint32_t depth);

static AtomId gslt_direct_reader_v1_parse_word(
    GSLTDirectStateV1 *state) {
    size_t start = state->cursor.pos;
    uint32_t codepoint;
    size_t width;
    if (state->cursor.pos >= state->cursor.input_len)
        return CETTA_ATOM_ID_NONE;
    if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
        codepoint = state->cursor.input[state->cursor.pos];
        width = 1u;
    } else if (!gslt_direct_reader_v1_peek(
                   &state->cursor, &codepoint, &width)) {
        return CETTA_ATOM_ID_NONE;
    }
    if (
        !gslt_direct_reader_v1_class_contains(
            state->plan->word_start, codepoint)) {
        return CETTA_ATOM_ID_NONE;
    }
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    while (state->cursor.pos < state->cursor.input_len) {
        if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
            codepoint = state->cursor.input[state->cursor.pos];
            width = 1u;
        } else if (!gslt_direct_reader_v1_peek(
                       &state->cursor, &codepoint, &width)) {
            return CETTA_ATOM_ID_NONE;
        }
        if (!gslt_direct_reader_v1_class_contains(
                state->plan->word_tail, codepoint))
            break;
        gslt_direct_reader_v1_consume_width(&state->cursor, width);
    }
    if (!gslt_direct_reader_v1_boundary(
            &state->plan->word_boundary, &state->cursor))
        return CETTA_ATOM_ID_NONE;
    return state->projection->word_bytes(
        state->projection->context, state->cursor.input + start,
        state->cursor.pos - start);
}

static AtomId gslt_direct_reader_v1_parse_variable(
    GSLTDirectStateV1 *state) {
    size_t start;
    uint32_t codepoint;
    size_t width;
    if (state->cursor.pos >= state->cursor.input_len)
        return CETTA_ATOM_ID_NONE;
    if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
        codepoint = state->cursor.input[state->cursor.pos];
        width = 1u;
    } else if (!gslt_direct_reader_v1_peek(
                   &state->cursor, &codepoint, &width)) {
        return CETTA_ATOM_ID_NONE;
    }
    if (codepoint != state->plan->variable_marker) {
        return CETTA_ATOM_ID_NONE;
    }
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    start = state->cursor.pos;
    if (start >= state->cursor.input_len)
        return CETTA_ATOM_ID_NONE;
    if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
        codepoint = state->cursor.input[state->cursor.pos];
        width = 1u;
    } else if (!gslt_direct_reader_v1_peek(
                   &state->cursor, &codepoint, &width)) {
        return CETTA_ATOM_ID_NONE;
    }
    if (
        !gslt_direct_reader_v1_class_contains(
            state->plan->variable_tail, codepoint)) {
        return CETTA_ATOM_ID_NONE;
    }
    do {
        gslt_direct_reader_v1_consume_width(&state->cursor, width);
        if (state->cursor.pos == state->cursor.input_len)
            break;
        if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
            codepoint = state->cursor.input[state->cursor.pos];
            width = 1u;
        } else if (!gslt_direct_reader_v1_peek(
                       &state->cursor, &codepoint, &width)) {
            return CETTA_ATOM_ID_NONE;
        }
    } while (gslt_direct_reader_v1_class_contains(
        state->plan->variable_tail, codepoint));
    if (!gslt_direct_reader_v1_boundary(
            &state->plan->variable_boundary, &state->cursor))
        return CETTA_ATOM_ID_NONE;
    return state->projection->variable_bytes(
        state->projection->context, state->cursor.input + start,
        state->cursor.pos - start);
}

static bool gslt_direct_reader_v1_parse_byte_escape(
    GSLTDirectStateV1 *state, GSLTDirectBytesV1 *decoded) {
    GSLTDirectCursorV1 probe = state->cursor;
    uint32_t value = 0u;
    if (!gslt_direct_reader_v1_match_literal(
            &probe, state->plan->byte_prefix,
            state->plan->byte_prefix_len))
        return false;
    for (uint32_t index = 0u;
         index < state->plan->byte_digit_class_len; index++) {
        uint32_t codepoint;
        int digit;
        if (!gslt_direct_reader_v1_next(&probe, &codepoint, NULL) ||
            !gslt_direct_reader_v1_class_contains(
                state->plan->byte_digit_classes[index], codepoint) ||
            (digit = gslt_direct_reader_v1_digit(
                 codepoint, state->plan->byte_radix)) < 0) {
            return false;
        }
        if (value > (UINT32_MAX - (uint32_t)digit) /
                        state->plan->byte_radix) {
            return false;
        }
        value = value * state->plan->byte_radix + (uint32_t)digit;
    }
    if (state->plan->byte_digit_class_len < state->plan->byte_hex_min ||
        state->plan->byte_digit_class_len > state->plan->byte_hex_max ||
        value > state->plan->byte_value_max ||
        !gslt_direct_reader_v1_bytes_append_codepoint(decoded, value)) {
        return false;
    }
    state->cursor = probe;
    return true;
}

static bool gslt_direct_reader_v1_parse_unicode_escape(
    GSLTDirectStateV1 *state, GSLTDirectBytesV1 *decoded) {
    GSLTDirectCursorV1 probe = state->cursor;
    uint32_t state_id = 0u;
    uint32_t digits = 0u;
    uint32_t value = 0u;
    if (!gslt_direct_reader_v1_match_literal(
            &probe, state->plan->unicode_prefix,
            state->plan->unicode_prefix_len))
        return false;
    for (;;) {
        uint32_t codepoint;
        size_t width;
        int digit;
        int16_t next_state;
        if (probe.pos >= probe.input_len)
            return false;
        if (probe.input[probe.pos] < UINT8_C(0x80)) {
            codepoint = probe.input[probe.pos];
            width = 1u;
        } else if (!gslt_direct_reader_v1_peek(
                       &probe, &codepoint, &width))
            return false;
        if (codepoint == state->plan->unicode_suffix) {
            if (!state->plan->unicode_accepting[state_id] ||
                digits < state->plan->unicode_hex_min ||
                digits > state->plan->unicode_hex_max ||
                value > state->plan->unicode_value_max ||
                !gslt_direct_reader_v1_bytes_append_codepoint(
                    decoded, value)) {
                return false;
            }
            gslt_direct_reader_v1_consume_width(&probe, width);
            state->cursor = probe;
            return true;
        }
        if (codepoint >= 256u ||
            (digit = gslt_direct_reader_v1_digit(
                 codepoint, state->plan->unicode_radix)) < 0 ||
            digits == state->plan->unicode_hex_max) {
            return false;
        }
        next_state = state->plan->unicode_transitions[
            (size_t)state_id * 256u + codepoint];
        if (next_state < 0 ||
            (uint32_t)next_state >= state->plan->unicode_state_len)
            return false;
        if (value > (UINT32_MAX - (uint32_t)digit) /
                        state->plan->unicode_radix)
            return false;
        value = value * state->plan->unicode_radix + (uint32_t)digit;
        digits++;
        state_id = (uint32_t)next_state;
        gslt_direct_reader_v1_consume_width(&probe, width);
    }
}

static bool gslt_direct_reader_v1_parse_simple_escape(
    GSLTDirectStateV1 *state, GSLTDirectBytesV1 *decoded) {
    GSLTDirectCursorV1 probe = state->cursor;
    uint32_t source;
    uint32_t target;
    if (!gslt_direct_reader_v1_match_literal(
            &probe, state->plan->simple_prefix,
            state->plan->simple_prefix_len) ||
        !gslt_direct_reader_v1_next(&probe, &source, NULL) ||
        !gslt_direct_reader_v1_simple_target(
            state->plan, source, &target) ||
        !gslt_direct_reader_v1_bytes_append_codepoint(decoded, target)) {
        return false;
    }
    state->cursor = probe;
    return true;
}

static AtomId gslt_direct_reader_v1_parse_string(
    GSLTDirectStateV1 *state) {
    GSLTDirectBytesV1 decoded = {0};
    AtomId result = CETTA_ATOM_ID_NONE;
    uint32_t codepoint;
    size_t width;
    if (state->cursor.pos >= state->cursor.input_len)
        goto done;
    if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
        codepoint = state->cursor.input[state->cursor.pos];
        width = 1u;
    } else if (!gslt_direct_reader_v1_peek(
                   &state->cursor, &codepoint, &width)) {
        goto done;
    }
    if (codepoint != state->plan->string_open)
        goto done;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    for (;;) {
        size_t start = state->cursor.pos;
        if (state->cursor.pos >= state->cursor.input_len)
            goto done;
        if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
            codepoint = state->cursor.input[state->cursor.pos];
            width = 1u;
        } else if (!gslt_direct_reader_v1_peek(
                       &state->cursor, &codepoint, &width))
            goto done;
        if (codepoint == state->plan->string_close) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            result = state->projection->string_bytes(
                state->projection->context, decoded.data, decoded.len);
            if (result == CETTA_ATOM_ID_NONE && decoded.len > 0u &&
                memchr(decoded.data, '\0', decoded.len)) {
                gslt_direct_reader_v1_error(
                    state->error_buf, state->error_buf_size,
                    "language projection rejects an embedded NUL");
            }
            goto done;
        }
        if (gslt_direct_reader_v1_class_contains(
                state->plan->string_plain, codepoint)) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            if (!gslt_direct_reader_v1_bytes_append(
                    &decoded, state->cursor.input + start, width))
                goto done;
            continue;
        }
        if (gslt_direct_reader_v1_parse_unicode_escape(state, &decoded) ||
            gslt_direct_reader_v1_parse_byte_escape(state, &decoded) ||
            gslt_direct_reader_v1_parse_simple_escape(state, &decoded)) {
            continue;
        }
        goto done;
    }
done:
    free(decoded.data);
    return result;
}

static AtomId gslt_direct_reader_v1_parse_expression(
    GSLTDirectStateV1 *state, uint32_t depth) {
    AtomId *children = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    AtomId result = CETTA_ATOM_ID_NONE;
    uint32_t codepoint;
    size_t width;
    if (depth == 0u || state->cursor.pos >= state->cursor.input_len)
        goto done;
    if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
        codepoint = state->cursor.input[state->cursor.pos];
        width = 1u;
    } else if (!gslt_direct_reader_v1_peek(
                   &state->cursor, &codepoint, &width)) {
        goto done;
    }
    if (codepoint != state->plan->expression_open)
        goto done;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    for (;;) {
        AtomId child;
        if (!gslt_direct_reader_v1_skip(state))
            goto done;
        if (state->cursor.pos >= state->cursor.input_len)
            goto done;
        if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
            codepoint = state->cursor.input[state->cursor.pos];
            width = 1u;
        } else if (!gslt_direct_reader_v1_peek(
                       &state->cursor, &codepoint, &width))
            goto done;
        if (codepoint == state->plan->expression_close) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            result = state->projection->expression(
                state->projection->context, children, len);
            goto done;
        }
        child = gslt_direct_reader_v1_parse_atom(state, depth - 1u);
        if (child == CETTA_ATOM_ID_NONE)
            goto done;
        if (len == cap) {
            uint32_t next = cap ? cap * 2u : 16u;
            if (next < cap || SIZE_MAX / (size_t)next < sizeof(*children))
                goto done;
            children = cetta_realloc(
                children, (size_t)next * sizeof(*children));
            cap = next;
        }
        children[len++] = child;
    }
done:
    free(children);
    return result;
}

static AtomId gslt_direct_reader_v1_parse_atom(
    GSLTDirectStateV1 *state, uint32_t depth) {
    uint32_t codepoint;
    size_t width;
    AtomId result;
    if (depth == 0u || state->cursor.pos >= state->cursor.input_len)
        return CETTA_ATOM_ID_NONE;
    if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
        /* ASCII fast path: a single indexed dispatch precomputed to preserve
         * the exact branch order (string, variable, expression, word). */
        switch (state->ascii_dispatch[state->cursor.input[state->cursor.pos]]) {
        case GSLT_DISP_STRING:
            result = gslt_direct_reader_v1_parse_string(state);
            break;
        case GSLT_DISP_VAR:
            result = gslt_direct_reader_v1_parse_variable(state);
            break;
        case GSLT_DISP_EXPR:
            result = gslt_direct_reader_v1_parse_expression(state, depth);
            break;
        case GSLT_DISP_WORD:
            result = gslt_direct_reader_v1_parse_word(state);
            break;
        default:
            result = CETTA_ATOM_ID_NONE;
            break;
        }
    } else if (!gslt_direct_reader_v1_peek(&state->cursor, &codepoint, &width)) {
        return CETTA_ATOM_ID_NONE;
    } else if (codepoint == state->plan->string_open) {
        result = gslt_direct_reader_v1_parse_string(state);
    } else if (codepoint == state->plan->variable_marker) {
        result = gslt_direct_reader_v1_parse_variable(state);
    } else if (codepoint == state->plan->expression_open) {
        result = gslt_direct_reader_v1_parse_expression(state, depth);
    } else if (gslt_direct_reader_v1_class_contains(
                   state->plan->word_start, codepoint)) {
        result = gslt_direct_reader_v1_parse_word(state);
    } else {
        result = CETTA_ATOM_ID_NONE;
    }
    if (result != CETTA_ATOM_ID_NONE) {
        state->tokens++;
        state->reductions++;
    }
    return result;
}

static bool gslt_direct_reader_v1_class_valid(
    const GSLTDirectScalarClassV1 *scalar_class) {
    if (!scalar_class || !scalar_class->ascii ||
        (scalar_class->point_len > 0u && !scalar_class->points))
        return false;
    for (uint32_t index = 0u; index < scalar_class->point_len; index++) {
        if (scalar_class->points[index] > UINT32_C(0x10ffff) ||
            (scalar_class->points[index] >= UINT32_C(0xd800) &&
             scalar_class->points[index] <= UINT32_C(0xdfff)) ||
            (index > 0u &&
             scalar_class->points[index - 1u] >=
                 scalar_class->points[index])) {
            return false;
        }
    }
    for (uint32_t codepoint = 0u; codepoint < 256u; codepoint++) {
        bool found = gslt_direct_reader_v1_cp_in(
            scalar_class->points, scalar_class->point_len, codepoint);
        bool expected = scalar_class->complement ? !found : found;
        if (scalar_class->ascii[codepoint] > 1u ||
            (scalar_class->ascii[codepoint] != 0u) != expected)
            return false;
    }
    return true;
}

static bool gslt_direct_reader_v1_scalar_valid(uint32_t codepoint) {
    return codepoint <= UINT32_C(0x10ffff) &&
           !(codepoint >= UINT32_C(0xd800) &&
             codepoint <= UINT32_C(0xdfff));
}

static bool gslt_direct_reader_v1_sha256_valid(const char *digest) {
    if (!digest)
        return false;
    for (size_t index = 0u; index < 64u; index++) {
        char byte = digest[index];
        if (!((byte >= '0' && byte <= '9') ||
              (byte >= 'a' && byte <= 'f'))) {
            return false;
        }
    }
    return digest[64] == '\0';
}

static bool gslt_direct_reader_v1_sequence_valid(
    const uint32_t *sequence, uint32_t sequence_len) {
    if (sequence_len == 0u || !sequence)
        return false;
    for (uint32_t index = 0u; index < sequence_len; index++)
        if (!gslt_direct_reader_v1_scalar_valid(sequence[index]))
            return false;
    return true;
}

static bool gslt_direct_reader_v1_class_disjoint(
    const GSLTDirectScalarClassV1 *left,
    const GSLTDirectScalarClassV1 *right) {
    if (!left || !right)
        return false;
    if (!left->complement) {
        for (uint32_t index = 0u; index < left->point_len; index++)
            if (gslt_direct_reader_v1_class_contains(
                    right, left->points[index]))
                return false;
        return true;
    }
    if (!right->complement) {
        for (uint32_t index = 0u; index < right->point_len; index++)
            if (gslt_direct_reader_v1_class_contains(
                    left, right->points[index]))
                return false;
        return true;
    }
    {
        uint32_t left_index = 0u;
        uint32_t right_index = 0u;
        uint64_t excluded_union_len = 0u;
        const uint64_t scalar_len = UINT64_C(0x110000) - UINT64_C(0x800);
        while (left_index < left->point_len ||
               right_index < right->point_len) {
            uint32_t next;
            if (right_index >= right->point_len ||
                (left_index < left->point_len &&
                 left->points[left_index] < right->points[right_index])) {
                next = left->points[left_index++];
            } else if (left_index >= left->point_len ||
                       right->points[right_index] <
                           left->points[left_index]) {
                next = right->points[right_index++];
            } else {
                next = left->points[left_index];
                left_index++;
                right_index++;
            }
            (void)next;
            excluded_union_len++;
        }
        return excluded_union_len == scalar_len;
    }
}

static bool gslt_direct_reader_v1_boundary_valid(
    const GSLTDirectBoundaryV1 *boundary,
    const GSLTDirectScalarClassV1 *consumed_class) {
    if (!boundary ||
        (boundary->literal_len > 0u && !boundary->literals) ||
        (boundary->class_len > 0u && !boundary->classes) ||
        (!boundary->allow_eof && boundary->literal_len == 0u &&
         boundary->class_len == 0u)) {
        return false;
    }
    for (uint32_t index = 0u; index < boundary->literal_len; index++) {
        uint32_t codepoint = boundary->literals[index];
        if (!gslt_direct_reader_v1_scalar_valid(codepoint) ||
            (index > 0u && boundary->literals[index - 1u] >= codepoint) ||
            (consumed_class && gslt_direct_reader_v1_class_contains(
                                   consumed_class, codepoint))) {
            return false;
        }
    }
    for (uint32_t index = 0u; index < boundary->class_len; index++) {
        const GSLTDirectScalarClassV1 *boundary_class =
            boundary->classes[index];
        if (!gslt_direct_reader_v1_class_valid(boundary_class) ||
            (consumed_class && !gslt_direct_reader_v1_class_disjoint(
                                   consumed_class, boundary_class))) {
            return false;
        }
        for (uint32_t previous = 0u; previous < index; previous++)
            if (boundary->classes[previous] == boundary_class)
                return false;
    }
    return true;
}

static bool gslt_direct_reader_v1_boundary_contains_codepoint(
    const GSLTDirectBoundaryV1 *boundary, uint32_t codepoint) {
    if (gslt_direct_reader_v1_literal_contains(
            boundary->literals, boundary->literal_len, codepoint)) {
        return true;
    }
    for (uint32_t index = 0u; index < boundary->class_len; index++)
        if (gslt_direct_reader_v1_class_contains(
                boundary->classes[index], codepoint)) {
            return true;
        }
    return false;
}

static bool gslt_direct_reader_v1_boundary_disjoint_class(
    const GSLTDirectBoundaryV1 *boundary,
    const GSLTDirectScalarClassV1 *scalar_class) {
    for (uint32_t index = 0u; index < boundary->literal_len; index++)
        if (gslt_direct_reader_v1_class_contains(
                scalar_class, boundary->literals[index])) {
            return false;
        }
    for (uint32_t index = 0u; index < boundary->class_len; index++)
        if (!gslt_direct_reader_v1_class_disjoint(
                boundary->classes[index], scalar_class)) {
            return false;
        }
    return true;
}

static uint32_t gslt_direct_reader_v1_common_prefix_len(
    const uint32_t *left, uint32_t left_len,
    const uint32_t *right, uint32_t right_len) {
    uint32_t length = 0u;
    while (length < left_len && length < right_len &&
           left[length] == right[length]) {
        length++;
    }
    return length;
}

static bool gslt_direct_reader_v1_token_rules_disjoint(
    const GSLTDirectTokenRuleV1 *left,
    const GSLTDirectTokenRuleV1 *right) {
    uint32_t common;
    if (left->literal_len == 0u) {
        if (right->literal_len == 0u)
            return gslt_direct_reader_v1_class_disjoint(
                left->first, right->first);
        return !gslt_direct_reader_v1_class_contains(
            left->first, right->literal[0]);
    }
    if (right->literal_len == 0u)
        return gslt_direct_reader_v1_token_rules_disjoint(right, left);
    common = gslt_direct_reader_v1_common_prefix_len(
        left->literal, left->literal_len,
        right->literal, right->literal_len);
    if (common < (left->literal_len < right->literal_len
                      ? left->literal_len
                      : right->literal_len)) {
        return true;
    }
    if (left->literal_len == right->literal_len) {
        if (!left->first && !right->first)
            return false;
        if (left->first && right->first)
            return gslt_direct_reader_v1_class_disjoint(
                left->first, right->first);
        if (!left->first)
            return gslt_direct_reader_v1_boundary_disjoint_class(
                &left->boundary, right->first);
        return gslt_direct_reader_v1_boundary_disjoint_class(
            &right->boundary, left->first);
    }
    {
        const GSLTDirectTokenRuleV1 *shorter =
            left->literal_len < right->literal_len ? left : right;
        const GSLTDirectTokenRuleV1 *longer =
            left->literal_len < right->literal_len ? right : left;
        uint32_t next = longer->literal[shorter->literal_len];
        if (shorter->first)
            return !gslt_direct_reader_v1_class_contains(
                shorter->first, next);
        return !gslt_direct_reader_v1_boundary_contains_codepoint(
            &shorter->boundary, next);
    }
}

static bool gslt_direct_reader_v1_token_prefix_disjoint(
    const GSLTDirectTokenRuleV1 *token,
    const GSLTDirectPrefixRuleV1 *prefix) {
    uint32_t common;
    if (token->literal_len == 0u)
        return !gslt_direct_reader_v1_class_contains(
            token->first, prefix->literal[0]);
    common = gslt_direct_reader_v1_common_prefix_len(
        token->literal, token->literal_len,
        prefix->literal, prefix->literal_len);
    if (common < (token->literal_len < prefix->literal_len
                      ? token->literal_len
                      : prefix->literal_len)) {
        return true;
    }
    if (token->literal_len < prefix->literal_len) {
        uint32_t next = prefix->literal[token->literal_len];
        if (token->first)
            return !gslt_direct_reader_v1_class_contains(
                token->first, next);
        return !gslt_direct_reader_v1_boundary_contains_codepoint(
            &token->boundary, next);
    }
    if (prefix->literal_len < token->literal_len) {
        uint32_t next = token->literal[prefix->literal_len];
        if (prefix->skip_before_payload)
            return false;
        return !gslt_direct_reader_v1_class_contains(
            prefix->payload_start, next);
    }
    if (prefix->skip_before_payload) {
        if (token->first)
            return false;
        return token->boundary.literal_len == 0u &&
               token->boundary.class_len == 0u;
    }
    if (token->first)
        return gslt_direct_reader_v1_class_disjoint(
            token->first, prefix->payload_start);
    return gslt_direct_reader_v1_boundary_disjoint_class(
        &token->boundary, prefix->payload_start);
}

static bool gslt_direct_reader_v1_map_valid(
    const GSLTDirectCodepointMapV1 *map, uint32_t map_len) {
    if (!map || map_len == 0u)
        return false;
    for (uint32_t index = 0u; index < map_len; index++) {
        if (!gslt_direct_reader_v1_scalar_valid(map[index].source) ||
            !gslt_direct_reader_v1_scalar_valid(map[index].target)) {
            return false;
        }
        for (uint32_t previous = 0u; previous < index; previous++)
            if (map[previous].source == map[index].source)
                return false;
    }
    return true;
}

static bool gslt_direct_reader_v1_sequence_prefix(
    const uint32_t *prefix, uint32_t prefix_len,
    const uint32_t *sequence, uint32_t sequence_len) {
    return prefix_len <= sequence_len &&
           memcmp(prefix, sequence,
                  (size_t)prefix_len * sizeof(*prefix)) == 0;
}

static bool gslt_direct_reader_v1_specialized_escape_distinct(
    const GSLTDirectReaderV1Plan *plan, const uint32_t *prefix,
    uint32_t prefix_len) {
    uint32_t ignored;
    if (!gslt_direct_reader_v1_sequence_prefix(
            plan->simple_prefix, plan->simple_prefix_len,
            prefix, prefix_len))
        return true;
    if (prefix_len == plan->simple_prefix_len)
        return false;
    return !gslt_direct_reader_v1_simple_target(
        plan, prefix[plan->simple_prefix_len], &ignored);
}

bool gslt_direct_reader_v1_plan_validate(
    const GSLTDirectReaderV1Plan *plan, char *error_buf,
    size_t error_buf_size) {
    const GSLTDirectScalarClassV1 *required_classes[6];
    bool has_unicode_accepting_state = false;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !plan->presentation_name || plan->presentation_name[0] == '\0' ||
        !plan->fragment ||
        strcmp(plan->fragment, GSLT_DIRECT_READER_V1_FRAGMENT) != 0 ||
        !gslt_direct_reader_v1_sha256_valid(plan->syntax_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->class_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->projection_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->compiler_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->composition_digest) ||
        !plan->profile || plan->profile[0] == '\0' ||
        plan->depth_limit == 0u ||
        !gslt_direct_reader_v1_scalar_valid(plan->expression_open) ||
        !gslt_direct_reader_v1_scalar_valid(plan->expression_close) ||
        !gslt_direct_reader_v1_scalar_valid(plan->string_open) ||
        !gslt_direct_reader_v1_scalar_valid(plan->string_close) ||
        !gslt_direct_reader_v1_scalar_valid(plan->variable_marker) ||
        !gslt_direct_reader_v1_scalar_valid(plan->comment_marker)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size, "invalid direct-reader plan header");
        return false;
    }
    required_classes[0] = plan->whitespace;
    required_classes[1] = plan->comment_body;
    required_classes[2] = plan->word_start;
    required_classes[3] = plan->word_tail;
    required_classes[4] = plan->variable_tail;
    required_classes[5] = plan->string_plain;
    for (size_t index = 0u;
         index < sizeof(required_classes) / sizeof(required_classes[0]);
         index++) {
        if (!gslt_direct_reader_v1_class_valid(required_classes[index])) {
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "invalid source-derived scalar class");
            return false;
        }
    }
    if (!gslt_direct_reader_v1_boundary_valid(
            &plan->comment_boundary, plan->comment_body) ||
        !gslt_direct_reader_v1_boundary_valid(
            &plan->word_boundary, plan->word_tail) ||
        !gslt_direct_reader_v1_boundary_valid(
            &plan->variable_boundary, plan->variable_tail)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "invalid source-derived token boundary");
        return false;
    }
    if (!gslt_direct_reader_v1_sequence_valid(
            plan->simple_prefix, plan->simple_prefix_len) ||
        !gslt_direct_reader_v1_map_valid(
            plan->simple_map, plan->simple_map_len) ||
        !gslt_direct_reader_v1_sequence_valid(
            plan->byte_prefix, plan->byte_prefix_len) ||
        !plan->byte_digit_classes || plan->byte_digit_class_len == 0u ||
        plan->byte_hex_min > plan->byte_hex_max ||
        plan->byte_digit_class_len < plan->byte_hex_min ||
        plan->byte_digit_class_len > plan->byte_hex_max ||
        plan->byte_radix < 2u || plan->byte_radix > 36u ||
        plan->byte_value_max > UINT32_C(0x10ffff) ||
        !gslt_direct_reader_v1_sequence_valid(
            plan->unicode_prefix, plan->unicode_prefix_len) ||
        !gslt_direct_reader_v1_scalar_valid(plan->unicode_suffix) ||
        !plan->unicode_transitions || !plan->unicode_accepting ||
        plan->unicode_state_len == 0u ||
        plan->unicode_state_len > (uint32_t)INT16_MAX + 1u ||
        plan->unicode_hex_min > plan->unicode_hex_max ||
        plan->unicode_value_max > UINT32_C(0x10ffff) ||
        plan->unicode_radix < 2u || plan->unicode_radix > 36u) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "invalid source-derived escape plan");
        return false;
    }
    for (uint32_t index = 0u;
         index < plan->byte_digit_class_len; index++) {
        if (!gslt_direct_reader_v1_class_valid(
                plan->byte_digit_classes[index])) {
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "invalid byte-escape digit class");
            return false;
        }
    }
    for (uint32_t state = 0u; state < plan->unicode_state_len; state++) {
        if (plan->unicode_accepting[state] > 1u) {
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "invalid Unicode residual acceptance marker");
            return false;
        }
        has_unicode_accepting_state =
            has_unicode_accepting_state ||
            plan->unicode_accepting[state] != 0u;
        for (uint32_t codepoint = 0u; codepoint < 256u; codepoint++) {
            int16_t target = plan->unicode_transitions[
                (size_t)state * 256u + codepoint];
            if (target < -1 ||
                (target >= 0 &&
                 ((uint32_t)target >= plan->unicode_state_len ||
                  gslt_direct_reader_v1_digit(
                      codepoint, plan->unicode_radix) < 0))) {
                gslt_direct_reader_v1_error(
                    error_buf, error_buf_size,
                    "invalid Unicode residual transition");
                return false;
            }
        }
    }
    if (!has_unicode_accepting_state ||
        (plan->unicode_hex_min > 0u && plan->unicode_accepting[0] != 0u)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "invalid Unicode residual acceptance language");
        return false;
    }
    if (!gslt_direct_reader_v1_specialized_escape_distinct(
            plan, plan->byte_prefix, plan->byte_prefix_len) ||
        !gslt_direct_reader_v1_specialized_escape_distinct(
            plan, plan->unicode_prefix, plan->unicode_prefix_len) ||
        gslt_direct_reader_v1_sequence_prefix(
            plan->byte_prefix, plan->byte_prefix_len,
            plan->unicode_prefix, plan->unicode_prefix_len) ||
        gslt_direct_reader_v1_sequence_prefix(
            plan->unicode_prefix, plan->unicode_prefix_len,
            plan->byte_prefix, plan->byte_prefix_len)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "direct-reader escape dispatch is ambiguous");
        return false;
    }
    if (plan->expression_open == plan->expression_close ||
        plan->expression_open == plan->string_open ||
        plan->expression_open == plan->string_close ||
        plan->expression_open == plan->variable_marker ||
        plan->expression_open == plan->comment_marker ||
        plan->expression_close == plan->string_open ||
        plan->expression_close == plan->string_close ||
        plan->expression_close == plan->variable_marker ||
        plan->expression_close == plan->comment_marker ||
        plan->string_open == plan->variable_marker ||
        plan->string_open == plan->comment_marker ||
        plan->string_close == plan->variable_marker ||
        plan->string_close == plan->comment_marker ||
        plan->variable_marker == plan->comment_marker ||
        gslt_direct_reader_v1_class_contains(
            plan->word_start, plan->expression_open) ||
        gslt_direct_reader_v1_class_contains(
            plan->word_start, plan->expression_close) ||
        gslt_direct_reader_v1_class_contains(
            plan->word_start, plan->string_open) ||
        gslt_direct_reader_v1_class_contains(
            plan->word_start, plan->string_close) ||
        gslt_direct_reader_v1_class_contains(
            plan->word_start, plan->variable_marker) ||
        gslt_direct_reader_v1_class_contains(
            plan->word_start, plan->comment_marker) ||
        gslt_direct_reader_v1_class_contains(
            plan->whitespace, plan->expression_open) ||
        gslt_direct_reader_v1_class_contains(
            plan->whitespace, plan->expression_close) ||
        gslt_direct_reader_v1_class_contains(
            plan->whitespace, plan->string_open) ||
        gslt_direct_reader_v1_class_contains(
            plan->whitespace, plan->string_close) ||
        gslt_direct_reader_v1_class_contains(
            plan->whitespace, plan->variable_marker) ||
        gslt_direct_reader_v1_class_contains(
            plan->whitespace, plan->comment_marker) ||
        gslt_direct_reader_v1_class_contains(
            plan->string_plain, plan->string_close) ||
        gslt_direct_reader_v1_class_contains(
            plan->string_plain, plan->simple_prefix[0]) ||
        gslt_direct_reader_v1_class_contains(
            plan->string_plain, plan->byte_prefix[0]) ||
        gslt_direct_reader_v1_class_contains(
            plan->string_plain, plan->unicode_prefix[0])) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "direct-reader atom dispatch is ambiguous");
        return false;
    }
    return true;
}

/* Build (once per plan, thread-locally cached) the ASCII node-kind dispatch
 * table: for each byte 0..255, which parse_atom branch it selects.  This turns
 * the per-node chain of codepoint comparisons + word-class membership into a
 * single indexed load on the common ASCII path, preserving the exact branch
 * order (string_open, variable_marker, expression_open, then word_start). */
static const uint8_t *gslt_direct_reader_v1_ascii_dispatch(
    const GSLTDirectReaderV1Plan *plan) {
    enum { GSLT_DISP_CACHE_SLOTS = 8 };
    static _Thread_local const GSLTDirectReaderV1Plan
        *gslt_disp_cache_keys[GSLT_DISP_CACHE_SLOTS];
    static _Thread_local uint8_t
        gslt_disp_cache_tables[GSLT_DISP_CACHE_SLOTS][256];
    static _Thread_local unsigned gslt_disp_cache_next;
    unsigned i;
    unsigned slot;
    uint8_t *table;
    for (i = 0u; i < GSLT_DISP_CACHE_SLOTS; i++) {
        if (gslt_disp_cache_keys[i] == plan)
            return gslt_disp_cache_tables[i];
    }
    slot = gslt_disp_cache_next % GSLT_DISP_CACHE_SLOTS;
    gslt_disp_cache_next++;
    table = gslt_disp_cache_tables[slot];
    for (i = 0u; i < 256u; i++) {
        uint8_t kind = (uint8_t)GSLT_DISP_NONE;
        if (i == plan->string_open)
            kind = (uint8_t)GSLT_DISP_STRING;
        else if (i == plan->variable_marker)
            kind = (uint8_t)GSLT_DISP_VAR;
        else if (i == plan->expression_open)
            kind = (uint8_t)GSLT_DISP_EXPR;
        else if (gslt_direct_reader_v1_class_contains(plan->word_start, i))
            kind = (uint8_t)GSLT_DISP_WORD;
        table[i] = kind;
    }
    gslt_disp_cache_keys[slot] = plan;
    return table;
}

int gslt_direct_reader_v1_parse_bytes_ids_impl(
    const GSLTDirectReaderV1Plan *plan, const uint8_t *input,
    size_t input_len, const GSLTDirectAtomProjectionV1 *projection,
    AtomId **out_ids,
    GSLTDirectReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size) {
    GSLTDirectStateV1 state;
    AtomId *ids = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    int result = -1;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_ids)
        *out_ids = NULL;
    if (receipt)
        memset(receipt, 0, sizeof(*receipt));
    if (!plan || !projection || !projection->context ||
        !projection->begin_form || !projection->word_bytes ||
        !projection->variable_bytes || !projection->string_bytes ||
        !projection->expression || !out_ids || input_len > UINT32_MAX ||
        (input_len > 0u && !input)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size, "invalid direct-reader invocation");
        return -1;
    }
    memset(&state, 0, sizeof(state));
    state.plan = plan;
    state.cursor.input = input;
    state.cursor.input_len = input_len;
    state.error_buf = error_buf;
    state.error_buf_size = error_buf_size;
    state.projection = projection;
    state.ascii_dispatch = gslt_direct_reader_v1_ascii_dispatch(plan);
    for (;;) {
        AtomId id;
        if (!gslt_direct_reader_v1_skip(&state))
            goto done;
        if (state.cursor.pos == input_len)
            break;
        if (!state.projection->begin_form(state.projection->context))
            goto done;
        id = gslt_direct_reader_v1_parse_atom(&state, plan->depth_limit);
        if (id == CETTA_ATOM_ID_NONE)
            goto done;
        if (len == cap) {
            uint32_t next = cap ? cap * 2u : 64u;
            if (next < cap || SIZE_MAX / (size_t)next < sizeof(*ids))
                goto done;
            ids = cetta_realloc(ids, (size_t)next * sizeof(*ids));
            cap = next;
        }
        ids[len++] = id;
    }
    if (len > (uint32_t)INT_MAX)
        goto done;
    if (receipt) {
        receipt->source_pass_count = 1u;
        receipt->input_byte_len = (uint32_t)input_len;
        receipt->token_len = state.tokens;
        receipt->shift_len = state.cursor.shifts;
        receipt->reduce_len = state.reductions;
    }
    *out_ids = ids;
    ids = NULL;
    result = (int)len;

done:
    if (result < 0 && error_buf && error_buf_size > 0u &&
        error_buf[0] == '\0') {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "source is outside the compiled S-expression LanguageDef");
    }
    free(ids);
    return result;
}

int gslt_direct_reader_v1_parse_bytes_ids(
    const GSLTDirectReaderV1Plan *plan, const uint8_t *input,
    size_t input_len, const GSLTDirectAtomProjectionV1 *projection,
    AtomId **out_ids,
    GSLTDirectReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size) {
    return gslt_direct_reader_v1_parse_bytes_ids_impl(
        plan, input, input_len, projection, out_ids, receipt,
        error_buf, error_buf_size);
}

typedef struct {
    const GSLTDirectPrefixReaderV1Plan *plan;
    GSLTDirectCursorV1 cursor;
    const GSLTDirectPrefixProjectionV1 *projection;
    uint32_t tokens;
    uint32_t prefixes;
    uint32_t reductions;
    char *error_buf;
    size_t error_buf_size;
} GSLTDirectPrefixStateV1;

static bool gslt_direct_prefix_skip(GSLTDirectPrefixStateV1 *state) {
    const GSLTDirectPrefixReaderV1Plan *plan = state->plan;
    for (;;) {
        uint32_t codepoint;
        size_t width;
        if (state->cursor.pos == state->cursor.input_len)
            return true;
        if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
            codepoint = state->cursor.input[state->cursor.pos];
            width = 1u;
        } else if (!gslt_direct_reader_v1_peek(
                       &state->cursor, &codepoint, &width)) {
            gslt_direct_reader_v1_error(
                state->error_buf, state->error_buf_size,
                "compiled prefix reader encountered malformed UTF-8");
            return false;
        }
        if (gslt_direct_reader_v1_class_contains(
                plan->whitespace, codepoint)) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            continue;
        }
        if (codepoint != plan->comment_marker)
            return true;
        gslt_direct_reader_v1_consume_width(&state->cursor, width);
        while (state->cursor.pos < state->cursor.input_len) {
            if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
                codepoint = state->cursor.input[state->cursor.pos];
                width = 1u;
            } else if (!gslt_direct_reader_v1_peek(
                           &state->cursor, &codepoint, &width)) {
                gslt_direct_reader_v1_error(
                    state->error_buf, state->error_buf_size,
                    "compiled prefix reader encountered malformed UTF-8");
                return false;
            }
            if (!gslt_direct_reader_v1_class_contains(
                    plan->comment_body, codepoint))
                break;
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
        }
        if (!gslt_direct_reader_v1_boundary(
                &plan->comment_boundary, &state->cursor)) {
            gslt_direct_reader_v1_error(
                state->error_buf, state->error_buf_size,
                "compiled prefix reader comment boundary rejected the source");
            return false;
        }
    }
}

static bool gslt_direct_prefix_escape_target(
    const GSLTDirectPrefixReaderV1Plan *plan, uint32_t source,
    uint32_t *target_out) {
    for (uint32_t index = 0u; index < plan->string_escape_map_len; index++) {
        if (plan->string_escape_map[index].source == source) {
            if (target_out)
                *target_out = plan->string_escape_map[index].target;
            return true;
        }
    }
    if (plan->string_escape_identity_fallback) {
        if (target_out)
            *target_out = source;
        return true;
    }
    return false;
}

static AtomId gslt_direct_prefix_parse_atom(
    GSLTDirectPrefixStateV1 *state, uint32_t depth);

static AtomId gslt_direct_prefix_parse_string(
    GSLTDirectPrefixStateV1 *state) {
    GSLTDirectBytesV1 decoded = {0};
    AtomId result = CETTA_ATOM_ID_NONE;
    uint32_t codepoint;
    size_t width;
    if (state->cursor.pos >= state->cursor.input_len)
        goto done;
    if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
        codepoint = state->cursor.input[state->cursor.pos];
        width = 1u;
    } else if (!gslt_direct_reader_v1_peek(
                   &state->cursor, &codepoint, &width)) {
        goto done;
    }
    if (codepoint != state->plan->string_open)
        goto done;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    for (;;) {
        size_t start = state->cursor.pos;
        if (state->cursor.pos >= state->cursor.input_len)
            goto done;
        if (state->cursor.input[state->cursor.pos] < UINT8_C(0x80)) {
            codepoint = state->cursor.input[state->cursor.pos];
            width = 1u;
        } else if (!gslt_direct_reader_v1_peek(
                       &state->cursor, &codepoint, &width)) {
            goto done;
        }
        if (codepoint == state->plan->string_close) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            result = state->projection->string_bytes(
                state->projection->context, decoded.data, decoded.len);
            if (result == CETTA_ATOM_ID_NONE && decoded.len > 0u &&
                memchr(decoded.data, '\0', decoded.len)) {
                gslt_direct_reader_v1_error(
                    state->error_buf, state->error_buf_size,
                    "language projection rejects an embedded NUL");
            }
            goto done;
        }
        if (gslt_direct_reader_v1_class_contains(
                state->plan->string_plain, codepoint)) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            if (!gslt_direct_reader_v1_bytes_append(
                    &decoded, state->cursor.input + start, width))
                goto done;
            continue;
        }
        {
            GSLTDirectCursorV1 probe = state->cursor;
            uint32_t source;
            uint32_t target;
            if (!gslt_direct_reader_v1_match_literal(
                    &probe, state->plan->string_escape_prefix,
                    state->plan->string_escape_prefix_len) ||
                !gslt_direct_reader_v1_next(&probe, &source, NULL) ||
                !gslt_direct_reader_v1_class_contains(
                    state->plan->string_escape_source, source) ||
                !gslt_direct_prefix_escape_target(
                    state->plan, source, &target) ||
                !gslt_direct_reader_v1_bytes_append_codepoint(
                    &decoded, target)) {
                goto done;
            }
            state->cursor = probe;
        }
    }
done:
    free(decoded.data);
    return result;
}

static AtomId gslt_direct_prefix_parse_token(
    GSLTDirectPrefixStateV1 *state) {
    for (uint32_t rule_index = 0u;
         rule_index < state->plan->token_rule_len; rule_index++) {
        const GSLTDirectTokenRuleV1 *rule =
            &state->plan->token_rules[rule_index];
        GSLTDirectCursorV1 probe = state->cursor;
        size_t start = probe.pos;
        size_t payload_start;
        AtomId result;
        uint32_t codepoint;
        size_t width;
        if (rule->literal_len > 0u &&
            !gslt_direct_reader_v1_match_literal(
                &probe, rule->literal, rule->literal_len))
            continue;
        payload_start = probe.pos;
        if (rule->first) {
            if (!gslt_direct_reader_v1_peek(
                    &probe, &codepoint, &width) ||
                !gslt_direct_reader_v1_class_contains(
                    rule->first, codepoint))
                continue;
            gslt_direct_reader_v1_consume_width(&probe, width);
            while (probe.pos < probe.input_len) {
                if (!gslt_direct_reader_v1_peek(
                        &probe, &codepoint, &width))
                    return CETTA_ATOM_ID_NONE;
                if (!gslt_direct_reader_v1_class_contains(
                        rule->tail, codepoint))
                    break;
                gslt_direct_reader_v1_consume_width(&probe, width);
            }
        }
        if (!gslt_direct_reader_v1_boundary(&rule->boundary, &probe))
            continue;
        if (rule->projection == GSLT_DIRECT_TOKEN_PROJECTION_WORD) {
            result = state->projection->word_bytes(
                state->projection->context,
                probe.input + (rule->strip_literal ? payload_start : start),
                probe.pos - (rule->strip_literal ? payload_start : start));
        } else if (rule->projection ==
                   GSLT_DIRECT_TOKEN_PROJECTION_VARIABLE) {
            result = state->projection->variable_bytes(
                state->projection->context,
                probe.input + (rule->strip_literal ? payload_start : start),
                probe.pos - (rule->strip_literal ? payload_start : start));
        } else {
            result = state->projection->anonymous_variable(
                state->projection->context);
        }
        if (result == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
        state->cursor = probe;
        return result;
    }
    return CETTA_ATOM_ID_NONE;
}

static AtomId gslt_direct_prefix_parse_prefix(
    GSLTDirectPrefixStateV1 *state, uint32_t depth) {
    for (uint32_t rule_index = 0u;
         rule_index < state->plan->prefix_rule_len; rule_index++) {
        const GSLTDirectPrefixRuleV1 *rule =
            &state->plan->prefix_rules[rule_index];
        GSLTDirectCursorV1 probe = state->cursor;
        AtomId payload;
        AtomId result;
        uint32_t codepoint;
        size_t width;
        if (!gslt_direct_reader_v1_match_literal(
                &probe, rule->literal, rule->literal_len))
            continue;
        if (rule->payload_start &&
            (!gslt_direct_reader_v1_peek(
                 &probe, &codepoint, &width) ||
             !gslt_direct_reader_v1_class_contains(
                 rule->payload_start, codepoint))) {
            continue;
        }
        state->cursor = probe;
        if (rule->skip_before_payload && !gslt_direct_prefix_skip(state))
            return CETTA_ATOM_ID_NONE;
        payload = gslt_direct_prefix_parse_atom(state, depth - 1u);
        if (payload == CETTA_ATOM_ID_NONE) {
            gslt_direct_reader_v1_error(
                state->error_buf, state->error_buf_size,
                "compiled prefix reader rejected a prefix payload");
            return CETTA_ATOM_ID_NONE;
        }
        result = state->projection->prefix(
            state->projection->context, rule->role, payload);
        if (result == CETTA_ATOM_ID_NONE) {
            gslt_direct_reader_v1_error(
                state->error_buf, state->error_buf_size,
                "language projection rejected a prefix payload");
            return CETTA_ATOM_ID_NONE;
        }
        state->prefixes++;
        return result;
    }
    return CETTA_ATOM_ID_NONE;
}

static AtomId gslt_direct_prefix_parse_expression(
    GSLTDirectPrefixStateV1 *state, uint32_t depth) {
    AtomId *children = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    AtomId result = CETTA_ATOM_ID_NONE;
    uint32_t codepoint;
    size_t width;
    if (depth == 0u || state->cursor.pos >= state->cursor.input_len)
        goto done;
    if (!gslt_direct_reader_v1_peek(
            &state->cursor, &codepoint, &width) ||
        codepoint != state->plan->expression_open)
        goto done;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    for (;;) {
        AtomId child;
        if (!gslt_direct_prefix_skip(state))
            goto done;
        if (state->cursor.pos >= state->cursor.input_len ||
            !gslt_direct_reader_v1_peek(
                &state->cursor, &codepoint, &width))
            goto done;
        if (codepoint == state->plan->expression_close) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            result = state->projection->expression(
                state->projection->context, children, len);
            goto done;
        }
        child = gslt_direct_prefix_parse_atom(state, depth - 1u);
        if (child == CETTA_ATOM_ID_NONE)
            goto done;
        if (len == cap) {
            uint32_t next = cap ? cap * 2u : 16u;
            if (next < cap || SIZE_MAX / (size_t)next < sizeof(*children))
                goto done;
            children = cetta_realloc(
                children, (size_t)next * sizeof(*children));
            cap = next;
        }
        children[len++] = child;
    }
done:
    free(children);
    return result;
}

static AtomId gslt_direct_prefix_parse_atom(
    GSLTDirectPrefixStateV1 *state, uint32_t depth) {
    AtomId result;
    uint32_t codepoint;
    size_t width;
    if (depth == 0u || state->cursor.pos >= state->cursor.input_len ||
        !gslt_direct_reader_v1_peek(
            &state->cursor, &codepoint, &width))
        return CETTA_ATOM_ID_NONE;
    if (codepoint == state->plan->string_open)
        result = gslt_direct_prefix_parse_string(state);
    else if (codepoint == state->plan->expression_open)
        result = gslt_direct_prefix_parse_expression(state, depth);
    else {
        result = gslt_direct_prefix_parse_prefix(state, depth);
        if (result == CETTA_ATOM_ID_NONE &&
            (!state->error_buf || state->error_buf_size == 0u ||
             state->error_buf[0] == '\0')) {
            result = gslt_direct_prefix_parse_token(state);
        }
    }
    if (result != CETTA_ATOM_ID_NONE) {
        state->tokens++;
        state->reductions++;
    }
    return result;
}

bool gslt_direct_prefix_reader_v1_plan_validate(
    const GSLTDirectPrefixReaderV1Plan *plan, char *error_buf,
    size_t error_buf_size) {
    const GSLTDirectScalarClassV1 *required_classes[4];
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !plan->presentation_name ||
        plan->presentation_name[0] == '\0' || !plan->fragment ||
        strcmp(plan->fragment, GSLT_DIRECT_PREFIX_READER_V1_FRAGMENT) != 0 ||
        !gslt_direct_reader_v1_sha256_valid(plan->syntax_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->class_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->projection_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->compiler_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->composition_digest) ||
        !plan->profile || plan->profile[0] == '\0' ||
        plan->depth_limit == 0u || !plan->prefix_rules ||
        plan->prefix_rule_len == 0u || !plan->token_rules ||
        plan->token_rule_len == 0u ||
        !gslt_direct_reader_v1_scalar_valid(plan->comment_marker) ||
        !gslt_direct_reader_v1_scalar_valid(plan->expression_open) ||
        !gslt_direct_reader_v1_scalar_valid(plan->expression_close) ||
        !gslt_direct_reader_v1_scalar_valid(plan->string_open) ||
        !gslt_direct_reader_v1_scalar_valid(plan->string_close)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "invalid direct prefix-reader plan header");
        return false;
    }
    required_classes[0] = plan->whitespace;
    required_classes[1] = plan->comment_body;
    required_classes[2] = plan->string_plain;
    required_classes[3] = plan->string_escape_source;
    for (size_t index = 0u;
         index < sizeof(required_classes) / sizeof(required_classes[0]);
         index++) {
        if (!gslt_direct_reader_v1_class_valid(required_classes[index])) {
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "invalid prefix-reader scalar class");
            return false;
        }
    }
    if (!gslt_direct_reader_v1_boundary_valid(
            &plan->comment_boundary, plan->comment_body) ||
        !gslt_direct_reader_v1_sequence_valid(
            plan->string_escape_prefix,
            plan->string_escape_prefix_len) ||
        !gslt_direct_reader_v1_map_valid(
            plan->string_escape_map, plan->string_escape_map_len) ||
        gslt_direct_reader_v1_class_contains(
            plan->string_plain, plan->string_close) ||
        gslt_direct_reader_v1_class_contains(
            plan->string_plain, plan->string_escape_prefix[0]) ||
        plan->expression_open == plan->expression_close ||
        plan->expression_open == plan->string_open ||
        plan->expression_open == plan->string_close ||
        plan->expression_open == plan->comment_marker ||
        plan->expression_close == plan->string_open ||
        plan->expression_close == plan->string_close ||
        plan->expression_close == plan->comment_marker ||
        plan->string_open == plan->comment_marker ||
        plan->string_close == plan->comment_marker) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "invalid prefix-reader delimiter or escape partition");
        return false;
    }
    for (uint32_t index = 0u; index < plan->prefix_rule_len; index++) {
        const GSLTDirectPrefixRuleV1 *rule = &plan->prefix_rules[index];
        if (!gslt_direct_reader_v1_sequence_valid(
                rule->literal, rule->literal_len) ||
            rule->role <= GSLT_DIRECT_PREFIX_ROLE_INVALID ||
            rule->role > GSLT_DIRECT_PREFIX_ROLE_RESOLVE_NAME ||
            (rule->payload_start &&
             !gslt_direct_reader_v1_class_valid(rule->payload_start)) ||
            ((rule->payload_start != NULL) ==
             rule->skip_before_payload) ||
            rule->literal[0] == plan->expression_open ||
            rule->literal[0] == plan->expression_close ||
            rule->literal[0] == plan->string_open ||
            rule->literal[0] == plan->string_close ||
            rule->literal[0] == plan->comment_marker) {
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "invalid source-derived prefix rule");
            return false;
        }
        for (uint32_t previous = 0u; previous < index; previous++) {
            if (plan->prefix_rules[previous].literal[0] ==
                rule->literal[0]) {
                gslt_direct_reader_v1_error(
                    error_buf, error_buf_size,
                    "prefix dispatch has overlapping leading scalars");
                return false;
            }
        }
    }
    for (uint32_t index = 0u; index < plan->token_rule_len; index++) {
        const GSLTDirectTokenRuleV1 *rule = &plan->token_rules[index];
        bool has_literal = rule->literal_len > 0u;
        bool has_classes = rule->first && rule->tail;
        if ((has_literal && !gslt_direct_reader_v1_sequence_valid(
                                rule->literal, rule->literal_len)) ||
            (!has_literal && !has_classes) ||
            ((rule->first == NULL) != (rule->tail == NULL)) ||
            (has_classes &&
             (!gslt_direct_reader_v1_class_valid(rule->first) ||
              !gslt_direct_reader_v1_class_valid(rule->tail))) ||
            !gslt_direct_reader_v1_boundary_valid(
                &rule->boundary, rule->tail) ||
            rule->projection <= GSLT_DIRECT_TOKEN_PROJECTION_INVALID ||
            rule->projection >
                GSLT_DIRECT_TOKEN_PROJECTION_ANONYMOUS_VARIABLE ||
            (rule->projection ==
                 GSLT_DIRECT_TOKEN_PROJECTION_ANONYMOUS_VARIABLE &&
             (!has_literal || has_classes || rule->strip_literal)) ||
            (rule->strip_literal && !has_literal)) {
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "invalid source-derived token rule");
            return false;
        }
        if (!has_literal) {
            if (gslt_direct_reader_v1_class_contains(
                    rule->first, plan->expression_open) ||
                gslt_direct_reader_v1_class_contains(
                    rule->first, plan->expression_close) ||
                gslt_direct_reader_v1_class_contains(
                    rule->first, plan->string_open) ||
                gslt_direct_reader_v1_class_contains(
                    rule->first, plan->string_close) ||
                gslt_direct_reader_v1_class_contains(
                    rule->first, plan->comment_marker)) {
                gslt_direct_reader_v1_error(
                    error_buf, error_buf_size,
                    "generic token dispatch overlaps a structural scalar");
                return false;
            }
            for (uint32_t prefix = 0u;
                 prefix < plan->prefix_rule_len; prefix++) {
                if (gslt_direct_reader_v1_class_contains(
                        rule->first,
                        plan->prefix_rules[prefix].literal[0])) {
                    gslt_direct_reader_v1_error(
                        error_buf, error_buf_size,
                        "generic token dispatch overlaps a prefix scalar");
                    return false;
                }
            }
        }
        for (uint32_t previous = 0u; previous < index; previous++) {
            if (!gslt_direct_reader_v1_token_rules_disjoint(
                    &plan->token_rules[previous], rule)) {
                gslt_direct_reader_v1_error(
                    error_buf, error_buf_size,
                    "token alternatives have overlapping dispatch");
                return false;
            }
        }
        for (uint32_t prefix = 0u;
             prefix < plan->prefix_rule_len; prefix++) {
            if (!gslt_direct_reader_v1_token_prefix_disjoint(
                    rule, &plan->prefix_rules[prefix])) {
                gslt_direct_reader_v1_error(
                    error_buf, error_buf_size,
                    "token dispatch overlaps a prefix alternative");
                return false;
            }
        }
    }
    return true;
}

int gslt_direct_prefix_reader_v1_parse_bytes_ids(
    const GSLTDirectPrefixReaderV1Plan *plan, const uint8_t *input,
    size_t input_len, const GSLTDirectPrefixProjectionV1 *projection,
    AtomId **out_ids, GSLTDirectPrefixReaderV1Receipt *receipt,
    char *error_buf, size_t error_buf_size) {
    GSLTDirectPrefixStateV1 state;
    AtomId *ids = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    int result = -1;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_ids)
        *out_ids = NULL;
    if (receipt)
        memset(receipt, 0, sizeof(*receipt));
    if (!plan || !projection || !projection->context ||
        !projection->begin_form || !projection->word_bytes ||
        !projection->variable_bytes || !projection->anonymous_variable ||
        !projection->string_bytes ||
        !projection->expression || !projection->prefix || !out_ids ||
        input_len > UINT32_MAX || (input_len > 0u && !input)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "invalid direct prefix-reader invocation");
        return -1;
    }
    memset(&state, 0, sizeof(state));
    state.plan = plan;
    state.cursor.input = input;
    state.cursor.input_len = input_len;
    state.projection = projection;
    state.error_buf = error_buf;
    state.error_buf_size = error_buf_size;
    for (;;) {
        AtomId id;
        if (!gslt_direct_prefix_skip(&state))
            goto done;
        if (state.cursor.pos == input_len)
            break;
        if (!state.projection->begin_form(state.projection->context))
            goto done;
        id = gslt_direct_prefix_parse_atom(&state, plan->depth_limit);
        if (id == CETTA_ATOM_ID_NONE)
            goto done;
        if (len == cap) {
            uint32_t next = cap ? cap * 2u : 64u;
            if (next < cap || SIZE_MAX / (size_t)next < sizeof(*ids))
                goto done;
            ids = cetta_realloc(ids, (size_t)next * sizeof(*ids));
            cap = next;
        }
        ids[len++] = id;
    }
    if (len > (uint32_t)INT_MAX)
        goto done;
    if (receipt) {
        receipt->source_pass_count = 1u;
        receipt->input_byte_len = (uint32_t)input_len;
        receipt->token_len = state.tokens;
        receipt->prefix_len = state.prefixes;
        receipt->shift_len = state.cursor.shifts;
        receipt->reduce_len = state.reductions;
    }
    *out_ids = ids;
    ids = NULL;
    result = (int)len;
done:
    if (result < 0 && error_buf && error_buf_size > 0u &&
        error_buf[0] == '\0') {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "source is outside the compiled prefix S-expression LanguageDef");
    }
    free(ids);
    return result;
}

typedef struct {
    const GSLTDirectPeTTaReaderV1Plan *plan;
    GSLTDirectCursorV1 cursor;
    const GSLTDirectPeTTaProjectionV1 *projection;
    uint32_t tokens;
    uint32_t reductions;
    char *error_buf;
    size_t error_buf_size;
} GSLTDirectPeTTaStateV1;

static bool gslt_direct_petta_peek(
    const GSLTDirectCursorV1 *cursor, uint32_t *codepoint,
    size_t *width, char *error_buf, size_t error_buf_size) {
    if (cursor->pos == cursor->input_len)
        return false;
    if (gslt_direct_reader_v1_peek(cursor, codepoint, width))
        return true;
    gslt_direct_reader_v1_error(
        error_buf, error_buf_size,
        "compiled PeTTa reader encountered malformed UTF-8");
    return false;
}

static bool gslt_direct_petta_append_current(
    GSLTDirectCursorV1 *cursor, GSLTDirectBytesV1 *output,
    size_t width) {
    if (!gslt_direct_reader_v1_bytes_append(
            output, cursor->input + cursor->pos, width))
        return false;
    gslt_direct_reader_v1_consume_width(cursor, width);
    return true;
}

static bool gslt_direct_petta_skip_document(
    const GSLTDirectPeTTaReaderV1Plan *plan,
    GSLTDirectCursorV1 *cursor, char *error_buf,
    size_t error_buf_size) {
    for (;;) {
        uint32_t codepoint;
        size_t width;
        if (cursor->pos == cursor->input_len)
            return true;
        if (!gslt_direct_petta_peek(
                cursor, &codepoint, &width, error_buf, error_buf_size))
            return false;
        if (gslt_direct_reader_v1_class_contains(
                plan->splitter_blank, codepoint)) {
            gslt_direct_reader_v1_consume_width(cursor, width);
            continue;
        }
        if (codepoint != plan->comment_marker)
            return true;
        gslt_direct_reader_v1_consume_width(cursor, width);
        while (cursor->pos < cursor->input_len) {
            if (!gslt_direct_petta_peek(
                    cursor, &codepoint, &width,
                    error_buf, error_buf_size))
                return false;
            if (!gslt_direct_reader_v1_class_contains(
                    plan->splitter_comment_body, codepoint))
                break;
            gslt_direct_reader_v1_consume_width(cursor, width);
        }
        if (cursor->pos < cursor->input_len) {
            if (!gslt_direct_petta_peek(
                    cursor, &codepoint, &width,
                    error_buf, error_buf_size) ||
                codepoint != plan->comment_line_end) {
                gslt_direct_reader_v1_error(
                    error_buf, error_buf_size,
                    "compiled PeTTa document comment has no valid boundary");
                return false;
            }
            gslt_direct_reader_v1_consume_width(cursor, width);
        }
    }
}

static bool gslt_direct_petta_extract_form(
    const GSLTDirectPeTTaReaderV1Plan *plan,
    GSLTDirectCursorV1 *cursor, GSLTDirectBytesV1 *form,
    char *error_buf, size_t error_buf_size) {
    uint32_t depth = 0u;
    uint32_t codepoint;
    size_t width;

    if (!gslt_direct_petta_peek(
            cursor, &codepoint, &width, error_buf, error_buf_size) ||
        codepoint != plan->expression_open ||
        !gslt_direct_petta_append_current(cursor, form, width)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "compiled PeTTa document expected an opening expression");
        return false;
    }
    depth = 1u;
    while (cursor->pos < cursor->input_len) {
        if (!gslt_direct_petta_peek(
                cursor, &codepoint, &width, error_buf, error_buf_size))
            return false;
        if (codepoint == plan->string_quote) {
            if (!gslt_direct_petta_append_current(cursor, form, width))
                return false;
            for (;;) {
                if (cursor->pos == cursor->input_len ||
                    !gslt_direct_petta_peek(
                        cursor, &codepoint, &width,
                        error_buf, error_buf_size)) {
                    gslt_direct_reader_v1_error(
                        error_buf, error_buf_size,
                        "compiled PeTTa splitter found an unclosed quote");
                    return false;
                }
                if (codepoint == plan->string_quote) {
                    if (!gslt_direct_petta_append_current(
                            cursor, form, width))
                        return false;
                    break;
                }
                if (!gslt_direct_reader_v1_class_contains(
                        plan->splitter_nonquote, codepoint) ||
                    !gslt_direct_petta_append_current(
                        cursor, form, width))
                    return false;
            }
            continue;
        }
        if (codepoint == plan->comment_marker) {
            gslt_direct_reader_v1_consume_width(cursor, width);
            while (cursor->pos < cursor->input_len) {
                if (!gslt_direct_petta_peek(
                        cursor, &codepoint, &width,
                        error_buf, error_buf_size))
                    return false;
                if (!gslt_direct_reader_v1_class_contains(
                        plan->splitter_comment_body, codepoint))
                    break;
                gslt_direct_reader_v1_consume_width(cursor, width);
            }
            if (cursor->pos == cursor->input_len ||
                !gslt_direct_petta_peek(
                    cursor, &codepoint, &width,
                    error_buf, error_buf_size) ||
                codepoint != plan->comment_line_end) {
                gslt_direct_reader_v1_error(
                    error_buf, error_buf_size,
                    "compiled PeTTa splitter requires LF after an in-form comment");
                return false;
            }
            gslt_direct_reader_v1_consume_width(cursor, width);
            continue;
        }
        if (codepoint == plan->expression_open) {
            if (depth == plan->depth_limit ||
                !gslt_direct_petta_append_current(cursor, form, width)) {
                gslt_direct_reader_v1_error(
                    error_buf, error_buf_size,
                    "compiled PeTTa document exceeded its nesting limit");
                return false;
            }
            depth++;
            continue;
        }
        if (codepoint == plan->expression_close) {
            if (!gslt_direct_petta_append_current(cursor, form, width))
                return false;
            depth--;
            if (depth == 0u)
                return true;
            continue;
        }
        if (!gslt_direct_reader_v1_class_contains(
                plan->splitter_ordinary, codepoint) ||
            !gslt_direct_petta_append_current(cursor, form, width)) {
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "compiled PeTTa splitter rejected a balanced-form scalar");
            return false;
        }
    }
    gslt_direct_reader_v1_error(
        error_buf, error_buf_size,
        "compiled PeTTa document has an unclosed expression");
    return false;
}

static bool gslt_direct_petta_skip_form(
    GSLTDirectPeTTaStateV1 *state) {
    while (state->cursor.pos < state->cursor.input_len) {
        uint32_t codepoint;
        size_t width;
        if (!gslt_direct_petta_peek(
                &state->cursor, &codepoint, &width,
                state->error_buf, state->error_buf_size))
            return false;
        if (!gslt_direct_reader_v1_class_contains(
                state->plan->form_blank, codepoint))
            break;
        gslt_direct_reader_v1_consume_width(&state->cursor, width);
    }
    return true;
}

static bool gslt_direct_petta_token_boundary(
    GSLTDirectPeTTaStateV1 *state) {
    uint32_t codepoint;
    size_t width;
    if (state->cursor.pos == state->cursor.input_len)
        return true;
    if (!gslt_direct_petta_peek(
            &state->cursor, &codepoint, &width,
            state->error_buf, state->error_buf_size))
        return false;
    (void)width;
    return gslt_direct_reader_v1_class_contains(
        state->plan->token_boundary, codepoint);
}

static bool gslt_direct_petta_escape_target(
    const GSLTDirectPeTTaReaderV1Plan *plan, uint32_t source,
    uint32_t *target) {
    for (uint32_t index = 0u; index < plan->string_escape_map_len;
         index++) {
        if (plan->string_escape_map[index].source == source) {
            *target = plan->string_escape_map[index].target;
            return true;
        }
    }
    if (plan->string_escape_identity_fallback) {
        *target = source;
        return true;
    }
    return false;
}

static AtomId gslt_direct_petta_parse_atom(
    GSLTDirectPeTTaStateV1 *state, uint32_t depth);

static AtomId gslt_direct_petta_parse_string(
    GSLTDirectPeTTaStateV1 *state) {
    GSLTDirectCursorV1 start = state->cursor;
    GSLTDirectBytesV1 decoded = {0};
    AtomId result = CETTA_ATOM_ID_NONE;
    uint32_t codepoint;
    size_t width;

    if (!gslt_direct_petta_peek(
            &state->cursor, &codepoint, &width,
            state->error_buf, state->error_buf_size) ||
        codepoint != state->plan->string_quote)
        goto failed;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    while (state->cursor.pos < state->cursor.input_len) {
        size_t scalar_start = state->cursor.pos;
        if (!gslt_direct_petta_peek(
                &state->cursor, &codepoint, &width,
                state->error_buf, state->error_buf_size))
            goto failed;
        if (codepoint == state->plan->string_quote) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            result = state->projection->string_bytes(
                state->projection->context, decoded.data, decoded.len);
            if (result == CETTA_ATOM_ID_NONE && decoded.len > 0u &&
                memchr(decoded.data, '\0', decoded.len)) {
                gslt_direct_reader_v1_error(
                    state->error_buf, state->error_buf_size,
                    "PeTTa string contains an embedded NUL that CeTTa cannot represent");
            }
            free(decoded.data);
            return result;
        }
        if (gslt_direct_reader_v1_class_contains(
                state->plan->string_plain, codepoint)) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            if (!gslt_direct_reader_v1_bytes_append(
                    &decoded, state->cursor.input + scalar_start, width))
                goto failed;
            continue;
        }
        if (codepoint == state->plan->escape_marker) {
            uint32_t target;
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            if (!gslt_direct_petta_peek(
                    &state->cursor, &codepoint, &width,
                    state->error_buf, state->error_buf_size) ||
                !gslt_direct_petta_escape_target(
                    state->plan, codepoint, &target) ||
                !gslt_direct_reader_v1_bytes_append_codepoint(
                    &decoded, target))
                goto failed;
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            continue;
        }
        goto failed;
    }

failed:
    state->cursor = start;
    free(decoded.data);
    return CETTA_ATOM_ID_NONE;
}

static AtomId gslt_direct_petta_parse_quoted_token(
    GSLTDirectPeTTaStateV1 *state) {
    GSLTDirectCursorV1 start = state->cursor;
    GSLTDirectBytesV1 body = {0};
    bool final_escape_quote = false;
    size_t final_quote_width = 0u;
    AtomId result = CETTA_ATOM_ID_NONE;
    uint32_t codepoint;
    size_t width;

    if (!gslt_direct_petta_peek(
            &state->cursor, &codepoint, &width,
            state->error_buf, state->error_buf_size) ||
        codepoint != state->plan->string_quote)
        goto failed;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    while (state->cursor.pos < state->cursor.input_len) {
        size_t scalar_start = state->cursor.pos;
        if (gslt_direct_petta_token_boundary(state))
            break;
        if (!gslt_direct_petta_peek(
                &state->cursor, &codepoint, &width,
                state->error_buf, state->error_buf_size))
            goto failed;
        if (gslt_direct_reader_v1_class_contains(
                state->plan->quoted_token_plain, codepoint)) {
            final_escape_quote = false;
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            if (!gslt_direct_reader_v1_bytes_append(
                    &body, state->cursor.input + scalar_start, width))
                goto failed;
            continue;
        }
        if (codepoint == state->plan->escape_marker) {
            size_t escape_start = state->cursor.pos;
            size_t quote_width;
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            if (!gslt_direct_petta_peek(
                    &state->cursor, &codepoint, &quote_width,
                    state->error_buf, state->error_buf_size) ||
                !gslt_direct_reader_v1_class_contains(
                    state->plan->token, codepoint))
                goto failed;
            gslt_direct_reader_v1_consume_width(
                &state->cursor, quote_width);
            if (!gslt_direct_reader_v1_bytes_append(
                    &body, state->cursor.input + escape_start,
                    state->cursor.pos - escape_start))
                goto failed;
            final_escape_quote =
                codepoint == state->plan->string_quote;
            final_quote_width = final_escape_quote ? quote_width : 0u;
            continue;
        }
        goto failed;
    }
    if (!final_escape_quote || !gslt_direct_petta_token_boundary(state) ||
        body.len == 0u)
        goto failed;
    /* The closing quote is stripped; its preceding backslash remains data. */
    if (final_quote_width > body.len)
        goto failed;
    body.len -= final_quote_width;
    result = state->projection->string_bytes(
        state->projection->context, body.data, body.len);
    if (result == CETTA_ATOM_ID_NONE && body.len > 0u &&
        memchr(body.data, '\0', body.len)) {
        gslt_direct_reader_v1_error(
            state->error_buf, state->error_buf_size,
            "PeTTa quoted token contains an embedded NUL that CeTTa cannot represent");
    }
    free(body.data);
    return result;

failed:
    state->cursor = start;
    free(body.data);
    return CETTA_ATOM_ID_NONE;
}

static AtomId gslt_direct_petta_parse_token(
    GSLTDirectPeTTaStateV1 *state) {
    GSLTDirectCursorV1 start = state->cursor;
    size_t token_start = state->cursor.pos;
    uint32_t codepoint;
    size_t width;

    if (!gslt_direct_petta_peek(
            &state->cursor, &codepoint, &width,
            state->error_buf, state->error_buf_size) ||
        !gslt_direct_reader_v1_class_contains(
            state->plan->token_first, codepoint))
        return CETTA_ATOM_ID_NONE;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    while (state->cursor.pos < state->cursor.input_len) {
        if (!gslt_direct_petta_peek(
                &state->cursor, &codepoint, &width,
                state->error_buf, state->error_buf_size))
            goto failed;
        if (!gslt_direct_reader_v1_class_contains(
                state->plan->token, codepoint))
            break;
        gslt_direct_reader_v1_consume_width(&state->cursor, width);
    }
    if (!gslt_direct_petta_token_boundary(state))
        goto failed;
    return state->projection->token_bytes(
        state->projection->context,
        state->cursor.input + token_start,
        state->cursor.pos - token_start);

failed:
    state->cursor = start;
    return CETTA_ATOM_ID_NONE;
}

static AtomId gslt_direct_petta_parse_variable_or_dollar(
    GSLTDirectPeTTaStateV1 *state) {
    GSLTDirectCursorV1 start = state->cursor;
    uint32_t codepoint;
    size_t width;
    size_t name_start;

    if (!gslt_direct_petta_peek(
            &state->cursor, &codepoint, &width,
            state->error_buf, state->error_buf_size) ||
        codepoint != state->plan->variable_marker)
        return CETTA_ATOM_ID_NONE;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    name_start = state->cursor.pos;
    while (state->cursor.pos < state->cursor.input_len) {
        if (!gslt_direct_petta_peek(
                &state->cursor, &codepoint, &width,
                state->error_buf, state->error_buf_size))
            goto failed;
        if (!gslt_direct_reader_v1_class_contains(
                state->plan->token, codepoint))
            break;
        gslt_direct_reader_v1_consume_width(&state->cursor, width);
    }
    if (!gslt_direct_petta_token_boundary(state))
        goto failed;
    if (state->cursor.pos == name_start)
        return state->projection->dollar_symbol(
            state->projection->context);
    return state->projection->variable_bytes(
        state->projection->context,
        state->cursor.input + name_start,
        state->cursor.pos - name_start,
        state->cursor.pos - name_start == 1u &&
            state->cursor.input[name_start] == (uint8_t)'_');

failed:
    state->cursor = start;
    return CETTA_ATOM_ID_NONE;
}

static AtomId gslt_direct_petta_parse_expression(
    GSLTDirectPeTTaStateV1 *state, uint32_t depth) {
    GSLTDirectCursorV1 start = state->cursor;
    AtomId *children = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    AtomId result = CETTA_ATOM_ID_NONE;
    uint32_t codepoint;
    size_t width;

    if (depth == 0u ||
        !gslt_direct_petta_peek(
            &state->cursor, &codepoint, &width,
            state->error_buf, state->error_buf_size) ||
        codepoint != state->plan->expression_open)
        goto failed;
    gslt_direct_reader_v1_consume_width(&state->cursor, width);
    for (;;) {
        AtomId child;
        if (!gslt_direct_petta_skip_form(state) ||
            state->cursor.pos == state->cursor.input_len)
            goto failed;
        if (!gslt_direct_petta_peek(
                &state->cursor, &codepoint, &width,
                state->error_buf, state->error_buf_size))
            goto failed;
        if (codepoint == state->plan->expression_close) {
            gslt_direct_reader_v1_consume_width(&state->cursor, width);
            result = state->projection->expression(
                state->projection->context, children, len);
            free(children);
            return result;
        }
        child = gslt_direct_petta_parse_atom(state, depth - 1u);
        if (child == CETTA_ATOM_ID_NONE)
            goto failed;
        if (len == cap) {
            uint32_t next = cap ? cap * 2u : 16u;
            if (next < cap)
                goto failed;
            children = cetta_realloc(
                children, (size_t)next * sizeof(*children));
            cap = next;
        }
        children[len++] = child;
    }

failed:
    state->cursor = start;
    free(children);
    return CETTA_ATOM_ID_NONE;
}

static AtomId gslt_direct_petta_parse_atom(
    GSLTDirectPeTTaStateV1 *state, uint32_t depth) {
    GSLTDirectCursorV1 start = state->cursor;
    AtomId result = CETTA_ATOM_ID_NONE;
    uint32_t codepoint;
    size_t width;

    if (depth == 0u || state->cursor.pos == state->cursor.input_len ||
        !gslt_direct_petta_peek(
            &state->cursor, &codepoint, &width,
            state->error_buf, state->error_buf_size))
        return CETTA_ATOM_ID_NONE;
    if (codepoint == state->plan->string_quote) {
        result = gslt_direct_petta_parse_string(state);
        if (result == CETTA_ATOM_ID_NONE) {
            state->cursor = start;
            result = gslt_direct_petta_parse_quoted_token(state);
        }
    } else if (codepoint == state->plan->expression_open) {
        result = gslt_direct_petta_parse_expression(state, depth);
    } else if (codepoint == state->plan->variable_marker) {
        result = gslt_direct_petta_parse_variable_or_dollar(state);
    } else if (gslt_direct_reader_v1_class_contains(
                   state->plan->token_first, codepoint)) {
        result = gslt_direct_petta_parse_token(state);
    }
    if (result != CETTA_ATOM_ID_NONE) {
        state->tokens++;
        state->reductions++;
    }
    return result;
}

static AtomId gslt_direct_petta_parse_form(
    const GSLTDirectPeTTaReaderV1Plan *plan,
    const uint8_t *input, size_t input_len,
    const GSLTDirectPeTTaProjectionV1 *projection,
    uint32_t *tokens, uint32_t *shifts, uint32_t *reductions,
    char *error_buf, size_t error_buf_size) {
    GSLTDirectPeTTaStateV1 state = {0};
    AtomId result;
    state.plan = plan;
    state.cursor.input = input;
    state.cursor.input_len = input_len;
    state.projection = projection;
    state.error_buf = error_buf;
    state.error_buf_size = error_buf_size;
    if (!gslt_direct_petta_skip_form(&state))
        return CETTA_ATOM_ID_NONE;
    result = gslt_direct_petta_parse_atom(&state, plan->depth_limit);
    if (result == CETTA_ATOM_ID_NONE ||
        !gslt_direct_petta_skip_form(&state) ||
        state.cursor.pos != input_len) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "filtered form is outside the compiled PeTTa form LanguageDef");
        return CETTA_ATOM_ID_NONE;
    }
    *tokens += state.tokens;
    *shifts += state.cursor.shifts;
    *reductions += state.reductions;
    return result;
}

bool gslt_direct_petta_reader_v1_plan_validate(
    const GSLTDirectPeTTaReaderV1Plan *plan, char *error_buf,
    size_t error_buf_size) {
    const GSLTDirectScalarClassV1 *classes[10];
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !plan->presentation_name ||
        plan->presentation_name[0] == '\0' || !plan->fragment ||
        strcmp(plan->fragment, GSLT_DIRECT_PETTA_READER_V1_FRAGMENT) != 0 ||
        !plan->profile || plan->profile[0] == '\0' ||
        !gslt_direct_reader_v1_sha256_valid(plan->splitter_syntax_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->splitter_class_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->form_syntax_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->form_class_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->projection_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->compiler_digest) ||
        !gslt_direct_reader_v1_sha256_valid(plan->composition_digest) ||
        plan->depth_limit == 0u) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "invalid direct PeTTa reader plan header");
        return false;
    }
    classes[0] = plan->splitter_blank;
    classes[1] = plan->splitter_nonquote;
    classes[2] = plan->splitter_comment_body;
    classes[3] = plan->splitter_ordinary;
    classes[4] = plan->form_blank;
    classes[5] = plan->token_boundary;
    classes[6] = plan->token;
    classes[7] = plan->token_first;
    classes[8] = plan->string_plain;
    classes[9] = plan->quoted_token_plain;
    for (size_t index = 0u; index < 10u; index++) {
        if (!gslt_direct_reader_v1_class_valid(classes[index])) {
            gslt_direct_reader_v1_error(
                error_buf, error_buf_size,
                "invalid source-derived PeTTa scalar class");
            return false;
        }
    }
    if (!gslt_direct_reader_v1_scalar_valid(plan->comment_marker) ||
        !gslt_direct_reader_v1_scalar_valid(plan->comment_line_end) ||
        !gslt_direct_reader_v1_scalar_valid(plan->expression_open) ||
        !gslt_direct_reader_v1_scalar_valid(plan->expression_close) ||
        !gslt_direct_reader_v1_scalar_valid(plan->string_quote) ||
        !gslt_direct_reader_v1_scalar_valid(plan->escape_marker) ||
        !gslt_direct_reader_v1_scalar_valid(plan->variable_marker) ||
        !gslt_direct_reader_v1_scalar_valid(plan->runnable_marker) ||
        plan->expression_open == plan->expression_close ||
        plan->string_quote == plan->escape_marker ||
        plan->string_escape_map_len == 0u ||
        !gslt_direct_reader_v1_map_valid(
            plan->string_escape_map, plan->string_escape_map_len) ||
        !plan->string_escape_identity_fallback ||
        gslt_direct_reader_v1_class_contains(
            plan->splitter_nonquote, plan->string_quote) ||
        gslt_direct_reader_v1_class_contains(
            plan->splitter_comment_body, plan->comment_line_end) ||
        gslt_direct_reader_v1_class_contains(
            plan->splitter_ordinary, plan->string_quote) ||
        gslt_direct_reader_v1_class_contains(
            plan->splitter_ordinary, plan->expression_open) ||
        gslt_direct_reader_v1_class_contains(
            plan->splitter_ordinary, plan->expression_close) ||
        gslt_direct_reader_v1_class_contains(
            plan->splitter_ordinary, plan->comment_marker) ||
        gslt_direct_reader_v1_class_contains(
            plan->string_plain, plan->string_quote) ||
        gslt_direct_reader_v1_class_contains(
            plan->string_plain, plan->escape_marker) ||
        gslt_direct_reader_v1_class_contains(
            plan->quoted_token_plain, plan->string_quote) ||
        gslt_direct_reader_v1_class_contains(
            plan->quoted_token_plain, plan->escape_marker) ||
        gslt_direct_reader_v1_class_contains(
            plan->token_first, plan->string_quote) ||
        gslt_direct_reader_v1_class_contains(
            plan->token_first, plan->variable_marker) ||
        gslt_direct_reader_v1_class_contains(
            plan->token_first, plan->expression_open) ||
        gslt_direct_reader_v1_class_contains(
            plan->token_first, plan->expression_close)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "direct PeTTa reader dispatch disagrees with its LanguageDefs");
        return false;
    }
    return true;
}

int gslt_direct_petta_reader_v1_parse_bytes_ids(
    const GSLTDirectPeTTaReaderV1Plan *plan, const uint8_t *input,
    size_t input_len, const GSLTDirectPeTTaProjectionV1 *projection,
    AtomId **out_ids, GSLTDirectPeTTaReaderV1Receipt *receipt,
    char *error_buf, size_t error_buf_size) {
    GSLTDirectCursorV1 document = {0};
    AtomId *ids = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    uint32_t forms = 0u;
    uint32_t tokens = 0u;
    uint32_t form_shifts = 0u;
    uint32_t reductions = 0u;
    int result = -1;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_ids)
        *out_ids = NULL;
    if (receipt)
        memset(receipt, 0, sizeof(*receipt));
    if (!plan || !projection || !projection->context ||
        !projection->begin_form || !projection->token_bytes ||
        !projection->variable_bytes || !projection->string_bytes ||
        !projection->dollar_symbol || !projection->expression ||
        !projection->finish_form || !out_ids || input_len > UINT32_MAX ||
        (input_len > 0u && !input)) {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "invalid direct PeTTa reader invocation");
        return -1;
    }
    document.input = input;
    document.input_len = input_len;
    while (true) {
        GSLTDirectBytesV1 form = {0};
        bool runnable = false;
        AtomId value;
        AtomId projected[2] = {CETTA_ATOM_ID_NONE, CETTA_ATOM_ID_NONE};
        uint32_t projected_len = 0u;
        uint32_t codepoint;
        size_t width;

        if (!gslt_direct_petta_skip_document(
                plan, &document, error_buf, error_buf_size))
            goto done;
        if (document.pos == document.input_len)
            break;
        if (!gslt_direct_petta_peek(
                &document, &codepoint, &width,
                error_buf, error_buf_size))
            goto done;
        if (codepoint == plan->runnable_marker) {
            runnable = true;
            gslt_direct_reader_v1_consume_width(&document, width);
        }
        if (!projection->begin_form(projection->context) ||
            !gslt_direct_petta_extract_form(
                plan, &document, &form, error_buf, error_buf_size)) {
            free(form.data);
            goto done;
        }
        value = gslt_direct_petta_parse_form(
            plan, form.data, form.len, projection,
            &tokens, &form_shifts, &reductions,
            error_buf, error_buf_size);
        free(form.data);
        if (value == CETTA_ATOM_ID_NONE ||
            !projection->finish_form(
                projection->context, runnable, value,
                projected, &projected_len) ||
            projected_len == 0u || projected_len > 2u)
            goto done;
        if (projected_len > UINT32_MAX - len)
            goto done;
        if (len + projected_len > cap) {
            uint32_t next = cap ? cap : 64u;
            while (next < len + projected_len) {
                if (next > UINT32_MAX / 2u)
                    goto done;
                next *= 2u;
            }
            ids = cetta_realloc(ids, (size_t)next * sizeof(*ids));
            cap = next;
        }
        for (uint32_t index = 0u; index < projected_len; index++) {
            if (projected[index] == CETTA_ATOM_ID_NONE)
                goto done;
            ids[len++] = projected[index];
        }
        forms++;
        reductions++;
    }
    if (len > (uint32_t)INT_MAX)
        goto done;
    if (receipt) {
        receipt->source_pass_count = 1u;
        receipt->form_replay_count = forms;
        receipt->input_byte_len = (uint32_t)input_len;
        receipt->form_len = forms;
        receipt->token_len = tokens;
        receipt->shift_len = document.shifts + form_shifts;
        receipt->reduce_len = reductions;
    }
    *out_ids = ids;
    ids = NULL;
    result = (int)len;

done:
    if (result < 0 && error_buf && error_buf_size > 0u &&
        error_buf[0] == '\0') {
        gslt_direct_reader_v1_error(
            error_buf, error_buf_size,
            "source is outside the compiled PeTTa LanguageDef composition");
    }
    free(ids);
    return result;
}
