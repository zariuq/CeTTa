#ifndef CETTA_MEGALODON_PFG_H
#define CETTA_MEGALODON_PFG_H

/*
 * Proofgold identities of Megalodon terms (syntax.ml pfg_propid,
 * pfg_objid, pfg_objid_pure_to_thy over mathdatapfg.ml).
 *
 * A front-end term (mg_front.h) of a monomorphic declaration, after
 * Megalodon's beta-eta normalization, is translated into Proofgold's term
 * language for the theory: a theory's primitives become Proofgold
 * primitives, other identities the pure roots of their Proofgold objects,
 * and the polymorphic ex, eq and neq become Proofgold's own quantifier and
 * equality.  The translation fails where Megalodon's does (polymorphism,
 * an identity without a Proofgold object, a primitive the theory lacks),
 * with Megalodon's message.  The Proofgold term is then normalized by
 * mathdatapfg.ml beta_eta_norm, bounded at 20 passes as Megalodon calls
 * it, and hashed with Proofgold's type serialization.
 *
 *   propid  = hashtag (hashpair thyid root) 33
 *   objid   = hashtag (hashpair thyid (hashpair root (hashtp a))) 32
 */

#include "mg_core.h"
#include "mg_front.h"
#include "mg_hash.h"

typedef enum {
    MG_PFG_OK = 0,
    MG_PFG_FAILURE,    /* Megalodon's Failure; the message says why */
    MG_PFG_REDEXMAX,   /* mathdatapfg.ml raises RedexMax */
    MG_PFG_NOMEM,
} MgPfgStatus;

/* Megalodon's pfgtmhh: the pure root of the Proofgold object of an
 * identity that is not a theory primitive. */
typedef bool (*MgPfgPure)(void *ctx, uint32_t name, MgHash *pure);

typedef struct {
    MgProfileTag theory;
    MgPfgPure pure;
    void *ctx;
    MgBuf *message;    /* receives the Failure text */
} MgPfgEnv;

/* pfg_propid2 of a closed beta-eta normal proposition: its pure root and
 * its proposition identity in the theory. */
MgPfgStatus mg_pfg_propid(const MgPfgEnv *env, const MgStore *s, const MgTm *normal,
                          MgHash *pure, MgHash *propid);
/* pfg_objid of a closed beta-eta normal term of type `tp`. */
MgPfgStatus mg_pfg_objid(const MgPfgEnv *env, const MgStore *s, const MgTm *normal,
                         const MgTp *tp, MgHash *pure, MgHash *objid);
/* pfg_objid_pure_to_thy: the object identity of a pure root at type `tp`. */
MgPfgStatus mg_pfg_objid_of_pure(const MgPfgEnv *env, const MgHash *pure, const MgTp *tp,
                                 MgHash *objid);

#endif
