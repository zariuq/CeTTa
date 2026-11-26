#include "native/seal_module.h"

#include "eval.h"
#include "library.h"
#include "match.h"
#include "native_sha256.h"
#include "seal.h"
#include "space_theory.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
    if (!elems)
        return NULL;
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; ++i)
        elems[i + 1u] = args[i];
    Atom *call = atom_expr(a, elems, nargs + 1u);
    Atom *tag = atom_symbol(a, "SealFault");
    Atom *detail = atom_symbol(a, reason);
    Atom *fault = tag && detail ? atom_expr2(a, tag, detail) : NULL;
    return call && fault ? atom_error(a, call, fault) : NULL;
}

static bool seal_raise(CettaCallOutcome *out, Arena *a, Atom *head,
                       Atom **args, uint32_t nargs, const char *reason) {
    Atom *fault = seal_fault(a, head, args, nargs, reason);
    *out = fault ? cetta_call_raised(fault) : cetta_call_interrupted();
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
        if (!elems)
            goto snapshot_no_memory;
        elems[0] = atom_symbol(a, "seal:snapshot");
        if (!elems[0])
            goto snapshot_no_memory;
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
            Atom *owned = atom_deep_copy(a, atom);
            if (!owned)
                goto snapshot_no_memory;
            elems[1u + kept++] = owned;
        }
        result = atom_expr(a, elems, kept + 1u);
    }
    if (!result)
        goto snapshot_no_memory;
    if (grant->state == SERVICE_GRANT_ONCE)
        grant->state = SERVICE_GRANT_CONSUMED;
    pthread_mutex_unlock(&service->lock);
    return seal_value(out, result);

snapshot_no_memory:
    pthread_mutex_unlock(&service->lock);
    return seal_raise_status(out, a, head, args, nargs, CETTA_SEAL_NOMEM);
}

static Space *seal_resolve_space(Space *current, Atom *arg) {
    if (!arg)
        return NULL;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_SPACE)
        return (Space *)arg->ground.ptr;
    if (arg->kind == ATOM_SYMBOL && arg->sym_id == g_builtin_syms.self)
        return current;
    return NULL;
}

static bool op_seal_head(Space *current, Arena *a, Atom *head, Atom **args,
                         uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 3u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    Space *target = seal_resolve_space(current, args[0]);
    const char *nick = seal_text_arg(args[2]);
    if (!target || !args[1] || args[1]->kind != ATOM_SYMBOL || !nick)
        return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealStatus status;
    CettaSealObject *root = cetta_seal_brand_new(nick, &status);
    if (!root)
        return seal_raise_status(out, a, head, args, nargs, status);
    CettaSealObject *writer = cetta_seal_attenuate(root, CETTA_SEAL_RIGHT_SEAL);
    cetta_seal_release(root);
    if (!writer)
        return seal_raise(out, a, head, args, nargs, "no-memory");
    const char *reason = NULL;
    if (!space_theory_head_attach_write_seal(
            target, args[1]->sym_id, writer, &reason)) {
        cetta_seal_release(writer);
        return seal_raise(out, a, head, args, nargs,
                          reason ? reason : "already-sealed");
    }
    Atom *writer_atom = seal_publish_object(a, writer);
    return writer_atom ? seal_value(out, writer_atom)
                       : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_head_sealed(Space *current, Arena *a, Atom *head, Atom **args,
                           uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 2u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    Space *target = seal_resolve_space(current, args[0]);
    if (!target || !args[1] || args[1]->kind != ATOM_SYMBOL)
        return seal_raise(out, a, head, args, nargs, "invalid");
    bool sealed = space_theory_head_is_authority_sealed(target, args[1]->sym_id);
    *out = cetta_call_value(atom_bool(a, sealed));
    return true;
}

static bool op_head_write(Space *current, Arena *a, Atom *head, Atom **args,
                          uint32_t nargs, CettaCallOutcome *out, bool is_remove) {
    if (nargs != 3u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealObject *facet = cetta_seal_atom_object(args[0]);
    if (!facet || cetta_seal_object_kind(facet) != CETTA_SEAL_OBJECT_FACET)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    Space *target = seal_resolve_space(current, args[1]);
    if (!target || !args[2])
        return seal_raise(out, a, head, args, nargs, "invalid");
    Atom **elems = arena_alloc(a, sizeof(Atom *) * 4u);
    if (!elems)
        return seal_raise(out, a, head, args, nargs, "no-memory");
    elems[0] = head;
    elems[1] = args[0];
    elems[2] = args[1];
    elems[3] = args[2];
    Atom *call = atom_expr(a, elems, 4u);
    if (!call)
        return seal_raise(out, a, head, args, nargs, "no-memory");
    const char *seal_reason = NULL;
    Atom *err = NULL;
    bool ok = is_remove
        ? (eval_authorized_head_space_remove &&
           eval_authorized_head_space_remove(
               target, a, call, args[2], facet, &seal_reason, &err))
        : (eval_authorized_head_space_add &&
           eval_authorized_head_space_add(
               target, a, call, args[2], facet, &seal_reason, &err));
    if (!ok) {
        if (seal_reason)
            return seal_raise(out, a, head, args, nargs, seal_reason);
        if (err) {
            *out = cetta_call_raised(err);
            return true;
        }
        return seal_raise(out, a, head, args, nargs, "invalid");
    }
    return seal_value(out, atom_unit(a));
}

/* ── Cross-process portable capability tokens (macaroon-hmac-sha256 v1) ─── *
 *
 * Open wire token shape (carrier-tagged & versioned for future carriers):
 *   (seal:token macaroon-hmac-sha256 1 <root-id-str> (caveats ...) <sig-hex>)
 *
 * Structured MeTTa caveats checked by a fixed, total, terminating checker:
 *   (head <symbol>)      - target head symbol must equal <symbol>
 *   (op add|remove|write)- write kind must be allowed by <op>
 *   (expires <int-ms>)   - wall-clock UTC ms at check time must be <= <int-ms>
 *   (nonce <hex-str>)    - single-use challenge minted by this authority
 */

#define SEAL_TOKEN_CARRIER_MACAROON "macaroon-hmac-sha256"
#define SEAL_TOKEN_VERSION_V1 1
#define SEAL_TOKEN_MAX_CAVEATS 64u

typedef struct {
    char hex[33];
    bool consumed;
} SealChallengeNonce;

typedef struct {
    atomic_size_t holders;
    pthread_mutex_t lock;
    CettaSealObject *write_facet;
    uint8_t secret[32];
    char **revoked_ids;
    size_t revoked_len;
    size_t revoked_cap;
    SealChallengeNonce *nonces;
    size_t nonce_len;
    size_t nonce_cap;
} SealTokenAuthority;

static const char g_token_authority_kind = 't';

static void seal_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0u; i < len; ++i)
        p[i] = 0u;
}

static bool seal_random_bytes(uint8_t *out, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return false;
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    size_t got = 0u;
    while (got < len) {
        ssize_t n = read(fd, out + got, len - got);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return false;
        }
        if (n == 0) {
            close(fd);
            return false;
        }
        got += (size_t)n;
    }
    close(fd);
    return true;
}

static int64_t seal_now_ms_utc(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L);
}

static void seal_bytes_to_hex(const uint8_t *in, size_t len, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0u; i < len; ++i) {
        out[i * 2u] = hex[(in[i] >> 4u) & 0x0fu];
        out[i * 2u + 1u] = hex[in[i] & 0x0fu];
    }
    out[len * 2u] = '\0';
}

static int seal_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool seal_hex_to_bytes(const char *hex, uint8_t *out, size_t byte_len) {
    if (!hex || strlen(hex) != byte_len * 2u)
        return false;
    for (size_t i = 0u; i < byte_len; ++i) {
        int hi = seal_hex_nibble(hex[i * 2u]);
        int lo = seal_hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool seal_ct_eq32(const uint8_t a[32], const uint8_t b[32]) {
    volatile uint8_t diff = 0u;
    for (size_t i = 0u; i < 32u; ++i)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0u;
}

static void seal_sha256_raw(const uint8_t *data, size_t len, uint8_t out[32]) {
    char hex[65];
    cetta_native_sha256_hex(data, len, hex);
    (void)seal_hex_to_bytes(hex, out, 32u);
}

static void seal_hmac_sha256(const uint8_t *key, size_t key_len,
                             const uint8_t *msg, size_t msg_len,
                             uint8_t out[32]) {
    uint8_t kblock[64];
    memset(kblock, 0, sizeof(kblock));
    if (key_len > 64u) {
        seal_sha256_raw(key, key_len, kblock);
    } else if (key_len > 0u) {
        memcpy(kblock, key, key_len);
    }
    uint8_t ipad[64];
    uint8_t opad[64];
    for (size_t i = 0u; i < 64u; ++i) {
        ipad[i] = (uint8_t)(kblock[i] ^ 0x36u);
        opad[i] = (uint8_t)(kblock[i] ^ 0x5cu);
    }
    CettaNativeSha256 inner_sha;
    cetta_native_sha256_init(&inner_sha);
    cetta_native_sha256_update(&inner_sha, ipad, 64u);
    if (msg_len > 0u)
        cetta_native_sha256_update(&inner_sha, msg, msg_len);
    char inner_hex[65];
    cetta_native_sha256_finish_hex(&inner_sha, inner_hex);
    uint8_t inner_raw[32];
    (void)seal_hex_to_bytes(inner_hex, inner_raw, 32u);

    CettaNativeSha256 outer_sha;
    cetta_native_sha256_init(&outer_sha);
    cetta_native_sha256_update(&outer_sha, opad, 64u);
    cetta_native_sha256_update(&outer_sha, inner_raw, 32u);
    char outer_hex[65];
    cetta_native_sha256_finish_hex(&outer_sha, outer_hex);
    (void)seal_hex_to_bytes(outer_hex, out, 32u);

    seal_secure_zero(kblock, sizeof(kblock));
    seal_secure_zero(ipad, sizeof(ipad));
    seal_secure_zero(opad, sizeof(opad));
    seal_secure_zero(inner_raw, sizeof(inner_raw));
    seal_secure_zero(inner_hex, sizeof(inner_hex));
    seal_secure_zero(outer_hex, sizeof(outer_hex));
}

static void token_authority_retain(void *resource) {
    SealTokenAuthority *auth = resource;
    atomic_fetch_add(&auth->holders, 1u);
}

static void token_authority_release(void *resource) {
    SealTokenAuthority *auth = resource;
    if (!auth || atomic_fetch_sub(&auth->holders, 1u) != 1u)
        return;
    for (size_t i = 0u; i < auth->revoked_len; ++i)
        free(auth->revoked_ids[i]);
    free(auth->revoked_ids);
    free(auth->nonces);
    cetta_seal_release(auth->write_facet);
    seal_secure_zero(auth->secret, sizeof(auth->secret));
    pthread_mutex_destroy(&auth->lock);
    free(auth);
}

static SealTokenAuthority *token_authority_from(Atom *atom) {
    pthread_once(&g_service_brand_once, service_brand_init);
    void *resource = NULL;
    CettaSealObject *capsule = cetta_seal_atom_object(atom);
    if (!g_service_root || !capsule ||
        cetta_seal_unseal_native(g_service_root, capsule,
                                 &g_token_authority_kind, &resource) !=
            CETTA_SEAL_OK)
        return NULL;
    return resource;
}

typedef enum {
    SEAL_CAVEAT_HEAD = 1,
    SEAL_CAVEAT_OP = 2,
    SEAL_CAVEAT_EXPIRES = 3,
    SEAL_CAVEAT_NONCE = 4,
} SealCaveatKind;

typedef enum {
    SEAL_OP_ADD = 1,
    SEAL_OP_REMOVE = 2,
    SEAL_OP_WRITE = 3,
} SealWriteOp;

typedef struct {
    SealCaveatKind kind;
    SymbolId head_sym;
    const char *head_name;
    SealWriteOp op;
    int64_t expires_ms;
    const char *nonce_hex;
} SealParsedCaveat;

static bool seal_parse_op_symbol(Atom *atom, SealWriteOp *out) {
    if (!atom || atom->kind != ATOM_SYMBOL)
        return false;
    const char *name = atom_name_cstr(atom);
    if (!name) return false;
    if (strcmp(name, "add") == 0) { *out = SEAL_OP_ADD; return true; }
    if (strcmp(name, "remove") == 0) { *out = SEAL_OP_REMOVE; return true; }
    if (strcmp(name, "write") == 0) { *out = SEAL_OP_WRITE; return true; }
    return false;
}

static bool seal_is_hex_string(const char *s, size_t min_len, size_t max_len) {
    if (!s) return false;
    size_t len = strlen(s);
    if (len < min_len || len > max_len || (len & 1u) != 0u)
        return false;
    for (size_t i = 0u; i < len; ++i) {
        if (seal_hex_nibble(s[i]) < 0)
            return false;
    }
    return true;
}

/* Fixed, total, fail-closed parser for structured MeTTa caveats. */
static bool seal_parse_caveat(Atom *atom, SealParsedCaveat *out) {
    memset(out, 0, sizeof(*out));
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2u ||
        !atom->expr.elems[0] || atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        !atom->expr.elems[1])
        return false;
    const char *tag = atom_name_cstr(atom->expr.elems[0]);
    Atom *arg = atom->expr.elems[1];
    if (!tag) return false;
    if (strcmp(tag, "head") == 0) {
        if (arg->kind != ATOM_SYMBOL)
            return false;
        const char *hname = atom_name_cstr(arg);
        if (!hname || !hname[0])
            return false;
        out->kind = SEAL_CAVEAT_HEAD;
        out->head_sym = arg->sym_id;
        out->head_name = hname;
        return true;
    }
    if (strcmp(tag, "op") == 0) {
        SealWriteOp op;
        if (!seal_parse_op_symbol(arg, &op))
            return false;
        out->kind = SEAL_CAVEAT_OP;
        out->op = op;
        return true;
    }
    if (strcmp(tag, "expires") == 0) {
        if (arg->kind != ATOM_GROUNDED || arg->ground.gkind != GV_INT ||
            arg->ground.ival < 0)
            return false;
        out->kind = SEAL_CAVEAT_EXPIRES;
        out->expires_ms = arg->ground.ival;
        return true;
    }
    if (strcmp(tag, "nonce") == 0) {
        if (arg->kind != ATOM_GROUNDED || arg->ground.gkind != GV_STRING ||
            !seal_is_hex_string(arg->ground.sval, 16u, 128u))
            return false;
        out->kind = SEAL_CAVEAT_NONCE;
        out->nonce_hex = arg->ground.sval;
        return true;
    }
    return false;
}

static bool seal_canonical_caveat(const SealParsedCaveat *cav,
                                  char *buf, size_t cap, size_t *len_out) {
    int n = -1;
    switch (cav->kind) {
    case SEAL_CAVEAT_HEAD:
        n = snprintf(buf, cap, "head|%zu:%s",
                     strlen(cav->head_name), cav->head_name);
        break;
    case SEAL_CAVEAT_OP: {
        const char *op_s = cav->op == SEAL_OP_ADD ? "add"
                         : cav->op == SEAL_OP_REMOVE ? "remove" : "write";
        n = snprintf(buf, cap, "op|%zu:%s", strlen(op_s), op_s);
        break;
    }
    case SEAL_CAVEAT_EXPIRES:
        n = snprintf(buf, cap, "expires|%" PRId64, cav->expires_ms);
        break;
    case SEAL_CAVEAT_NONCE:
        n = snprintf(buf, cap, "nonce|%zu:%s",
                     strlen(cav->nonce_hex), cav->nonce_hex);
        break;
    }
    if (n <= 0 || (size_t)n >= cap)
        return false;
    *len_out = (size_t)n;
    return true;
}

static bool seal_compute_root_mac(const uint8_t secret[32],
                                  const char *root_id,
                                  uint8_t out[32]) {
    char msg[256];
    size_t id_len = strlen(root_id);
    int n = snprintf(msg, sizeof(msg), "seal:token|v%d|%s|%zu:%s",
                     SEAL_TOKEN_VERSION_V1, SEAL_TOKEN_CARRIER_MACAROON,
                     id_len, root_id);
    if (n <= 0 || (size_t)n >= sizeof(msg))
        return false;
    seal_hmac_sha256(secret, 32u, (const uint8_t *)msg, (size_t)n, out);
    return true;
}

static bool seal_chain_caveat_mac(const uint8_t prev[32],
                                  const SealParsedCaveat *cav,
                                  uint8_t out[32]) {
    char buf[512];
    size_t len = 0u;
    if (!seal_canonical_caveat(cav, buf, sizeof(buf), &len))
        return false;
    seal_hmac_sha256(prev, 32u, (const uint8_t *)buf, len, out);
    return true;
}

typedef struct {
    const char *root_id;
    Atom *const *caveat_atoms;
    uint32_t caveat_len;
    uint8_t sig[32];
} SealParsedToken;

static bool seal_parse_wire_token(Atom *token, SealParsedToken *out,
                                  const char **reason_out) {
    memset(out, 0, sizeof(*out));
    if (!token || token->kind != ATOM_EXPR || token->expr.len != 6u ||
        !atom_is_symbol(token->expr.elems[0], "seal:token")) {
        *reason_out = "invalid-token";
        return false;
    }
    Atom *carrier = token->expr.elems[1];
    Atom *version = token->expr.elems[2];
    Atom *root_id = token->expr.elems[3];
    Atom *caveats = token->expr.elems[4];
    Atom *sig = token->expr.elems[5];
    if (!carrier || carrier->kind != ATOM_SYMBOL) {
        *reason_out = "unsupported-carrier";
        return false;
    }
    const char *cname = atom_name_cstr(carrier);
    if (!cname || strcmp(cname, SEAL_TOKEN_CARRIER_MACAROON) != 0) {
        *reason_out = "unsupported-carrier";
        return false;
    }
    if (!version || version->kind != ATOM_GROUNDED ||
        version->ground.gkind != GV_INT ||
        version->ground.ival != SEAL_TOKEN_VERSION_V1) {
        *reason_out = "unsupported-version";
        return false;
    }
    if (!root_id || root_id->kind != ATOM_GROUNDED ||
        root_id->ground.gkind != GV_STRING ||
        !root_id->ground.sval[0] || strlen(root_id->ground.sval) > 128u) {
        *reason_out = "invalid-token";
        return false;
    }
    if (!caveats || caveats->kind != ATOM_EXPR) {
        *reason_out = "invalid-token";
        return false;
    }
    if (caveats->expr.len == 0u) {
        out->caveat_atoms = NULL;
        out->caveat_len = 0u;
    } else if (atom_is_symbol(caveats->expr.elems[0], "caveats")) {
        out->caveat_atoms = caveats->expr.elems + 1u;
        out->caveat_len = caveats->expr.len - 1u;
    } else {
        *reason_out = "invalid-token";
        return false;
    }
    if (out->caveat_len > SEAL_TOKEN_MAX_CAVEATS) {
        *reason_out = "invalid-token";
        return false;
    }
    if (!sig || sig->kind != ATOM_GROUNDED ||
        sig->ground.gkind != GV_STRING ||
        !seal_hex_to_bytes(sig->ground.sval, out->sig, 32u)) {
        *reason_out = "forged-proof";
        return false;
    }
    out->root_id = root_id->ground.sval;
    return true;
}

static Atom *seal_make_wire_token(Arena *a, const char *root_id,
                                  Atom *const *caveats, uint32_t caveat_len,
                                  const uint8_t sig[32]) {
    Atom **cav_elems = arena_alloc(a, sizeof(Atom *) * (size_t)(caveat_len + 1u));
    if (!cav_elems)
        return NULL;
    cav_elems[0] = atom_symbol(a, "caveats");
    if (!cav_elems[0])
        return NULL;
    for (uint32_t i = 0u; i < caveat_len; ++i)
        cav_elems[1u + i] = caveats[i];
    Atom *cav_expr = atom_expr(a, cav_elems, caveat_len + 1u);
    char sig_hex[65];
    seal_bytes_to_hex(sig, 32u, sig_hex);
    Atom *elems[6] = {
        atom_symbol(a, "seal:token"),
        atom_symbol(a, SEAL_TOKEN_CARRIER_MACAROON),
        atom_int(a, SEAL_TOKEN_VERSION_V1),
        atom_string(a, root_id),
        cav_expr,
        atom_string(a, sig_hex),
    };
    for (size_t i = 0u; i < 6u; ++i) {
        if (!elems[i])
            return NULL;
    }
    return atom_expr(a, elems, 6u);
}

static bool seal_authority_verify_token(SealTokenAuthority *auth,
                                        Atom *token_atom,
                                        SymbolId target_head,
                                        SealWriteOp req_op,
                                        const char **reason_out) {
    SealParsedToken parsed;
    if (!seal_parse_wire_token(token_atom, &parsed, reason_out))
        return false;
    SealParsedCaveat cavs[SEAL_TOKEN_MAX_CAVEATS];
    for (uint32_t i = 0u; i < parsed.caveat_len; ++i) {
        if (!seal_parse_caveat(parsed.caveat_atoms[i], &cavs[i])) {
            *reason_out = "unsupported-caveat";
            return false;
        }
    }
    uint8_t mac[32];
    if (!seal_compute_root_mac(auth->secret, parsed.root_id, mac)) {
        *reason_out = "invalid-token";
        return false;
    }
    for (uint32_t i = 0u; i < parsed.caveat_len; ++i) {
        uint8_t next[32];
        if (!seal_chain_caveat_mac(mac, &cavs[i], next)) {
            seal_secure_zero(mac, sizeof(mac));
            *reason_out = "unsupported-caveat";
            return false;
        }
        memcpy(mac, next, 32u);
        seal_secure_zero(next, sizeof(next));
    }
    bool sig_ok = seal_ct_eq32(mac, parsed.sig);
    seal_secure_zero(mac, sizeof(mac));
    if (!sig_ok) {
        *reason_out = "forged-proof";
        return false;
    }
    int64_t now_ms = seal_now_ms_utc();
    size_t matched_nonces[SEAL_TOKEN_MAX_CAVEATS];
    uint32_t matched_nonce_count = 0u;

    pthread_mutex_lock(&auth->lock);
    for (size_t i = 0u; i < auth->revoked_len; ++i) {
        if (strcmp(auth->revoked_ids[i], parsed.root_id) == 0) {
            pthread_mutex_unlock(&auth->lock);
            *reason_out = "revoked";
            return false;
        }
    }
    for (uint32_t i = 0u; i < parsed.caveat_len; ++i) {
        const SealParsedCaveat *c = &cavs[i];
        switch (c->kind) {
        case SEAL_CAVEAT_HEAD:
            if (target_head == SYMBOL_ID_NONE || c->head_sym != target_head) {
                pthread_mutex_unlock(&auth->lock);
                *reason_out = "wrong-head";
                return false;
            }
            break;
        case SEAL_CAVEAT_OP:
            if ((c->op == SEAL_OP_ADD && req_op != SEAL_OP_ADD) ||
                (c->op == SEAL_OP_REMOVE && req_op != SEAL_OP_REMOVE)) {
                pthread_mutex_unlock(&auth->lock);
                *reason_out = "missing-right";
                return false;
            }
            break;
        case SEAL_CAVEAT_EXPIRES:
            if (now_ms > c->expires_ms) {
                pthread_mutex_unlock(&auth->lock);
                *reason_out = "expired";
                return false;
            }
            break;
        case SEAL_CAVEAT_NONCE: {
            bool found = false;
            size_t idx = 0u;
            for (size_t ni = 0u; ni < auth->nonce_len; ++ni) {
                if (strcmp(auth->nonces[ni].hex, c->nonce_hex) == 0) {
                    found = true;
                    idx = ni;
                    break;
                }
            }
            if (!found) {
                pthread_mutex_unlock(&auth->lock);
                *reason_out = "invalid-nonce";
                return false;
            }
            if (auth->nonces[idx].consumed) {
                pthread_mutex_unlock(&auth->lock);
                *reason_out = "nonce-replayed";
                return false;
            }
            for (uint32_t k = 0u; k < matched_nonce_count; ++k) {
                if (matched_nonces[k] == idx) {
                    pthread_mutex_unlock(&auth->lock);
                    *reason_out = "nonce-replayed";
                    return false;
                }
            }
            matched_nonces[matched_nonce_count++] = idx;
            break;
        }
        }
    }
    for (uint32_t k = 0u; k < matched_nonce_count; ++k)
        auth->nonces[matched_nonces[k]].consumed = true;
    pthread_mutex_unlock(&auth->lock);
    return true;
}

static bool op_now_ms(Arena *a, CettaCallOutcome *out) {
    return seal_value(out, atom_int(a, seal_now_ms_utc()));
}

static bool op_new_token_authority(Arena *a, Atom *head, Atom **args,
                                   uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 2u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    CettaSealObject *facet = cetta_seal_atom_object(args[0]);
    const char *nick = seal_text_arg(args[1]);
    if (!facet || cetta_seal_object_kind(facet) != CETTA_SEAL_OBJECT_FACET)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    if ((cetta_seal_facet_rights(facet) & CETTA_SEAL_RIGHT_SEAL) == 0u)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_MISSING_RIGHT);
    if (!nick)
        return seal_raise(out, a, head, args, nargs, "invalid");
    pthread_once(&g_service_brand_once, service_brand_init);
    if (!g_service_root)
        return seal_raise(out, a, head, args, nargs, "no-memory");
    SealTokenAuthority *auth = calloc(1u, sizeof(*auth));
    if (!auth)
        return seal_raise(out, a, head, args, nargs, "no-memory");
    if (!seal_random_bytes(auth->secret, sizeof(auth->secret))) {
        free(auth);
        return seal_raise(out, a, head, args, nargs, "entropy-unavailable");
    }
    atomic_init(&auth->holders, 1u);
    pthread_mutex_init(&auth->lock, NULL);
    cetta_seal_retain(facet);
    auth->write_facet = facet;
    CettaSealNativePayload payload = {
        .kind = &g_token_authority_kind,
        .resource = auth,
        .retain = token_authority_retain,
        .release = token_authority_release,
    };
    CettaSealObject *capsule = NULL;
    CettaSealStatus st = cetta_seal_native(g_service_root, &payload, &capsule);
    token_authority_release(auth); /* capsule owns the hold now */
    if (st != CETTA_SEAL_OK)
        return seal_raise_status(out, a, head, args, nargs, st);
    Atom *atom = seal_publish_object(a, capsule);
    return atom ? seal_value(out, atom)
                : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_token_mint(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                          CettaCallOutcome *out) {
    if (nargs != 1u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    SealTokenAuthority *auth = token_authority_from(args[0]);
    if (!auth)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    uint8_t id_bytes[16];
    if (!seal_random_bytes(id_bytes, sizeof(id_bytes)))
        return seal_raise(out, a, head, args, nargs, "entropy-unavailable");
    char root_id[33];
    seal_bytes_to_hex(id_bytes, sizeof(id_bytes), root_id);
    uint8_t sig[32];
    if (!seal_compute_root_mac(auth->secret, root_id, sig))
        return seal_raise(out, a, head, args, nargs, "invalid");
    Atom *tok = seal_make_wire_token(a, root_id, NULL, 0u, sig);
    seal_secure_zero(sig, sizeof(sig));
    return tok ? seal_value(out, tok)
               : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_token_attenuate(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 2u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    const char *reason = NULL;
    SealParsedToken parsed;
    if (!seal_parse_wire_token(args[0], &parsed, &reason))
        return seal_raise(out, a, head, args, nargs, reason);
    if (parsed.caveat_len >= SEAL_TOKEN_MAX_CAVEATS)
        return seal_raise(out, a, head, args, nargs, "capacity");
    SealParsedCaveat new_cav;
    if (!seal_parse_caveat(args[1], &new_cav))
        return seal_raise(out, a, head, args, nargs, "unsupported-caveat");
    uint8_t next_sig[32];
    if (!seal_chain_caveat_mac(parsed.sig, &new_cav, next_sig))
        return seal_raise(out, a, head, args, nargs, "unsupported-caveat");
    Atom *cavs[SEAL_TOKEN_MAX_CAVEATS];
    for (uint32_t i = 0u; i < parsed.caveat_len; ++i)
        cavs[i] = parsed.caveat_atoms[i];
    cavs[parsed.caveat_len] = args[1];
    Atom *tok = seal_make_wire_token(
        a, parsed.root_id, cavs, parsed.caveat_len + 1u, next_sig);
    seal_secure_zero(next_sig, sizeof(next_sig));
    return tok ? seal_value(out, tok)
               : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_token_challenge(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 1u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    SealTokenAuthority *auth = token_authority_from(args[0]);
    if (!auth)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    uint8_t rnd[16];
    if (!seal_random_bytes(rnd, sizeof(rnd)))
        return seal_raise(out, a, head, args, nargs, "entropy-unavailable");
    char hex[33];
    seal_bytes_to_hex(rnd, sizeof(rnd), hex);
    pthread_mutex_lock(&auth->lock);
    if (auth->nonce_len == auth->nonce_cap) {
        size_t cap = auth->nonce_cap ? auth->nonce_cap * 2u : 16u;
        SealChallengeNonce *next = realloc(auth->nonces, cap * sizeof(*next));
        if (!next) {
            pthread_mutex_unlock(&auth->lock);
            return seal_raise(out, a, head, args, nargs, "no-memory");
        }
        auth->nonces = next;
        auth->nonce_cap = cap;
    }
    SealChallengeNonce *slot = &auth->nonces[auth->nonce_len++];
    memcpy(slot->hex, hex, sizeof(slot->hex));
    slot->consumed = false;
    pthread_mutex_unlock(&auth->lock);
    Atom *atom = atom_string(a, hex);
    return atom ? seal_value(out, atom)
                : seal_raise(out, a, head, args, nargs, "no-memory");
}

static bool op_token_revoke(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                            CettaCallOutcome *out) {
    if (nargs != 2u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    SealTokenAuthority *auth = token_authority_from(args[0]);
    if (!auth)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    const char *root_id = NULL;
    if (args[1] && args[1]->kind == ATOM_GROUNDED &&
        args[1]->ground.gkind == GV_STRING) {
        root_id = args[1]->ground.sval;
    } else if (args[1] && args[1]->kind == ATOM_EXPR && args[1]->expr.len == 6u &&
               atom_is_symbol(args[1]->expr.elems[0], "seal:token") &&
               args[1]->expr.elems[3] &&
               args[1]->expr.elems[3]->kind == ATOM_GROUNDED &&
               args[1]->expr.elems[3]->ground.gkind == GV_STRING) {
        root_id = args[1]->expr.elems[3]->ground.sval;
    }
    if (!root_id || !root_id[0])
        return seal_raise(out, a, head, args, nargs, "invalid");
    pthread_mutex_lock(&auth->lock);
    for (size_t i = 0u; i < auth->revoked_len; ++i) {
        if (strcmp(auth->revoked_ids[i], root_id) == 0) {
            pthread_mutex_unlock(&auth->lock);
            return seal_value(out, atom_unit(a));
        }
    }
    if (auth->revoked_len == auth->revoked_cap) {
        size_t cap = auth->revoked_cap ? auth->revoked_cap * 2u : 8u;
        char **next = realloc(auth->revoked_ids, cap * sizeof(*next));
        if (!next) {
            pthread_mutex_unlock(&auth->lock);
            return seal_raise(out, a, head, args, nargs, "no-memory");
        }
        auth->revoked_ids = next;
        auth->revoked_cap = cap;
    }
    size_t len = strlen(root_id);
    char *copy = malloc(len + 1u);
    if (!copy) {
        pthread_mutex_unlock(&auth->lock);
        return seal_raise(out, a, head, args, nargs, "no-memory");
    }
    memcpy(copy, root_id, len + 1u);
    auth->revoked_ids[auth->revoked_len++] = copy;
    pthread_mutex_unlock(&auth->lock);
    return seal_value(out, atom_unit(a));
}

static bool op_token_verify(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                            CettaCallOutcome *out) {
    if (nargs != 4u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    SealTokenAuthority *auth = token_authority_from(args[0]);
    if (!auth)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    if (!args[2] || args[2]->kind != ATOM_SYMBOL)
        return seal_raise(out, a, head, args, nargs, "invalid");
    SealWriteOp req_op;
    if (!seal_parse_op_symbol(args[3], &req_op) || req_op == SEAL_OP_WRITE)
        return seal_raise(out, a, head, args, nargs, "invalid");
    const char *reason = NULL;
    if (!seal_authority_verify_token(auth, args[1], args[2]->sym_id,
                                     req_op, &reason))
        return seal_raise(out, a, head, args, nargs,
                          reason ? reason : "forged-proof");
    return seal_value(out, atom_unit(a));
}

static bool op_token_head_write(Space *current, Arena *a, Atom *head,
                                Atom **args, uint32_t nargs,
                                CettaCallOutcome *out, bool is_remove) {
    if (nargs != 4u)
        return seal_raise(out, a, head, args, nargs, "invalid");
    SealTokenAuthority *auth = token_authority_from(args[0]);
    if (!auth)
        return seal_raise_status(out, a, head, args, nargs,
                                 CETTA_SEAL_WRONG_FACET);
    Space *target = seal_resolve_space(current, args[2]);
    if (!target || !args[3])
        return seal_raise(out, a, head, args, nargs, "invalid");
    SymbolId target_head = SYMBOL_ID_NONE;
    (void)space_theory_classify_atom(args[3], &target_head, NULL);
    const char *reason = NULL;
    if (!seal_authority_verify_token(
            auth, args[1], target_head,
            is_remove ? SEAL_OP_REMOVE : SEAL_OP_ADD, &reason))
        return seal_raise(out, a, head, args, nargs,
                          reason ? reason : "forged-proof");
    Atom *elems[5] = {head, args[0], args[1], args[2], args[3]};
    Atom *call = atom_expr(a, elems, 5u);
    if (!call)
        return seal_raise(out, a, head, args, nargs, "no-memory");
    const char *seal_reason = NULL;
    Atom *err = NULL;
    bool ok = is_remove
        ? (eval_authorized_head_space_remove &&
           eval_authorized_head_space_remove(
               target, a, call, args[3], auth->write_facet, &seal_reason, &err))
        : (eval_authorized_head_space_add &&
           eval_authorized_head_space_add(
               target, a, call, args[3], auth->write_facet, &seal_reason, &err));
    if (!ok) {
        if (seal_reason)
            return seal_raise(out, a, head, args, nargs, seal_reason);
        if (err) {
            *out = cetta_call_raised(err);
            return true;
        }
        return seal_raise(out, a, head, args, nargs, "invalid");
    }
    return seal_value(out, atom_unit(a));
}

static bool seal_dispatch(const char *op, Space *space, Arena *a, Atom *head,
                          Atom **args, uint32_t nargs, CettaCallOutcome *out) {
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
    if (strcmp(op, "seal-head") == 0) return op_seal_head(space, a, head, args, nargs, out);
    if (strcmp(op, "head-sealed?") == 0) return op_head_sealed(space, a, head, args, nargs, out);
    if (strcmp(op, "head-add") == 0) return op_head_write(space, a, head, args, nargs, out, false);
    if (strcmp(op, "head-remove") == 0) return op_head_write(space, a, head, args, nargs, out, true);
    if (strcmp(op, "now-ms") == 0) return op_now_ms(a, out);
    if (strcmp(op, "new-token-authority") == 0) return op_new_token_authority(a, head, args, nargs, out);
    if (strcmp(op, "token-mint") == 0) return op_token_mint(a, head, args, nargs, out);
    if (strcmp(op, "token-attenuate") == 0) return op_token_attenuate(a, head, args, nargs, out);
    if (strcmp(op, "token-challenge") == 0) return op_token_challenge(a, head, args, nargs, out);
    if (strcmp(op, "token-revoke") == 0) return op_token_revoke(a, head, args, nargs, out);
    if (strcmp(op, "token-verify") == 0) return op_token_verify(a, head, args, nargs, out);
    if (strcmp(op, "token-head-add") == 0) return op_token_head_write(space, a, head, args, nargs, out, false);
    if (strcmp(op, "token-head-remove") == 0) return op_token_head_write(space, a, head, args, nargs, out, true);
    return false;
}

bool cetta_seal_module_call(struct CettaLibraryContext *ctx, Space *space,
                            Arena *a, Atom *head, Atom **args, uint32_t nargs,
                            CettaCallOutcome *out) {
    (void)ctx;
    if (!head || head->kind != ATOM_SYMBOL || !a || !out)
        return false;
    bool soft;
    const char *op = seal_op_of(head, &soft);
    if (!op || !seal_dispatch(op, space, a, head, args, nargs, out))
        return false;
    /* a try- form: the fault is the call's value */
    if (soft && out->kind == CETTA_CALL_RAISED)
        *out = cetta_call_completed_value(out->term);
    return true;
}
