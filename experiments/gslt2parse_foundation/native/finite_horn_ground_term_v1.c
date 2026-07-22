#define _POSIX_C_SOURCE 200809L

#include "finite_horn_ground_term_v1.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(size_t) <= sizeof(CettaExprLen),
               "CettaExprLen must hold every addressable expression length");

typedef struct {
    uint8_t *bytes;
    size_t len;
    size_t cap;
} FHGroundTermV1Buffer;

typedef struct {
    Arena *arena;
    const uint8_t *bytes;
    size_t len;
    size_t pos;
    char *error;
    size_t error_cap;
} FHGroundTermV1Parser;

typedef struct {
    const Atom *atom;
    CettaExprIndex next_child;
} FHGroundTermV1RenderFrame;

typedef struct {
    FHGroundTermV1RenderFrame *data;
    size_t len;
    size_t cap;
} FHGroundTermV1RenderStack;

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
    size_t open_pos;
} FHGroundTermV1ParseFrame;

typedef struct {
    FHGroundTermV1ParseFrame *data;
    size_t len;
    size_t cap;
} FHGroundTermV1ParseStack;

typedef struct {
    const Atom **atoms;
    uint8_t *states;
    size_t len;
    size_t tombstones;
    size_t cap;
} FHGroundTermV1ActiveSet;

typedef enum {
    FHGT_V1_ACTIVE_INSERTED = 0,
    FHGT_V1_ACTIVE_DUPLICATE = 1,
    FHGT_V1_ACTIVE_OOM = 2
} FHGroundTermV1ActiveInsert;

static bool fhgt_v1_error(char *error,
                          size_t error_cap,
                          const char *format,
                          ...) {
    va_list arguments;

    if (error && error_cap > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(error, error_cap, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool fhgt_v1_parser_error(FHGroundTermV1Parser *parser,
                                 const char *format,
                                 ...) {
    va_list arguments;

    if (parser->error && parser->error_cap > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(parser->error, parser->error_cap, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool fhgt_v1_buffer_reserve(FHGroundTermV1Buffer *buffer,
                                   size_t extra) {
    size_t needed;
    size_t cap;
    uint8_t *next;

    if (extra > SIZE_MAX - buffer->len)
        return false;
    needed = buffer->len + extra;
    if (needed <= buffer->cap)
        return true;
    cap = buffer->cap ? buffer->cap : 256u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }
    next = (uint8_t *)realloc(buffer->bytes, cap);
    if (!next)
        return false;
    buffer->bytes = next;
    buffer->cap = cap;
    return true;
}

static bool fhgt_v1_buffer_append(FHGroundTermV1Buffer *buffer,
                                  const void *bytes,
                                  size_t len) {
    if (!fhgt_v1_buffer_reserve(buffer, len))
        return false;
    if (len > 0u)
        memcpy(buffer->bytes + buffer->len, bytes, len);
    buffer->len += len;
    return true;
}

static bool fhgt_v1_buffer_byte(FHGroundTermV1Buffer *buffer, uint8_t byte) {
    return fhgt_v1_buffer_append(buffer, &byte, 1u);
}

static bool fhgt_v1_buffer_literal(FHGroundTermV1Buffer *buffer,
                                   const char *text) {
    return fhgt_v1_buffer_append(buffer, text, strlen(text));
}

static bool fhgt_v1_utf8_decode(const uint8_t *bytes,
                                size_t len,
                                size_t pos,
                                uint32_t *codepoint,
                                size_t *width) {
    uint8_t first;
    uint32_t value;
    uint32_t minimum;
    size_t count;
    size_t index;

    if (pos >= len)
        return false;
    first = bytes[pos];
    if (first <= 0x7fu) {
        *codepoint = first;
        *width = 1u;
        return true;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        value = (uint32_t)(first & 0x1fu);
        minimum = 0x80u;
        count = 2u;
    } else if (first >= 0xe0u && first <= 0xefu) {
        value = (uint32_t)(first & 0x0fu);
        minimum = 0x800u;
        count = 3u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        value = (uint32_t)(first & 0x07u);
        minimum = 0x10000u;
        count = 4u;
    } else {
        return false;
    }
    if (count > len - pos)
        return false;
    for (index = 1u; index < count; index++) {
        uint8_t continuation = bytes[pos + index];
        if ((continuation & 0xc0u) != 0x80u)
            return false;
        value = (value << 6u) | (uint32_t)(continuation & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) {
        return false;
    }
    *codepoint = value;
    *width = count;
    return true;
}

static bool fhgt_v1_unicode_space(uint32_t codepoint) {
    return (codepoint >= 0x09u && codepoint <= 0x0du) ||
           codepoint == 0x20u || codepoint == 0x85u ||
           codepoint == 0xa0u || codepoint == 0x1680u ||
           (codepoint >= 0x2000u && codepoint <= 0x200au) ||
           codepoint == 0x2028u || codepoint == 0x2029u ||
           codepoint == 0x202fu || codepoint == 0x205fu ||
           codepoint == 0x3000u;
}

static bool fhgt_v1_integer_token(const uint8_t *bytes, size_t len) {
    size_t pos = 0u;

    if (len > 0u && bytes[0] == (uint8_t)'-')
        pos = 1u;
    if (pos == len)
        return false;
    for (; pos < len; pos++) {
        if (bytes[pos] < (uint8_t)'0' || bytes[pos] > (uint8_t)'9')
            return false;
    }
    return true;
}

static bool fhgt_v1_symbol_bytes_valid(const uint8_t *bytes, size_t len) {
    size_t pos = 0u;

    if (len == 0u || fhgt_v1_integer_token(bytes, len) ||
        (len > 1u && bytes[0] == (uint8_t)'?')) {
        return false;
    }
    while (pos < len) {
        uint32_t codepoint;
        size_t width;

        if (!fhgt_v1_utf8_decode(bytes, len, pos, &codepoint, &width) ||
            fhgt_v1_unicode_space(codepoint) || codepoint == (uint32_t)'(' ||
            codepoint == (uint32_t)')' || codepoint == (uint32_t)';' ||
            codepoint == (uint32_t)'"') {
            return false;
        }
        pos += width;
    }
    return true;
}

static bool fhgt_v1_render_string(FHGroundTermV1Buffer *buffer,
                                  const char *text) {
    const uint8_t *bytes = (const uint8_t *)text;
    size_t len = strlen(text);
    size_t pos = 0u;

    if (!fhgt_v1_buffer_byte(buffer, (uint8_t)'"'))
        return false;
    while (pos < len) {
        uint32_t codepoint;
        size_t width;
        char escape[7];

        if (!fhgt_v1_utf8_decode(bytes, len, pos, &codepoint, &width))
            return false;
        switch (codepoint) {
        case '"':
            if (!fhgt_v1_buffer_literal(buffer, "\\\""))
                return false;
            break;
        case '\\':
            if (!fhgt_v1_buffer_literal(buffer, "\\\\"))
                return false;
            break;
        case 0x08u:
            if (!fhgt_v1_buffer_literal(buffer, "\\b"))
                return false;
            break;
        case 0x0cu:
            if (!fhgt_v1_buffer_literal(buffer, "\\f"))
                return false;
            break;
        case 0x0au:
            if (!fhgt_v1_buffer_literal(buffer, "\\n"))
                return false;
            break;
        case 0x0du:
            if (!fhgt_v1_buffer_literal(buffer, "\\r"))
                return false;
            break;
        case 0x09u:
            if (!fhgt_v1_buffer_literal(buffer, "\\t"))
                return false;
            break;
        default:
            if (codepoint < 0x20u) {
                (void)snprintf(escape, sizeof(escape), "\\u%04x", codepoint);
                if (!fhgt_v1_buffer_literal(buffer, escape))
                    return false;
            } else if (!fhgt_v1_buffer_append(buffer, bytes + pos, width)) {
                return false;
            }
            break;
        }
        pos += width;
    }
    return fhgt_v1_buffer_byte(buffer, (uint8_t)'"');
}

static size_t fhgt_v1_active_hash(const Atom *atom) {
    uintptr_t value = (uintptr_t)atom;
    value >>= 3u;
    value ^= value >> 17u;
    value *= (uintptr_t)UINT32_C(0xed5ad4bb);
    value ^= value >> 11u;
    return (size_t)value;
}

static bool fhgt_v1_active_rehash(FHGroundTermV1ActiveSet *set,
                                  size_t cap) {
    const Atom **atoms;
    uint8_t *states;
    size_t index;

    if (cap < 64u || (cap & (cap - 1u)) != 0u ||
        cap > SIZE_MAX / sizeof(*atoms)) {
        return false;
    }
    atoms = calloc(cap, sizeof(*atoms));
    states = calloc(cap, sizeof(*states));
    if (!atoms || !states) {
        free(atoms);
        free(states);
        return false;
    }
    for (index = 0u; index < set->cap; index++) {
        size_t position;
        if (set->states[index] != 1u)
            continue;
        position = fhgt_v1_active_hash(set->atoms[index]) & (cap - 1u);
        while (states[position] == 1u)
            position = (position + 1u) & (cap - 1u);
        atoms[position] = set->atoms[index];
        states[position] = 1u;
    }
    free(set->atoms);
    free(set->states);
    set->atoms = atoms;
    set->states = states;
    set->tombstones = 0u;
    set->cap = cap;
    return true;
}

static FHGroundTermV1ActiveInsert fhgt_v1_active_insert(
    FHGroundTermV1ActiveSet *set,
    const Atom *atom) {
    size_t position;
    size_t first_tombstone = SIZE_MAX;

    if (set->cap == 0u && !fhgt_v1_active_rehash(set, 64u))
        return FHGT_V1_ACTIVE_OOM;
    if (set->len + set->tombstones + 1u > set->cap / 2u) {
        size_t cap = set->len + 1u > set->cap / 4u
            ? set->cap * 2u : set->cap;
        if (cap < set->cap || !fhgt_v1_active_rehash(set, cap))
            return FHGT_V1_ACTIVE_OOM;
    }
    position = fhgt_v1_active_hash(atom) & (set->cap - 1u);
    while (set->states[position] != 0u) {
        if (set->states[position] == 1u && set->atoms[position] == atom)
            return FHGT_V1_ACTIVE_DUPLICATE;
        if (set->states[position] == 2u && first_tombstone == SIZE_MAX)
            first_tombstone = position;
        position = (position + 1u) & (set->cap - 1u);
    }
    if (first_tombstone != SIZE_MAX) {
        position = first_tombstone;
        set->tombstones--;
    }
    set->atoms[position] = atom;
    set->states[position] = 1u;
    set->len++;
    return FHGT_V1_ACTIVE_INSERTED;
}

static void fhgt_v1_active_remove(FHGroundTermV1ActiveSet *set,
                                  const Atom *atom) {
    size_t position;

    if (set->cap == 0u)
        return;
    position = fhgt_v1_active_hash(atom) & (set->cap - 1u);
    while (set->states[position] != 0u) {
        if (set->states[position] == 1u && set->atoms[position] == atom) {
            set->atoms[position] = NULL;
            set->states[position] = 2u;
            set->len--;
            set->tombstones++;
            return;
        }
        position = (position + 1u) & (set->cap - 1u);
    }
}

static void fhgt_v1_active_free(FHGroundTermV1ActiveSet *set) {
    free(set->atoms);
    free(set->states);
    memset(set, 0, sizeof(*set));
}

static bool fhgt_v1_render_leaf(FHGroundTermV1Buffer *buffer,
                                const Atom *atom,
                                char *error,
                                size_t error_cap) {
    char integer[64];

    if (!atom)
        return fhgt_v1_error(error, error_cap, "null finite-Horn term");
    switch (atom->kind) {
    case ATOM_SYMBOL: {
        const char *name = atom_name_cstr((Atom *)atom);
        size_t len = strlen(name);
        if (!fhgt_v1_symbol_bytes_valid((const uint8_t *)name, len)) {
            return fhgt_v1_error(error, error_cap,
                                 "symbol has no unambiguous finite-Horn encoding");
        }
        return fhgt_v1_buffer_append(buffer, name, len) ||
               fhgt_v1_error(error, error_cap,
                             "out of memory rendering finite-Horn symbol");
    }
    case ATOM_VAR:
        return fhgt_v1_error(error, error_cap,
                             "variable crossed a finite-Horn ground boundary");
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT) {
            int length = snprintf(integer, sizeof(integer), "%" PRId64,
                                  atom->ground.ival);
            if (length <= 0 || (size_t)length >= sizeof(integer))
                return fhgt_v1_error(error, error_cap,
                                     "integer rendering failed");
            return fhgt_v1_buffer_append(buffer, integer, (size_t)length) ||
                   fhgt_v1_error(error, error_cap,
                                 "out of memory rendering finite-Horn integer");
        }
        if (atom->ground.gkind == GV_BIGINT) {
            const char *bigint = atom_bigint_cstr((Atom *)atom);
            return fhgt_v1_buffer_literal(buffer, bigint) ||
                   fhgt_v1_error(error, error_cap,
                                 "out of memory rendering finite-Horn bigint");
        }
        if (atom->ground.gkind == GV_STRING) {
            return fhgt_v1_render_string(buffer, atom->ground.sval) ||
                   fhgt_v1_error(error, error_cap,
                                 "string is not valid finite-Horn UTF-8");
        }
        return fhgt_v1_error(error, error_cap,
                             "unsupported grounded value in finite-Horn term");
    case ATOM_EXPR:
        return fhgt_v1_error(error, error_cap,
                             "expression reached finite-Horn leaf renderer");
    }
    return fhgt_v1_error(error, error_cap, "unknown Atom kind");
}

static bool fhgt_v1_render_stack_push(
    FHGroundTermV1RenderStack *stack,
    FHGroundTermV1ActiveSet *active,
    const Atom *atom,
    FHGroundTermV1Buffer *buffer,
    char *error,
    size_t error_cap) {
    FHGroundTermV1ActiveInsert inserted;

    if (!atom || atom->kind != ATOM_EXPR ||
        !cetta_expr_len_fits_size(atom->expr.len)) {
        return fhgt_v1_error(
            error, error_cap, "malformed finite-Horn expression");
    }
    inserted = fhgt_v1_active_insert(active, atom);
    if (inserted == FHGT_V1_ACTIVE_DUPLICATE) {
        return fhgt_v1_error(
            error, error_cap, "cyclic finite-Horn term");
    }
    if (inserted == FHGT_V1_ACTIVE_OOM)
        return false;
    if (stack->len == stack->cap) {
        size_t cap = stack->cap ? stack->cap * 2u : 64u;
        FHGroundTermV1RenderFrame *next;
        if (cap < stack->cap || cap > SIZE_MAX / sizeof(*next)) {
            fhgt_v1_active_remove(active, atom);
            return false;
        }
        next = realloc(stack->data, sizeof(*next) * cap);
        if (!next) {
            fhgt_v1_active_remove(active, atom);
            return false;
        }
        stack->data = next;
        stack->cap = cap;
    }
    if (!fhgt_v1_buffer_byte(buffer, (uint8_t)'(')) {
        fhgt_v1_active_remove(active, atom);
        return false;
    }
    stack->data[stack->len++] = (FHGroundTermV1RenderFrame){
        .atom = atom,
        .next_child = 0u,
    };
    return true;
}

static bool fhgt_v1_render_iterative(FHGroundTermV1Buffer *buffer,
                                     const Atom *term,
                                     char *error,
                                     size_t error_cap) {
    FHGroundTermV1RenderStack stack = {0};
    FHGroundTermV1ActiveSet active = {0};
    bool ok = false;

    if (!term)
        return fhgt_v1_error(error, error_cap, "null finite-Horn term");
    if (term->kind != ATOM_EXPR) {
        return fhgt_v1_render_leaf(buffer, term, error, error_cap);
    }
    if (!fhgt_v1_render_stack_push(
            &stack, &active, term, buffer, error, error_cap)) {
        goto done;
    }
    while (stack.len > 0u) {
        FHGroundTermV1RenderFrame *frame = &stack.data[stack.len - 1u];
        const Atom *expression = frame->atom;
        if (frame->next_child == expression->expr.len) {
            if (!fhgt_v1_buffer_byte(buffer, (uint8_t)')'))
                goto done;
            fhgt_v1_active_remove(&active, expression);
            stack.len--;
            continue;
        }
        if (frame->next_child > 0u &&
            !fhgt_v1_buffer_byte(buffer, (uint8_t)' ')) {
            goto done;
        }
        {
            const Atom *child =
                expression->expr.elems[frame->next_child++];
            if (child && child->kind == ATOM_EXPR) {
                if (!fhgt_v1_render_stack_push(
                        &stack, &active, child, buffer,
                        error, error_cap)) {
                    goto done;
                }
            } else if (!fhgt_v1_render_leaf(
                           buffer, child, error, error_cap)) {
                goto done;
            }
        }
    }
    ok = true;

done:
    free(stack.data);
    fhgt_v1_active_free(&active);
    return ok;
}

bool fh_ground_term_v1_render(const Atom *term,
                              uint8_t **out,
                              size_t *out_len,
                              char *error,
                              size_t error_cap) {
    FHGroundTermV1Buffer buffer = {0};

    if (out)
        *out = NULL;
    if (out_len)
        *out_len = 0u;
    if (error && error_cap > 0u)
        error[0] = '\0';
    if (!out || !out_len)
        return fhgt_v1_error(error, error_cap,
                             "invalid finite-Horn render request");
    if (!fhgt_v1_render_iterative(&buffer, term, error, error_cap) ||
        !fhgt_v1_buffer_byte(&buffer, 0u)) {
        free(buffer.bytes);
        if (!error || error_cap == 0u || error[0] == '\0')
            (void)fhgt_v1_error(error, error_cap,
                                "out of memory rendering finite-Horn term");
        return false;
    }
    buffer.len--;
    *out = buffer.bytes;
    *out_len = buffer.len;
    return true;
}

static bool fhgt_v1_append_codepoint(FHGroundTermV1Buffer *buffer,
                                     uint32_t codepoint) {
    uint8_t encoded[4];
    size_t len;

    if (codepoint <= 0x7fu) {
        encoded[0] = (uint8_t)codepoint;
        len = 1u;
    } else if (codepoint <= 0x7ffu) {
        encoded[0] = (uint8_t)(0xc0u | (codepoint >> 6u));
        encoded[1] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        len = 2u;
    } else if (codepoint <= 0xffffu) {
        encoded[0] = (uint8_t)(0xe0u | (codepoint >> 12u));
        encoded[1] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[2] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        len = 3u;
    } else {
        encoded[0] = (uint8_t)(0xf0u | (codepoint >> 18u));
        encoded[1] = (uint8_t)(0x80u | ((codepoint >> 12u) & 0x3fu));
        encoded[2] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[3] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        len = 4u;
    }
    return fhgt_v1_buffer_append(buffer, encoded, len);
}

static int fhgt_v1_hex_digit(uint8_t byte) {
    if (byte >= (uint8_t)'0' && byte <= (uint8_t)'9')
        return (int)(byte - (uint8_t)'0');
    if (byte >= (uint8_t)'a' && byte <= (uint8_t)'f')
        return 10 + (int)(byte - (uint8_t)'a');
    if (byte >= (uint8_t)'A' && byte <= (uint8_t)'F')
        return 10 + (int)(byte - (uint8_t)'A');
    return -1;
}

static bool fhgt_v1_parse_hex4(FHGroundTermV1Parser *parser,
                               uint32_t *value) {
    uint32_t parsed = 0u;
    size_t index;

    if (parser->len - parser->pos < 4u) {
        return fhgt_v1_parser_error(
            parser, "incomplete Unicode escape at byte offset %zu",
            parser->pos);
    }
    for (index = 0u; index < 4u; index++) {
        int digit = fhgt_v1_hex_digit(parser->bytes[parser->pos + index]);
        if (digit < 0) {
            return fhgt_v1_parser_error(
                parser, "invalid Unicode escape at byte offset %zu",
                parser->pos + index);
        }
        parsed = (parsed << 4u) | (uint32_t)digit;
    }
    parser->pos += 4u;
    *value = parsed;
    return true;
}

static bool fhgt_v1_skip(FHGroundTermV1Parser *parser) {
    while (parser->pos < parser->len) {
        uint32_t codepoint;
        size_t width;

        if (parser->bytes[parser->pos] == (uint8_t)';') {
            while (parser->pos < parser->len &&
                   parser->bytes[parser->pos] != (uint8_t)'\n') {
                parser->pos++;
            }
            continue;
        }
        if (!fhgt_v1_utf8_decode(parser->bytes, parser->len, parser->pos,
                                 &codepoint, &width)) {
            return fhgt_v1_parser_error(
                parser, "invalid UTF-8 at byte offset %zu", parser->pos);
        }
        if (!fhgt_v1_unicode_space(codepoint))
            break;
        parser->pos += width;
    }
    return true;
}

static Atom *fhgt_v1_parse_string(FHGroundTermV1Parser *parser) {
    FHGroundTermV1Buffer decoded = {0};
    size_t start = parser->pos++;

    while (parser->pos < parser->len) {
        uint8_t byte = parser->bytes[parser->pos];
        if (byte == (uint8_t)'"') {
            Atom *term;

            parser->pos++;
            if (!fhgt_v1_buffer_byte(&decoded, 0u)) {
                (void)fhgt_v1_parser_error(
                    parser, "out of memory while parsing finite-Horn string");
                free(decoded.bytes);
                return NULL;
            }
            term = atom_string(parser->arena, (const char *)decoded.bytes);
            free(decoded.bytes);
            return term;
        }
        if (byte == (uint8_t)'\\') {
            uint32_t codepoint;

            parser->pos++;
            if (parser->pos >= parser->len) {
                (void)fhgt_v1_parser_error(
                    parser, "unterminated string at byte offset %zu", start);
                free(decoded.bytes);
                return NULL;
            }
            switch (parser->bytes[parser->pos++]) {
            case (uint8_t)'"': codepoint = (uint32_t)'"'; break;
            case (uint8_t)'\\': codepoint = (uint32_t)'\\'; break;
            case (uint8_t)'/': codepoint = (uint32_t)'/'; break;
            case (uint8_t)'b': codepoint = 0x08u; break;
            case (uint8_t)'f': codepoint = 0x0cu; break;
            case (uint8_t)'n': codepoint = 0x0au; break;
            case (uint8_t)'r': codepoint = 0x0du; break;
            case (uint8_t)'t': codepoint = 0x09u; break;
            case (uint8_t)'u': {
                uint32_t low;

                if (!fhgt_v1_parse_hex4(parser, &codepoint)) {
                    free(decoded.bytes);
                    return NULL;
                }
                if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                    if (parser->len - parser->pos < 6u ||
                        parser->bytes[parser->pos] != (uint8_t)'\\' ||
                        parser->bytes[parser->pos + 1u] != (uint8_t)'u') {
                        (void)fhgt_v1_parser_error(
                            parser,
                            "unpaired high surrogate at byte offset %zu",
                            parser->pos - 4u);
                        free(decoded.bytes);
                        return NULL;
                    }
                    parser->pos += 2u;
                    if (!fhgt_v1_parse_hex4(parser, &low)) {
                        free(decoded.bytes);
                        return NULL;
                    }
                    if (low < 0xdc00u || low > 0xdfffu) {
                        (void)fhgt_v1_parser_error(
                            parser,
                            "invalid low surrogate at byte offset %zu",
                            parser->pos - 4u);
                        free(decoded.bytes);
                        return NULL;
                    }
                    codepoint = 0x10000u +
                                ((codepoint - 0xd800u) << 10u) +
                                (low - 0xdc00u);
                } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
                    (void)fhgt_v1_parser_error(
                        parser, "unpaired low surrogate at byte offset %zu",
                        parser->pos - 4u);
                    free(decoded.bytes);
                    return NULL;
                }
                break;
            }
            default:
                (void)fhgt_v1_parser_error(
                    parser, "invalid string escape at byte offset %zu",
                    parser->pos - 1u);
                free(decoded.bytes);
                return NULL;
            }
            if (codepoint == 0u) {
                (void)fhgt_v1_parser_error(
                    parser,
                    "U+0000 is not representable by the CeTTa Atom string carrier");
                free(decoded.bytes);
                return NULL;
            }
            if (!fhgt_v1_append_codepoint(&decoded, codepoint)) {
                (void)fhgt_v1_parser_error(
                    parser, "out of memory while parsing finite-Horn string");
                free(decoded.bytes);
                return NULL;
            }
            continue;
        }
        {
            uint32_t codepoint;
            size_t width;

            if (!fhgt_v1_utf8_decode(parser->bytes, parser->len, parser->pos,
                                     &codepoint, &width)) {
                (void)fhgt_v1_parser_error(
                    parser, "invalid UTF-8 in string at byte offset %zu",
                    parser->pos);
                free(decoded.bytes);
                return NULL;
            }
            if (codepoint < 0x20u) {
                (void)fhgt_v1_parser_error(
                    parser,
                    "unescaped control character at byte offset %zu",
                    parser->pos);
                free(decoded.bytes);
                return NULL;
            }
            if (!fhgt_v1_buffer_append(
                    &decoded, parser->bytes + parser->pos, width)) {
                (void)fhgt_v1_parser_error(
                    parser, "out of memory while parsing finite-Horn string");
                free(decoded.bytes);
                return NULL;
            }
            parser->pos += width;
        }
    }
    (void)fhgt_v1_parser_error(
        parser, "unterminated string at byte offset %zu", start);
    free(decoded.bytes);
    return NULL;
}

static bool fhgt_v1_delimiter(FHGroundTermV1Parser *parser,
                              size_t pos,
                              size_t *width) {
    uint8_t byte = parser->bytes[pos];
    uint32_t codepoint;

    if (byte == (uint8_t)'(' || byte == (uint8_t)')' ||
        byte == (uint8_t)';' || byte == (uint8_t)'"') {
        *width = 1u;
        return true;
    }
    if (!fhgt_v1_utf8_decode(parser->bytes, parser->len, pos,
                             &codepoint, width)) {
        *width = 0u;
        return false;
    }
    return fhgt_v1_unicode_space(codepoint);
}

static Atom *fhgt_v1_parse_atom(FHGroundTermV1Parser *parser) {
    size_t start = parser->pos;
    size_t token_len;
    char *text;
    Atom *term;

    while (parser->pos < parser->len) {
        size_t width;
        bool delimiter = fhgt_v1_delimiter(parser, parser->pos, &width);
        if (width == 0u) {
            (void)fhgt_v1_parser_error(
                parser, "invalid UTF-8 at byte offset %zu", parser->pos);
            return NULL;
        }
        if (delimiter)
            break;
        parser->pos += width;
    }
    token_len = parser->pos - start;
    if (token_len == 0u) {
        (void)fhgt_v1_parser_error(
            parser, "invalid token at byte offset %zu", start);
        return NULL;
    }
    if (parser->bytes[start] == (uint8_t)'?' && token_len > 1u) {
        (void)fhgt_v1_parser_error(
            parser, "variable crossed a finite-Horn ground boundary");
        return NULL;
    }
    text = (char *)malloc(token_len + 1u);
    if (!text) {
        (void)fhgt_v1_parser_error(
            parser, "out of memory while parsing finite-Horn token");
        return NULL;
    }
    memcpy(text, parser->bytes + start, token_len);
    text[token_len] = '\0';
    if (fhgt_v1_integer_token(parser->bytes + start, token_len)) {
        char *end = NULL;
        long long value;

        errno = 0;
        value = strtoll(text, &end, 10);
        if (errno == 0 && end && *end == '\0')
            term = atom_int(parser->arena, (int64_t)value);
        else
            term = atom_bigint(parser->arena, text);
    } else {
        term = atom_symbol(parser->arena, text);
    }
    free(text);
    if (!term) {
        (void)fhgt_v1_parser_error(
            parser, "invalid or unrepresentable finite-Horn token");
    }
    return term;
}

static void fhgt_v1_parse_stack_free(FHGroundTermV1ParseStack *stack) {
    size_t index;

    for (index = 0u; index < stack->len; index++)
        free(stack->data[index].items);
    free(stack->data);
    memset(stack, 0, sizeof(*stack));
}

static bool fhgt_v1_parse_stack_push(FHGroundTermV1Parser *parser,
                                     FHGroundTermV1ParseStack *stack,
                                     size_t open_pos) {
    if (stack->len == stack->cap) {
        size_t cap = stack->cap ? stack->cap * 2u : 64u;
        FHGroundTermV1ParseFrame *next;

        if (cap < stack->cap || cap > SIZE_MAX / sizeof(*next)) {
            return fhgt_v1_parser_error(
                parser, "finite-Horn expression nesting is too large");
        }
        next = realloc(stack->data, cap * sizeof(*next));
        if (!next) {
            return fhgt_v1_parser_error(
                parser,
                "out of memory while parsing finite-Horn expression");
        }
        stack->data = next;
        stack->cap = cap;
    }
    stack->data[stack->len++] = (FHGroundTermV1ParseFrame){
        .items = NULL,
        .len = 0u,
        .cap = 0u,
        .open_pos = open_pos,
    };
    return true;
}

static bool fhgt_v1_parse_frame_append(FHGroundTermV1Parser *parser,
                                       FHGroundTermV1ParseFrame *frame,
                                       Atom *item) {
    if (frame->len == frame->cap) {
        size_t cap = frame->cap ? frame->cap * 2u : 8u;
        Atom **next;

        if (cap < frame->cap || cap > SIZE_MAX / sizeof(*next)) {
            return fhgt_v1_parser_error(
                parser, "finite-Horn expression is too wide");
        }
        next = realloc(frame->items, cap * sizeof(*next));
        if (!next) {
            return fhgt_v1_parser_error(
                parser,
                "out of memory while parsing finite-Horn expression");
        }
        frame->items = next;
        frame->cap = cap;
    }
    frame->items[frame->len++] = item;
    return true;
}

static Atom *fhgt_v1_parse_iterative(FHGroundTermV1Parser *parser) {
    FHGroundTermV1ParseStack stack = {0};
    Atom *root = NULL;
    bool complete = false;

    while (!complete) {
        Atom *node;

        if (!fhgt_v1_skip(parser))
            goto done;
        if (parser->pos >= parser->len) {
            if (stack.len > 0u) {
                (void)fhgt_v1_parser_error(
                    parser, "unclosed '(' at byte offset %zu",
                    stack.data[stack.len - 1u].open_pos);
            } else {
                (void)fhgt_v1_parser_error(
                    parser, "expected finite-Horn term");
            }
            goto done;
        }
        if (parser->bytes[parser->pos] == (uint8_t)'(') {
            size_t open_pos = parser->pos++;
            if (!fhgt_v1_parse_stack_push(parser, &stack, open_pos))
                goto done;
            continue;
        }
        if (parser->bytes[parser->pos] == (uint8_t)')') {
            FHGroundTermV1ParseFrame *frame;

            if (stack.len == 0u) {
                (void)fhgt_v1_parser_error(
                    parser, "unexpected ')' at byte offset %zu", parser->pos);
                goto done;
            }
            parser->pos++;
            frame = &stack.data[stack.len - 1u];
            node = atom_expr(
                parser->arena, frame->items, (CettaExprLen)frame->len);
            free(frame->items);
            frame->items = NULL;
            stack.len--;
        } else if (parser->bytes[parser->pos] == (uint8_t)'"') {
            node = fhgt_v1_parse_string(parser);
            if (!node)
                goto done;
        } else {
            node = fhgt_v1_parse_atom(parser);
            if (!node)
                goto done;
        }
        if (stack.len == 0u) {
            root = node;
            complete = true;
        } else if (!fhgt_v1_parse_frame_append(
                       parser, &stack.data[stack.len - 1u], node)) {
            goto done;
        }
    }

done:
    fhgt_v1_parse_stack_free(&stack);
    return complete ? root : NULL;
}

bool fh_ground_term_v1_parse(Arena *arena,
                             const uint8_t *bytes,
                             size_t len,
                             Atom **out,
                             char *error,
                             size_t error_cap) {
    FHGroundTermV1Parser parser = {
        .arena = arena,
        .bytes = bytes,
        .len = len,
        .pos = 0u,
        .error = error,
        .error_cap = error_cap,
    };
    Atom *term;
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;

    if (out)
        *out = NULL;
    if (error && error_cap > 0u)
        error[0] = '\0';
    if (!arena || !bytes || !out) {
        return fhgt_v1_error(error, error_cap,
                             "invalid finite-Horn parse request");
    }
    term = fhgt_v1_parse_iterative(&parser);
    if (!term || !fhgt_v1_skip(&parser))
        return false;
    if (parser.pos != len) {
        return fhgt_v1_error(
            error, error_cap,
            "trailing bytes after finite-Horn term at byte offset %zu",
            parser.pos);
    }
    if (!fh_ground_term_v1_render(term, &canonical, &canonical_len,
                                  error, error_cap)) {
        return false;
    }
    if (canonical_len != len || memcmp(canonical, bytes, len) != 0) {
        free(canonical);
        return fhgt_v1_error(error, error_cap,
                             "finite-Horn term is not canonically encoded");
    }
    free(canonical);
    *out = term;
    return true;
}
