#include "native/interaction_core.h"

#include "eval.h"
#include "library.h"
#include "match.h"
#include "seal.h"
#include "shared_transition.h"

#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * The interaction engine: the kernel of the `interaction` library
 * (lib/prime/interaction.metta, bound by native/interaction_module.c) and
 * the one commit engine of every lane (native/interaction_core.h).
 *
 * One interaction is one commit.  A commit takes some occurrences, keeps
 * some, queries some regions and makes new occurrences; it is enabled
 * when what it takes and keeps is present, nothing is both taken and kept,
 * and its predicate queries still hold.  This file is the part of that machine a
 * program cannot bypass:
 *
 *   regions   a region is a space with a profile: which rights (facets)
 *             its holder has and which it may hand to others.  Facets are
 *             receive (take), peek (keep), send (make) and observe (full
 *             read).  A region's space is made fresh and reached only
 *             through its handles; no name gives a right over it.  A plain
 *             space has every right.
 *   handles   the holder's and the attenuated handles are capsules of a
 *             brand private to this file: opaque, unforgeable, matched by
 *             identity.  Authority travels with them.  Whether two handles
 *             name one region is the only identity a program sees
 *             (same-region); a handle with no rights is a bare name.
 *   walk      the sources of a transform, matched against the live store:
 *             each answer picks occurrences by their position at a revision
 *             of their space, so a row is linear by construction (nothing
 *             taken twice, nothing both taken and kept).  Rights are checked
 *             where each source reads.  The walk stops at a `let` (code runs
 *             in the library, never here) and at `every`.
 *   commit    Enabled, then fire: every region the transform scanned is
 *             still at the revision it was read at, the footprint is linear
 *             and permitted, then the takes leave by position and the makes
 *             arrive.  Nothing else changes the store.
 *
 * The kernel runs no code.  Values read from the store are substituted into
 * the continuation of a template as held data (atom_prime_held_wrap), so
 * reading stored code never runs it; `eval` runs it.
 *
 * Results are data:
 *   (ia:walked (ia:seen (LOC REV) ...) RESULT ...), RESULT one of
 *     (ia:row TEMPLATE FOOTPRINT)
 *     (ia:let PATTERN EXPRESSION STATE)
 *     (ia:every SUFFIX-STATE BASE-FOOTPRINT)
 *     (ia:absent QUERY-STATE CONTINUATION-STATE)
 *     (ia:refused REASON)
 *     (ia:stuck SOURCE)
 *   STATE = (ia:state SOURCES TEMPLATE FOOTPRINT BASE)
 *   FOOTPRINT entries: (take LOC REV POS ATOM) (keep LOC REV POS ATOM)
 *                      (full LOC REV PATTERN COUNT)
 */

/* The lane of the running firing: how values reach code (held data on the
 * Prime lane, quoted on the PeTTa lane, as they are when the caller has
 * protected its code), and the host of a cetta_interaction_fire call. */
static _Thread_local CettaInteractionLane g_ia_lane = CETTA_INTERACTION_LANE_PRIME;
static _Thread_local bool g_ia_raw;
static _Thread_local const CettaInteractionHost *g_ia_host;

enum {
    IA_RECEIVE = 1u,
    IA_PEEK = 2u,
    IA_SEND = 4u,
    IA_OBSERVE = 8u,
    IA_ALL = 15u,
};

typedef struct {
    const char *name;
    uint32_t holder;    /* the region holder's facets */
    uint32_t delegable; /* the most a handed-out facet may carry */
} IaProfile;

static const IaProfile g_profiles[] = {
    {"space", IA_ALL, IA_ALL},
    {"channel", IA_RECEIVE | IA_PEEK | IA_SEND, IA_RECEIVE | IA_PEEK | IA_SEND},
    {"mailbox", IA_ALL, IA_SEND},
    {"log", IA_PEEK | IA_SEND | IA_OBSERVE, IA_PEEK | IA_SEND},
    {"board", IA_SEND | IA_PEEK | IA_OBSERVE, IA_PEEK},
};

static const IaProfile *ia_profile_named(const char *name) {
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof(g_profiles) / sizeof(g_profiles[0]); i++)
        if (strcmp(g_profiles[i].name, name) == 0)
            return &g_profiles[i];
    return NULL;
}

static uint32_t ia_profile_allowed(const IaProfile *p) {
    return p ? (p->holder | p->delegable) : IA_ALL;
}

/* ── the region table ─────────────────────────────────────────────────── */

typedef struct {
    Space *space;
    const IaProfile *profile; /* NULL: a plain space, every right */
    uint64_t key;
    /* traces only: the occurrence identity of each row, in row order, and
     * a copy of its atom */
    uint64_t *ids;
    Atom **atoms;
    size_t len, cap;
    bool shadowed;
} IaRegion;

static pthread_mutex_t g_regions_gate = PTHREAD_MUTEX_INITIALIZER;
static IaRegion **g_regions;
static size_t g_region_len, g_region_cap;
static uint64_t g_next_key = 1u;

/* The record of `space`, made on first sight.  Never freed: a region's
 * record lives as long as the session (its space is session storage). */
static IaRegion *ia_region_of(Space *space, bool create) {
    if (!space)
        return NULL;
    IaRegion *found = NULL;
    pthread_mutex_lock(&g_regions_gate);
    for (size_t i = 0; i < g_region_len && !found; i++)
        if (g_regions[i]->space == space)
            found = g_regions[i];
    if (!found && create) {
        if (g_region_len == g_region_cap) {
            size_t cap = g_region_cap ? g_region_cap * 2u : 64u;
            IaRegion **items = realloc(g_regions, cap * sizeof(*items));
            if (items) {
                g_regions = items;
                g_region_cap = cap;
            }
        }
        if (g_region_len < g_region_cap) {
            found = calloc(1u, sizeof(*found));
            if (found) {
                found->space = space;
                found->key = g_next_key++;
                g_regions[g_region_len++] = found;
            }
        }
    }
    pthread_mutex_unlock(&g_regions_gate);
    return found;
}

/* ── handles ──────────────────────────────────────────────────────────── */

typedef struct {
    atomic_uint holders;
    IaRegion *region;
    uint32_t rights;
} IaHandle;

static const char g_handle_kind = 'h';
static CettaSealBrandCell g_brand = CETTA_SEAL_BRAND_CELL_INIT("region");

static void ia_handle_retain(void *resource) {
    IaHandle *h = resource;
    atomic_fetch_add(&h->holders, 1u);
}

static void ia_handle_release(void *resource) {
    IaHandle *h = resource;
    if (h && atomic_fetch_sub(&h->holders, 1u) == 1u)
        free(h);
}

static CettaSealObject *ia_root(void) {
    return cetta_seal_brand_get(&g_brand, NULL);
}

static Atom *ia_handle_atom(Arena *a, IaRegion *region, uint32_t rights) {
    CettaSealObject *root = ia_root();
    IaHandle *h = calloc(1u, sizeof(*h));
    if (!root || !h) {
        free(h);
        return NULL;
    }
    atomic_init(&h->holders, 1u);
    h->region = region;
    h->rights = rights;
    CettaSealNativePayload payload = {
        .kind = &g_handle_kind,
        .resource = h,
        .retain = ia_handle_retain,
        .release = ia_handle_release,
    };
    CettaSealObject *capsule = NULL;
    CettaSealStatus status = cetta_seal_native(root, &payload, &capsule);
    ia_handle_release(h); /* the capsule holds it now */
    if (status != CETTA_SEAL_OK)
        return NULL;
    Atom *atom = cetta_seal_object_atom(a, capsule);
    cetta_seal_release(capsule);
    return atom;
}

static IaHandle *ia_handle_of(Atom *atom) {
    CettaSealObject *root = ia_root();
    CettaSealObject *capsule = atom ? cetta_seal_atom_object(atom) : NULL;
    void *resource = NULL;
    if (!root || !capsule ||
        cetta_seal_unseal_native(root, capsule, &g_handle_kind, &resource) !=
            CETTA_SEAL_OK)
        return NULL;
    return resource;
}

/* ── rights as data ───────────────────────────────────────────────────── */

static const struct { const char *name; uint32_t bit; } g_rights[] = {
    {"receive", IA_RECEIVE}, {"peek", IA_PEEK},
    {"send", IA_SEND}, {"observe", IA_OBSERVE},
};

static const char *ia_right_name(uint32_t bit) {
    for (size_t i = 0; i < 4u; i++)
        if (g_rights[i].bit == bit)
            return g_rights[i].name;
    return "none";
}

static bool ia_rights_from(Arena *a, Atom *list, uint32_t *out) {
    *out = 0u;
    if (!list)
        return false;
    if (atom_contains_prime_held(list))
        list = atom_prime_held_strip(a, list);
    if (!list) return false;
    if (list->kind == ATOM_SYMBOL) {
        for (size_t j = 0; j < 4u; j++)
            if (atom_is_symbol(list, g_rights[j].name)) {
                *out = g_rights[j].bit;
                return true;
            }
        return false;
    }
    if (list->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0; i < list->expr.len; i++) {
        bool known = false;
        for (size_t j = 0; j < 4u; j++)
            if (atom_is_symbol(list->expr.elems[i], g_rights[j].name)) {
                *out |= g_rights[j].bit;
                known = true;
            }
        if (!known)
            return false;
    }
    return true;
}

static Atom *ia_rights_atom(Arena *a, uint32_t rights) {
    Atom *items[4];
    CettaExprLen n = 0;
    for (size_t j = 0; j < 4u; j++)
        if (rights & g_rights[j].bit)
            items[n++] = atom_symbol(a, g_rights[j].name);
    return atom_expr(a, items, n);
}

/* ── locations ────────────────────────────────────────────────────────── */

typedef struct {
    Space *space;
    IaRegion *region;
    uint32_t rights;
    Atom *atom; /* as it was named, for the trace and the commit */
} IaLoc;

static Atom *ia_strip(Arena *a, Atom *x) {
    return x && atom_contains_prime_held(x) ? atom_prime_held_strip(a, x) : x;
}

/* NULL when `loc` names a region; else the reason it does not. */
static Atom *ia_resolve(Arena *a, Atom *loc, IaLoc *out) {
    memset(out, 0, sizeof(*out));
    loc = ia_strip(a, loc);
    out->atom = loc;
    if (!loc)
        return atom_symbol(a, "no-location");
    if (loc->kind == ATOM_VAR)
        return atom_expr2(a, atom_symbol(a, "unbound-location"), loc);
    IaHandle *h = ia_handle_of(loc);
    if (h) {
        out->space = h->region->space;
        out->region = h->region;
        out->rights = h->rights;
        return NULL;
    }
    Space *space = NULL;
    if (g_ia_host && g_ia_host->resolve_space) {
        if (!g_ia_host->resolve_space(g_ia_host->context, loc, &space))
            return atom_expr2(a, atom_symbol(a, "not-a-location"), loc);
        if (!space)
            return atom_expr2(a, atom_symbol(a, "empty-location"), loc);
    } else if (loc->kind == ATOM_GROUNDED && loc->ground.gkind == GV_SPACE)
        space = (Space *)loc->ground.ptr;
    else if (loc->kind == ATOM_SYMBOL)
        space = resolve_space(eval_current_registry(), loc);
    if (!space)
        return atom_expr2(a, atom_symbol(a, "not-a-location"), loc);
    out->space = space;
    out->region = ia_region_of(space, true);
    if (!out->region)
        return atom_symbol(a, "no-memory");
    /* Naming is not authority.  A region is reached only through its
     * handles: its space is made fresh and never named, and should a name
     * or a space atom reach it some other way, it gives no right.  A plain
     * space has every right. */
    if (out->region->profile)
        return atom_expr2(a, atom_symbol(a, "region-needs-handle"), loc);
    out->rights = IA_ALL;
    return NULL;
}

static Atom *ia_missing(Arena *a, uint32_t right, IaLoc *loc) {
    return atom_expr3(a, atom_symbol(a, "missing-right"),
                      atom_symbol(a, ia_right_name(right)), loc->atom);
}

static Atom *ia_check_right(Arena *a, IaLoc *loc, uint32_t needed) {
    for (size_t j = 0; j < 4u; j++)
        if ((needed & g_rights[j].bit) && !(loc->rights & g_rights[j].bit))
            return ia_missing(a, g_rights[j].bit, loc);
    return NULL;
}


/* ── traces for the verified checker ──────────────────────────────────── */

/* CETTA_INTERACTION_TRACE=FILE writes the run as one trace tree in the
 * format of the occurrence-commit trace checker: every commit with what it
 * took, kept and made (by occurrence identity, never reused), the regions
 * it read in full with the identities seen, and the rows of an `every`.
 * A change made outside the kernel (add-atom, remove-atom) is written as
 * its own commit, a make or a take, before the next kernel commit. */

static FILE *g_trace;
static bool g_trace_checked;
static uint64_t g_trace_next_id;
static uint64_t g_trace_edge;
static Arena g_trace_store;

typedef struct {
    VarId *vars;
    size_t len, cap;
} IaVarMap;

static void ia_trace_atom(FILE *f, Atom *x, IaVarMap *vm);

static void ia_trace_region_name(FILE *f, IaRegion *r) {
    fprintf(f, "(nm %llu)", (unsigned long long)r->key);
}

static void ia_trace_atom(FILE *f, Atom *x, IaVarMap *vm) {
    if (!x) {
        fputs("()", f);
        return;
    }
    if (atom_prime_held_is(x))
        x = atom_prime_held_payload(x);
    switch (x->kind) {
    case ATOM_SYMBOL:
        fputs(atom_name_cstr(x), f);
        return;
    case ATOM_VAR: {
        size_t i = 0;
        while (i < vm->len && !var_id_eq(vm->vars[i], x->var_id))
            i++;
        if (i == vm->len) {
            if (vm->len == vm->cap) {
                size_t c = vm->cap ? vm->cap * 2u : 8u;
                VarId *v = realloc(vm->vars, c * sizeof(*v));
                if (!v) {
                    fputs("()", f);
                    return;
                }
                vm->vars = v;
                vm->cap = c;
            }
            vm->vars[vm->len++] = x->var_id;
        }
        fprintf(f, "(pv %zu)", i);
        return;
    }
    case ATOM_EXPR:
        fputc('(', f);
        for (CettaExprIndex i = 0; i < x->expr.len; i++) {
            if (i)
                fputc(' ', f);
            ia_trace_atom(f, x->expr.elems[i], vm);
        }
        fputc(')', f);
        return;
    default: {
        IaHandle *h = ia_handle_of(x);
        if (h) {
            ia_trace_region_name(f, h->region);
            return;
        }
        if (x->kind == ATOM_GROUNDED && x->ground.gkind == GV_SPACE) {
            IaRegion *r = ia_region_of((Space *)x->ground.ptr, true);
            if (r) {
                ia_trace_region_name(f, r);
                return;
            }
        }
        if (x->kind == ATOM_GROUNDED && x->ground.gkind == GV_INT) {
            fprintf(f, "%lld", (long long)x->ground.ival);
            return;
        }
        if (x->kind == ATOM_GROUNDED && x->ground.gkind == GV_BOOL) {
            fputs(x->ground.bval ? "True" : "False", f);
            return;
        }
        fputs("ia-grounded", f);
        return;
    }
    }
}

static void ia_trace_finish(void);

static bool ia_trace_on(void) {
    if (!g_trace_checked) {
        g_trace_checked = true;
        const char *path = getenv("CETTA_INTERACTION_TRACE");
        if (path && *path) {
            g_trace = fopen(path, "w");
            if (g_trace) {
                arena_init_detached(&g_trace_store);
                fputs("(tree (run 1) interaction serial (init ()) (track no))\n", g_trace);
                fflush(g_trace);
                atexit(ia_trace_finish);
            }
        }
    }
    return g_trace != NULL;
}

/* One edge: a commit. */
static uint64_t g_trace_mismatch;

/* At exit: the store the run reached, by identity, for the checker to
 * compare with the store its own firing computed. */
static void ia_trace_finish(void) {
    if (!g_trace)
        return;
    fprintf(g_trace, "(leaf %llu (outcome () (kernel-mismatch %llu)) (store (",
            (unsigned long long)g_trace_edge, (unsigned long long)g_trace_mismatch);
    bool first = true;
    for (size_t i = 0; i < g_region_len; i++) {
        IaRegion *r = g_regions[i];
        for (size_t k = 0; r->shadowed && k < r->len; k++) {
            fprintf(g_trace, first ? "%llu" : " %llu", (unsigned long long)r->ids[k]);
            first = false;
        }
    }
    fputs(")))\n", g_trace);
    fprintf(g_trace, "(tree-end (run 1) complete %llu)\n", (unsigned long long)g_trace_edge);
    fclose(g_trace);
    g_trace = NULL;
    if (g_trace_mismatch)
        fprintf(stderr, "interaction trace: %llu kernel mismatches\n",
                (unsigned long long)g_trace_mismatch);
}

static void ia_trace_edge_begin(void) {
    fprintf(g_trace, "(edge %llu %llu (fire (c ",
            (unsigned long long)(g_trace_edge + 1u),
            (unsigned long long)g_trace_edge);
    g_trace_edge++;
}

static void ia_trace_edge_end(void) {
    fputs(") ()))\n", g_trace);
    fflush(g_trace);
}

static bool ia_shadow_push(IaRegion *r, uint64_t id, Atom *atom) {
    if (r->len == r->cap) {
        size_t c = r->cap ? r->cap * 2u : 64u;
        uint64_t *ids = realloc(r->ids, c * sizeof(*ids));
        Atom **atoms = realloc(r->atoms, c * sizeof(*atoms));
        if (ids) r->ids = ids;
        if (atoms) r->atoms = atoms;
        if (!ids || !atoms)
            return false;
        r->cap = c;
    }
    r->ids[r->len] = id;
    r->atoms[r->len] = atom_deep_copy(&g_trace_store, atom);
    r->len++;
    return true;
}

typedef struct {
    uint64_t id;
    Atom *atom;
    bool used;
} IaPoolItem;

/* Align the shadow of `r` with its rows.  `made` are the identities and
 * atoms a kernel commit just made there.  Rows no identity accounts for
 * were made outside the kernel, identities no row accounts for were taken
 * outside it: both are written as one outside commit. */
static bool g_trace_after_kernel;

static void ia_shadow_sync(IaRegion *r, const uint64_t *made_ids,
                           Atom *const *made_atoms, size_t made_len) {
    Space *space = r->space;
    size_t pool_len = r->len + made_len;
    IaPoolItem *pool = pool_len ? calloc(pool_len, sizeof(*pool)) : NULL;
    for (size_t i = 0; i < r->len; i++)
        pool[i] = (IaPoolItem){r->ids[i], r->atoms[i], false};
    for (size_t i = 0; i < made_len; i++)
        pool[r->len + i] = (IaPoolItem){made_ids[i], made_atoms[i], false};
    CettaCount len = space_length64(space);
    uint64_t *ids = len ? calloc((size_t)len, sizeof(*ids)) : NULL;
    bool *fresh = len ? calloc((size_t)len, sizeof(*fresh)) : NULL;
    size_t outside_made = 0u;
    for (CettaIndex i = 0; i < len; i++) {
        Atom *row = space_get_at64(space, i);
        size_t k = 0;
        while (k < pool_len && (pool[k].used || !atom_eq(pool[k].atom, row)))
            k++;
        if (k < pool_len) {
            pool[k].used = true;
            ids[i] = pool[k].id;
        } else {
            ids[i] = g_trace_next_id++;
            fresh[i] = true;
            outside_made++;
        }
    }
    size_t outside_taken = 0u;
    for (size_t k = 0; k < r->len; k++)
        if (!pool[k].used)
            outside_taken++;
    if ((outside_made || outside_taken) && g_trace_after_kernel)
        g_trace_mismatch++;
    if (outside_made || outside_taken) {
        ia_trace_edge_begin();
        IaVarMap vm = {0};
        fputc('(', g_trace);
        bool first = true;
        for (size_t k = 0; k < r->len; k++)
            if (!pool[k].used) {
                fprintf(g_trace, first ? "%llu" : " %llu", (unsigned long long)pool[k].id);
                first = false;
            }
        fputs(") () (", g_trace);
        first = true;
        for (CettaIndex i = 0; i < len; i++)
            if (fresh[i]) {
                fprintf(g_trace, first ? "(%llu " : " (%llu ", (unsigned long long)ids[i]);
                ia_trace_region_name(g_trace, r);
                fputc(' ', g_trace);
                ia_trace_atom(g_trace, space_get_at64(space, i), &vm);
                fputc(')', g_trace);
                first = false;
            }
        fputs(") () ()", g_trace);
        free(vm.vars);
        ia_trace_edge_end();
    }
    r->len = 0u;
    for (CettaIndex i = 0; i < len; i++)
        (void)ia_shadow_push(r, ids[i], space_get_at64(space, i));
    r->shadowed = true;
    free(pool);
    free(ids);
    free(fresh);
}

static uint64_t ia_shadow_id(IaRegion *r, CettaIndex pos) {
    return pos < r->len ? r->ids[pos] : UINT64_MAX;
}

/* The identities of the rows of `r` that `pattern` matches, now. */
static void ia_trace_seen(IaRegion *r, Atom *pattern, Arena *a) {
    Space *space = r->space;
    CettaCount len = space_length64(space);
    bool first = true;
    for (CettaIndex i = 0; i < len; i++) {
        Atom *row = space_get_at64(space, i);
        CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
        CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
        if (!cetta_frame_identity_scope_try(&frame_identity_scope, &epoch))
            break;
        Bindings b;
        bindings_init(&b);
        if (row && match_atoms_epoch(pattern, row, &b, a, epoch)) {
            fprintf(g_trace, first ? "%llu" : " %llu",
                    (unsigned long long)ia_shadow_id(r, i));
            first = false;
        }
        bindings_free(&b);
    }
}

/* ── growable atom lists ──────────────────────────────────────────────── */

typedef struct {
    Atom **items;
    size_t len, cap;
} IaList;

static bool ia_push(IaList *l, Atom *x) {
    if (!x)
        return false;
    if (l->len == l->cap) {
        size_t cap = l->cap ? l->cap * 2u : 16u;
        Atom **items = realloc(l->items, cap * sizeof(*items));
        if (!items)
            return false;
        l->items = items;
        l->cap = cap;
    }
    l->items[l->len++] = x;
    return true;
}

static Atom *ia_list_atom(Arena *a, IaList *l) {
    return atom_expr(a, l->items, (CettaExprLen)l->len);
}

/* ── substitution ─────────────────────────────────────────────────────── */

/* How values reach code: held data on the Prime lane; quoted on the PeTTa
 * lane, as lib_transform quotes them.  Set for each machine run. */

static Atom *ia_hold(Arena *a, Atom *v) {
    if (g_ia_raw)
        return v;
    if (g_ia_lane == CETTA_INTERACTION_LANE_PETTA)
        return atom_expr2(a, atom_symbol(a, "quote"), v);
    return atom_prime_held_wrap(a, v);
}

/* `t` under `b`, with every expression value of a bound variable held:
 * code positions receive data. */
static Atom *ia_apply_held(Bindings *b, Arena *a, Atom *t) {
    if (!t)
        return NULL;
    if (t->kind == ATOM_VAR) {
        Atom *v = bindings_apply(b, a, t);
        if (v && v != t && v->kind == ATOM_EXPR && v->expr.len > 0u &&
            !atom_prime_held_is(v))
            return ia_hold(a, v);
        return v;
    }
    if (t->kind != ATOM_EXPR || t->expr.len == 0u || !atom_has_vars(t))
        return t;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * t->expr.len);
    bool changed = false;
    for (CettaExprIndex i = 0; i < t->expr.len; i++) {
        elems[i] = ia_apply_held(b, a, t->expr.elems[i]);
        if (!elems[i])
            return NULL;
        changed = changed || elems[i] != t->expr.elems[i];
    }
    return changed ? atom_expr(a, elems, t->expr.len) : t;
}

static bool ia_head(Atom *x, const char *word, CettaExprLen len) {
    return x && x->kind == ATOM_EXPR && x->expr.len == len &&
           atom_is_symbol(x->expr.elems[0], word);
}

static bool ia_head_any(Atom *x, const char *word) {
    return x && x->kind == ATOM_EXPR && x->expr.len >= 1u &&
           atom_is_symbol(x->expr.elems[0], word);
}

/* A source under `b`: patterns and locations receive values, the code of a
 * `let` receives held values. */
static Atom *ia_subst_source(Bindings *b, Arena *a, Atom *c) {
    if (ia_head(c, "let", 3u)) {
        Atom *p = bindings_apply(b, a, c->expr.elems[1]);
        Atom *e = ia_apply_held(b, a, c->expr.elems[2]);
        return p && e ? atom_expr3(a, c->expr.elems[0], p, e) : NULL;
    }
    if ((ia_head_any(c, ",") || ia_head(c, "every", 2u)) && atom_has_vars(c)) {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * c->expr.len);
        elems[0] = c->expr.elems[0];
        for (CettaExprIndex i = 1; i < c->expr.len; i++)
            if (!(elems[i] = ia_subst_source(b, a, c->expr.elems[i])))
                return NULL;
        return atom_expr(a, elems, c->expr.len);
    }
    return bindings_apply(b, a, c);
}

/* A template item: an emission `(add-atom LOC ATOM)` receives values (it
 * is stored data); any other item is code and receives held values. */
static Atom *ia_subst_item(Bindings *b, Arena *a, Atom *t) {
    if (ia_head(t, "add-atom", 3u))
        return bindings_apply(b, a, t);
    return ia_apply_held(b, a, t);
}

static Atom *ia_subst_template(Bindings *b, Arena *a, Atom *t) {
    if (ia_head_any(t, ",")) {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * t->expr.len);
        elems[0] = t->expr.elems[0];
        for (CettaExprIndex i = 1; i < t->expr.len; i++)
            if (!(elems[i] = ia_subst_item(b, a, t->expr.elems[i])))
                return NULL;
        return atom_expr(a, elems, t->expr.len);
    }
    return ia_subst_item(b, a, t);
}

/* ── the walk ─────────────────────────────────────────────────────────── */

typedef struct {
    Arena *a;
    Atom *default_loc;
    IaList results;
    IaList seen;      /* (LOC REV), one per region scanned */
    IaList seen_keys; /* the regions' keys, to keep `seen` one per region */
    IaList scans;     /* (LOC REV PATTERN), sampled when each scan begins */
    bool record_scans;
    bool unfiltered;  /* a negative query observes matches before linear admission */
    bool first;       /* stop at the first result: a choice of rows */
    bool truncated;   /* stopped with branches left */
    bool failed;      /* allocation */
    Atom *refused;
    unsigned depth;
} IaWalk;

static void ia_walk_free(IaWalk *w) {
    free(w->results.items);
    free(w->seen.items);
    free(w->seen_keys.items);
    free(w->scans.items);
}

static void ia_note_seen(IaWalk *w, IaLoc *loc) {
    Atom *key = atom_int(w->a, (int64_t)loc->region->key);
    for (size_t i = 0; i < w->seen_keys.len; i++)
        if (atom_eq(w->seen_keys.items[i], key))
            return;
    if (!ia_push(&w->seen_keys, key) ||
        !ia_push(&w->seen, atom_expr2(w->a, loc->atom,
                     atom_int(w->a, (int64_t)space_revision(loc->space)))))
        w->failed = true;
}

/* The conjuncts of SOURCES: () is none, (, c ...) its children, else one
 * (kept in *one). */
static void ia_conjuncts(Atom *sources, Atom **one, Atom ***out, size_t *len) {
    *out = NULL;
    *len = 0u;
    if (!sources || (sources->kind == ATOM_EXPR && sources->expr.len == 0u))
        return;
    if (ia_head_any(sources, ",")) {
        *out = sources->expr.elems + 1;
        *len = sources->expr.len - 1u;
        return;
    }
    *one = sources;
    *out = one;
    *len = 1u;
}

/* A held value's syntax, one level: the library passes what a Data
 * position received. */
static Atom *ia_unhold(Atom *x) {
    return x && atom_prime_held_is(x) ? atom_prime_held_payload(x) : x;
}

/* A firing as the kernel reads it.  A crossing set written on a transform or
 * a choose, X{$x ...}, says which names the form shares with the scope
 * around; the reader has applied it when the form was read, and the braces
 * stay as written structure.  The kernel reads the form inside them, as the
 * evaluator does. */
static Atom *ia_firing(Atom *x) {
    x = ia_unhold(x);
    while (x && atom_is_prime_meta(x) && atom_is_prime_braces(x->expr.elems[2]))
        x = ia_unhold(x->expr.elems[1]);
    return x;
}

/* Whether a source is written as a source form (and not a plain pattern). */
static bool ia_source_form(Atom *x) {
    return (x && x->kind == ATOM_EXPR && x->expr.len == 0u) ||
           ia_head_any(x, ",") || ia_head(x, "let", 3u) ||
           ia_head(x, "every", 2u) || ia_head(x, "read", 2u) ||
           ia_head(x, "read", 3u) || ia_head(x, "take", 2u) ||
           ia_head(x, "take", 3u) || ia_head(x, "absent", 2u) ||
           ia_head(x, "absent", 3u) || ia_head_any(x, "unique") ||
           ia_head_any(x, "all");
}

typedef enum { IA_USE_KEEP, IA_USE_TAKE } IaUse;

/* A matched source: (read [R] p), (take [R] p), or a plain pattern. */
typedef struct {
    IaUse use;
    Atom *loc;
    Atom *pattern;
} IaMatched;

static bool ia_matched(IaWalk *w, Atom *c, IaMatched *m) {
    if ((ia_head(c, "read", 2u) || ia_head(c, "take", 2u))) {
        m->use = ia_head(c, "take", 2u) ? IA_USE_TAKE : IA_USE_KEEP;
        m->loc = w->default_loc;
        m->pattern = c->expr.elems[1];
        return true;
    }
    if ((ia_head(c, "read", 3u) || ia_head(c, "take", 3u))) {
        m->use = ia_head(c, "take", 3u) ? IA_USE_TAKE : IA_USE_KEEP;
        m->loc = c->expr.elems[1];
        m->pattern = c->expr.elems[2];
        return true;
    }
    m->use = IA_USE_KEEP;
    m->loc = w->default_loc;
    m->pattern = c;
    return true;
}

/* One occurrence of a scan: its position and stored atom, and the bindings
 * that make the pattern its instance. */
typedef bool (*IaVisit)(IaWalk *w, void *data, CettaIndex pos, Atom *atom,
                        Bindings *b);

static bool ia_scan_all(IaWalk *w, IaLoc *loc, Atom *pattern, IaVisit visit,
                        void *data);
static bool ia_visit_collect(IaWalk *w, void *data, CettaIndex pos,
                             Atom *atom, Bindings *b);
#define ia_visit_collect_fwd ia_visit_collect

/* A negative result is evidence only after the canonical matcher completed.
 * The builder owns the private binding trail on every outcome. */
static CettaMatchCompletion ia_match(Atom *pattern, Atom *value, Bindings *bindings,
                                     Arena *a, CettaFrameIdentity epoch) {
    BindingsBuilder builder;
    bindings_builder_init_owned(&builder, bindings);
    /* Stored rows introduce a fresh activation. A computed result already
     * has materialized identities, including free variables shared with
     * the remaining sources; it must not acquire a second rule frame. */
    CettaMatchCompletion result = cetta_frame_identity_is_none(epoch)
        ? match_atoms_builder_checked(pattern, value, &builder, a, UINT64_MAX)
        : match_atoms_epoch_builder_checked(
            pattern, value, &builder, a, epoch, UINT64_MAX);
    bindings_builder_take(&builder, bindings);
    return result;
}

/* The occurrences one at a time, from the space's own lazy cursor: a first
 * row commits without the region's other candidates being listed.  Each
 * yielded position is checked to name the yielded atom in the live space;
 * otherwise the scan is redone in full. */
static bool ia_scan_lazy(IaWalk *w, IaLoc *loc, Atom *pattern, IaVisit visit,
                         void *data, bool *done) {
    Space *space = loc->space;
    *done = false;
    if (space->overlay_base || space_has_dependencies(space) ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return true;
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (!space_occurrence_cursor_init(space, pattern, &cursor))
        return true;
    bool ok = true, visited = false;
    CettaCount len = space_length64(space);
    for (;;) {
        CettaIndex pos = 0u;
        Atom *atom = NULL;
        SpaceOccurrenceCursorStep step = space_occurrence_cursor_next(&cursor, &pos, &atom);
        if (step == SPACE_OCCURRENCE_CURSOR_END)
            break;
        if (step != SPACE_OCCURRENCE_CURSOR_ITEM || !atom || pos >= len ||
            space_get_at64(space, pos) != atom) {
            if (visited) {
                /* positions already handed out cannot be taken back */
                w->failed = true;
                ok = false;
            }
            space_occurrence_cursor_release(&cursor);
            return ok;
        }
        CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
        CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
        if (!cetta_frame_identity_scope_try(&frame_identity_scope, &epoch)) {
            w->failed = true;
            ok = false;
            break;
        }
        Bindings b;
        bindings_init(&b);
        CettaMatchCompletion matched = ia_match(pattern, atom, &b, w->a, epoch);
        if (matched == CETTA_MATCH_INCOMPLETE) {
            w->refused = atom_symbol(w->a, "match-incomplete");
            ok = false;
        } else if (matched == CETTA_MATCH_MATCHED) {
            visited = true;
            ok = visit(w, data, pos, atom, &b);
        }
        bindings_free(&b);
        if (!ok)
            break;
    }
    space_occurrence_cursor_release(&cursor);
    *done = true;
    return ok && !w->failed;
}

/* Every stored occurrence of `loc` whose atom the pattern matches. */
static bool ia_scan(IaWalk *w, IaLoc *loc, Atom *pattern, IaVisit visit,
                    void *data) {
    if (w->record_scans) {
        Atom *why = ia_check_right(w->a, loc, IA_OBSERVE);
        if (why) {
            w->refused = why;
            return false;
        }
    }
    ia_note_seen(w, loc);
    if (w->record_scans &&
        !ia_push(&w->scans, atom_expr3(w->a, loc->atom,
          atom_int(w->a, (int64_t)space_revision(loc->space)), pattern)))
        w->failed = true;
    if (w->first && visit != ia_visit_collect_fwd) {
        bool done = false;
        bool ok = ia_scan_lazy(w, loc, pattern, visit, data, &done);
        if (done || !ok)
            return ok;
    }
    return ia_scan_all(w, loc, pattern, visit, data);
}

static bool ia_scan_all(IaWalk *w, IaLoc *loc, Atom *pattern, IaVisit visit,
                        void *data) {
    Space *space = loc->space;
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))
    CettaSharedTransitionGuard shared_transition = {0};
    cetta_shared_transition_guard_enter(&shared_transition);
    CettaCount len = space_length64(space);
    bool indexed = !space->overlay_base && !space_has_dependencies(space) &&
        len > MATCH_TRIE_THRESHOLD && pattern->kind == ATOM_EXPR &&
        pattern->expr.len > 0u &&
        (space->match_backend.kind == SPACE_ENGINE_NATIVE ||
         space->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT);
    CettaIndex *candidates = NULL;
    CettaCount count = indexed ? space_match_candidates64(space, pattern, &candidates)
                               : len;
    for (CettaCount i = 1; indexed && i < count; i++)
        if (candidates[i - 1u] >= candidates[i]) {
            /* not logical order: read every row instead */
            free(candidates);
            candidates = NULL;
            indexed = false;
            count = len;
        }
    bool ok = true;
    for (CettaCount i = 0; ok && i < count; i++) {
        CettaIndex pos = indexed ? candidates[i] : i;
        if (pos >= len)
            continue;
        Atom *atom = space_get_at64(space, pos);
        if (!atom)
            continue;
        CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
        CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
        if (!cetta_frame_identity_scope_try(&frame_identity_scope, &epoch)) {
            w->failed = true;
            break;
        }
        Bindings b;
        bindings_init(&b);
        CettaMatchCompletion matched = ia_match(pattern, atom, &b, w->a, epoch);
        if (matched == CETTA_MATCH_INCOMPLETE) {
            w->refused = atom_symbol(w->a, "match-incomplete");
            ok = false;
        } else if (matched == CETTA_MATCH_MATCHED)
            ok = visit(w, data, pos, atom, &b);
        bindings_free(&b);
    }
    free(candidates);
    return ok && !w->failed;
}

static Atom *ia_entry(Arena *a, const char *kind, IaLoc *loc, CettaIndex pos,
                      Atom *atom) {
    Atom *items[5] = {
        atom_symbol(a, kind), loc->atom,
        atom_int(a, (int64_t)space_revision(loc->space)),
        atom_int(a, (int64_t)pos), atom,
    };
    return atom_expr(a, items, 5u);
}

/* The space a resolved location atom names, without diagnostics. */
static Space *ia_space_of(Atom *loc) {
    if (!loc)
        return NULL;
    IaHandle *h = ia_handle_of(loc);
    if (h)
        return h->region->space;
    if (loc->kind == ATOM_GROUNDED && loc->ground.gkind == GV_SPACE)
        return (Space *)loc->ground.ptr;
    if (loc->kind == ATOM_SYMBOL)
        return resolve_space(eval_current_registry(), loc);
    return NULL;
}

/* Computed reads retain the complete issued token, including imported-view
 * epochs. Sampling a fresh epoch at validation would bless an older result. */
static Atom *ia_captured_read(Arena *a, SpaceReadToken read) {
    Atom *items[7] = {atom_symbol(a, "ia:captured-read"),
        atom_space(a, (Space *)read.space),
        atom_int(a, (int64_t)read.instance_id), atom_int(a, (int64_t)read.revision),
        atom_int(a, (int64_t)read.prefix_epoch), atom_int(a, (int64_t)read.base_prefix_epoch),
        atom_int(a, (int64_t)read.base_dependency_epoch)};
    return atom_expr(a, items, 7u);
}

static bool ia_captured_read_token(Atom *entry, SpaceReadToken *read) {
    if (!ia_head(entry, "ia:captured-read", 7u))
        return false;
    Space *space = ia_space_of(entry->expr.elems[1]);
    uint64_t fields[5];
    for (size_t i = 0; i < 5u; i++) {
        Atom *value = entry->expr.elems[i + 2u];
        if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT)
            return false;
        fields[i] = (uint64_t)value->ground.ival;
    }
    *read = (SpaceReadToken){.space = space, .instance_id = fields[0],
        .revision = fields[1], .prefix_epoch = fields[2],
        .base_prefix_epoch = fields[3], .base_dependency_epoch = fields[4]};
    return space && read->instance_id != 0u;
}

/* Whether occurrence (space, pos) is already taken, or kept, in a
 * footprint list (an expression of entries). */
static bool ia_fp_has(Atom *fp, Space *space, CettaIndex pos, bool takes,
                      bool keeps) {
    if (!fp || fp->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0; i < fp->expr.len; i++) {
        Atom *e = fp->expr.elems[i];
        bool take = ia_head(e, "take", 5u), keep = ia_head(e, "keep", 5u);
        if (!((take && takes) || (keep && keeps)))
            continue;
        Atom *p = e->expr.elems[3];
        if (p->kind != ATOM_GROUNDED || p->ground.gkind != GV_INT ||
            (CettaIndex)p->ground.ival != pos)
            continue;
        if (ia_space_of(e->expr.elems[1]) == space)
            return true;
    }
    return false;
}

/* The identification condition of one row: a take may meet nothing of the
 * row, a keep may not meet a take of the row (keeps may share).  A row of
 * an `every` suffix is linear on its own; the whole commit, the kernel's
 * picks and every row together, must be linear too, and the commit's own
 * check refuses it otherwise (the rows of the source list, then the
 * linear rows fire). */
static bool ia_linear_with(Atom *fp, Atom *base, IaUse use, Space *space,
                           CettaIndex pos) {
    (void)base;
    bool keeps = use == IA_USE_TAKE;
    return !ia_fp_has(fp, space, pos, true, keeps);
}

static Atom *ia_cons(Arena *a, Atom *x, Atom *list) {
    CettaExprLen n = list && list->kind == ATOM_EXPR ? list->expr.len : 0u;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (n + 1u));
    elems[0] = x;
    for (CettaExprLen i = 0; i < n; i++)
        elems[i + 1u] = list->expr.elems[i];
    return atom_expr(a, elems, n + 1u);
}

static void ia_walk_list(IaWalk *w, Atom **cs, size_t n, Atom *tpl, Atom *fp,
                         Atom *base);

/* The rest of the conjuncts and the template under `b`, then onward. */
static void ia_continue(IaWalk *w, Bindings *b, Atom **rest, size_t n,
                        Atom *tpl, Atom *fp, Atom *base) {
    Atom **next = n ? arena_alloc(w->a, sizeof(Atom *) * n) : NULL;
    for (size_t i = 0; i < n; i++)
        if (!(next[i] = ia_subst_source(b, w->a, rest[i]))) {
            w->failed = true;
            return;
        }
    Atom *t = ia_subst_template(b, w->a, tpl);
    if (!t) {
        w->failed = true;
        return;
    }
    ia_walk_list(w, next, n, t, fp, base);
}

typedef struct {
    IaLoc *loc;
    IaUse use;
    Atom **rest;
    size_t n;
    Atom *tpl, *fp, *base;
} IaMatchStep;

static bool ia_visit_match(IaWalk *w, void *data, CettaIndex pos, Atom *atom,
                           Bindings *b) {
    IaMatchStep *s = data;
    if (!w->unfiltered && !ia_linear_with(s->fp, s->base, s->use, s->loc->space, pos))
        return true;
    Atom *e = ia_entry(w->a, s->use == IA_USE_TAKE ? "take" : "keep", s->loc,
                       pos, atom);
    ia_continue(w, b, s->rest, s->n, s->tpl, ia_cons(w->a, e, s->fp), s->base);
    if (w->first && w->results.len) {
        w->truncated = true;
        return false;
    }
    return !w->failed && !w->refused;
}

typedef struct {
    IaList atoms;
    IaList positions;
    Bindings first;
    bool have_first;
} IaCollect;

static bool ia_visit_collect(IaWalk *w, void *data, CettaIndex pos,
                             Atom *atom, Bindings *b) {
    IaCollect *c = data;
    if (!c->have_first) {
        if (!bindings_clone(&c->first, b))
            return false;
        c->have_first = true;
    }
    return ia_push(&c->atoms, atom) &&
           ia_push(&c->positions, atom_int(w->a, (int64_t)pos));
}

static void ia_collect_free(IaCollect *c) {
    free(c->atoms.items);
    free(c->positions.items);
    if (c->have_first)
        bindings_free(&c->first);
}

static void ia_refuse(IaWalk *w, Atom *reason) {
    if (!w->refused)
        w->refused = reason;
}

/* (unique [take] c) and (all [take] c COLL): the inner matched source. */
static bool ia_quantified(IaWalk *w, Atom *c, bool all, IaMatched *m,
                          Atom **coll) {
    CettaExprLen want = all ? 3u : 2u;
    Atom *inner;
    bool take_word = false;
    if (c->expr.len == want + 1u && atom_is_symbol(c->expr.elems[1], "take")) {
        take_word = true;
        inner = c->expr.elems[2];
        *coll = all ? c->expr.elems[3] : NULL;
    } else if (c->expr.len == want) {
        inner = c->expr.elems[1];
        *coll = all ? c->expr.elems[2] : NULL;
    } else
        return false;
    ia_matched(w, inner, m);
    if (take_word)
        m->use = IA_USE_TAKE;
    return true;
}

static void ia_walk_list(IaWalk *w, Atom **cs, size_t n, Atom *tpl, Atom *fp,
                         Atom *base) {
    if (w->failed || w->refused)
        return;
    if (w->first && w->results.len) {
        w->truncated = true;
        return;
    }
    if (++w->depth > 100000u) {
        ia_refuse(w, atom_symbol(w->a, "walk-too-deep"));
        return;
    }
    if (n == 0u) {
        if (!ia_push(&w->results, atom_expr3(w->a, atom_symbol(w->a, "ia:row"),
                                             tpl, fp)))
            w->failed = true;
        return;
    }
    Atom *c = cs[0];
    Atom **rest = cs + 1;
    size_t m = n - 1u;
    Arena *a = w->a;
    /* a nested conjunction splices */
    if (ia_head_any(c, ",")) {
        size_t k = c->expr.len - 1u;
        Atom **flat = arena_alloc(a, sizeof(Atom *) * (k + m + 1u));
        for (size_t i = 0; i < k; i++)
            flat[i] = c->expr.elems[i + 1u];
        for (size_t i = 0; i < m; i++)
            flat[k + i] = rest[i];
        ia_walk_list(w, flat, k + m, tpl, fp, base);
        return;
    }
    if (c->kind == ATOM_EXPR && c->expr.len == 0u) {
        ia_walk_list(w, rest, m, tpl, fp, base);
        return;
    }
    if (ia_head(c, "let", 3u)) {
        Atom *state_items[5] = {
            atom_symbol(a, "ia:state"), atom_expr(a, rest, (CettaExprLen)m),
            tpl, fp, base,
        };
        Atom *items[4] = {
            atom_symbol(a, "ia:let"), c->expr.elems[1], c->expr.elems[2],
            atom_expr(a, state_items, 5u),
        };
        if (!ia_push(&w->results, atom_expr(a, items, 4u)))
            w->failed = true;
        return;
    }
    if (ia_head(c, "every", 2u)) {
        Atom **inner;
        size_t k;
        Atom *src = c->expr.elems[1];
        if (src->kind == ATOM_EXPR && src->expr.len == 0u) {
            inner = NULL;
            k = 0u;
        } else if (ia_head_any(src, ",")) {
            inner = src->expr.elems + 1;
            k = src->expr.len - 1u;
        } else {
            inner = &c->expr.elems[1];
            k = 1u;
        }
        Atom **flat = arena_alloc(a, sizeof(Atom *) * (k + m + 1u));
        for (size_t i = 0; i < k; i++)
            flat[i] = inner[i];
        for (size_t i = 0; i < m; i++)
            flat[k + i] = rest[i];
        Atom *state_items[5] = {
            atom_symbol(a, "ia:state"), atom_expr(a, flat, (CettaExprLen)(k + m)),
            tpl, atom_unit(a), fp,
        };
        if (!ia_push(&w->results,
                     atom_expr3(a, atom_symbol(a, "ia:every"),
                                atom_expr(a, state_items, 5u), fp)))
            w->failed = true;
        return;
    }
    if (ia_head(c, "absent", 2u) || ia_head(c, "absent", 3u)) {
        Atom *loc_atom = c->expr.len == 3u ? c->expr.elems[1] : w->default_loc;
        Atom *pattern = c->expr.elems[c->expr.len - 1u];
        if (c->expr.len == 2u && ia_source_form(pattern)) {
            Atom *one = NULL;
            Atom **inner;
            size_t count;
            ia_conjuncts(pattern, &one, &inner, &count);
            Atom *query[5] = {atom_symbol(a, "ia:state"),
                atom_expr(a, inner, (CettaExprLen)count), atom_unit(a),
                atom_unit(a), atom_unit(a)};
            Atom *next[5] = {atom_symbol(a, "ia:state"),
                atom_expr(a, rest, (CettaExprLen)m), tpl, fp, base};
            if (!ia_push(&w->results, atom_expr3(a, atom_symbol(a, "ia:absent"),
                atom_expr(a, query, 5u), atom_expr(a, next, 5u))))
                w->failed = true;
            return;
        }

        IaLoc loc;
        Atom *why = ia_resolve(a, loc_atom, &loc);
        if (!why)
            why = ia_check_right(a, &loc, IA_OBSERVE);
        if (why) {
            ia_refuse(w, why);
            return;
        }
        IaCollect col = {0};
        bool ok = ia_scan(w, &loc, pattern, ia_visit_collect, &col);
        size_t found = col.atoms.len;
        ia_collect_free(&col);
        if (!ok) {
            w->failed = true;
            return;
        }
        if (found == 0u) {
            Atom *items[5] = {atom_symbol(a, "full"), loc.atom,
                              atom_int(a, (int64_t)space_revision(loc.space)),
                              pattern, atom_int(a, 0)};
            ia_walk_list(w, rest, m, tpl,
                         ia_cons(a, atom_expr(a, items, 5u), fp), base);
        }
        return;
    }
    bool unique = ia_head_any(c, "unique") && (c->expr.len == 2u || c->expr.len == 3u);
    bool all = ia_head_any(c, "all") && (c->expr.len == 3u || c->expr.len == 4u);
    if (getenv("CETTA_INTERACTION_DEBUG")) {
        fprintf(stderr, "[ia] conjunct kind=%d len=%u unique=%d all=%d: ", (int)c->kind,
                c->kind == ATOM_EXPR ? (unsigned)c->expr.len : 0u, unique, all);
        atom_print(c, stderr);
        if (c->kind == ATOM_EXPR && c->expr.len)
            fprintf(stderr, " head-kind=%d", (int)c->expr.elems[0]->kind);
        fprintf(stderr, "\n");
    }
    if (unique || all) {
        IaMatched q;
        Atom *coll = NULL;
        if (!ia_quantified(w, c, all, &q, &coll)) {
            if (!ia_push(&w->results, atom_expr2(a, atom_symbol(a, "ia:stuck"), c)))
                w->failed = true;
            return;
        }
        IaLoc loc;
        Atom *why = ia_resolve(a, q.loc, &loc);
        if (!why)
            why = ia_check_right(a, &loc, IA_OBSERVE |
                                 (q.use == IA_USE_TAKE ? IA_RECEIVE : 0u));
        if (why) {
            ia_refuse(w, why);
            return;
        }
        IaCollect col = {0};
        if (!ia_scan(w, &loc, q.pattern, ia_visit_collect, &col)) {
            ia_collect_free(&col);
            w->failed = true;
            return;
        }
        size_t found = col.atoms.len;
        Atom *full_items[5] = {atom_symbol(a, "full"), loc.atom,
                               atom_int(a, (int64_t)space_revision(loc.space)),
                               q.pattern, atom_int(a, (int64_t)found)};
        Atom *nfp = ia_cons(a, atom_expr(a, full_items, 5u), fp);
        if (unique) {
            if (found == 1u) {
                CettaIndex pos = (CettaIndex)col.positions.items[0]->ground.ival;
                if (q.use == IA_USE_TAKE) {
                    if (!w->unfiltered && !ia_linear_with(fp, base, IA_USE_TAKE, loc.space, pos)) {
                        ia_collect_free(&col);
                        return;
                    }
                    nfp = ia_cons(a, ia_entry(a, "take", &loc, pos,
                                              col.atoms.items[0]), nfp);
                }
                ia_continue(w, &col.first, rest, m, tpl, nfp, base);
            }
            ia_collect_free(&col);
            return;
        }
        /* all: the region's matches as one collection */
        if (q.use == IA_USE_TAKE)
            for (size_t i = 0; i < found; i++) {
                CettaIndex pos = (CettaIndex)col.positions.items[i]->ground.ival;
                if (!w->unfiltered && !ia_linear_with(fp, base, IA_USE_TAKE, loc.space, pos)) {
                    ia_collect_free(&col);
                    return;
                }
                nfp = ia_cons(a, ia_entry(a, "take", &loc, pos,
                                          col.atoms.items[i]), nfp);
            }
        Atom *bag = ia_list_atom(a, &col.atoms);
        ia_collect_free(&col);
        Bindings b;
        bindings_init(&b);
        CettaMatchCompletion matched = ia_match(coll, bag, &b, a, CETTA_FRAME_IDENTITY_NONE);
        if (matched == CETTA_MATCH_INCOMPLETE)
            w->refused = atom_symbol(a, "match-incomplete");
        else if (matched == CETTA_MATCH_MATCHED)
            ia_continue(w, &b, rest, m, tpl, nfp, base);
        bindings_free(&b);
        return;
    }
    IaMatched q;
    ia_matched(w, c, &q);
    IaLoc loc;
    Atom *why = ia_resolve(a, q.loc, &loc);
    if (!why)
        why = ia_check_right(a, &loc, q.use == IA_USE_TAKE ? IA_RECEIVE : IA_PEEK);
    if (why) {
        ia_refuse(w, why);
        return;
    }
    IaMatchStep step = {.loc = &loc, .use = q.use, .rest = rest, .n = m,
                        .tpl = tpl, .fp = fp, .base = base};
    if (!ia_scan(w, &loc, q.pattern, ia_visit_match, &step) && !w->refused &&
        !w->truncated)
        w->failed = true;
}

static Atom *ia_walk_result(IaWalk *w) {
    Arena *a = w->a;
    if (w->failed)
        return NULL;
    if (w->refused) {
        Atom *r = atom_expr2(a, atom_symbol(a, "ia:refused"), w->refused);
        Atom *items[3] = {atom_symbol(a, "ia:walked"),
                          ia_cons(a, atom_symbol(a, "ia:seen"), ia_list_atom(a, &w->seen)),
                          r};
        return atom_expr(a, items, 3u);
    }
    Atom *seen = ia_cons(a, atom_symbol(a, "ia:seen"), ia_list_atom(a, &w->seen));
    IaList out = {0};
    bool ok = ia_push(&out, atom_symbol(a, "ia:walked")) && ia_push(&out, seen);
    for (size_t i = 0; ok && i < w->results.len; i++)
        ok = ia_push(&out, w->results.items[i]);
    Atom *result = ok ? ia_list_atom(a, &out) : NULL;
    free(out.items);
    return result;
}


/* ── identification by sorting ────────────────────────────────────────── */

typedef struct {
    Space *space;
    CettaIndex pos;
    uint32_t row;
    bool take;
    Atom *loc, *posatom;
} IaOcc;

static int ia_occ_cmp(const void *l, const void *r) {
    const IaOcc *x = l, *y = r;
    if (x->space != y->space)
        return (uintptr_t)x->space < (uintptr_t)y->space ? -1 : 1;
    if (x->pos != y->pos)
        return x->pos < y->pos ? -1 : 1;
    return x->row < y->row ? -1 : x->row > y->row;
}

/* The take and keep entries of `fp` as occurrences, tagged with `row`. */
static bool ia_occs_push(IaOcc **items, size_t *len, size_t *cap, Atom *fp,
                         uint32_t row) {
    if (!fp || fp->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < fp->expr.len; i++) {
        Atom *e = fp->expr.elems[i];
        bool take = ia_head(e, "take", 5u), keep = ia_head(e, "keep", 5u);
        if (!take && !keep)
            continue;
        Atom *p = e->expr.elems[3];
        Space *space = ia_space_of(e->expr.elems[1]);
        if (!space || p->kind != ATOM_GROUNDED || p->ground.gkind != GV_INT)
            continue;
        if (*len == *cap) {
            size_t c = *cap ? *cap * 2u : 64u;
            IaOcc *next = realloc(*items, c * sizeof(*next));
            if (!next)
                return false;
            *items = next;
            *cap = c;
        }
        (*items)[(*len)++] = (IaOcc){space, (CettaIndex)p->ground.ival, row,
                                     take, e->expr.elems[1], p};
    }
    return true;
}

/* The first conflict among the occurrences: a take met by another take, or
 * by a keep of another row (rows: `every`) or of any entry (one commit). */
static Atom *ia_occs_conflict(Arena *a, IaOcc *items, size_t len,
                              bool across_rows) {
    if (len > 1u)
        qsort(items, len, sizeof(*items), ia_occ_cmp);
    for (size_t i = 0; i < len; ) {
        size_t j = i;
        while (j < len && items[j].space == items[i].space &&
               items[j].pos == items[i].pos)
            j++;
        /* the group i..j-1 is one occurrence */
        for (size_t x = i; x < j; x++) {
            if (!items[x].take)
                continue;
            for (size_t y = i; y < j; y++) {
                if (y == x || (across_rows && items[y].row == items[x].row))
                    continue;
                const char *why = across_rows
                    ? (items[y].take ? "every-shares-a-take" : "every-keeps-a-take")
                    : (items[y].take ? "taken-twice" : "taken-and-kept");
                return atom_expr3(a, atom_symbol(a, why), items[x].loc,
                                  items[x].posatom);
            }
        }
        i = j;
    }
    return NULL;
}

/* ── operations ───────────────────────────────────────────────────────── */

static bool ia_value(CettaCallOutcome *out, Atom *value) {
    *out = value ? cetta_call_completed_value(value) : cetta_call_failure();
    return true;
}

static Atom *ia_refusal(Arena *a, Atom *reason) {
    return atom_expr2(a, atom_symbol(a, "ia:refused"), reason);
}

/* (facet LOC RIGHTS) -> a handle with the common rights, within what the
 * region's profile lets its holder hand out. */
static bool op_facet(Arena *a, Atom **args, uint32_t nargs,
                     CettaCallOutcome *out) {
    if (nargs != 2u)
        return false;
    IaLoc loc;
    Atom *why = ia_resolve(a, args[0], &loc);
    if (why)
        return ia_value(out, ia_refusal(a, why));
    uint32_t wanted;
    if (!ia_rights_from(a, args[1], &wanted))
        return ia_value(out, ia_refusal(a, atom_expr2(a,
            atom_symbol(a, "unknown-rights"), ia_strip(a, args[1]))));
    const IaProfile *p = loc.region ? loc.region->profile : NULL;
    uint32_t rights = loc.rights & wanted & (p ? p->delegable : IA_ALL);
    return ia_value(out, ia_handle_atom(a, loc.region, rights));
}

static bool op_rights(Arena *a, Atom **args, uint32_t nargs,
                      CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    IaLoc loc;
    Atom *why = ia_resolve(a, args[0], &loc);
    return ia_value(out, why ? ia_refusal(a, why) : ia_rights_atom(a, loc.rights));
}

static bool op_profile(Arena *a, Atom **args, uint32_t nargs,
                       CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    IaLoc loc;
    Atom *why = ia_resolve(a, args[0], &loc);
    if (why)
        return ia_value(out, ia_refusal(a, why));
    const IaProfile *p = loc.region ? loc.region->profile : NULL;
    return ia_value(out, atom_symbol(a, p ? p->name : "space"));
}

/* The coordination a region's profile licenses: no region epoch when
 * nobody may read it in full; monotone (replicable without coordination)
 * when nobody may take from it; which events can wake a waiter there. */
static bool op_strategy(Arena *a, Atom **args, uint32_t nargs,
                        CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    IaLoc loc;
    Atom *why = ia_resolve(a, args[0], &loc);
    if (why)
        return ia_value(out, ia_refusal(a, why));
    uint32_t allowed = ia_profile_allowed(loc.region ? loc.region->profile : NULL);
    bool observe = allowed & IA_OBSERVE, receive = allowed & IA_RECEIVE;
    Atom *wakes[3];
    CettaExprLen nw = 0;
    wakes[nw++] = atom_symbol(a, "make");
    if (receive && observe)
        wakes[nw++] = atom_symbol(a, "take");
    Atom *items[4] = {
        atom_symbol(a, "strategy"),
        atom_expr2(a, atom_symbol(a, "epoch"),
                   atom_symbol(a, observe ? "region" : "none")),
        atom_expr2(a, atom_symbol(a, "monotone"),
                   atom_symbol(a, receive ? "no" : "yes")),
        atom_expr2(a, atom_symbol(a, "woken-by"), atom_expr(a, wakes, nw)),
    };
    return ia_value(out, atom_expr(a, items, 4u));
}

/* (same-region L1 L2) -> True when both locations name one region, whatever
 * their rights; False otherwise, also for an operand that is no location.
 * Equality is all a program learns of a region's identity: a handle with no
 * rights names its region and grants nothing, so a reader can pin a region
 * and refuse a substitute. */
static bool op_same_region(Arena *a, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    if (nargs != 2u)
        return false;
    IaLoc x, y;
    bool same = !ia_resolve(a, args[0], &x) && !ia_resolve(a, args[1], &y) &&
                x.region == y.region;
    return ia_value(out, same ? atom_true(a) : atom_false(a));
}

/* (start DEFAULT SOURCES TEMPLATE): the walk of a transform as written. */
static bool op_start(Arena *a, Atom **args, uint32_t nargs,
                     CettaCallOutcome *out) {
    if (nargs != 3u)
        return false;
    Atom *sources = ia_unhold(args[1]);
    Atom *tpl = ia_unhold(args[2]);
    Atom *one = NULL;
    Atom **cs;
    size_t n;
    ia_conjuncts(sources, &one, &cs, &n);
    IaWalk w = {.a = a, .default_loc = ia_strip(a, args[0])};
    ia_walk_list(&w, cs, n, tpl, atom_unit(a), atom_unit(a));
    Atom *result = ia_walk_result(&w);
    ia_walk_free(&w);
    if (!result)
        return ia_value(out, ia_refusal(a, atom_symbol(a, "walk-failed")));
    return ia_value(out, result);
}

/* (walk DEFAULT STATE): a walk resumed after a `let`. */
static bool op_walk(Arena *a, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    if (nargs != 2u)
        return false;
    Atom *state = ia_unhold(args[1]);
    if (!ia_head(state, "ia:state", 5u))
        return ia_value(out, ia_refusal(a, atom_symbol(a, "not-a-state")));
    IaWalk w = {.a = a, .default_loc = ia_strip(a, args[0])};
    Atom *sources = state->expr.elems[1];
    if (sources->kind != ATOM_EXPR)
        return ia_value(out, ia_refusal(a, atom_symbol(a, "not-a-state")));
    ia_walk_list(&w, sources->expr.elems, sources->expr.len,
                 state->expr.elems[2], state->expr.elems[3],
                 state->expr.elems[4]);
    Atom *result = ia_walk_result(&w);
    ia_walk_free(&w);
    if (!result)
        return ia_value(out, ia_refusal(a, atom_symbol(a, "walk-failed")));
    return ia_value(out, result);
}

/* (walk-suffix DEFAULT STATE): continue a suffix through the same row and
 * suspension events as an ordinary source walk. */
static bool op_walk_suffix(Arena *a, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    if (nargs != 2u)
        return false;
    Atom *state = ia_unhold(args[1]);
    if (!ia_head(state, "ia:state", 5u))
        return ia_value(out, ia_refusal(a, atom_symbol(a, "not-a-state")));
    IaWalk w = {.a = a, .default_loc = ia_strip(a, args[0])};
    Atom *sources = state->expr.elems[1];
    ia_walk_list(&w, sources->expr.elems, sources->expr.len,
                 state->expr.elems[2], state->expr.elems[3],
                 state->expr.elems[4]);
    Atom *result = ia_walk_result(&w);
    ia_walk_free(&w);
    if (!result)
        return ia_value(out, ia_refusal(a, atom_symbol(a, "walk-failed")));
    return ia_value(out, result);
}

/* (bind PATTERN VALUE STATE) -> STATE under the match, or no answer. */
static bool op_bind(Arena *a, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    if (nargs != 3u)
        return false;
    Atom *pattern = ia_unhold(args[0]);
    Atom *value = ia_strip(a, args[1]);
    Atom *state = ia_unhold(args[2]);
    if (!ia_head(state, "ia:state", 5u))
        return ia_value(out, ia_refusal(a, atom_symbol(a, "not-a-state")));
    Bindings b;
    bindings_init(&b);
    Atom *result = NULL;
    CettaMatchCompletion matched = ia_match(pattern, value, &b, a, CETTA_FRAME_IDENTITY_NONE);
    if (matched == CETTA_MATCH_INCOMPLETE)
        result = ia_refusal(a, atom_symbol(a, "match-incomplete"));
    else if (matched == CETTA_MATCH_MATCHED) {
        Atom *sources = state->expr.elems[1];
        Atom **next = arena_alloc(a, sizeof(Atom *) * (sources->expr.len + 1u));
        bool ok = true;
        for (CettaExprIndex i = 0; ok && i < sources->expr.len; i++)
            ok = (next[i] = ia_subst_source(&b, a, sources->expr.elems[i])) != NULL;
        Atom *tpl = ok ? ia_subst_template(&b, a, state->expr.elems[2]) : NULL;
        if (tpl) {
            Atom *items[5] = {state->expr.elems[0],
                              atom_expr(a, next, sources->expr.len), tpl,
                              state->expr.elems[3], state->expr.elems[4]};
            result = atom_expr(a, items, 5u);
        } else
            result = ia_refusal(a, atom_symbol(a, "binding-failed"));
    }
    bindings_free(&b);
    return ia_value(out, result);
}

/* (split TEMPLATE DEST) -> (ia:split MAKES CONTINUATION).  An emission
 * (add-atom LOC ATOM) is a make of the commit; with a destination every
 * instance is a make there; any other item runs after the commit. */
static bool op_split(Arena *a, Atom **args, uint32_t nargs,
                     CettaCallOutcome *out) {
    if (nargs != 2u)
        return false;
    Atom *t = args[0];
    Atom *dest = ia_strip(a, args[1]);
    bool has_dest = !(dest && dest->kind == ATOM_SYMBOL &&
                      atom_is_symbol(dest, "ia:caller"));
    Atom *single[1];
    Atom **items;
    size_t n;
    Atom *tt = atom_prime_held_is(t) ? atom_prime_held_payload(t) : t;
    if (ia_head_any(tt, ",")) {
        items = tt->expr.elems + 1;
        n = tt->expr.len - 1u;
    } else if (tt->kind == ATOM_EXPR && tt->expr.len == 0u) {
        items = NULL;
        n = 0u;
    } else {
        single[0] = tt;
        items = single;
        n = 1u;
    }
    IaList makes = {0}, cont = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < n; i++) {
        Atom *item = items[i];
        if (has_dest)
            ok = ia_push(&makes, atom_expr2(a, dest, ia_strip(a, item)));
        else if (ia_head(item, "add-atom", 3u))
            ok = ia_push(&makes, atom_expr2(a, item->expr.elems[1],
                                            ia_strip(a, item->expr.elems[2])));
        else
            ok = ia_push(&cont, item);
    }
    Atom *result = ok ? atom_expr3(a, atom_symbol(a, "ia:split"),
                                   ia_list_atom(a, &makes), ia_list_atom(a, &cont))
                      : NULL;
    free(makes.items);
    free(cont.items);
    return ia_value(out, result);
}

typedef struct {
    Space *space;
    IaLoc loc;
    CettaIndex *takes;
    Atom **taken_atoms;
    size_t take_len, take_cap;
} IaSpaceTakes;


/* (commit SEEN FOOTPRINT MAKES) -> (ia:committed TAKEN MADE) or
 * (ia:refused REASON); TAKEN and MADE name each region taken from and made
 * into once, by the commit's first location for it.  Enabled: every region
 * read is at the revision it was read at (so every predicate query still holds),
 * every occurrence taken or kept is still the atom read, nothing is taken
 * twice or both taken and kept, and each part has its right.  Then the
 * takes leave and the makes arrive, and nothing else changes.
 *
 * The order of these steps is the protocol ia_commit_run, declared in
 * langdef/protocols/interaction_commit.module and generated as ordinary C into
 * src/generated/interaction_commit_protocol.generated.h.  Guards establish the
 * facts each step requires; the writes happen inside one epoch; the epoch is
 * published only when every space the commit changed has moved its revision.
 * A refusal before the epoch leaves the store as it was: a guard's reason, or
 * no-memory when the commit cannot reserve what its writes need.  A failure
 * inside the epoch is (commit-failed REASON) and may leave some writes made:
 * take-failed, make-failed, or revision-unmoved when the space layer changed a
 * space without moving its revision.  Defining ia_commit_run_CHECKED to 1
 * observes each fact again where a step requires it. */
static void ia_trace_commit(Arena *a, Atom *seen, Atom *fp, Atom *makes,
                            Atom *rows, IaLoc *made, uint64_t **made_ids);
static void ia_trace_after(Atom *makes, IaLoc *made, uint64_t *made_ids);

/* The parts of an absence of sources, ((LOC REV PATTERN) ...): each region
 * still observable by the committer, and at the revision the walk saw. */
static Atom *ia_check_full_of(Arena *a, Atom *parts) {
    if (!parts || parts->kind != ATOM_EXPR)
        return atom_symbol(a, "malformed-entry");
    for (CettaExprIndex k = 0; k < parts->expr.len; k++) {
        Atom *part = parts->expr.elems[k];
        if (part->kind != ATOM_EXPR || part->expr.len != 3u)
            return atom_expr2(a, atom_symbol(a, "malformed-entry"), part);
        IaLoc loc;
        Atom *why = ia_resolve(a, part->expr.elems[0], &loc);
        if (!why)
            why = ia_check_right(a, &loc, IA_OBSERVE);
        if (why)
            return why;
        Atom *rev = part->expr.elems[1];
        if (rev->kind != ATOM_GROUNDED || rev->ground.gkind != GV_INT ||
            (uint64_t)rev->ground.ival != space_revision(loc.space))
            return atom_expr2(a, atom_symbol(a, "stale"), loc.atom);
    }
    return NULL;
}

/* A space the epoch may change, as it was when the epoch opened. */
typedef struct {
    Space *space;
    uint64_t revision;
    CettaCount length;
    bool takes; /* occurrences leave it: its contents change */
} IaWritten;

/* One commit, as the protocol's hooks see it. */
typedef struct IaCommitRun {
    Arena *a;
    Atom *seen, *fp, *makes, *rows;
    Atom *why; /* why the last step failed */
    IaSpaceTakes *groups;
    size_t group_len, group_cap;
    IaLoc *made;
    uint64_t *made_ids;
    IaWritten *written;
    size_t written_len;
    bool epoch_open, taken, made_done;
    IaList taken_regions, made_regions;
} IaCommitRun;

/* A test can replay, after the writes, a space layer that does not move a
 * revision; a run never sets it. */
static void (*g_ia_commit_after_writes)(IaCommitRun *c);

/* Every region read is still at the revision it was read at. */
static Atom *ia_commit_seen_reason(IaCommitRun *c) {
    Arena *a = c->a;
    for (CettaExprIndex i = 0; i < c->seen->expr.len; i++) {
        Atom *e = c->seen->expr.elems[i];
        if (ia_head_any(e, "ia:seen"))
            continue;
        if (ia_head_any(e, "ia:captured-read")) {
            SpaceReadToken read;
            if (!ia_captured_read_token(e, &read))
                return atom_symbol(a, "malformed-seen");
            if (!space_read_token_is_current(read))
                return atom_expr2(a, atom_symbol(a, "stale"), e->expr.elems[1]);
            continue;
        }
        if (e->kind != ATOM_EXPR || e->expr.len != 2u)
            return atom_symbol(a, "malformed-seen");
        IaLoc loc;
        Atom *why = ia_resolve(a, e->expr.elems[0], &loc);
        if (why)
            return why;
        Atom *rev = e->expr.elems[1];
        if (rev->kind != ATOM_GROUNDED || rev->ground.gkind != GV_INT ||
            (uint64_t)rev->ground.ival != space_revision(loc.space))
            return atom_expr2(a, atom_symbol(a, "stale"), loc.atom);
    }
    return NULL;
}

/* Each entry of the footprint has its right and is at the revision read; each
 * position taken or kept still holds the atom read; each predicate query of
 * an absence of sources is at the revision read.  With `collect`, the takes
 * are grouped by space for the writes. */
static Atom *ia_commit_footprint_reason(IaCommitRun *c, bool collect) {
    Arena *a = c->a;
    for (CettaExprIndex i = 0; i < c->fp->expr.len; i++) {
        Atom *e = c->fp->expr.elems[i];
        if (ia_head(e, "full-of", 2u)) {
            Atom *why = ia_check_full_of(a, e->expr.elems[1]);
            if (why)
                return why;
            continue;
        }
        bool take = ia_head(e, "take", 5u), keep = ia_head(e, "keep", 5u);
        bool full = ia_head(e, "full", 5u);
        if (!take && !keep && !full)
            return atom_expr2(a, atom_symbol(a, "malformed-entry"), e);
        IaLoc loc;
        Atom *why = ia_resolve(a, e->expr.elems[1], &loc);
        if (why)
            return why;
        why = ia_check_right(a, &loc, take ? IA_RECEIVE : keep ? IA_PEEK
                                                               : IA_OBSERVE);
        if (why)
            return why;
        Atom *rev = e->expr.elems[2];
        bool rev_ok = rev->kind == ATOM_GROUNDED && rev->ground.gkind == GV_INT &&
                      (full && rev->ground.ival == 0
                           ? true
                           : (uint64_t)rev->ground.ival == space_revision(loc.space));
        if (!rev_ok)
            return atom_expr2(a, atom_symbol(a, "stale"), loc.atom);
        if (full)
            continue;
        Atom *p = e->expr.elems[3];
        if (p->kind != ATOM_GROUNDED || p->ground.gkind != GV_INT ||
            p->ground.ival < 0)
            return atom_expr2(a, atom_symbol(a, "malformed-entry"), e);
        CettaIndex pos = (CettaIndex)p->ground.ival;
        Atom *now = pos < space_length64(loc.space)
            ? space_get_at64(loc.space, pos) : NULL;
        if (!now || !atom_eq(now, e->expr.elems[4]))
            return atom_expr2(a, atom_symbol(a, "stale"), loc.atom);
        if (!take || !collect)
            continue;
        size_t g = 0;
        while (g < c->group_len && c->groups[g].space != loc.space)
            g++;
        if (g == c->group_len) {
            if (c->group_len == c->group_cap) {
                size_t cap = c->group_cap ? c->group_cap * 2u : 4u;
                IaSpaceTakes *next = realloc(c->groups, cap * sizeof(*next));
                if (!next)
                    return atom_symbol(a, "no-memory");
                c->groups = next;
                c->group_cap = cap;
            }
            c->groups[c->group_len++] = (IaSpaceTakes){.space = loc.space, .loc = loc};
        }
        IaSpaceTakes *st = &c->groups[g];
        if (st->take_len == st->take_cap) {
            size_t cap = st->take_cap ? st->take_cap * 2u : 8u;
            CettaIndex *t = realloc(st->takes, cap * sizeof(*t));
            if (t)
                st->takes = t;
            Atom **ta = realloc(st->taken_atoms, cap * sizeof(*ta));
            if (ta)
                st->taken_atoms = ta;
            if (!t || !ta)
                return atom_symbol(a, "no-memory");
            st->take_cap = cap;
        }
        st->taken_atoms[st->take_len] = now;
        st->takes[st->take_len++] = pos;
    }
    return NULL;
}

/* Identification: nothing taken twice, nothing both taken and kept. */
static Atom *ia_commit_linear_reason(IaCommitRun *c) {
    IaOcc *occs = NULL;
    size_t len = 0u, cap = 0u;
    Atom *why = ia_occs_push(&occs, &len, &cap, c->fp, 0u)
        ? ia_occs_conflict(c->a, occs, len, false)
        : atom_symbol(c->a, "no-memory");
    free(occs);
    return why;
}

/* Every make names a location with the send right.  `made`, when given,
 * receives the locations. */
static Atom *ia_commit_makes_reason(IaCommitRun *c, IaLoc *made) {
    Arena *a = c->a;
    for (CettaExprIndex i = 0; i < c->makes->expr.len; i++) {
        Atom *mk = c->makes->expr.elems[i];
        if (mk->kind != ATOM_EXPR || mk->expr.len != 2u)
            return atom_expr2(a, atom_symbol(a, "malformed-make"), mk);
        IaLoc here;
        IaLoc *loc = made ? &made[i] : &here;
        Atom *why = ia_resolve(a, mk->expr.elems[0], loc);
        if (!why)
            why = ia_check_right(a, loc, IA_SEND);
        if (why)
            return why;
    }
    return NULL;
}

/* The guards. */
static bool ia_commit_seen(IaCommitRun *c) {
    c->why = ia_commit_seen_reason(c);
    return !c->why;
}

static bool ia_commit_footprint(IaCommitRun *c) {
    c->why = ia_commit_footprint_reason(c, true);
    return !c->why;
}

static bool ia_commit_linear(IaCommitRun *c) {
    c->why = ia_commit_linear_reason(c);
    return !c->why;
}

static bool ia_commit_makes(IaCommitRun *c) {
    size_t n = c->makes->expr.len;
    c->made = n ? calloc(n, sizeof(*c->made)) : NULL;
    c->why = n && !c->made ? atom_symbol(c->a, "no-memory")
                           : ia_commit_makes_reason(c, c->made);
    return !c->why;
}

/* The facts, observed again where a step requires them (checked mode only,
 * hence inline). */
static inline bool ia_commit_is_current(IaCommitRun *c) {
    return !ia_commit_seen_reason(c);
}

static inline bool ia_commit_is_formed(IaCommitRun *c) {
    return !ia_commit_footprint_reason(c, false);
}

static inline bool ia_commit_is_linear(IaCommitRun *c) {
    return !ia_commit_linear_reason(c);
}

static inline bool ia_commit_is_permitted(IaCommitRun *c) {
    return !ia_commit_makes_reason(c, NULL);
}

static inline bool ia_commit_was_taken(IaCommitRun *c) {
    return c->taken;
}

static inline bool ia_commit_was_made(IaCommitRun *c) {
    return c->made_done;
}

/* Every space the commit changed has moved its revision: one that lost
 * occurrences, or whose length changed, is not at its opening revision. */
static bool ia_commit_has_moved(IaCommitRun *c) {
    for (size_t k = 0; k < c->written_len; k++) {
        IaWritten *w = &c->written[k];
        bool changed = w->takes || space_length64(w->space) != w->length;
        if (changed && space_revision(w->space) == w->revision)
            return false;
    }
    return true;
}

static inline bool ia_commit_epoch_is_open(IaCommitRun *c) {
    return c->epoch_open;
}

/* Room for `n` items, so that later pushes cannot fail. */
static bool ia_reserve(IaList *l, size_t n) {
    if (n <= l->cap)
        return true;
    Atom **items = realloc(l->items, n * sizeof(*items));
    if (!items)
        return false;
    l->items = items;
    l->cap = n;
    return true;
}

static void ia_commit_written_note(IaCommitRun *c, Space *space, bool takes) {
    for (size_t k = 0; k < c->written_len; k++)
        if (c->written[k].space == space) {
            c->written[k].takes |= takes;
            return;
        }
    c->written[c->written_len++] = (IaWritten){
        space, space_revision(space), space_length64(space), takes};
}

/* Reserve mutation bookkeeping before the first write. Backend atom
 * admission has its own allocation boundary; failure there is an engine
 * failure whose already changed state is published by the protocol. */
static bool ia_commit_epoch_open(IaCommitRun *c) {
    size_t n = c->group_len + c->makes->expr.len;
    c->written = n ? calloc(n, sizeof(*c->written)) : NULL;
    if ((n && !c->written) ||
        !ia_reserve(&c->taken_regions, c->group_len) ||
        !ia_reserve(&c->made_regions, c->makes->expr.len)) {
        free(c->written);
        c->written = NULL;
        c->why = atom_symbol(c->a, "no-memory");
        return false;
    }
    c->written_len = 0u;
    for (size_t g = 0; g < c->group_len; g++)
        ia_commit_written_note(c, c->groups[g].space, true);
    for (CettaExprIndex i = 0; i < c->makes->expr.len; i++)
        ia_commit_written_note(c, c->made[i].space, false);
    c->epoch_open = true;
    return true;
}

static void ia_commit_epoch_close(IaCommitRun *c) {
    free(c->written);
    c->written = NULL;
    c->written_len = 0u;
    c->epoch_open = false;
}

static void ia_commit_epoch_abandon(IaCommitRun *c) {
    ia_commit_epoch_close(c);
}

static void ia_commit_epoch_publish(IaCommitRun *c) {
    ia_commit_epoch_close(c);
}

/* The writes: the takes leave by position, then the makes arrive. */
static bool ia_commit_take(IaCommitRun *c) {
    if (ia_trace_on())
        ia_trace_commit(c->a, c->seen, c->fp, c->makes, c->rows, c->made,
                        &c->made_ids);
    bool removed = true;
    for (size_t g = 0; g < c->group_len && removed; g++) {
        IaSpaceTakes *st = &c->groups[g];
        removed = cetta_interaction_remove_positions(st->space, st->takes,
                                                     st->taken_atoms,
                                                     st->take_len);
        if (removed)
            (void)ia_push(&c->taken_regions, st->loc.atom);
    }
    c->taken = removed;
    if (!removed)
        c->why = atom_symbol(c->a, "take-failed");
    return removed;
}

static bool ia_commit_make(IaCommitRun *c) {
    Arena *persistent = eval_current_persistent_arena();
    for (CettaExprIndex i = 0; i < c->makes->expr.len; i++) {
        Atom *atom = ia_strip(c->a, c->makes->expr.elems[i]->expr.elems[1]);
        if (!space_admit_atom(c->made[i].space, persistent, atom)) {
            c->why = atom_symbol(c->a, "make-failed");
            return false;
        }
        bool dup = false;
        for (CettaExprIndex k = 0; k < i && !dup; k++)
            dup = c->made[k].region == c->made[i].region;
        if (!dup)
            (void)ia_push(&c->made_regions, c->made[i].atom);
    }
    if (ia_trace_on())
        ia_trace_after(c->makes, c->made, c->made_ids);
    if (g_ia_commit_after_writes)
        g_ia_commit_after_writes(c);
    c->made_done = true;
    return true;
}

static bool ia_commit_moved(IaCommitRun *c) {
    if (ia_commit_has_moved(c))
        return true;
    c->why = atom_symbol(c->a, "revision-unmoved");
    return false;
}

#include "generated/interaction_commit_protocol.generated.h"

static void ia_commit_run_free(IaCommitRun *c) {
    for (size_t g = 0; g < c->group_len; g++) {
        free(c->groups[g].takes);
        free(c->groups[g].taken_atoms);
    }
    free(c->groups);
    free(c->made);
    free(c->made_ids);
    free(c->written);
    free(c->taken_regions.items);
    free(c->made_regions.items);
}

static bool op_commit(Arena *a, Atom **args, uint32_t nargs,
                      CettaCallOutcome *out) {
    if (nargs != 3u && nargs != 4u)
        return false;
    Atom *rows = nargs == 4u ? ia_strip(a, args[3]) : NULL;
    Atom *seen = ia_strip(a, args[0]);
    Atom *fp = ia_strip(a, args[1]);
    Atom *makes = ia_strip(a, args[2]);
    if (!seen || seen->kind != ATOM_EXPR || !fp || fp->kind != ATOM_EXPR ||
        !makes || makes->kind != ATOM_EXPR)
        return ia_value(out, ia_refusal(a, atom_symbol(a, "malformed-commit")));
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))
    CettaSharedTransitionGuard shared_transition = {0};
    cetta_shared_transition_guard_enter(&shared_transition);
    IaCommitRun c = {.a = a, .seen = seen, .fp = fp, .makes = makes, .rows = rows};
    Atom *result;
    switch (ia_commit_run(&c)) {
    case ia_commit_run_answered:
        result = atom_expr3(a, atom_symbol(a, "ia:committed"),
                            ia_list_atom(a, &c.taken_regions),
                            ia_list_atom(a, &c.made_regions));
        break;
    case ia_commit_run_declined:
        result = ia_refusal(a, c.why ? c.why : atom_symbol(a, "declined"));
        break;
    case ia_commit_run_resource_failure:
        result = ia_refusal(a, atom_symbol(a, "no-memory"));
        break;
    default:
        result = ia_refusal(a, atom_expr2(a, atom_symbol(a, "commit-failed"),
                                          c.why ? c.why : atom_symbol(a, "engine")));
        break;
    }
    ia_commit_run_free(&c);
    return ia_value(out, result);
}

/* (amalgamate ROWS) for `every`: ROWS are footprints, each linear with the
 * base.  The amalgamated commit refuses rows that take a common occurrence
 * or where one keeps what another takes; it is their concatenation. */

/* Write one kernel commit, before it fires: the regions it involves are
 * first aligned (outside changes become their own commits). */
static void ia_trace_sync_loc(Atom *loc_atom) {
    Space *space = ia_space_of(loc_atom);
    IaRegion *r = space ? ia_region_of(space, true) : NULL;
    if (r)
        ia_shadow_sync(r, NULL, NULL, 0u);
}

static void ia_trace_note_taken(IaRegion *r, uint64_t id);

/* An absence of sources as one predicate query: the union of the
 * (region, pattern) pairs its sources read, and every identity that matches
 * one of them now. */
static void ia_trace_query_union(Arena *a, Atom *parts, bool first) {
    IaVarMap pvm = {0};
    fputs(first ? "(rd (" : " (rd (", g_trace);
    bool any = false;
    for (CettaExprIndex k = 0; parts->kind == ATOM_EXPR && k < parts->expr.len; k++) {
        Atom *part = parts->expr.elems[k];
        IaRegion *r = ia_region_of(ia_space_of(part->expr.elems[0]), true);
        if (!r)
            continue;
        fputs(any ? " (" : "(", g_trace);
        ia_trace_region_name(g_trace, r);
        fputc(' ', g_trace);
        ia_trace_atom(g_trace, part->expr.elems[2], &pvm);
        fputc(')', g_trace);
        any = true;
    }
    fputs(") (", g_trace);
    uint64_t *ids = NULL;
    size_t n = 0u, cap = 0u;
    for (CettaExprIndex k = 0; parts->kind == ATOM_EXPR && k < parts->expr.len; k++) {
        Atom *part = parts->expr.elems[k];
        IaRegion *r = ia_region_of(ia_space_of(part->expr.elems[0]), true);
        if (!r)
            continue;
        CettaCount len = space_length64(r->space);
        for (CettaIndex i = 0; i < len; i++) {
            Atom *row = space_get_at64(r->space, i);
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
            if (!cetta_frame_identity_scope_try(&frame_identity_scope, &epoch))
                break;
            Bindings b;
            bindings_init(&b);
            bool hit = row && match_atoms_epoch(part->expr.elems[2], row, &b, a, epoch);
            bindings_free(&b);
            if (!hit)
                continue;
            uint64_t id = ia_shadow_id(r, i);
            bool known = false;
            for (size_t j = 0; j < n && !known; j++)
                known = ids[j] == id;
            if (known)
                continue;
            if (n == cap) {
                size_t c = cap ? cap * 2u : 16u;
                uint64_t *next = realloc(ids, c * sizeof(*next));
                if (!next)
                    break;
                ids = next;
                cap = c;
            }
            ids[n++] = id;
        }
    }
    for (size_t j = 0; j < n; j++)
        fprintf(g_trace, j ? " %llu" : "%llu", (unsigned long long)ids[j]);
    fputs("))", g_trace);
    free(ids);
    free(pvm.vars);
}

/* The kernel's own picks in an `every` commit, written as its first row: the
 * takes of the footprint, as a multiset, less the takes of every row.  A take
 * of the base that a row repeats is therefore listed in both rows. */
static void ia_trace_kernel_row(Atom *fp, Atom *rows) {
    typedef struct { Space *space; CettaIndex pos; bool used; } IaRowTake;
    size_t n = 0u;
    for (CettaExprIndex k = 0; k < rows->expr.len; k++) {
        Atom *row = rows->expr.elems[k];
        for (CettaExprIndex i = 0; row->kind == ATOM_EXPR && i < row->expr.len; i++)
            if (ia_head(row->expr.elems[i], "take", 5u))
                n++;
    }
    IaRowTake *rt = n ? calloc(n, sizeof(*rt)) : NULL;
    size_t len = 0u;
    for (CettaExprIndex k = 0; rt && k < rows->expr.len; k++) {
        Atom *row = rows->expr.elems[k];
        for (CettaExprIndex i = 0; row->kind == ATOM_EXPR && i < row->expr.len; i++) {
            Atom *e = row->expr.elems[i];
            if (!ia_head(e, "take", 5u))
                continue;
            rt[len++] = (IaRowTake){ia_space_of(e->expr.elems[1]),
                                    (CettaIndex)e->expr.elems[3]->ground.ival,
                                    false};
        }
    }
    fputc('(', g_trace);
    bool first = true;
    for (CettaExprIndex i = 0; i < fp->expr.len; i++) {
        Atom *e = fp->expr.elems[i];
        if (!ia_head(e, "take", 5u))
            continue;
        Space *space = ia_space_of(e->expr.elems[1]);
        CettaIndex pos = (CettaIndex)e->expr.elems[3]->ground.ival;
        bool accounted = false;
        for (size_t j = 0; j < len && !accounted; j++)
            if (!rt[j].used && rt[j].space == space && rt[j].pos == pos) {
                rt[j].used = true;
                accounted = true;
            }
        if (accounted)
            continue;
        IaRegion *r = ia_region_of(space, true);
        fprintf(g_trace, first ? "%llu" : " %llu",
                (unsigned long long)ia_shadow_id(r, pos));
        first = false;
    }
    fputc(')', g_trace);
    free(rt);
}

static void ia_trace_commit(Arena *a, Atom *seen, Atom *fp, Atom *makes,
                            Atom *rows, IaLoc *made, uint64_t **made_ids) {
    for (CettaExprIndex i = 0; i < seen->expr.len; i++) {
        Atom *e = seen->expr.elems[i];
        if (e->kind == ATOM_EXPR && e->expr.len == 2u)
            ia_trace_sync_loc(e->expr.elems[0]);
    }
    for (CettaExprIndex i = 0; i < fp->expr.len; i++) {
        Atom *e = fp->expr.elems[i];
        if (ia_head(e, "full-of", 2u)) {
            Atom *parts = e->expr.elems[1];
            for (CettaExprIndex k = 0; parts->kind == ATOM_EXPR &&
                                       k < parts->expr.len; k++)
                ia_trace_sync_loc(parts->expr.elems[k]->expr.elems[0]);
            continue;
        }
        ia_trace_sync_loc(e->expr.elems[1]);
    }
    for (CettaExprIndex i = 0; i < makes->expr.len; i++)
        ia_trace_sync_loc(makes->expr.elems[i]->expr.elems[0]);
    IaVarMap vm = {0};
    ia_trace_edge_begin();
    for (int pass = 0; pass < 2; pass++) {
        const char *kind = pass == 0 ? "take" : "keep";
        fputc('(', g_trace);
        bool first = true;
        for (CettaExprIndex i = 0; i < fp->expr.len; i++) {
            Atom *e = fp->expr.elems[i];
            if (!ia_head(e, kind, 5u))
                continue;
            IaRegion *r = ia_region_of(ia_space_of(e->expr.elems[1]), true);
            uint64_t id = ia_shadow_id(r, (CettaIndex)e->expr.elems[3]->ground.ival);
            fprintf(g_trace, first ? "%llu" : " %llu", (unsigned long long)id);
            if (pass == 0)
                ia_trace_note_taken(r, id);
            first = false;
        }
        fputs(") ", g_trace);
    }
    *made_ids = makes->expr.len ? calloc(makes->expr.len, sizeof(uint64_t)) : NULL;
    fputc('(', g_trace);
    for (CettaExprIndex i = 0; i < makes->expr.len; i++) {
        uint64_t id = g_trace_next_id++;
        if (*made_ids)
            (*made_ids)[i] = id;
        fprintf(g_trace, i ? " (%llu " : "(%llu ", (unsigned long long)id);
        ia_trace_region_name(g_trace, made[i].region);
        fputc(' ', g_trace);
        ia_trace_atom(g_trace, ia_strip(a, makes->expr.elems[i]->expr.elems[1]), &vm);
        fputc(')', g_trace);
    }
    fputs(") (", g_trace);
    bool first = true;
    for (CettaExprIndex i = 0; i < fp->expr.len; i++) {
        Atom *e = fp->expr.elems[i];
        if (ia_head(e, "full-of", 2u)) {
            ia_trace_query_union(a, e->expr.elems[1], first);
            first = false;
            continue;
        }
        if (!ia_head(e, "full", 5u))
            continue;
        IaRegion *r = ia_region_of(ia_space_of(e->expr.elems[1]), true);
        if (!r)
            continue;
        IaVarMap pvm = {0};
        fputs(first ? "(rd ((" : " (rd ((", g_trace);
        ia_trace_region_name(g_trace, r);
        fputc(' ', g_trace);
        ia_trace_atom(g_trace, e->expr.elems[3], &pvm);
        fputs(")) (", g_trace);
        ia_trace_seen(r, e->expr.elems[3], a);
        fputs("))", g_trace);
        free(pvm.vars);
        first = false;
    }
    fputs(") (", g_trace);
    /* only an `every` commit has rows; its kernel row comes first */
    bool kernel_row = rows && rows->kind == ATOM_EXPR && rows->expr.len > 0u;
    if (kernel_row)
        ia_trace_kernel_row(fp, rows);
    for (CettaExprIndex k = 0; rows && rows->kind == ATOM_EXPR && k < rows->expr.len; k++) {
        Atom *row = rows->expr.elems[k];
        fputs(kernel_row || k ? " (" : "(", g_trace);
        bool f2 = true;
        for (CettaExprIndex i = 0; row->kind == ATOM_EXPR && i < row->expr.len; i++) {
            Atom *e = row->expr.elems[i];
            if (!ia_head(e, "take", 5u))
                continue;
            IaRegion *r = ia_region_of(ia_space_of(e->expr.elems[1]), true);
            fprintf(g_trace, f2 ? "%llu" : " %llu", (unsigned long long)
                    ia_shadow_id(r, (CettaIndex)e->expr.elems[3]->ground.ival));
            f2 = false;
        }
        fputc(')', g_trace);
    }
    fputc(')', g_trace);
    free(vm.vars);
    ia_trace_edge_end();
}

/* After the commit fired: the kernel's takes leave the shadow by identity,
 * then each region it touched is aligned with what it made. */
typedef struct { IaRegion *region; uint64_t id; } IaTakenId;
static IaTakenId *g_trace_taken;
static size_t g_trace_taken_len, g_trace_taken_cap;

static void ia_trace_note_taken(IaRegion *r, uint64_t id) {
    if (g_trace_taken_len == g_trace_taken_cap) {
        size_t c = g_trace_taken_cap ? g_trace_taken_cap * 2u : 64u;
        IaTakenId *t = realloc(g_trace_taken, c * sizeof(*t));
        if (!t)
            return;
        g_trace_taken = t;
        g_trace_taken_cap = c;
    }
    g_trace_taken[g_trace_taken_len++] = (IaTakenId){r, id};
}

static void ia_trace_after(Atom *makes, IaLoc *made, uint64_t *made_ids) {
    IaRegion *touched[128];
    size_t nt = 0u;
    for (size_t t = 0; t < g_trace_taken_len; t++) {
        IaRegion *r = g_trace_taken[t].region;
        for (size_t i = 0; i < r->len; i++)
            if (r->ids[i] == g_trace_taken[t].id) {
                memmove(r->ids + i, r->ids + i + 1u, (r->len - i - 1u) * sizeof(*r->ids));
                memmove(r->atoms + i, r->atoms + i + 1u, (r->len - i - 1u) * sizeof(*r->atoms));
                r->len--;
                break;
            }
        bool have = false;
        for (size_t k = 0; k < nt; k++)
            have = have || touched[k] == r;
        if (!have && nt < 128u)
            touched[nt++] = r;
    }
    g_trace_taken_len = 0u;
    for (CettaExprIndex i = 0; i < makes->expr.len; i++) {
        bool have = false;
        for (size_t k = 0; k < nt; k++)
            have = have || touched[k] == made[i].region;
        if (!have && nt < 128u)
            touched[nt++] = made[i].region;
    }
    for (size_t k = 0; k < nt; k++) {
        IaRegion *r = touched[k];
        uint64_t *ids = makes->expr.len ? calloc(makes->expr.len, sizeof(*ids)) : NULL;
        Atom **atoms = makes->expr.len ? calloc(makes->expr.len, sizeof(*atoms)) : NULL;
        size_t n = 0u;
        for (CettaExprIndex j = 0; j < makes->expr.len; j++)
            if (made[j].region == r) {
                ids[n] = made_ids ? made_ids[j] : 0u;
                atoms[n++] = makes->expr.elems[j]->expr.elems[1];
            }
        g_trace_after_kernel = true;
        ia_shadow_sync(r, ids, atoms, n);
        g_trace_after_kernel = false;
        free(ids);
        free(atoms);
    }
}

static bool op_amalgamate(Arena *a, Atom **args, uint32_t nargs,
                          CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    Atom *rows = ia_strip(a, args[0]);
    if (!rows || rows->kind != ATOM_EXPR)
        return ia_value(out, ia_refusal(a, atom_symbol(a, "malformed-rows")));
    IaOcc *occs = NULL;
    size_t len = 0u, cap = 0u;
    IaList all = {0};
    for (CettaExprIndex r = 0; r < rows->expr.len; r++) {
        Atom *fp = rows->expr.elems[r];
        if (fp->kind != ATOM_EXPR)
            continue;
        (void)ia_occs_push(&occs, &len, &cap, fp, (uint32_t)r);
        for (CettaExprIndex i = 0; i < fp->expr.len; i++)
            (void)ia_push(&all, fp->expr.elems[i]);
    }
    Atom *why = ia_occs_conflict(a, occs, len, true);
    free(occs);
    if (why) {
        free(all.items);
        return ia_value(out, ia_refusal(a, why));
    }
    Atom *result = ia_list_atom(a, &all);
    free(all.items);
    return ia_value(out, atom_expr2(a, atom_symbol(a, "ia:amalgamated"), result));
}

/* (formed DEFAULT SOURCES TEMPLATE) -> True, or (ia:refused REASON) for a
 * text that is not formed: a location written as a variable no earlier
 * source binds (it would read every region: refused for everyone), or a
 * written location without the right its use needs.  Locations that only
 * data supplies are checked when the walk reaches them. */
typedef struct {
    Arena *a;
    Atom *default_loc;
    IaList bound; /* variables bound by earlier sources */
    Atom *why;
} IaForm;

static bool ia_form_bound(IaForm *f, Atom *v) {
    for (size_t i = 0; i < f->bound.len; i++)
        if (atom_eq(f->bound.items[i], v))
            return true;
    return false;
}

static void ia_form_bind(IaForm *f, Atom *pattern) {
    if (!pattern)
        return;
    if (pattern->kind == ATOM_VAR) {
        if (!ia_form_bound(f, pattern) && !ia_push(&f->bound, pattern))
            f->why = atom_symbol(f->a, "no-memory");
        return;
    }
    if (pattern->kind == ATOM_EXPR)
        for (CettaExprIndex i = 0; i < pattern->expr.len; i++)
            ia_form_bind(f, pattern->expr.elems[i]);
}

static void ia_form_loc(IaForm *f, Atom *loc, uint32_t needed) {
    if (f->why || !loc)
        return;
    if (loc->kind == ATOM_VAR) {
        if (!ia_form_bound(f, loc))
            f->why = atom_expr2(f->a, atom_symbol(f->a, "location-variable"), loc);
        return;
    }
    if (atom_has_vars(loc)) {
        f->why = atom_expr2(f->a, atom_symbol(f->a, "location-pattern"), loc);
        return;
    }
    IaLoc l;
    Atom *why = ia_resolve(f->a, loc, &l);
    /* A location is a name: a space, a handle, or a variable an earlier
     * source binds.  Code in a location position is not run (templates and
     * sources are data), so it names nothing. */
    f->why = why ? why : ia_check_right(f->a, &l, needed);
}

static void ia_form_sources(IaForm *f, Atom *src);
static void ia_form_absent_sources(IaForm *f, Atom *src);

static void ia_form_source(IaForm *f, Atom *c) {
    if (f->why || !c)
        return;
    if (ia_head_any(c, ",") ) {
        ia_form_sources(f, c);
        return;
    }
    if (c->kind == ATOM_EXPR && c->expr.len == 0u)
        return;
    if (ia_head(c, "let", 3u)) {
        ia_form_bind(f, c->expr.elems[1]);
        return;
    }
    if (ia_head(c, "every", 2u)) {
        ia_form_source(f, c->expr.elems[1]);
        return;
    }
    if (ia_head(c, "absent", 2u) || ia_head(c, "absent", 3u)) {
        if (c->expr.len == 3u)
            ia_form_loc(f, c->expr.elems[1], IA_OBSERVE);
        else if (ia_source_form(c->expr.elems[1])) {
            size_t outer = f->bound.len;
            ia_form_absent_sources(f, c->expr.elems[1]);
            f->bound.len = outer;
        } else
            ia_form_loc(f, f->default_loc, IA_OBSERVE);
        return;
    }
    bool unique = ia_head_any(c, "unique") && (c->expr.len == 2u || c->expr.len == 3u);
    bool all = ia_head_any(c, "all") && (c->expr.len == 3u || c->expr.len == 4u);
    if (unique || all) {
        IaWalk w = {.a = f->a, .default_loc = f->default_loc};
        IaMatched q;
        Atom *coll = NULL;
        if (!ia_quantified(&w, c, all, &q, &coll))
            return;
        ia_form_loc(f, q.loc, IA_OBSERVE | (q.use == IA_USE_TAKE ? IA_RECEIVE : 0u));
        if (unique)
            ia_form_bind(f, q.pattern);
        if (coll)
            ia_form_bind(f, coll);
        return;
    }
    IaWalk w = {.a = f->a, .default_loc = f->default_loc};
    IaMatched q;
    ia_matched(&w, c, &q);
    ia_form_loc(f, q.loc, q.use == IA_USE_TAKE ? IA_RECEIVE : IA_PEEK);
    ia_form_bind(f, q.pattern);
}

static void ia_form_sources(IaForm *f, Atom *src) {
    if (ia_head_any(src, ",")) {
        for (CettaExprIndex i = 1; i < src->expr.len && !f->why; i++)
            ia_form_source(f, src->expr.elems[i]);
        return;
    }
    ia_form_source(f, src);
}

/* An absence of sources reads the union of its sources' regions as one
 * predicate query: each source is formed as written, and each location that
 * a matched source names must also observe.  A location an earlier source
 * binds is checked when the walk reaches it. */
static void ia_form_absent_sources(IaForm *f, Atom *src) {
    ia_form_sources(f, src);
    Atom *one = NULL;
    Atom **cs;
    size_t n;
    ia_conjuncts(src, &one, &cs, &n);
    for (size_t i = 0; i < n && !f->why; i++) {
        Atom *c = cs[i];
        if ((c->kind == ATOM_EXPR && c->expr.len == 0u) ||
            ia_head_any(c, ",") || ia_head(c, "let", 3u) ||
            ia_head(c, "every", 2u) || ia_head(c, "absent", 2u) ||
            ia_head(c, "absent", 3u) || ia_head_any(c, "unique") ||
            ia_head_any(c, "all"))
            continue;
        IaWalk w = {.a = f->a, .default_loc = f->default_loc};
        IaMatched q;
        ia_matched(&w, c, &q);
        if (q.loc && q.loc->kind == ATOM_VAR)
            continue;
        ia_form_loc(f, q.loc, IA_OBSERVE);
    }
}

static void ia_form_template(IaForm *f, Atom *t) {
    if (f->why || !t)
        return;
    if (ia_head_any(t, ",")) {
        for (CettaExprIndex i = 1; i < t->expr.len; i++)
            ia_form_template(f, t->expr.elems[i]);
        return;
    }
    if (ia_head(t, "add-atom", 3u))
        ia_form_loc(f, t->expr.elems[1], IA_SEND);
}

static bool op_formed(Arena *a, Atom **args, uint32_t nargs,
                      CettaCallOutcome *out) {
    if (nargs != 3u)
        return false;
    IaForm f = {.a = a, .default_loc = ia_strip(a, args[0])};
    Atom *sources = ia_unhold(args[1]);
    Atom *tpl = ia_unhold(args[2]);
    ia_form_sources(&f, sources);
    ia_form_template(&f, tpl);
    free(f.bound.items);
    return ia_value(out, f.why ? ia_refusal(a, f.why) : atom_true(a));
}


static Atom *ia_keep(Atom *x);
static uint64_t ia_park(Atom *firing, bool always, Atom *seen);

/* ── the agenda: parked firings and ready processes ───────────────────── */

/* A parked firing waits for a change in a region its last attempt read.
 * The wait index is the record of what that attempt read, with the
 * revision it read: a firing is woken exactly when one of those regions
 * has changed since, so no wakeup is lost, whatever wrote the region.
 * `await` drops its entry when it fires; `whenever` keeps it.  Ready
 * processes are run one step at a time; the policy picks which next. */

typedef struct {
    Space *space;
    uint64_t rev;
    SpaceReadToken read; /* issued computed read, or zero for the region form */
} IaSeenAt;

typedef struct {
    uint64_t ticket;
    bool always;
    bool live;
    bool needs_recheck;
    Atom *firing;
    IaSeenAt *seen;
    size_t seen_len;
} IaParked;

typedef enum { IA_POLICY_FIFO, IA_POLICY_LIFO, IA_POLICY_SEEDED } IaPolicy;

static pthread_mutex_t g_agenda_gate = PTHREAD_MUTEX_INITIALIZER;
static IaParked *g_parked;
static size_t g_parked_len, g_parked_cap;
static Atom **g_ready;
static size_t g_ready_len, g_ready_cap;
static uint64_t g_next_ticket = 1u;
static IaPolicy g_policy = IA_POLICY_FIFO;
static uint64_t g_seed = 0u;
static bool g_policy_read_env;
static bool g_draining;
static uint64_t g_stat_attempts, g_stat_fires, g_stat_parks, g_stat_woken,
    g_stat_still;

static uint64_t ia_random(void) {
    /* xorshift64*: a reproducible schedule from the seed */
    uint64_t x = g_seed ? g_seed : UINT64_C(0x9E3779B97F4A7C15);
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_seed = x;
    return x * UINT64_C(0x2545F4914F6CDD1D);
}

static void ia_policy_from_env(void) {
    if (g_policy_read_env)
        return;
    g_policy_read_env = true;
    const char *seed = getenv("CETTA_INTERACTION_SEED");
    if (seed && *seed) {
        g_policy = IA_POLICY_SEEDED;
        g_seed = strtoull(seed, NULL, 10) * UINT64_C(2654435761) + 1u;
    }
}

static bool ia_seen_from(Atom *seen, IaSeenAt **out, size_t *len) {
    *out = NULL;
    *len = 0u;
    if (!seen || seen->kind != ATOM_EXPR)
        return true;
    IaSeenAt *items = seen->expr.len ? calloc(seen->expr.len, sizeof(*items)) : NULL;
    if (seen->expr.len && !items)
        return false;
    size_t n = 0u;
    for (CettaExprIndex i = 0; i < seen->expr.len; i++) {
        Atom *e = seen->expr.elems[i];
        if (ia_head_any(e, "ia:captured-read")) {
            SpaceReadToken read;
            if (!ia_captured_read_token(e, &read)) {
                free(items);
                return false;
            }
            items[n++] = (IaSeenAt){.space = (Space *)read.space,
                .rev = read.revision, .read = read};
            continue;
        }
        if (e->kind != ATOM_EXPR || e->expr.len != 2u ||
            atom_is_symbol(e->expr.elems[0], "ia:seen"))
            continue;
        Space *space = ia_space_of(e->expr.elems[0]);
        Atom *rev = e->expr.elems[1];
        if (!space || rev->kind != ATOM_GROUNDED || rev->ground.gkind != GV_INT)
            continue;
        items[n++] = (IaSeenAt){.space = space, .rev = (uint64_t)rev->ground.ival};
    }
    *out = items;
    *len = n;
    return true;
}

static bool ia_parked_changed(const IaParked *p) {
    if (p->needs_recheck || p->seen_len == 0u)
        return true;
    for (size_t i = 0; i < p->seen_len; i++)
        if (p->seen[i].read.space ? !space_read_token_is_current(p->seen[i].read)
                                : space_revision(p->seen[i].space) != p->seen[i].rev)
            return true;
    return false;
}

static IaParked *ia_parked_find(uint64_t ticket) {
    for (size_t i = 0; i < g_parked_len; i++)
        if (g_parked[i].live && g_parked[i].ticket == ticket)
            return &g_parked[i];
    return NULL;
}

static bool ia_ticket_of(Atom *x, uint64_t *ticket) {
    x = ia_unhold(x);
    if (ia_head(x, "ia:ticket", 2u))
        x = x->expr.elems[1];
    if (!x || x->kind != ATOM_GROUNDED || x->ground.gkind != GV_INT ||
        x->ground.ival <= 0)
        return false;
    *ticket = (uint64_t)x->ground.ival;
    return true;
}

/* SEEN as (ia:seen (LOC REV) ...) or ((LOC REV) ...). */
static bool ia_seen_at(Atom *seen, IaSeenAt **out, size_t *len) {
    return ia_seen_from(ia_unhold(seen), out, len);
}

/* (agenda-park KIND FIRING SEEN) -> (ia:ticket N) */
static bool op_agenda_park(Arena *a, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    if (nargs != 3u)
        return false;
    Atom *kind = ia_strip(a, args[0]);
    uint64_t ticket = ia_park(args[1], kind && atom_is_symbol(kind, "always"), args[2]);
    return ia_value(out, ticket ? atom_expr2(a, atom_symbol(a, "ia:ticket"),
        atom_int(a, (int64_t)ticket)) : ia_refusal(a, atom_symbol(a, "no-memory")));
}

/* (agenda-woken) -> ((ia:woken TICKET KIND FIRING) ...), the live parked
 * firings whose read regions changed, in policy order. */
static bool op_agenda_woken(Arena *a, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 0u && nargs != 1u)
        return false;
    ia_policy_from_env();
    IaList list = {0};
    pthread_mutex_lock(&g_agenda_gate);
    size_t live = 0u;
    for (size_t i = 0; i < g_parked_len; i++) {
        IaParked *p = &g_parked[i];
        if (!p->live)
            continue;
        live++;
        if (!ia_parked_changed(p)) {
            g_stat_still++;
            continue;
        }
        g_stat_woken++;
        Atom *items[4] = {atom_symbol(a, "ia:woken"),
                          atom_int(a, (int64_t)p->ticket),
                          atom_symbol(a, p->always ? "always" : "once"),
                          p->firing};
        (void)ia_push(&list, atom_expr(a, items, 4u));
    }
    /* compact dead entries now and then */
    if (live * 2u < g_parked_len && g_parked_len > 64u) {
        size_t k = 0u;
        for (size_t i = 0; i < g_parked_len; i++) {
            if (g_parked[i].live)
                g_parked[k++] = g_parked[i];
            else
                free(g_parked[i].seen);
        }
        g_parked_len = k;
    }
    if (g_policy == IA_POLICY_LIFO)
        for (size_t i = 0; i < list.len / 2u; i++) {
            Atom *t = list.items[i];
            list.items[i] = list.items[list.len - 1u - i];
            list.items[list.len - 1u - i] = t;
        }
    else if (g_policy == IA_POLICY_SEEDED)
        for (size_t i = list.len; i > 1u; i--) {
            size_t j = (size_t)(ia_random() % i);
            Atom *t = list.items[i - 1u];
            list.items[i - 1u] = list.items[j];
            list.items[j] = t;
        }
    pthread_mutex_unlock(&g_agenda_gate);
    Atom *result = ia_list_atom(a, &list);
    free(list.items);
    return ia_value(out, result);
}

/* (agenda-seen TICKET SEEN): what the firing's last attempt read. */
static bool op_agenda_seen(Arena *a, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    if (nargs != 2u)
        return false;
    uint64_t ticket;
    if (!ia_ticket_of(args[0], &ticket))
        return ia_value(out, atom_false(a));
    IaSeenAt *observed = NULL;
    size_t count = 0u;
    if (!ia_seen_at(args[1], &observed, &count))
        return ia_value(out, ia_refusal(a, atom_symbol(a, "no-memory")));
    pthread_mutex_lock(&g_agenda_gate);
    IaParked *p = ia_parked_find(ticket);
    if (p) {
        free(p->seen);
        p->seen = observed;
        p->seen_len = count;
        p->needs_recheck = false;
    }
    pthread_mutex_unlock(&g_agenda_gate);
    if (!p)
        free(observed);
    return ia_value(out, p ? atom_true(a) : atom_false(a));
}

/* (agenda-drop TICKET) -> True when the entry was live: taking the parked
 * occurrence, by its firing or by a cancellation, happens once. */
static bool op_agenda_drop(Arena *a, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    uint64_t ticket;
    if (!ia_ticket_of(args[0], &ticket))
        return ia_value(out, atom_false(a));
    pthread_mutex_lock(&g_agenda_gate);
    IaParked *p = ia_parked_find(ticket);
    if (p)
        p->live = false;
    pthread_mutex_unlock(&g_agenda_gate);
    return ia_value(out, p ? atom_true(a) : atom_false(a));
}

static bool op_agenda_push(Arena *a, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    Atom *x = ia_keep(ia_unhold(args[0]));
    if (!x)
        return false;
    pthread_mutex_lock(&g_agenda_gate);
    if (g_ready_len == g_ready_cap) {
        size_t cap = g_ready_cap ? g_ready_cap * 2u : 64u;
        Atom **items = realloc(g_ready, cap * sizeof(*items));
        if (!items) {
            pthread_mutex_unlock(&g_agenda_gate);
            return false;
        }
        g_ready = items;
        g_ready_cap = cap;
    }
    g_ready[g_ready_len++] = x;
    pthread_mutex_unlock(&g_agenda_gate);
    return ia_value(out, atom_unit(a));
}

/* (agenda-pop) -> (ia:ready PROCESS) or ia:empty */
static bool op_agenda_pop(Arena *a, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 0u && nargs != 1u)
        return false;
    ia_policy_from_env();
    pthread_mutex_lock(&g_agenda_gate);
    Atom *x = NULL;
    if (g_ready_len) {
        size_t i = g_policy == IA_POLICY_LIFO ? g_ready_len - 1u
                 : g_policy == IA_POLICY_SEEDED ? (size_t)(ia_random() % g_ready_len)
                 : 0u;
        x = g_ready[i];
        memmove(g_ready + i, g_ready + i + 1u,
                (g_ready_len - i - 1u) * sizeof(*g_ready));
        g_ready_len--;
    }
    pthread_mutex_unlock(&g_agenda_gate);
    return ia_value(out, x ? atom_expr2(a, atom_symbol(a, "ia:ready"), x)
                           : atom_symbol(a, "ia:empty"));
}

/* (agenda-enter) -> True for the outermost driver; (agenda-leave) */
static bool op_agenda_enter(Arena *a, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 0u && nargs != 1u)
        return false;
    pthread_mutex_lock(&g_agenda_gate);
    bool first = !g_draining;
    g_draining = true;
    pthread_mutex_unlock(&g_agenda_gate);
    return ia_value(out, first ? atom_true(a) : atom_false(a));
}

static bool op_agenda_leave(Arena *a, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 0u && nargs != 1u)
        return false;
    pthread_mutex_lock(&g_agenda_gate);
    g_draining = false;
    pthread_mutex_unlock(&g_agenda_gate);
    return ia_value(out, atom_unit(a));
}

/* (agenda-policy fifo | lifo | (seeded N)) */
static bool op_agenda_policy(Arena *a, Atom **args, uint32_t nargs,
                             CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    Atom *p = ia_strip(a, args[0]);
    pthread_mutex_lock(&g_agenda_gate);
    g_policy_read_env = true;
    bool ok = true;
    if (p && atom_is_symbol(p, "fifo"))
        g_policy = IA_POLICY_FIFO;
    else if (p && atom_is_symbol(p, "lifo"))
        g_policy = IA_POLICY_LIFO;
    else if (ia_head(p, "seeded", 2u) &&
             p->expr.elems[1]->kind == ATOM_GROUNDED &&
             p->expr.elems[1]->ground.gkind == GV_INT) {
        g_policy = IA_POLICY_SEEDED;
        g_seed = (uint64_t)p->expr.elems[1]->ground.ival * UINT64_C(2654435761) + 1u;
    } else
        ok = false;
    pthread_mutex_unlock(&g_agenda_gate);
    return ia_value(out, ok ? atom_unit(a)
                            : ia_refusal(a, atom_expr2(a, atom_symbol(a, "unknown-policy"), p)));
}

static bool op_agenda_count(Arena *a, Atom **args, uint32_t nargs,
                            CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    Atom *what = ia_strip(a, args[0]);
    pthread_mutex_lock(&g_agenda_gate);
    if (what && atom_is_symbol(what, "attempt"))
        g_stat_attempts++;
    else if (what && atom_is_symbol(what, "fire"))
        g_stat_fires++;
    pthread_mutex_unlock(&g_agenda_gate);
    return ia_value(out, atom_unit(a));
}

static bool op_agenda_stats(Arena *a, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 0u && nargs != 1u)
        return false;
    pthread_mutex_lock(&g_agenda_gate);
    size_t live = 0u;
    for (size_t i = 0; i < g_parked_len; i++)
        live += g_parked[i].live ? 1u : 0u;
    Atom *items[8] = {
        atom_symbol(a, "agenda"),
        atom_expr2(a, atom_symbol(a, "parked"), atom_int(a, (int64_t)live)),
        atom_expr2(a, atom_symbol(a, "ready"), atom_int(a, (int64_t)g_ready_len)),
        atom_expr2(a, atom_symbol(a, "parks"), atom_int(a, (int64_t)g_stat_parks)),
        atom_expr2(a, atom_symbol(a, "attempts"), atom_int(a, (int64_t)g_stat_attempts)),
        atom_expr2(a, atom_symbol(a, "fires"), atom_int(a, (int64_t)g_stat_fires)),
        atom_expr2(a, atom_symbol(a, "woken"), atom_int(a, (int64_t)g_stat_woken)),
        atom_expr2(a, atom_symbol(a, "unchanged"), atom_int(a, (int64_t)g_stat_still)),
    };
    pthread_mutex_unlock(&g_agenda_gate);
    return ia_value(out, atom_expr(a, items, 8u));
}

/* (instantiate PATTERN VALUE TERM) -> TERM under the match of PATTERN
 * against VALUE, or no answer.  TERM is syntax; nothing is evaluated. */
static bool op_instantiate(Arena *a, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    if (nargs != 3u)
        return false;
    Atom *pattern = ia_unhold(args[0]);
    Atom *value = ia_strip(a, args[1]);
    Atom *term = ia_unhold(args[2]);
    Bindings b;
    bindings_init(&b);
    Atom *result = match_atoms(pattern, value, &b, a)
        ? ia_apply_held(&b, a, term) : NULL;
    bindings_free(&b);
    return ia_value(out, result);
}

/* (changed SEEN) -> whether a region read has changed since. */
static bool op_changed(Arena *a, Atom **args, uint32_t nargs,
                       CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    IaParked probe = {0};
    ia_seen_at(args[0], &probe.seen, &probe.seen_len);
    bool changed = probe.seen_len == 0u || ia_parked_changed(&probe);
    free(probe.seen);
    return ia_value(out, changed ? atom_true(a) : atom_false(a));
}

/* (is-handle X) */
static bool op_is_handle(Arena *a, Atom **args, uint32_t nargs,
                         CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    return ia_value(out, ia_handle_of(ia_strip(a, args[0])) ? atom_true(a)
                                                           : atom_false(a));
}


/* ── the firing machine ───────────────────────────────────────────────── */

/* One firing (a transform, or a choice among transforms) runs here, from
 * the walk to the commit.  It stops only to have code run: the expression
 * of a `let`.  Then it answers (ia:need MACHINE E) and the library resumes
 * it with E's values.  Its other answers are (ia:done OUTCOME):
 *   (ia:fired X)     committed; X is the continuation to run (held code)
 *   ia:none          no candidate is enabled
 *   ia:parked        parked on the agenda
 *   ia:quiescent     the agenda has nothing to run
 *   (ia:process P)   a ready process to run (held code)
 *   (Error ...)      not formed, or refused */

static Atom *ia_call_term(bool (*op)(Arena *, Atom **, uint32_t, CettaCallOutcome *),
                          Arena *a, Atom **args, uint32_t n) {
    CettaCallOutcome out = cetta_call_failure();
    if (!op(a, args, n, &out) || out.kind != CETTA_CALL_VALUE)
        return NULL;
    return out.term;
}

static pthread_mutex_t g_store_gate = PTHREAD_MUTEX_INITIALIZER;
static Arena g_store;
static bool g_store_ready;

/* A copy that lives as long as the session: parked firings and ready
 * processes outlast the call that made them. */
static Atom *ia_keep(Atom *x) {
    if (!x)
        return NULL;
    pthread_mutex_lock(&g_store_gate);
    if (!g_store_ready) {
        arena_init_detached(&g_store);
        g_store_ready = true;
    }
    Atom *copy = atom_deep_copy(&g_store, x);
    pthread_mutex_unlock(&g_store_gate);
    return copy;
}

typedef enum { IA_JOB_WALK, IA_JOB_LET } IaJobKind;

typedef struct {
    IaJobKind kind;
    Atom *state;
    Atom *pattern, *expr;
    int ctx; /* -1: the kernel; otherwise the group owning this job */
} IaJob;

typedef enum { IA_CTX_EVERY, IA_CTX_ABSENT } IaCtxKind;

typedef struct {
    size_t outstanding, row_count;
    IaCtxKind kind;
    int parent;
    bool observe; /* inside an absence query, including nested every groups */
    IaList fps, tpls;
    IaList scans;
    Atom *base, *continuation;
} IaCtx;

typedef enum { IA_MODE_NOW, IA_MODE_AWAIT, IA_MODE_AGENDA } IaMode;

typedef struct {
    atomic_uint holders;
    Arena arena;
    IaMode mode;
    Atom **alts;
    size_t nalts, alt_i;
    bool in_attempt;
    Atom *region, *dest;
    IaJob *jobs;
    size_t njobs, capjobs, jobhead;
    IaCtx *ctxs;
    size_t nctxs, capctxs;
    IaList cands, seen, all_seen;
    Atom *refused, *stuck;
    IaJob let_job;
    bool waiting;
    bool computing; /* a borrowed evaluator owns the pending request */
    bool cancelled;
    bool retired;
    uint64_t request;
    Atom *firing; /* as parked, for an await that does not fire now */
    Atom *current; /* the alternative being attempted */
    CettaInteractionLane lane;
    bool instances_only; /* answer the rows' instances; the caller routes them */
    bool raw_values;     /* the caller protected its code positions */
    const CettaInteractionHost *host; /* during cetta_interaction_fire only */
    bool full;     /* enumerate every row (after a first-row attempt failed) */
    bool replay_safe; /* source matching alone can be repeated; computed requests cannot */
    bool truncated;
    uint64_t *woken;
    size_t nwoken, woken_i;
    uint64_t ticket;
    bool always;
} IaMachine;

static const char g_machine_kind = 'm';

static bool ia_machine_observe_read(void *context, SpaceReadToken read) {
    IaMachine *m = context;
    if (!read.space || !read.instance_id || m->refused)
        return false;
    for (size_t i = 0; i < m->seen.len; i++) {
        SpaceReadToken old;
        if (ia_captured_read_token(m->seen.items[i], &old) &&
            old.space == read.space && old.instance_id == read.instance_id &&
            old.revision == read.revision && old.prefix_epoch == read.prefix_epoch &&
            old.base_prefix_epoch == read.base_prefix_epoch &&
            old.base_dependency_epoch == read.base_dependency_epoch)
            return true;
    }
    Atom *entry = ia_captured_read(&m->arena, read);
    if (!entry || !ia_push(&m->seen, entry)) {
        m->refused = atom_symbol(&m->arena, "no-memory");
        return false;
    }
    return true;
}

static void ia_machine_reset_attempt(IaMachine *m) {
    m->njobs = 0u;
    m->jobhead = 0u;
    for (size_t i = 0; i < m->nctxs; i++) {
        free(m->ctxs[i].fps.items);
        free(m->ctxs[i].tpls.items);
        free(m->ctxs[i].scans.items);
    }
    m->nctxs = 0u;
    free(m->cands.items);
    m->cands = (IaList){0};
    free(m->seen.items);
    m->seen = (IaList){0};
    m->refused = m->stuck = NULL;
    m->in_attempt = false;
    m->waiting = false;
}

static void ia_machine_retain(void *resource) {
    IaMachine *m = resource;
    atomic_fetch_add(&m->holders, 1u);
}

/* Issued tokens may outlive the terminal reply so that reusing one can be
 * refused. They need the request identity, not the finished walk's storage.
 * Replies and parked firings have already acquired their own Atom owners. */
static void ia_machine_retire(IaMachine *m) {
    if (!m || m->retired)
        return;
    m->retired = true;
    ia_machine_reset_attempt(m);
    free(m->jobs); m->jobs = NULL; m->capjobs = 0u;
    free(m->ctxs); m->ctxs = NULL; m->capctxs = 0u;
    free(m->all_seen.items);
    m->all_seen = (IaList){0};
    free(m->alts); m->alts = NULL; m->nalts = m->alt_i = 0u;
    free(m->woken); m->woken = NULL; m->nwoken = m->woken_i = 0u;
    m->region = m->dest = m->firing = m->current = NULL;
    m->let_job = (IaJob){0};
    arena_free(&m->arena);
}

static void ia_machine_release(void *resource) {
    IaMachine *m = resource;
    if (!m || atomic_fetch_sub(&m->holders, 1u) != 1u)
        return;
    ia_machine_retire(m);
    free(m);
}

static IaMachine *ia_machine_new(IaMode mode) {
    IaMachine *m = calloc(1u, sizeof(*m));
    if (!m)
        return NULL;
    atomic_init(&m->holders, 1u);
    arena_init_detached(&m->arena);
    m->mode = mode;
    return m;
}

typedef struct {
    atomic_uint holders;
    IaMachine *machine;
    uint64_t request;
} IaResumeToken;

static void ia_resume_token_retain(void *resource) {
    IaResumeToken *token = resource;
    atomic_fetch_add(&token->holders, 1u);
}

static void ia_resume_token_release(void *resource) {
    IaResumeToken *token = resource;
    if (!token || atomic_fetch_sub(&token->holders, 1u) != 1u)
        return;
    ia_machine_release(token->machine);
    free(token);
}

/* Each suspension owns one particular request, rather than a reusable
 * authority to submit values to whichever request happens to be current. */
static Atom *ia_machine_atom(Arena *a, IaMachine *m) {
    CettaSealObject *root = ia_root();
    IaResumeToken *token = calloc(1u, sizeof(*token));
    if (!root || !token) {
        free(token);
        return NULL;
    }
    atomic_init(&token->holders, 1u);
    token->machine = m;
    token->request = m->request;
    ia_machine_retain(m);
    CettaSealNativePayload payload = {
        .kind = &g_machine_kind, .resource = token,
        .retain = ia_resume_token_retain, .release = ia_resume_token_release,
    };
    CettaSealObject *capsule = NULL;
    CettaSealStatus status = cetta_seal_native(root, &payload, &capsule);
    ia_resume_token_release(token);
    if (status != CETTA_SEAL_OK)
        return NULL;
    Atom *atom = cetta_seal_object_atom(a, capsule);
    cetta_seal_release(capsule);
    return atom;
}

static IaResumeToken *ia_resume_token_of(Atom *atom) {
    CettaSealObject *root = ia_root();
    CettaSealObject *capsule = atom ? cetta_seal_atom_object(atom) : NULL;
    void *resource = NULL;
    if (!root || !capsule ||
        cetta_seal_unseal_native(root, capsule, &g_machine_kind, &resource) !=
            CETTA_SEAL_OK)
        return NULL;
    return resource;
}

static void ia_machine_cancel(IaMachine *m) {
    m->cancelled = true;
    m->waiting = false;
    m->in_attempt = false;
    m->njobs = m->jobhead = 0u;
}

static bool ia_job_push(IaMachine *m, IaJob job) {
    if (job.ctx >= 0 && m->ctxs[job.ctx].outstanding == SIZE_MAX)
        return false;
    if (m->njobs == m->capjobs) {
        size_t cap = m->capjobs ? m->capjobs * 2u : 16u;
        IaJob *jobs = realloc(m->jobs, cap * sizeof(*jobs));
        if (!jobs)
            return false;
        m->jobs = jobs;
        m->capjobs = cap;
    }
    if (job.ctx >= 0)
        m->ctxs[job.ctx].outstanding++;
    m->jobs[m->njobs++] = job;
    return true;
}

static int ia_ctx_new(IaMachine *m, IaCtxKind kind, int parent, Atom *base,
                       Atom *continuation) {
    if (m->nctxs >= INT_MAX ||
        (parent >= 0 && m->ctxs[parent].outstanding == SIZE_MAX))
        return -1;
    if (m->nctxs == m->capctxs) {
        size_t cap = m->capctxs ? m->capctxs * 2u : 4u;
        IaCtx *ctxs = realloc(m->ctxs, cap * sizeof(*ctxs));
        if (!ctxs)
            return -1;
        m->ctxs = ctxs;
        m->capctxs = cap;
    }
    bool observe = kind == IA_CTX_ABSENT || (parent >= 0 && m->ctxs[parent].observe);
    m->ctxs[m->nctxs] = (IaCtx){.kind = kind, .parent = parent, .observe = observe, .base = base,
                                .continuation = continuation};
    /* A child group holds its parent until it has delivered its result or
     * scheduled the saved continuation. */
    if (parent >= 0)
        m->ctxs[parent].outstanding++;
    return (int)m->nctxs++;
}

static size_t ia_open_ctxs(IaMachine *m) {
    size_t n = 0u;
    for (size_t i = 0; i < m->nctxs; i++)
        n += m->ctxs[i].outstanding > 0 ? 1u : 0u;
    return n;
}

static Atom *ia_cand(Arena *a, Atom *fp, Atom *tpls) {
    return atom_expr3(a, atom_symbol(a, "ia:cand"), fp, tpls);
}

/* A completed source row belongs to its enclosing group. Negative
 * queries count rows before linear admission; every filters its body rows
 * before summing them. The aggregate itself can still fail commit admission. */
static void ia_deliver_row(IaMachine *m, int parent, Atom *fp, Atom *templates,
                            Atom *rows) {
    Arena *a = &m->arena;
    if (parent < 0) {
        Atom *items[4] = {atom_symbol(a, "ia:cand"), fp, templates, rows};
        Atom *candidate = rows ? atom_expr(a, items, 4u) : ia_cand(a, fp, templates);
        if (!ia_push(&m->cands, candidate))
            m->refused = atom_symbol(a, "no-memory");
        return;
    }
    IaCtx *ctx = &m->ctxs[parent];
    if (ctx->kind == IA_CTX_ABSENT) {
        if (ctx->row_count == SIZE_MAX)
            m->refused = atom_symbol(a, "no-memory");
        else
            ctx->row_count++;
        return;
    }
    IaOcc *occs = NULL;
    size_t len = 0u, cap = 0u;
    if (!ia_occs_push(&occs, &len, &cap, fp, 0u)) {
        free(occs);
        m->refused = atom_symbol(a, "no-memory");
        return;
    }
    Atom *conflict = ia_occs_conflict(a, occs, len, false);
    free(occs);
    if (conflict)
        return;
    if (!ia_push(&ctx->fps, fp)) {
        m->refused = atom_symbol(a, "no-memory");
        return;
    }
    for (CettaExprIndex i = 0; i < templates->expr.len; i++)
        if (!ia_push(&ctx->tpls, templates->expr.elems[i])) {
            m->refused = atom_symbol(a, "no-memory");
            return;
        }
}

/* Preserve scan-time revisions, including repeated or empty observations.
 * An inner request may have run code before this group completes. */
static Atom *ia_ctx_observation(IaMachine *m, IaCtx *ctx) {
    Arena *a = &m->arena;
    for (size_t i = 0; i < ctx->scans.len; i++) {
        Atom *scan = ctx->scans.items[i];
        IaLoc loc;
        Atom *why = ia_resolve(a, scan->expr.elems[0], &loc);
        if (!why)
            why = ia_check_right(a, &loc, IA_OBSERVE);
        if (why) {
            m->refused = why;
            return NULL;
        }
    }
    return atom_expr2(a, atom_symbol(a, "full-of"), ia_list_atom(a, &ctx->scans));
}

/* Close a group only after every owned job and child group has completed.
 * Parent completion is iterative so nested queries do not recurse in C here. */
static void ia_ctx_done(IaMachine *m, int c) {
    Arena *a = &m->arena;
    while (c >= 0 && !m->refused && !m->stuck) {
        IaCtx *ctx = &m->ctxs[c];
        if (!ctx->outstanding) {
            m->refused = atom_symbol(a, "group-already-completed");
            return;
        }
        if (--ctx->outstanding)
            return;
        int parent = ctx->parent;
        if (ctx->kind == IA_CTX_ABSENT) {
            if (ctx->row_count == 0u) {
                Atom *observation = ia_ctx_observation(m, ctx);
                if (!observation)
                    return;
                Atom *next = ctx->continuation;
                Atom *items[5] = {next->expr.elems[0], next->expr.elems[1],
                    next->expr.elems[2], ia_cons(a, observation, next->expr.elems[3]),
                    next->expr.elems[4]};
                if (!ia_job_push(m, (IaJob){.kind = IA_JOB_WALK,
                    .state = atom_expr(a, items, 5u), .ctx = parent})) {
                    m->refused = atom_symbol(a, "no-memory");
                    return;
                }
            }
        } else {
            Atom *observation = ctx->observe ? ia_ctx_observation(m, ctx) : NULL;
            if (ctx->observe && !observation)
                return;
            IaList all = {0};
            bool ok = !observation || ia_push(&all, observation);
            for (size_t r = 0; ok && r < ctx->fps.len; r++) {
                Atom *fp = ctx->fps.items[r];
                for (CettaExprIndex i = 0; ok && i < fp->expr.len; i++)
                    ok = ia_push(&all, fp->expr.elems[i]);
            }
            for (CettaExprIndex i = 0; ok && ctx->base &&
                     ctx->base->kind == ATOM_EXPR && i < ctx->base->expr.len; i++)
                ok = ia_push(&all, ctx->base->expr.elems[i]);
            if (ok)
                ia_deliver_row(m, parent, ia_list_atom(a, &all),
                    ia_list_atom(a, &ctx->tpls), ia_list_atom(a, &ctx->fps));
            else
                m->refused = atom_symbol(a, "no-memory");
            free(all.items);
        }
        c = parent;
    }
}

/* Only declared source constructors can request evaluation. Patterns and
 * templates that merely contain the spelling `let` are not computations. */
static bool ia_sources_compute(Atom *source) {
    if (ia_head(source, "let", 3u))
        return true;
    if (ia_head_any(source, ",")) {
        for (CettaExprIndex i = 1; i < source->expr.len; i++)
            if (ia_sources_compute(source->expr.elems[i]))
                return true;
    } else if (ia_head(source, "every", 2u) ||
               (ia_head(source, "absent", 2u) && ia_source_form(source->expr.elems[1])))
        return ia_sources_compute(source->expr.elems[1]);
    return false;
}

/* Begin one transform (transform R SOURCES TEMPLATE [DEST]). */
static void ia_attempt_begin(IaMachine *m, Atom *x) {
    Arena *a = &m->arena;
    ia_machine_reset_attempt(m);
    m->in_attempt = true;
    m->current = x;
    m->truncated = false;
    x = ia_firing(x);
    if (!(ia_head(x, "transform", 4u) || ia_head(x, "transform", 5u))) {
        m->refused = atom_expr2(a, atom_symbol(a, "not-a-firing"), x);
        return;
    }
    m->region = ia_strip(a, x->expr.elems[1]);
    m->dest = x->expr.len == 5u ? ia_strip(a, x->expr.elems[4])
                                : atom_symbol(a, "ia:caller");
    Atom *sources = ia_unhold(x->expr.elems[2]);
    Atom *tpl = ia_unhold(x->expr.elems[3]);
    m->replay_safe = !ia_sources_compute(sources);
    Atom *fargs[3] = {m->region, sources, tpl};
    Atom *formed = ia_call_term(op_formed, a, fargs, 3u);
    if (getenv("CETTA_INTERACTION_DEBUG")) {
        fprintf(stderr, "[ia] formed: ");
        if (formed) atom_print(formed, stderr); else fprintf(stderr, "NULL");
        fprintf(stderr, "\n");
    }
    if (!formed || !(atom_is_symbol(formed, "True") ||
                     (formed->kind == ATOM_GROUNDED &&
                      formed->ground.gkind == GV_BOOL && formed->ground.bval))) {
        m->refused = ia_head(formed, "ia:refused", 2u)
            ? atom_expr2(a, atom_symbol(a, "not-formed"), formed->expr.elems[1])
            : atom_symbol(a, "not-formed");
        return;
    }
    Atom *one = NULL;
    Atom **cs;
    size_t n;
    ia_conjuncts(sources, &one, &cs, &n);
    Atom *items[5] = {atom_symbol(a, "ia:state"), atom_expr(a, cs, (CettaExprLen)n),
                      tpl, atom_unit(a), atom_unit(a)};
    if (!ia_job_push(m, (IaJob){.kind = IA_JOB_WALK,
                                .state = atom_expr(a, items, 5u), .ctx = -1}))
        m->refused = atom_symbol(a, "no-memory");
}

static void ia_run_walk(IaMachine *m, IaJob job) {
    Arena *a = &m->arena;
    IaWalk w = {.a = a, .default_loc = m->region,
                .unfiltered = job.ctx >= 0 && m->ctxs[job.ctx].kind == IA_CTX_ABSENT,
                .record_scans = job.ctx >= 0 && m->ctxs[job.ctx].observe,
                .first = job.ctx < 0 && m->replay_safe && !m->full};
    Atom *state = job.state;
    Atom *sources = state->expr.elems[1];
    ia_walk_list(&w, sources->expr.elems, sources->expr.len,
                 state->expr.elems[2], state->expr.elems[3],
                 state->expr.elems[4]);
    for (size_t i = 0; i < w.seen.len; i++)
        if (!ia_push(&m->seen, w.seen.items[i]))
            w.failed = true;
    for (int c = job.ctx; c >= 0; c = m->ctxs[c].parent)
        for (size_t i = 0; i < w.scans.len; i++)
            if (!ia_push(&m->ctxs[c].scans, w.scans.items[i]))
                w.failed = true;
    if (w.failed && !w.refused)
        m->refused = atom_symbol(a, "walk-failed");
    if (w.refused && !m->refused)
        m->refused = w.refused;
    for (size_t i = 0; !m->refused && i < w.results.len; i++) {
        Atom *r = w.results.items[i];
        if (ia_head(r, "ia:row", 3u)) {
            ia_deliver_row(m, job.ctx, r->expr.elems[2],
                atom_expr(a, &r->expr.elems[1], 1u), NULL);
        } else if (ia_head(r, "ia:let", 4u)) {
            if (!ia_job_push(m, (IaJob){.kind = IA_JOB_LET,
                .state = r->expr.elems[3], .pattern = r->expr.elems[1],
                .expr = r->expr.elems[2], .ctx = job.ctx}))
                m->refused = atom_symbol(a, "no-memory");
        } else if (ia_head(r, "ia:every", 3u) || ia_head(r, "ia:absent", 3u)) {
            bool absent = ia_head(r, "ia:absent", 3u);
            int c = ia_ctx_new(m, absent ? IA_CTX_ABSENT : IA_CTX_EVERY,
                job.ctx, absent ? NULL : r->expr.elems[2],
                absent ? r->expr.elems[2] : NULL);
            if (c < 0)
                m->refused = atom_symbol(a, "no-memory");
            else {
                m->ctxs[c].outstanding = 1u; /* held until the push is in */
                if (!ia_job_push(m, (IaJob){.kind = IA_JOB_WALK,
                    .state = r->expr.elems[1], .ctx = c}))
                    m->refused = atom_symbol(a, "no-memory");
                ia_ctx_done(m, c);
            }
        } else if (ia_head(r, "ia:stuck", 2u) && !m->stuck) {
            m->stuck = r->expr.elems[1];
        }
    }
    if (w.truncated)
        m->truncated = true;
    ia_walk_free(&w);
}

/* The continuation of a committed candidate, as code: its answers. */
static Atom *ia_continuation(Arena *a, IaList *cont, IaList *makes, Atom *dest) {
    bool caller = dest && atom_is_symbol(dest, "ia:caller");
    Atom *alts;
    if (!caller) {
        Atom *n = atom_int(a, (int64_t)makes->len);
        alts = atom_expr(a, &n, 1u);
    } else if (cont->len == 0u) {
        Atom *unit = atom_unit(a);
        alts = atom_expr(a, &unit, 1u);
    } else if (cont->len == 1u) {
        if (g_ia_lane == CETTA_INTERACTION_LANE_PETTA)
            return cont->items[0];
        return atom_prime_held_is(cont->items[0]) ? cont->items[0]
                                                   : atom_prime_held_wrap(a, cont->items[0]);
    } else
        alts = ia_list_atom(a, cont);
    Atom *x = atom_expr2(a, atom_symbol(a, "superpose"), alts);
    return g_ia_lane == CETTA_INTERACTION_LANE_PETTA ? x : atom_prime_held_wrap(a, x);
}

/* A refusal of admission may select another candidate. Resource and engine
 * failures terminate this attempt; an engine failure may follow published
 * effects and must never be reported as an ordinary missing match. */
static void ia_commit_failure(IaMachine *m, Atom *result) {
    if (!result || !ia_head(result, "ia:refused", 2u)) {
        m->refused = atom_symbol(&m->arena, "commit-engine-failure");
        return;
    }
    Atom *why = result->expr.elems[1];
    if (atom_is_symbol(why, "no-memory") || ia_head(why, "commit-failed", 2u))
        m->refused = why;
}

/* Try candidates in order; prepare output lists and the continuation before
 * entering the commit protocol. Backend admission is a separate boundary. */
static Atom *ia_commit_first(IaMachine *m, Arena *out) {
    if (m->cancelled || eval_native_interrupted()) {
        ia_machine_cancel(m);
        return NULL;
    }
    Arena *a = &m->arena;
    Atom *seen = ia_list_atom(a, &m->seen);
    for (size_t i = 0; i < m->cands.len; i++) {
        Atom *cand = m->cands.items[i];
        Atom *fp = cand->expr.elems[1], *tpls = cand->expr.elems[2];
        IaList makes = {0}, cont = {0};
        if (m->instances_only) {
            Atom *prepared = atom_deep_copy(out, tpls);
            Atom *cargs[4] = {seen, fp, atom_unit(a),
                              cand->expr.len == 4u ? cand->expr.elems[3] : atom_unit(a)};
            Atom *done = ia_call_term(op_commit, a, cargs, 4u);
            if (ia_head(done, "ia:committed", 3u))
                return prepared;
            ia_commit_failure(m, done);
            if (m->refused)
                return NULL;
            continue;
        }
        bool ok = true;
        for (CettaExprIndex t = 0; ok && t < tpls->expr.len; t++) {
            Atom *sargs[2] = {tpls->expr.elems[t], m->dest};
            Atom *split = ia_call_term(op_split, a, sargs, 2u);
            if (!ia_head(split, "ia:split", 3u)) {
                ok = false;
                break;
            }
            for (CettaExprIndex k = 0; ok && k < split->expr.elems[1]->expr.len; k++)
                ok = ia_push(&makes, split->expr.elems[1]->expr.elems[k]);
            for (CettaExprIndex k = 0; ok && k < split->expr.elems[2]->expr.len; k++)
                ok = ia_push(&cont, split->expr.elems[2]->expr.elems[k]);
        }
        if (!ok) {
            free(makes.items);
            free(cont.items);
            m->refused = atom_symbol(a, "no-memory");
            return NULL;
        }
        Atom *prepared = atom_deep_copy(out, ia_continuation(a, &cont, &makes, m->dest));
        Atom *cargs[4] = {seen, fp, ia_list_atom(a, &makes),
                          cand->expr.len == 4u ? cand->expr.elems[3] : atom_unit(a)};
        Atom *done = ia_call_term(op_commit, a, cargs, 4u);
        free(makes.items);
        free(cont.items);
        if (ia_head(done, "ia:committed", 3u))
            return prepared;
        ia_commit_failure(m, done);
        if (m->refused)
            return NULL;
    }
    return NULL;
}

static Atom *ia_done(Arena *a, Atom *outcome) {
    return atom_expr2(a, atom_symbol(a, "ia:done"), outcome);
}

static void ia_set_alts(IaMachine *m, Atom *firing) {
    firing = ia_firing(firing);
    free(m->alts);
    m->alts = NULL;
    m->nalts = m->alt_i = 0u;
    if (ia_head_any(firing, "choose")) {
        size_t n = firing->expr.len - 1u;
        m->alts = n ? calloc(n, sizeof(Atom *)) : NULL;
        if (n && !m->alts) {
            m->in_attempt = true;
            m->refused = atom_symbol(&m->arena, "no-memory");
            return;
        }
        for (size_t i = 0; i < n; i++)
            m->alts[i] = ia_firing(firing->expr.elems[i + 1u]);
        m->nalts = n;
    } else {
        m->alts = calloc(1u, sizeof(Atom *));
        if (!m->alts) {
            m->in_attempt = true;
            m->refused = atom_symbol(&m->arena, "no-memory");
            return;
        }
        m->alts[0] = firing;
        m->nalts = 1u;
    }
}

static bool ia_agenda_next(IaMachine *m);
static bool ia_parked_update(uint64_t ticket, Atom *seen, bool drop);
static uint64_t ia_park(Atom *firing, bool always, Atom *seen);

static Atom *ia_machine_run_inner(IaMachine *m, Arena *out) {
    Arena *a = &m->arena;
    g_ia_lane = m->lane;
    g_ia_raw = m->raw_values;
    g_ia_host = m->host;
    for (;;) {
        if (m->cancelled || eval_native_interrupted()) {
            ia_machine_cancel(m);
            return NULL;
        }
        if (!m->in_attempt) {
            if (m->alt_i < m->nalts) {
                g_stat_attempts++;
                ia_attempt_begin(m, m->alts[m->alt_i++]);
                continue;
            }
            /* no alternative fired */
            Atom *seen_all = ia_list_atom(a, &m->all_seen);
            if (m->mode == IA_MODE_AWAIT) {
                if (!ia_park(m->firing, false, seen_all))
                    return ia_done(out, atom_error(out, atom_symbol(out, "transform"),
                        atom_expr2(out, atom_symbol(out, "refused"), atom_symbol(out, "no-memory"))));
                return ia_done(out, atom_symbol(out, "ia:parked"));
            }
            if (m->mode == IA_MODE_AGENDA) {
                if (!ia_parked_update(m->ticket, seen_all, false))
                    return ia_done(out, atom_error(out, atom_symbol(out, "transform"),
                        atom_expr2(out, atom_symbol(out, "refused"), atom_symbol(out, "no-memory"))));
                if (ia_agenda_next(m))
                    continue;
                return ia_done(out, atom_symbol(out, "ia:quiescent"));
            }
            return ia_done(out, atom_symbol(out, "ia:none"));
        }
        if (m->refused || m->stuck) {
            m->njobs = m->jobhead = 0u;
        } else if (m->jobhead < m->njobs) {
            IaJob job = m->jobs[m->jobhead++];
            if (m->jobhead == m->njobs)
                m->jobhead = m->njobs = 0u;
            if (job.kind == IA_JOB_LET) {
                if (m->request == UINT64_MAX) {
                    m->refused = atom_symbol(a, "request-capacity");
                    continue;
                }
                m->request++;
                m->let_job = job;
                m->waiting = true;
                Atom *machine = ia_machine_atom(out, m);
                if (!machine)
                    return NULL;
                Atom *e = atom_deep_copy(out, job.expr);
                if (m->lane == CETTA_INTERACTION_LANE_PRIME)
                    e = atom_prime_held_wrap(out, e);
                return atom_expr3(out, atom_symbol(out, "ia:need"), machine, e);
            }
            ia_run_walk(m, job);
            ia_ctx_done(m, job.ctx);
            if (!m->refused && !m->stuck && m->cands.len && ia_open_ctxs(m) == 0u) {
                for (size_t i = 0; i < m->seen.len; i++)
                    if (!ia_push(&m->all_seen, m->seen.items[i]))
                        m->refused = atom_symbol(a, "no-memory");
                if (m->refused)
                    continue;
                Atom *fired = ia_commit_first(m, out);
                if (m->refused)
                    continue;
                if (fired) {
                    m->in_attempt = false;
                    m->full = false;
                    g_stat_fires++;
                    if (m->mode == IA_MODE_AGENDA)
                        ia_parked_update(m->ticket, NULL, !m->always);
                    return ia_done(out, atom_expr2(out, atom_symbol(out, "ia:fired"), fired));
                }
                /* none of these is enabled: they are spent */
                free(m->cands.items);
                m->cands = (IaList){0};
            }
            continue;
        }
        /* the attempt is complete */
        for (size_t i = 0; i < m->seen.len; i++)
            if (!ia_push(&m->all_seen, m->seen.items[i]))
                m->refused = atom_symbol(a, "no-memory");
        if (m->refused) {
            Atom *why = atom_deep_copy(out, m->refused);
            m->in_attempt = false;
            if (m->mode == IA_MODE_AGENDA) {
                (void)ia_parked_update(m->ticket, NULL, true);
                if (ia_head(why, "not-formed", 2u)) {
                    if (ia_agenda_next(m))
                        continue;
                    return ia_done(out, atom_symbol(out, "ia:quiescent"));
                }
            }
            if (!ia_head(why, "not-formed", 2u))
                why = atom_expr2(out, atom_symbol(out, "refused"), why);
            return ia_done(out, atom_error(out, atom_symbol(out, "transform"), why));
        }
        if (m->stuck) {
            m->in_attempt = false;
            return ia_done(out, atom_expr2(out, atom_symbol(out, "ia:stuck"),
                                           atom_deep_copy(out, m->stuck)));
        }
        Atom *fired = ia_commit_first(m, out);
        if (m->refused)
            continue;
        m->in_attempt = false;
        if (!fired && m->truncated && !m->full) {
            m->full = true;
            ia_attempt_begin(m, m->current);
            continue;
        }
        m->full = false;
        if (fired) {
            g_stat_fires++;
            if (m->mode == IA_MODE_AGENDA)
                ia_parked_update(m->ticket, NULL, !m->always);
            return ia_done(out, atom_expr2(out, atom_symbol(out, "ia:fired"), fired));
        }
    }
}

static Atom *ia_machine_run(IaMachine *m, Arena *out) {
    Atom *result = ia_machine_run_inner(m, out);
    if (!result)
        ia_machine_cancel(m);
    if (!m->waiting)
        ia_machine_retire(m);
    return result;
}

/* Resume after a `let`: each value matched against its pattern continues
 * the walk. */
static Atom *ia_machine_resume(IaMachine *m, Atom *values, Arena *out) {
    if (!m->waiting)
        return ia_done(out, atom_symbol(out, "ia:none"));
    Arena *a = &m->arena;
    g_ia_lane = m->lane;
    g_ia_raw = m->raw_values;
    g_ia_host = m->host;
    m->waiting = false;
    IaJob job = m->let_job;
    values = ia_strip(a, values);
    if (!values || values->kind != ATOM_EXPR)
        m->refused = atom_symbol(a, "malformed-computed-results");
    if (!m->refused)
        for (CettaExprIndex i = 0; i < values->expr.len; i++) {
            Atom *v = atom_deep_copy(a, values->expr.elems[i]);
            Atom *bargs[3] = {job.pattern, v, job.state};
            Atom *next = ia_call_term(op_bind, a, bargs, 3u);
            if (ia_head(next, "ia:refused", 2u)) {
                m->refused = next->expr.elems[1];
                break;
            }
            if (ia_head(next, "ia:state", 5u) &&
                !ia_job_push(m, (IaJob){.kind = IA_JOB_WALK,
                                        .state = next, .ctx = job.ctx})) {
                m->refused = atom_symbol(a, "no-memory");
                break;
            }
        }
    ia_ctx_done(m, job.ctx);
    return ia_machine_run(m, out);
}

static Atom *ia_machine_start_lane(IaMode mode, Atom *firing, Arena *out,
                                   CettaInteractionLane lane) {
    IaMachine *m = ia_machine_new(mode);
    if (!m)
        return NULL;
    m->lane = lane;
    Atom *kept = atom_deep_copy(&m->arena, ia_unhold(firing));
    m->firing = kept;
    ia_set_alts(m, kept);
    Atom *result = ia_machine_run(m, out);
    ia_machine_release(m);
    return result;
}

static Atom *ia_machine_start(IaMode mode, Atom *firing, Arena *out) {
    return ia_machine_start_lane(mode, firing, out, CETTA_INTERACTION_LANE_PRIME);
}

/* ── the agenda, for the machine ──────────────────────────────────────── */

static uint64_t ia_park(Atom *firing, bool always, Atom *seen) {
    IaSeenAt *observed = NULL;
    size_t observed_len = 0u;
    if (seen && !ia_seen_at(seen, &observed, &observed_len))
        return 0u;
    Atom *kept = ia_keep(ia_unhold(firing));
    if (!kept) {
        free(observed);
        return 0u;
    }
    pthread_mutex_lock(&g_agenda_gate);
    if (g_next_ticket > INT64_MAX) {
        pthread_mutex_unlock(&g_agenda_gate);
        free(observed);
        return 0u;
    }
    if (g_parked_len == g_parked_cap) {
        size_t cap = g_parked_cap ? g_parked_cap * 2u : 64u;
        IaParked *items = realloc(g_parked, cap * sizeof(*items));
        if (!items) {
            pthread_mutex_unlock(&g_agenda_gate);
            free(observed);
            return 0u;
        }
        g_parked = items;
        g_parked_cap = cap;
    }
    IaParked *p = &g_parked[g_parked_len++];
    memset(p, 0, sizeof(*p));
    p->ticket = g_next_ticket++;
    p->always = always;
    p->live = true;
    p->firing = kept;
    p->seen = observed;
    p->seen_len = observed_len;
    g_stat_parks++;
    uint64_t ticket = p->ticket;
    pthread_mutex_unlock(&g_agenda_gate);
    return ticket;
}

/* After an attempt: what it read (a firing is woken by a change there),
 * or its withdrawal. */
static bool ia_parked_update(uint64_t ticket, Atom *seen, bool drop) {
    IaSeenAt *observed = NULL;
    size_t count = 0u;
    bool ready = drop || !seen || ia_seen_at(seen, &observed, &count);
    pthread_mutex_lock(&g_agenda_gate);
    IaParked *p = ia_parked_find(ticket);
    if (p && !ready)
        p->needs_recheck = true;
    if (p && ready) {
        if (drop)
            p->live = false;
        free(p->seen);
        p->seen = observed;
        p->seen_len = count;
        p->needs_recheck = false;
    }
    pthread_mutex_unlock(&g_agenda_gate);
    if (!p)
        free(observed);
    return ready;
}

static bool ia_agenda_next(IaMachine *m) {
    while (m->woken_i < m->nwoken) {
        uint64_t t = m->woken[m->woken_i++];
        pthread_mutex_lock(&g_agenda_gate);
        IaParked *p = ia_parked_find(t);
        Atom *firing = p ? p->firing : NULL;
        bool always = p && p->always;
        pthread_mutex_unlock(&g_agenda_gate);
        if (!firing)
            continue;
        m->ticket = t;
        m->always = always;
        free(m->all_seen.items);
        m->all_seen = (IaList){0};
        ia_set_alts(m, firing);
        return true;
    }
    return false;
}

/* The live parked firings whose read regions changed, in policy order. */
static size_t ia_woken_tickets(uint64_t **out) {
    ia_policy_from_env();
    pthread_mutex_lock(&g_agenda_gate);
    uint64_t *t = g_parked_len ? malloc(g_parked_len * sizeof(*t)) : NULL;
    size_t n = 0u, live = 0u;
    for (size_t i = 0; i < g_parked_len; i++) {
        IaParked *p = &g_parked[i];
        if (!p->live)
            continue;
        live++;
        if (!ia_parked_changed(p)) {
            g_stat_still++;
            continue;
        }
        g_stat_woken++;
        t[n++] = p->ticket;
    }
    if (live * 2u < g_parked_len && g_parked_len > 64u) {
        size_t k = 0u;
        for (size_t i = 0; i < g_parked_len; i++) {
            if (g_parked[i].live)
                g_parked[k++] = g_parked[i];
            else
                free(g_parked[i].seen);
        }
        g_parked_len = k;
    }
    if (g_policy == IA_POLICY_LIFO)
        for (size_t i = 0; i < n / 2u; i++) {
            uint64_t x = t[i];
            t[i] = t[n - 1u - i];
            t[n - 1u - i] = x;
        }
    else if (g_policy == IA_POLICY_SEEDED)
        for (size_t i = n; i > 1u; i--) {
            size_t j = (size_t)(ia_random() % i);
            uint64_t x = t[i - 1u];
            t[i - 1u] = t[j];
            t[j] = x;
        }
    pthread_mutex_unlock(&g_agenda_gate);
    *out = t;
    return n;
}

static Atom *ia_pop_ready(void) {
    ia_policy_from_env();
    pthread_mutex_lock(&g_agenda_gate);
    Atom *x = NULL;
    if (g_ready_len) {
        size_t i = g_policy == IA_POLICY_LIFO ? g_ready_len - 1u
                 : g_policy == IA_POLICY_SEEDED ? (size_t)(ia_random() % g_ready_len)
                 : 0u;
        x = g_ready[i];
        memmove(g_ready + i, g_ready + i + 1u,
                (g_ready_len - i - 1u) * sizeof(*g_ready));
        g_ready_len--;
    }
    pthread_mutex_unlock(&g_agenda_gate);
    return x;
}

static bool ia_push_ready(Atom *x) {
    Atom *kept = ia_keep(ia_unhold(x));
    if (!kept)
        return false;
    pthread_mutex_lock(&g_agenda_gate);
    if (g_ready_len == g_ready_cap) {
        size_t cap = g_ready_cap ? g_ready_cap * 2u : 64u;
        Atom **items = realloc(g_ready, cap * sizeof(*items));
        if (!items) {
            pthread_mutex_unlock(&g_agenda_gate);
            return false;
        }
        g_ready = items;
        g_ready_cap = cap;
    }
    g_ready[g_ready_len++] = kept;
    pthread_mutex_unlock(&g_agenda_gate);
    return true;
}

/* ── firing entry points ──────────────────────────────────────────────── */

static bool ia_answer(CettaCallOutcome *out, Atom *x) {
    *out = x ? cetta_call_completed_value(x) : cetta_call_interrupted();
    return true;
}

/* (fire R SOURCES TEMPLATE [DEST]): R is a value; SOURCES and TEMPLATE
 * are syntax. */
static bool op_fire(Arena *a, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 3u && nargs != 4u)
        return false;
    Atom *items[5] = {atom_symbol(a, "transform"), ia_strip(a, args[0]),
                      ia_unhold(args[1]), ia_unhold(args[2]),
                      nargs == 4u ? ia_strip(a, args[3]) : NULL};
    Atom *firing = atom_expr(a, items, nargs + 1u);
    return ia_answer(out, ia_machine_start(IA_MODE_NOW, firing, a));
}

/* (now X): a firing description X, now. */
static bool op_now(Arena *a, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    return ia_answer(out, ia_machine_start(IA_MODE_NOW, args[0], a));
}

/* (now-choose X ...) */
static bool op_now_choose(Arena *a, Atom **args, uint32_t nargs,
                          CettaCallOutcome *out) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * (nargs + 1u));
    items[0] = atom_symbol(a, "choose");
    for (uint32_t i = 0; i < nargs; i++)
        items[i + 1u] = ia_unhold(args[i]);
    return ia_answer(out, ia_machine_start(IA_MODE_NOW,
                                           atom_expr(a, items, nargs + 1u), a));
}

static bool op_await(Arena *a, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    return ia_answer(out, ia_machine_start(IA_MODE_AWAIT, args[0], a));
}

/* A rule is formed when it is installed: an error then, not a rule that
 * never fires. */
static Atom *ia_install_error(Arena *a, Atom *firing) {
    firing = ia_firing(firing);
    Atom **alts = NULL;
    size_t n = 0u;
    if (ia_head_any(firing, "choose")) {
        alts = firing->expr.elems + 1;
        n = firing->expr.len - 1u;
    } else {
        alts = &firing;
        n = 1u;
    }
    for (size_t i = 0; i < n; i++) {
        Atom *x = ia_firing(alts[i]);
        if (!(ia_head(x, "transform", 4u) || ia_head(x, "transform", 5u)))
            return atom_error(a, atom_symbol(a, "whenever"),
                              atom_expr2(a, atom_symbol(a, "not-a-firing"), x));
        Atom *fargs[3] = {ia_strip(a, x->expr.elems[1]), ia_unhold(x->expr.elems[2]),
                          ia_unhold(x->expr.elems[3])};
        Atom *formed = ia_call_term(op_formed, a, fargs, 3u);
        if (ia_head(formed, "ia:refused", 2u))
            return atom_error(a, atom_symbol(a, "transform"),
                              atom_expr2(a, atom_symbol(a, "not-formed"),
                                         formed->expr.elems[1]));
    }
    return NULL;
}

static bool op_whenever(Arena *a, Atom **args, uint32_t nargs,
                        CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    Atom *error = ia_install_error(a, args[0]);
    if (error)
        return ia_answer(out, ia_done(a, error));
    if (!ia_park(args[0], true, NULL))
        return ia_answer(out, NULL);
    return ia_answer(out, ia_done(a, atom_symbol(a, "ia:parked")));
}

static bool op_resume(Arena *a, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 2u)
        return false;
    IaResumeToken *token = ia_resume_token_of(ia_strip(a, args[0]));
    IaMachine *m = token ? token->machine : NULL;
    if (!m || m->cancelled || m->computing || !m->waiting || token->request != m->request)
        return ia_answer(out, ia_done(a, atom_error(a, atom_symbol(a, "transform"),
            atom_expr2(a, atom_symbol(a, "refused"), atom_symbol(a, "stale-resumption")))));
    return ia_answer(out, ia_machine_resume(m, args[1], a));
}

/* Run the request's stored expression, rather than accepting replacement
 * code or installing an observer across unrelated evaluator calls. */
static bool op_compute(Space *space, Arena *a, Atom **args, uint32_t nargs,
                        CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    IaResumeToken *token = ia_resume_token_of(ia_strip(a, args[0]));
    IaMachine *m = token ? token->machine : NULL;
    if (!m || m->cancelled || m->computing || !m->waiting || token->request != m->request)
        return ia_answer(out, ia_done(a, atom_error(a, atom_symbol(a, "transform"),
            atom_expr2(a, atom_symbol(a, "refused"), atom_symbol(a, "stale-resumption")))));
    CettaEvalReadScope scope = {.observe = ia_machine_observe_read, .context = m};
    Atom **values = NULL, *error = NULL;
    size_t len = 0u;
    m->computing = true;
    eval_read_scope_enter(&scope);
    bool complete = eval_interaction_values(space, a, m->let_job.expr, &values, &len, &error);
    eval_read_scope_leave(&scope);
    m->computing = false;
    if (!complete || scope.incomplete || m->refused) {
        free(values);
        m->waiting = false;
        if (m->refused)
            error = atom_error(a, atom_symbol(a, "transform"),
                atom_expr2(a, atom_symbol(a, "refused"), atom_deep_copy(a, m->refused)));
        if (!error)
            error = atom_error(a, atom_symbol(a, "transform"),
                atom_symbol(a, "incomplete-computation"));
        /* A host's diagnostic may still cite the stored request. */
        error = atom_deep_copy(a, error);
        ia_machine_retire(m);
        return ia_answer(out, ia_done(a, error));
    }
    Atom *reply = atom_expr(a, values, (CettaExprLen)len);
    free(values);
    return ia_answer(out, ia_machine_resume(m, reply, a));
}

/* (agenda-step): a ready process to run, else the woken firings attempted
 * in policy order until one fires, else quiescence. */
static bool op_agenda_step(Arena *a, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 0u && nargs != 1u)
        return false;
    Atom *ready = ia_pop_ready();
    if (ready)
        return ia_answer(out, ia_done(a, atom_expr2(a, atom_symbol(a, "ia:process"),
            atom_prime_held_wrap(a, atom_deep_copy(a, ready)))));
    uint64_t *woken = NULL;
    size_t n = ia_woken_tickets(&woken);
    if (n == 0u) {
        free(woken);
        return ia_answer(out, ia_done(a, atom_symbol(a, "ia:quiescent")));
    }
    IaMachine *m = ia_machine_new(IA_MODE_AGENDA);
    if (!m) {
        free(woken);
        return false;
    }
    m->woken = woken;
    m->nwoken = n;
    if (!ia_agenda_next(m)) {
        ia_machine_release(m);
        return ia_answer(out, ia_done(a, atom_symbol(a, "ia:quiescent")));
    }
    Atom *result = ia_machine_run(m, a);
    ia_machine_release(m);
    return ia_answer(out, result);
}

static bool op_spawn(Arena *a, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    for (uint32_t i = 0; i < nargs; i++)
        if (!ia_push_ready(args[i]))
            return false;
    return ia_answer(out, atom_unit(a));
}

/* (seq V ANSWERS K): K with V bound to each answer, as ready processes. */
static bool op_seq(Arena *a, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 3u)
        return false;
    Atom *v = ia_unhold(args[0]);
    Atom *answers = ia_strip(a, args[1]);
    Atom *k = ia_unhold(args[2]);
    if (answers && answers->kind == ATOM_EXPR)
        for (CettaExprIndex i = 0; i < answers->expr.len; i++) {
            Bindings b;
            bindings_init(&b);
            if (match_atoms(v, answers->expr.elems[i], &b, a)) {
                /* today's operations: values are substituted as they are */
                Atom *next = bindings_apply(&b, a, k);
                if (next)
                    (void)ia_push_ready(next);
            }
            bindings_free(&b);
        }
    return ia_answer(out, atom_unit(a));
}

/* A fresh region of `profile`, in session storage, as its holder's handle. */
static Atom *ia_fresh_region(Arena *a, Space *program, const IaProfile *profile) {
    Arena *pa = eval_current_persistent_arena();
    Space *ns = pa ? arena_alloc(pa, sizeof(Space)) : NULL;
    if (!ns)
        return NULL;
    space_init_with_universe(ns, program ? program->native.universe : NULL);
    ns->kind = SPACE_KIND_ATOM;
    SpaceEngine engine = program ? program->match_backend.kind : SPACE_ENGINE_NATIVE;
    if (engine == SPACE_ENGINE_MORK)
        engine = SPACE_ENGINE_PATHMAP;
    if (!space_match_backend_try_set(ns, engine) ||
        !eval_track_new_space(ns)) {
        space_free(ns);
        return NULL;
    }
    IaRegion *region = ia_region_of(ns, true);
    if (!region)
        return NULL;
    region->profile = profile;
    return ia_handle_atom(a, region, profile->holder);
}

static bool op_new_region(Space *program, Arena *a, Atom **args, uint32_t nargs,
                          CettaCallOutcome *out) {
    if (nargs != 1u)
        return false;
    Atom *name = ia_strip(a, args[0]);
    const IaProfile *profile = name && name->kind == ATOM_SYMBOL
        ? ia_profile_named(atom_name_cstr(name)) : NULL;
    if (!profile)
        return ia_answer(out, atom_error(a, atom_expr2(a, atom_symbol(a, "new-region"), name),
                                         atom_symbol(a, "UnknownProfile")));
    return ia_answer(out, ia_fresh_region(a, program, profile));
}

/* Rholang receipts: (for (p <- c [& q <- d ...]) P) with one arrow kind. */
static bool op_for(Arena *a, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 2u)
        return false;
    Atom *binds = ia_unhold(args[0]);
    Atom *body = ia_unhold(args[1]);
    if (!binds || binds->kind != ATOM_EXPR || binds->expr.len < 3u)
        return false;
    IaList sources = {0};
    (void)ia_push(&sources, atom_symbol(a, ","));
    const char *kind = NULL;
    Atom *first_loc = NULL;
    bool mixed = false;
    for (CettaExprIndex i = 0; i + 2u < binds->expr.len + 1u; i += 4u) {
        if (i + 2u >= binds->expr.len)
            break;
        Atom *p = binds->expr.elems[i], *arrow = binds->expr.elems[i + 1u],
             *c = binds->expr.elems[i + 2u];
        const char *k = atom_is_symbol(arrow, "<-") ? "once"
                      : atom_is_symbol(arrow, "<=") ? "always"
                      : atom_is_symbol(arrow, "<<-") ? "peek" : NULL;
        if (!k) {
            free(sources.items);
            return ia_answer(out, atom_error(a, atom_symbol(a, "for"),
                                             atom_symbol(a, "NotAReceipt")));
        }
        const char *whole = strcmp(k, "always") == 0 ? "always" : "once";
        if (kind && strcmp(kind, whole) != 0)
            mixed = true;
        kind = whole;
        if (!first_loc)
            first_loc = c;
        (void)ia_push(&sources, atom_expr3(a, atom_symbol(a,
            strcmp(k, "peek") == 0 ? "read" : "take"), c, p));
        if (i + 3u < binds->expr.len && !atom_is_symbol(binds->expr.elems[i + 3u], "&")) {
            free(sources.items);
            return ia_answer(out, atom_error(a, atom_symbol(a, "for"),
                                             atom_symbol(a, "NotAReceipt")));
        }
    }
    if (mixed) {
        free(sources.items);
        return ia_answer(out, atom_error(a, atom_symbol(a, "for"),
                                         atom_symbol(a, "MixedReceipt")));
    }
    Atom *items[4] = {atom_symbol(a, "transform"), first_loc,
                      ia_list_atom(a, &sources), body};
    free(sources.items);
    Atom *firing = atom_expr(a, items, 4u);
    if (strcmp(kind, "always") == 0) {
        Atom *error = ia_install_error(a, firing);
        if (error)
            return ia_answer(out, ia_done(a, error));
        if (!ia_park(firing, true, NULL))
            return false;
        return ia_answer(out, ia_done(a, atom_symbol(a, "ia:parked")));
    }
    return ia_answer(out, ia_machine_start(IA_MODE_AWAIT, firing, a));
}

/* (contract C P BODY) = (for (P <= C) BODY) */
static bool op_contract(Arena *a, Atom **args, uint32_t nargs,
                        CettaCallOutcome *out) {
    if (nargs != 3u)
        return false;
    Atom *c = ia_strip(a, args[0]);
    Atom *src = atom_expr3(a, atom_symbol(a, "take"), c, ia_unhold(args[1]));
    Atom *items[4] = {atom_symbol(a, "transform"), c, src, ia_unhold(args[2])};
    Atom *error = ia_install_error(a, atom_expr(a, items, 4u));
    if (error)
        return ia_answer(out, ia_done(a, error));
    if (!ia_park(atom_expr(a, items, 4u), true, NULL))
        return false;
    return ia_answer(out, ia_done(a, atom_symbol(a, "ia:parked")));
}

/* ── helpers shared with other firing realizations ─────────────────────── */

typedef struct {
    CettaIndex pos;
    Atom *atom;
} IaPosAtom;

static int ia_pos_atom_cmp(const void *l, const void *r) {
    const IaPosAtom *x = l, *y = r;
    return x->pos < y->pos ? -1 : x->pos > y->pos;
}

bool cetta_interaction_remove_positions(Space *space, const CettaIndex *positions,
                                        Atom *const *atoms, size_t len) {
    if (!space || (len && (!positions || !atoms)))
        return false;
    if (len == 0u)
        return true;
    IaPosAtom *sorted = malloc(len * sizeof(*sorted));
    if (!sorted)
        return false;
    for (size_t k = 0; k < len; k++)
        sorted[k] = (IaPosAtom){positions[k], atoms[k]};
    qsort(sorted, len, sizeof(*sorted), ia_pos_atom_cmp);
    CettaIndex *pos = malloc(len * sizeof(*pos));
    if (!pos) {
        free(sorted);
        return false;
    }
    for (size_t k = 0; k < len; k++)
        pos[k] = sorted[k].pos;
    bool removed = space_remove_logical_positions(space, pos, len);
    if (!removed) {
        CettaCount n = space_length64(space);
        uint8_t *mask = calloc((size_t)n ? (size_t)n : 1u, 1u);
        for (size_t k = 0; mask && k < len; k++)
            if (pos[k] < n)
                mask[pos[k]] = 1u;
        CettaCount count = 0u;
        removed = mask && space_remove_occurrence_mask_stable(space, mask, n, &count) &&
                  count == len;
        free(mask);
    }
    if (!removed) {
        removed = true;
        for (size_t k = 0; k < len; k++)
            removed = space_remove(space, sorted[k].atom) && removed;
    }
    free(pos);
    free(sorted);
    return removed;
}

bool cetta_interaction_picks_linear(Space *const *spaces, const CettaIndex *positions,
                                    const bool *takes, size_t len, size_t *conflict_out) {
    IaOcc *occs = len ? malloc(len * sizeof(*occs)) : NULL;
    if (len && !occs)
        return false;
    for (size_t i = 0; i < len; i++)
        occs[i] = (IaOcc){spaces[i], positions[i], (uint32_t)i, takes[i], NULL, NULL};
    if (len > 1u)
        qsort(occs, len, sizeof(*occs), ia_occ_cmp);
    bool linear = true;
    for (size_t i = 0; i + 1u < len && linear; i++)
        if (occs[i].space == occs[i + 1u].space && occs[i].pos == occs[i + 1u].pos &&
            (occs[i].take || occs[i + 1u].take)) {
            linear = false;
            if (conflict_out)
                *conflict_out = occs[i].take ? occs[i].row : occs[i + 1u].row;
        }
    free(occs);
    return linear;
}

/* The lane-neutral entry: one firing now, `let` code evaluated through the
 * host when it can, else suspended for the caller. */
CettaInteractionOutcome cetta_interaction_fire(
    const CettaInteractionHost *host, Arena *a, Atom *region, Atom *sources,
    Atom *template_atom, Atom *dest, Atom **continuation_out) {
    *continuation_out = NULL;
    CettaInteractionLane lane = host ? host->lane : CETTA_INTERACTION_LANE_PRIME;
    Atom *items[5] = {atom_symbol(a, "transform"), region, sources, template_atom, dest};
    Atom *firing = atom_expr(a, items, dest ? 5u : 4u);
    IaMachine *m = ia_machine_new(IA_MODE_NOW);
    if (!m)
        return CETTA_INTERACTION_ERROR;
    m->lane = lane;
    m->instances_only = host && host->instances_only;
    m->raw_values = host && host->raw_values;
    m->host = host;
    m->firing = atom_deep_copy(&m->arena, firing);
    ia_set_alts(m, m->firing);
    Atom *r = ia_machine_run(m, a);
    while (r && ia_head(r, "ia:need", 3u) && host && host->evaluate) {
        Atom **values = NULL;
        size_t n = 0u;
        Atom *error = NULL;
        Atom *e = ia_unhold(r->expr.elems[2]);
        CettaEvalReadScope scope = {.observe = ia_machine_observe_read, .context = m};
        m->computing = true;
        eval_read_scope_enter(&scope);
        bool complete = host->evaluate(host->context, a, e, &values, &n, &error);
        eval_read_scope_leave(&scope);
        m->computing = false;
        if (!complete || scope.incomplete || m->refused) {
            free(values);
            if (m->refused)
                error = atom_error(a, atom_symbol(a, "transform"),
                    atom_expr2(a, atom_symbol(a, "refused"), atom_deep_copy(a, m->refused)));
            error = atom_deep_copy(a, error);
            ia_machine_cancel(m);
            ia_machine_retire(m);
            m->host = NULL;
            g_ia_host = NULL;
            ia_machine_release(m);
            *continuation_out = error;
            return CETTA_INTERACTION_ERROR;
        }
        Atom *list = atom_expr(a, values, (CettaExprLen)n);
        free(values);
        r = ia_machine_resume(m, list, a);
    }
    /* the host is borrowed for this call only */
    m->host = NULL;
    g_ia_host = NULL;
    ia_machine_release(m);
    if (!r)
        return CETTA_INTERACTION_ERROR;
    if (ia_head(r, "ia:need", 3u)) {
        *continuation_out = r;
        return CETTA_INTERACTION_SUSPENDED;
    }
    Atom *outcome = ia_head(r, "ia:done", 2u) ? r->expr.elems[1] : r;
    if (ia_head(outcome, "ia:fired", 2u)) {
        *continuation_out = outcome->expr.elems[1];
        return CETTA_INTERACTION_FIRED;
    }
    if (outcome->kind == ATOM_SYMBOL && atom_is_symbol(outcome, "ia:none"))
        return CETTA_INTERACTION_NONE;
    *continuation_out = outcome;
    return CETTA_INTERACTION_ERROR;
}

/* (fire-lane LANE R SOURCES TEMPLATE [DEST]): the same firing for a lane
 * whose library drives it (the PeTTa lane). */
/* Syntax a lane passed as (quote X): a native call's arguments are compiled
 * as code on the PeTTa lane, so its library quotes sources and templates. */
static Atom *ia_unquote(Atom *x) {
    x = ia_unhold(x);
    return ia_head(x, "quote", 2u) ? x->expr.elems[1] : x;
}

static bool op_fire_lane(Arena *a, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (nargs != 4u && nargs != 5u)
        return false;
    Atom *lane_atom = ia_strip(a, args[0]);
    bool instances = lane_atom && atom_is_symbol(lane_atom, "petta-instances");
    CettaInteractionLane lane = lane_atom && (atom_is_symbol(lane_atom, "petta") || instances)
        ? CETTA_INTERACTION_LANE_PETTA : CETTA_INTERACTION_LANE_PRIME;
    Atom *items[5] = {atom_symbol(a, "transform"), ia_strip(a, args[1]),
                      ia_unquote(args[2]), ia_unquote(args[3]),
                      nargs == 5u ? ia_strip(a, args[4]) : NULL};
    Atom *firing = atom_expr(a, items, nargs);
    if (!instances)
        return ia_answer(out, ia_machine_start_lane(IA_MODE_NOW, firing, a, lane));
    /* the call shape lib_transform's tf:fire would make through the
     * lane-neutral entry: (tf:fired INSTANCES) or no answer */
    CettaInteractionHost host = {.lane = lane, .instances_only = true};
    Atom *x = NULL;
    CettaInteractionOutcome o = cetta_interaction_fire(&host, a, items[1], items[2],
                                                       items[3], items[4], &x);
    if (o == CETTA_INTERACTION_FIRED)
        return ia_answer(out, atom_expr2(a, atom_symbol(a, "tf:fired"), x));
    if (o == CETTA_INTERACTION_NONE) {
        *out = cetta_call_failure();
        return true;
    }
    return ia_answer(out, x ? x : atom_symbol(a, "ia:error"));
}

/* ── the operations ───────────────────────────────────────────────────── */

bool cetta_interaction_core_call(Space *space, Arena *a, const char *op,
                                 Atom **args, uint32_t nargs,
                                 CettaCallOutcome *out) {
    if (!op || !a || !out)
        return false;
    if (strcmp(op, "facet") == 0) return op_facet(a, args, nargs, out);
    if (strcmp(op, "same-region") == 0) return op_same_region(a, args, nargs, out);
    if (strcmp(op, "rights") == 0) return op_rights(a, args, nargs, out);
    if (strcmp(op, "profile") == 0) return op_profile(a, args, nargs, out);
    if (strcmp(op, "strategy") == 0) return op_strategy(a, args, nargs, out);
    if (strcmp(op, "start") == 0) return op_start(a, args, nargs, out);
    if (strcmp(op, "walk") == 0) return op_walk(a, args, nargs, out);
    if (strcmp(op, "walk-suffix") == 0) return op_walk_suffix(a, args, nargs, out);
    if (strcmp(op, "bind") == 0) return op_bind(a, args, nargs, out);
    if (strcmp(op, "split") == 0) return op_split(a, args, nargs, out);
    if (strcmp(op, "commit") == 0) return op_commit(a, args, nargs, out);
    if (strcmp(op, "amalgamate") == 0) return op_amalgamate(a, args, nargs, out);
    if (strcmp(op, "formed") == 0) return op_formed(a, args, nargs, out);
    if (strcmp(op, "agenda-park") == 0) return op_agenda_park(a, args, nargs, out);
    if (strcmp(op, "agenda-woken") == 0) return op_agenda_woken(a, nargs, out);
    if (strcmp(op, "agenda-seen") == 0) return op_agenda_seen(a, args, nargs, out);
    if (strcmp(op, "agenda-drop") == 0) return op_agenda_drop(a, args, nargs, out);
    if (strcmp(op, "agenda-push") == 0) return op_agenda_push(a, args, nargs, out);
    if (strcmp(op, "agenda-pop") == 0) return op_agenda_pop(a, nargs, out);
    if (strcmp(op, "agenda-enter") == 0) return op_agenda_enter(a, nargs, out);
    if (strcmp(op, "agenda-leave") == 0) return op_agenda_leave(a, nargs, out);
    if (strcmp(op, "agenda-policy") == 0) return op_agenda_policy(a, args, nargs, out);
    if (strcmp(op, "agenda-count") == 0) return op_agenda_count(a, args, nargs, out);
    if (strcmp(op, "agenda-stats") == 0) return op_agenda_stats(a, nargs, out);
    if (strcmp(op, "instantiate") == 0) return op_instantiate(a, args, nargs, out);
    if (strcmp(op, "changed") == 0) return op_changed(a, args, nargs, out);
    if (strcmp(op, "is-handle") == 0) return op_is_handle(a, args, nargs, out);
    if (strcmp(op, "fire") == 0) return op_fire(a, args, nargs, out);
    if (strcmp(op, "fire-lane") == 0) return op_fire_lane(a, args, nargs, out);
    if (strcmp(op, "now") == 0) return op_now(a, args, nargs, out);
    if (strcmp(op, "now-choose") == 0) return op_now_choose(a, args, nargs, out);
    if (strcmp(op, "await") == 0) return op_await(a, args, nargs, out);
    if (strcmp(op, "whenever") == 0) return op_whenever(a, args, nargs, out);
    if (strcmp(op, "resume") == 0) return op_resume(a, args, nargs, out);
    if (strcmp(op, "compute") == 0) return op_compute(space, a, args, nargs, out);
    if (strcmp(op, "agenda-step") == 0) return op_agenda_step(a, nargs, out);
    if (strcmp(op, "spawn") == 0) return op_spawn(a, args, nargs, out);
    if (strcmp(op, "seq") == 0) return op_seq(a, args, nargs, out);
    if (strcmp(op, "new-region") == 0) return op_new_region(space, a, args, nargs, out);
    if (strcmp(op, "for") == 0) return op_for(a, args, nargs, out);
    if (strcmp(op, "contract") == 0) return op_contract(a, args, nargs, out);
    return false;
}
