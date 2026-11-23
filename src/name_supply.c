/*
 * name_supply.c -- the splittable supply of generated names (name_supply.h).
 *
 * A thread draws from the supply installed for it.  The main thread, and a
 * thread that installs none, draws from the process's root; an embedding
 * engine installs its own root around each entry; a parallel worker installs
 * the supply its task was given when it was pushed (parallel_executor.c), and
 * a supply of its own between tasks.  Drawing touches only the drawing
 * thread's supply: no shared counter and no atomic operation on the path.
 * Moving on to a new block, once per filled word, allocates the block and
 * hashes its key; nothing else here allocates.
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

static CettaNameSupply g_name_supply_process_root = {.path = 1u};
/* The block of a supply outside the split tree: never freed, and never
   drawn in (a supply in it moves on before its first draw or split). */
static CettaNameBlock g_name_block_unplaced = {.references = 1u};
/* The blocks made outside the split tree, numbered so that each is new. */
static _Atomic uint64_t g_name_supply_unplaced_blocks;
static _Atomic uint64_t g_name_supply_live_blocks;
_Thread_local CettaNameSupply *cetta_name_supply_installed
    CETTA_NAME_SUPPLY_INTERNAL = &g_name_supply_process_root;
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

/* ── Blocks ──────────────────────────────────────────────────────────── */

void cetta_name_block_release(CettaNameBlock *block) {
    if (block == &g_name_block_unplaced)
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

/* Move SUPPLY on to its successor: a new block, whose key hashes the key of
   the block it leaves and its path there (one supply outside the split tree
   hashes a number of its own instead), with an empty path and new
   counters. */
static __attribute__((noinline)) void name_supply_advance(
    CettaNameSupply *supply) {
    name_supply_require_configured();
    CettaNameBlock *left = supply->block;
    uint8_t message[25];
    size_t length;
    if (left == &g_name_block_unplaced) {
        message[0] = 0x03u;
        siphash_store_le64(message + 1,
                           atomic_fetch_add_explicit(
                               &g_name_supply_unplaced_blocks, 1u,
                               memory_order_relaxed));
        length = 9u;
    } else {
        message[0] = 0x01u;
        name_supply_store_be64(message + 1, left ? left->key_hi : 0u);
        name_supply_store_be64(message + 9, left ? left->key_lo : 0u);
        siphash_store_le64(message + 17, supply->path);
        length = 25u;
    }
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
    *supply = (CettaNameSupply){.block = block, .path = 1u};
    if (left)
        cetta_name_block_release(left);
}

/* ── Supplies ────────────────────────────────────────────────────────── */

void cetta_name_supply_init_root(CettaNameSupply *supply) {
    if (supply)
        *supply = (CettaNameSupply){.path = 1u};
}

void cetta_name_supply_init_unplaced(CettaNameSupply *supply) {
    /* A full path and spent counters: the first draw or split moves on. */
    if (supply)
        *supply = (CettaNameSupply){
            .block = &g_name_block_unplaced, .path = UINT64_MAX,
            .drawn = {UINT64_MAX, UINT64_MAX}};
}

CettaNameSupply *cetta_name_supply_install(CettaNameSupply *supply) {
    CettaNameSupply *previous = cetta_name_supply_installed;
    cetta_name_supply_installed = supply ? supply : &g_name_supply_process_root;
    return previous;
}

/* SipHash-2-4-128 under the seed of the kind, the path and the counter,
   and of the block's key beyond the root block: the root block's message
   is 17 bytes and every other block's 33, so the two never meet. */
static __attribute__((noinline)) CettaGeneratedName
name_supply_hashed_name(CettaNameKind kind, const CettaNameBlock *block,
                        uint64_t path, uint64_t index) {
    uint8_t message[33];
    uint8_t digest[16];
    message[0] = (uint8_t)kind;
    siphash_store_le64(message + 1, path);
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
    if (__builtin_expect(index == 0u, 0)) {
        /* This kind has had every counter value of the block, or the supply
           is outside the split tree: the names go on in a new block. */
        name_supply_advance(supply);
        index = 1u;
    }
    supply->drawn[kind] = index;
    if (g_name_supply_mode == NAME_SUPPLY_MODE_COUNTER)
        return (CettaGeneratedName){
            supply->path, index,
            (uintptr_t)supply->block | (uintptr_t)CETTA_NAME_FORM_COUNTED};
    return name_supply_hashed_name(kind, supply->block, supply->path, index);
}

CettaNameSupplyChild cetta_name_supply_split_beyond(CettaNameSupply *parent) {
    /* The child's code does not fit behind PARENT's path, or PARENT has
       numbered every child one word can: in a new block the first child is
       one bit. */
    name_supply_advance(parent);
    return cetta_name_supply_split(parent);
}
