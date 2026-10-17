#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_hash.h"

#include <string.h>

/* ── SHA-256 (FIPS 180-4) ─────────────────────────────────────────────── */

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
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

static void sha_init(Sha256 *s) {
    static const uint32_t init[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(s->h, init, sizeof init);
    s->len = 0;
    s->n = 0;
}

static void sha_update(Sha256 *s, const void *data, size_t n) {
    const uint8_t *p = data;
    s->len += n;
    while (n) {
        size_t k = 64u - s->n;
        if (k > n)
            k = n;
        memcpy(s->buf + s->n, p, k);
        s->n += k;
        p += k;
        n -= k;
        if (s->n == 64u) {
            sha_block(s, s->buf);
            s->n = 0;
        }
    }
}

static void sha_final(Sha256 *s, uint8_t out[32]) {
    uint64_t bits = s->len * 8u;
    uint8_t one = 0x80u, zero = 0u;
    sha_update(s, &one, 1);
    while (s->n != 56u)
        sha_update(s, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; ++i)
        len[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_update(s, len, 8);
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void mg_sha256(const void *data, size_t len, uint8_t out[32]) {
    Sha256 s;
    sha_init(&s);
    sha_update(&s, data, len);
    sha_final(&s, out);
}

/* ── hash.ml combinators ──────────────────────────────────────────────── */

static void be32(uint8_t out[4], uint32_t x) {
    out[0] = (uint8_t)(x >> 24);
    out[1] = (uint8_t)(x >> 16);
    out[2] = (uint8_t)(x >> 8);
    out[3] = (uint8_t)x;
}

static MgHash hashint32(uint32_t x) {
    uint8_t data[8] = {0, 0, 0, 1};
    be32(data + 4, x);
    MgHash h;
    mg_sha256(data, sizeof data, h.b);
    return h;
}

static MgHash hashpair(const MgHash *x, const MgHash *y) {
    uint8_t data[68] = {0, 0, 0, 7};
    memcpy(data + 4, x->b, 32);
    memcpy(data + 36, y->b, 32);
    MgHash h;
    mg_sha256(data, sizeof data, h.b);
    return h;
}

static MgHash hashtag(const MgHash *x, uint32_t tag) {
    uint8_t data[40] = {0, 0, 0, 8};
    be32(data + 4, tag);
    memcpy(data + 8, x->b, 32);
    MgHash h;
    mg_sha256(data, sizeof data, h.b);
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

typedef struct {
    uint8_t *buf;
    size_t len, cap;
    uint32_t partial;
    int bits;
    bool nomem;
} BitWriter;

static void emit_byte(BitWriter *w, uint8_t byte) {
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
static void put_bits(BitWriter *w, uint32_t b, int n) {
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

static bool put_varintb(BitWriter *w, uint32_t x) {
    if (x < 4u) {
        put_bits(w, 0, 2);
        put_bits(w, x, 2);
    } else if (x < 20u) {
        put_bits(w, 1, 2);
        put_bits(w, x - 4u, 4);
    } else if (x < 276u) {
        put_bits(w, 2, 2);
        put_bits(w, x - 20u, 8);
    } else if (x < 65536u) {
        put_bits(w, 3, 2);
        put_bits(w, x, 16);
    } else {
        return false; /* upstream: invalid base type / type variable */
    }
    return true;
}

bool mg_hash_tp(const MgTp *root, MgHash *out) {
    BitWriter w = {0};
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
            put_bits(&w, 0, 2);
            TPUSH(t->b);
            TPUSH(t->a);
            break;
        case MG_TP_PROP:
            put_bits(&w, 1, 2);
            break;
        case MG_TP_BASE:
            put_bits(&w, 2, 2);
            ok = put_varintb(&w, t->n);
            break;
        case MG_TP_VAR:
            put_bits(&w, 3, 3);
            ok = put_varintb(&w, t->n);
            break;
        case MG_TP_ALL:
            put_bits(&w, 7, 3);
            TPUSH(t->a);
            break;
        default:
            ok = false;
            break;
        }
    }
#undef TPUSH
    if (ok && w.bits > 0)
        emit_byte(&w, (uint8_t)w.partial);
    ok = ok && !w.nomem;
    if (ok) {
        MgHash digest;
        mg_sha256(w.buf, w.len, digest.b);
        *out = hashtag(&digest, 64u);
    }
    mg_sys_free(w.buf);
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
        case MG_TM_PRIM: x = hashint32(m->n); *out = hashtag(&x, 96u); return true;
        case MG_TM_DB: x = hashint32(m->n); *out = hashtag(&x, 97u); return true;
        case MG_TM_AP: x = hashpair(k0, k1); *out = hashtag(&x, 98u); return true;
        case MG_TM_LAM:
            if (!mg_hash_tp(m->tp, &t)) return false;
            x = hashpair(&t, k0); *out = hashtag(&x, 99u); return true;
        case MG_TM_IMP: x = hashpair(k0, k1); *out = hashtag(&x, 100u); return true;
        case MG_TM_ALL:
            if (!mg_hash_tp(m->tp, &t)) return false;
            x = hashpair(&t, k0); *out = hashtag(&x, 101u); return true;
        case MG_TM_TAP:
            if (!mg_hash_tp(m->tp, &t)) return false;
            x = hashpair(k0, &t); *out = hashtag(&x, 102u); return true;
        case MG_TM_TLAM: *out = hashtag(k0, 103u); return true;
        case MG_TM_TALL: *out = hashtag(k0, 104u); return true;
        }
        return false;
    }
    const MgPf *p = node;
    switch (p->tag) {
    case MG_PF_GPA: return name_hash(s, p->n, out);
    case MG_PF_KNOWN:
        if (!name_hash(s, p->n, &x)) return false;
        *out = hashtag(&x, 128u); return true;
    case MG_PF_HYP: x = hashint32(p->n); *out = hashtag(&x, 129u); return true;
    case MG_PF_TMAP: x = hashpair(k0, k1); *out = hashtag(&x, 130u); return true;
    case MG_PF_PFAP: x = hashpair(k0, k1); *out = hashtag(&x, 131u); return true;
    case MG_PF_PLAM: x = hashpair(k0, k1); *out = hashtag(&x, 132u); return true;
    case MG_PF_TLAM:
        if (!mg_hash_tp(p->tp, &t)) return false;
        x = hashpair(&t, k0); *out = hashtag(&x, 133u); return true;
    case MG_PF_TPAP:
        if (!mg_hash_tp(p->tp, &t)) return false;
        x = hashpair(k0, &t); *out = hashtag(&x, 134u); return true;
    case MG_PF_TPLAM: *out = hashtag(k0, 135u); return true;
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
