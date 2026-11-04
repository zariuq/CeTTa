/* The space file's laws, executed (Mettapedia.Machines.SpaceFileLaws):
 * reading a file back gives the facts it was written from, in order and
 * multiplicity; a lookup on a first symbol through the side reads exactly
 * the facts a scan selects, and reads no other record; a symbol the
 * dictionary lacks answers nothing and reads no record.  Malformed files
 * are refused when opened. */
#define _POSIX_C_SOURCE 200809L
#include "space_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint64_t rng(void) {
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static int failures = 0;

static void fail(const char *what) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
}

/* Symbols of the generated facts: the empty one, one holding a zero byte,
 * and short ASCII ones that share prefixes. */
enum { ALPHABET = 24, MAX_ARITY = 4 };
static char alphabet_bytes[ALPHABET][8];
static uint32_t alphabet_length[ALPHABET];

static void make_alphabet(void) {
    alphabet_length[0] = 0u;
    alphabet_bytes[1][0] = 'z';
    alphabet_bytes[1][1] = '\0';
    alphabet_bytes[1][2] = 'z';
    alphabet_length[1] = 3u;
    for (int index = 2; index < ALPHABET; index++) {
        uint32_t length = 1u + (uint32_t)(index % 4);
        for (uint32_t at = 0u; at < length; at++)
            alphabet_bytes[index][at] = (char)('a' + (index + (int)at) % 5);
        alphabet_length[index] = length;
    }
}

static bool field_equal(CettaSpaceFileField a, const char *bytes, uint32_t length) {
    return a.length == length && (!length || memcmp(a.bytes, bytes, length) == 0);
}

static void temp_path(char *path, size_t size) {
    const char *dir = getenv("TMPDIR");
    snprintf(path, size, "%s/cetta-space-file-XXXXXX", dir && dir[0] ? dir : "/tmp");
    int fd = mkstemp(path);
    if (fd >= 0)
        close(fd);
}

static void check_round(int round) {
    size_t count = (size_t)(rng() % 400u);
    if (round == 0)
        count = 0u;
    CettaSpaceFileFact *facts = calloc(count ? count : 1u, sizeof(*facts));
    CettaSpaceFileField *fields = calloc((count ? count : 1u) * MAX_ARITY, sizeof(*fields));
    uint32_t *symbols = calloc((count ? count : 1u) * MAX_ARITY, sizeof(*symbols));
    for (size_t index = 0u; index < count; index++) {
        uint32_t arity = (uint32_t)(rng() % (MAX_ARITY + 1u));
        for (uint32_t field = 0u; field < arity; field++) {
            uint32_t symbol = (uint32_t)(rng() % ALPHABET);
            symbols[index * MAX_ARITY + field] = symbol;
            fields[index * MAX_ARITY + field] = (CettaSpaceFileField){
                alphabet_bytes[symbol], alphabet_length[symbol]};
        }
        facts[index] = (CettaSpaceFileFact){fields + index * MAX_ARITY, arity};
    }
    char path[256];
    temp_path(path, sizeof(path));
    if (!cetta_space_file_write(path, facts, count)) {
        fail("a file is written");
        goto done;
    }
    const char *error = NULL;
    CettaSpaceFile *file = cetta_space_file_open(path, &error);
    if (!file) {
        fprintf(stderr, "open: %s\n", error ? error : "?");
        fail("a written file opens");
        goto done;
    }
    /* Reading back gives the facts, in order and multiplicity. */
    if (cetta_space_file_fact_count(file) != count)
        fail("the file holds as many facts as were written");
    for (size_t index = 0u; index < count; index++) {
        CettaSpaceFileField read[MAX_ARITY];
        uint32_t arity = 0u;
        if (!cetta_space_file_fact(file, index, read, MAX_ARITY, &arity) ||
            arity != facts[index].arity) {
            fail("a fact reads back with its arity");
            break;
        }
        for (uint32_t field = 0u; field < arity; field++) {
            uint32_t symbol = symbols[index * MAX_ARITY + field];
            if (!field_equal(read[field], alphabet_bytes[symbol], alphabet_length[symbol])) {
                fail("a fact reads back with its symbols");
                break;
            }
        }
    }
    /* Lookups: every alphabet symbol and two the facts never use. */
    unsigned char *selected = calloc(count ? count : 1u, 1u);
    for (int key = -2; key < ALPHABET; key++) {
        const char *key_bytes = key >= 0 ? alphabet_bytes[key] : key == -1 ? "absent" : "zz\0";
        uint32_t key_length = key >= 0 ? alphabet_length[key] : key == -1 ? 6u : 3u;
        bool used = false;
        for (size_t index = 0u; index < count; index++) {
            selected[index] = facts[index].arity > 0u &&
                field_equal(facts[index].fields[0], key_bytes, key_length);
            if (facts[index].arity)
                for (uint32_t field = 0u; field < facts[index].arity; field++)
                    used = used || field_equal(facts[index].fields[field], key_bytes, key_length);
        }
        uint64_t before = cetta_space_file_records_read(file);
        CettaSpaceFileCursor cursor;
        bool found = cetta_space_file_seek(file, key_bytes, key_length, &cursor);
        if (found != used)
            fail("a seek finds exactly the symbols the dictionary holds");
        if (cetta_space_file_records_read(file) != before)
            fail("a seek reads no record");
        uint64_t fact, run = 0u;
        while (cetta_space_file_cursor_next(&cursor, &fact)) {
            run++;
            if (fact >= count || !selected[fact]) {
                fail("a lookup reads only facts with that first symbol");
                break;
            }
            selected[fact] = 2u;
            CettaSpaceFileField first;
            uint32_t arity;
            CettaSpaceFileField read[MAX_ARITY];
            if (!cetta_space_file_fact(file, fact, read, MAX_ARITY, &arity) || arity == 0u) {
                fail("a looked-up fact reads back");
                break;
            }
            first = read[0];
            if (!field_equal(first, key_bytes, key_length))
                fail("a looked-up fact starts with the key");
        }
        for (size_t index = 0u; index < count; index++)
            if (selected[index] == 1u) {
                fail("a lookup reads every fact with that first symbol");
                break;
            }
        if (cetta_space_file_records_read(file) != before + run)
            fail("a lookup reads only the records it answers");
        if (!used && run != 0u)
            fail("an absent symbol answers nothing");
    }
    free(selected);
    cetta_space_file_close(file);
done:
    unlink(path);
    free(facts);
    free(fields);
    free(symbols);
}

static bool fact_equal(const CettaSpaceFileFact *a, const CettaSpaceFileFact *b) {
    if (a->arity != b->arity)
        return false;
    for (uint32_t field = 0u; field < a->arity; field++)
        if (!field_equal(a->fields[field], b->fields[field].bytes, b->fields[field].length))
            return false;
    return true;
}

static void random_facts(CettaSpaceFileFact *facts, CettaSpaceFileField *fields,
                         size_t count) {
    for (size_t index = 0u; index < count; index++) {
        uint32_t arity = (uint32_t)(rng() % (MAX_ARITY + 1u));
        for (uint32_t field = 0u; field < arity; field++) {
            uint32_t symbol = (uint32_t)(rng() % ALPHABET);
            fields[index * MAX_ARITY + field] = (CettaSpaceFileField){
                alphabet_bytes[symbol], alphabet_length[symbol]};
        }
        facts[index] = (CettaSpaceFileFact){fields + index * MAX_ARITY, arity};
    }
}

/* Compaction keeps the space's facts: the file's facts no tombstone hides,
 * in order, then the added ones; and every lookup answers as a scan of
 * them (SpaceFileOverlay.compact_same_bag, renumber_reads_back). */
static void check_compaction(void) {
    size_t count = (size_t)(rng() % 200u);
    size_t added_count = (size_t)(rng() % 40u);
    size_t tomb_count = (size_t)(rng() % 12u);
    CettaSpaceFileFact *facts = calloc(count + 1u, sizeof(*facts));
    CettaSpaceFileField *fields = calloc((count + 1u) * MAX_ARITY, sizeof(*fields));
    CettaSpaceFileFact *added = calloc(added_count + 1u, sizeof(*added));
    CettaSpaceFileField *added_fields = calloc((added_count + 1u) * MAX_ARITY, sizeof(*added_fields));
    CettaSpaceFileFact *tombs = calloc(tomb_count + 1u, sizeof(*tombs));
    CettaSpaceFileField *tomb_fields = calloc((tomb_count + 1u) * MAX_ARITY, sizeof(*tomb_fields));
    random_facts(facts, fields, count);
    random_facts(added, added_fields, added_count);
    random_facts(tombs, tomb_fields, tomb_count);
    /* Half the tombstones name a fact the file holds. */
    for (size_t index = 0u; count && index < tomb_count; index += 2u)
        tombs[index] = facts[rng() % count];
    char path[256], compacted[256];
    temp_path(path, sizeof(path));
    temp_path(compacted, sizeof(compacted));
    CettaSpaceFile *file = NULL, *result = NULL;
    if (!cetta_space_file_write(path, facts, count) ||
        !(file = cetta_space_file_open(path, NULL))) {
        fail("a file for compaction is written and opened");
        goto done;
    }
    if (!cetta_space_file_compact(file, added, added_count, tombs, tomb_count, compacted) ||
        !(result = cetta_space_file_open(compacted, NULL))) {
        fail("a compacted file is written and opened");
        goto done;
    }
    /* The expected facts. */
    CettaSpaceFileFact *expected = calloc(count + added_count + 1u, sizeof(*expected));
    size_t expected_count = 0u;
    for (size_t index = 0u; index < count; index++) {
        bool gone = false;
        for (size_t tomb = 0u; tomb < tomb_count && !gone; tomb++)
            gone = fact_equal(&facts[index], &tombs[tomb]);
        if (!gone)
            expected[expected_count++] = facts[index];
    }
    for (size_t index = 0u; index < added_count; index++)
        expected[expected_count++] = added[index];
    if (cetta_space_file_fact_count(result) != expected_count)
        fail("compaction keeps as many facts as the space holds");
    for (size_t index = 0u; index < expected_count && index < cetta_space_file_fact_count(result); index++) {
        CettaSpaceFileField read[MAX_ARITY];
        uint32_t arity = 0u;
        CettaSpaceFileFact got = {read, 0u};
        if (!cetta_space_file_fact(result, index, read, MAX_ARITY, &arity)) {
            fail("a compacted fact reads back");
            break;
        }
        got.arity = arity;
        if (!fact_equal(&got, &expected[index])) {
            fail("compaction keeps the facts in order");
            break;
        }
    }
    for (int key = 0; key < ALPHABET; key++) {
        size_t want = 0u, run = 0u;
        for (size_t index = 0u; index < expected_count; index++)
            want += expected[index].arity > 0u &&
                field_equal(expected[index].fields[0], alphabet_bytes[key], alphabet_length[key]);
        CettaSpaceFileCursor cursor;
        uint64_t fact;
        cetta_space_file_seek(result, alphabet_bytes[key], alphabet_length[key], &cursor);
        while (cetta_space_file_cursor_next(&cursor, &fact))
            run++;
        if (run != want) {
            fail("a lookup in the compacted file answers as a scan");
            break;
        }
    }
    free(expected);
done:
    cetta_space_file_close(file);
    cetta_space_file_close(result);
    unlink(path);
    unlink(compacted);
    free(facts); free(fields); free(added); free(added_fields); free(tombs); free(tomb_fields);
}

/* A small file's bytes, for the negative controls. */
static unsigned char *small_file(size_t *size_out) {
    static CettaSpaceFileField ab[2] = {{"a", 1u}, {"b", 1u}};
    static CettaSpaceFileField bc[2] = {{"b", 1u}, {"c", 1u}};
    static CettaSpaceFileField ac[2] = {{"a", 1u}, {"c", 1u}};
    CettaSpaceFileFact facts[3] = {{ab, 2u}, {bc, 2u}, {ac, 2u}};
    char path[256];
    temp_path(path, sizeof(path));
    unsigned char *bytes = NULL;
    if (cetta_space_file_write(path, facts, 3u)) {
        FILE *stream = fopen(path, "rb");
        if (stream) {
            fseek(stream, 0, SEEK_END);
            long size = ftell(stream);
            fseek(stream, 0, SEEK_SET);
            bytes = malloc((size_t)size);
            if (bytes && fread(bytes, 1u, (size_t)size, stream) == (size_t)size)
                *size_out = (size_t)size;
            else {
                free(bytes);
                bytes = NULL;
            }
            fclose(stream);
        }
    }
    unlink(path);
    return bytes;
}

static uint64_t header_u64(const unsigned char *bytes, size_t at) {
    uint64_t value;
    memcpy(&value, bytes + at, sizeof(value));
    return value;
}

static void put_u64(unsigned char *bytes, size_t at, uint64_t value) {
    memcpy(bytes + at, &value, sizeof(value));
}

static void expect_refused(const unsigned char *bytes, size_t size, const char *what) {
    char path[256];
    temp_path(path, sizeof(path));
    FILE *stream = fopen(path, "wb");
    if (!stream || fwrite(bytes, 1u, size, stream) != size) {
        fail("a control is written");
        if (stream)
            fclose(stream);
        unlink(path);
        return;
    }
    fclose(stream);
    CettaSpaceFile *file = cetta_space_file_open(path, NULL);
    if (file) {
        fprintf(stderr, "accepted: %s\n", what);
        fail("a malformed file is refused");
        cetta_space_file_close(file);
    }
    unlink(path);
}

static void check_controls(void) {
    size_t size = 0u;
    unsigned char *good = small_file(&size);
    if (!good) {
        fail("the control file is written");
        return;
    }
    unsigned char *bad = malloc(size);
    uint64_t dict_bytes_off = header_u64(good, 40);
    uint64_t rec_bytes_off = header_u64(good, 56);
    uint64_t side_keys_off = header_u64(good, 64);
    uint64_t side_facts_off = header_u64(good, 72);

    expect_refused(good, size - 1u, "truncated");
    memcpy(bad, good, size); bad[0] = 'X';
    expect_refused(bad, size, "magic");
    memcpy(bad, good, size); bad[8] = 1u;
    expect_refused(bad, size, "version");
    memcpy(bad, good, size); put_u64(bad, 24, UINT64_MAX / 4u);
    expect_refused(bad, size, "fact count");
    memcpy(bad, good, size);
    { unsigned char swap = bad[dict_bytes_off]; bad[dict_bytes_off] = bad[dict_bytes_off + 1u]; bad[dict_bytes_off + 1u] = swap; }
    expect_refused(bad, size, "dictionary order");
    memcpy(bad, good, size); bad[rec_bytes_off + 1u] = 0x05u;
    expect_refused(bad, size, "record position");
    memcpy(bad, good, size); put_u64(bad, side_facts_off + 8u, header_u64(good, side_facts_off));
    expect_refused(bad, size, "side permutation");
    memcpy(bad, good, size); put_u64(bad, side_keys_off, 3u);
    expect_refused(bad, size, "side key");

    /* The unmodified control still opens. */
    char path[256];
    temp_path(path, sizeof(path));
    FILE *stream = fopen(path, "wb");
    if (stream) {
        fwrite(good, 1u, size, stream);
        fclose(stream);
        CettaSpaceFile *file = cetta_space_file_open(path, NULL);
        if (!file)
            fail("the unmodified control opens");
        cetta_space_file_close(file);
    }
    unlink(path);
    free(bad);
    free(good);
}

int main(void) {
    make_alphabet();
    for (int round = 0; round < 60; round++)
        check_round(round);
    for (int round = 0; round < 40; round++)
        check_compaction();
    check_controls();
    if (failures)
        return 1;
    printf("PASS: space files read back their facts, look up a first symbol through the side reading only its facts, answer an absent symbol without a read, and refuse 8 malformed controls (60 rounds); compaction keeps the space's facts, in order, and its lookups (40 rounds)\n");
    return 0;
}
