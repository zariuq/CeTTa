/*
 * The splittable name supply (src/name_supply.h): SipHash-2-4 with 128-bit
 * output against the reference vectors and OpenSSL, the numeral and its
 * spelling, the order of names, split paths, and that no depth, width or
 * count fails: a chain of a million levels, a million siblings and more
 * children than one word numbers, a counter past its end, a supply outside
 * the split tree, and millions of names from a random tree of supplies, all
 * distinct as names and as spellings, the same on a second run, with every
 * block given back.  Run once per mode: CETTA_NAME_SUPPLY=counter, and
 * CETTA_NAME_SUPPLY=unforgeable with
 * CETTA_NAME_SEED=000102030405060708090a0b0c0d0e0f.
 */
#include "name_supply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static long g_checks;

#define CHECK(condition, ...)                                         \
    do {                                                              \
        g_checks++;                                                   \
        if (!(condition)) {                                           \
            g_failures++;                                             \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                    \
            fputc('\n', stderr);                                      \
        }                                                             \
    } while (0)

static bool g_unforgeable;

/* SipHash-2-4 with 128-bit output of the bytes 0, 1, ..., n-1 under the key
 * 00 01 ... 0f, for n = 0 .. 63: the specification's vectors_sip128. */
static const char *const k_sip128_vectors[64] = {
    "a3817f04ba25a8e66df67214c7550293",
    "da87c1d86b99af44347659119b22fc45",
    "8177228da4a45dc7fca38bdef60affe4",
    "9c70b60c5267a94e5f33b6b02985ed51",
    "f88164c12d9c8faf7d0f6e7c7bcd5579",
    "1368875980776f8854527a07690e9627",
    "14eeca338b208613485ea0308fd7a15e",
    "a1f1ebbed8dbc153c0b84aa61ff08239",
    "3b62a9ba6258f5610f83e264f31497b4",
    "264499060ad9baabc47f8b02bb6d71ed",
    "00110dc378146956c95447d3f3d0fbba",
    "0151c568386b6677a2b4dc6f81e5dc18",
    "d626b266905ef35882634df68532c125",
    "9869e247e9c08b10d029934fc4b952f7",
    "31fcefac66d7de9c7ec7485fe4494902",
    "5493e99933b0a8117e08ec0f97cfc3d9",
    "6ee2a4ca67b054bbfd3315bf85230577",
    "473d06e8738db89854c066c47ae47740",
    "a426e5e423bf4885294da481feaef723",
    "78017731cf65fab074d5208952512eb1",
    "9e25fc833f2290733e9344a5e83839eb",
    "568e495abe525a218a2214cd3e071d12",
    "4a29b54552d16b9a469c10528eff0aae",
    "c9d184ddd5a9f5e0cf8ce29a9abf691c",
    "2db479ae78bd50d8882a8a178a6132ad",
    "8ece5f042d5e447b5051b9eacb8d8f6f",
    "9c0b53b4b3c307e87eaee08678141f66",
    "abf248af69a6eae4bfd3eb2f129eeb94",
    "0664da1668574b88b935f3027358aef4",
    "aa4b9dc4bf337de90cd4fd3c467c6ab7",
    "ea5c7f471faf6bde2b1ad7d4686d2287",
    "2939b0183223fafc1723de4f52c43d35",
    "7c3956ca5eeafc3e363e9d556546eb68",
    "77c6077146f01c32b6b69d5f4ea9ffcf",
    "37a6986cb8847edf0925f0f1309b54de",
    "a705f0e69da9a8f907241a2e923c8cc8",
    "3dc47d1f29c448461e9e76ed904f6711",
    "0d62bf01e6fc0e1a0d3c4751c5d3692b",
    "8c03468bca7c669ee4fd5e084bbee7b5",
    "528a5bb93baf2c9c4473cce5d0d22bd9",
    "df6a301e95c95dad97ae0cc8c6913bd8",
    "801189902c857f39e73591285e70b6db",
    "e617346ac9c231bb3650ae34ccca0c5b",
    "27d93437efb721aa401821dcec5adf89",
    "89237d9ded9c5e78d8b1c9b166cc7342",
    "4a6d8091bf5e7d651189fa94a250b14c",
    "0e33f96055e7ae893ffc0e3dcf492902",
    "e61c432b720b19d18ec8d84bdc63151b",
    "f7e5aef549f782cf379055a608269b16",
    "438d030fd0b7a54fa837f2ad201a6403",
    "a590d3ee4fbf04e3247e0d27f286423f",
    "5fe2c1a172fe93c4b15cd37caef9f538",
    "2c97325cbd06b36eb2133dd08b3a017c",
    "92c814227a6bca949ff0659f002ad39e",
    "dce850110bd8328cfbd50841d6911d87",
    "67f14984c7da791248e32bb5922583da",
    "1938f2cf72d54ee97e94166fa91d2a36",
    "74481e9646ed49fe0f6224301604698e",
    "57fca5de98a9d6d8006438d0583d8a1d",
    "9fecde1cefdc1cbed4763674d9575359",
    "e3040c00eb28f15366ca73cbd872e740",
    "7697009a6a831dfecca91c5993670f7a",
    "5853542321f567a005d547a4f04759bd",
    "5150d1772f50834a503e069a973fbd7c",
};

static void hex_bytes(const uint8_t *bytes, size_t length, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0u; i < length; i++) {
        out[2u * i] = hex[bytes[i] >> 4];
        out[2u * i + 1u] = hex[bytes[i] & 15u];
    }
    out[2u * length] = '\0';
}

static void test_siphash_vectors(void) {
    uint8_t key[16], message[64], digest[16];
    char text[33];
    for (unsigned i = 0u; i < 16u; i++) key[i] = (uint8_t)i;
    for (unsigned i = 0u; i < 64u; i++) message[i] = (uint8_t)i;
    for (unsigned length = 0u; length < 64u; length++) {
        cetta_siphash24_128(key, message, length, digest);
        hex_bytes(digest, 16u, text);
        CHECK(strcmp(text, k_sip128_vectors[length]) == 0,
              "SipHash-2-4-128 vector %u: %s, expected %s", length, text,
              k_sip128_vectors[length]);
    }
}

/* ── Names by hand ───────────────────────────────────────────────────── */

static CettaGeneratedName counted(const CettaNameBlock *block, uint64_t path,
                                  uint64_t counter) {
    return (CettaGeneratedName){path, counter,
                                (uintptr_t)block |
                                    (uintptr_t)CETTA_NAME_FORM_COUNTED};
}

/* The path of RANKS below the root's start bit. */
static uint64_t path_of(const uint64_t *ranks, size_t count) {
    uint64_t path = 1u;
    for (size_t i = 0u; i < count; i++) {
        uint64_t code = ranks[i] + 1u;
        unsigned width = 2u * (63u - (unsigned)__builtin_clzll(code)) + 1u;
        path = (path << width) | code;
    }
    return path;
}

static bool numeral_is(CettaGeneratedName name, const char *expected) {
    char text[CETTA_GENERATED_NAME_NUMERAL_MAX];
    size_t length = cetta_generated_name_numeral(name, text, sizeof(text));
    return length == strlen(expected) && strcmp(text, expected) == 0;
}

/* The ranks and the counter a counted numeral spells, behind a block key
   when it has one: false when the text is not such a numeral. */
static bool read_counted(const char *text, bool *keyed, uint64_t *ranks,
                         size_t *count, uint64_t *counter) {
    size_t n = 0u;
    *keyed = false;
    const char *p = text;
    size_t key_length = strspn(p, "0123456789abcdef");
    if (key_length == 32u && p[32] == '.') {
        *keyed = true;
        p += 33;
    }
    uint64_t parts[70];
    size_t parts_len = 0u;
    for (;;) {
        if (*p < '0' || *p > '9' || parts_len == 70u)
            return false;
        if (p[0] == '0' && p[1] >= '0' && p[1] <= '9')
            return false;
        uint64_t v = 0u;
        while (*p >= '0' && *p <= '9') {
            uint64_t d = (uint64_t)(*p - '0');
            if (v > (UINT64_MAX - d) / 10u)
                return false;
            v = v * 10u + d;
            p++;
        }
        parts[parts_len++] = v;
        if (*p == '\0')
            break;
        if (*p != '.')
            return false;
        p++;
    }
    for (size_t i = 0u; i + 1u < parts_len; i++)
        ranks[n++] = parts[i];
    *count = n;
    *counter = parts[parts_len - 1u];
    return *counter != 0u;
}

static void test_formatter(void) {
    static CettaNameBlock block = {.references = 1u,
                                   .key_hi = UINT64_C(0x0123456789abcdef),
                                   .key_lo = UINT64_C(0xfedcba9876543210)};
    char text[CETTA_GENERATED_NAME_SPELLING_MAX];
    CHECK(numeral_is(counted(NULL, 1u, 1u), "1") &&
              numeral_is(counted(NULL, 1u, 3u), "3") &&
              numeral_is(counted(NULL, 1u, UINT64_MAX), "18446744073709551615"),
          "the root's names are its counter");
    static const uint64_t c0[] = {0u}, c1[] = {1u}, c2[] = {2u}, c3[] = {3u},
                          deep[] = {0u, 9u, 10u, 0u};
    CHECK(path_of(c0, 1) == 0x3u && path_of(c1, 1) == 0xau &&
              path_of(c2, 1) == 0xbu && path_of(c3, 1) == 0x24u,
          "the gamma codes of ranks 0 to 3 behind the start bit");
    CHECK(numeral_is(counted(NULL, path_of(c0, 1), 1u), "0.1") &&
              numeral_is(counted(NULL, path_of(c1, 1), 2u), "1.2") &&
              numeral_is(counted(NULL, path_of(c3, 1), 7u), "3.7") &&
              numeral_is(counted(NULL, path_of(deep, 4), 5u), "0.9.10.0.5"),
          "a child's names are its ranks and the counter");
    CHECK(numeral_is(counted(&block, 1u, 4u),
                     "0123456789abcdeffedcba9876543210.4") &&
              numeral_is(counted(&block, path_of(c2, 1), 1u),
                         "0123456789abcdeffedcba9876543210.2.1"),
          "beyond the root block a name is spelled behind its block's key");
    /* The widest: a key, 63 ranks of 0 and the largest counter. */
    size_t widest = cetta_generated_name_spell(
        CETTA_NAME_KIND_SPACE, counted(&block, UINT64_MAX, UINT64_MAX), text,
        sizeof(text));
    CHECK(widest == CETTA_GENERATED_NAME_SPELLING_MAX - 1u &&
              strncmp(text, "&space(0123456789abcdeffedcba9876543210.0.0.", 44) == 0 &&
              strcmp(text + widest - 22u, ".18446744073709551615)") == 0,
          "the widest spelling fills the bound exactly (%zu): %s", widest, text);
    CettaGeneratedName hashed = {UINT64_C(0x0123456789abcdef),
                                 UINT64_C(0xfedcba9876543210),
                                 (uintptr_t)CETTA_NAME_FORM_HASHED};
    CHECK(numeral_is(hashed, "0123456789abcdeffedcba9876543210"),
          "a hashed name is 32 hexadecimal digits");
    CHECK(cetta_generated_name_numeral((CettaGeneratedName){0}, text,
                                       sizeof(text)) == 0u,
          "no name has no numeral");
    CHECK(cetta_generated_name_numeral(hashed, text,
                                       CETTA_GENERATED_NAME_NUMERAL_MAX - 1u) == 0u &&
              cetta_generated_name_spell(CETTA_NAME_KIND_SPACE, hashed, text,
                                         CETTA_GENERATED_NAME_SPELLING_MAX - 1u) == 0u,
          "a short buffer refuses the numeral and the spelling");
    /* Negative controls: no path, a path that ends inside a code, no
       counter. */
    CHECK(cetta_generated_name_numeral(counted(NULL, 0u, 1u), text, sizeof(text)) == 0u &&
              cetta_generated_name_numeral(counted(NULL, 0x2u, 1u), text, sizeof(text)) == 0u &&
              cetta_generated_name_numeral(counted(NULL, 0x5u, 1u), text, sizeof(text)) == 0u &&
              cetta_generated_name_numeral(counted(NULL, 1u, 0u), text, sizeof(text)) == 0u,
          "a counted name without a whole path or a counter is spelled no way");
    CettaGeneratedName three = counted(NULL, 1u, 3u);
    CHECK(cetta_generated_name_spell(CETTA_NAME_KIND_SPACE, three, text,
                                     sizeof(text)) == 9u &&
              strcmp(text, "&space(3)") == 0,
          "space spelling: %s", text);
    CHECK(cetta_generated_name_spell(CETTA_NAME_KIND_CALLABLE, three, text,
                                     sizeof(text)) == 8u &&
              strcmp(text, "lambda_3") == 0,
          "callable spelling: %s", text);
    int64_t number = 0;
    CHECK(cetta_generated_name_small(three, &number) && number == 3,
          "a root name is a small number");
    CHECK(!cetta_generated_name_small(counted(NULL, path_of(c0, 1), 1u), &number) &&
              !cetta_generated_name_small(counted(&block, 1u, 1u), &number) &&
              !cetta_generated_name_small(counted(NULL, 1u, UINT64_MAX), &number) &&
              !cetta_generated_name_small(hashed, &number),
          "a child's, a block's, a wide or a hashed name is no small number");
}

static void test_order(void) {
    static CettaNameBlock low = {.references = 1u, .key_hi = 1u, .key_lo = 9u};
    static CettaNameBlock high = {.references = 1u, .key_hi = 2u, .key_lo = 0u};
    static const uint64_t r0[] = {0u}, r00[] = {0u, 0u}, r01[] = {0u, 1u},
                          r1[] = {1u}, r9[] = {9u}, r10[] = {10u},
                          r2_1[] = {2u, 1u};
    /* Ascending, each before the next. */
    CettaGeneratedName ascending[] = {
        counted(NULL, 1u, 1u),
        counted(NULL, 1u, 2u),
        counted(NULL, 1u, 10u),
        counted(NULL, path_of(r0, 1), 1u),
        counted(NULL, path_of(r0, 1), 2u),
        counted(NULL, path_of(r00, 2), 1u),
        counted(NULL, path_of(r01, 2), 1u),
        counted(NULL, path_of(r1, 1), 1u),
        counted(NULL, path_of(r2_1, 2), 1u),
        counted(NULL, path_of(r9, 1), 1u),
        counted(NULL, path_of(r10, 1), 1u),
        counted(&low, 1u, 1u),
        counted(&low, path_of(r0, 1), 1u),
        counted(&high, 1u, 1u),
        {0u, 5u, (uintptr_t)CETTA_NAME_FORM_HASHED},
        {1u, 0u, (uintptr_t)CETTA_NAME_FORM_HASHED},
    };
    size_t n = sizeof(ascending) / sizeof(ascending[0]);
    bool ordered = true;
    for (size_t i = 0u; i < n; i++)
        for (size_t j = 0u; j < n; j++) {
            int c = cetta_generated_name_compare(ascending[i], ascending[j]);
            int expected = i < j ? -1 : i > j ? 1 : 0;
            if (c != expected) {
                ordered = false;
                fprintf(stderr, "order of %zu and %zu: %d, expected %d\n", i,
                        j, c, expected);
            }
            if ((c == 0) != cetta_generated_name_eq(ascending[i], ascending[j]))
                ordered = false;
        }
    CHECK(ordered, "the order: the root's names as drawn, then children by "
                   "rank (9 before 10, a path before its extensions), other "
                   "blocks by key, hashed names by their bits");
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

typedef struct {
    uint64_t hi, lo;
} Fingerprint;

/* A 128-bit fingerprint of NAME's spelling as a name of KIND. */
static Fingerprint spelling_fingerprint(CettaNameKind kind,
                                        CettaGeneratedName name) {
    static const uint8_t key[16] = {7, 1, 4, 1, 5, 9, 2, 6,
                                    5, 3, 5, 8, 9, 7, 9, 3};
    char text[CETTA_GENERATED_NAME_SPELLING_MAX];
    size_t length = cetta_generated_name_spell(kind, name, text, sizeof(text));
    CHECK(length > 0u, "a drawn name is spelled");
    uint8_t digest[16];
    cetta_siphash24_128(key, text, length, digest);
    Fingerprint f = {0u, 0u};
    for (unsigned i = 0u; i < 8u; i++) {
        f.hi = (f.hi << 8) | digest[i];
        f.lo = (f.lo << 8) | digest[8u + i];
    }
    return f;
}

static int compare_fingerprints(const void *left, const void *right) {
    const Fingerprint *a = left, *b = right;
    if (a->hi != b->hi) return a->hi < b->hi ? -1 : 1;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return 0;
}

static size_t fingerprint_repeats(Fingerprint *prints, size_t count) {
    qsort(prints, count, sizeof(*prints), compare_fingerprints);
    size_t repeats = 0u;
    for (size_t i = 1u; i < count; i++)
        repeats += compare_fingerprints(&prints[i - 1u], &prints[i]) == 0;
    return repeats;
}

/* Every name a counted supply gives spells its own place: its ranks read
   back give its path. */
static void check_reads_back(CettaGeneratedName name) {
    if (cetta_generated_name_form(name) != CETTA_NAME_FORM_COUNTED)
        return;
    char text[CETTA_GENERATED_NAME_NUMERAL_MAX];
    uint64_t ranks[64], counter = 0u;
    size_t count = 0u;
    bool keyed = false;
    CHECK(cetta_generated_name_numeral(name, text, sizeof(text)) > 0u &&
              read_counted(text, &keyed, ranks, &count, &counter) &&
              keyed == (cetta_generated_name_block(name) != NULL) &&
              count < 64u && path_of(ranks, count) == name.hi &&
              counter == name.lo,
          "the numeral %s reads back as the name's place", text);
}

static void test_mode(void) {
    char error[200];
    CHECK(cetta_name_supply_configure(NULL, NULL, error, sizeof(error)),
          "the environment's mode reads: %s", error);
    g_unforgeable = cetta_name_supply_unforgeable();
    const char *seed = getenv("CETTA_NAME_SEED");
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    CettaGeneratedName first = cetta_name_supply_fresh(&root, CETTA_NAME_KIND_SPACE);
    CettaGeneratedName second = cetta_name_supply_fresh(&root, CETTA_NAME_KIND_SPACE);
    CettaGeneratedName callable = cetta_name_supply_fresh(&root, CETTA_NAME_KIND_CALLABLE);
    CettaNameSupply child;
    cetta_name_supply_init_child(&child, cetta_name_supply_split(&root));
    CettaGeneratedName child_first = cetta_name_supply_fresh(&child, CETTA_NAME_KIND_SPACE);
    /* The root's counter at its end: the names go on in a new block. */
    root.drawn[CETTA_NAME_KIND_SPACE] = UINT64_MAX;
    CettaGeneratedName next = cetta_name_supply_fresh(&root, CETTA_NAME_KIND_SPACE);
    char text[CETTA_GENERATED_NAME_SPELLING_MAX];
    if (!g_unforgeable) {
        CHECK(numeral_is(first, "1") && numeral_is(second, "2") &&
                  numeral_is(callable, "1") && numeral_is(child_first, "0.1"),
              "the counter root numbers each kind 1, 2, ... and its first "
              "child 0.1, ...");
        cetta_generated_name_spell(CETTA_NAME_KIND_SPACE, first, text, sizeof(text));
        CHECK(strcmp(text, "&space(1)") == 0, "the first root space is &space(1): %s", text);
        /* SipHash-2-4-128 under the zero key of 01, the root block's zero
           key and the root's path 1, as OpenSSL computes it. */
        CHECK(numeral_is(next, "529d30b948a8330fdb6a1996a5480869.1"),
              "the root's names beyond its counter: in the block keyed by "
              "the hash of its place");
    } else {
        CHECK(cetta_generated_name_form(first) == CETTA_NAME_FORM_HASHED &&
                  cetta_generated_name_form(child_first) == CETTA_NAME_FORM_HASHED &&
                  cetta_generated_name_form(next) == CETTA_NAME_FORM_HASHED,
              "unforgeable names are hashed");
        if (seed && strcmp(seed, "000102030405060708090a0b0c0d0e0f") == 0) {
            /* As OpenSSL's SIPHASH MAC computes them under the reference
               key: kind 0, path 1, counter 1 (little-endian); and in the
               block the root moves on to (key 2a40...49af, the hash of
               01, zero key, path 1), the same with that key behind. */
            CHECK(numeral_is(first, "f3f3de7fefc948f7bf69b66666179892"),
                  "the first hashed root space name");
            CHECK(numeral_is(next, "f5beaae6dd82c315d79cfa416a1f7047"),
                  "the first hashed name beyond the root's counter");
        }
    }
    CHECK(cetta_name_supply_live_blocks() == 1u && root.block != NULL,
          "the new block is alive");
    cetta_name_supply_dispose(&root);
    cetta_name_supply_dispose(&child);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and given back");
    printf("PASS: %s mode names the root 1, 2, ..., a child by its ranks, "
           "and goes on past a counter's end\n",
           g_unforgeable ? "unforgeable" : "counter");
}

static void test_split_paths(void) {
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    CHECK(root.path == 1u && root.block == NULL, "the root's path is the start bit");
    static const uint64_t first[] = {0x3u, 0xau, 0xbu, 0x24u};
    for (unsigned i = 0u; i < 4u; i++) {
        CettaNameSupplyChild child = cetta_name_supply_split(&root);
        CHECK(child.path == first[i] && child.block == NULL,
              "child %u of the root has path %llx, expected %llx", i,
              (unsigned long long)child.path, (unsigned long long)first[i]);
    }
    CHECK(root.children == 4u, "the root counted its children");
    /* Sixty-three first children fill the word; the next split moves the
       parent on to a new block, and its child starts there. */
    CettaNameSupply chain;
    cetta_name_supply_init_root(&chain);
    for (unsigned depth = 1u; depth <= 63u; depth++) {
        CettaNameSupplyChild child = cetta_name_supply_split(&chain);
        cetta_name_supply_init_child(&chain, child);
    }
    CHECK(chain.path == UINT64_MAX && chain.block == NULL,
          "a chain of 63 first children fills the word");
    CettaGeneratedName deepest = cetta_name_supply_fresh(&chain, CETTA_NAME_KIND_SPACE);
    CHECK(cetta_generated_name_is_name(deepest), "the deepest place of the root block gives names");
    check_reads_back(deepest);
    CettaNameSupplyChild beyond = cetta_name_supply_split(&chain);
    CHECK(chain.block != NULL && chain.path == 1u && chain.children == 1u &&
              beyond.block == chain.block && beyond.path == 0x3u &&
              cetta_name_supply_live_blocks() == 1u,
          "the 64th split moves the parent on to a new block, and the child "
          "starts in it");
    CettaGeneratedName parent_after = cetta_name_supply_fresh(&chain, CETTA_NAME_KIND_SPACE);
    CettaNameSupply below;
    cetta_name_supply_init_child(&below, beyond);
    CettaGeneratedName child_name = cetta_name_supply_fresh(&below, CETTA_NAME_KIND_SPACE);
    CHECK(!cetta_generated_name_eq(parent_after, deepest) &&
              !cetta_generated_name_eq(parent_after, child_name) &&
              !cetta_generated_name_eq(child_name, deepest),
          "the parent's names before and after, and the child's, are distinct");
    check_reads_back(parent_after);
    check_reads_back(child_name);
    cetta_name_supply_dispose(&chain);
    CHECK(cetta_name_supply_live_blocks() == 1u, "the child keeps the block");
    cetta_name_supply_dispose(&below);
    CHECK(cetta_name_supply_live_blocks() == 0u, "the last holder gives it back");
    /* Width: below 2^32 - 1 a rank's code takes at most 63 bits behind the
       root's start bit; the next moves the parent on. */
    CettaNameSupply wide_root;
    cetta_name_supply_init_root(&wide_root);
    wide_root.children = (UINT64_C(1) << 32) - 2u;
    CettaNameSupplyChild wide = cetta_name_supply_split(&wide_root);
    CHECK(wide.path == ((UINT64_C(1) << 63) | ((UINT64_C(1) << 32) - 1u)) &&
              wide.block == NULL,
          "rank 2^32 - 2 is coded in 63 bits: %llx", (unsigned long long)wide.path);
    CettaNameSupplyChild wider = cetta_name_supply_split(&wide_root);
    CHECK(wider.block != NULL && wider.path == 0x3u && wide_root.children == 1u,
          "rank 2^32 - 1 moves the parent on, where the child is the first");
    cetta_name_supply_child_release(wider);
    cetta_name_supply_dispose(&wide_root);
    CHECK(cetta_name_supply_live_blocks() == 0u, "every block is given back");
}

/* A chain of LEVELS supplies, each the first child of the one before, which
   ends as soon as it has split (as a task that pushes its successor does),
   drawing every 997 levels and at the bottom.  The fingerprints of the
   names into PRINTS; the most blocks alive at once into *PEAK. */
static size_t chain_names(unsigned levels, Fingerprint *prints, uint64_t *peak) {
    CettaNameSupply chain;
    cetta_name_supply_init_root(&chain);
    size_t count = 0u;
    *peak = 0u;
    for (unsigned level = 0u; level < levels; level++) {
        if (level % 997u == 0u) {
            CettaNameKind kind = level % 2u ? CETTA_NAME_KIND_CALLABLE
                                            : CETTA_NAME_KIND_SPACE;
            CettaGeneratedName name = cetta_name_supply_fresh(&chain, kind);
            check_reads_back(name);
            prints[count++] = spelling_fingerprint(kind, name);
        }
        CettaNameSupplyChild child = cetta_name_supply_split(&chain);
        cetta_name_supply_dispose(&chain);
        cetta_name_supply_init_child(&chain, child);
        uint64_t live = cetta_name_supply_live_blocks();
        if (live > *peak)
            *peak = live;
    }
    for (unsigned i = 0u; i < 3u; i++) {
        CettaNameKind kind = i % 2u ? CETTA_NAME_KIND_CALLABLE : CETTA_NAME_KIND_SPACE;
        CettaGeneratedName name = cetta_name_supply_fresh(&chain, kind);
        check_reads_back(name);
        prints[count++] = spelling_fingerprint(kind, name);
    }
    cetta_name_supply_dispose(&chain);
    return count;
}

static void test_depth(void) {
    enum { LEVELS = 1000000u };
    size_t capacity = LEVELS / 997u + 8u;
    Fingerprint *first = malloc(capacity * sizeof(*first));
    Fingerprint *again = malloc(capacity * sizeof(*again));
    uint64_t peak = 0u, peak_again = 0u;
    size_t count = chain_names(LEVELS, first, &peak);
    CHECK(cetta_name_supply_live_blocks() == 0u, "a chain gives back every block");
    size_t count_again = chain_names(LEVELS, again, &peak_again);
    CHECK(count == count_again && memcmp(first, again, count * sizeof(*first)) == 0,
          "a chain of a million levels gives the same names on a second run");
    CHECK(peak <= 2u, "at most two blocks of a chain are alive at once (%llu)",
          (unsigned long long)peak);
    size_t repeats = fingerprint_repeats(first, count);
    CHECK(repeats == 0u, "%zu of %zu names of a chain repeat", repeats, count);
    printf("PASS: a chain of %u levels gives %zu distinct names, the same on "
           "a second run, with at most %llu blocks alive at once and all "
           "given back\n",
           (unsigned)LEVELS, count, (unsigned long long)peak);
    free(first);
    free(again);
}

static void test_width(void) {
    enum { SIBLINGS = 1000000u };
    Fingerprint *prints = malloc((SIBLINGS + 2u) * sizeof(*prints));
    CettaNameSupply parent;
    cetta_name_supply_init_root(&parent);
    size_t count = 0u;
    for (unsigned i = 0u; i < SIBLINGS; i++) {
        CettaNameSupply child;
        cetta_name_supply_init_child(&child, cetta_name_supply_split(&parent));
        CettaGeneratedName name = cetta_name_supply_fresh(&child, CETTA_NAME_KIND_SPACE);
        if (i % 4099u == 0u)
            check_reads_back(name);
        prints[count++] = spelling_fingerprint(CETTA_NAME_KIND_SPACE, name);
        cetta_name_supply_dispose(&child);
    }
    prints[count++] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&parent, CETTA_NAME_KIND_SPACE));
    size_t repeats = fingerprint_repeats(prints, count);
    CHECK(repeats == 0u, "%zu of %zu names of siblings repeat", repeats, count);
    CHECK(cetta_name_supply_live_blocks() == 0u && parent.block == NULL,
          "a million siblings fit the root block");
    cetta_name_supply_dispose(&parent);
    printf("PASS: a million siblings and their parent give %zu distinct names\n",
           count);
    free(prints);
}

static void test_count(void) {
    CettaNameSupply supply;
    cetta_name_supply_init_root(&supply);
    supply.drawn[CETTA_NAME_KIND_SPACE] = UINT64_MAX - 2u;
    Fingerprint prints[8];
    CettaGeneratedName names[6];
    for (unsigned i = 0u; i < 6u; i++) {
        names[i] = cetta_generated_name_retain(
            cetta_name_supply_fresh(&supply, CETTA_NAME_KIND_SPACE));
        prints[i] = spelling_fingerprint(CETTA_NAME_KIND_SPACE, names[i]);
        check_reads_back(names[i]);
    }
    bool distinct = true;
    for (unsigned i = 0u; i < 6u; i++)
        for (unsigned j = i + 1u; j < 6u; j++)
            distinct = distinct && !cetta_generated_name_eq(names[i], names[j]);
    CHECK(distinct && fingerprint_repeats(prints, 6u) == 0u,
          "a counter at its end goes on in a new block, every name distinct");
    CHECK(supply.block != NULL && supply.drawn[CETTA_NAME_KIND_SPACE] == 4u &&
              cetta_name_supply_live_blocks() == 1u,
          "the supply moved on once and counts from 1 there");
    cetta_name_supply_dispose(&supply);
    /* A counted name kept by a holder keeps its block; a hashed one is its
       128 bits and keeps nothing. */
    CHECK(cetta_name_supply_live_blocks() == (g_unforgeable ? 0u : 1u),
          "names kept by a holder keep their block when they refer to it");
    for (unsigned i = 0u; i < 6u; i++)
        cetta_generated_name_release(names[i]);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and give it back when released");
    printf("PASS: drawing goes on past a counter's end\n");
}

static void test_unplaced(void) {
    CettaNameSupply between;
    cetta_name_supply_init_unplaced(&between);
    cetta_name_supply_dispose(&between);
    CHECK(cetta_name_supply_live_blocks() == 0u, "an unused supply outside the tree holds nothing");
    cetta_name_supply_init_unplaced(&between);
    CettaGeneratedName a = cetta_name_supply_fresh(&between, CETTA_NAME_KIND_SPACE);
    CettaNameSupplyChild child = cetta_name_supply_split(&between);
    CettaGeneratedName b = cetta_name_supply_fresh(&between, CETTA_NAME_KIND_SPACE);
    CettaNameSupply other;
    cetta_name_supply_init_unplaced(&other);
    CettaGeneratedName c = cetta_name_supply_fresh(&other, CETTA_NAME_KIND_SPACE);
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    CettaGeneratedName d = cetta_name_supply_fresh(&root, CETTA_NAME_KIND_SPACE);
    Fingerprint prints[4] = {
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, a),
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, b),
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, c),
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, d),
    };
    CHECK(cetta_generated_name_is_name(a) && cetta_generated_name_is_name(b) &&
              cetta_generated_name_is_name(c) && fingerprint_repeats(prints, 4u) == 0u &&
              child.block == between.block,
          "a supply outside the tree gives fresh names in blocks of its own");
    cetta_name_supply_child_release(child);
    cetta_name_supply_dispose(&between);
    cetta_name_supply_dispose(&other);
    cetta_name_supply_dispose(&root);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and gives them back");
}

/* ── A random tree of supplies ───────────────────────────────────────── */

typedef struct {
    CettaGeneratedName name;
    unsigned kind;
} DrawnName;

static uint64_t g_random;
static uint64_t next_random(void) {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 7;
    g_random ^= g_random << 17;
    return g_random;
}

static int compare_names(const void *left, const void *right) {
    const DrawnName *a = left, *b = right;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    if (a->name.tag != b->name.tag) return a->name.tag < b->name.tag ? -1 : 1;
    if (a->name.hi != b->name.hi) return a->name.hi < b->name.hi ? -1 : 1;
    if (a->name.lo != b->name.lo) return a->name.lo < b->name.lo ? -1 : 1;
    return 0;
}

/* NAMES names from a random tree of supplies: mostly deep (a split goes on
   in the child nine times in ten), some wide, every supply ending at some
   point.  The names are kept, holding their blocks, in DRAWN; the result is
   a fingerprint of their spellings in order. */
static Fingerprint random_tree(size_t names, DrawnName *drawn,
                               Fingerprint *prints) {
    enum { ACTIVE = 4096u };
    CettaNameSupply *active = malloc(ACTIVE * sizeof(*active));
    size_t active_len = 1u;
    cetta_name_supply_init_root(&active[0]);
    g_random = UINT64_C(0x9e3779b97f4a7c15);
    size_t count = 0u;
    uint64_t running = UINT64_C(0xcbf29ce484222325);
    size_t current = 0u;
    while (count < names) {
        uint64_t r = next_random();
        unsigned action = (unsigned)(r % 100u);
        if (action < 45u) {
            CettaNameKind kind = (r >> 8) & 1u ? CETTA_NAME_KIND_CALLABLE
                                                : CETTA_NAME_KIND_SPACE;
            CettaGeneratedName name = cetta_generated_name_retain(
                cetta_name_supply_fresh(&active[current], kind));
            drawn[count].name = name;
            drawn[count].kind = (unsigned)kind;
            prints[count] = spelling_fingerprint(kind, name);
            running = (running ^ prints[count].hi ^ prints[count].lo) *
                      UINT64_C(0x100000001b3);
            count++;
        } else if (action < 85u && active_len < ACTIVE) {
            cetta_name_supply_init_child(&active[active_len],
                                         cetta_name_supply_split(&active[current]));
            if (((r >> 16) % 10u) != 0u)
                current = active_len;
            active_len++;
        } else if (active_len > 1u) {
            /* A supply ends: the last one takes its place. */
            cetta_name_supply_dispose(&active[current]);
            active[current] = active[--active_len];
            current = (size_t)((r >> 24) % active_len);
        } else {
            current = 0u;
        }
        if (((r >> 40) % 64u) == 0u)
            current = (size_t)((r >> 32) % active_len);
    }
    for (size_t i = 0u; i < active_len; i++)
        cetta_name_supply_dispose(&active[i]);
    free(active);
    return (Fingerprint){running, count};
}

static void test_random_tree(void) {
    enum { NAMES = 2000000u };
    DrawnName *drawn = malloc(NAMES * sizeof(*drawn));
    Fingerprint *prints = malloc(NAMES * sizeof(*prints));
    Fingerprint first = random_tree(NAMES, drawn, prints);
    uint64_t blocks = cetta_name_supply_live_blocks();
    size_t checked = 0u;
    for (size_t i = 0u; i < NAMES; i += 1013u, checked++)
        check_reads_back(drawn[i].name);
    qsort(drawn, NAMES, sizeof(*drawn), compare_names);
    size_t repeats = 0u;
    for (size_t i = 1u; i < NAMES; i++)
        repeats += compare_names(&drawn[i - 1u], &drawn[i]) == 0;
    CHECK(repeats == 0u, "%zu of %u names of a random tree repeat", repeats,
          (unsigned)NAMES);
    size_t spelled_repeats = fingerprint_repeats(prints, NAMES);
    CHECK(spelled_repeats == 0u, "%zu of %u spellings of a random tree repeat",
          spelled_repeats, (unsigned)NAMES);
    for (size_t i = 0u; i < NAMES; i++)
        cetta_generated_name_release(drawn[i].name);
    CHECK(cetta_name_supply_live_blocks() == 0u,
          "the names of a random tree give back every block");
    Fingerprint again = random_tree(NAMES, drawn, prints);
    CHECK(again.hi == first.hi && again.lo == first.lo,
          "a random tree gives the same names on a second run");
    for (size_t i = 0u; i < NAMES; i++)
        cetta_generated_name_release(drawn[i].name);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and gives its blocks back again");
    printf("PASS: %u names from a random tree of supplies (%llu blocks held "
           "by them) are distinct as names and as spellings, the same on a "
           "second run, and give every block back\n",
           (unsigned)NAMES, (unsigned long long)blocks);
    free(drawn);
    free(prints);
}

int main(void) {
    test_siphash_vectors();
    test_formatter();
    test_order();
    test_mode();
    test_split_paths();
    test_depth();
    test_width();
    test_count();
    test_unplaced();
    test_random_tree();
    if (g_failures) {
        fprintf(stderr, "FAIL: %d of %ld name supply checks\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: %ld name supply checks (SipHash-2-4-128 reference vectors, "
           "numerals, order, split paths, depth, width, count)\n",
           g_checks);
    return 0;
}
