#include "str_stream.h"
#include "string_ops.h"
#include "utf8.h"
#include <stdlib.h>
#include <string.h>

static Atom *expects(CettaStrRefusal *refusal, const char *expected) {
    refusal->expected = expected;
    return NULL;
}

static Atom *refuses(CettaStrRefusal *refusal, const char *reason, size_t offset) {
    refusal->reason = reason;
    refusal->offset = offset;
    return NULL;
}

static bool bytes_arg(Atom *value, const char **bytes, size_t *length) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_STRING)
        return false;
    *bytes = value->ground.sval;
    *length = atom_string_len(value);
    return true;
}

static bool bool_arg(Atom *value, bool *out) {
    if (value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_BOOL) {
        *out = value->ground.bval;
        return true;
    }
    if (atom_is_symbol(value, "True") || atom_is_symbol(value, "true")) { *out = true; return true; }
    if (atom_is_symbol(value, "False") || atom_is_symbol(value, "false")) { *out = false; return true; }
    return false;
}

static bool headed(Atom *value, const char *name, CettaExprLen length) {
    return value && value->kind == ATOM_EXPR && value->expr.len == length &&
        atom_is_symbol(value->expr.elems[0], name);
}

static Atom *decoder(Arena *arena, Atom *policy, const char *tail, size_t length, int64_t seen) {
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "Utf8DecoderV1"), policy,
        atom_string_n(arena, tail, length), atom_int(arena, seen)}, 4u);
}

Atom *cetta_str_utf8_new(Arena *arena, Atom *const *args, CettaStrRefusal *refusal) {
    if (!atom_is_symbol(args[0], "strict") && !atom_is_symbol(args[0], "ignore"))
        return expects(refusal, "expected strict or ignore UTF-8 policy");
    return decoder(arena, args[0], "", 0u, 0);
}

Atom *cetta_str_utf8_feed(Arena *arena, Atom *const *args, CettaStrRefusal *refusal) {
    const char *tail, *bytes;
    size_t tail_length, length;
    bool final;
    Atom *state = args[0];
    if (!headed(state, "Utf8DecoderV1", 4u) ||
        !bytes_arg(state->expr.elems[2], &tail, &tail_length) ||
        !bytes_arg(args[1], &bytes, &length) || !bool_arg(args[2], &final))
        return expects(refusal, "expected UTF-8 decoder state, bytes and final Boolean");
    Atom *policy = state->expr.elems[1], *count = state->expr.elems[3];
    bool strict = atom_is_symbol(policy, "strict");
    if ((!strict && !atom_is_symbol(policy, "ignore")) || count->kind != ATOM_GROUNDED ||
        count->ground.gkind != GV_INT || count->ground.ival < 0 ||
        (uint64_t)count->ground.ival < tail_length ||
        (tail_length && !cetta_utf8_incomplete_prefix((const uint8_t *)tail, tail_length)))
        return expects(refusal, "decoder tail must be an incomplete scalar prefix with consistent byte count");
    uint64_t seen = (uint64_t)count->ground.ival;
    if (length > INT64_MAX - seen || tail_length > SIZE_MAX - length ||
        length + tail_length > UINT32_MAX)
        return refuses(refusal, "StrByteCountCapacityV1", (size_t)seen);
    size_t total = length + tail_length;
    char *joined = malloc(total ? total : 1u);
    if (!joined) return refuses(refusal, "StrCapacityV1", (size_t)seen);
    memcpy(joined, tail, tail_length);
    memcpy(joined + tail_length, bytes, length);
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    size_t offset = 0u;
    while (offset < total) {
        uint32_t scalar, width;
        if (cetta_utf8_decode_one((const uint8_t *)joined, total, offset, &scalar, &width)) {
            if (!cetta_byte_buffer_append(&out, joined + offset, width)) {
                cetta_byte_buffer_free(&out);
                free(joined);
                return refuses(refusal, "StrCapacityV1", (size_t)seen);
            }
            offset += width;
        } else if (!final && cetta_utf8_incomplete_prefix(
                       (const uint8_t *)joined + offset, total - offset)) break;
        else if (strict) {
            size_t bad = (size_t)(seen - tail_length) + offset;
            cetta_byte_buffer_free(&out);
            free(joined);
            return refuses(refusal, "StrMalformedUtf8V1", bad);
        } else offset++; /* Ignoring invalid bytes must not discard valid neighbours. */
    }
    Atom *next = decoder(arena, policy, joined + offset, total - offset,
                          (int64_t)(seen + length));
    Atom *result = atom_expr3(arena, atom_symbol(arena, "Utf8DecodedV1"),
        atom_string_n(arena, out.bytes ? out.bytes : "", out.len), next);
    cetta_byte_buffer_free(&out);
    free(joined);
    return result;
}

static bool line_boundary(uint32_t scalar) {
    return scalar == '\n' || scalar == '\r' || scalar == '\v' || scalar == '\f' ||
        scalar == 0x1cu || scalar == 0x1du || scalar == 0x1eu || scalar == 0x85u ||
        scalar == 0x2028u || scalar == 0x2029u;
}

static Atom *reverse_rows(Arena *arena, Atom *rows) {
    Atom *ordered = atom_symbol(arena, "LNil");
    for (Atom *row = rows; !atom_is_symbol(row, "LNil"); row = row->expr.elems[2])
        ordered = atom_expr3(arena, atom_symbol(arena, "LCons"), row->expr.elems[1], ordered);
    return ordered;
}

Atom *cetta_str_line_new(Arena *arena, Atom *const *args, CettaStrRefusal *refusal) {
    (void)args;
    (void)refusal;
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "LineBufferV1"),
        atom_symbol(arena, "LNil"), atom_int(arena, 1), atom_false(arena)}, 4u);
}

Atom *cetta_str_line_segments(Arena *arena, Atom *const *args, CettaStrRefusal *refusal) {
    const char *text;
    size_t length;
    bool previous_cr;
    if (!bytes_arg(args[0], &text, &length) || !bool_arg(args[1], &previous_cr))
        return expects(refusal, "expected UTF-8 text and preceding-CR Boolean");
    Atom *parts = atom_symbol(arena, "LNil");
    size_t start = previous_cr && length && text[0] == '\n' ? 1u : 0u;
    bool trailing_cr = previous_cr && !length;
    for (size_t offset = start; offset < length;) {
        uint32_t scalar, width;
        if (!cetta_utf8_decode_one((const uint8_t *)text, length, offset, &scalar, &width))
            return refuses(refusal, "StrMalformedUtf8V1", offset);
        if (line_boundary(scalar)) {
            Atom *part = atom_expr3(arena, atom_symbol(arena, "LinePartV1"),
                atom_string_n(arena, text + start, offset - start), atom_true(arena));
            parts = atom_expr3(arena, atom_symbol(arena, "LCons"), part, parts);
            offset += width;
            trailing_cr = scalar == '\r' && offset == length;
            if (scalar == '\r' && offset < length && text[offset] == '\n') offset++;
            start = offset;
        } else { offset += width; trailing_cr = false; }
    }
    if (start < length) {
        Atom *part = atom_expr3(arena, atom_symbol(arena, "LinePartV1"),
            atom_string_n(arena, text + start, length - start), atom_false(arena));
        parts = atom_expr3(arena, atom_symbol(arena, "LCons"), part, parts);
    }
    return atom_expr3(arena, atom_symbol(arena, "LineSegmentsV1"), reverse_rows(arena, parts),
                       atom_bool(arena, trailing_cr));
}

/* Copy each fragment once, only when its complete line is published. The
 * persistent reverse list is extended in constant work per incoming fragment. */
static bool list_cycle_step(Atom *next, Atom **fast) {
    for (unsigned i = 0u; i < 2u; i++) {
        if (!headed(*fast, "LCons", 3u)) { *fast = NULL; return false; }
        *fast = (*fast)->expr.elems[2];
    }
    return *fast == next;
}

static Atom *join_fragments(Arena *arena, Atom *fragments, CettaStrRefusal *refusal) {
    size_t length = 0u;
    Atom *fast = fragments;
    for (Atom *part = fragments; !atom_is_symbol(part, "LNil"); part = part->expr.elems[2]) {
        const char *bytes;
        size_t size;
        if (!headed(part, "LCons", 3u) || !bytes_arg(part->expr.elems[1], &bytes, &size))
            return expects(refusal, "expected finite reverse list of byte fragments");
        if (size > UINT32_MAX - length) return refuses(refusal, "StrLineCapacityV1", length);
        length += size;
        if (list_cycle_step(part->expr.elems[2], &fast))
            return expects(refusal, "expected acyclic reverse list of byte fragments");
    }
    char *joined = malloc(length ? length : 1u);
    if (!joined) return refuses(refusal, "StrCapacityV1", length);
    size_t position = length;
    for (Atom *part = fragments; !atom_is_symbol(part, "LNil"); part = part->expr.elems[2]) {
        Atom *piece = part->expr.elems[1];
        size_t size = atom_string_len(piece);
        position -= size;
        memcpy(joined + position, piece->ground.sval, size);
    }
    Atom *result = atom_string_n(arena, joined, length);
    free(joined);
    return result;
}

Atom *cetta_str_collect_lines(Arena *arena, Atom *const *args, CettaStrRefusal *refusal) {
    Atom *buffer = args[0], *segments = args[1];
    bool final, previous_cr, next_cr;
    if (!headed(buffer, "LineBufferV1", 4u) || !headed(segments, "LineSegmentsV1", 3u) ||
        !bool_arg(args[2], &final) || !bool_arg(buffer->expr.elems[3], &previous_cr) ||
        !bool_arg(segments->expr.elems[2], &next_cr))
        return expects(refusal, "expected line buffer, line segments and final Boolean");
    Atom *counter = buffer->expr.elems[2];
    if (counter->kind != ATOM_GROUNDED || counter->ground.gkind != GV_INT || counter->ground.ival < 0)
        return expects(refusal, "line number must be a nonnegative integer");
    int64_t number = counter->ground.ival;
    Atom *fragments = buffer->expr.elems[1], *lines = atom_symbol(arena, "LNil");
    Atom *fast = segments->expr.elems[1];
    for (Atom *row = segments->expr.elems[1]; !atom_is_symbol(row, "LNil"); row = row->expr.elems[2]) {
        if (!headed(row, "LCons", 3u) || !headed(row->expr.elems[1], "LinePartV1", 3u))
            return expects(refusal, "expected finite list of line parts");
        Atom *part = row->expr.elems[1];
        const char *bytes;
        size_t size;
        bool complete;
        if (!bytes_arg(part->expr.elems[1], &bytes, &size) || !bool_arg(part->expr.elems[2], &complete))
            return expects(refusal, "line part must contain bytes and completion Boolean");
        if (list_cycle_step(row->expr.elems[2], &fast))
            return expects(refusal, "expected acyclic list of line parts");
        if (size) fragments = atom_expr3(arena, atom_symbol(arena, "LCons"), part->expr.elems[1], fragments);
        if (complete) {
            if (number == INT64_MAX) return refuses(refusal, "StrLineCountCapacityV1", (size_t)number);
            Atom *line = join_fragments(arena, fragments, refusal);
            if (!line) return NULL;
            Atom *record = atom_expr3(arena, atom_symbol(arena, "LineV1"), atom_int(arena, number++), line);
            lines = atom_expr3(arena, atom_symbol(arena, "LCons"), record, lines);
            fragments = atom_symbol(arena, "LNil");
        }
    }
    if (final && !atom_is_symbol(fragments, "LNil")) {
        if (number == INT64_MAX) return refuses(refusal, "StrLineCountCapacityV1", (size_t)number);
        Atom *line = join_fragments(arena, fragments, refusal);
        if (!line) return NULL;
        Atom *record = atom_expr3(arena, atom_symbol(arena, "LineV1"), atom_int(arena, number++), line);
        lines = atom_expr3(arena, atom_symbol(arena, "LCons"), record, lines);
        fragments = atom_symbol(arena, "LNil");
    }
    Atom *next = atom_expr(arena, (Atom *[]){atom_symbol(arena, "LineBufferV1"),
        fragments, atom_int(arena, number), atom_bool(arena, next_cr)}, 4u);
    return atom_expr3(arena, atom_symbol(arena, "LinesCollectedV1"), reverse_rows(arena, lines), next);
}

/* A position-only observer need not assemble completed lines it will discard.
 * Both views retain the unfinished prefix for later value observation. */
static Atom *feed_lines(Arena *arena, Atom *const *args, CettaStrRefusal *refusal,
                        bool publish) {
    Atom *buffer = args[0];
    const char *text;
    size_t length;
    bool final, previous_cr;
    if (!headed(buffer, "LineBufferV1", 4u) || !bytes_arg(args[1], &text, &length) ||
        !bool_arg(args[2], &final) || !bool_arg(buffer->expr.elems[3], &previous_cr))
        return expects(refusal, "expected line buffer, UTF-8 text and final Boolean");
    Atom *counter = buffer->expr.elems[2];
    if (counter->kind != ATOM_GROUNDED || counter->ground.gkind != GV_INT || counter->ground.ival < 0)
        return expects(refusal, "line number must be a nonnegative integer");
    int64_t number = counter->ground.ival;
    Atom *fragments = buffer->expr.elems[1], *rows = atom_symbol(arena, "LNil");
    size_t start = previous_cr && length && text[0] == '\n' ? 1u : 0u;
    bool trailing_cr = previous_cr && !length;
    for (size_t offset = start; offset < length;) {
        uint32_t scalar, width;
        if (!cetta_utf8_decode_one((const uint8_t *)text, length, offset, &scalar, &width))
            return refuses(refusal, "StrMalformedUtf8V1", offset);
        if (line_boundary(scalar)) {
            if (number == INT64_MAX) return refuses(refusal, "StrLineCountCapacityV1", (size_t)number);
            if (publish) {
                if (offset > start)
                    fragments = atom_expr3(arena, atom_symbol(arena, "LCons"),
                        atom_string_n(arena, text + start, offset - start), fragments);
                Atom *line = join_fragments(arena, fragments, refusal);
                if (!line) return NULL;
                rows = atom_expr3(arena, atom_symbol(arena, "LCons"),
                    atom_expr3(arena, atom_symbol(arena, "LineV1"), atom_int(arena, number), line), rows);
            }
            number++;
            fragments = atom_symbol(arena, "LNil");
            offset += width;
            trailing_cr = scalar == '\r' && offset == length;
            if (scalar == '\r' && offset < length && text[offset] == '\n') offset++;
            start = offset;
        } else { offset += width; trailing_cr = false; }
    }
    if (start < length)
        fragments = atom_expr3(arena, atom_symbol(arena, "LCons"),
            atom_string_n(arena, text + start, length - start), fragments);
    if (final && !atom_is_symbol(fragments, "LNil")) {
        if (number == INT64_MAX) return refuses(refusal, "StrLineCountCapacityV1", (size_t)number);
        if (publish) {
            Atom *line = join_fragments(arena, fragments, refusal);
            if (!line) return NULL;
            rows = atom_expr3(arena, atom_symbol(arena, "LCons"),
                atom_expr3(arena, atom_symbol(arena, "LineV1"), atom_int(arena, number), line), rows);
        }
        number++;
        fragments = atom_symbol(arena, "LNil");
    }
    Atom *next = atom_expr(arena, (Atom *[]){atom_symbol(arena, "LineBufferV1"),
        fragments, atom_int(arena, number), atom_bool(arena, trailing_cr)}, 4u);
    return publish ? atom_expr3(arena, atom_symbol(arena, "LinesCollectedV1"),
                                reverse_rows(arena, rows), next) : next;
}

Atom *cetta_str_feed_lines(Arena *arena, Atom *const *args, CettaStrRefusal *refusal) {
    return feed_lines(arena, args, refusal, true);
}

Atom *cetta_str_advance_lines(Arena *arena, Atom *const *args, CettaStrRefusal *refusal) {
    return feed_lines(arena, args, refusal, false);
}

/* Simple case equivalence for ASCII literals, including the four Unicode
 * characters in Python's Unicode-uncased ASCII letter class. No expansions,
 * locale policy, arbitrary Unicode-pattern folding or regex syntax is implied. */
static uint32_t ascii_fold(uint32_t scalar) {
    if (scalar >= 'A' && scalar <= 'Z') return scalar + ('a' - 'A');
    if (scalar == 0x130u || scalar == 0x131u) return 'i';
    if (scalar == 0x17fu) return 's';
    if (scalar == 0x212au) return 'k';
    return scalar;
}

Atom *cetta_str_find_ascii_uncased(Arena *arena, Atom *const *args, CettaStrRefusal *refusal) {
    const char *text, *needle;
    size_t length, needed;
    if (!bytes_arg(args[0], &text, &length) || !bytes_arg(args[1], &needle, &needed))
        return expects(refusal, "expected byte string and ASCII literal search string");
    for (size_t i = 0u; i < needed; i++)
        if ((unsigned char)needle[i] > 0x7fu)
            return refuses(refusal, "StrAsciiLiteralRequiredV1", i);
    if (!needed) return atom_int(arena, 0);
    if (needed > SIZE_MAX / (2u * sizeof(size_t)))
        return refuses(refusal, "StrCapacityV1", 0u);
    size_t *memory = malloc(2u * needed * sizeof(*memory));
    if (!memory) return refuses(refusal, "StrCapacityV1", 0u);
    size_t *prefixes = memory, *positions = memory + needed;
    prefixes[0] = 0u;
    for (size_t i = 1u, matched = 0u; i < needed; i++) {
        uint32_t scalar = ascii_fold((unsigned char)needle[i]);
        while (matched && scalar != ascii_fold((unsigned char)needle[matched]))
            matched = prefixes[matched - 1u];
        if (scalar == ascii_fold((unsigned char)needle[matched])) matched++;
        prefixes[i] = matched;
    }
    for (size_t offset = 0u, unit = 0u, matched = 0u; offset < length; unit++) {
        uint32_t scalar, width;
        positions[unit % needed] = offset;
        if (!cetta_utf8_decode_one((const uint8_t *)text, length, offset, &scalar, &width)) {
            /* Undecodable filename bytes are opaque units, never erased. */
            scalar = 0x110000u + (unsigned char)text[offset];
            width = 1u;
        }
        scalar = ascii_fold(scalar);
        while (matched && scalar != ascii_fold((unsigned char)needle[matched]))
            matched = prefixes[matched - 1u];
        if (scalar == ascii_fold((unsigned char)needle[matched])) matched++;
        if (matched == needed) {
            size_t start = positions[(unit + 1u - needed) % needed];
            free(memory);
            return atom_int(arena, (int64_t)start);
        }
        offset += width;
    }
    free(memory);
    return atom_expr2(arena, atom_symbol(arena, "TextNotFound"), args[1]);
}
