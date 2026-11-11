#include "support_view.h"

#include "exec_saturation.h"
#include "library_presentation.h"
#include "match.h"
#include "space_match_backend.h"
#include "symbol.h"
#include "../native/support/support_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SV_NONE UINT32_MAX

/* ── The presentation ──────────────────────────────────────────────────── */

static const char *const SUPPORT_CONSTRUCTORS[] = {
    "support:view", "support:base", "support:just", "added", "removed",
    "derived", "base", "found", "exec",
};

static bool support_setting(const char *name, int *cached) {
    if (*cached < 0) {
        const char *setting = getenv(name);
        *cached = setting && strcmp(setting, "1") == 0;
    }
    return *cached != 0;
}

/* CETTA_SUPPORT_REFERENCE=1 runs the library's definitions, so the two can be
 * compared. */
static bool support_reference(void) {
    static int reference = -1;
    return support_setting("CETTA_SUPPORT_REFERENCE", &reference);
}

/* CETTA_SUPPORT_TRACE=1 reports admissions, declines and cache reads. */
static bool support_trace(void) {
    static int trace = -1;
    return support_setting("CETTA_SUPPORT_TRACE", &trace);
}

static _Thread_local struct {
    SpaceProgramToken program;
    char path[4096];
    bool admitted;
} g_support_presentation;

bool cetta_support_presentation_admitted(Space *program,
                                         const char *library_path) {
    if (!program || !library_path || !*library_path || support_reference())
        return false;
    SpaceProgramToken token = space_program_token(program);
    if (space_program_token_eq(g_support_presentation.program, token) &&
        strcmp(g_support_presentation.path, library_path) == 0)
        return g_support_presentation.admitted;
    bool admitted =
        strlen(library_path) < sizeof(g_support_presentation.path) &&
        cetta_library_presentation_exact(
            program, library_path, SUPPORT_CONSTRUCTORS,
            sizeof(SUPPORT_CONSTRUCTORS) / sizeof(SUPPORT_CONSTRUCTORS[0]),
            support_trace() ? "support" : NULL);
    g_support_presentation.program = token;
    snprintf(g_support_presentation.path, sizeof(g_support_presentation.path),
             "%s", library_path);
    g_support_presentation.admitted = admitted;
    if (support_trace())
        fprintf(stderr, "[support] presentation %s: %s\n",
                admitted ? "admitted" : "refused", library_path);
    return admitted;
}

typedef enum {
    SV_OPEN,
    SV_ASSERT,
    SV_RETRACT,
    SV_WHY,
    SV_NOT_OURS,
} SvOperation;

static SvOperation support_operation(SymbolId head, CettaExprLen arity) {
    if (arity != 2u || head == SYMBOL_ID_NONE ||
        symbol_id_is_builtin(head) || !g_symbols)
        return SV_NOT_OURS;
    /* Names are resolved once for this symbol-table incarnation. */
    static _Thread_local const SymbolTable *owner;
    static _Thread_local uint64_t owner_id;
    static _Thread_local SymbolId ids[4];
    uint64_t current_id = symbol_table_instance_id(g_symbols);
    if (owner != g_symbols || owner_id != current_id) {
        ids[0] = atom_symbol_literal_id("support:open");
        ids[1] = atom_symbol_literal_id("support:assert");
        ids[2] = atom_symbol_literal_id("support:retract");
        ids[3] = atom_symbol_literal_id("support:why");
        owner = g_symbols;
        owner_id = current_id;
    }
    for (size_t i = 0u; i < 4u; i++)
        if (head == ids[i])
            return (SvOperation)i;
    return SV_NOT_OURS;
}

bool cetta_support_head_slow(SymbolId head, CettaExprLen arity) {
    return support_operation(head, arity) != SV_NOT_OURS;
}

/* ── The incremental state ─────────────────────────────────────────────── */

typedef struct {
    Atom *atom;
    uint32_t hash;
} SvAtom;

enum { SV_ABSENT = 0, SV_PRESENT = 1, SV_PENDING = 2 };

/* What a view and its log hold.  Atoms are numbered as they are first met;
 * justifications are the support graph's own: a base fact (no key, no body,
 * usable while its record is in the log) or a recorded derivation (a key,
 * the head and its body atoms, always usable). */
typedef struct {
    bool live;
    uint64_t used;
    Space *view, *log;
    uint64_t view_instance, log_instance;
    uint64_t view_revision, log_revision;
    Atom *directives;
    CettaExecDirective *rules;
    uint32_t *rule_key;
    uint32_t nrules;
    /* Every row of every directive over the view is a recorded derivation
     * whose head the view holds: the view is a fixpoint of its own rounds. */
    bool closed;
    Arena arena;
    SvAtom *atoms;
    uint8_t *state;
    uint32_t *base;
    uint32_t natoms, capatoms;
    uint32_t *atom_table;
    uint32_t atom_table_cap;
    Atom **keys;
    uint32_t nkeys, capkeys;
    uint32_t *jkey, *jhead, *jstart, *jlen;
    uint8_t *jenabled;
    uint32_t njust, capjust;
    uint32_t *jbody;
    uint32_t nbody, capbody;
    uint32_t *just_table;
    uint32_t just_table_cap;
    SupportGraph *graph;
} SvCache;

#define SV_CACHES 8u
static _Thread_local SvCache g_sv_caches[SV_CACHES];
static _Thread_local uint64_t g_sv_clock;

static void sv_reset(SvCache *c) {
    if (c->live || c->graph) {
        free(c->rules);
        free(c->rule_key);
        free(c->atoms);
        free(c->state);
        free(c->base);
        free(c->atom_table);
        free(c->keys);
        free(c->jkey);
        free(c->jhead);
        free(c->jstart);
        free(c->jlen);
        free(c->jenabled);
        free(c->jbody);
        free(c->just_table);
        support_free(c->graph);
        arena_free(&c->arena);
    }
    memset(c, 0, sizeof(*c));
}

static bool sv_begin(SvCache *c) {
    sv_reset(c);
    arena_init(&c->arena);
    c->graph = support_new(0u);
    c->live = c->graph != NULL;
    return c->live;
}

static bool sv_grow(void **items, size_t size, uint32_t cap) {
    void *grown = realloc(*items, size * (size_t)cap);
    if (!grown)
        return false;
    *items = grown;
    return true;
}

static uint32_t sv_mix(uint32_t h, uint32_t x) {
    return h ^ (x + 0x9e3779b9u + (h << 6) + (h >> 2));
}

static bool sv_atom_table_grow(SvCache *c) {
    uint32_t cap = c->atom_table_cap ? c->atom_table_cap * 2u : 256u;
    uint32_t *table = calloc(cap, sizeof(*table));
    if (!table)
        return false;
    for (uint32_t i = 0u; i < c->natoms; i++) {
        uint32_t k = c->atoms[i].hash & (cap - 1u);
        while (table[k])
            k = (k + 1u) & (cap - 1u);
        table[k] = i + 1u;
    }
    free(c->atom_table);
    c->atom_table = table;
    c->atom_table_cap = cap;
    return true;
}

static uint32_t sv_atom_find(const SvCache *c, Atom *atom, uint32_t hash) {
    if (!c->atom_table_cap)
        return SV_NONE;
    uint32_t mask = c->atom_table_cap - 1u;
    for (uint32_t k = hash & mask;; k = (k + 1u) & mask) {
        uint32_t entry = c->atom_table[k];
        if (!entry)
            return SV_NONE;
        const SvAtom *known = &c->atoms[entry - 1u];
        if (known->hash == hash && atom_eq(known->atom, atom))
            return entry - 1u;
    }
}

static uint32_t sv_atom_lookup(const SvCache *c, Atom *atom) {
    return sv_atom_find(c, atom, atom_hash(atom));
}

/* The number of `atom`, kept from now on. */
static uint32_t sv_atom_intern(SvCache *c, Atom *atom) {
    uint32_t hash = atom_hash(atom);
    uint32_t id = sv_atom_find(c, atom, hash);
    if (id != SV_NONE)
        return id;
    if ((c->natoms + 1u) * 2u > c->atom_table_cap && !sv_atom_table_grow(c))
        return SV_NONE;
    if (c->natoms == c->capatoms) {
        uint32_t cap = c->capatoms ? 2u * c->capatoms : 256u;
        if (!sv_grow((void **)&c->atoms, sizeof(*c->atoms), cap) ||
            !sv_grow((void **)&c->state, sizeof(*c->state), cap) ||
            !sv_grow((void **)&c->base, sizeof(*c->base), cap))
            return SV_NONE;
        c->capatoms = cap;
    }
    Atom *copy = atom_deep_copy(&c->arena, atom);
    if (!copy || !support_grow(c->graph, c->natoms + 1u))
        return SV_NONE;
    id = c->natoms++;
    c->atoms[id] = (SvAtom){copy, hash};
    c->state[id] = SV_ABSENT;
    c->base[id] = SV_NONE;
    uint32_t mask = c->atom_table_cap - 1u;
    uint32_t k = hash & mask;
    while (c->atom_table[k])
        k = (k + 1u) & mask;
    c->atom_table[k] = id + 1u;
    return id;
}

static uint32_t sv_key_intern(SvCache *c, Atom *key) {
    for (uint32_t i = 0u; i < c->nkeys; i++)
        if (atom_eq(c->keys[i], key))
            return i;
    if (c->nkeys == c->capkeys) {
        uint32_t cap = c->capkeys ? 2u * c->capkeys : 8u;
        if (!sv_grow((void **)&c->keys, sizeof(*c->keys), cap))
            return SV_NONE;
        c->capkeys = cap;
    }
    Atom *copy = atom_deep_copy(&c->arena, key);
    if (!copy)
        return SV_NONE;
    c->keys[c->nkeys] = copy;
    return c->nkeys++;
}

static uint32_t sv_derivation_hash(uint32_t key, uint32_t head,
                                   const uint32_t *body, uint32_t n) {
    uint32_t h = sv_mix(sv_mix(0x5bd1e995u, key), head);
    for (uint32_t i = 0u; i < n; i++)
        h = sv_mix(h, body[i]);
    return sv_mix(h, n);
}

static bool sv_derivation_is(const SvCache *c, uint32_t j, uint32_t key,
                             uint32_t head, const uint32_t *body,
                             uint32_t n) {
    return c->jkey[j] == key && c->jhead[j] == head && c->jlen[j] == n &&
           (n == 0u || memcmp(c->jbody + c->jstart[j], body,
                              sizeof(*body) * n) == 0);
}

static bool sv_just_table_grow(SvCache *c) {
    uint32_t cap = c->just_table_cap ? c->just_table_cap * 2u : 256u;
    uint32_t *table = calloc(cap, sizeof(*table));
    if (!table)
        return false;
    for (uint32_t j = 0u; j < c->njust; j++) {
        if (c->jkey[j] == SV_NONE)
            continue;
        uint32_t k = sv_derivation_hash(c->jkey[j], c->jhead[j],
                                        c->jbody + c->jstart[j], c->jlen[j]) &
                     (cap - 1u);
        while (table[k])
            k = (k + 1u) & (cap - 1u);
        table[k] = j + 1u;
    }
    free(c->just_table);
    c->just_table = table;
    c->just_table_cap = cap;
    return true;
}

/* A justification of `head`: with no key a base fact, else the derivation of
 * `head` by that key from `body`.  SV_NONE when memory ran out. */
static uint32_t sv_just_add(SvCache *c, uint32_t key, uint32_t head,
                            const uint32_t *body, uint32_t n, bool enabled) {
    if (c->njust == c->capjust) {
        uint32_t cap = c->capjust ? 2u * c->capjust : 256u;
        if (!sv_grow((void **)&c->jkey, sizeof(*c->jkey), cap) ||
            !sv_grow((void **)&c->jhead, sizeof(*c->jhead), cap) ||
            !sv_grow((void **)&c->jstart, sizeof(*c->jstart), cap) ||
            !sv_grow((void **)&c->jlen, sizeof(*c->jlen), cap) ||
            !sv_grow((void **)&c->jenabled, sizeof(*c->jenabled), cap))
            return SV_NONE;
        c->capjust = cap;
    }
    if (c->nbody + n > c->capbody) {
        uint32_t cap = c->capbody ? c->capbody : 1024u;
        while (cap < c->nbody + n)
            cap *= 2u;
        if (!sv_grow((void **)&c->jbody, sizeof(*c->jbody), cap))
            return SV_NONE;
        c->capbody = cap;
    }
    if (key != SV_NONE && (c->njust + 1u) * 2u > c->just_table_cap &&
        !sv_just_table_grow(c))
        return SV_NONE;
    uint32_t j = support_add(c->graph, head, body, n);
    if (j != c->njust)
        return SV_NONE;
    c->jkey[j] = key;
    c->jhead[j] = head;
    c->jstart[j] = c->nbody;
    c->jlen[j] = n;
    c->jenabled[j] = enabled ? 1u : 0u;
    if (n)
        memcpy(c->jbody + c->nbody, body, sizeof(*body) * n);
    c->nbody += n;
    c->njust++;
    if (key != SV_NONE) {
        uint32_t mask = c->just_table_cap - 1u;
        uint32_t k = sv_derivation_hash(key, head, body, n) & mask;
        while (c->just_table[k])
            k = (k + 1u) & mask;
        c->just_table[k] = j + 1u;
    }
    return j;
}

/* The derivation of `head` by `key` from `body`, recorded now unless it was
 * (*fresh says which). */
static uint32_t sv_derivation(SvCache *c, uint32_t key, uint32_t head,
                              const uint32_t *body, uint32_t n, bool *fresh) {
    *fresh = false;
    if (c->just_table_cap) {
        uint32_t mask = c->just_table_cap - 1u;
        for (uint32_t k = sv_derivation_hash(key, head, body, n) & mask;;
             k = (k + 1u) & mask) {
            uint32_t entry = c->just_table[k];
            if (!entry)
                break;
            if (sv_derivation_is(c, entry - 1u, key, head, body, n))
                return entry - 1u;
        }
    }
    *fresh = true;
    return sv_just_add(c, key, head, body, n, true);
}

static bool sv_enabled(void *context, uint32_t j) {
    return ((const SvCache *)context)->jenabled[j] != 0u;
}

/* ── Reading directives, views and logs ────────────────────────────────── */

static bool sv_var_occurs(Atom *atom, VarId id) {
    if (!atom || !atom_has_vars(atom))
        return false;
    if (atom->kind == ATOM_VAR)
        return var_id_eq(atom->var_id, id);
    if (atom->kind == ATOM_EXPR)
        for (CettaExprLen i = 0u; i < atom->expr.len; i++)
            if (sv_var_occurs(atom->expr.elems[i], id))
                return true;
    return false;
}

/* Every variable of `t` is bound by a row of the directive's query. */
static bool sv_vars_bound(Atom *t, const CettaExecDirective *rule) {
    if (!atom_has_vars(t))
        return true;
    if (t->kind == ATOM_VAR) {
        for (CettaExprLen i = 0u; i < rule->npatterns; i++)
            if (sv_var_occurs(rule->patterns[i], t->var_id))
                return true;
        return false;
    }
    if (t->kind == ATOM_EXPR)
        for (CettaExprLen i = 0u; i < t->expr.len; i++)
            if (!sv_vars_bound(t->expr.elems[i], rule))
                return false;
    return true;
}

/* Directives as exec:saturate reads them, with ground keys and templates
 * whose instances are ground over ground facts. */
static bool sv_rules_read(SvCache *c, Atom *directives) {
    if (!directives || directives->kind != ATOM_EXPR)
        return false;
    c->directives = atom_deep_copy(&c->arena, directives);
    if (!c->directives)
        return false;
    uint32_t n = (uint32_t)c->directives->expr.len;
    c->nrules = n;
    c->rules = n ? calloc(n, sizeof(*c->rules)) : NULL;
    c->rule_key = n ? calloc(n, sizeof(*c->rule_key)) : NULL;
    if (n && (!c->rules || !c->rule_key))
        return false;
    for (uint32_t i = 0u; i < n; i++) {
        CettaExecDirective *rule = &c->rules[i];
        if (!cetta_exec_directive_read(c->directives->expr.elems[i], rule) ||
            atom_has_vars(rule->key))
            return false;
        for (CettaExprLen t = 0u; t < rule->ntemplates; t++)
            if (!sv_vars_bound(rule->templates[t], rule))
                return false;
        c->rule_key[i] = sv_key_intern(c, rule->key);
        if (c->rule_key[i] == SV_NONE)
            return false;
    }
    return true;
}

/* The view's atoms become present: ground, and none twice. */
static bool sv_view_read(SvCache *c, Space *view, Arena *a) {
    CettaCount len = space_length64(view);
    for (CettaIndex i = 0u; i < len; i++) {
        Atom *atom = space_view_at64(view, i, a);
        if (!atom)
            continue;
        if (atom_has_vars(atom))
            return false;
        uint32_t id = sv_atom_intern(c, atom);
        if (id == SV_NONE || c->state[id] != SV_ABSENT)
            return false;
        c->state[id] = SV_PRESENT;
    }
    return true;
}

static bool sv_is(Atom *atom, const char *head, CettaExprLen len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
           atom_is_symbol(atom->expr.elems[0], head);
}

/* The log's records, each once: (support:base A) and
 * (support:just KEY HEAD (B ...)), ground. */
static bool sv_log_read(SvCache *c, Space *log, Arena *a) {
    uint32_t *body = NULL;
    uint32_t cap = 0u;
    bool ok = true;
    CettaCount len = space_length64(log);
    for (CettaIndex i = 0u; ok && i < len; i++) {
        Atom *record = space_view_at64(log, i, a);
        if (!record)
            continue;
        ok = !atom_has_vars(record);
        if (ok && sv_is(record, "support:base", 2u)) {
            uint32_t id = sv_atom_intern(c, record->expr.elems[1]);
            ok = id != SV_NONE && c->base[id] == SV_NONE;
            if (ok) {
                c->base[id] = sv_just_add(c, SV_NONE, id, NULL, 0u, true);
                ok = c->base[id] != SV_NONE;
            }
        } else if (ok && sv_is(record, "support:just", 4u) &&
                   record->expr.elems[3]->kind == ATOM_EXPR) {
            Atom *list = record->expr.elems[3];
            uint32_t n = (uint32_t)list->expr.len;
            if (n > cap) {
                cap = n;
                ok = sv_grow((void **)&body, sizeof(*body), cap);
            }
            uint32_t key = ok ? sv_key_intern(c, record->expr.elems[1])
                              : SV_NONE;
            uint32_t head = ok ? sv_atom_intern(c, record->expr.elems[2])
                               : SV_NONE;
            ok = key != SV_NONE && head != SV_NONE;
            for (uint32_t k = 0u; ok && k < n; k++) {
                body[k] = sv_atom_intern(c, list->expr.elems[k]);
                ok = body[k] != SV_NONE;
            }
            bool fresh = false;
            ok = ok && sv_derivation(c, key, head, body, n, &fresh) !=
                           SV_NONE && fresh;
        } else {
            ok = false;
        }
    }
    free(body);
    return ok;
}

static void sv_seal(SvCache *c, Space *view, Space *log) {
    c->view = view;
    c->log = log;
    c->view_instance = space_instance_id(view);
    c->log_instance = space_instance_id(log);
    c->view_revision = space_revision(view);
    c->log_revision = space_revision(log);
    c->used = ++g_sv_clock;
}

/* Sources for the atoms without one; the present atoms left without one are
 * written to *gone (malloc'd), their number to *ngone. */
static bool sv_recheck(SvCache *c, uint32_t **gone, uint32_t *ngone) {
    uint32_t *unsupported = NULL, count = 0u;
    if (!support_recheck(c->graph, sv_enabled, NULL, c, &unsupported,
                         &count))
        return false;
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < count; i++)
        if (c->state[unsupported[i]] == SV_PRESENT)
            unsupported[n++] = unsupported[i];
    if (gone) {
        *gone = unsupported;
        *ngone = n;
    } else {
        free(unsupported);
    }
    return true;
}

static SvCache *sv_slot(void) {
    SvCache *slot = &g_sv_caches[0];
    for (uint32_t i = 0u; i < SV_CACHES; i++) {
        SvCache *c = &g_sv_caches[i];
        if (!c->live)
            return c;
        if (c->used < slot->used)
            slot = c;
    }
    return slot;
}

/* The state of the handle's view and log under its directives: the cache
 * its last writes left, or one read from them now.  NULL when they are no
 * state this realization keeps. */
static SvCache *sv_state(Space *view, Space *log, Atom *directives, Arena *a) {
    SvCache *slot = NULL;
    for (uint32_t i = 0u; i < SV_CACHES && !slot; i++) {
        SvCache *c = &g_sv_caches[i];
        if (c->live && c->log == log &&
            c->log_instance == space_instance_id(log))
            slot = c;
    }
    if (slot && slot->view == view &&
        slot->view_instance == space_instance_id(view) &&
        slot->view_revision == space_revision(view) &&
        slot->log_revision == space_revision(log) &&
        atom_eq(slot->directives, directives)) {
        slot->used = ++g_sv_clock;
        return slot;
    }
    if (!slot)
        slot = sv_slot();
    if (support_trace())
        fprintf(stderr, "[support] reading the view and its log\n");
    if (!sv_begin(slot) || !sv_rules_read(slot, directives) ||
        !sv_log_read(slot, log, a) || !sv_view_read(slot, view, a) ||
        !sv_recheck(slot, NULL, NULL)) {
        sv_reset(slot);
        return NULL;
    }
    slot->closed = false;
    sv_seal(slot, view, log);
    return slot;
}

/* ── Records and answers ───────────────────────────────────────────────── */

static Atom *sv_base_record(Arena *a, Atom *atom) {
    return atom_expr2(a, atom_symbol(a, "support:base"), atom);
}

static Atom *sv_body(Arena *a, const SvCache *c, uint32_t j, bool copy) {
    uint32_t n = c->jlen[j];
    Atom **items = arena_alloc(a, sizeof(*items) * (n ? n : 1u));
    if (!items)
        return NULL;
    for (uint32_t k = 0u; k < n; k++) {
        Atom *atom = c->atoms[c->jbody[c->jstart[j] + k]].atom;
        items[k] = copy ? atom_deep_copy(a, atom) : atom;
        if (!items[k])
            return NULL;
    }
    return atom_expr(a, items, n);
}

static Atom *sv_just_record(Arena *a, const SvCache *c, uint32_t j) {
    Atom *body = sv_body(a, c, j, false);
    if (!body)
        return NULL;
    Atom *items[4] = {atom_symbol(a, "support:just"), c->keys[c->jkey[j]],
                      c->atoms[c->jhead[j]].atom, body};
    return atom_expr(a, items, 4u);
}

/* (TAG (A ...)) of the atoms numbered in `ids`, copied into `a`. */
static Atom *sv_atoms_answer(Arena *a, const SvCache *c, const char *tag,
                             const uint32_t *ids, uint32_t n) {
    Atom **items = arena_alloc(a, sizeof(*items) * (n ? n : 1u));
    if (!items)
        return NULL;
    for (uint32_t i = 0u; i < n; i++) {
        items[i] = atom_deep_copy(a, c->atoms[ids[i]].atom);
        if (!items[i])
            return NULL;
    }
    Atom *list = atom_expr(a, items, n);
    return list ? atom_expr2(a, atom_symbol(a, tag), list) : NULL;
}

/* ── Rounds ────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t *items;
    uint32_t len, cap;
} SvIds;

static bool sv_ids_push(SvIds *ids, uint32_t id) {
    if (ids->len == ids->cap) {
        uint32_t cap = ids->cap ? 2u * ids->cap : 64u;
        if (!sv_grow((void **)&ids->items, sizeof(*ids->items), cap))
            return false;
        ids->cap = cap;
    }
    ids->items[ids->len++] = id;
    return true;
}

typedef struct {
    SvCache *c;
    Arena *a;
    const CettaSupportHost *host;
    Atom *log_ref;
    uint32_t *body;
    uint32_t body_cap;
    SvIds pending;
    Atom *error;
} SvRound;

/* A row of a directive: its body atoms and every template instance are
 * numbered, each derivation not recorded before is recorded in the log, and
 * each head the view does not hold is to be published. */
static bool sv_visit(void *context, const CettaExecDirective *rule,
                     Bindings *row) {
    SvRound *r = context;
    SvCache *c = r->c;
    uint32_t key = c->rule_key[rule - c->rules];
    uint32_t n = (uint32_t)rule->npatterns;
    if (n > r->body_cap) {
        if (!sv_grow((void **)&r->body, sizeof(*r->body), n))
            return false;
        r->body_cap = n;
    }
    for (uint32_t i = 0u; i < n; i++) {
        Atom *instance = bindings_apply(row, r->a, rule->patterns[i]);
        r->body[i] = instance && !atom_has_vars(instance)
            ? sv_atom_intern(c, instance) : SV_NONE;
        if (r->body[i] == SV_NONE)
            return false;
    }
    for (CettaExprLen t = 0u; t < rule->ntemplates; t++) {
        Atom *instance = bindings_apply(row, r->a, rule->templates[t]);
        uint32_t head = instance && !atom_has_vars(instance)
            ? sv_atom_intern(c, instance) : SV_NONE;
        bool fresh = false;
        uint32_t j = head == SV_NONE
            ? SV_NONE : sv_derivation(c, key, head, r->body, n, &fresh);
        if (j == SV_NONE)
            return false;
        if (fresh) {
            Atom *record = sv_just_record(r->a, c, j);
            if (!record)
                return false;
            r->error = r->host->add(r->host->context, r->log_ref, record);
            if (r->error)
                return false;
        }
        if (c->state[head] == SV_ABSENT) {
            c->state[head] = SV_PENDING;
            if (!sv_ids_push(&r->pending, head))
                return false;
        }
    }
    return true;
}

/* Rounds until one publishes nothing: the first over the whole view when it
 * is not known to be closed, else over the rows reading `seed`; after that
 * over the rows reading what the previous round published.  The atoms
 * published are appended to `added`. */
static bool sv_saturate(SvCache *c, Arena *a, const CettaSupportHost *host,
                        Atom *view_ref, Atom *log_ref, Space *view,
                        const uint32_t *seed, uint32_t nseed, SvIds *added,
                        Atom **error_out) {
    SvRound r = {.c = c, .a = a, .host = host, .log_ref = log_ref};
    bool full = !c->closed;
    uint32_t delta_from = 0u, delta_len = nseed;
    const uint32_t *delta_ids = seed;
    bool ok = true;
    while (ok && (full || delta_len > 0u)) {
        Space delta;
        if (!full) {
            space_init_with_universe(&delta, view->native.universe);
            for (uint32_t i = 0u; i < delta_len; i++)
                space_add(&delta, c->atoms[delta_ids[delta_from + i]].atom);
        }
        r.pending.len = 0u;
        for (uint32_t i = 0u; ok && i < c->nrules; i++)
            ok = cetta_exec_directive_rows(view, full ? NULL : &delta, a,
                                           &c->rules[i], sv_visit, &r);
        if (!full)
            space_free(&delta);
        full = false;
        uint32_t round_from = added->len;
        for (uint32_t i = 0u; ok && i < r.pending.len; i++) {
            uint32_t id = r.pending.items[i];
            r.error = host->add(host->context, view_ref, c->atoms[id].atom);
            ok = !r.error && sv_ids_push(added, id);
            c->state[id] = SV_PRESENT;
        }
        delta_ids = added->items;
        delta_from = round_from;
        delta_len = added->len - round_from;
    }
    free(r.body);
    free(r.pending.items);
    if (!ok) {
        *error_out = r.error;
        return false;
    }
    c->closed = true;
    return true;
}

/* ── Operations ────────────────────────────────────────────────────────── */

typedef struct {
    Atom *view_ref, *log_ref, *directives;
    Space *view, *log;
} SvHandle;

/* A space whose rows this realization reads as they are: not the program,
 * and not an attached compiled space, whose rows are materialized by the
 * first write the library's own path makes. */
static bool sv_space_readable(Space *program, Space *space) {
    return space && space != program &&
           !space_match_backend_is_attached_compiled(space);
}

/* (support:view VIEW LOG DIRECTIVES) over two readable spaces. */
static bool sv_handle(Space *program, const CettaSupportHost *host,
                      Atom *atom, SvHandle *h) {
    if (!sv_is(atom, "support:view", 4u))
        return false;
    h->view_ref = atom->expr.elems[1];
    h->log_ref = atom->expr.elems[2];
    h->directives = atom->expr.elems[3];
    h->view = host->resolve_space(host->context, h->view_ref);
    h->log = host->resolve_space(host->context, h->log_ref);
    return sv_space_readable(program, h->view) &&
           sv_space_readable(program, h->log) && h->view != h->log;
}

static CettaSupportOutcome sv_failed(SvCache *c, Arena *a, Atom *call,
                                     Atom *error, Atom **answer_out) {
    sv_reset(c);
    *answer_out = error ? error
                        : atom_error(a, call, atom_symbol(a, "SupportViewFailed"));
    return CETTA_SUPPORT_ERROR;
}

static CettaSupportOutcome sv_open(Space *program, Arena *a, Atom *call,
                                   const CettaSupportHost *host,
                                   Atom **answer_out) {
    Atom *view_ref = call->expr.elems[1];
    Atom *directives = call->expr.elems[2];
    Space *view = host->resolve_space(host->context, view_ref);
    if (!sv_space_readable(program, view))
        return CETTA_SUPPORT_DECLINED;
    SvCache *c = sv_slot();
    if (!sv_begin(c) || !sv_rules_read(c, directives) ||
        !sv_view_read(c, view, a)) {
        sv_reset(c);
        return CETTA_SUPPORT_DECLINED;
    }
    Atom *log_ref = host->new_space(host->context);
    Space *log = log_ref ? host->resolve_space(host->context, log_ref) : NULL;
    if (!sv_space_readable(program, log) || log == view) {
        sv_reset(c);
        return CETTA_SUPPORT_DECLINED;
    }
    uint32_t nbase = c->natoms;
    for (uint32_t id = 0u; id < nbase; id++) {
        Atom *error = host->add(host->context, log_ref,
                                sv_base_record(a, c->atoms[id].atom));
        if (error)
            return sv_failed(c, a, call, error, answer_out);
        c->base[id] = sv_just_add(c, SV_NONE, id, NULL, 0u, true);
        if (c->base[id] == SV_NONE)
            return sv_failed(c, a, call, NULL, answer_out);
    }
    SvIds added = {0};
    Atom *error = NULL;
    c->closed = false;
    bool ok = sv_saturate(c, a, host, view_ref, log_ref, view, NULL, 0u,
                          &added, &error) &&
              sv_recheck(c, NULL, NULL);
    free(added.items);
    if (!ok)
        return sv_failed(c, a, call, error, answer_out);
    sv_seal(c, view, log);
    Atom *items[4] = {atom_symbol(a, "support:view"), view_ref, log_ref,
                      directives};
    *answer_out = atom_expr(a, items, 4u);
    return *answer_out ? CETTA_SUPPORT_ANSWERED
                       : sv_failed(c, a, call, NULL, answer_out);
}

static CettaSupportOutcome sv_assert(SvCache *c, Arena *a, Atom *call,
                                     const CettaSupportHost *host,
                                     const SvHandle *h, Atom *atom,
                                     Atom **answer_out) {
    uint32_t id = sv_atom_intern(c, atom);
    if (id == SV_NONE)
        return sv_failed(c, a, call, NULL, answer_out);
    uint32_t base = c->base[id];
    if (base == SV_NONE || !c->jenabled[base]) {
        Atom *error = host->add(host->context, h->log_ref,
                                sv_base_record(a, c->atoms[id].atom));
        if (error)
            return sv_failed(c, a, call, error, answer_out);
        if (base == SV_NONE) {
            c->base[id] = sv_just_add(c, SV_NONE, id, NULL, 0u, true);
            if (c->base[id] == SV_NONE)
                return sv_failed(c, a, call, NULL, answer_out);
        } else {
            c->jenabled[base] = 1u;
            if (!support_reopen(c->graph, id))
                return sv_failed(c, a, call, NULL, answer_out);
        }
    }
    SvIds added = {0};
    if (c->state[id] == SV_ABSENT) {
        Atom *error = host->add(host->context, h->view_ref, c->atoms[id].atom);
        if (error)
            return sv_failed(c, a, call, error, answer_out);
        c->state[id] = SV_PRESENT;
        if (!sv_ids_push(&added, id))
            return sv_failed(c, a, call, NULL, answer_out);
    }
    Atom *error = NULL;
    uint32_t nseed = added.len;
    bool ok = sv_saturate(c, a, host, h->view_ref, h->log_ref, h->view,
                          added.items, nseed, &added, &error) &&
              sv_recheck(c, NULL, NULL);
    Atom *answer = ok ? sv_atoms_answer(a, c, "added", added.items, added.len)
                      : NULL;
    free(added.items);
    if (!answer)
        return sv_failed(c, a, call, error, answer_out);
    sv_seal(c, h->view, h->log);
    *answer_out = answer;
    return CETTA_SUPPORT_ANSWERED;
}

static CettaSupportOutcome sv_retract(SvCache *c, Arena *a, Atom *call,
                                      const CettaSupportHost *host,
                                      const SvHandle *h, Atom *atom,
                                      Atom **answer_out) {
    uint32_t id = sv_atom_lookup(c, atom);
    uint32_t base = id == SV_NONE ? SV_NONE : c->base[id];
    if (base != SV_NONE && c->jenabled[base]) {
        Atom *error = host->remove(host->context, h->log_ref,
                                   sv_base_record(a, c->atoms[id].atom));
        if (error)
            return sv_failed(c, a, call, error, answer_out);
        c->jenabled[base] = 0u;
        if (!support_lose(c->graph, base))
            return sv_failed(c, a, call, NULL, answer_out);
    }
    uint32_t *gone = NULL, ngone = 0u;
    if (!sv_recheck(c, &gone, &ngone))
        return sv_failed(c, a, call, NULL, answer_out);
    for (uint32_t i = 0u; i < ngone; i++) {
        Atom *error = host->remove(host->context, h->view_ref,
                                   c->atoms[gone[i]].atom);
        if (error) {
            free(gone);
            return sv_failed(c, a, call, error, answer_out);
        }
        c->state[gone[i]] = SV_ABSENT;
    }
    Atom *answer = sv_atoms_answer(a, c, "removed", gone, ngone);
    free(gone);
    if (!answer)
        return sv_failed(c, a, call, NULL, answer_out);
    sv_seal(c, h->view, h->log);
    *answer_out = answer;
    return CETTA_SUPPORT_ANSWERED;
}

static CettaSupportOutcome sv_why(SvCache *c, Arena *a, Atom *atom,
                                  Atom **answer_out) {
    uint32_t id = sv_atom_lookup(c, atom);
    if (id == SV_NONE || c->state[id] != SV_PRESENT) {
        *answer_out = atom_unit(a);
        return CETTA_SUPPORT_ANSWERED;
    }
    uint32_t base = c->base[id];
    if (base != SV_NONE && c->jenabled[base]) {
        Atom *tag = atom_symbol(a, "base");
        *answer_out = atom_expr(a, &tag, 1u);
        return CETTA_SUPPORT_ANSWERED;
    }
    uint32_t j = support_source(c->graph, id);
    /* A present atom without a well-founded derivation, which only a write
     * this realization did not make leaves: the library answers nothing. */
    if (j == SV_NONE || c->jkey[j] == SV_NONE)
        return CETTA_SUPPORT_DECLINED;
    Atom *key = atom_deep_copy(a, c->keys[c->jkey[j]]);
    Atom *body = sv_body(a, c, j, true);
    *answer_out = key && body
        ? atom_expr3(a, atom_symbol(a, "derived"), key, body) : NULL;
    return *answer_out ? CETTA_SUPPORT_ANSWERED : CETTA_SUPPORT_DECLINED;
}

CettaSupportOutcome cetta_support_call(Space *program, Arena *a, Atom *call,
                                       const CettaSupportHost *host,
                                       Atom **answer_out) {
    if (!program || !a || !call || !host || !answer_out ||
        call->kind != ATOM_EXPR || call->expr.len != 3u ||
        !host->resolve_space || !host->add || !host->remove ||
        !host->new_space)
        return CETTA_SUPPORT_DECLINED;
    SvOperation operation = support_operation(
        atom_head_symbol_id(call), call->expr.len - 1u);
    if (operation == SV_NOT_OURS)
        return CETTA_SUPPORT_DECLINED;
    *answer_out = NULL;
    CettaSupportOutcome outcome = CETTA_SUPPORT_DECLINED;
    if (operation == SV_OPEN) {
        outcome = sv_open(program, a, call, host, answer_out);
    } else {
        SvHandle h;
        Atom *atom = call->expr.elems[2];
        SvCache *c = !atom_has_vars(atom) &&
                sv_handle(program, host, call->expr.elems[1], &h)
            ? sv_state(h.view, h.log, h.directives, a) : NULL;
        if (c && operation == SV_ASSERT)
            outcome = sv_assert(c, a, call, host, &h, atom, answer_out);
        else if (c && operation == SV_RETRACT)
            outcome = sv_retract(c, a, call, host, &h, atom, answer_out);
        else if (c)
            outcome = sv_why(c, a, atom, answer_out);
    }
    if (support_trace())
        fprintf(stderr, "[support] %s %s\n",
                atom_name_cstr(call->expr.elems[0]),
                outcome == CETTA_SUPPORT_ANSWERED ? "answered"
                : outcome == CETTA_SUPPORT_ERROR  ? "error"
                                                  : "declined");
    return outcome;
}
