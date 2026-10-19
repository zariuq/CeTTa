#include "native/seal_module.h"

#include "library.h"
#include "match.h"
#include "seal.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/*
 * The `seal` library: MeTTa access to the shared seal substrate (seal.h).
 *
 *   (seal:new NICK)               -> (seal:facets SEALER UNSEALER)
 *   (seal:seal SEALER PAYLOAD)    -> a fresh capsule; PAYLOAD is captured
 *                                    as written, not evaluated
 *   (seal:unseal UNSEALER BOX)    -> the retained payload, unchanged; no
 *                                    answer for a wrong brand or non-box
 *   (seal:attenuate FACET RIGHTS) -> facet with RIGHTS ∩ its rights
 *   (seal:new-key)                -> a fresh key
 *   (seal:portable VALUE)         -> VALUE when it names no runtime
 *                                    identity, else a not-portable fault
 *   (seal:instantiate VALUE)      -> VALUE with fresh variables (explicit)
 *
 * Faults (a wrong facet, a missing right, a denied policy) are raised
 * errors `(Error CALL (SealFault REASON))`; a recognition miss is an empty
 * answer.  The try- spelling of an operation (__cetta_lib_seal_try-unseal
 * ...) answers the same Error as its value instead of raising it, so a
 * program can branch on a fault; a miss is still no answer.  A key-locked service keeps its unsealer private:
 *
 *   (seal:new-service NICK)       -> (seal:service USER ADMIN SEALER)
 *   (seal:lock SEALER KEY DATA)   -> capsule of (seal:keyed KEY DATA)
 *   (seal:grant ADMIN KEY MODE)   -> MODE is always or once
 *   (seal:revoke ADMIN KEY)
 *   (seal:open USER BOX KEY)      -> DATA, checking the current policy for
 *                                    the offered key at this call
 *   (seal:reader USER)            -> a facet limited to portable space reads
 *   (seal:read-space READER BOX KEY) -> portable rows of a sealed space;
 *                                    USER may also make this call
 *
 * USER and ADMIN are capsules of a brand private to this module; no MeTTa
 * operation unseals them.  Holding a key reference is the caller's offer:
 * guessed text or numbers are not keys.
 */

#define SEAL_HEAD_PREFIX "__cetta_lib_seal_"

/* ── service records ──────────────────────────────────────────────────── */

typedef enum {
    SERVICE_GRANT_ALWAYS = 1,
    SERVICE_GRANT_ONCE = 2,
    SERVICE_GRANT_REVOKED = 3,
    SERVICE_GRANT_CONSUMED = 4,
} ServiceGrantState;

typedef struct {
    CettaSealObject *key;
    ServiceGrantState state;
} ServiceGrant;

typedef struct {
    atomic_size_t holders;
    pthread_mutex_t lock;
    CettaSealObject *unsealer; /* private */
    CettaSealObject *sealer;
    ServiceGrant *grants;
    size_t grant_len;
    size_t grant_cap;
} SealService;

static const char g_service_user_kind = 'u';
static const char g_service_admin_kind = 'a';
static const char g_service_reader_kind = 'r';

static pthread_once_t g_service_brand_once = PTHREAD_ONCE_INIT;
static CettaSealObject *g_service_root; /* never released: process brand */

static void service_brand_init(void) {
    g_service_root = cetta_seal_brand_new("seal-service", NULL);
}

static void service_retain(void *resource) {
    SealService *service = resource;
    atomic_fetch_add(&service->holders, 1u);
}

static void service_release(void *resource) {
    SealService *service = resource;
    if (!service || atomic_fetch_sub(&service->holders, 1u) != 1u)
        return;
    for (size_t i = 0; i < service->grant_len; ++i)
        cetta_seal_release(service->grants[i].key);
    free(service->grants);
    cetta_seal_release(service->unsealer);
    cetta_seal_release(service->sealer);
    pthread_mutex_destroy(&service->lock);
    free(service);
}

/* ── helpers ──────────────────────────────────────────────────────────── */

static Atom *seal_fault(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                        const char *reason) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1u));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; ++i)
        elems[i + 1u] = args[i];
    Atom *call = atom_expr(a, elems, nargs + 1u);
    return atom_error(a, call,
                      atom_expr2(a, atom_symbol(a, "SealFault"),
                                 atom_symbol(a, reason)));
}

static bool seal_raise(CettaCallOutcome *out, Arena *a, Atom *head,
                       Atom **args, uint32_t nargs, const char *reason) {
    *out = cetta_call_raised(seal_fault(a, head, args, nargs, reason));
    return true;
}

static bool seal_raise_status(CettaCallOutcome *out, Arena *a, Atom *head,
                              Atom **args, uint32_t nargs,
                              CettaSealStatus status) {
    return seal_raise(out, a, head, args, nargs,
                      cetta_seal_status_name(status));
}

static bool seal_value(CettaCallOutcome *out, Atom *value) {
    *out = cetta_call_completed_value(value);
    return true;
}

static const char *seal_text_arg(Atom *atom) {
    if (!atom)
        return NULL;
    if (atom->kind == ATOM_SYMBOL)
        return atom_name_cstr(atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
        return atom->ground.sval;
    return NULL;
}

/* An owned object reference becomes an atom held by `a`, then the owned
 * reference is dropped: the arena's hold keeps the object alive. */
static Atom *seal_publish_object(Arena *a, CettaSealObject *object) {
    if (!object)
        return NULL;
    Atom *atom = cetta_seal_object_atom(a, object);
    cetta_seal_release(object);
    return atom;
}

/* The operation a head names, after the prefix and an optional "try-". */
static const char *seal_op_of(Atom *head, bool *soft) {
    const char *name = atom_name_cstr(head);
    size_t prefix = sizeof(SEAL_HEAD_PREFIX) - 1u;
    *soft = false;
    if (!name || strncmp(name, SEAL_HEAD_PREFIX, prefix) != 0)
        return NULL;
    name += prefix;
    *soft = strncmp(name, "try-", 4) == 0;
    return *soft ? name + 4 : name;
}

static SealService *service_from(Atom *atom, const void *kind) {
    pthread_once(&g_service_brand_once, service_brand_init);
    void *resource = NULL;
    CettaSealObject *capsule = cetta_seal_atom_object(atom);
    if (!g_service_root || !capsule ||
        cetta_seal_unseal_native(g_service_root, capsule, kind, &resource) !=
            CETTA_SEAL_OK)
        return NULL;
    return resource;
}

static ServiceGrant *service_grant(SealService *service,
                                   CettaSealObject *key) {
    for (size_t i = 0; i < service->grant_len; ++i)
        if (service->grants[i].key == key)
            return &service->grants[i];
    return NULL;
}

/* ── operations ───────────────────────────────────────────────────────── */

static bool op_new(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                   CettaCallOutcome *out) {
    const char *nick = nargs == 1u ? seal_text_arg(args[0]) : NULL;
    if (!nick)
        return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealStatus status;
    CettaSealObject *root = cetta_seal_brand_new(nick, &status);
    if (!root)
        return seal_raise_status(out, a, head, args, nargs, status);
    CettaSealObject *sealer = cetta_seal_attenuate(root, CETTA_SEAL_RIGHT_SEAL);
    CettaSealObject *unsealer =
        cetta_seal_attenuate(root, CETTA_SEAL_RIGHT_UNSEAL);
    /* The both-rights root stays private to this call: it is released, and
     * no operation derives it from either attenuated facet. */
    cetta_seal_release(root);
    Atom *sealer_atom = seal_publish_object(a, sealer);
    Atom *unsealer_atom = seal_publish_object(a, unsealer);
    if (!sealer_atom || !unsealer_atom)
        return seal_raise(out, a, head, args, nargs, "no-memory");
    return seal_value(out, atom_expr3(a, atom_symbol(a, "seal:facets"),
                                      sealer_atom, unsealer_atom));
}

static bool op_seal(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    if (nargs != 2u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealObject *facet = cetta_seal_atom_object(args[0]);
    CettaSealObject *capsule = NULL;
    CettaSealStatus status = cetta_seal_atom(facet, args[1], &capsule);
    if (status != CETTA_SEAL_OK) {
        if (!facet)
            status = CETTA_SEAL_WRONG_FACET;
        return seal_raise_status(out, a, head, args, nargs, status);
    }
    Atom *atom = seal_publish_object(a, capsule);
    return atom ? seal_value(out, atom)
                : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_unseal(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                      CettaCallOutcome *out) {
    if (nargs != 2u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealObject *facet = cetta_seal_atom_object(args[0]);
    if (!facet)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    Atom *payload = NULL;
    CettaSealStatus status = cetta_seal_unseal_atom(
        facet, cetta_seal_atom_object(args[1]), a, &payload);
    if (status == CETTA_SEAL_MISS) {
        *out = cetta_call_failure();
        return true;
    }
    if (status != CETTA_SEAL_OK)
        return seal_raise_status(out, a, head, args, nargs, status);
    return seal_value(out, payload);
}

static bool op_attenuate(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                         CettaCallOutcome *out) {
    const char *rights = nargs == 2u ? seal_text_arg(args[1]) : NULL;
    CettaSealObject *facet = nargs == 2u ? cetta_seal_atom_object(args[0])
                                         : NULL;
    if (!rights)
        return seal_raise(out, a, head, args, nargs, "invalid");
    uint32_t wanted;
    if (strcmp(rights, "seal") == 0) wanted = CETTA_SEAL_RIGHT_SEAL;
    else if (strcmp(rights, "unseal") == 0) wanted = CETTA_SEAL_RIGHT_UNSEAL;
    else if (strcmp(rights, "none") == 0) wanted = CETTA_SEAL_RIGHTS_NONE;
    else return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealObject *result = cetta_seal_attenuate(facet, wanted);
    if (!result)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    Atom *atom = seal_publish_object(a, result);
    return atom ? seal_value(out, atom)
                : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_new_key(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                       CettaCallOutcome *out) {
    CettaSealStatus status;
    CettaSealObject *key = cetta_seal_key_new(&status);
    if (!key)
        return seal_raise_status(out, a, head, args, nargs, status);
    Atom *atom = seal_publish_object(a, key);
    return atom ? seal_value(out, atom)
                : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_kind(Arena *a, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    const char *name = "Data";
    if (nargs == 1u) {
        CettaSealObject *object = cetta_seal_atom_object(args[0]);
        switch (cetta_seal_object_kind(object)) {
        case CETTA_SEAL_OBJECT_FACET:
            switch (cetta_seal_facet_rights(object)) {
            case CETTA_SEAL_RIGHT_SEAL: name = "Sealer"; break;
            case CETTA_SEAL_RIGHT_UNSEAL: name = "Unsealer"; break;
            case CETTA_SEAL_RIGHTS_BOTH: name = "Seal"; break;
            default: name = "SealFacet"; break;
            }
            break;
        case CETTA_SEAL_OBJECT_CAPSULE: name = "Sealed"; break;
        case CETTA_SEAL_OBJECT_KEY: name = "Key"; break;
        case CETTA_SEAL_OBJECT_NONE: break;
        }
    }
    return seal_value(out, atom_symbol(a, name));
}

static bool op_same_brand(Arena *a, Atom **args, uint32_t nargs,
                          CettaCallOutcome *out) {
    bool same = nargs == 2u &&
                cetta_seal_same_brand(cetta_seal_atom_object(args[0]),
                                      cetta_seal_atom_object(args[1]));
    /* A truth value as the evaluator's own comparisons answer one, so HE's
     * if and PeTTa's if both read it. */
    *out = cetta_call_value(atom_bool(a, same));
    return true;
}

static bool op_nickname(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                        CettaCallOutcome *out) {
    const char *nick = nargs == 1u
        ? cetta_seal_nickname(cetta_seal_atom_object(args[0])) : NULL;
    if (!nick)
        return seal_raise(out, a, head, args, nargs, "invalid");
    return seal_value(out, atom_string(a, nick));
}

static bool op_portable(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                        CettaCallOutcome *out) {
    if (nargs != 1u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealStatus status = cetta_seal_check_portable(args[0]);
    if (status != CETTA_SEAL_OK)
        return seal_raise_status(out, a, head, args, nargs, status);
    return seal_value(out, args[0]);
}

static bool op_instantiate(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    if (nargs != 1u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    Atom *fresh = rename_vars_except(a, args[0], atom_unit(a));
    if (!fresh)
        return seal_raise(out, a, head, args, nargs, "invalid");
    return seal_value(out, fresh);
}

static Atom *service_handle(Arena *a, SealService *service, const void *kind) {
    CettaSealNativePayload payload = {
        .kind = kind, .resource = service,
        .retain = service_retain, .release = service_release,
    };
    CettaSealObject *capsule = NULL;
    if (cetta_seal_native(g_service_root, &payload, &capsule) != CETTA_SEAL_OK)
        return NULL;
    return seal_publish_object(a, capsule);
}

static bool op_reader(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                       CettaCallOutcome *out) {
    if (nargs == 1u && service_from(args[0], &g_service_reader_kind))
        return seal_value(out, args[0]);
    SealService *service = nargs == 1u
        ? service_from(args[0], &g_service_user_kind) : NULL;
    if (!service)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    Atom *reader = service_handle(a, service, &g_service_reader_kind);
    return reader ? seal_value(out, reader)
                  : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_new_service(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    const char *nick = nargs == 1u ? seal_text_arg(args[0]) : NULL;
    pthread_once(&g_service_brand_once, service_brand_init);
    if (!nick || !g_service_root)
        return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealStatus status;
    CettaSealObject *root = cetta_seal_brand_new(nick, &status);
    if (!root)
        return seal_raise_status(out, a, head, args, nargs, status);
    SealService *service = calloc(1u, sizeof(*service));
    if (!service) {
        cetta_seal_release(root);
        return seal_raise(out, a, head, args, nargs, "no-memory");
    }
    atomic_init(&service->holders, 1u);
    pthread_mutex_init(&service->lock, NULL);
    service->unsealer = cetta_seal_attenuate(root, CETTA_SEAL_RIGHT_UNSEAL);
    service->sealer = cetta_seal_attenuate(root, CETTA_SEAL_RIGHT_SEAL);
    cetta_seal_release(root);
    Atom *user = service_handle(a, service, &g_service_user_kind);
    Atom *admin = service_handle(a, service, &g_service_admin_kind);
    cetta_seal_retain(service->sealer);
    Atom *sealer = seal_publish_object(a, service->sealer);
    service_release(service); /* the handles hold it now */
    if (!user || !admin || !sealer)
        return seal_raise(out, a, head, args, nargs, "no-memory");
    Atom *elems[4] = {atom_symbol(a, "seal:service"), user, admin, sealer};
    return seal_value(out, atom_expr(a, elems, 4u));
}

static bool op_lock(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    if (nargs != 3u ||
        cetta_seal_object_kind(cetta_seal_atom_object(args[1])) !=
            CETTA_SEAL_OBJECT_KEY)
        return seal_raise(out, a, head, args, nargs, "invalid");
    Atom *keyed = atom_expr3(a, atom_symbol(a, "seal:keyed"), args[1], args[2]);
    CettaSealObject *capsule = NULL;
    CettaSealStatus status =
        cetta_seal_atom(cetta_seal_atom_object(args[0]), keyed, &capsule);
    if (status != CETTA_SEAL_OK)
        return seal_raise_status(out, a, head, args, nargs,
                                 cetta_seal_atom_object(args[0])
                                     ? status : CETTA_SEAL_WRONG_FACET);
    Atom *atom = seal_publish_object(a, capsule);
    return atom ? seal_value(out, atom)
                : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_grant(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                     CettaCallOutcome *out, bool revoke) {
    SealService *service = nargs >= 2u
        ? service_from(args[0], &g_service_admin_kind) : NULL;
    CettaSealObject *key = nargs >= 2u ? cetta_seal_atom_object(args[1]) : NULL;
    const char *mode = !revoke && nargs == 3u ? seal_text_arg(args[2]) : NULL;
    if (!service)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    if (cetta_seal_object_kind(key) != CETTA_SEAL_OBJECT_KEY ||
        (!revoke && (!mode || (strcmp(mode, "always") != 0 &&
                               strcmp(mode, "once") != 0))) ||
        (revoke && nargs != 2u))
        return seal_raise(out, a, head, args, nargs, "invalid");
    ServiceGrantState state = revoke ? SERVICE_GRANT_REVOKED
        : strcmp(mode, "once") == 0 ? SERVICE_GRANT_ONCE
                                    : SERVICE_GRANT_ALWAYS;
    pthread_mutex_lock(&service->lock);
    ServiceGrant *grant = service_grant(service, key);
    if (!grant) {
        if (service->grant_len == service->grant_cap) {
            size_t cap = service->grant_cap ? service->grant_cap * 2u : 4u;
            ServiceGrant *grown = realloc(service->grants, cap * sizeof(*grown));
            if (!grown) {
                pthread_mutex_unlock(&service->lock);
                return seal_raise(out, a, head, args, nargs, "no-memory");
            }
            service->grants = grown;
            service->grant_cap = cap;
        }
        cetta_seal_retain(key);
        grant = &service->grants[service->grant_len++];
        grant->key = key;
    }
    grant->state = state;
    pthread_mutex_unlock(&service->lock);
    return seal_value(out, atom_unit(a));
}

/* Open a key-locked box under the current policy. Authentication and result
 * construction happen under the service lock; a one-shot grant is consumed
 * only after both succeed. */
static bool op_open(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out, bool read_space) {
    SealService *service = nargs == 3u
        ? service_from(args[0], &g_service_user_kind) : NULL;
    if (!service && read_space && nargs == 3u)
        service = service_from(args[0], &g_service_reader_kind);
    CettaSealObject *key = nargs == 3u ? cetta_seal_atom_object(args[2]) : NULL;
    if (!service)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    if (cetta_seal_object_kind(key) != CETTA_SEAL_OBJECT_KEY) {
        *out = cetta_call_failure(); /* no key offered: no answer */
        return true;
    }
    pthread_mutex_lock(&service->lock);
    ServiceGrant *grant = service_grant(service, key);
    bool permitted = grant && (grant->state == SERVICE_GRANT_ALWAYS ||
                               grant->state == SERVICE_GRANT_ONCE);
    if (!permitted) {
        pthread_mutex_unlock(&service->lock);
        return seal_raise_status(out, a, head, args, nargs, CETTA_SEAL_DENIED);
    }
    Atom *payload = NULL;
    CettaSealStatus status = cetta_seal_unseal_atom(
        service->unsealer, cetta_seal_atom_object(args[1]), a, &payload);
    bool matched = status == CETTA_SEAL_OK && payload &&
        payload->kind == ATOM_EXPR && payload->expr.len == 3u &&
        atom_is_symbol(payload->expr.elems[0], "seal:keyed") &&
        cetta_seal_atom_object(payload->expr.elems[1]) == key;
    Atom *data = matched ? payload->expr.elems[2] : NULL;
    if (matched && read_space &&
        !(data->kind == ATOM_GROUNDED && data->ground.gkind == GV_SPACE))
        matched = false;
    if (status != CETTA_SEAL_OK && status != CETTA_SEAL_MISS) {
        pthread_mutex_unlock(&service->lock);
        return seal_raise_status(out, a, head, args, nargs, status);
    }
    if (!matched) {
        pthread_mutex_unlock(&service->lock);
        *out = cetta_call_failure();
        return true;
    }
    Atom *result = data;
    if (read_space) {
        /* A copy alone can retain authority hidden in a row (including a
         * nested space). ServiceView.lean uses this same portable boundary.
         * Other holders may mutate the source; this is not a concurrent
         * transaction or a revocation of their aliases. */
        Space *space = data->ground.ptr;
        CettaCount length = space_length64(space);
        if (length >= SIZE_MAX / sizeof(Atom *)) {
            pthread_mutex_unlock(&service->lock);
            return seal_raise(out, a, head, args, nargs, "capacity");
        }
        Atom **elems = arena_alloc(a, sizeof(Atom *) * (size_t)(length + 1u));
        elems[0] = atom_symbol(a, "seal:snapshot");
        CettaCount kept = 0;
        for (CettaCount i = 0; i < length; ++i) {
            Atom *atom = space_get_at64(space, i);
            if (!atom)
                continue;
            CettaSealStatus portable = cetta_seal_check_portable(atom);
            if (portable != CETTA_SEAL_OK) {
                pthread_mutex_unlock(&service->lock);
                return seal_raise_status(out, a, head, args, nargs, portable);
            }
            elems[1u + kept++] = atom_deep_copy(a, atom);
        }
        result = atom_expr(a, elems, kept + 1u);
    }
    if (grant->state == SERVICE_GRANT_ONCE)
        grant->state = SERVICE_GRANT_CONSUMED;
    pthread_mutex_unlock(&service->lock);
    return seal_value(out, result);
}

static bool seal_dispatch(const char *op, Arena *a, Atom *head, Atom **args,
                          uint32_t nargs, CettaCallOutcome *out) {
    if (strcmp(op, "new") == 0) return op_new(a, head, args, nargs, out);
    if (strcmp(op, "seal") == 0) return op_seal(a, head, args, nargs, out);
    if (strcmp(op, "unseal") == 0) return op_unseal(a, head, args, nargs, out);
    if (strcmp(op, "attenuate") == 0) return op_attenuate(a, head, args, nargs, out);
    if (strcmp(op, "new-key") == 0) return op_new_key(a, head, args, nargs, out);
    if (strcmp(op, "kind") == 0) return op_kind(a, args, nargs, out);
    if (strcmp(op, "same-brand") == 0) return op_same_brand(a, args, nargs, out);
    if (strcmp(op, "nickname") == 0) return op_nickname(a, head, args, nargs, out);
    if (strcmp(op, "portable") == 0) return op_portable(a, head, args, nargs, out);
    if (strcmp(op, "instantiate") == 0) return op_instantiate(a, head, args, nargs, out);
    if (strcmp(op, "new-service") == 0) return op_new_service(a, head, args, nargs, out);
    if (strcmp(op, "reader") == 0) return op_reader(a, head, args, nargs, out);
    if (strcmp(op, "lock") == 0) return op_lock(a, head, args, nargs, out);
    if (strcmp(op, "grant") == 0) return op_grant(a, head, args, nargs, out, false);
    if (strcmp(op, "revoke") == 0) return op_grant(a, head, args, nargs, out, true);
    if (strcmp(op, "open") == 0) return op_open(a, head, args, nargs, out, false);
    if (strcmp(op, "read-space") == 0) return op_open(a, head, args, nargs, out, true);
    return false;
}

bool cetta_seal_module_call(struct CettaLibraryContext *ctx, Space *space,
                            Arena *a, Atom *head, Atom **args, uint32_t nargs,
                            CettaCallOutcome *out) {
    (void)ctx;
    (void)space;
    if (!head || head->kind != ATOM_SYMBOL || !a || !out)
        return false;
    bool soft;
    const char *op = seal_op_of(head, &soft);
    if (!op || !seal_dispatch(op, a, head, args, nargs, out))
        return false;
    /* a try- form: the fault is the call's value */
    if (soft && out->kind == CETTA_CALL_RAISED)
        *out = cetta_call_completed_value(out->term);
    return true;
}
