#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_hash.h"

#include <pthread.h>
#include <string.h>

/* ── SHA-256 (FIPS 180-4) ─────────────────────────────────────────────── */

typedef struct {
    uint32_t h[8];
} Sha256;

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(Sha256 *s, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

/* The same compression with the SHA extensions (x86-64), when the
 * processor has them: chosen once, at the first digest. */
#if defined(__x86_64__) && defined(__GNUC__)
#include <cpuid.h>
#include <immintrin.h>
#include <stdatomic.h>

__attribute__((target("sha,sse4.1,ssse3"))) static void sha_block_ni(Sha256 *s, const uint8_t *p) {
    const __m128i mask = _mm_set_epi64x(0x0c0d0e0f08090a0bLL, 0x0405060700010203LL);
    __m128i tmp = _mm_loadu_si128((const __m128i *)&s->h[0]);
    __m128i state1 = _mm_loadu_si128((const __m128i *)&s->h[4]);
    tmp = _mm_shuffle_epi32(tmp, 0xB1);          /* CDAB */
    state1 = _mm_shuffle_epi32(state1, 0x1B);    /* EFGH */
    __m128i state0 = _mm_alignr_epi8(tmp, state1, 8); /* ABEF */
    state1 = _mm_blend_epi16(state1, tmp, 0xF0); /* CDGH */
    const __m128i abef = state0, cdgh = state1;
    __m128i m[4];
    for (int i = 0; i < 4; ++i)
        m[i] = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 16 * i)), mask);
    for (int i = 0; i < 16; ++i) {
        __m128i msg = _mm_add_epi32(m[i & 3], _mm_loadu_si128((const __m128i *)&K[4 * i]));
        state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
        msg = _mm_shuffle_epi32(msg, 0x0E);
        state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
        if (i < 12) {
            /* W[4i+16 .. 4i+19] from W[4i .. 4i+15] */
            __m128i t = _mm_sha256msg1_epu32(m[i & 3], m[(i + 1) & 3]);
            t = _mm_add_epi32(t, _mm_alignr_epi8(m[(i + 3) & 3], m[(i + 2) & 3], 4));
            m[i & 3] = _mm_sha256msg2_epu32(t, m[(i + 3) & 3]);
        }
    }
    state0 = _mm_add_epi32(state0, abef);
    state1 = _mm_add_epi32(state1, cdgh);
    tmp = _mm_shuffle_epi32(state0, 0x1B);       /* FEBA */
    state1 = _mm_shuffle_epi32(state1, 0xB1);    /* DCHG */
    state0 = _mm_blend_epi16(tmp, state1, 0xF0); /* DCBA */
    state1 = _mm_alignr_epi8(state1, tmp, 8);    /* ABEF */
    _mm_storeu_si128((__m128i *)&s->h[0], state0);
    _mm_storeu_si128((__m128i *)&s->h[4], state1);
}

static bool sha_extensions(void) {
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d) || !(c & bit_SSE4_1) || !(c & bit_SSSE3))
        return false;
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d))
        return false;
    return (b & bit_SHA) != 0;
}

static void sha_block_any(Sha256 *s, const uint8_t *p) {
    /* 0 not known yet, 1 software, 2 extensions; every thread finds the
     * same */
    static atomic_int which;
    int w = atomic_load_explicit(&which, memory_order_relaxed);
    if (w == 0) {
        w = sha_extensions() ? 2 : 1;
        atomic_store_explicit(&which, w, memory_order_relaxed);
    }
    if (w == 2)
        sha_block_ni(s, p);
    else
        sha_block(s, p);
}
#define SHA_BLOCK sha_block_any
#else
#define SHA_BLOCK sha_block
#endif

/* MG_SHA_SELFTEST: the extensions against the portable code. */
bool mg_sha256_selftest(void) {
#if defined(__x86_64__) && defined(__GNUC__)
    if (!sha_extensions())
        return true;
    uint8_t block[64];
    uint64_t x = 0x9e3779b97f4a7c15u;
    for (int round = 0; round < 4096; ++round) {
        for (int i = 0; i < 64; ++i) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            block[i] = (uint8_t)x;
        }
        Sha256 a, b;
        for (int i = 0; i < 8; ++i)
            a.h[i] = b.h[i] = (uint32_t)(x >> (i * 4)) * 2654435761u + (uint32_t)round;
        sha_block(&a, block);
        sha_block_ni(&b, block);
        if (memcmp(a.h, b.h, sizeof a.h) != 0)
            return false;
    }
#endif
    return true;
}

static void sha_init(Sha256 *s) {
    static const uint32_t init[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(s->h, init, sizeof init);
}

/* One-shot digest: whole blocks straight from the input, then the last
 * one or two blocks with the padding and the bit length. */
void mg_sha256(const void *data, size_t len, uint8_t out[32]) {
    Sha256 s;
    sha_init(&s);
    const uint8_t *p = data;
    size_t rest = len;
    for (; rest >= 64u; p += 64, rest -= 64u)
        SHA_BLOCK(&s, p);
    uint8_t tail[128];
    memset(tail, 0, sizeof tail);
    if (rest)
        memcpy(tail, p, rest);
    tail[rest] = 0x80u;
    size_t blocks = rest < 56u ? 1u : 2u;
    uint64_t bits = (uint64_t)len * 8u;
    for (int i = 0; i < 8; ++i)
        tail[blocks * 64u - 1u - (size_t)i] = (uint8_t)(bits >> (8 * i));
    SHA_BLOCK(&s, tail);
    if (blocks == 2u)
        SHA_BLOCK(&s, tail + 64);
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = (uint8_t)(s.h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s.h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s.h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s.h[i];
    }
}

/* ── hash.ml combinators ──────────────────────────────────────────────── */

static void be32(uint8_t out[4], uint32_t x) {
    out[0] = (uint8_t)(x >> 24);
    out[1] = (uint8_t)(x >> 16);
    out[2] = (uint8_t)(x >> 8);
    out[3] = (uint8_t)x;
}

static MgHash sha_int32(uint32_t x) {
    uint8_t data[8] = {0, 0, 0, 1};
    be32(data + 4, x);
    MgHash h;
    mg_sha256(data, sizeof data, h.b);
    return h;
}

static MgHash sha_pair(const MgHash *x, const MgHash *y) {
    uint8_t data[68] = {0, 0, 0, 7};
    memcpy(data + 4, x->b, 32);
    memcpy(data + 36, y->b, 32);
    MgHash h;
    mg_sha256(data, sizeof data, h.b);
    return h;
}

static MgHash sha_tag(const MgHash *x, uint32_t tag) {
    uint8_t data[40] = {0, 0, 0, 8};
    be32(data + 4, tag);
    memcpy(data + 8, x->b, 32);
    MgHash h;
    mg_sha256(data, sizeof data, h.b);
    return h;
}

/* hash.ml keeps the results of hashint32, hashpair and hashtag in tables
 * for the life of the process, so a subterm that many declarations share
 * is hashed once.  These tables do the same for each thread, keyed by the
 * exact inputs; a table that reaches its bound starts over, and a table
 * that cannot grow is simply not consulted. */

enum { MEMO_SMALL = 1024, MEMO_MAX_ENTRIES = 1u << 20 };

typedef struct {
    uint32_t *index;      /* entry number + 1; 0 is an empty slot */
    uint8_t *entries;     /* key then result, `size` bytes each */
    uint32_t cap;         /* index slots, a power of two */
    uint32_t len, room;   /* entries used, entries allocated */
} Memo;

typedef struct {
    Memo pair, tag;
    MgHash small[MEMO_SMALL];
    uint64_t small_have[MEMO_SMALL / 64];
} Memos;

static pthread_key_t g_memo_key;
static pthread_once_t g_memo_once = PTHREAD_ONCE_INIT;
static bool g_memo_key_ok;
static _Thread_local Memos *t_memos;
static _Thread_local bool t_memos_failed;

static void memos_free(void *arg) {
    Memos *m = arg;
    if (!m)
        return;
    mg_sys_free(m->pair.index);
    mg_sys_free(m->pair.entries);
    mg_sys_free(m->tag.index);
    mg_sys_free(m->tag.entries);
    mg_sys_free(m);
}

static void memo_key_init(void) { g_memo_key_ok = pthread_key_create(&g_memo_key, memos_free) == 0; }

static Memos *memos(void) {
    if (t_memos || t_memos_failed)
        return t_memos;
    pthread_once(&g_memo_once, memo_key_init);
    Memos *m = g_memo_key_ok ? mg_sys_alloc(sizeof *m) : NULL;
    if (m) {
        memset(m, 0, sizeof *m);
        if (pthread_setspecific(g_memo_key, m) != 0) {
            mg_sys_free(m);
            m = NULL;
        }
    }
    t_memos = m;
    t_memos_failed = m == NULL;
    return m;
}

static uint64_t memo_mix(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

/* The slot hash of a key: its first and last eight bytes, mixed. */
static uint64_t memo_hash(const uint8_t *key, size_t klen) {
    uint64_t a, b;
    memcpy(&a, key, 8);
    memcpy(&b, key + klen - 8u, 8);
    return memo_mix(a ^ memo_mix(b + klen));
}

static const uint8_t *memo_find(const Memo *m, const uint8_t *key, size_t klen, size_t size,
                                uint64_t h, uint32_t *slot) {
    uint32_t i = (uint32_t)h & (m->cap - 1u);
    for (uint32_t k; (k = m->index[i]) != 0u; i = (i + 1u) & (m->cap - 1u)) {
        const uint8_t *e = m->entries + (size_t)(k - 1u) * size;
        if (memcmp(e, key, klen) == 0)
            return e + klen;
    }
    *slot = i;
    return NULL;
}

static bool memo_rehash(Memo *m, uint32_t cap, size_t klen, size_t size) {
    uint32_t *index = mg_sys_alloc(sizeof(uint32_t) * cap);
    if (!index)
        return false;
    memset(index, 0, sizeof(uint32_t) * cap);
    for (uint32_t k = 0; k < m->len; ++k) {
        uint32_t i = (uint32_t)memo_hash(m->entries + (size_t)k * size, klen) & (cap - 1u);
        while (index[i])
            i = (i + 1u) & (cap - 1u);
        index[i] = k + 1u;
    }
    mg_sys_free(m->index);
    m->index = index;
    m->cap = cap;
    return true;
}

/* Room for one more entry; false when the table cannot take it. */
static bool memo_reserve(Memo *m, size_t klen, size_t size) {
    if (m->len >= MEMO_MAX_ENTRIES) {
        m->len = 0;
        memset(m->index, 0, sizeof(uint32_t) * m->cap);
    }
    if ((uint64_t)(m->len + 1u) * 2u > m->cap &&
        !memo_rehash(m, m->cap ? m->cap * 2u : 1024u, klen, size))
        return false;
    if (m->len == m->room) {
        uint32_t room = m->room ? m->room * 2u : 512u;
        uint8_t *grown = mg_sys_realloc(m->entries, (size_t)room * size);
        if (!grown)
            return false;
        m->entries = grown;
        m->room = room;
    }
    return true;
}

static void memo_add(Memo *m, const uint8_t *key, size_t klen, size_t size, const MgHash *r) {
    if (!memo_reserve(m, klen, size))
        return;
    uint32_t slot;
    if (memo_find(m, key, klen, size, memo_hash(key, klen), &slot))
        return;
    uint8_t *e = m->entries + (size_t)m->len * size;
    memcpy(e, key, klen);
    memcpy(e + klen, r->b, 32);
    m->index[slot] = ++m->len;
}

MgHash mg_hash_int32(uint32_t x) {
    Memos *m = x < MEMO_SMALL ? memos() : NULL;
    if (!m)
        return sha_int32(x);
    uint64_t bit = UINT64_C(1) << (x & 63u);
    if (!(m->small_have[x >> 6] & bit)) {
        m->small[x] = sha_int32(x);
        m->small_have[x >> 6] |= bit;
    }
    return m->small[x];
}

MgHash mg_hash_pair(const MgHash *x, const MgHash *y) {
    Memos *m = memos();
    if (!m)
        return sha_pair(x, y);
    uint8_t key[64];
    memcpy(key, x->b, 32);
    memcpy(key + 32, y->b, 32);
    uint32_t slot;
    const uint8_t *r = m->pair.cap ? memo_find(&m->pair, key, 64u, 96u, memo_hash(key, 64u), &slot)
                                   : NULL;
    MgHash h;
    if (r) {
        memcpy(h.b, r, 32);
        return h;
    }
    h = sha_pair(x, y);
    memo_add(&m->pair, key, 64u, 96u, &h);
    return h;
}

MgHash mg_hash_tag(const MgHash *x, uint32_t tag) {
    Memos *m = memos();
    if (!m)
        return sha_tag(x, tag);
    uint8_t key[36];
    memcpy(key, x->b, 32);
    memcpy(key + 32, &tag, 4);
    uint32_t slot;
    const uint8_t *r = m->tag.cap ? memo_find(&m->tag, key, 36u, 68u, memo_hash(key, 36u), &slot)
                                  : NULL;
    MgHash h;
    if (r) {
        memcpy(h.b, r, 32);
        return h;
    }
    h = sha_tag(x, tag);
    memo_add(&m->tag, key, 36u, 68u, &h);
    return h;
}

void mg_hash_hex(const MgHash *h, char out[65]) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        out[2 * i] = digits[h->b[i] >> 4];
        out[2 * i + 1] = digits[h->b[i] & 15u];
    }
    out[64] = '\0';
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool mg_hash_parse(const char *hex, size_t len, MgHash *out) {
    if (!hex || len != 64u)
        return false;
    for (int i = 0; i < 32; ++i) {
        int hi = hex_digit(hex[2 * i]), lo = hex_digit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out->b[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

/* ── hashtp: the serialization of mathdata.ml seo_tp ──────────────────── */


static void emit_byte(MgBits *w, uint8_t byte) {
    if (w->nomem)
        return;
    if (w->len == w->cap) {
        size_t cap = w->cap ? w->cap * 2u : 64u;
        uint8_t *grown = mg_sys_realloc(w->buf, cap);
        if (!grown) {
            w->nomem = true;
            return;
        }
        w->buf = grown;
        w->cap = cap;
    }
    w->buf[w->len++] = byte;
}

/* ser.ml seosb: `n` bits of `b`, least significant first. */
void mg_bits_put(MgBits *w, uint32_t b, int n) {
    while (n > 0) {
        if (w->bits + n >= 8) {
            int take = 8 - w->bits;
            emit_byte(w, (uint8_t)((w->partial | (b << w->bits)) & 255u));
            b >>= take;
            n -= take;
            w->partial = 0;
            w->bits = 0;
        } else {
            w->partial |= b << w->bits;
            w->bits += n;
            n = 0;
        }
    }
}

bool mg_bits_varintb(MgBits *w, uint32_t x) {
    if (x < 4u) {
        mg_bits_put(w, 0, 2);
        mg_bits_put(w, x, 2);
    } else if (x < 20u) {
        mg_bits_put(w, 1, 2);
        mg_bits_put(w, x - 4u, 4);
    } else if (x < 276u) {
        mg_bits_put(w, 2, 2);
        mg_bits_put(w, x - 20u, 8);
    } else if (x < 65536u) {
        mg_bits_put(w, 3, 2);
        mg_bits_put(w, x, 16);
    } else {
        return false; /* upstream: invalid base type / type variable */
    }
    return true;
}

bool mg_bits_digest(MgBits *w, MgHash *out) {
    if (w->bits > 0)
        emit_byte(w, (uint8_t)w->partial);
    bool ok = !w->nomem;
    if (ok)
        mg_sha256(w->buf, w->len, out->b);
    mg_sys_free(w->buf);
    *w = (MgBits){0};
    return ok;
}

bool mg_hash_tp(const MgTp *root, MgHash *out) {
    MgBits w = {0};
    const MgTp **stack = NULL;
    size_t len = 0, cap = 0;
    bool ok = true;
#define TPUSH(x) do { \
        if (len == cap) { \
            size_t nc = cap ? cap * 2u : 32u; \
            const MgTp **g = mg_sys_realloc(stack, sizeof(*g) * nc); \
            if (!g) { ok = false; break; } \
            stack = g; cap = nc; \
        } \
        stack[len++] = (x); \
    } while (0)
    TPUSH(root);
    while (ok && len) {
        const MgTp *t = stack[--len];
        switch (t->tag) {
        case MG_TP_ARR:
            mg_bits_put(&w, 0, 2);
            TPUSH(t->b);
            TPUSH(t->a);
            break;
        case MG_TP_PROP:
            mg_bits_put(&w, 1, 2);
            break;
        case MG_TP_BASE:
            mg_bits_put(&w, 2, 2);
            ok = mg_bits_varintb(&w, t->n);
            break;
        case MG_TP_VAR:
            mg_bits_put(&w, 3, 3);
            ok = mg_bits_varintb(&w, t->n);
            break;
        case MG_TP_ALL:
            mg_bits_put(&w, 7, 3);
            TPUSH(t->a);
            break;
        default:
            ok = false;
            break;
        }
    }
#undef TPUSH
    MgHash digest;
    ok = mg_bits_digest(&w, &digest) && ok;
    if (ok)
        *out = mg_hash_tag(&digest, 64u);
    mg_sys_free(stack);
    return ok;
}

/* ── hash roots of terms and proofs (iterative, memoized per call) ────── */

typedef struct {
    const void *node;
    MgHash hash;
} HashSlot;

typedef struct {
    HashSlot *slots;
    size_t cap, len;
} HashMemo;

static size_t hm_index(const void *node, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)node * UINT64_C(0x9e3779b97f4a7c15);
    h ^= h >> 31;
    return (size_t)h & (cap - 1u);
}

static const MgHash *hm_get(const HashMemo *m, const void *node) {
    if (!m->cap)
        return NULL;
    for (size_t i = hm_index(node, m->cap);; i = (i + 1u) & (m->cap - 1u)) {
        if (!m->slots[i].node)
            return NULL;
        if (m->slots[i].node == node)
            return &m->slots[i].hash;
    }
}

static bool hm_put(HashMemo *m, const void *node, const MgHash *h) {
    if ((m->len + 1u) * 2u > m->cap) {
        size_t cap = m->cap ? m->cap * 2u : 256u;
        HashSlot *slots = mg_sys_alloc(sizeof(HashSlot) * cap);
        if (!slots)
            return false;
        memset(slots, 0, sizeof(HashSlot) * cap);
        for (size_t j = 0; j < m->cap; ++j) {
            if (!m->slots[j].node)
                continue;
            size_t i = hm_index(m->slots[j].node, cap);
            while (slots[i].node)
                i = (i + 1u) & (cap - 1u);
            slots[i] = m->slots[j];
        }
        mg_sys_free(m->slots);
        m->slots = slots;
        m->cap = cap;
    }
    size_t i = hm_index(node, m->cap);
    while (m->slots[i].node)
        i = (i + 1u) & (m->cap - 1u);
    m->slots[i] = (HashSlot){node, *h};
    ++m->len;
    return true;
}

enum { H_TM = 0, H_PF = 1 };

typedef struct {
    const void *node;
    uint8_t kind;
    uint8_t stage;
} HFrame;

static bool name_hash(const MgStore *s, uint32_t id, MgHash *out) {
    const MgName *n = s ? mg_name_get(s, id) : NULL;
    return n && mg_hash_parse(n->text, n->len, out);
}

static uint32_t hchildren(const void *node, int kind, const void *child[2], int ckind[2]) {
    if (kind == H_TM) {
        const MgTm *m = node;
        switch (m->tag) {
        case MG_TM_AP:
        case MG_TM_IMP:
            child[0] = m->f; child[1] = m->x; ckind[0] = ckind[1] = H_TM; return 2;
        case MG_TM_LAM:
        case MG_TM_ALL:
        case MG_TM_TAP:
        case MG_TM_TLAM:
        case MG_TM_TALL:
            child[0] = m->f; ckind[0] = H_TM; return 1;
        default: return 0;
        }
    }
    const MgPf *p = node;
    switch (p->tag) {
    case MG_PF_TMAP: child[0] = p->p; child[1] = p->t; ckind[0] = H_PF; ckind[1] = H_TM; return 2;
    case MG_PF_PFAP: child[0] = p->p; child[1] = p->q; ckind[0] = ckind[1] = H_PF; return 2;
    case MG_PF_PLAM: child[0] = p->t; child[1] = p->p; ckind[0] = H_TM; ckind[1] = H_PF; return 2;
    case MG_PF_TLAM:
    case MG_PF_TPAP:
    case MG_PF_TPLAM:
        child[0] = p->p; ckind[0] = H_PF; return 1;
    default: return 0;
    }
}

static bool hcombine(const MgStore *s, const void *node, int kind, const MgHash *k0,
                     const MgHash *k1, MgHash *out) {
    MgHash x, t;
    if (kind == H_TM) {
        const MgTm *m = node;
        switch (m->tag) {
        case MG_TM_NAME: return name_hash(s, m->n, out);
        case MG_TM_PRIM: x = mg_hash_int32(m->n); *out = mg_hash_tag(&x, 96u); return true;
        case MG_TM_DB: x = mg_hash_int32(m->n); *out = mg_hash_tag(&x, 97u); return true;
        case MG_TM_AP: x = mg_hash_pair(k0, k1); *out = mg_hash_tag(&x, 98u); return true;
        case MG_TM_LAM:
            if (!mg_hash_tp(m->tp, &t)) return false;
            x = mg_hash_pair(&t, k0); *out = mg_hash_tag(&x, 99u); return true;
        case MG_TM_IMP: x = mg_hash_pair(k0, k1); *out = mg_hash_tag(&x, 100u); return true;
        case MG_TM_ALL:
            if (!mg_hash_tp(m->tp, &t)) return false;
            x = mg_hash_pair(&t, k0); *out = mg_hash_tag(&x, 101u); return true;
        case MG_TM_TAP:
            if (!mg_hash_tp(m->tp, &t)) return false;
            x = mg_hash_pair(k0, &t); *out = mg_hash_tag(&x, 102u); return true;
        case MG_TM_TLAM: *out = mg_hash_tag(k0, 103u); return true;
        case MG_TM_TALL: *out = mg_hash_tag(k0, 104u); return true;
        }
        return false;
    }
    const MgPf *p = node;
    switch (p->tag) {
    case MG_PF_GPA: return name_hash(s, p->n, out);
    case MG_PF_KNOWN:
        if (!name_hash(s, p->n, &x)) return false;
        *out = mg_hash_tag(&x, 128u); return true;
    case MG_PF_HYP: x = mg_hash_int32(p->n); *out = mg_hash_tag(&x, 129u); return true;
    case MG_PF_TMAP: x = mg_hash_pair(k0, k1); *out = mg_hash_tag(&x, 130u); return true;
    case MG_PF_PFAP: x = mg_hash_pair(k0, k1); *out = mg_hash_tag(&x, 131u); return true;
    case MG_PF_PLAM: x = mg_hash_pair(k0, k1); *out = mg_hash_tag(&x, 132u); return true;
    case MG_PF_TLAM:
        if (!mg_hash_tp(p->tp, &t)) return false;
        x = mg_hash_pair(&t, k0); *out = mg_hash_tag(&x, 133u); return true;
    case MG_PF_TPAP:
        if (!mg_hash_tp(p->tp, &t)) return false;
        x = mg_hash_pair(k0, &t); *out = mg_hash_tag(&x, 134u); return true;
    case MG_PF_TPLAM: *out = mg_hash_tag(k0, 135u); return true;
    }
    return false;
}

static bool hash_root(const MgStore *s, const void *root, int kind, MgHash *out) {
    HashMemo memo = {0};
    HFrame *stack = NULL;
    size_t len = 0, cap = 0;
    bool ok = true;
#define HPUSH(n, k) do { \
        if (len == cap) { \
            size_t nc = cap ? cap * 2u : 64u; \
            HFrame *g = mg_sys_realloc(stack, sizeof(*g) * nc); \
            if (!g) { ok = false; break; } \
            stack = g; cap = nc; \
        } \
        stack[len++] = (HFrame){(n), (uint8_t)(k), 0u}; \
    } while (0)
    HPUSH(root, kind);
    while (ok && len) {
        HFrame *f = &stack[len - 1u];
        if (hm_get(&memo, f->node)) {
            --len;
            continue;
        }
        const void *child[2] = {NULL, NULL};
        int ckind[2] = {0, 0};
        uint32_t n = hchildren(f->node, f->kind, child, ckind);
        if (f->stage == 0u && n) {
            f->stage = 1u;
            const void *node = f->node;
            (void)node;
            for (uint32_t k = n; k >= 1u; --k)
                if (!hm_get(&memo, child[k - 1u]))
                    HPUSH(child[k - 1u], ckind[k - 1u]);
            continue;
        }
        const MgHash *k0 = n >= 1u ? hm_get(&memo, child[0]) : NULL;
        const MgHash *k1 = n >= 2u ? hm_get(&memo, child[1]) : NULL;
        MgHash h, a0, a1;
        if (k0) a0 = *k0;
        if (k1) a1 = *k1;
        ok = hcombine(s, f->node, f->kind, k0 ? &a0 : NULL, k1 ? &a1 : NULL, &h) &&
             hm_put(&memo, f->node, &h);
        --len;
    }
#undef HPUSH
    const MgHash *r = ok ? hm_get(&memo, root) : NULL;
    if (r)
        *out = *r;
    mg_sys_free(stack);
    mg_sys_free(memo.slots);
    return r != NULL;
}

bool mg_hash_tm(const MgStore *s, const MgTm *m, MgHash *out) {
    return hash_root(s, m, H_TM, out);
}

bool mg_hash_pf(const MgStore *s, const MgPf *p, MgHash *out) {
    return hash_root(s, p, H_PF, out);
}

/* ── identities: syntax.ml tm_beta_eta_exeq_norm then hash ────────────── */

static const char HEX_EX[] = "912ad2cdc2d23bb8aa0a5070945f2a90976a948b0e8308917244591f3747f099";
static const char HEX_EQ[] = "5a6af35fb6d6bea477dd0f822b8e01ca0d57cc50dfd41744307bc94597fdaa4a";
static const char HEX_NEQ[] = "7966a66a9bb198103c2a540ccd5ebebdff33c10843cc10eebfc98715e142989c";
static const char HEX_NOT[] = "058f630dd89cad5a22daa56e097e3bdf85ce16ebd3dbf7994e404e2a98800f7f";

typedef enum { EXEQ_NONE = 0, EXEQ_EX, EXEQ_EQ, EXEQ_NEQ } ExeqKind;

static ExeqKind exeq_kind(const MgStore *s, const MgTm *m) {
    if (m->tag != MG_TM_TAP || m->f->tag != MG_TM_NAME)
        return EXEQ_NONE;
    const MgName *n = mg_name_get(s, m->f->n);
    if (!n || n->len != 64u)
        return EXEQ_NONE;
    if (memcmp(n->text, HEX_EX, 64) == 0) return EXEQ_EX;
    if (memcmp(n->text, HEX_EQ, 64) == 0) return EXEQ_EQ;
    if (memcmp(n->text, HEX_NEQ, 64) == 0) return EXEQ_NEQ;
    return EXEQ_NONE;
}

/* The inlined instance (syntax.ml tm_beta_eta_exeq_norm_1). */
static MgTm *exeq_instance(MgStore *s, ExeqKind k, MgTp *a, MgProfileTag profile) {
    MgTp *prop = mg_tp_prop(s);
    MgTm *d0 = mg_tm_db(s, 0), *d1 = mg_tm_db(s, 1), *d2 = mg_tm_db(s, 2);
    if (k == EXEQ_EX) {
        /* Lam(a->Prop, All(Prop, Imp(All(a, Imp(Ap(DB2,DB0),DB1)), DB0))) */
        MgTm *inner = mg_tm_all(s, a, mg_tm_imp(s, mg_tm_ap(s, d2, d0), d1));
        return mg_tm_lam(s, mg_tp_arr(s, a, prop),
                         mg_tm_all(s, prop, mg_tm_imp(s, inner, d0)));
    }
    /* All(a->a->Prop, Imp(Ap(Ap(DB0,DB2),DB1), Ap(Ap(DB0,DB1),DB2))) */
    MgTm *leibniz = mg_tm_all(s, mg_tp_arr(s, a, mg_tp_arr(s, a, prop)),
                              mg_tm_imp(s, mg_tm_ap(s, mg_tm_ap(s, d0, d2), d1),
                                        mg_tm_ap(s, mg_tm_ap(s, d0, d1), d2)));
    MgTm *body = leibniz;
    if (k == EXEQ_NEQ) {
        if (profile == MG_PROFILE_HF) {
            uint32_t not_name = mg_name_intern(s, HEX_NOT, 64u);
            body = not_name ? mg_tm_ap(s, mg_tm_name(s, not_name), leibniz) : NULL;
        } else {
            body = mg_tm_imp(s, leibniz, mg_tm_all(s, prop, d0));
        }
    }
    return body ? mg_tm_lam(s, a, mg_tm_lam(s, a, body)) : NULL;
}

/* Rebuild `root` with every ex/eq/neq instance inlined (iterative). */
typedef struct {
    const MgTm *node;
    MgTm *result;
} RwSlot;

static MgTm *exeq_expand(MgCheck *c, MgTm *root) {
    MgStore *s = &c->env.theory->store;
    MgProfileTag profile = c->env.theory->profile;
    if (!(root->facts & MG_TM_HAS_TYPEREG))
        return root;
    HashMemo dummy = {0};
    (void)dummy;
    RwSlot *slots = NULL;
    size_t cap = 0, used = 0;
    typedef struct {
        MgTm *node;
        uint8_t stage;
    } RFrame;
    RFrame *stack = NULL;
    size_t len = 0, scap = 0;
    MgTm *out = NULL;
    bool ok = true;
#define RGET(n, res) do { \
        res = NULL; \
        if (cap) for (size_t i_ = hm_index((n), cap);; i_ = (i_ + 1u) & (cap - 1u)) { \
            if (!slots[i_].node) break; \
            if (slots[i_].node == (n)) { res = slots[i_].result; break; } \
        } \
    } while (0)
#define RPUT(n, r) do { \
        if ((used + 1u) * 2u > cap) { \
            size_t nc = cap ? cap * 2u : 256u; \
            RwSlot *g = mg_sys_alloc(sizeof(RwSlot) * nc); \
            if (!g) { ok = false; break; } \
            memset(g, 0, sizeof(RwSlot) * nc); \
            for (size_t j_ = 0; j_ < cap; ++j_) if (slots[j_].node) { \
                size_t i_ = hm_index(slots[j_].node, nc); \
                while (g[i_].node) i_ = (i_ + 1u) & (nc - 1u); \
                g[i_] = slots[j_]; } \
            mg_sys_free(slots); slots = g; cap = nc; \
        } \
        size_t i_ = hm_index((n), cap); \
        while (slots[i_].node) i_ = (i_ + 1u) & (cap - 1u); \
        slots[i_] = (RwSlot){(n), (r)}; ++used; \
    } while (0)
#define RPUSH(n) do { \
        if (len == scap) { \
            size_t nc = scap ? scap * 2u : 64u; \
            RFrame *g = mg_sys_realloc(stack, sizeof(*g) * nc); \
            if (!g) { ok = false; break; } \
            stack = g; scap = nc; \
        } \
        stack[len++] = (RFrame){(n), 0u}; \
    } while (0)
    RPUSH(root);
    while (ok && len) {
        RFrame *f = &stack[len - 1u];
        MgTm *m = f->node, *hit;
        RGET(m, hit);
        if (hit) {
            --len;
            continue;
        }
        if (!(m->facts & MG_TM_HAS_TYPEREG)) {
            RPUT(m, m);
            --len;
            continue;
        }
        ExeqKind k = exeq_kind(s, m);
        if (k != EXEQ_NONE) {
            MgTm *r = exeq_instance(s, k, m->tp, profile);
            if (!r) {
                ok = false;
                break;
            }
            RPUT(m, r);
            --len;
            continue;
        }
        MgTm *kids[2] = {m->f, m->x};
        uint32_t n = (m->tag == MG_TM_AP || m->tag == MG_TM_IMP) ? 2u : 1u;
        if (f->stage == 0u) {
            f->stage = 1u;
            for (uint32_t i = n; i >= 1u; --i) {
                MgTm *already;
                RGET(kids[i - 1u], already);
                if (!already)
                    RPUSH(kids[i - 1u]);
            }
            continue;
        }
        MgTm *a, *b = NULL;
        RGET(kids[0], a);
        if (n == 2u)
            RGET(kids[1], b);
        MgTm *r = NULL;
        switch (m->tag) {
        case MG_TM_AP: r = mg_tm_ap(s, a, b); break;
        case MG_TM_IMP: r = mg_tm_imp(s, a, b); break;
        case MG_TM_LAM: r = mg_tm_lam(s, m->tp, a); break;
        case MG_TM_ALL: r = mg_tm_all(s, m->tp, a); break;
        case MG_TM_TAP: r = mg_tm_tap(s, a, m->tp); break;
        case MG_TM_TLAM: r = mg_tm_tlam(s, a); break;
        case MG_TM_TALL: r = mg_tm_tall(s, a); break;
        default: r = m; break;
        }
        if (!r) {
            ok = false;
            break;
        }
        RPUT(m, r);
        --len;
    }
    if (ok)
        RGET(root, out);
#undef RGET
#undef RPUT
#undef RPUSH
    mg_sys_free(slots);
    mg_sys_free(stack);
    if (!out)
        mg_fail(c, MG_NOMEM, "identity expansion");
    return out;
}

MgStatus mg_hash_identity(MgCheck *c, MgTm *term, uint32_t levels, bool lam,
                          MgHash *out) {
    MgStore *s = &c->env.theory->store;
    MgTm *inner = term;
    for (uint32_t i = 0; i < levels; ++i) {
        if (inner->tag != (lam ? MG_TM_TLAM : MG_TM_TALL))
            return MG_MALFORMED;
        inner = inner->f;
    }
    MgTm *expanded = exeq_expand(c, inner);
    MgTm *normal = expanded ? mg_tm_nf(c, expanded) : NULL;
    if (!normal)
        return c->status == MG_OK ? MG_NOMEM : c->status;
    for (uint32_t i = 0; normal && i < levels; ++i)
        normal = lam ? mg_tm_tlam(s, normal) : mg_tm_tall(s, normal);
    if (!normal)
        return MG_NOMEM;
    return mg_hash_tm(s, normal, out) ? MG_OK : MG_MALFORMED;
}

bool mg_hash_names_definition(MgCheck *c, uint32_t name, MgTp *type, MgTm *body) {
    const MgName *n = mg_name_get(&c->env.theory->store, name);
    MgHash want, got;
    if (!n || !mg_hash_parse(n->text, n->len, &want))
        return false;
    uint32_t levels = 0;
    for (const MgTp *t = type; t && t->tag == MG_TP_ALL; t = t->a)
        ++levels;
    return mg_hash_identity(c, body, levels, true, &got) == MG_OK &&
           memcmp(want.b, got.b, 32) == 0;
}
