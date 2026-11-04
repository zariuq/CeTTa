#define _POSIX_C_SOURCE 200809L
#include "space_file.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    SPACE_FILE_HEADER_BYTES = 96,
    SPACE_FILE_VERSION = 0,
};

static const char space_file_magic[8] = {'C', 'E', 'T', 'T', 'A', 'S', 'F', '0'};

/* The header's fields, at fixed offsets. */
typedef struct {
    uint64_t symbol_count;
    uint64_t fact_count;
    uint64_t dict_index_off;
    uint64_t dict_bytes_off;
    uint64_t rec_index_off;
    uint64_t rec_bytes_off;
    uint64_t side_keys_off;
    uint64_t side_facts_off;
    uint64_t file_size;
} SpaceFileHeader;

struct CettaSpaceFile {
    const unsigned char *base;
    size_t size;
    SpaceFileHeader header;
    uint64_t records_read;
};

/* ---- byte helpers ------------------------------------------------------ */

static uint64_t load_u64(const unsigned char *at) {
    uint64_t value;
    memcpy(&value, at, sizeof(value));
    return value;
}

static int symbol_compare(const char *a, uint32_t a_len,
                          const char *b, uint32_t b_len) {
    uint32_t common = a_len < b_len ? a_len : b_len;
    int order = common ? memcmp(a, b, common) : 0;
    if (order)
        return order;
    return a_len < b_len ? -1 : a_len > b_len ? 1 : 0;
}

typedef struct {
    unsigned char *bytes;
    size_t length;
    size_t capacity;
    bool failed;
} ByteBuffer;

static void buffer_reserve(ByteBuffer *buffer, size_t extra) {
    if (buffer->failed)
        return;
    if (buffer->length + extra < buffer->length) {
        buffer->failed = true;
        return;
    }
    size_t needed = buffer->length + extra;
    if (needed <= buffer->capacity)
        return;
    size_t capacity = buffer->capacity ? buffer->capacity : 256u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            buffer->failed = true;
            return;
        }
        capacity *= 2u;
    }
    unsigned char *grown = realloc(buffer->bytes, capacity);
    if (!grown) {
        buffer->failed = true;
        return;
    }
    buffer->bytes = grown;
    buffer->capacity = capacity;
}

static void buffer_put(ByteBuffer *buffer, const void *bytes, size_t length) {
    buffer_reserve(buffer, length);
    if (buffer->failed || !length)
        return;
    memcpy(buffer->bytes + buffer->length, bytes, length);
    buffer->length += length;
}

static void buffer_put_u64(ByteBuffer *buffer, uint64_t value) {
    buffer_put(buffer, &value, sizeof(value));
}

static void buffer_put_varint(ByteBuffer *buffer, uint64_t value) {
    unsigned char bytes[10];
    size_t length = 0u;
    do {
        unsigned char byte = (unsigned char)(value & 0x7fu);
        value >>= 7u;
        if (value)
            byte |= 0x80u;
        bytes[length++] = byte;
    } while (value);
    buffer_put(buffer, bytes, length);
}

static void buffer_align8(ByteBuffer *buffer) {
    static const unsigned char zeros[8] = {0};
    size_t pad = (8u - (buffer->length & 7u)) & 7u;
    buffer_put(buffer, zeros, pad);
}

/* Reads a varint within [*at, end); false when it runs past `end` or past
 * 64 bits. */
static bool read_varint(const unsigned char **at, const unsigned char *end,
                        uint64_t *value_out) {
    uint64_t value = 0u;
    for (unsigned shift = 0u; shift < 64u; shift += 7u) {
        if (*at >= end)
            return false;
        unsigned char byte = *(*at)++;
        if (shift == 63u && (byte & 0x7eu))
            return false;
        value |= (uint64_t)(byte & 0x7fu) << shift;
        if (!(byte & 0x80u)) {
            *value_out = value;
            return true;
        }
    }
    return false;
}

/* ---- writing ----------------------------------------------------------- */

typedef struct {
    const char *bytes;
    uint32_t length;
} Symbol;

static int symbol_order(const void *left, const void *right) {
    const Symbol *a = left;
    const Symbol *b = right;
    return symbol_compare(a->bytes, a->length, b->bytes, b->length);
}

typedef struct {
    uint64_t key;
    uint64_t fact;
} SideEntry;

static int side_order(const void *left, const void *right) {
    const SideEntry *a = left;
    const SideEntry *b = right;
    if (a->key != b->key)
        return a->key < b->key ? -1 : 1;
    return a->fact < b->fact ? -1 : a->fact > b->fact ? 1 : 0;
}

/* The position of `field` in the sorted dictionary of `count` symbols. */
static uint64_t dictionary_position(const Symbol *dict, uint64_t count,
                                    const CettaSpaceFileField *field) {
    uint64_t low = 0u;
    uint64_t high = count;
    while (low < high) {
        uint64_t middle = low + (high - low) / 2u;
        int order = symbol_compare(dict[middle].bytes, dict[middle].length,
                                   field->bytes, field->length);
        if (order < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

bool cetta_space_file_write(const char *path, const CettaSpaceFileFact *facts,
                            size_t count) {
    if (!path || (count && !facts))
        return false;
    size_t field_total = 0u;
    for (size_t index = 0u; index < count; index++) {
        if (facts[index].arity && !facts[index].fields)
            return false;
        if (field_total + facts[index].arity < field_total)
            return false;
        field_total += facts[index].arity;
    }
    Symbol *dict = malloc(sizeof(*dict) * (field_total ? field_total : 1u));
    SideEntry *side = malloc(sizeof(*side) * (count ? count : 1u));
    uint64_t *rec_offsets = malloc(sizeof(*rec_offsets) * (count + 1u));
    ByteBuffer records = {0};
    ByteBuffer out = {0};
    bool ok = dict && side && rec_offsets;
    uint64_t symbols = 0u;
    for (size_t index = 0u; ok && index < count; index++)
        for (uint32_t field = 0u; field < facts[index].arity; field++) {
            dict[symbols].bytes = facts[index].fields[field].bytes;
            dict[symbols].length = facts[index].fields[field].length;
            if (dict[symbols].length && !dict[symbols].bytes)
                ok = false;
            symbols++;
        }
    if (ok && symbols) {
        qsort(dict, symbols, sizeof(*dict), symbol_order);
        uint64_t kept = 1u;
        for (uint64_t index = 1u; index < symbols; index++)
            if (symbol_order(&dict[kept - 1u], &dict[index]) != 0)
                dict[kept++] = dict[index];
        symbols = kept;
    }
    for (size_t index = 0u; ok && index < count; index++) {
        rec_offsets[index] = records.length;
        buffer_put_varint(&records, facts[index].arity);
        uint64_t first = 0u;
        for (uint32_t field = 0u; field < facts[index].arity; field++) {
            uint64_t position = dictionary_position(
                dict, symbols, &facts[index].fields[field]);
            if (field == 0u)
                first = position + 1u;
            buffer_put_varint(&records, position);
        }
        side[index] = (SideEntry){.key = first, .fact = index};
    }
    if (ok)
        rec_offsets[count] = records.length;
    ok = ok && !records.failed;
    if (ok)
        qsort(side, count, sizeof(*side), side_order);

    if (ok) {
        SpaceFileHeader header = {.symbol_count = symbols, .fact_count = count};
        /* Sections in order, each u64 array 8-aligned. */
        size_t dict_bytes_length = 0u;
        for (uint64_t index = 0u; index < symbols; index++)
            dict_bytes_length += dict[index].length;
        header.dict_index_off = SPACE_FILE_HEADER_BYTES;
        header.dict_bytes_off = header.dict_index_off + (symbols + 1u) * 8u;
        uint64_t rec_index_unaligned = header.dict_bytes_off + dict_bytes_length;
        header.rec_index_off = (rec_index_unaligned + 7u) & ~(uint64_t)7u;
        header.rec_bytes_off = header.rec_index_off + ((uint64_t)count + 1u) * 8u;
        uint64_t side_unaligned = header.rec_bytes_off + records.length;
        header.side_keys_off = (side_unaligned + 7u) & ~(uint64_t)7u;
        header.side_facts_off = header.side_keys_off + (uint64_t)count * 8u;
        header.file_size = header.side_facts_off + (uint64_t)count * 8u;

        buffer_put(&out, space_file_magic, sizeof(space_file_magic));
        uint32_t version = SPACE_FILE_VERSION;
        uint32_t reserved = 0u;
        buffer_put(&out, &version, sizeof(version));
        buffer_put(&out, &reserved, sizeof(reserved));
        buffer_put_u64(&out, header.symbol_count);
        buffer_put_u64(&out, header.fact_count);
        buffer_put_u64(&out, header.dict_index_off);
        buffer_put_u64(&out, header.dict_bytes_off);
        buffer_put_u64(&out, header.rec_index_off);
        buffer_put_u64(&out, header.rec_bytes_off);
        buffer_put_u64(&out, header.side_keys_off);
        buffer_put_u64(&out, header.side_facts_off);
        buffer_put_u64(&out, header.file_size);
        buffer_align8(&out);
        while (!out.failed && out.length < SPACE_FILE_HEADER_BYTES)
            buffer_put(&out, "", 1u);
        uint64_t offset = 0u;
        for (uint64_t index = 0u; index < symbols; index++) {
            buffer_put_u64(&out, offset);
            offset += dict[index].length;
        }
        buffer_put_u64(&out, offset);
        for (uint64_t index = 0u; index < symbols; index++)
            buffer_put(&out, dict[index].bytes, dict[index].length);
        buffer_align8(&out);
        for (size_t index = 0u; index <= count; index++)
            buffer_put_u64(&out, rec_offsets[index]);
        buffer_put(&out, records.bytes, records.length);
        buffer_align8(&out);
        for (size_t index = 0u; index < count; index++)
            buffer_put_u64(&out, side[index].key);
        for (size_t index = 0u; index < count; index++)
            buffer_put_u64(&out, side[index].fact);
        ok = !out.failed && out.length == header.file_size;
    }
    if (ok) {
        FILE *stream = fopen(path, "wb");
        ok = stream != NULL;
        if (ok) {
            ok = fwrite(out.bytes, 1u, out.length, stream) == out.length;
            ok = (fclose(stream) == 0) && ok;
        }
    }
    free(dict);
    free(side);
    free(rec_offsets);
    free(records.bytes);
    free(out.bytes);
    return ok;
}

/* ---- reading ----------------------------------------------------------- */

static bool span_ok(uint64_t offset, uint64_t length, uint64_t size) {
    return offset <= size && length <= size - offset;
}

static bool mul8_ok(uint64_t count, uint64_t *bytes_out) {
    if (count > UINT64_MAX / 8u)
        return false;
    *bytes_out = count * 8u;
    return true;
}

static const char *space_file_validate(CettaSpaceFile *file) {
    const unsigned char *base = file->base;
    uint64_t size = file->size;
    if (size < SPACE_FILE_HEADER_BYTES)
        return "shorter than a header";
    if (memcmp(base, space_file_magic, sizeof(space_file_magic)) != 0)
        return "not a space file";
    uint32_t version;
    memcpy(&version, base + 8, sizeof(version));
    if (version != SPACE_FILE_VERSION)
        return "unknown version";
    SpaceFileHeader *h = &file->header;
    h->symbol_count = load_u64(base + 16);
    h->fact_count = load_u64(base + 24);
    h->dict_index_off = load_u64(base + 32);
    h->dict_bytes_off = load_u64(base + 40);
    h->rec_index_off = load_u64(base + 48);
    h->rec_bytes_off = load_u64(base + 56);
    h->side_keys_off = load_u64(base + 64);
    h->side_facts_off = load_u64(base + 72);
    h->file_size = load_u64(base + 80);
    if (h->file_size != size)
        return "size disagrees with the header";
    uint64_t dict_index_bytes, rec_index_bytes, side_bytes;
    if (h->symbol_count == UINT64_MAX || h->fact_count == UINT64_MAX ||
        !mul8_ok(h->symbol_count + 1u, &dict_index_bytes) ||
        !mul8_ok(h->fact_count + 1u, &rec_index_bytes) ||
        !mul8_ok(h->fact_count, &side_bytes))
        return "counts overflow";
    if (h->dict_index_off < SPACE_FILE_HEADER_BYTES ||
        !span_ok(h->dict_index_off, dict_index_bytes, size) ||
        h->dict_bytes_off != h->dict_index_off + dict_index_bytes ||
        h->rec_index_off < h->dict_bytes_off ||
        !span_ok(h->rec_index_off, rec_index_bytes, size) ||
        h->rec_bytes_off != h->rec_index_off + rec_index_bytes ||
        h->side_keys_off < h->rec_bytes_off ||
        !span_ok(h->side_keys_off, side_bytes, size) ||
        h->side_facts_off != h->side_keys_off + side_bytes ||
        !span_ok(h->side_facts_off, side_bytes, size) ||
        h->side_facts_off + side_bytes != size)
        return "sections out of place";
    /* The dictionary: offsets ascending within its bytes, symbols strictly
     * ascending. */
    uint64_t dict_bytes_length = h->rec_index_off - h->dict_bytes_off;
    uint64_t previous = 0u;
    for (uint64_t index = 0u; index <= h->symbol_count; index++) {
        uint64_t offset = load_u64(base + h->dict_index_off + index * 8u);
        if (offset < previous || offset > dict_bytes_length)
            return "dictionary offsets out of order";
        if (index > 0u && index < h->symbol_count) {
            uint64_t start = load_u64(base + h->dict_index_off + (index - 1u) * 8u);
            uint64_t next = load_u64(base + h->dict_index_off + (index + 1u) * 8u);
            if (next < offset || next > dict_bytes_length)
                return "dictionary offsets out of order";
            if (offset - start > UINT32_MAX || next - offset > UINT32_MAX)
                return "a symbol too long";
            if (symbol_compare((const char *)base + h->dict_bytes_off + start,
                               (uint32_t)(offset - start),
                               (const char *)base + h->dict_bytes_off + offset,
                               (uint32_t)(next - offset)) >= 0)
                return "dictionary not strictly ascending";
        }
        previous = offset;
    }
    /* Records: each decodes exactly within its span, with positions inside
     * the dictionary. */
    uint64_t rec_bytes_length = h->side_keys_off - h->rec_bytes_off;
    unsigned char *seen = calloc(h->fact_count ? h->fact_count : 1u, 1u);
    if (!seen)
        return "out of memory";
    const char *problem = NULL;
    uint64_t previous_offset = 0u;
    for (uint64_t index = 0u; !problem && index <= h->fact_count; index++) {
        uint64_t offset = load_u64(base + h->rec_index_off + index * 8u);
        if (offset < previous_offset || offset > rec_bytes_length)
            problem = "record offsets out of order";
        previous_offset = offset;
    }
    for (uint64_t index = 0u; !problem && index < h->fact_count; index++) {
        uint64_t start = load_u64(base + h->rec_index_off + index * 8u);
        uint64_t end = load_u64(base + h->rec_index_off + (index + 1u) * 8u);
        const unsigned char *at = base + h->rec_bytes_off + start;
        const unsigned char *limit = base + h->rec_bytes_off + end;
        uint64_t arity, position, first = 0u;
        if (!read_varint(&at, limit, &arity) || arity > UINT32_MAX)
            problem = "a record's arity is malformed";
        for (uint64_t field = 0u; !problem && field < arity; field++) {
            if (!read_varint(&at, limit, &position) ||
                position >= h->symbol_count)
                problem = "a record names no symbol";
            else if (field == 0u)
                first = position + 1u;
        }
        if (!problem && at != limit)
            problem = "a record does not fill its span";
        /* The side entry for this fact must carry its key: checked below
         * against the side's own order. */
        (void)first;
    }
    /* The side: a permutation of the facts, keys ascending, each key the
     * one its fact's record gives. */
    uint64_t previous_key = 0u;
    for (uint64_t index = 0u; !problem && index < h->fact_count; index++) {
        uint64_t key = load_u64(base + h->side_keys_off + index * 8u);
        uint64_t fact = load_u64(base + h->side_facts_off + index * 8u);
        if (key < previous_key)
            problem = "side keys out of order";
        else if (fact >= h->fact_count || seen[fact])
            problem = "side is not a permutation of the facts";
        else {
            seen[fact] = 1u;
            uint64_t start = load_u64(base + h->rec_index_off + fact * 8u);
            uint64_t end = load_u64(base + h->rec_index_off + (fact + 1u) * 8u);
            const unsigned char *at = base + h->rec_bytes_off + start;
            const unsigned char *limit = base + h->rec_bytes_off + end;
            uint64_t arity, position;
            uint64_t expected = 0u;
            if (read_varint(&at, limit, &arity) && arity > 0u &&
                read_varint(&at, limit, &position))
                expected = position + 1u;
            if (key != expected)
                problem = "a side key disagrees with its record";
        }
        previous_key = key;
    }
    free(seen);
    return problem;
}

CettaSpaceFile *cetta_space_file_open(const char *path, const char **error) {
    const char *problem = NULL;
    CettaSpaceFile *file = NULL;
    int fd = path ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    struct stat info;
    if (fd < 0 || fstat(fd, &info) != 0 || info.st_size <= 0) {
        problem = "unreadable";
    } else {
        void *mapping = mmap(NULL, (size_t)info.st_size, PROT_READ,
                             MAP_PRIVATE, fd, 0);
        if (mapping == MAP_FAILED) {
            problem = "cannot be mapped";
        } else {
            file = calloc(1u, sizeof(*file));
            if (!file) {
                munmap(mapping, (size_t)info.st_size);
                problem = "out of memory";
            } else {
                file->base = mapping;
                file->size = (size_t)info.st_size;
                problem = space_file_validate(file);
                if (problem) {
                    munmap(mapping, file->size);
                    free(file);
                    file = NULL;
                }
            }
        }
    }
    if (fd >= 0)
        close(fd);
    if (error)
        *error = problem;
    return file;
}

void cetta_space_file_close(CettaSpaceFile *file) {
    if (!file)
        return;
    munmap((void *)file->base, file->size);
    free(file);
}

uint64_t cetta_space_file_fact_count(const CettaSpaceFile *file) {
    return file ? file->header.fact_count : 0u;
}

uint64_t cetta_space_file_symbol_count(const CettaSpaceFile *file) {
    return file ? file->header.symbol_count : 0u;
}

uint64_t cetta_space_file_records_read(const CettaSpaceFile *file) {
    return file ? file->records_read : 0u;
}

static CettaSpaceFileField space_file_symbol(const CettaSpaceFile *file,
                                             uint64_t position) {
    const SpaceFileHeader *h = &file->header;
    uint64_t start = load_u64(file->base + h->dict_index_off + position * 8u);
    uint64_t end = load_u64(file->base + h->dict_index_off + (position + 1u) * 8u);
    return (CettaSpaceFileField){
        .bytes = (const char *)file->base + h->dict_bytes_off + start,
        .length = (uint32_t)(end - start)};
}

bool cetta_space_file_fact(const CettaSpaceFile *file, uint64_t index,
                           CettaSpaceFileField *fields, uint32_t capacity,
                           uint32_t *arity_out) {
    if (!file || index >= file->header.fact_count)
        return false;
    const SpaceFileHeader *h = &file->header;
    uint64_t start = load_u64(file->base + h->rec_index_off + index * 8u);
    uint64_t end = load_u64(file->base + h->rec_index_off + (index + 1u) * 8u);
    const unsigned char *at = file->base + h->rec_bytes_off + start;
    const unsigned char *limit = file->base + h->rec_bytes_off + end;
    uint64_t arity;
    if (!read_varint(&at, limit, &arity) || arity > capacity)
        return false;
    for (uint64_t field = 0u; field < arity; field++) {
        uint64_t position;
        if (!read_varint(&at, limit, &position))
            return false;
        if (fields)
            fields[field] = space_file_symbol(file, position);
    }
    ((CettaSpaceFile *)file)->records_read++;
    if (arity_out)
        *arity_out = (uint32_t)arity;
    return true;
}

bool cetta_space_file_seek(const CettaSpaceFile *file,
                           const char *key, uint32_t key_length,
                           CettaSpaceFileCursor *cursor) {
    if (!cursor)
        return false;
    *cursor = (CettaSpaceFileCursor){.file = file, .next = 0u, .end = 0u};
    if (!file || (key_length && !key))
        return false;
    const SpaceFileHeader *h = &file->header;
    /* The key's position in the dictionary, by binary search. */
    uint64_t low = 0u;
    uint64_t high = h->symbol_count;
    while (low < high) {
        uint64_t middle = low + (high - low) / 2u;
        CettaSpaceFileField symbol = space_file_symbol(file, middle);
        if (symbol_compare(symbol.bytes, symbol.length, key, key_length) < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low == h->symbol_count)
        return false;
    CettaSpaceFileField found = space_file_symbol(file, low);
    if (symbol_compare(found.bytes, found.length, key, key_length) != 0)
        return false;
    uint64_t wanted = low + 1u;
    /* The run of side entries carrying that key. */
    uint64_t first = 0u, last = h->fact_count;
    while (first < last) {
        uint64_t middle = first + (last - first) / 2u;
        if (load_u64(file->base + h->side_keys_off + middle * 8u) < wanted)
            first = middle + 1u;
        else
            last = middle;
    }
    uint64_t past = first, bound = h->fact_count;
    while (past < bound) {
        uint64_t middle = past + (bound - past) / 2u;
        if (load_u64(file->base + h->side_keys_off + middle * 8u) <= wanted)
            past = middle + 1u;
        else
            bound = middle;
    }
    cursor->next = first;
    cursor->end = past;
    return true;
}

bool cetta_space_file_cursor_next(CettaSpaceFileCursor *cursor,
                                  uint64_t *fact_index) {
    if (!cursor || !cursor->file || cursor->next >= cursor->end)
        return false;
    const SpaceFileHeader *h = &cursor->file->header;
    uint64_t fact = load_u64(cursor->file->base + h->side_facts_off +
                             cursor->next * 8u);
    cursor->next++;
    if (fact_index)
        *fact_index = fact;
    return true;
}

/* ---- compaction -------------------------------------------------------- */

static int fact_compare(const CettaSpaceFileFact *a, const CettaSpaceFileFact *b) {
    if (a->arity != b->arity)
        return a->arity < b->arity ? -1 : 1;
    for (uint32_t field = 0u; field < a->arity; field++) {
        int order = symbol_compare(a->fields[field].bytes, a->fields[field].length,
                                   b->fields[field].bytes, b->fields[field].length);
        if (order)
            return order;
    }
    return 0;
}

static int fact_order(const void *left, const void *right) {
    return fact_compare(left, right);
}

bool cetta_space_file_compact(const CettaSpaceFile *file,
                              const CettaSpaceFileFact *added,
                              size_t added_count,
                              const CettaSpaceFileFact *tombstones,
                              size_t tombstone_count, const char *path) {
    if (!file || !path || (added_count && !added) ||
        (tombstone_count && !tombstones))
        return false;
    uint64_t file_count = file->header.fact_count;
    if (file_count > SIZE_MAX - added_count)
        return false;
    /* The tombstones sorted, so each file fact is looked up by bisection. */
    CettaSpaceFileFact *hidden = malloc(sizeof(*hidden) *
                                        (tombstone_count ? tombstone_count : 1u));
    /* Each surviving file fact's fields, pointing into the mapping. */
    CettaSpaceFileFact *kept = malloc(sizeof(*kept) *
                                      ((size_t)file_count + added_count + 1u));
    size_t field_room = 0u;
    for (uint64_t index = 0u; index < file_count; index++) {
        uint64_t start = load_u64(file->base + file->header.rec_index_off + index * 8u);
        const unsigned char *at = file->base + file->header.rec_bytes_off + start;
        const unsigned char *limit = file->base + file->header.side_keys_off;
        uint64_t arity = 0u;
        if (read_varint(&at, limit, &arity))
            field_room += (size_t)arity;
    }
    CettaSpaceFileField *fields = malloc(sizeof(*fields) * (field_room ? field_room : 1u));
    bool ok = hidden && kept && fields;
    if (ok && tombstone_count) {
        memcpy(hidden, tombstones, sizeof(*hidden) * tombstone_count);
        qsort(hidden, tombstone_count, sizeof(*hidden), fact_order);
    }
    size_t kept_count = 0u;
    size_t used = 0u;
    for (uint64_t index = 0u; ok && index < file_count; index++) {
        uint32_t arity = 0u;
        CettaSpaceFileField *slot = fields + used;
        if (!cetta_space_file_fact(file, index, slot,
                                   (uint32_t)(field_room - used), &arity)) {
            ok = false;
            break;
        }
        CettaSpaceFileFact fact = {.fields = slot, .arity = arity};
        bool gone = false;
        size_t low = 0u, high = tombstone_count;
        while (low < high) {
            size_t middle = low + (high - low) / 2u;
            int order = fact_compare(&hidden[middle], &fact);
            if (order == 0) {
                gone = true;
                break;
            }
            if (order < 0)
                low = middle + 1u;
            else
                high = middle;
        }
        if (!gone) {
            kept[kept_count++] = fact;
            used += arity;
        }
    }
    for (size_t index = 0u; ok && index < added_count; index++)
        kept[kept_count++] = added[index];
    if (ok)
        ok = cetta_space_file_write(path, kept, kept_count);
    free(hidden);
    free(kept);
    free(fields);
    return ok;
}
