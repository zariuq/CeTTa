#include "atom_blob.h"

#include <stdlib.h>
#include <string.h>

enum {
    ATOM_BLOB_TAG_SYMBOL = 0x01,
    ATOM_BLOB_TAG_VAR = 0x02,
    ATOM_BLOB_TAG_EXPR = 0x03,
    ATOM_BLOB_TAG_INT = 0x04,
    ATOM_BLOB_TAG_FLOAT = 0x05,
    ATOM_BLOB_TAG_BOOL = 0x06,
    ATOM_BLOB_TAG_STRING = 0x07,
    ATOM_BLOB_TAG_BIGINT = 0x08,
    ATOM_BLOB_TAG_RATIONAL = 0x09,
};

typedef struct {
    const unsigned char *data;
    uint32_t len;
    uint32_t pos;
    const char *strings;
    uint32_t string_bytes;
    AtomBlobStatus status;
} AtomBlobReader;

typedef struct {
    Atom **children;
    uint16_t count;
    uint16_t filled;
} AtomBlobFrame;

typedef struct {
    AtomBlobFrame inline_frames[16];
    AtomBlobFrame *frames;
    size_t len;
    size_t cap;
} AtomBlobStack;

const char *atom_blob_status_name(AtomBlobStatus status) {
    switch (status) {
    case ATOM_BLOB_OK: return "AtomBlobOk";
    case ATOM_BLOB_NULL_ARGUMENT: return "AtomBlobNullArgument";
    case ATOM_BLOB_BAD_MAGIC: return "AtomBlobBadMagic";
    case ATOM_BLOB_BAD_VERSION: return "AtomBlobBadVersion";
    case ATOM_BLOB_TRUNCATED: return "AtomBlobTruncated";
    case ATOM_BLOB_BAD_STRING_TABLE: return "AtomBlobBadStringTable";
    case ATOM_BLOB_BAD_ATOM_COUNT: return "AtomBlobBadAtomCount";
    case ATOM_BLOB_UNSUPPORTED_GROUND_TAG:
        return "AtomBlobUnsupportedGroundTag";
    case ATOM_BLOB_CAPACITY_OVERFLOW: return "AtomBlobCapacityOverflow";
    case ATOM_BLOB_TRAILING_DATA: return "AtomBlobTrailingData";
    }
    return "AtomBlobUnknownStatus";
}

static bool atom_blob_take(AtomBlobReader *reader, uint32_t count,
                           const unsigned char **bytes) {
    if (!reader || count > reader->len - reader->pos) {
        if (reader) reader->status = ATOM_BLOB_TRUNCATED;
        return false;
    }
    if (bytes) *bytes = reader->data + reader->pos;
    reader->pos += count;
    return true;
}

static bool atom_blob_u8(AtomBlobReader *reader, uint8_t *value) {
    const unsigned char *bytes = NULL;
    if (!atom_blob_take(reader, 1u, &bytes)) return false;
    *value = bytes[0];
    return true;
}

static bool atom_blob_u16(AtomBlobReader *reader, uint16_t *value) {
    const unsigned char *bytes = NULL;
    if (!atom_blob_take(reader, 2u, &bytes)) return false;
    memcpy(value, bytes, sizeof(*value));
    return true;
}

static bool atom_blob_u32(AtomBlobReader *reader, uint32_t *value) {
    const unsigned char *bytes = NULL;
    if (!atom_blob_take(reader, 4u, &bytes)) return false;
    memcpy(value, bytes, sizeof(*value));
    return true;
}

static bool atom_blob_i64(AtomBlobReader *reader, int64_t *value) {
    const unsigned char *bytes = NULL;
    if (!atom_blob_take(reader, 8u, &bytes)) return false;
    memcpy(value, bytes, sizeof(*value));
    return true;
}

static const char *atom_blob_string(AtomBlobReader *reader, uint16_t offset) {
    if (offset >= reader->string_bytes) {
        reader->status = ATOM_BLOB_BAD_STRING_TABLE;
        return NULL;
    }
    const char *text = reader->strings + offset;
    size_t remaining = (size_t)reader->string_bytes - (size_t)offset;
    if (!memchr(text, '\0', remaining)) {
        reader->status = ATOM_BLOB_BAD_STRING_TABLE;
        return NULL;
    }
    return text;
}

static void atom_blob_stack_init(AtomBlobStack *stack) {
    stack->frames = stack->inline_frames;
    stack->len = 0u;
    stack->cap = sizeof(stack->inline_frames) /
                 sizeof(stack->inline_frames[0]);
}

static void atom_blob_stack_free(AtomBlobStack *stack) {
    if (stack->frames != stack->inline_frames) free(stack->frames);
}

static bool atom_blob_stack_push(AtomBlobStack *stack, AtomBlobFrame frame) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->frames))
            return false;
        size_t next_cap = stack->cap * 2u;
        if (stack->frames == stack->inline_frames) {
            AtomBlobFrame *next = cetta_malloc(
                sizeof(*next) * next_cap);
            memcpy(next, stack->inline_frames,
                   sizeof(*next) * stack->len);
            stack->frames = next;
        } else {
            stack->frames = cetta_realloc(
                stack->frames, sizeof(*stack->frames) * next_cap);
        }
        stack->cap = next_cap;
    }
    stack->frames[stack->len++] = frame;
    return true;
}

static bool atom_blob_validate_strings(AtomBlobReader *reader,
                                       uint16_t string_count) {
    uint32_t offset = 0u;
    for (uint16_t i = 0u; i < string_count; i++) {
        if (offset >= reader->string_bytes) return false;
        const char *start = reader->strings + offset;
        size_t remaining = (size_t)reader->string_bytes - (size_t)offset;
        const char *end = memchr(start, '\0', remaining);
        if (!end) return false;
        offset += (uint32_t)(end - start) + 1u;
    }
    return offset == reader->string_bytes;
}

static Atom *atom_blob_decode_ground_atom(AtomBlobReader *reader,
                                          Arena *arena) {
    AtomBlobStack stack;
    Atom *node = NULL;
    atom_blob_stack_init(&stack);

    for (;;) {
        uint8_t tag = 0u;
        if (!atom_blob_u8(reader, &tag)) goto fail;
        if (tag == ATOM_BLOB_TAG_SYMBOL) {
            uint16_t offset = 0u;
            if (!atom_blob_u16(reader, &offset)) goto fail;
            const char *name = atom_blob_string(reader, offset);
            if (!name) goto fail;
            node = atom_symbol(arena, name);
        } else if (tag == ATOM_BLOB_TAG_INT) {
            int64_t value = 0;
            if (!atom_blob_i64(reader, &value)) goto fail;
            node = atom_int(arena, value);
        } else if (tag == ATOM_BLOB_TAG_EXPR) {
            uint16_t count = 0u;
            if (!atom_blob_u16(reader, &count)) goto fail;
            Atom **children = count
                ? arena_alloc(arena, sizeof(*children) * (size_t)count)
                : NULL;
            if (count > 0u) {
                if (!atom_blob_stack_push(
                        &stack, (AtomBlobFrame){children, count, 0u})) {
                    reader->status = ATOM_BLOB_CAPACITY_OVERFLOW;
                    goto fail;
                }
                continue;
            }
            node = atom_expr(arena, NULL, 0u);
        } else {
            reader->status = ATOM_BLOB_UNSUPPORTED_GROUND_TAG;
            goto fail;
        }

        while (stack.len > 0u) {
            AtomBlobFrame *frame = &stack.frames[stack.len - 1u];
            frame->children[frame->filled++] = node;
            if (frame->filled < frame->count) {
                node = NULL;
                break;
            }
            node = atom_expr(arena, frame->children, frame->count);
            stack.len--;
        }
        if (!node) continue;
        if (stack.len == 0u) break;
    }

    atom_blob_stack_free(&stack);
    return node;

fail:
    atom_blob_stack_free(&stack);
    return NULL;
}

Atom *atom_blob_decode_single_ground(Arena *arena,
                                     const unsigned char *blob,
                                     uint32_t blob_len,
                                     AtomBlobStatus *status_out) {
    AtomBlobStatus status = ATOM_BLOB_OK;
    Atom *result = NULL;
    if (!arena || !blob) {
        status = ATOM_BLOB_NULL_ARGUMENT;
        goto done;
    }

    AtomBlobReader reader = {
        .data = blob,
        .len = blob_len,
        .pos = 0u,
        .strings = NULL,
        .string_bytes = 0u,
        .status = ATOM_BLOB_OK,
    };
    const unsigned char *magic = NULL;
    if (!atom_blob_take(&reader, 4u, &magic)) goto reader_error;
    if (memcmp(magic, "CSTD", 4u) != 0) {
        status = ATOM_BLOB_BAD_MAGIC;
        goto done;
    }

    uint16_t version = 0u;
    uint16_t string_count = 0u;
    uint32_t string_bytes = 0u;
    if (!atom_blob_u16(&reader, &version) ||
        !atom_blob_u16(&reader, &string_count) ||
        !atom_blob_u32(&reader, &string_bytes))
        goto reader_error;
    if (version != 1u) {
        status = ATOM_BLOB_BAD_VERSION;
        goto done;
    }

    const unsigned char *strings = NULL;
    if (!atom_blob_take(&reader, string_bytes, &strings)) goto reader_error;
    reader.strings = (const char *)strings;
    reader.string_bytes = string_bytes;
    if (!atom_blob_validate_strings(&reader, string_count)) {
        status = ATOM_BLOB_BAD_STRING_TABLE;
        goto done;
    }

    uint16_t atom_count = 0u;
    if (!atom_blob_u16(&reader, &atom_count)) goto reader_error;
    if (atom_count != 1u) {
        status = ATOM_BLOB_BAD_ATOM_COUNT;
        goto done;
    }
    result = atom_blob_decode_ground_atom(&reader, arena);
    if (!result) goto reader_error;
    if (reader.pos != reader.len) {
        result = NULL;
        status = ATOM_BLOB_TRAILING_DATA;
        goto done;
    }
    status = ATOM_BLOB_OK;
    goto done;

reader_error:
    status = reader.status == ATOM_BLOB_OK
        ? ATOM_BLOB_TRUNCATED : reader.status;
done:
    if (status_out) *status_out = status;
    return result;
}
