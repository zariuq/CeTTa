/*
 * name_supply.c -- the supply of generated names (name_supply.h): shares
 * of the unit interval with clocks, in the style of interval tree clocks.
 *
 * A thread draws from the supply installed for it.  The main thread, and a
 * thread that installs none, draws from the process's root; an embedding
 * engine installs its own root around each entry; a parallel worker installs
 * the supply its task was given when it was pushed (parallel_executor.c), and
 * a supply of its own between tasks.  Drawing touches only the drawing
 * thread's supply: no shared counter and no atomic operation on the share.
 *
 * The share algebra here follows Mettapedia's Machines/NameSupplies/Shares
 * exactly: a share is none, all, or a node of two halves, kept normal (no
 * node with two empty or two full halves); split and join are its split and
 * join, the pieces of a spawn BalancedPieces' balancedPiece and a push
 * SiblingPushes' countedChild, both along split.  The word holds a share as
 * a path when it is one dyadic interval and as its prefix code otherwise;
 * the general operations read a share out of its word into its prefix code,
 * a token per node or leaf, operate on that, and write the result back into
 * a word in the one form it has.  Moving on to a new block, when a share
 * does not fit the word, allocates the block and hashes its key; nothing
 * else here allocates.
 */

#include "name_supply.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    NAME_SUPPLY_MODE_COUNTER = 0,
    NAME_SUPPLY_MODE_UNFORGEABLE,
} NameSupplyMode;

static NameSupplyMode g_name_supply_mode = NAME_SUPPLY_MODE_COUNTER;
/* The key of every hash: the seed in unforgeable mode, zero in counter
   mode, whose block keys are public. */
static uint8_t g_name_supply_key[16];
static bool g_name_supply_valid = true;
static char g_name_supply_error[192];
static const char *g_name_supply_requested_mode;
static const char *g_name_supply_requested_seed;
static pthread_once_t g_name_supply_once = PTHREAD_ONCE_INIT;
static atomic_bool g_name_supply_ready;

static CettaNameSupply g_name_supply_process_root = {
    .share = CETTA_NAME_SHARE_WHOLE};
/* The block of a supply outside the split tree: never freed, and never
   drawn in (a supply in it moves on before its first draw or split). */
CettaNameBlock cetta_name_block_unplaced CETTA_NAME_SUPPLY_INTERNAL = {
    .references = 1u};
/* The blocks made outside the split tree, numbered so that each is new. */
static _Atomic uint64_t g_name_supply_unplaced_blocks;
static _Atomic uint64_t g_name_supply_live_blocks;
_Thread_local CettaNameSupply *cetta_name_supply_installed
    CETTA_NAME_SUPPLY_INTERNAL = &g_name_supply_process_root;
_Thread_local uint64_t *cetta_name_supply_ending_clocks
    CETTA_NAME_SUPPLY_INTERNAL;
atomic_bool cetta_name_supply_counting CETTA_NAME_SUPPLY_INTERNAL;

/* ── SipHash-2-4, 128-bit output ─────────────────────────────────────── */

static inline uint64_t siphash_rotl(uint64_t x, unsigned b) {
    return (x << b) | (x >> (64u - b));
}

/* Words as bytes in a fixed order, whatever the machine's. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define NAME_SUPPLY_LE64(v) (v)
#define NAME_SUPPLY_BE64(v) __builtin_bswap64(v)
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define NAME_SUPPLY_LE64(v) __builtin_bswap64(v)
#define NAME_SUPPLY_BE64(v) (v)
#else
#error "name_supply.c needs the machine's byte order"
#endif

static inline uint64_t siphash_load_le64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return NAME_SUPPLY_LE64(v);
}

static inline void siphash_store_le64(uint8_t *p, uint64_t v) {
    v = NAME_SUPPLY_LE64(v);
    memcpy(p, &v, sizeof(v));
}

static inline void name_supply_store_be64(uint8_t *p, uint64_t v) {
    v = NAME_SUPPLY_BE64(v);
    memcpy(p, &v, sizeof(v));
}

static inline uint64_t name_supply_load_be64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return NAME_SUPPLY_BE64(v);
}

#define SIPROUND                                                        \
    do {                                                                \
        v0 += v1; v1 = siphash_rotl(v1, 13); v1 ^= v0;                  \
        v0 = siphash_rotl(v0, 32);                                      \
        v2 += v3; v3 = siphash_rotl(v3, 16); v3 ^= v2;                  \
        v0 += v3; v3 = siphash_rotl(v3, 21); v3 ^= v0;                  \
        v2 += v1; v1 = siphash_rotl(v1, 17); v1 ^= v2;                  \
        v2 = siphash_rotl(v2, 32);                                      \
    } while (0)

void cetta_siphash24_128(const uint8_t key[16], const void *input,
                         size_t length, uint8_t out[16]) {
    const uint8_t *in = input;
    uint64_t k0 = siphash_load_le64(key);
    uint64_t k1 = siphash_load_le64(key + 8);
    uint64_t v0 = UINT64_C(0x736f6d6570736575) ^ k0;
    uint64_t v1 = UINT64_C(0x646f72616e646f6d) ^ k1 ^ 0xeeu;
    uint64_t v2 = UINT64_C(0x6c7967656e657261) ^ k0;
    uint64_t v3 = UINT64_C(0x7465646279746573) ^ k1;
    const uint8_t *end = in + (length - length % 8u);
    for (; in != end; in += 8) {
        uint64_t m = siphash_load_le64(in);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }
    uint64_t b = (uint64_t)length << 56;
    switch (length & 7u) {
    case 7: b |= (uint64_t)in[6] << 48; /* fall through */
    case 6: b |= (uint64_t)in[5] << 40; /* fall through */
    case 5: b |= (uint64_t)in[4] << 32; /* fall through */
    case 4: b |= (uint64_t)in[3] << 24; /* fall through */
    case 3: b |= (uint64_t)in[2] << 16; /* fall through */
    case 2: b |= (uint64_t)in[1] << 8;  /* fall through */
    case 1: b |= (uint64_t)in[0];       break;
    default: break;
    }
    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;
    v2 ^= 0xeeu;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    siphash_store_le64(out, v0 ^ v1 ^ v2 ^ v3);
    v1 ^= 0xddu;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    siphash_store_le64(out + 8, v0 ^ v1 ^ v2 ^ v3);
}

#undef SIPROUND

/* ── Configuration ───────────────────────────────────────────────────── */

static int name_supply_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A seed is 1 to 32 hexadecimal digits: the key's 16 bytes, most
   significant first, the missing leading digits zero. */
static bool name_supply_read_seed(const char *text, uint8_t key[16]) {
    size_t length = text ? strlen(text) : 0u;
    if (length == 0u || length > 32u)
        return false;
    memset(key, 0, 16u);
    for (size_t i = 0u; i < length; i++) {
        int digit = name_supply_hex_digit(text[length - 1u - i]);
        if (digit < 0)
            return false;
        key[15u - i / 2u] |= (uint8_t)(i % 2u ? digit << 4 : digit);
    }
    return true;
}

static bool name_supply_random_key(uint8_t key[16]) {
    FILE *source = fopen("/dev/urandom", "rb");
    if (!source)
        return false;
    bool ok = fread(key, 1u, 16u, source) == 16u;
    fclose(source);
    return ok;
}

static void name_supply_configure_once(void) {
    const char *mode = g_name_supply_requested_mode
        ? g_name_supply_requested_mode : getenv("CETTA_NAME_SUPPLY");
    /* Only hashed names have a seed: counter mode reads none from the
       environment, and checks one given on the command line. */
    bool hashed = mode && strcmp(mode, "unforgeable") == 0;
    const char *seed = g_name_supply_requested_seed
        ? g_name_supply_requested_seed
        : hashed ? getenv("CETTA_NAME_SEED") : NULL;
    uint8_t seed_key[16] = {0};
    g_name_supply_valid = true;
    if (!mode || !*mode || strcmp(mode, "counter") == 0) {
        g_name_supply_mode = NAME_SUPPLY_MODE_COUNTER;
    } else if (strcmp(mode, "unforgeable") == 0) {
        g_name_supply_mode = NAME_SUPPLY_MODE_UNFORGEABLE;
    } else {
        snprintf(g_name_supply_error, sizeof(g_name_supply_error),
                 "unknown name supply '%s' (expected counter or unforgeable)",
                 mode);
        g_name_supply_valid = false;
    }
    if (g_name_supply_valid && seed && *seed &&
        !name_supply_read_seed(seed, seed_key)) {
        snprintf(g_name_supply_error, sizeof(g_name_supply_error),
                 "invalid name seed '%s' (expected 1 to 32 hexadecimal digits)",
                 seed);
        g_name_supply_valid = false;
    }
    if (g_name_supply_valid && g_name_supply_mode == NAME_SUPPLY_MODE_UNFORGEABLE &&
        !(seed && *seed)) {
        if (!name_supply_random_key(seed_key)) {
            snprintf(g_name_supply_error, sizeof(g_name_supply_error),
                     "cannot read a random name seed: %s", strerror(errno));
            g_name_supply_valid = false;
        } else {
            char text[33];
            static const char hex[] = "0123456789abcdef";
            for (unsigned i = 0u; i < 16u; i++) {
                text[2u * i] = hex[seed_key[i] >> 4];
                text[2u * i + 1u] = hex[seed_key[i] & 15u];
            }
            text[32] = '\0';
            fprintf(stderr, "cetta: name seed %s (replay with --name-seed=%s)\n",
                    text, text);
        }
    }
    /* Counter mode keys its blocks with the public zero key. */
    if (g_name_supply_mode == NAME_SUPPLY_MODE_UNFORGEABLE)
        memcpy(g_name_supply_key, seed_key, sizeof(g_name_supply_key));
    atomic_store_explicit(&cetta_name_supply_counting,
                          g_name_supply_valid &&
                              g_name_supply_mode == NAME_SUPPLY_MODE_COUNTER,
                          memory_order_relaxed);
    atomic_store_explicit(&g_name_supply_ready, true, memory_order_release);
}

bool cetta_name_supply_configure(const char *mode, const char *seed,
                                 char *error, size_t error_size) {
    if (atomic_load_explicit(&g_name_supply_ready, memory_order_acquire) &&
        (mode || seed)) {
        /* A name was drawn before the options were read: they would not
           be the ones the run's names were drawn under. */
        if (error && error_size)
            snprintf(error, error_size,
                     "the name supply was in use before its options were read");
        return false;
    }
    g_name_supply_requested_mode = mode;
    g_name_supply_requested_seed = seed;
    pthread_once(&g_name_supply_once, name_supply_configure_once);
    if (!g_name_supply_valid && error && error_size)
        snprintf(error, error_size, "%s", g_name_supply_error);
    return g_name_supply_valid;
}

/* The configuration every draw and block is made under.  A host that never
   configured and whose environment cannot be read gets no names under it:
   the process stops, saying why, rather than give a name it was not asked
   for.  cetta and the embedding API configure first, so they never come
   here with a value that cannot be read. */
static void name_supply_require_configured(void) {
    if (!atomic_load_explicit(&g_name_supply_ready, memory_order_acquire))
        pthread_once(&g_name_supply_once, name_supply_configure_once);
    if (__builtin_expect(!g_name_supply_valid, 0)) {
        fprintf(stderr, "cetta: no generated names: %s\n", g_name_supply_error);
        abort();
    }
}

bool cetta_name_supply_unforgeable(void) {
    name_supply_require_configured();
    return g_name_supply_mode == NAME_SUPPLY_MODE_UNFORGEABLE;
}

/* ── Shares ──────────────────────────────────────────────────────────────
 *
 * A share read out of its word is its prefix code: a token per node or
 * leaf, in preorder, 0 none, 1 all, 2 a node followed by its two halves (the
 * serialization of PortableNames.writeShare).  A word's share has at most
 * 125 tokens (an interval 62 halvings deep); the union of two has at most
 * 249, and its halves a few more. */

enum {
    NAME_SHARE_NONE = 0,
    NAME_SHARE_ALL = 1,
    NAME_SHARE_NODE = 2,
    NAME_SHARE_TOKENS_MAX = 256,
    /* The deepest interval and the longest prefix code a word holds. */
    NAME_SHARE_PATH_MAX = 62,
    NAME_SHARE_CODE_MAX = 62,
};

typedef struct {
    uint8_t token[NAME_SHARE_TOKENS_MAX];
    uint32_t length;
} NameShareCode;

static inline void name_share_put(NameShareCode *code, uint8_t token) {
    if (code->length < NAME_SHARE_TOKENS_MAX)
        code->token[code->length] = token;
    code->length++;
}

/* The index just past the subtree that starts at AT. */
static uint32_t name_share_skip(const NameShareCode *code, uint32_t at) {
    uint32_t open = 1u;
    while (open != 0u && at < code->length) {
        if (code->token[at++] == NAME_SHARE_NODE)
            open++;
        else
            open--;
    }
    return at;
}

/* The share a word holds; false for no share word. */
static bool name_share_read(uint64_t word, NameShareCode *out) {
    out->length = 0u;
    if (word == 0u)
        return false;
    if (!(word & CETTA_NAME_SHARE_CODED)) {
        unsigned depth = 63u - (unsigned)__builtin_clzll(word);
        for (unsigned bit = depth; bit-- > 0u;) {
            name_share_put(out, NAME_SHARE_NODE);
            if ((word >> bit) & 1u)
                name_share_put(out, NAME_SHARE_NONE);
        }
        name_share_put(out, NAME_SHARE_ALL);
        for (unsigned bit = 0u; bit < depth; bit++)
            if (!((word >> bit) & 1u))
                name_share_put(out, NAME_SHARE_NONE);
        return true;
    }
    uint64_t code = word & ~CETTA_NAME_SHARE_CODED;
    if (code == 0u)
        return false;
    unsigned bits = 63u - (unsigned)__builtin_clzll(code);
    unsigned open = 1u;
    while (open != 0u) {
        if (bits == 0u)
            return false;
        if ((code >> --bits) & 1u) {
            name_share_put(out, NAME_SHARE_NODE);
            open++;
            continue;
        }
        if (bits == 0u)
            return false;
        name_share_put(out, (code >> --bits) & 1u ? NAME_SHARE_ALL
                                                  : NAME_SHARE_NONE);
        open--;
    }
    return bits == 0u;
}

/* The bits of the prefix code of SHARE: a node 1, an empty half 00, a full
   half 01.  Their number. */
static uint32_t name_share_code_bits(const NameShareCode *share) {
    uint32_t bits = 0u;
    for (uint32_t i = 0u; i < share->length; i++)
        bits += share->token[i] == NAME_SHARE_NODE ? 1u : 2u;
    return bits;
}

/* SHARE's word, in the one form it has: a path when it is one dyadic
   interval, otherwise its prefix code.  False when it fits neither. */
static bool name_share_write(const NameShareCode *share, uint64_t *word) {
    if (share->length == 0u || share->length > NAME_SHARE_TOKENS_MAX)
        return false;
    /* A normal share is one interval exactly when it has one full leaf:
       every node then has one empty half and the full leaf below the
       other, so the path is read off in one pass. */
    uint32_t full = 0u, bits = 0u;
    for (uint32_t i = 0u; i < share->length; i++) {
        full += share->token[i] == NAME_SHARE_ALL;
        bits += share->token[i] == NAME_SHARE_NODE ? 1u : 2u;
    }
    if (full == 1u) {
        uint64_t path = 1u;
        unsigned depth = 0u;
        for (uint32_t at = 0u; share->token[at] == NAME_SHARE_NODE;) {
            bool right = share->token[at + 1u] == NAME_SHARE_NONE;
            path = (path << 1) | (uint64_t)right;
            at += right ? 2u : 1u;
            if (++depth > NAME_SHARE_PATH_MAX)
                return false;
        }
        *word = path;
        return true;
    }
    if (bits > NAME_SHARE_CODE_MAX)
        return false;
    uint64_t code = 1u;
    for (uint32_t i = 0u; i < share->length; i++) {
        uint8_t token = share->token[i];
        if (token == NAME_SHARE_NODE)
            code = (code << 1) | 1u;
        else
            code = (code << 2) | token;
    }
    *word = CETTA_NAME_SHARE_CODED | code;
    return true;
}

/* The split of SHARE (Shares.split): down the nodes with an empty half to
   the whole interval, which halves, or to a node with two nonempty halves,
   which parts into them; the path down is kept around both. */
static void name_share_split(const NameShareCode *share, NameShareCode *left,
                             NameShareCode *right) {
    uint8_t turns[NAME_SHARE_TOKENS_MAX];
    uint32_t depth = 0u;
    uint32_t at = 0u;
    left->length = 0u;
    right->length = 0u;
    while (share->token[at] == NAME_SHARE_NODE && depth < NAME_SHARE_TOKENS_MAX) {
        if (share->token[at + 1u] == NAME_SHARE_NONE) {
            turns[depth++] = 1u;
            at += 2u;
            continue;
        }
        uint32_t right_half = name_share_skip(share, at + 1u);
        if (share->token[right_half] == NAME_SHARE_NONE) {
            turns[depth++] = 0u;
            at += 1u;
            continue;
        }
        break;
    }
    for (uint32_t level = 0u; level < depth; level++) {
        name_share_put(left, NAME_SHARE_NODE);
        name_share_put(right, NAME_SHARE_NODE);
        if (turns[level]) {
            name_share_put(left, NAME_SHARE_NONE);
            name_share_put(right, NAME_SHARE_NONE);
        }
    }
    switch (share->token[at]) {
    case NAME_SHARE_NONE:
        name_share_put(left, NAME_SHARE_NONE);
        name_share_put(right, NAME_SHARE_NONE);
        break;
    case NAME_SHARE_ALL:
        name_share_put(left, NAME_SHARE_NODE);
        name_share_put(left, NAME_SHARE_ALL);
        name_share_put(left, NAME_SHARE_NONE);
        name_share_put(right, NAME_SHARE_NODE);
        name_share_put(right, NAME_SHARE_NONE);
        name_share_put(right, NAME_SHARE_ALL);
        break;
    default: {
        uint32_t first = at + 1u;
        uint32_t second = name_share_skip(share, first);
        uint32_t end = name_share_skip(share, second);
        name_share_put(left, NAME_SHARE_NODE);
        for (uint32_t i = first; i < second; i++)
            name_share_put(left, share->token[i]);
        name_share_put(left, NAME_SHARE_NONE);
        name_share_put(right, NAME_SHARE_NODE);
        name_share_put(right, NAME_SHARE_NONE);
        for (uint32_t i = second; i < end; i++)
            name_share_put(right, share->token[i]);
        break;
    }
    }
    for (uint32_t level = depth; level-- > 0u;) {
        if (!turns[level]) {
            name_share_put(left, NAME_SHARE_NONE);
            name_share_put(right, NAME_SHARE_NONE);
        }
    }
}

static void name_share_copy_subtree(const NameShareCode *from, uint32_t *at,
                                    NameShareCode *out) {
    uint32_t end = name_share_skip(from, *at);
    for (uint32_t i = *at; i < end; i++)
        name_share_put(out, from->token[i]);
    *at = end;
}

/* The join of the subtrees at *A and *B (Shares.join), onto OUT, with a node
   of two empty or two full halves made a leaf (Shares.node'). */
static void name_share_join_at(const NameShareCode *a, uint32_t *at_a,
                               const NameShareCode *b, uint32_t *at_b,
                               NameShareCode *out) {
    uint8_t first = a->token[*at_a], second = b->token[*at_b];
    if (first == NAME_SHARE_NONE) {
        (*at_a)++;
        name_share_copy_subtree(b, at_b, out);
        return;
    }
    if (second == NAME_SHARE_NONE) {
        (*at_b)++;
        name_share_copy_subtree(a, at_a, out);
        return;
    }
    if (first == NAME_SHARE_ALL || second == NAME_SHARE_ALL) {
        *at_a = name_share_skip(a, *at_a);
        *at_b = name_share_skip(b, *at_b);
        name_share_put(out, NAME_SHARE_ALL);
        return;
    }
    uint32_t node = out->length;
    name_share_put(out, NAME_SHARE_NODE);
    (*at_a)++;
    (*at_b)++;
    name_share_join_at(a, at_a, b, at_b, out);
    name_share_join_at(a, at_a, b, at_b, out);
    if (out->length == node + 3u && node + 2u < NAME_SHARE_TOKENS_MAX &&
        out->token[node + 1u] == out->token[node + 2u] &&
        out->token[node + 1u] != NAME_SHARE_NODE) {
        out->token[node] = out->token[node + 1u];
        out->length = node + 1u;
    }
}

static void name_share_join(const NameShareCode *a, const NameShareCode *b,
                            NameShareCode *out) {
    uint32_t at_a = 0u, at_b = 0u;
    out->length = 0u;
    name_share_join_at(a, &at_a, b, &at_b, out);
}

/* The prefix code going down four halvings, most significant first: a
   node 1 for each, with an empty left half 00 before each right turn; and
   its length, 4 and 2 for each right turn. */
static const uint16_t k_name_code_down[16] = {
    0x00f, 0x03c, 0x039, 0x0e4, 0x033, 0x0cc, 0x0c9, 0x324,
    0x027, 0x09c, 0x099, 0x264, 0x093, 0x24c, 0x249, 0x924};
static const uint8_t k_name_code_down_bits[16] = {
    4, 6, 6, 8, 6, 8, 8, 10, 6, 8, 8, 10, 8, 10, 10, 12};

/* CODE with the code going down the LENGTH halvings of PATH (its low bits,
   most significant first, 1 a right turn) appended; *LEFT_TURNS gets how
   many of them turn left, each of which an empty right half 00 closes on
   the way up. */
static inline uint64_t name_code_down(uint64_t code, uint64_t path,
                                      unsigned length, unsigned *left_turns) {
    unsigned appended = 0u, rest = length;
    while (rest >= 4u) {
        rest -= 4u;
        unsigned nibble = (unsigned)(path >> rest) & 15u;
        code = (code << k_name_code_down_bits[nibble]) | k_name_code_down[nibble];
        appended += k_name_code_down_bits[nibble];
    }
    if (rest != 0u) {
        /* The last turns, as a nibble whose missing turns go left: each of
           those is a lone node 1 at the end, which is dropped. */
        unsigned missing = 4u - rest;
        unsigned nibble = ((unsigned)path << missing) & 15u;
        unsigned bits = k_name_code_down_bits[nibble] - missing;
        code = (code << bits) | (uint64_t)(k_name_code_down[nibble] >> missing);
        appended += bits;
    }
    *left_turns = length - (appended - length) / 2u;
    return code;
}

/* CODE with the prefix code of the interval at the end of the LENGTH
   halvings of PATH appended: down the path, the full half 01, and the
   empty right halves that close the left turns on the way up. */
static inline uint64_t name_code_interval(uint64_t code, uint64_t path,
                                          unsigned length) {
    unsigned left_turns;
    code = name_code_down(code, path, length, &left_turns);
    return ((code << 2) | 1u) << (2u * left_turns);
}

/* The union of the intervals A and B (Shares.join) as a word, without
   reading them into tokens: the larger when one lies in the other, and
   otherwise the prefix code of the path they share, the node where they
   part, and each one's path below it, 3 (depth A + depth B - shared) - 1
   bits.  False when that would not fit. */
static bool name_share_join_intervals(uint64_t a, uint64_t b, uint64_t *word) {
    unsigned da = 63u - (unsigned)__builtin_clzll(a);
    unsigned db = 63u - (unsigned)__builtin_clzll(b);
    if (da <= db && (b >> (db - da)) == a) {
        *word = a;
        return true;
    }
    if (db < da && (a >> (da - db)) == b) {
        *word = b;
        return true;
    }
    /* The paths, each behind a start bit at bit 62, part at the first bit
       they differ in. */
    unsigned shared = (unsigned)__builtin_clzll((a << (62u - da)) ^
                                                (b << (62u - db))) - 2u;
    if (3u * (da + db - shared) - 1u > NAME_SHARE_CODE_MAX)
        return false;
    bool a_left = !((a >> (da - 1u - shared)) & 1u);
    uint64_t left = a_left ? a : b, right = a_left ? b : a;
    unsigned below_left = (a_left ? da : db) - shared - 1u;
    unsigned below_right = (a_left ? db : da) - shared - 1u;
    unsigned prefix_left_turns;
    uint64_t code = name_code_down(1u, (a >> (da - shared)) & ((UINT64_C(1) << shared) - 1u),
                                   shared, &prefix_left_turns);
    code = (code << 1) | 1u;
    code = name_code_interval(code, left & ((UINT64_C(1) << below_left) - 1u), below_left);
    code = name_code_interval(code, right & ((UINT64_C(1) << below_right) - 1u), below_right);
    *word = CETTA_NAME_SHARE_CODED | (code << (2u * prefix_left_turns));
    return true;
}

/* The bits of a share's code below *AT, read downward: going down a path
   to its full half, 1 for each right turn; its length into *LENGTH.  False
   when the code does not go down one path there. */
static bool name_code_read_down(uint64_t code, unsigned *at, uint64_t *path,
                                unsigned *length) {
    uint64_t turns = 0u;
    unsigned n = 0u, i = *at;
    for (;;) {
        if (i < 2u)
            return false;
        if ((code >> (i - 1u)) & 1u) {
            /* A node: an empty left half 00 next means a right turn. */
            i -= 1u;
            bool right = i >= 2u && ((code >> (i - 2u)) & 3u) == 0u;
            if (right)
                i -= 2u;
            turns = (turns << 1) | (uint64_t)right;
            if (++n > NAME_SHARE_PATH_MAX)
                return false;
            continue;
        }
        if (((code >> (i - 2u)) & 3u) != 1u)
            return false;
        i -= 2u;
        *at = i;
        *path = turns;
        *length = n;
        return true;
    }
}

/* The empty halves 00 below *AT, read downward: their number. */
static unsigned name_code_read_empty(uint64_t code, unsigned *at) {
    unsigned n = 0u;
    while (*at >= 2u && ((code >> (*at - 2u)) & 3u) == 0u) {
        *at -= 2u;
        n++;
    }
    return n;
}

/* Whether the coded share CODED is the union of two intervals, as a join of
   two pieces makes (name_share_join_intervals), and which: its halves
   (Shares.split parts it at the node where the intervals part).  The code
   goes down the shared path, the node and the left path to the left full
   half, up the left path's left turns, down and up the right path, and up
   the shared path's left turns; the node is the shared path's next left
   turn, the one after as many as the closing empty halves the right path
   leaves over. */
static bool name_share_two_intervals(uint64_t coded, uint64_t *left,
                                     uint64_t *right) {
    uint64_t code = coded & ~CETTA_NAME_SHARE_CODED;
    if (code == 0u)
        return false;
    unsigned at = 63u - (unsigned)__builtin_clzll(code);
    uint64_t down, right_path;
    unsigned down_length, right_length;
    if (!name_code_read_down(code, &at, &down, &down_length))
        return false;
    unsigned left_closed = name_code_read_empty(code, &at);
    if (!name_code_read_down(code, &at, &right_path, &right_length))
        return false;
    unsigned closed = name_code_read_empty(code, &at);
    unsigned right_left_turns = right_length - (unsigned)__builtin_popcountll(right_path);
    if (at != 0u || closed < right_left_turns)
        return false;
    unsigned shared_left_turns = closed - right_left_turns;
    /* The node: the left turn of DOWN after SHARED_LEFT_TURNS of them. */
    unsigned shared = 0u, seen = 0u;
    for (;; shared++) {
        if (shared == down_length)
            return false;
        if (!((down >> (down_length - 1u - shared)) & 1u)) {
            if (seen == shared_left_turns)
                break;
            seen++;
        }
    }
    unsigned below = down_length - shared - 1u;
    uint64_t left_path = down & ((UINT64_C(1) << below) - 1u);
    if (below - (unsigned)__builtin_popcountll(left_path) != left_closed ||
        shared + 1u + below > NAME_SHARE_PATH_MAX ||
        shared + 1u + right_length > NAME_SHARE_PATH_MAX)
        return false;
    uint64_t stem = (UINT64_C(1) << shared) | (down >> (below + 1u));
    *left = (((stem << 1)) << below) | left_path;
    *right = (((stem << 1) | 1u) << right_length) | right_path;
    return true;
}

/* The halves of the share in WORD, as words (Shares.split): false when
   WORD is no share or a half would not fit. */
static bool name_share_halves(uint64_t word, uint64_t *left, uint64_t *right) {
    if (cetta_name_share_halves_in_word(word)) {
        *left = word << 1;
        *right = (word << 1) | 1u;
        return true;
    }
    if ((word & CETTA_NAME_SHARE_CODED) && name_share_two_intervals(word, left, right))
        return true;
    NameShareCode share, first, second;
    if (!name_share_read(word, &share))
        return false;
    name_share_split(&share, &first, &second);
    return name_share_write(&first, left) && name_share_write(&second, right);
}

/* ── Blocks ──────────────────────────────────────────────────────────── */

void cetta_name_block_release(CettaNameBlock *block) {
    if (block == &cetta_name_block_unplaced)
        return;
    if (atomic_fetch_sub_explicit(&block->references, 1u,
                                  memory_order_acq_rel) == 1u) {
        free(block);
        atomic_fetch_sub_explicit(&g_name_supply_live_blocks, 1u,
                                  memory_order_relaxed);
    }
}

uint64_t cetta_name_supply_live_blocks(void) {
    return atomic_load_explicit(&g_name_supply_live_blocks,
                                memory_order_relaxed);
}

/* A new block, holding one reference, keyed by the hash of MESSAGE. */
static __attribute__((noinline)) CettaNameBlock *name_block_make(
    const uint8_t *message, size_t length) {
    name_supply_require_configured();
    uint8_t digest[16];
    cetta_siphash24_128(g_name_supply_key, message, length, digest);
    CettaNameBlock *block = malloc(sizeof(*block));
    if (!block) {
        fputs("cetta: out of memory for a block of generated names\n", stderr);
        abort();
    }
    atomic_init(&block->references, 1u);
    block->key_hi = name_supply_load_be64(digest);
    block->key_lo = name_supply_load_be64(digest + 8);
    atomic_fetch_add_explicit(&g_name_supply_live_blocks, 1u,
                              memory_order_relaxed);
    return block;
}

/* A block's key, zero for the root block, into MESSAGE. */
static size_t name_block_put_key(uint8_t *message, const CettaNameBlock *block) {
    name_supply_store_be64(message, block ? block->key_hi : 0u);
    name_supply_store_be64(message + 8, block ? block->key_lo : 0u);
    return 16u;
}

/* What identifies a supply's place and time in a block's key: its block's
   key, its clocks, and its share: a 0 and the share's word when it has one
   (the word is the share's one form, so this is the share), otherwise a 1,
   the length in bits of the share's prefix code and the bits, most
   significant first. */
static size_t name_block_put_place(uint8_t *message, const CettaNameBlock *block,
                                   const uint64_t *drawn, uint64_t word,
                                   const NameShareCode *share) {
    size_t length = name_block_put_key(message, block);
    for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++) {
        siphash_store_le64(message + length, drawn[kind]);
        length += 8u;
    }
    if (!share) {
        message[length++] = 0u;
        siphash_store_le64(message + length, word);
        return length + 8u;
    }
    message[length++] = 1u;
    uint32_t bits = name_share_code_bits(share);
    message[length++] = (uint8_t)(bits & 0xffu);
    message[length++] = (uint8_t)(bits >> 8);
    uint8_t byte = 0u;
    unsigned filled = 0u;
    for (uint32_t i = 0u; i < share->length; i++) {
        uint8_t token = share->token[i];
        unsigned width = token == NAME_SHARE_NODE ? 1u : 2u;
        unsigned value = token == NAME_SHARE_NODE ? 1u : token;
        for (unsigned bit = width; bit-- > 0u;) {
            byte = (uint8_t)((byte << 1) | ((value >> bit) & 1u));
            if (++filled == 8u) {
                message[length++] = byte;
                byte = 0u;
                filled = 0u;
            }
        }
    }
    if (filled != 0u)
        message[length++] = (uint8_t)(byte << (8u - filled));
    return length;
}

/* The largest place: a key, the clocks, a tag, the length and the bits of
   a share of 256 tokens. */
enum { NAME_BLOCK_PLACE_MAX = 16 + 8 * CETTA_NAME_KIND_COUNT + 1 + 2 + 64 };

/* SUPPLY goes on in BLOCK, which it holds, with its whole interval at its
   clocks and nothing pushed; its old block is let go. */
static void name_supply_enter(CettaNameSupply *supply, CettaNameBlock *block) {
    CettaNameBlock *left = supply->block;
    supply->block = block;
    supply->share = CETTA_NAME_SHARE_WHOLE;
    supply->pushed = 0u;
    supply->pushes = 0u;
    if (left)
        cetta_name_block_release(left);
}

/* Move SUPPLY on to a block of its own for the share it holds, or for the
   share WIDE when that does not fit a word: the whole interval of a new
   block keyed by its place, at its clocks.  What is left of its pool is
   given up with the old block. */
static __attribute__((noinline)) void name_supply_move_on(
    CettaNameSupply *supply, const NameShareCode *wide) {
    uint8_t message[1 + NAME_BLOCK_PLACE_MAX];
    message[0] = 0x01u;
    size_t length = 1u + name_block_put_place(message + 1, supply->block,
                                              supply->drawn, supply->share,
                                              wide);
    name_supply_enter(supply, name_block_make(message, length));
}

/* Move PUSHER, whose pool is spent, on to a block of its own for its share
   and what is left of its pool: the whole interval of a new block keyed by
   its block's key, its clocks, its share's word, the word of its last push
   and the count, all little-endian but the key. */
static __attribute__((noinline)) void name_supply_move_on_spent(
    CettaNameSupply *pusher) {
    uint8_t message[1 + 16 + 8 * CETTA_NAME_KIND_COUNT + 24];
    message[0] = 0x04u;
    size_t length = 1u + name_block_put_key(message + 1, pusher->block);
    for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++) {
        siphash_store_le64(message + length, pusher->drawn[kind]);
        length += 8u;
    }
    siphash_store_le64(message + length, pusher->share);
    siphash_store_le64(message + length + 8u, pusher->pushed);
    siphash_store_le64(message + length + 16u, pusher->pushes);
    length += 24u;
    name_supply_enter(pusher, name_block_make(message, length));
}

/* Move SUPPLY into a fresh block outside the split tree, keyed by a number
   of the process's own, with its clocks back at zero: for a supply outside
   the tree, and for one whose clock of some kind has given every value.
   Its clocks are handed on first, as those of a supply that ends. */
static __attribute__((noinline)) void name_supply_move_out(
    CettaNameSupply *supply) {
    uint8_t message[9];
    message[0] = 0x03u;
    siphash_store_le64(message + 1,
                       atomic_fetch_add_explicit(
                           &g_name_supply_unplaced_blocks, 1u,
                           memory_order_relaxed));
    CettaNameBlock *block = name_block_make(message, sizeof(message));
    CettaNameBlock *left = supply->block;
    uint64_t *clocks = cetta_name_supply_ending_clocks;
    if (clocks && left != &cetta_name_block_unplaced)
        cetta_name_supply_absorb(clocks, supply);
    *supply = (CettaNameSupply){.block = block,
                                .share = CETTA_NAME_SHARE_WHOLE};
    if (left)
        cetta_name_block_release(left);
}

/* ── Supplies ────────────────────────────────────────────────────────── */

void cetta_name_supply_init_root(CettaNameSupply *supply) {
    if (supply)
        *supply = (CettaNameSupply){.share = CETTA_NAME_SHARE_WHOLE};
}

void cetta_name_supply_init_unplaced(CettaNameSupply *supply) {
    /* No share and spent clocks: the first draw or split moves on. */
    if (supply)
        *supply = (CettaNameSupply){
            .block = &cetta_name_block_unplaced, .share = 0u,
            .drawn = {UINT64_MAX, UINT64_MAX}};
}

CettaNameSupply *cetta_name_supply_install(CettaNameSupply *supply) {
    CettaNameSupply *previous = cetta_name_supply_installed;
    cetta_name_supply_installed = supply ? supply : &g_name_supply_process_root;
    return previous;
}

/* SipHash-2-4-128 under the seed of the kind, the share and the clock, and
   of the block's key beyond the root block: the root block's message is 17
   bytes and every other block's 33, so the two never meet. */
static __attribute__((noinline)) CettaGeneratedName
name_supply_hashed_name(CettaNameKind kind, const CettaNameBlock *block,
                        uint64_t share, uint64_t index) {
    uint8_t message[33];
    uint8_t digest[16];
    message[0] = (uint8_t)kind;
    siphash_store_le64(message + 1, share);
    siphash_store_le64(message + 9, index);
    if (!block) {
        cetta_siphash24_128(g_name_supply_key, message, 17u, digest);
    } else {
        name_supply_store_be64(message + 17, block->key_hi);
        name_supply_store_be64(message + 25, block->key_lo);
        cetta_siphash24_128(g_name_supply_key, message, 33u, digest);
    }
    return (CettaGeneratedName){name_supply_load_be64(digest),
                                name_supply_load_be64(digest + 8),
                                (uintptr_t)CETTA_NAME_FORM_HASHED};
}

CettaGeneratedName cetta_name_supply_fresh(CettaNameSupply *supply,
                                           CettaNameKind kind) {
    name_supply_require_configured();
    uint64_t index = supply->drawn[kind] + 1u;
    if (__builtin_expect(index == 0u || supply->share == 0u, 0)) {
        /* This kind has had every clock value, or the supply is outside the
           split tree: the names go on in a fresh block. */
        name_supply_move_out(supply);
        index = 1u;
    }
    supply->drawn[kind] = index;
    if (g_name_supply_mode == NAME_SUPPLY_MODE_COUNTER)
        return (CettaGeneratedName){
            supply->share, index,
            (uintptr_t)supply->block | (uintptr_t)CETTA_NAME_FORM_COUNTED};
    return name_supply_hashed_name(kind, supply->block, supply->share, index);
}

/* The halves of SUPPLY: LEFT keeps its pool, RIGHT has nothing pushed. */
static void name_supply_split_slow(CettaNameSupply *supply,
                                   CettaNameSupply *left,
                                   CettaNameSupply *right) {
    uint64_t first = 0u, second = 0u;
    if (supply->share == 0u) {
        /* Outside the split tree. */
        name_supply_move_out(supply);
    } else if (name_share_halves(supply->share, &first, &second)) {
        *left = *supply;
        *right = *supply;
        left->share = first;
        right->share = second;
        right->pushed = 0u;
        right->pushes = 0u;
        if (supply->block)
            cetta_name_block_retain(supply->block);
        return;
    } else {
        /* A half does not fit the word: the supply moves on with the share
           it has, and splits the whole interval of its new block. */
        name_supply_move_on(supply, NULL);
    }
    *left = *supply;
    *right = *supply;
    left->share = CETTA_NAME_SHARE_WHOLE << 1;
    right->share = (CETTA_NAME_SHARE_WHOLE << 1) | 1u;
    cetta_name_block_retain(supply->block);
}

void cetta_name_supply_fork_beyond(CettaNameSupply *parent,
                                   CettaNameSupply *child) {
    CettaNameSupply left;
    name_supply_split_slow(parent, &left, child);
    *parent = left;
}

/* PUSHER's first push, its share's halves being OWN, which it keeps to
   draw from, and POOL, an interval at most 61 halvings deep: POOL's left
   half (countedPath 0). */
static void name_supply_push_first(CettaNameSupply *pusher,
                                   CettaNameSupply *child, uint64_t own,
                                   uint64_t pool) {
    uint64_t word = pool << 1;
    pusher->share = own;
    pusher->pushed = word;
    pusher->pushes = 1u;
    *child = *pusher;
    child->share = word;
    child->pushed = 0u;
    child->pushes = 0u;
    if (child->block)
        cetta_name_block_retain(child->block);
}

void cetta_name_supply_push_beyond(CettaNameSupply *pusher,
                                   CettaNameSupply *child) {
    uint64_t own = 0u, pool = 0u;
    if (pusher->pushes != 0u) {
        /* The first push of the next block would not fit: the pool is
           spent. */
        name_supply_move_on_spent(pusher);
    } else if (pusher->share == 0u) {
        /* Outside the split tree. */
        name_supply_move_out(pusher);
    } else if (name_share_halves(pusher->share, &own, &pool) &&
               pool - 1u < (UINT64_C(1) << 62) - 1u) {
        /* A coded share whose right half is one interval with room for a
           halving below it. */
        name_supply_push_first(pusher, child, own, pool);
        return;
    } else {
        /* An interval too deep, or a coded share whose right half is no
           interval: the supply moves on with the share it has. */
        name_supply_move_on(pusher, NULL);
    }
    /* The whole interval of the supply's new block. */
    name_supply_push_first(pusher, child, CETTA_NAME_SHARE_WHOLE << 1,
                           (CETTA_NAME_SHARE_WHOLE << 1) | 1u);
}

/* The two places of a join across blocks, as one message: each block's
   key, clocks and share, in their order as bytes, so the join of two
   supplies is the join of them the other way round. */
static size_t name_join_message(uint8_t *message, const CettaNameSupply *a,
                                const CettaNameSupply *b) {
    uint8_t first[NAME_BLOCK_PLACE_MAX], second[NAME_BLOCK_PLACE_MAX];
    size_t first_length =
        name_block_put_place(first, a->block, a->drawn, a->share, NULL);
    size_t second_length =
        name_block_put_place(second, b->block, b->drawn, b->share, NULL);
    size_t shorter = first_length < second_length ? first_length : second_length;
    int order = memcmp(first, second, shorter);
    if (order == 0)
        order = first_length < second_length ? -1 : first_length > second_length;
    message[0] = 0x02u;
    const uint8_t *lower = order <= 0 ? first : second;
    const uint8_t *upper = order <= 0 ? second : first;
    size_t lower_length = order <= 0 ? first_length : second_length;
    size_t upper_length = order <= 0 ? second_length : first_length;
    memcpy(message + 1, lower, lower_length);
    memcpy(message + 1 + lower_length, upper, upper_length);
    return 1u + lower_length + upper_length;
}

/* The join of any two supplies, through their shares' prefix codes. */
static __attribute__((noinline)) CettaNameSupply name_supply_join_codes(
    const CettaNameSupply *first, const CettaNameSupply *second) {
    CettaNameSupply joined = *first;
    joined.pushed = 0u;
    joined.pushes = 0u;
    cetta_name_supply_absorb(joined.drawn, second);
    NameShareCode a, b, both;
    bool same_block = first->block == second->block;
    bool readable = first->share != 0u && second->share != 0u &&
                    first->block != &cetta_name_block_unplaced &&
                    second->block != &cetta_name_block_unplaced &&
                    (!same_block || (name_share_read(first->share, &a) &&
                                     name_share_read(second->share, &b)));
    if (!readable) {
        /* A supply outside the split tree has no share to join. */
        joined.block = &cetta_name_block_unplaced;
        joined.share = 0u;
        name_supply_move_out(&joined);
        return joined;
    }
    if (same_block) {
        name_share_join(&a, &b, &both);
        uint64_t word = 0u;
        if (both.length <= NAME_SHARE_TOKENS_MAX &&
            name_share_write(&both, &word)) {
            joined.share = word;
            if (joined.block)
                cetta_name_block_retain(joined.block);
            return joined;
        }
        /* The union does not fit the word: it moves on to a block of its
           own, keyed by the union's place.  The move lets go of a block the
           join does not hold, so take one first. */
        if (joined.block)
            cetta_name_block_retain(joined.block);
        name_supply_move_on(&joined, &both);
        return joined;
    }
    uint8_t message[1 + 2 * NAME_BLOCK_PLACE_MAX];
    size_t length = name_join_message(message, first, second);
    joined.block = name_block_make(message, length);
    joined.share = CETTA_NAME_SHARE_WHOLE;
    return joined;
}

CettaNameSupply cetta_name_supply_join_beyond(const CettaNameSupply *first,
                                              const CettaNameSupply *second) {
    uint64_t a = first->share, b = second->share, word;
    if (first->block == second->block &&
        first->block != &cetta_name_block_unplaced && a != 0u && b != 0u &&
        ((a | b) & CETTA_NAME_SHARE_CODED) == 0u &&
        name_share_join_intervals(a, b, &word)) {
        /* Two intervals of one block, as the continuation of a COMM between
           two pieces of a spawn joins. */
        CettaNameSupply joined = *first;
        joined.share = word;
        joined.pushed = 0u;
        joined.pushes = 0u;
        cetta_name_supply_absorb(joined.drawn, second);
        if (joined.block)
            cetta_name_block_retain(joined.block);
        return joined;
    }
    return name_supply_join_codes(first, second);
}
