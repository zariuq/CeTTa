/*
 * The supply of generated names (src/name_supply.h): SipHash-2-4 with
 * 128-bit output against the reference vectors and OpenSSL, the numeral and
 * its spelling, the order of names, the share algebra of interval tree
 * clocks (Almeida, Baquero and Fonte 2008) against a separate
 * implementation as trees (split, join and normal forms, the balanced
 * pieces of a spawn, counted pushes) over both forms of the word, and that
 * no depth, width or count fails: spawns of up to 4097 components that are
 * disjoint, cover their share and lie within ceil(log2 k) halvings of it, a
 * million pushes from one supply that stay in the root block within 40
 * halvings, a spent pool, chains of a million levels, a counter past its
 * end, a supply outside the split tree, a million balanced forks and joins
 * that never leave the root block, and millions of names from a random tree
 * of supplies that push, fork, spawn, join and end, all distinct as names
 * and as spellings, the same on a second run, with every block given back.
 * Run once per mode: CETTA_NAME_SUPPLY=counter, and
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

/* ── Shares, separately: interval-tree-clock shares, as trees ────────── */

typedef struct Share Share;
struct Share {
    enum { SHARE_NONE, SHARE_ALL, SHARE_NODE } kind;
    const Share *left, *right;
};

/* Trees from a pool that the tests never give back before they end. */
enum { POOL = 1u << 22 };
static Share *g_pool;
static size_t g_pool_used;
static const Share k_none = {SHARE_NONE, NULL, NULL};
static const Share k_all = {SHARE_ALL, NULL, NULL};

static const Share *share_node(const Share *left, const Share *right) {
    if (g_pool_used == POOL) {
        fputs("FAIL: the share pool is exhausted\n", stderr);
        exit(1);
    }
    Share *node = &g_pool[g_pool_used++];
    *node = (Share){SHARE_NODE, left, right};
    return node;
}

/* A normal node: two empty halves are none, two full halves all. */
static const Share *share_node_normal(const Share *left, const Share *right) {
    if (left->kind == SHARE_NONE && right->kind == SHARE_NONE) return &k_none;
    if (left->kind == SHARE_ALL && right->kind == SHARE_ALL) return &k_all;
    return share_node(left, right);
}

static const Share *share_join(const Share *a, const Share *b) {
    if (a->kind == SHARE_NONE) return b;
    if (b->kind == SHARE_NONE) return a;
    if (a->kind == SHARE_ALL || b->kind == SHARE_ALL) return &k_all;
    return share_node_normal(share_join(a->left, b->left),
                             share_join(a->right, b->right));
}

static void share_split(const Share *s, const Share **first,
                        const Share **second) {
    switch (s->kind) {
    case SHARE_NONE:
        *first = *second = &k_none;
        return;
    case SHARE_ALL:
        *first = share_node(&k_all, &k_none);
        *second = share_node(&k_none, &k_all);
        return;
    case SHARE_NODE:
        if (s->left->kind == SHARE_NONE) {
            const Share *a, *b;
            share_split(s->right, &a, &b);
            *first = share_node(&k_none, a);
            *second = share_node(&k_none, b);
        } else if (s->right->kind == SHARE_NONE) {
            const Share *a, *b;
            share_split(s->left, &a, &b);
            *first = share_node(a, &k_none);
            *second = share_node(b, &k_none);
        } else {
            *first = share_node(s->left, &k_none);
            *second = share_node(&k_none, s->right);
        }
        return;
    }
}

/* The share down a path of halvings from S, true the right half. */
static const Share *share_at(const Share *s, const bool *path, unsigned length) {
    for (unsigned i = 0u; i < length; i++) {
        const Share *first, *second;
        share_split(s, &first, &second);
        s = path[i] ? second : first;
    }
    return s;
}

/* The balanced path of I among K into PATH: the first ceil(K/2) go left,
   the rest right, and so on down; its length. */
static unsigned balanced_path(uint64_t k, uint64_t i, bool *path) {
    unsigned length = 0u;
    while (k > 1u) {
        uint64_t left = (k + 1u) / 2u;
        if (i < left) {
            path[length++] = false;
            k = left;
        } else {
            path[length++] = true;
            i -= left;
            k /= 2u;
        }
    }
    return length;
}

/* The counted path of N into PATH: j = floor(log2(N+1)) right halvings, a
   left halving, and the balanced path of N + 1 - 2^j among 2^j (the j bits
   of N + 1 below its leading one); its length. */
static unsigned counted_path(uint64_t n, bool *path) {
    unsigned j = 0u;
    while ((n + 1u) >> (j + 1u))
        j++;
    unsigned length = 0u;
    for (unsigned r = 0u; r < j; r++)
        path[length++] = true;
    path[length++] = false;
    return length + balanced_path(UINT64_C(1) << j,
                                  n + 1u - (UINT64_C(1) << j), path + length);
}

/* ceil(log2 K). */
static unsigned ceil_log2(uint64_t k) {
    unsigned c = 0u;
    while ((UINT64_C(1) << c) < k)
        c++;
    return c;
}

/* The interval word of the dyadic interval below the interval WORD along
   PATH. */
static uint64_t word_along(uint64_t word, const bool *path, unsigned length) {
    for (unsigned i = 0u; i < length; i++)
        word = (word << 1) | (uint64_t)path[i];
    return word;
}

static unsigned share_depth(const Share *s) {
    if (s->kind != SHARE_NODE) return 0u;
    unsigned left = share_depth(s->left), right = share_depth(s->right);
    return 1u + (left > right ? left : right);
}

/* Whether two normal shares overlap: some point is in both. */
static bool share_overlap(const Share *a, const Share *b) {
    if (a->kind == SHARE_NONE || b->kind == SHARE_NONE) return false;
    if (a->kind == SHARE_ALL || b->kind == SHARE_ALL) return true;
    return share_overlap(a->left, b->left) || share_overlap(a->right, b->right);
}

static bool share_eq(const Share *a, const Share *b) {
    if (a->kind != b->kind) return false;
    if (a->kind != SHARE_NODE) return true;
    return share_eq(a->left, b->left) && share_eq(a->right, b->right);
}

/* A share's prefix code, as a name of counter mode spells it: 0 none,
   1 all, 2 a node and its halves. */
static size_t share_write(const Share *s, char *out) {
    if (s->kind == SHARE_NONE) { out[0] = '0'; return 1u; }
    if (s->kind == SHARE_ALL) { out[0] = '1'; return 1u; }
    out[0] = '2';
    size_t length = 1u + share_write(s->left, out + 1);
    return length + share_write(s->right, out + length);
}

static const Share *share_read_at(const char **text) {
    char c = *(*text)++;
    if (c == '0') return &k_none;
    if (c == '1') return &k_all;
    if (c != '2') return NULL;
    const Share *left = share_read_at(text);
    const Share *right = left ? share_read_at(text) : NULL;
    return right ? share_node(left, right) : NULL;
}

/* The share a word holds, read from its spelling. */
static const Share *share_of_word(uint64_t word) {
    char text[CETTA_NAME_SHARE_SPELLING_MAX + 1u];
    size_t length = cetta_name_share_spell(word, text);
    if (length == 0u)
        return NULL;
    text[length] = '\0';
    const char *at = text;
    const Share *share = share_read_at(&at);
    return share && *at == '\0' ? share : NULL;
}

/* The word of a share by the header's account of the two forms: a single
   interval at most 62 halvings deep as its start bit and path, anything
   else as its prefix code behind the mark, when it is at most 62 bits; 0
   when it fits neither. */
static uint64_t word_of_share(const Share *s) {
    uint64_t path = 1u;
    unsigned depth = 0u;
    const Share *at = s;
    while (at->kind == SHARE_NODE) {
        if (at->left->kind == SHARE_NONE) {
            path = (path << 1) | 1u;
            at = at->right;
        } else if (at->right->kind == SHARE_NONE) {
            path <<= 1;
            at = at->left;
        } else {
            break;
        }
        if (++depth > 62u)
            return 0u;
    }
    if (at->kind == SHARE_ALL)
        return path;
    char tokens[512];
    size_t count = share_write(s, tokens);
    unsigned bits = 0u;
    uint64_t code = 1u;
    for (size_t i = 0u; i < count; i++) {
        if (tokens[i] == '2') { code = (code << 1) | 1u; bits += 1u; }
        else { code = (code << 2) | (uint64_t)(tokens[i] - '0'); bits += 2u; }
        if (bits > 62u)
            return 0u;
    }
    return CETTA_NAME_SHARE_CODED | code;
}

static bool supply_is_share(const CettaNameSupply *supply, const Share *share) {
    const Share *held = share_of_word(supply->share);
    return held && share_eq(held, share) &&
           word_of_share(share) == supply->share;
}

/* ── Names by hand ───────────────────────────────────────────────────── */

static CettaGeneratedName counted(const CettaNameBlock *block, uint64_t share,
                                  uint64_t clock) {
    return (CettaGeneratedName){share, clock,
                                (uintptr_t)block |
                                    (uintptr_t)CETTA_NAME_FORM_COUNTED};
}

static bool numeral_is(CettaGeneratedName name, const char *expected) {
    char text[CETTA_GENERATED_NAME_NUMERAL_MAX];
    size_t length = cetta_generated_name_numeral(name, text, sizeof(text));
    return length == strlen(expected) && strcmp(text, expected) == 0;
}

/* The share's prefix code and the clock a counted numeral spells, behind a
   block key when it has one: false when the text is not such a numeral. */
static bool read_counted(const char *text, bool *keyed, const Share **share,
                         uint64_t *clock) {
    const char *p = text;
    *keyed = false;
    *share = &k_all;
    size_t key_length = strspn(p, "0123456789abcdef");
    if (key_length == 32u && p[32] == '.') {
        *keyed = true;
        p += 33;
    }
    const char *dot = strrchr(p, '.');
    if (dot) {
        const char *at = p;
        *share = share_read_at(&at);
        if (!*share || at != dot)
            return false;
        p = dot + 1;
    } else if (*keyed) {
        return false;
    }
    if (*p < '1' || *p > '9')
        return false;
    uint64_t v = 0u;
    while (*p >= '0' && *p <= '9') {
        uint64_t d = (uint64_t)(*p - '0');
        if (v > (UINT64_MAX - d) / 10u)
            return false;
        v = v * 10u + d;
        p++;
    }
    *clock = v;
    return *p == '\0';
}

static void test_formatter(void) {
    static CettaNameBlock block = {.references = 1u,
                                   .key_hi = UINT64_C(0x0123456789abcdef),
                                   .key_lo = UINT64_C(0xfedcba9876543210)};
    char text[CETTA_GENERATED_NAME_SPELLING_MAX];
    CHECK(numeral_is(counted(NULL, 1u, 1u), "1") &&
              numeral_is(counted(NULL, 1u, 3u), "3") &&
              numeral_is(counted(NULL, 1u, UINT64_MAX), "18446744073709551615"),
          "the root's names are its clock");
    /* Intervals: the halves, a quarter, and the code of a share that is
       none: node (node all none) (node none all). */
    uint64_t two_quarters = CETTA_NAME_SHARE_CODED | UINT64_C(0xe91);
    CHECK(numeral_is(counted(NULL, 3u, 1u), "201.1") &&
              numeral_is(counted(NULL, 2u, 2u), "210.2") &&
              numeral_is(counted(NULL, 5u, 3u), "22010.3") &&
              numeral_is(counted(NULL, 6u, 4u), "20210.4") &&
              numeral_is(counted(NULL, two_quarters, 7u), "2210201.7"),
          "a share's names are its prefix code and the clock");
    CHECK(numeral_is(counted(&block, 1u, 4u),
                     "0123456789abcdeffedcba9876543210.1.4") &&
              numeral_is(counted(&block, 3u, 1u),
                         "0123456789abcdeffedcba9876543210.201.1"),
          "beyond the root block a name is spelled behind its block's key");
    /* The widest: a key, an interval 62 halvings deep, the largest clock. */
    size_t widest = cetta_generated_name_spell(
        CETTA_NAME_KIND_SPACE, counted(&block, UINT64_C(1) << 62, UINT64_MAX),
        text, sizeof(text));
    CHECK(widest == CETTA_GENERATED_NAME_SPELLING_MAX - 1u &&
              strncmp(text, "&space(0123456789abcdeffedcba9876543210.2222", 44) == 0 &&
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
    /* Negative controls: no share, a code cut off inside a leaf, a code
       with a bit past its end, a mark with no code, no clock. */
    CHECK(cetta_generated_name_numeral(counted(NULL, 0u, 1u), text, sizeof(text)) == 0u &&
              cetta_generated_name_numeral(
                  counted(NULL, CETTA_NAME_SHARE_CODED | 0x6u, 1u), text,
                  sizeof(text)) == 0u &&
              cetta_generated_name_numeral(
                  counted(NULL, CETTA_NAME_SHARE_CODED | 0x9u, 1u), text,
                  sizeof(text)) == 0u &&
              cetta_generated_name_numeral(
                  counted(NULL, CETTA_NAME_SHARE_CODED, 1u), text,
                  sizeof(text)) == 0u &&
              cetta_generated_name_numeral(counted(NULL, 1u, 0u), text, sizeof(text)) == 0u,
          "a counted name without a whole share or a clock is spelled no way");
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
    CHECK(!cetta_generated_name_small(counted(NULL, 3u, 1u), &number) &&
              !cetta_generated_name_small(counted(&block, 1u, 1u), &number) &&
              !cetta_generated_name_small(counted(NULL, 1u, UINT64_MAX), &number) &&
              !cetta_generated_name_small(hashed, &number),
          "a share's, a block's, a wide or a hashed name is no small number");
}

static void test_order(void) {
    static CettaNameBlock low = {.references = 1u, .key_hi = 1u, .key_lo = 9u};
    static CettaNameBlock high = {.references = 1u, .key_hi = 2u, .key_lo = 0u};
    uint64_t coded = CETTA_NAME_SHARE_CODED | UINT64_C(0xe91);
    /* Ascending, each before the next. */
    CettaGeneratedName ascending[] = {
        counted(NULL, 1u, 1u),
        counted(NULL, 1u, 2u),
        counted(NULL, 1u, 10u),
        counted(NULL, 2u, 1u),
        counted(NULL, 2u, 2u),
        counted(NULL, 3u, 1u),
        counted(NULL, 4u, 1u),
        counted(NULL, 7u, 1u),
        counted(NULL, coded, 1u),
        counted(&low, 1u, 1u),
        counted(&low, 3u, 1u),
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
    CHECK(ordered, "the order: the root's names as drawn, then the root "
                   "block's shares by word and clock, other blocks by key, "
                   "hashed names by their bits");
}

/* ── The share algebra, against the trees ────────────────────────────── */

/* A live supply of the random algebra, as the trees hold it: its share,
   and once it has pushed, its pool and its count. */
typedef struct {
    const Share *share;
    const Share *pool;
    uint64_t pushes;
} TreeSupply;

/* Whether SUPPLY's pushes are the trees': its count, and the word of its
   last push, the pool along the counted path of the count before. */
static bool pushes_are(const CettaNameSupply *supply, const TreeSupply *tree) {
    if (supply->pushes != tree->pushes)
        return false;
    if (tree->pushes == 0u)
        return supply->pushed == 0u;
    bool path[160];
    unsigned length = counted_path(tree->pushes - 1u, path);
    return word_of_share(share_at(tree->pool, path, length)) == supply->pushed;
}

/* Every share word of a supply that has not moved on is the trees' share:
   forks, counted pushes, the balanced pieces of a spawn and joins, at
   random, compared after each step with the same operation on the trees,
   which hold the same shares, pools and counts; the live shares are
   pairwise disjoint.  A supply moves on to a block exactly when a share it
   needs would not fit the word. */
static void test_share_algebra(void) {
    enum { LIVE = 16u, STEPS = 200000u, ROUND = 40u };
    CettaNameSupply supply[LIVE];
    TreeSupply tree[LIVE];
    size_t live = 1u;
    cetta_name_supply_init_root(&supply[0]);
    tree[0] = (TreeSupply){&k_all, NULL, 0u};
    uint64_t random = UINT64_C(0x2545f4914f6cdd1d);
    size_t agreed = 0u, moved = 0u, joins = 0u, spawns = 0u, pushes = 0u,
           forks = 0u, coded = 0u, kept = 0u;
    bool agree = true;
    for (unsigned step = 0u; step < STEPS; step++) {
        if (step % ROUND == 0u) {
            /* Every round starts again from the whole interval, once the
               live shares are checked to be the trees' and apart. */
            for (size_t i = 0u; agree && i < live; i++) {
                agree = supply_is_share(&supply[i], tree[i].share) &&
                        pushes_are(&supply[i], &tree[i]);
                for (size_t j = i + 1u; agree && j < live; j++)
                    agree = supply[i].block != supply[j].block ||
                            !share_overlap(tree[i].share, tree[j].share);
            }
            for (size_t i = 0u; i < live; i++)
                cetta_name_supply_dispose(&supply[i]);
            live = 1u;
            cetta_name_supply_init_root(&supply[0]);
            tree[0] = (TreeSupply){&k_all, NULL, 0u};
            g_pool_used = 0u;
        }
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        size_t i = (size_t)(random % live);
        unsigned action = (unsigned)((random >> 20) % 12u);
        if (action < 2u && live < LIVE) {
            /* A fork in two: the parent keeps the left half and its pool. */
            CettaNameBlock *block = supply[i].block;
            CettaNameSupply child = cetta_name_supply_fork(&supply[i]);
            const Share *first, *second;
            share_split(tree[i].share, &first, &second);
            forks++;
            if (supply[i].block != block) {
                /* Moved on because a half would not fit: the halves of its
                   new block, and its pool given up. */
                moved++;
                agree = agree && child.block == supply[i].block &&
                        (word_of_share(first) == 0u ||
                         word_of_share(second) == 0u) &&
                        supply[i].share == 2u && child.share == 3u &&
                        supply[i].pushes == 0u;
                tree[i] = (TreeSupply){share_node(&k_all, &k_none), NULL, 0u};
                tree[live] = (TreeSupply){share_node(&k_none, &k_all), NULL, 0u};
            } else {
                agree = agree && word_of_share(first) != 0u &&
                        word_of_share(second) != 0u && child.pushes == 0u;
                tree[i].share = first;
                tree[live] = (TreeSupply){second, NULL, 0u};
            }
            supply[live++] = child;
        } else if (action < 5u && live < LIVE) {
            /* A push: push n is the pool along the counted path of n; at
               the first the pusher keeps the left half and pools the
               right. */
            CettaNameBlock *block = supply[i].block;
            CettaNameSupply child;
            cetta_name_supply_push_into(&supply[i], &child);
            pushes++;
            TreeSupply before = tree[i];
            const Share *pool = before.pool, *own = before.share;
            if (before.pushes == 0u)
                share_split(before.share, &own, &pool);
            bool path[160];
            unsigned length = counted_path(before.pushes, path);
            const Share *expected = share_at(pool, path, length);
            if (supply[i].block != block) {
                /* Moved on: the share's halves, or a pool that is no
                   interval with room below it, or a spent pool, would not
                   fit; it pushes from the right half of its new block. */
                moved++;
                uint64_t pool_word = word_of_share(pool);
                bool needed = word_of_share(expected) == 0u ||
                              word_of_share(own) == 0u ||
                              (before.pushes == 0u &&
                               ((pool_word & CETTA_NAME_SHARE_CODED) != 0u ||
                                pool_word == 0u));
                agree = agree && needed && child.block == supply[i].block &&
                        supply[i].share == 2u && child.share == 6u &&
                        supply[i].pushes == 1u && supply[i].pushed == 6u;
                tree[i] = (TreeSupply){share_node(&k_all, &k_none),
                                       share_node(&k_none, &k_all), 1u};
                tree[live] = (TreeSupply){share_of_word(6u), NULL, 0u};
            } else {
                agree = agree && supply_is_share(&child, expected) &&
                        supply_is_share(&supply[i], own) &&
                        child.pushes == 0u && child.pushed == 0u &&
                        child.drawn[0] == supply[i].drawn[0] &&
                        child.drawn[1] == supply[i].drawn[1];
                tree[i] = (TreeSupply){own, pool, before.pushes + 1u};
                tree[live] = (TreeSupply){expected, NULL, 0u};
            }
            supply[live++] = child;
        } else if (action < 7u && live + 4u <= LIVE) {
            /* A spawn of two to five: the balanced pieces of the share; the
               spawner ends. */
            uint32_t parts = 2u + (uint32_t)((random >> 28) % 4u);
            CettaNameSupplyPieces pieces;
            cetta_name_supply_pieces_begin(&pieces, &supply[i], parts);
            CettaNameSupply got[5];
            for (uint32_t p = 0u; p < parts; p++)
                got[p] = cetta_name_supply_pieces_next(&pieces);
            agree = agree && pieces.waiting == 0u;
            spawns++;
            const Share *from = tree[i].share;
            bool any_moved = false;
            for (uint32_t p = 0u; p < parts; p++) {
                bool path[64];
                unsigned length = balanced_path(parts, p, path);
                const Share *expected = share_at(from, path, length);
                TreeSupply *slot = p == 0u ? &tree[i] : &tree[live + p - 1u];
                agree = agree && got[p].pushes == 0u &&
                        got[p].drawn[0] == supply[i].drawn[0];
                if (got[p].block == supply[i].block) {
                    agree = agree && supply_is_share(&got[p], expected);
                    *slot = (TreeSupply){expected, NULL, 0u};
                } else {
                    /* Moved on below a share that did not halve in the word:
                       compare with what it moved from, so only re-read. */
                    any_moved = true;
                    *slot = (TreeSupply){share_of_word(got[p].share), NULL, 0u};
                }
            }
            moved += any_moved;
            cetta_name_supply_dispose(&supply[i]);
            supply[i] = got[0];
            for (uint32_t p = 1u; p < parts; p++)
                supply[live++] = got[p];
        } else if (action < 10u && live > 1u) {
            /* A COMM: two supplies end and join into one, often the one
               made last, as a COMM between the parts of a spawn. */
            size_t j = (random >> 32) & 1u ? live - 1u
                                           : (size_t)((random >> 33) % live);
            if (j == i) j = (i + 1u) % live;
            CettaNameSupply joined = cetta_name_supply_join(&supply[i], &supply[j]);
            joins++;
            agree = agree && joined.pushes == 0u && joined.pushed == 0u;
            if (supply[i].block == supply[j].block && joined.block == supply[i].block) {
                const Share *both = share_join(tree[i].share, tree[j].share);
                agree = agree && supply_is_share(&joined, both);
                coded += (joined.share & CETTA_NAME_SHARE_CODED) != 0u;
                kept++;
                tree[i] = (TreeSupply){both, NULL, 0u};
            } else {
                agree = agree && joined.share == CETTA_NAME_SHARE_WHOLE &&
                        joined.block != NULL;
                if (supply[i].block == supply[j].block)
                    agree = agree && word_of_share(share_join(tree[i].share,
                                                              tree[j].share)) == 0u;
                moved++;
                tree[i] = (TreeSupply){&k_all, NULL, 0u};
            }
            agree = agree && joined.drawn[0] >= supply[i].drawn[0] &&
                    joined.drawn[0] >= supply[j].drawn[0];
            cetta_name_supply_dispose(&supply[i]);
            cetta_name_supply_dispose(&supply[j]);
            supply[i] = joined;
            supply[j] = supply[--live];
            tree[j] = tree[live];
        } else {
            /* A draw, or the end of a supply. */
            if (live > 1u && (random >> 40) % 4u == 0u) {
                cetta_name_supply_dispose(&supply[i]);
                supply[i] = supply[--live];
                tree[i] = tree[live];
            } else {
                (void)cetta_name_supply_fresh(&supply[i], CETTA_NAME_KIND_SPACE);
            }
        }
        agreed++;
        if (!agree) {
            fprintf(stderr, "FAIL: the shares part ways at step %u\n", step);
            break;
        }
    }
    /* Every live share is the tree it should be, and no two overlap. */
    for (size_t i = 0u; agree && i < live; i++) {
        agree = supply_is_share(&supply[i], tree[i].share) &&
                pushes_are(&supply[i], &tree[i]);
        for (size_t j = i + 1u; agree && j < live; j++)
            agree = supply[i].block != supply[j].block ||
                    !share_overlap(tree[i].share, tree[j].share);
    }
    CHECK(agree, "the words' shares are the trees' shares after %zu steps "
                 "(%zu forks, %zu pushes, %zu spawns, %zu joins, %zu moves on)",
          agreed, forks, pushes, spawns, joins, moved);
    for (size_t i = 0u; i < live; i++)
        cetta_name_supply_dispose(&supply[i]);
    CHECK(cetta_name_supply_live_blocks() == 0u,
          "the algebra's blocks are all given back");
    printf("PASS: the words' shares are the trees' shares over %zu random "
           "steps: %zu forks, %zu counted pushes, %zu balanced spawns, %zu "
           "joins (%zu in the word, %zu of them coded), %zu moves on\n",
           agreed, forks, pushes, spawns, joins, kept, coded, moved);

    /* By hand: the halves join to the whole, two quarters that are no
       interval, and their split. */
    CettaNameSupply root, left, right;
    cetta_name_supply_init_root(&root);
    cetta_name_supply_split(&root, &left, &right);
    CettaNameSupply whole = cetta_name_supply_join(&left, &right);
    CHECK(left.share == 2u && right.share == 3u &&
              whole.share == CETTA_NAME_SHARE_WHOLE && whole.block == NULL,
          "the halves of the whole join to the whole");
    CettaNameSupply ll, lr, rl, rr;
    cetta_name_supply_split(&left, &ll, &lr);
    cetta_name_supply_split(&right, &rl, &rr);
    CettaNameSupply outer = cetta_name_supply_join(&ll, &rr);
    CHECK(ll.share == 4u && rr.share == 7u &&
              outer.share == (CETTA_NAME_SHARE_CODED | UINT64_C(0xe91)),
          "two quarters that are no interval keep their prefix code: %llx",
          (unsigned long long)outer.share);
    CettaNameSupply back_left, back_right;
    cetta_name_supply_split(&outer, &back_left, &back_right);
    CHECK(back_left.share == 4u && back_right.share == 7u,
          "and split back into them");
    CettaNameSupply inner = cetta_name_supply_join(&lr, &rl);
    CettaNameSupply all_four = cetta_name_supply_join(&back_left, &back_right);
    CettaNameSupply again = cetta_name_supply_join(&all_four, &inner);
    CHECK(again.share == CETTA_NAME_SHARE_WHOLE && again.block == NULL,
          "a share joined with its complement is the whole");
    cetta_name_supply_dispose(&again);
    cetta_name_supply_dispose(&all_four);
    cetta_name_supply_dispose(&inner);
    cetta_name_supply_dispose(&whole);
    CHECK(cetta_name_supply_live_blocks() == 0u,
          "shares that fit the word make no block");

    /* Every two intervals at most 6 halvings deep, and pairs about the
       62-bit edge of the code: their join is the trees' join in the word,
       or, exactly when that would not fit, the whole of a block of its
       own. */
    bool unions = true;
    size_t pairs = 0u, kept_pairs = 0u, moved_pairs = 0u;
    uint64_t edge[] = {(UINT64_C(1) << 20) | 0x5a5a5u, (UINT64_C(1) << 21) | 0x12345u,
                       (UINT64_C(1) << 21) | 0x1a345u, (UINT64_C(1) << 10) | 0x3ffu,
                       (UINT64_C(1) << 10), (UINT64_C(1) << 31) | 0x7fffffffu,
                       (UINT64_C(1) << 11) | 0x400u, 3u, 2u};
    for (unsigned round = 0u; round < 2u; round++) {
        size_t count = round == 0u ? 127u : sizeof(edge) / sizeof(edge[0]);
        for (size_t x = 0u; x < count; x++)
            for (size_t y = 0u; y < count; y++) {
                uint64_t wa = round == 0u ? x + 1u : edge[x];
                uint64_t wb = round == 0u ? y + 1u : edge[y];
                g_pool_used = 0u;
                CettaNameSupply sa, sb;
                cetta_name_supply_init_root(&sa);
                cetta_name_supply_init_root(&sb);
                sa.share = wa;
                sb.share = wb;
                CettaNameSupply joined = cetta_name_supply_join(&sa, &sb);
                const Share *both = share_join(share_of_word(wa), share_of_word(wb));
                uint64_t expected = word_of_share(both);
                if (expected != 0u) {
                    unions = unions && joined.block == NULL &&
                             supply_is_share(&joined, both);
                    kept_pairs++;
                    /* And its halves are the trees' split: for two intervals
                       that part, the two intervals. */
                    const Share *first, *second;
                    share_split(both, &first, &second);
                    CettaNameSupply copy = cetta_name_supply_copy(&joined), left, right;
                    cetta_name_supply_split(&copy, &left, &right);
                    if (word_of_share(first) != 0u && word_of_share(second) != 0u)
                        unions = unions && left.block == NULL && right.block == NULL &&
                                 supply_is_share(&left, first) &&
                                 supply_is_share(&right, second);
                    cetta_name_supply_dispose(&left);
                    cetta_name_supply_dispose(&right);
                } else {
                    unions = unions && joined.block != NULL &&
                             joined.share == CETTA_NAME_SHARE_WHOLE;
                    moved_pairs++;
                }
                pairs++;
                cetta_name_supply_dispose(&joined);
            }
    }
    CHECK(unions && cetta_name_supply_live_blocks() == 0u,
          "the join of two intervals is the trees' join in the word, or moves on "
          "exactly when that would not fit (%zu pairs: %zu in the word, %zu "
          "moved)", pairs, kept_pairs, moved_pairs);
    printf("PASS: the joins of %zu pairs of intervals are the trees' "
           "joins, and their halves the trees' splits: %zu in the word, %zu "
           "too wide for it in blocks of their own\n",
           pairs, kept_pairs, moved_pairs);
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

/* Every name a counted supply gives spells its own place: its share and
   clock read back. */
static void check_reads_back(CettaGeneratedName name) {
    if (cetta_generated_name_form(name) != CETTA_NAME_FORM_COUNTED)
        return;
    char text[CETTA_GENERATED_NAME_NUMERAL_MAX];
    const Share *share = NULL;
    uint64_t clock = 0u;
    bool keyed = false;
    CHECK(cetta_generated_name_numeral(name, text, sizeof(text)) > 0u &&
              read_counted(text, &keyed, &share, &clock) &&
              keyed == (cetta_generated_name_block(name) != NULL) &&
              word_of_share(share) == name.hi && clock == name.lo,
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
    cetta_name_supply_push_into(&root, &child);
    CettaGeneratedName child_first = cetta_name_supply_fresh(&child, CETTA_NAME_KIND_SPACE);
    CettaGeneratedName left_next = cetta_name_supply_fresh(&root, CETTA_NAME_KIND_SPACE);
    /* The root's clock at its end: the names go on in a fresh block. */
    CettaNameSupply whole;
    cetta_name_supply_init_root(&whole);
    whole.drawn[CETTA_NAME_KIND_SPACE] = UINT64_MAX;
    CettaGeneratedName next = cetta_name_supply_fresh(&whole, CETTA_NAME_KIND_SPACE);
    char text[CETTA_GENERATED_NAME_SPELLING_MAX];
    if (!g_unforgeable) {
        CHECK(numeral_is(first, "1") && numeral_is(second, "2") &&
                  numeral_is(callable, "1") && numeral_is(child_first, "20210.3") &&
                  numeral_is(left_next, "210.3"),
              "the counter root numbers each kind 1, 2, ...; its first push "
              "hands the task the left half of the right half at the root's "
              "clocks, and the root draws from the left half");
        cetta_generated_name_spell(CETTA_NAME_KIND_SPACE, first, text, sizeof(text));
        CHECK(strcmp(text, "&space(1)") == 0, "the first root space is &space(1): %s", text);
        /* The first fresh block of the process: SipHash-2-4-128 under the
           zero key of 03 and the number 0, as OpenSSL computes it. */
        CHECK(numeral_is(next, "612cc42f37e7903c0c928b0fd5987992.1.1"),
              "the root's names beyond its clock: in a fresh block");
    } else {
        CHECK(cetta_generated_name_form(first) == CETTA_NAME_FORM_HASHED &&
                  cetta_generated_name_form(child_first) == CETTA_NAME_FORM_HASHED &&
                  cetta_generated_name_form(next) == CETTA_NAME_FORM_HASHED,
              "unforgeable names are hashed");
        if (seed && strcmp(seed, "000102030405060708090a0b0c0d0e0f") == 0) {
            /* As OpenSSL's SIPHASH MAC computes them under the reference
               key: kind 0, the whole share 1, clock 1 (little-endian); and
               in the fresh block (key 0d60...4a7d, the hash of 03 and 0),
               the same with that key behind. */
            CHECK(numeral_is(first, "f3f3de7fefc948f7bf69b66666179892"),
                  "the first hashed root space name");
            CHECK(numeral_is(next, "14e6dfbb00457cefd9fb4208ef2ec2f7"),
                  "the first hashed name beyond the root's clock");
        }
    }
    CHECK(!cetta_generated_name_eq(child_first, left_next) &&
              !cetta_generated_name_eq(child_first, first),
          "a pushed task's names and its pusher's are distinct");
    CHECK(cetta_name_supply_live_blocks() == 1u && whole.block != NULL,
          "the fresh block is alive");
    cetta_name_supply_dispose(&whole);
    cetta_name_supply_dispose(&root);
    cetta_name_supply_dispose(&child);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and given back");
    printf("PASS: %s mode names the root 1, 2, ..., a share by its prefix "
           "code and clock, and goes on past a clock's end\n",
           g_unforgeable ? "unforgeable" : "counter");
}

/* A supply moves on to a block of its own when a half would not fit: an
   interval 62 halvings deep, and the union of two deep intervals; a join
   across blocks takes a block of its own. */
static void test_moving_on(void) {
    CettaNameSupply chain;
    cetta_name_supply_init_root(&chain);
    for (unsigned depth = 1u; depth <= 62u; depth++) {
        CettaNameSupply child = cetta_name_supply_fork(&chain);
        cetta_name_supply_dispose(&child);
    }
    CHECK(chain.share == (UINT64_C(1) << 62) && chain.block == NULL,
          "62 pushes leave the leftmost interval 62 halvings deep");
    CettaGeneratedName deepest = cetta_name_supply_fresh(&chain, CETTA_NAME_KIND_SPACE);
    CHECK(cetta_generated_name_is_name(deepest), "the deepest share of the root block gives names");
    check_reads_back(deepest);
    CettaNameSupply beyond = cetta_name_supply_fork(&chain);
    CHECK(chain.block != NULL && chain.share == 2u &&
              beyond.block == chain.block && beyond.share == 3u &&
              cetta_name_supply_live_blocks() == 1u,
          "the 63rd push moves the parent on to a block of its own, which "
          "it halves with the task");
    {
        /* SipHash-2-4-128 under the mode's key (zero, or the seed) of 01,
           the root block's zero key, the clocks 1 and 0 and the share's
           word behind a 0, all little-endian but the key, as OpenSSL
           computes it. */
        char key[33];
        uint8_t digest[16];
        for (unsigned i = 0u; i < 8u; i++) {
            digest[i] = (uint8_t)(chain.block->key_hi >> (56u - 8u * i));
            digest[8u + i] = (uint8_t)(chain.block->key_lo >> (56u - 8u * i));
        }
        hex_bytes(digest, 16u, key);
        const char *seed = getenv("CETTA_NAME_SEED");
        const char *expected = !g_unforgeable
            ? "7031db4c9febedb92ab6bf3cd288ae56"
            : seed && strcmp(seed, "000102030405060708090a0b0c0d0e0f") == 0
                ? "6dd4baad13b7b58d08339359fb5be744" : NULL;
        CHECK(!expected || strcmp(key, expected) == 0,
              "the block's key is the hash of the share it stands for: %s", key);
    }
    CettaGeneratedName parent_after = cetta_name_supply_fresh(&chain, CETTA_NAME_KIND_SPACE);
    CettaGeneratedName child_name = cetta_name_supply_fresh(&beyond, CETTA_NAME_KIND_SPACE);
    CHECK(!cetta_generated_name_eq(parent_after, deepest) &&
              !cetta_generated_name_eq(parent_after, child_name) &&
              !cetta_generated_name_eq(child_name, deepest) &&
              chain.drawn[CETTA_NAME_KIND_SPACE] == 2u &&
              beyond.drawn[CETTA_NAME_KIND_SPACE] == 2u,
          "the parent's names before and after, and the task's, are "
          "distinct, at the clocks they had");
    check_reads_back(parent_after);
    check_reads_back(child_name);
    /* Join back in the block: the whole of the block. */
    CettaNameSupply rejoined = cetta_name_supply_join(&chain, &beyond);
    CHECK(rejoined.block == chain.block && rejoined.share == CETTA_NAME_SHARE_WHOLE,
          "the halves of a block join to its whole");
    /* A join across blocks: the root block's whole and the block's whole. */
    CettaNameSupply other;
    cetta_name_supply_init_root(&other);
    CettaNameSupply across = cetta_name_supply_join(&rejoined, &other);
    CettaNameSupply across_again = cetta_name_supply_join(&other, &rejoined);
    CHECK(across.block != NULL && across.block != rejoined.block &&
              across.share == CETTA_NAME_SHARE_WHOLE &&
              across.block->key_hi == across_again.block->key_hi &&
              across.block->key_lo == across_again.block->key_lo &&
              cetta_name_supply_live_blocks() == 3u,
          "a join across blocks takes a block of its own, the same either "
          "way round");
    cetta_name_supply_dispose(&across_again);
    cetta_name_supply_dispose(&across);
    cetta_name_supply_dispose(&rejoined);
    cetta_name_supply_dispose(&chain);
    CHECK(cetta_name_supply_live_blocks() == 1u, "the task keeps the block");
    cetta_name_supply_dispose(&beyond);
    CHECK(cetta_name_supply_live_blocks() == 0u, "the last holder gives it back");
    /* The union of two intervals 40 halvings deep does not fit the code:
       it moves on to a block of its own. */
    CettaNameSupply deep_left, deep_right;
    cetta_name_supply_init_root(&deep_left);
    cetta_name_supply_init_root(&deep_right);
    deep_left.share = UINT64_C(1) << 40;
    deep_right.share = (UINT64_C(1) << 41) - 1u;
    CettaNameSupply wide = cetta_name_supply_join(&deep_left, &deep_right);
    CHECK(wide.block != NULL && wide.share == CETTA_NAME_SHARE_WHOLE &&
              cetta_name_supply_live_blocks() == 1u,
          "a union too wide for the word moves on to a block of its own");
    cetta_name_supply_dispose(&wide);
    CHECK(cetta_name_supply_live_blocks() == 0u, "every block is given back");
}

/* The block key a supply's move-on should have: SipHash-2-4-128 under the
   mode's key of MESSAGE, as the high and low words of the block's key. */
static void expected_key(const uint8_t *message, size_t length, uint64_t *hi,
                         uint64_t *lo) {
    uint8_t key[16] = {0}, digest[16];
    if (g_unforgeable) {
        const char *seed = getenv("CETTA_NAME_SEED");
        if (!seed || strcmp(seed, "000102030405060708090a0b0c0d0e0f") != 0) {
            *hi = *lo = 0u;
            return;
        }
        for (unsigned i = 0u; i < 16u; i++)
            key[i] = (uint8_t)i;
    }
    cetta_siphash24_128(key, message, length, digest);
    *hi = *lo = 0u;
    for (unsigned i = 0u; i < 8u; i++) {
        *hi = (*hi << 8) | digest[i];
        *lo = (*lo << 8) | digest[8u + i];
    }
}

static size_t put_le64(uint8_t *at, uint64_t value) {
    for (unsigned i = 0u; i < 8u; i++)
        at[i] = (uint8_t)(value >> (8u * i));
    return 8u;
}

static bool key_is(const CettaNameBlock *block, const uint8_t *message,
                   size_t length) {
    uint64_t hi, lo;
    expected_key(message, length, &hi, &lo);
    return block && ((hi == 0u && lo == 0u && g_unforgeable) ||
                     (block->key_hi == hi && block->key_lo == lo));
}

/* ── Balanced pieces ─────────────────────────────────────────────────── */

/* The measure of a share, the total length of its intervals, as a
   multiple of 2^-62; the share lies at most 62 halvings below DEPTH. */
static uint64_t share_measure(const Share *s, unsigned depth) {
    if (s->kind == SHARE_NONE) return 0u;
    if (s->kind == SHARE_ALL) return UINT64_C(1) << (62u - depth);
    return share_measure(s->left, depth + 1u) + share_measure(s->right, depth + 1u);
}

/* The pieces of spawns against the balanced pieces on the trees.  From
   the whole interval, a half, a quarter, an interval 40 halvings deep and
   two coded shares, spawns of 1 to 4097 components: every piece is the
   share along the balanced path of i among k, at the spawner's clocks with
   nothing pushed, and within ceil(log2 k) halvings of it; together they
   cover the share and their measures add up to its own, so no two
   overlap, which the trees check pair by pair up to 129 pieces; a second
   spawn gives the same pieces.  The 640 pieces of the whole interval are
   intervals at most 10 halvings deep. */
static void test_balanced_pieces(void) {
    static const uint32_t counts[] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 15u, 16u, 17u, 31u, 32u,
        33u, 63u, 64u, 65u, 100u, 127u, 128u, 129u, 255u, 256u, 257u, 640u,
        1000u, 1023u, 1024u, 1025u, 4097u};
    /* Coded: two quarters LL and RR, and L with RLL and RRR. */
    g_pool_used = 0u;
    const Share *three = share_node(
        &k_all, share_node(share_node(&k_all, &k_none), share_node(&k_none, &k_all)));
    const uint64_t spawners[] = {
        CETTA_NAME_SHARE_WHOLE, 3u, 2u, 5u,
        (UINT64_C(1) << 40) | UINT64_C(0x5a5a5a5a5),
        CETTA_NAME_SHARE_CODED | UINT64_C(0xe91), word_of_share(three)};
    enum { MOST = 4097u };
    CettaNameSupply *got = malloc(MOST * sizeof(*got));
    const Share **expected = malloc(MOST * sizeof(*expected));
    bool exact = true, bounded = true, covered = true, apart = true,
         same = true, clean = true, wide = true;
    size_t spawns = 0u, pieces_taken = 0u;
    for (size_t w = 0u; w < sizeof(spawners) / sizeof(spawners[0]); w++) {
        for (size_t c = 0u; c < sizeof(counts) / sizeof(counts[0]); c++) {
            uint32_t k = counts[c];
            g_pool_used = 0u;
            const Share *tree = share_of_word(spawners[w]);
            CettaNameSupply from;
            cetta_name_supply_init_root(&from);
            from.share = spawners[w];
            from.drawn[CETTA_NAME_KIND_SPACE] = 7u;
            from.drawn[CETTA_NAME_KIND_CALLABLE] = 3u;
            CettaNameSupplyPieces pieces, again;
            cetta_name_supply_pieces_begin(&pieces, &from, k);
            for (uint32_t i = 0u; i < k; i++)
                got[i] = cetta_name_supply_pieces_next(&pieces);
            clean = clean && pieces.waiting == 0u && from.share == spawners[w];
            cetta_name_supply_pieces_begin(&again, &from, k);
            for (uint32_t i = 0u; i < k; i++) {
                CettaNameSupply second = cetta_name_supply_pieces_next(&again);
                same = same && second.share == got[i].share &&
                       second.block == got[i].block;
                cetta_name_supply_dispose(&second);
            }
            unsigned bound = share_depth(tree) + ceil_log2(k);
            bool interval = (spawners[w] & CETTA_NAME_SHARE_CODED) == 0u;
            const Share *all_pieces = &k_none;
            uint64_t measures = 0u;
            for (uint32_t i = 0u; i < k; i++) {
                /* The share along the path; of an interval, the interval
                   one path further. */
                bool path[64];
                unsigned length = balanced_path(k, i, path);
                expected[i] = interval
                    ? share_of_word(word_along(spawners[w], path, length))
                    : share_at(tree, path, length);
                exact = exact && got[i].block == NULL &&
                        supply_is_share(&got[i], expected[i]) &&
                        got[i].drawn[0] == 7u && got[i].drawn[1] == 3u &&
                        got[i].pushes == 0u && got[i].pushed == 0u;
                bounded = bounded && share_depth(expected[i]) <= bound;
                all_pieces = share_join(all_pieces, expected[i]);
                measures += share_measure(expected[i], 0u);
                if (k <= 129u)
                    for (uint32_t j = 0u; j < i; j++)
                        apart = apart && !share_overlap(expected[j], expected[i]);
            }
            covered = covered && share_eq(all_pieces, tree);
            apart = apart && measures == share_measure(tree, 0u);
            if (spawners[w] == CETTA_NAME_SHARE_WHOLE && k == 640u)
                for (uint32_t i = 0u; i < k; i++)
                    wide = wide && (got[i].share & CETTA_NAME_SHARE_CODED) == 0u &&
                           63u - (unsigned)__builtin_clzll(got[i].share) <= 10u;
            for (uint32_t i = 0u; i < k; i++)
                cetta_name_supply_dispose(&got[i]);
            spawns++;
            pieces_taken += k;
        }
    }
    CHECK(exact, "every piece is the share along its balanced path, at the "
                 "spawner's clocks, with nothing pushed");
    CHECK(bounded, "every piece lies within ceil(log2 k) halvings of the share");
    CHECK(covered, "the pieces cover the share");
    CHECK(apart, "no two pieces overlap: pair by pair, and their measures add "
                 "up to the share's");
    CHECK(same, "a second spawn gives the same pieces");
    CHECK(clean, "a spawn takes all its pieces and leaves the spawner as it was");
    CHECK(wide, "640 pieces of the whole interval are intervals at most 10 "
                "halvings deep");
    CHECK(cetta_name_supply_live_blocks() == 0u, "pieces that fit make no block");

    /* Below an interval 58 halvings deep, 64 pieces would lie 64 deep: each
       of the 16 shares 62 deep moves on to a block of its own, keyed by its
       place, which its four pieces share. */
    CettaNameSupply deep;
    cetta_name_supply_init_root(&deep);
    deep.share = UINT64_C(1) << 58;
    deep.drawn[CETTA_NAME_KIND_SPACE] = 1u;
    CettaNameSupplyPieces pieces;
    cetta_name_supply_pieces_begin(&pieces, &deep, 64u);
    Fingerprint prints[65];
    bool grouped = true, keyed = true;
    for (uint32_t i = 0u; i < 64u; i++) {
        got[i] = cetta_name_supply_pieces_next(&pieces);
        grouped = grouped && got[i].block != NULL &&
                  got[i].block == got[i - i % 4u].block &&
                  got[i].share == 4u + i % 4u;
        if (i % 4u == 0u) {
            uint8_t message[64];
            size_t length = 0u;
            message[length++] = 0x01u;
            memset(message + length, 0, 16u);
            length += 16u;
            length += put_le64(message + length, 1u);
            length += put_le64(message + length, 0u);
            message[length++] = 0u;
            length += put_le64(message + length, (UINT64_C(1) << 62) | (i / 4u));
            keyed = keyed && key_is(got[i].block, message, length);
            grouped = grouped && (i == 0u || got[i].block != got[i - 4u].block);
        }
        prints[i] = spelling_fingerprint(
            CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&got[i], CETTA_NAME_KIND_SPACE));
    }
    prints[64] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&deep, CETTA_NAME_KIND_SPACE));
    CHECK(grouped && cetta_name_supply_live_blocks() == 16u,
          "64 pieces 58 halvings down: 16 blocks of their own, four pieces each "
          "(%llu blocks)", (unsigned long long)cetta_name_supply_live_blocks());
    CHECK(keyed, "each block is keyed by the place of the share 62 halvings deep "
                 "it stands for");
    CHECK(fingerprint_repeats(prints, 65u) == 0u,
          "the pieces' names and the spawner's are distinct");
    for (uint32_t i = 0u; i < 64u; i++)
        cetta_name_supply_dispose(&got[i]);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and give their blocks back");
    /* A spawn that stops early ends the pieces it did not take. */
    cetta_name_supply_pieces_begin(&pieces, &deep, 64u);
    for (uint32_t i = 0u; i < 6u; i++)
        got[i] = cetta_name_supply_pieces_next(&pieces);
    cetta_name_supply_pieces_end(&pieces);
    for (uint32_t i = 0u; i < 6u; i++)
        cetta_name_supply_dispose(&got[i]);
    CHECK(pieces.waiting == 0u && cetta_name_supply_live_blocks() == 0u,
          "a spawn that stops early gives back the blocks of the pieces it did "
          "not take");
    cetta_name_supply_dispose(&deep);
    printf("PASS: %zu spawns of 1 to 4097 components from seven shares take "
           "%zu balanced pieces: each the share along its balanced path, within "
           "ceil(log2 k) halvings, covering it, pairwise apart, the same "
           "again; 640 pieces of the whole lie within 10 halvings; below 58 "
           "halvings, pieces move on in blocks keyed by their places\n",
           spawns, pieces_taken);
    free(got);
    free(expected);
}

/* ── Counted pushes ──────────────────────────────────────────────────── */

/* An open-addressed set of share words. */
typedef struct {
    uint64_t *slot;
    size_t mask;
} WordSet;

static void word_set_init(WordSet *set, size_t at_least) {
    size_t size = 1u;
    while (size < 2u * at_least) size <<= 1;
    set->slot = calloc(size, sizeof(*set->slot));
    set->mask = size - 1u;
}

static size_t word_set_at(const WordSet *set, uint64_t word) {
    size_t at = (size_t)((word * UINT64_C(0x9e3779b97f4a7c15)) >> 20) & set->mask;
    while (set->slot[at] != 0u && set->slot[at] != word)
        at = (at + 1u) & set->mask;
    return at;
}

/* PUSHES pushes from a root, one at a time, each pushed task drawing a
   name, and the pusher drawing every thousand: push n must be the right
   half along the counted path of n in the root block, within
   2 floor(log2(n+1)) + 2 halvings.  The words into WORDS, the names'
   fingerprints into PRINTS (their number returned), and whether every
   push and name was as it should be into *OK. */
static size_t push_names(unsigned pushes, uint64_t *words, Fingerprint *prints,
                         bool *ok) {
    CettaNameSupply pusher;
    cetta_name_supply_init_root(&pusher);
    size_t count = 0u;
    bool fine = true;
    for (unsigned n = 0u; n < pushes; n++) {
        if (n % 1000u == 0u) {
            CettaGeneratedName own = cetta_name_supply_fresh(&pusher, CETTA_NAME_KIND_SPACE);
            fine = fine && cetta_generated_name_block(own) == NULL;
            prints[count++] = spelling_fingerprint(CETTA_NAME_KIND_SPACE, own);
        }
        CettaNameSupply child;
        cetta_name_supply_push_into(&pusher, &child);
        bool path[64];
        unsigned length = counted_path(n, path);
        unsigned block = 0u;
        while ((n + 1u) >> (block + 1u))
            block++;
        fine = fine && child.block == NULL && child.pushes == 0u &&
               child.share == word_along(3u, path, length) &&
               length + 1u <= 2u * block + 2u &&
               child.drawn[0] == pusher.drawn[0];
        words[n] = child.share;
        CettaNameKind kind = n % 3u ? CETTA_NAME_KIND_SPACE : CETTA_NAME_KIND_CALLABLE;
        CettaGeneratedName name = cetta_name_supply_fresh(&child, kind);
        fine = fine && cetta_generated_name_block(name) == NULL;
        if (n % 4099u == 0u)
            check_reads_back(name);
        prints[count++] = spelling_fingerprint(kind, name);
        cetta_name_supply_dispose(&child);
    }
    fine = fine && pusher.block == NULL && pusher.share == 2u &&
           pusher.pushes == pushes && cetta_name_supply_live_blocks() == 0u;
    prints[count++] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&pusher, CETTA_NAME_KIND_SPACE));
    cetta_name_supply_dispose(&pusher);
    *ok = fine;
    return count;
}

static void test_counted_pushes(void) {
    enum { PUSHES = 1000000u, SHORT = 100u };
    uint64_t *words = malloc(PUSHES * sizeof(*words));
    uint64_t *short_words = malloc(SHORT * sizeof(*short_words));
    size_t slots = PUSHES + PUSHES / 1000u + 8u;
    Fingerprint *first = malloc(slots * sizeof(*first));
    Fingerprint *again = malloc(slots * sizeof(*again));
    bool ok = false, ok_again = false, ok_short = false;
    size_t count = push_names(PUSHES, words, first, &ok);
    size_t count_again = push_names(PUSHES, words, again, &ok_again);
    CHECK(ok && ok_again, "push n is the pool, the right half, along the "
                          "counted path of n, in the root block, within "
                          "2 floor(log2(n+1)) + 2 halvings, and the pusher "
                          "keeps the left half");
    CHECK(count == count_again && memcmp(first, again, count * sizeof(*first)) == 0,
          "a million pushes give the same names on a second run");
    Fingerprint short_prints[SHORT + SHORT / 1000u + 8u];
    push_names(SHORT, short_words, short_prints, &ok_short);
    CHECK(ok_short && memcmp(short_words, words, SHORT * sizeof(*words)) == 0,
          "the first pushes do not depend on how many follow (count-oblivious)");
    /* No push's interval lies inside another's: none of a push's prefixes
       down to the pool is a push. */
    WordSet set;
    word_set_init(&set, PUSHES);
    for (unsigned n = 0u; n < PUSHES; n++)
        set.slot[word_set_at(&set, words[n])] = words[n];
    size_t nested = 0u, deepest = 0u;
    for (unsigned n = 0u; n < PUSHES; n++) {
        unsigned depth = 63u - (unsigned)__builtin_clzll(words[n]);
        if (depth > deepest) deepest = depth;
        for (uint64_t prefix = words[n] >> 1; prefix > 3u; prefix >>= 1)
            nested += set.slot[word_set_at(&set, prefix)] == prefix;
    }
    CHECK(nested == 0u, "no push lies inside another (%zu nested)", nested);
    CHECK(deepest <= 40u, "a million pushes lie within 40 halvings (%zu)", deepest);
    size_t repeats = fingerprint_repeats(first, count);
    CHECK(repeats == 0u, "%zu of %zu names of the pushes repeat", repeats, count);
    printf("PASS: a million pushes from the root, one at a time: push n is the "
           "pool along the counted path of n, the deepest %zu halvings "
           "down, none inside another, all %zu names distinct and in the "
           "root block, the same on a second run, the first ones whatever "
           "follows\n",
           deepest, count);
    free(set.slot);
    free(words);
    free(short_words);
    free(first);
    free(again);
}

/* A pool spent, and first pushes that do not fit: the pusher moves on to a
   block of its own, keyed by its place, and pushes from the right half of
   its whole interval there; a coded share whose right half is one interval
   pushes from that interval in its own block. */
static void test_pushes_moving_on(void) {
    /* From an interval 50 halvings deep: the pool lies 51 deep, so pushes
       0 to 62 fit, and push 63 would lie 64 deep. */
    CettaNameSupply pusher;
    cetta_name_supply_init_root(&pusher);
    pusher.share = UINT64_C(1) << 50;
    pusher.drawn[CETTA_NAME_KIND_SPACE] = 4u;
    Fingerprint prints[80];
    size_t count = 0u;
    bool fit = true;
    for (unsigned n = 0u; n < 63u; n++) {
        CettaNameSupply child;
        cetta_name_supply_push_into(&pusher, &child);
        bool path[64];
        unsigned length = counted_path(n, path);
        fit = fit && child.block == NULL &&
              child.share == word_along((UINT64_C(1) << 51) | 1u, path, length);
        prints[count++] = spelling_fingerprint(
            CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&child, CETTA_NAME_KIND_SPACE));
        cetta_name_supply_dispose(&child);
    }
    CHECK(fit && pusher.share == UINT64_C(1) << 51 && pusher.pushes == 63u,
          "pushes 0 to 62 from a pool 51 halvings deep fit the word");
    uint64_t own = pusher.share, last = pusher.pushed;
    prints[count++] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&pusher, CETTA_NAME_KIND_SPACE));
    CettaNameSupply beyond;
    cetta_name_supply_push_into(&pusher, &beyond);
    CHECK(beyond.block != NULL && beyond.block == pusher.block &&
              pusher.share == 2u && beyond.share == 6u && pusher.pushes == 1u &&
              pusher.drawn[CETTA_NAME_KIND_SPACE] == 5u &&
              cetta_name_supply_live_blocks() == 1u,
          "push 63 finds the pool spent: the pusher moves on and pushes from "
          "the right half of its new block's whole interval");
    {
        uint8_t message[64];
        size_t length = 0u;
        message[length++] = 0x04u;
        memset(message + length, 0, 16u);
        length += 16u;
        length += put_le64(message + length, 5u);
        length += put_le64(message + length, 0u);
        length += put_le64(message + length, own);
        length += put_le64(message + length, last);
        length += put_le64(message + length, 63u);
        CHECK(key_is(pusher.block, message, length),
              "the block is keyed by the pusher's place: its block's key, "
              "clocks, share, last push and count");
    }
    CettaNameSupply next;
    cetta_name_supply_push_into(&pusher, &next);
    CHECK(next.block == pusher.block && next.share == 28u,
          "and pushes on there: push 1 of its new pool");
    prints[count++] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&beyond, CETTA_NAME_KIND_SPACE));
    prints[count++] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&next, CETTA_NAME_KIND_SPACE));
    prints[count++] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&pusher, CETTA_NAME_KIND_SPACE));
    CHECK(fingerprint_repeats(prints, count) == 0u,
          "the names before and after the move are distinct");
    cetta_name_supply_dispose(&next);
    cetta_name_supply_dispose(&beyond);
    cetta_name_supply_dispose(&pusher);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and give their block back");

    /* A first push from an interval 61 halvings deep would lie 63 deep: the
       supply moves on as a split does, keyed by its share. */
    CettaNameSupply deep;
    cetta_name_supply_init_root(&deep);
    deep.share = UINT64_C(1) << 61;
    CettaNameSupply first;
    cetta_name_supply_push_into(&deep, &first);
    {
        uint8_t message[64];
        size_t length = 0u;
        message[length++] = 0x01u;
        memset(message + length, 0, 16u);
        length += 16u;
        length += put_le64(message + length, 0u);
        length += put_le64(message + length, 0u);
        message[length++] = 0u;
        length += put_le64(message + length, UINT64_C(1) << 61);
        CHECK(first.block != NULL && first.block == deep.block &&
                  deep.share == 2u && first.share == 6u && deep.pushes == 1u &&
                  key_is(deep.block, message, length),
              "a first push 63 halvings deep moves the pusher on, keyed by "
              "its share");
    }
    cetta_name_supply_dispose(&first);
    cetta_name_supply_dispose(&deep);

    /* Coded: LL and RR keep LL and push from RR in the root block; L, RLL
       and RRR would pool a share that is no interval, and move on. */
    CettaNameSupply quarters;
    cetta_name_supply_init_root(&quarters);
    quarters.share = CETTA_NAME_SHARE_CODED | UINT64_C(0xe91);
    CettaNameSupply from_quarters;
    cetta_name_supply_push_into(&quarters, &from_quarters);
    CettaNameSupply second;
    cetta_name_supply_push_into(&quarters, &second);
    CHECK(quarters.block == NULL && quarters.share == 4u &&
              from_quarters.block == NULL && from_quarters.share == 14u &&
              second.block == NULL && second.share == 7u * 8u + 4u,
          "two quarters keep LL and push from RR in their block");
    g_pool_used = 0u;
    const Share *three = share_node(
        &k_all, share_node(share_node(&k_all, &k_none), share_node(&k_none, &k_all)));
    CettaNameSupply coded;
    cetta_name_supply_init_root(&coded);
    coded.share = word_of_share(three);
    CettaNameSupply from_coded;
    cetta_name_supply_push_into(&coded, &from_coded);
    CHECK(coded.block != NULL && from_coded.block == coded.block &&
              coded.share == 2u && from_coded.share == 6u,
          "a share whose right half is no interval moves on before it pushes");
    cetta_name_supply_dispose(&from_quarters);
    cetta_name_supply_dispose(&second);
    cetta_name_supply_dispose(&quarters);
    cetta_name_supply_dispose(&from_coded);
    cetta_name_supply_dispose(&coded);
    CHECK(cetta_name_supply_live_blocks() == 0u, "every block is given back");
    printf("PASS: a spent pool and first pushes that do not fit move the "
           "pusher on to a block keyed by its place\n");
}

/* A chain of LEVELS supplies, each the first push of the one before (or,
   FORKS, its right half), which ends as soon as it has pushed (as a task
   that pushes its successor does), drawing every 997 levels and at the
   bottom.  The fingerprints of the names into PRINTS; the most blocks
   alive at once into *PEAK. */
static size_t chain_names(unsigned levels, bool forks, Fingerprint *prints,
                          uint64_t *peak) {
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
        CettaNameSupply child;
        if (forks)
            cetta_name_supply_fork_into(&chain, &child);
        else
            cetta_name_supply_push_into(&chain, &child);
        uint64_t live = cetta_name_supply_live_blocks();
        if (live > *peak)
            *peak = live;
        cetta_name_supply_dispose(&chain);
        chain = child;
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
    for (unsigned forks = 0u; forks < 2u; forks++) {
        const char *how = forks ? "fork" : "push";
        uint64_t peak = 0u, peak_again = 0u;
        size_t count = chain_names(LEVELS, forks, first, &peak);
        CHECK(cetta_name_supply_live_blocks() == 0u,
              "a chain by %s gives back every block", how);
        size_t count_again = chain_names(LEVELS, forks, again, &peak_again);
        CHECK(count == count_again && memcmp(first, again, count * sizeof(*first)) == 0,
              "a chain of a million levels by %s gives the same names on a "
              "second run", how);
        CHECK(peak <= 2u, "at most two blocks of a chain by %s are alive at "
                          "once (%llu)", how, (unsigned long long)peak);
        size_t repeats = fingerprint_repeats(first, count);
        CHECK(repeats == 0u, "%zu of %zu names of a chain by %s repeat",
              repeats, count, how);
        printf("PASS: a chain of %u levels, each the %s of the one before, "
               "gives %zu distinct names, the same on a second run, with at "
               "most %llu blocks alive at once and all given back\n",
               (unsigned)LEVELS, forks ? "right half" : "first push", count,
               (unsigned long long)peak);
    }
    free(first);
    free(again);
}

/* A million siblings pushed one at a time by one parent, each drawing a
   name: counted pushes keep every one in the root block. */
static void test_width(void) {
    enum { SIBLINGS = 1000000u };
    Fingerprint *prints = malloc((SIBLINGS + 2u) * sizeof(*prints));
    CettaNameSupply parent;
    cetta_name_supply_init_root(&parent);
    size_t count = 0u;
    uint64_t peak = 0u;
    for (unsigned i = 0u; i < SIBLINGS; i++) {
        CettaNameSupply child;
        cetta_name_supply_push_into(&parent, &child);
        CettaGeneratedName name = cetta_name_supply_fresh(&child, CETTA_NAME_KIND_SPACE);
        if (i % 4099u == 0u)
            check_reads_back(name);
        prints[count++] = spelling_fingerprint(CETTA_NAME_KIND_SPACE, name);
        uint64_t live = cetta_name_supply_live_blocks();
        if (live > peak)
            peak = live;
        cetta_name_supply_dispose(&child);
    }
    prints[count++] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&parent, CETTA_NAME_KIND_SPACE));
    size_t repeats = fingerprint_repeats(prints, count);
    CHECK(repeats == 0u, "%zu of %zu names of siblings repeat", repeats, count);
    CHECK(peak == 0u && parent.block == NULL,
          "a million siblings and their parent stay in the root block (%llu "
          "blocks)", (unsigned long long)peak);
    cetta_name_supply_dispose(&parent);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and make no block");
    printf("PASS: a million siblings and their parent give %zu distinct "
           "names, all in the root block\n", count);
    free(prints);
}

/* A balanced fork and join, as the duplicator loop's spawn and COMM: the
   supply comes back to itself, its share never leaves the word, and its
   names never repeat. */
static void test_balanced(void) {
    enum { ROUNDS = 1000000u };
    Fingerprint *prints = malloc(3u * ROUNDS * sizeof(*prints));
    CettaNameSupply loop;
    cetta_name_supply_init_root(&loop);
    CettaNameSupply start = cetta_name_supply_fork(&loop);
    cetta_name_supply_dispose(&loop);
    loop = start;
    uint64_t share = loop.share;
    size_t count = 0u;
    bool same = true;
    for (unsigned round = 0u; round < ROUNDS; round++) {
        CettaNameSupplyPieces pieces;
        cetta_name_supply_pieces_begin(&pieces, &loop, 2u);
        cetta_name_supply_dispose(&loop);
        CettaNameSupply receiver = cetta_name_supply_pieces_next(&pieces);
        CettaNameSupply sender = cetta_name_supply_pieces_next(&pieces);
        prints[count++] = spelling_fingerprint(
            CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&sender, CETTA_NAME_KIND_SPACE));
        prints[count++] = spelling_fingerprint(
            CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&receiver, CETTA_NAME_KIND_SPACE));
        loop = cetta_name_supply_join(&sender, &receiver);
        cetta_name_supply_dispose(&sender);
        cetta_name_supply_dispose(&receiver);
        prints[count++] = spelling_fingerprint(
            CETTA_NAME_KIND_SPACE, cetta_name_supply_fresh(&loop, CETTA_NAME_KIND_SPACE));
        same = same && loop.share == share && loop.block == NULL;
    }
    CHECK(same && cetta_name_supply_live_blocks() == 0u,
          "a million balanced forks and joins keep the share and make no block");
    size_t repeats = fingerprint_repeats(prints, count);
    CHECK(repeats == 0u, "%zu of %zu names of the balanced loop repeat",
          repeats, count);
    cetta_name_supply_dispose(&loop);
    printf("PASS: a million balanced forks and joins give %zu distinct names "
           "in the root block, the share as it was\n", count);
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
          "a clock at its end goes on in a fresh block, every name distinct");
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
    printf("PASS: drawing goes on past a clock's end\n");
}

static void test_unplaced(void) {
    CettaNameSupply between;
    cetta_name_supply_init_unplaced(&between);
    cetta_name_supply_dispose(&between);
    CHECK(cetta_name_supply_live_blocks() == 0u, "an unused supply outside the tree holds nothing");
    cetta_name_supply_init_unplaced(&between);
    CettaGeneratedName a = cetta_name_supply_fresh(&between, CETTA_NAME_KIND_SPACE);
    CettaNameSupply child = cetta_name_supply_fork(&between);
    CettaGeneratedName b = cetta_name_supply_fresh(&between, CETTA_NAME_KIND_SPACE);
    CettaNameSupply other;
    cetta_name_supply_init_unplaced(&other);
    CettaNameSupply forked = cetta_name_supply_fork(&other);
    CettaGeneratedName c = cetta_name_supply_fresh(&other, CETTA_NAME_KIND_SPACE);
    CettaNameSupply third, pushed;
    cetta_name_supply_init_unplaced(&third);
    cetta_name_supply_push_into(&third, &pushed);
    CettaGeneratedName e = cetta_name_supply_fresh(&pushed, CETTA_NAME_KIND_SPACE);
    CettaGeneratedName f = cetta_name_supply_fresh(&third, CETTA_NAME_KIND_SPACE);
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    CettaGeneratedName d = cetta_name_supply_fresh(&root, CETTA_NAME_KIND_SPACE);
    Fingerprint prints[6] = {
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, a),
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, b),
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, c),
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, d),
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, e),
        spelling_fingerprint(CETTA_NAME_KIND_SPACE, f),
    };
    CHECK(cetta_generated_name_is_name(a) && cetta_generated_name_is_name(b) &&
              cetta_generated_name_is_name(c) && fingerprint_repeats(prints, 6u) == 0u &&
              child.block == between.block && forked.block == other.block &&
              other.block != between.block && pushed.block == third.block &&
              third.block != other.block && third.block != between.block &&
              third.share == 2u && pushed.share == 6u,
          "a supply outside the tree gives fresh names in blocks of its own, "
          "and pushes from one");
    cetta_name_supply_dispose(&child);
    cetta_name_supply_dispose(&forked);
    cetta_name_supply_dispose(&pushed);
    cetta_name_supply_dispose(&third);
    cetta_name_supply_dispose(&between);
    cetta_name_supply_dispose(&other);
    cetta_name_supply_dispose(&root);
    CHECK(cetta_name_supply_live_blocks() == 0u, "and gives them back");
}

/* The clocks of the supplies that end are handed to the thread's ending
   clocks, if any. */
static void test_ending_clocks(void) {
    uint64_t clocks[CETTA_NAME_KIND_COUNT] = {0u, 0u};
    uint64_t *previous = cetta_name_supply_ending_clocks_install(clocks);
    CettaNameSupply root, unplaced;
    cetta_name_supply_init_root(&root);
    cetta_name_supply_init_unplaced(&unplaced);
    CettaNameSupply task;
    cetta_name_supply_push_into(&root, &task);
    for (unsigned i = 0u; i < 5u; i++)
        (void)cetta_name_supply_fresh(&task, CETTA_NAME_KIND_CALLABLE);
    cetta_name_supply_dispose(&task);
    cetta_name_supply_dispose(&unplaced);
    CHECK(clocks[CETTA_NAME_KIND_SPACE] == 0u &&
              clocks[CETTA_NAME_KIND_CALLABLE] == 5u,
          "an ending supply hands on its clocks, and one outside the tree "
          "none (%llu %llu)", (unsigned long long)clocks[0],
          (unsigned long long)clocks[1]);
    cetta_name_supply_ending_clocks_install(previous);
    cetta_name_supply_dispose(&root);
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

/* NAMES names from a random tree of supplies: mostly deep (a push or a
   fork goes on in the task nine times in ten), some wide, now and then a
   spawn of three, two supplies joining at times as a COMM's do, every
   supply ending at some point.  The names are kept, holding their blocks,
   in DRAWN; the result is a fingerprint of their spellings in order. */
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
        } else if (action < 80u && active_len < ACTIVE) {
            if (action < 70u)
                cetta_name_supply_push_into(&active[current], &active[active_len]);
            else
                cetta_name_supply_fork_into(&active[current], &active[active_len]);
            if (((r >> 16) % 10u) != 0u)
                current = active_len;
            active_len++;
        } else if (action < 82u && active_len + 2u <= ACTIVE) {
            /* A spawn of three: the spawner's balanced pieces. */
            CettaNameSupplyPieces pieces;
            cetta_name_supply_pieces_begin(&pieces, &active[current], 3u);
            cetta_name_supply_dispose(&active[current]);
            active[current] = cetta_name_supply_pieces_next(&pieces);
            active[active_len++] = cetta_name_supply_pieces_next(&pieces);
            active[active_len++] = cetta_name_supply_pieces_next(&pieces);
        } else if (action < 88u && active_len > 1u) {
            /* Two supplies end together, leaving their join. */
            size_t other = (size_t)((r >> 24) % active_len);
            if (other == current)
                other = (current + 1u) % active_len;
            CettaNameSupply joined =
                cetta_name_supply_join(&active[current], &active[other]);
            cetta_name_supply_dispose(&active[current]);
            cetta_name_supply_dispose(&active[other]);
            active[current] = joined;
            active[other] = active[--active_len];
            if (current == active_len)
                current = other;
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
    printf("PASS: %u names from a random tree of supplies that push, fork, "
           "spawn, join and end (%llu blocks held by them) are distinct as "
           "names and as spellings, the same on a second run, and give every "
           "block back\n",
           (unsigned)NAMES, (unsigned long long)blocks);
    free(drawn);
    free(prints);
}

int main(void) {
    g_pool = malloc(POOL * sizeof(*g_pool));
    test_siphash_vectors();
    test_formatter();
    test_order();
    test_mode();
    test_share_algebra();
    test_moving_on();
    test_balanced_pieces();
    test_counted_pushes();
    test_pushes_moving_on();
    test_depth();
    test_width();
    test_balanced();
    test_count();
    test_unplaced();
    test_ending_clocks();
    test_random_tree();
    free(g_pool);
    if (g_failures) {
        fprintf(stderr, "FAIL: %d of %ld name supply checks\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: %ld name supply checks (SipHash-2-4-128 reference vectors, "
           "numerals, order, the share algebra, balanced pieces, counted "
           "pushes, depth, width, balance, count)\n",
           g_checks);
    return 0;
}
