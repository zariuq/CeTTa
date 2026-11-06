#ifndef CETTA_MEGALODON_HASH_H
#define CETTA_MEGALODON_HASH_H

/*
 * Megalodon content identities (upstream hash.ml, mathdata.ml tm_hashroot /
 * pf_hashroot / hashtp, syntax.ml tm_id / ptm_lam_id / ptm_all_id).
 *
 *   hashint32 x  = SHA256(00000001 || be32 x)
 *   hashpair x y = SHA256(00000007 || x || y)
 *   hashtag x t  = SHA256(00000008 || be32 t || x)
 *   hashtp a     = hashtag (SHA256 (tp_to_str a)) 64, tp_to_str the
 *                  bit-packed (least significant bit first) serialization
 *                  of mathdata.ml seo_tp
 *
 * An identity is the hash root of the front end's normal form: the
 * polymorphic `ex`, `eq` and `neq` of the Egal-family preambles are
 * inlined at their type instances (syntax.ml tm_beta_eta_exeq_norm), then
 * the term is beta-eta normalized; a polymorphic declaration's type
 * binders are added afterwards.  A name in a hashed term must be a 64-digit
 * hexadecimal hash (a content reference); other names have no identity.
 *
 * Identities name objects; they never authorize anything by themselves.
 */

#include "mg_core.h"

typedef struct {
    uint8_t b[32];
} MgHash;

void mg_sha256(const void *data, size_t len, uint8_t out[32]);

/* hash.ml's combinators. */
MgHash mg_hash_int32(uint32_t x);
MgHash mg_hash_pair(const MgHash *x, const MgHash *y);
MgHash mg_hash_tag(const MgHash *x, uint32_t tag);

/* ser.ml's bit-packed output (seosb): `n` bits of `b`, least significant
 * first.  Zero-initialize, write, then take the digest, which releases
 * the buffer. */
typedef struct {
    uint8_t *buf;
    size_t len, cap;
    uint32_t partial;
    int bits;
    bool nomem;
} MgBits;
void mg_bits_put(MgBits *w, uint32_t b, int n);
/* ser.ml seo_varintb; false above 65535 (upstream refuses those). */
bool mg_bits_varintb(MgBits *w, uint32_t x);
/* SHA-256 of the bits written, the final partial byte included; false
 * when memory ran out.  The buffer is released either way. */
bool mg_bits_digest(MgBits *w, MgHash *out);

void mg_hash_hex(const MgHash *h, char out[65]);
bool mg_hash_parse(const char *hex, size_t len, MgHash *out);

bool mg_hash_tp(const MgTp *t, MgHash *out);
/* False when a name is not a hexadecimal hash or memory ran out. */
bool mg_hash_tm(const MgStore *s, const MgTm *m, MgHash *out);
bool mg_hash_pf(const MgStore *s, const MgPf *p, MgHash *out);

/* syntax.ml tm_id / ptm_lam_id (lam) or ptm_all_id (!lam) of a kernel
 * declaration body or proposition carrying `levels` type binders
 * (TTpLam^levels / TTpAll^levels around the front-end term). */
MgStatus mg_hash_identity(MgCheck *c, MgTm *term, uint32_t levels, bool lam,
                          MgHash *out);

/* Whether `name` is the Megalodon identity of the definition body `body`
 * (a content-addressed name). */
bool mg_hash_names_definition(MgCheck *c, uint32_t name, MgTp *type, MgTm *body);

/* The SHA extensions' compression equals the portable one on random
 * blocks (true where the processor has none). */
bool mg_sha256_selftest(void);

#endif /* CETTA_MEGALODON_HASH_H */
