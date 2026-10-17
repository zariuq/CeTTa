#ifndef CETTA_SEAL_H
#define CETTA_SEAL_H

/*
 * Shared opaque authority (lib_seal).
 *
 * Reference contract: docs/seal.md and the Lean
 * model Mettapedia/Languages/MeTTa/Seal/NativeContract.lean.  The value
 * classes correspond to that model's authority constructors:
 *
 *   NativeContract.Value.cap brand rights   -> a FACET of a brand
 *   NativeContract.Value.box id brand data  -> a CAPSULE
 *   NativeContract.Value.nonce id           -> a KEY
 *
 * Every object is a GV_SEAL atom leaf (atom.h).  Its identity is the object
 * itself: equality, hashing, unification, repeated-variable matching,
 * indexing and removal all use pointer identity (Value.key: a capsule by its
 * allocation identity only, a facet by brand and rights, which this
 * implementation makes one canonical object, a key by its identity).  The
 * printed observer is `(Sealed NICK)`, `(Sealer NICK)`, `(Unsealer NICK)`,
 * `(Seal NICK)`, `(SealFacet NICK)` or `(Key)`; the nickname is public
 * display metadata and grants nothing.  No operation turns printed text, a
 * number or a payload into an object.
 *
 * Creation of a brand hands out its two attenuated facets.  The both-rights
 * root facet stays with the issuer (C code that creates a brand for private
 * use keeps it; the MeTTa library never returns it).  Neither attenuated
 * facet yields the other.
 *
 * Payloads are retained descriptors.  An atom payload is deep-copied into
 * an arena owned by the capsule: its term structure survives every reset of
 * the arenas it came from, and the copy retains the owners of everything it
 * names (other seal objects, foreign records, saved bindings, native-handle
 * owners) through the ordinary arena-hold mechanism.  Variables keep their
 * identities: unsealing never freshens, instantiates or evaluates.  Spaces
 * and state cells are named by reference, exactly as the runtime names them:
 * sealing does not copy, freeze or snapshot their contents, and does not
 * revoke other aliases.  A native payload carries its own retain/release
 * and a kind tag chosen by the issuing kernel.
 *
 * Objects are immutable after construction and count holders atomically,
 * so references may cross evaluation branches and threads as themselves.
 * Identity counters are 64-bit and never wrap: exhaustion is a resource
 * outcome, never reuse of a live identity.
 */

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

typedef struct CettaSealObject CettaSealObject;

typedef enum {
    CETTA_SEAL_OBJECT_NONE = 0,
    CETTA_SEAL_OBJECT_FACET = 1,
    CETTA_SEAL_OBJECT_CAPSULE = 2,
    CETTA_SEAL_OBJECT_KEY = 3,
} CettaSealObjectKind;

/* Rights of a facet: a subset lattice over {seal, unseal}. */
enum {
    CETTA_SEAL_RIGHTS_NONE = 0u,
    CETTA_SEAL_RIGHT_SEAL = 1u,
    CETTA_SEAL_RIGHT_UNSEAL = 2u,
    CETTA_SEAL_RIGHTS_BOTH = 3u,
};

/* Outcomes keep the model's distinctions: a recognition miss (wrong brand,
 * not a capsule) is an empty answer; a wrong facet, a missing right and a
 * denied policy are faults; resource outcomes are never logical answers. */
typedef enum {
    CETTA_SEAL_OK = 0,
    CETTA_SEAL_MISS,
    CETTA_SEAL_WRONG_FACET,
    CETTA_SEAL_MISSING_RIGHT,
    CETTA_SEAL_DENIED,
    CETTA_SEAL_NOT_PORTABLE,
    CETTA_SEAL_NOMEM,
    CETTA_SEAL_IDENTITY_EXHAUSTED,
    CETTA_SEAL_INVALID,
} CettaSealStatus;

const char *cetta_seal_status_name(CettaSealStatus status);

/* A native payload.  `kind` is an address chosen by the issuing kernel; an
 * unseal that names a different kind is a recognition miss. */
typedef struct {
    const void *kind;
    void *resource;
    void (*retain)(void *resource);
    void (*release)(void *resource);
} CettaSealNativePayload;

/* ── objects ──────────────────────────────────────────────────────────── */

/* A fresh brand.  Returns its both-rights root facet (one owned reference);
 * attenuate it to hand out facets.  NULL with *status on failure. */
CettaSealObject *cetta_seal_brand_new(const char *nickname,
                                      CettaSealStatus *status);
/* A private process-lifetime brand, initialized on first successful use.
 * Keep the cell at a stable address and do not copy or destroy it. The
 * returned root is borrowed from the cell; failed allocation leaves it
 * uninitialized and may be retried. Concurrent callers get the same root. */
typedef struct {
    _Atomic(CettaSealObject *) current;
    pthread_mutex_t gate;
    const char *nickname;
} CettaSealBrandCell;
#define CETTA_SEAL_BRAND_CELL_INIT(nick) \
    { .current = NULL, .gate = PTHREAD_MUTEX_INITIALIZER, .nickname = (nick) }
CettaSealObject *cetta_seal_brand_get(CettaSealBrandCell *cell,
                                      CettaSealStatus *status);
/* The facet of the same brand with rights `facet.rights ∩ wanted`; an
 * owned reference.  NULL when `facet` is not a facet. */
CettaSealObject *cetta_seal_attenuate(CettaSealObject *facet, uint32_t wanted);
/* A fresh key (an unforgeable nonce reference). */
CettaSealObject *cetta_seal_key_new(CettaSealStatus *status);

void cetta_seal_retain(CettaSealObject *object);
void cetta_seal_release(CettaSealObject *object);

/* Seal a payload under `facet` (needs the seal right); *out is an owned
 * reference to a fresh capsule.  The atom payload is copied, not evaluated;
 * NULL atoms are invalid. */
CettaSealStatus cetta_seal_atom(CettaSealObject *facet, Atom *payload,
                                CettaSealObject **out);
CettaSealStatus cetta_seal_native(CettaSealObject *facet,
                                  const CettaSealNativePayload *payload,
                                  CettaSealObject **out);
/* Same, with a public atom view retained beside the native payload (for
 * read-only inspection by the issuing service; never a source of
 * authority). */
CettaSealStatus cetta_seal_native_with_view(
    CettaSealObject *facet, const CettaSealNativePayload *payload,
    Atom *view, CettaSealObject **out);

/* Unseal with `facet` (needs the unseal right).  OK copies the retained
 * payload atom into `dst` unchanged.  A capsule of another brand, a native
 * payload, or a non-capsule is a MISS. */
CettaSealStatus cetta_seal_unseal_atom(CettaSealObject *facet,
                                       CettaSealObject *capsule, Arena *dst,
                                       Atom **out);
/* Unseal a native payload of `kind`.  *resource_out is borrowed: valid for
 * as long as the caller holds `capsule`. */
CettaSealStatus cetta_seal_unseal_native(CettaSealObject *facet,
                                         CettaSealObject *capsule,
                                         const void *kind,
                                         void **resource_out);
/* The public view of a native capsule (borrowed, capsule-owned), only
 * through a facet with the unseal right of the capsule's brand. */
CettaSealStatus cetta_seal_unseal_view(CettaSealObject *facet,
                                       CettaSealObject *capsule,
                                       Atom **view_out);

/* ── read-only observations (no authority) ────────────────────────────── */

CettaSealObjectKind cetta_seal_object_kind(const CettaSealObject *object);
/* Rights of a facet; CETTA_SEAL_RIGHTS_NONE for anything else. */
uint32_t cetta_seal_facet_rights(const CettaSealObject *facet);
/* Whether two facets or capsules belong to one brand.  Learning this needs
 * no right beyond holding both references. */
bool cetta_seal_same_brand(const CettaSealObject *left,
                           const CettaSealObject *right);
/* The brand nickname (public display metadata), or NULL for a key. */
const char *cetta_seal_nickname(const CettaSealObject *object);

/* ── atoms ────────────────────────────────────────────────────────────── */

/* The atom of `object` in `arena`; the arena retains the object. */
Atom *cetta_seal_object_atom(Arena *arena, CettaSealObject *object);
/* The object of a GV_SEAL atom, borrowed while an arena holds the atom. */
CettaSealObject *cetta_seal_atom_object(const Atom *atom);

/* Portable encoders: OK when `atom` holds no authority object anywhere,
 * NOT_PORTABLE otherwise.  A portable certificate is ordinary data; importing
 * it requires fresh checking, never deserialized authority. */
CettaSealStatus cetta_seal_check_portable(const Atom *atom);

#ifdef CETTA_TEST_HOOKS
/* Start the identity counter near its end to exercise exhaustion. */
void cetta_seal_test_set_next_identity(uint64_t next);
/* Fail the n-th following allocation (1-based); 0 disables. */
void cetta_seal_test_fail_allocation(uint64_t nth);
uint64_t cetta_seal_test_live_objects(void);
#endif

#endif /* CETTA_SEAL_H */
