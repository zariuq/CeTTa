/* Include space.h first when space_theory.h is included directly, before
 * setting CETTA_SPACE_THEORY_H, so space.h's own #include "space_theory.h"
 * defines SpaceTheoryToken at the exact right point. */
#ifndef CETTA_SPACE_H
#include "space.h"
#endif

#ifndef CETTA_SPACE_THEORY_H
#define CETTA_SPACE_THEORY_H

/*
 * Live Space Theory View (`SpaceTheoryView`), Atom Classifier,
 * Versioned Theory Token (`SpaceTheoryToken`), Version-Counter Guards,
 * Lazy Per-Head Version Counters & Sealing (`SpaceHeadClocks`),
 * Observation Lattice, and Region Validity Checks (`SpaceRegionGuard`).
 *
 * A live MeTTa Space presents an executable theory over the shared Atom
 * signature:
 *   - ordinary atoms remain inspectable data,
 *   - `(= lhs rhs)` / `(=% lhs rhs)` atoms are its ordered function definitions,
 *   - `(: subject type)` / `(:< sub super)` atoms are its type and subtype assignments,
 *   - `(=e ...)` / `(=e> ...)` / `(=e< ...)` atoms are its scoped ground equations.
 *
 * Every dialect (HE, PeTTa, Prime), write-time classifier, and compiled fast
 * path consults and validates this single view via name-stable head SymbolIds
 * and the unified program-clock discipline (read clock before token; re-arm
 * clock only after confirming the token is still current).
 */

/* ── Theory Coordinate Token ────────────────────────────────────────────── */

/* Lifetime-qualified coordinate of the live theory projection a Space presents
 * to callability, named-arity, typing, effect classification, and regional
 * admission.  Data-only mutations leave it unchanged.  An overlay freezes a
 * base storage prefix while reading its modules live; `base_dependency_epoch`
 * folds that chain so module appends invalidate overlays without unrelated
 * spaces doing so. */
typedef struct SpaceTheoryToken {
    const Space *space;
    uint64_t instance_id;
    uint64_t equation_revision;
    uint64_t declaration_revision;
    uint64_t base_dependency_epoch;
} SpaceTheoryToken;

static inline bool space_theory_token_cacheable(SpaceTheoryToken token) {
    return token.instance_id != 0u &&
           token.base_dependency_epoch != UINT64_MAX;
}

/* Inline token capture: ordinary non-overlay spaces pay zero out-of-line
 * dependency-walk calls.  Revisions and lifetime identity are loaded
 * atomically (relaxed), ordered after acquire on space_global_program_epoch()
 * (Mettapedia/Machines/VersionedValidation/Clock.lean).  Equational-alias
 * mutations (`=e`) advance `equation_revision` alongside `equational_revision`,
 * so `SpaceTheoryToken` and `SpaceEquationToken` both invalidate on `=e` edits
 * without adding a sixth coordinate. */
static inline SpaceTheoryToken space_theory_token(const Space *s) {
    if (!s)
        return (SpaceTheoryToken){0};
    return (SpaceTheoryToken){
        .space = s,
        .instance_id =
            atomic_load_explicit(&s->instance_id, memory_order_relaxed),
        .equation_revision =
            atomic_load_explicit(&s->equation_revision, memory_order_relaxed),
        .declaration_revision =
            atomic_load_explicit(&s->declaration_revision, memory_order_relaxed),
        .base_dependency_epoch =
            s->overlay_base ? space_overlay_dependency_epoch(s) : 0u,
    };
}

static inline bool space_theory_token_eq(SpaceTheoryToken left,
                                         SpaceTheoryToken right) {
    return left.space == right.space &&
           left.instance_id == right.instance_id &&
           left.equation_revision == right.equation_revision &&
           left.declaration_revision == right.declaration_revision &&
           left.base_dependency_epoch == right.base_dependency_epoch;
}

static inline bool space_theory_token_is_current(SpaceTheoryToken token) {
    const Space *s = token.space;
    return s && token.instance_id != 0u &&
           token.base_dependency_epoch != UINT64_MAX &&
           token.instance_id ==
               atomic_load_explicit(&s->instance_id, memory_order_relaxed) &&
           token.equation_revision ==
               atomic_load_explicit(&s->equation_revision, memory_order_relaxed) &&
           token.declaration_revision ==
               atomic_load_explicit(&s->declaration_revision,
                                    memory_order_relaxed) &&
           token.base_dependency_epoch ==
               (s->overlay_base ? space_overlay_dependency_epoch(s) : 0u);
}

static inline bool space_theory_token_matches_live_space(
        SpaceTheoryToken token, const Space *live_space) {
    return live_space && token.space == live_space &&
           space_theory_token_is_current(token);
}

/* Declaration-only currency: ignores equation-only and data-only mutations
 * while remaining sound on overlays and module dependency chains. */
static inline bool space_theory_token_declarations_current(
        SpaceTheoryToken token, const Space *live_space) {
    return live_space && token.space == live_space &&
           token.instance_id != 0u &&
           token.base_dependency_epoch != UINT64_MAX &&
           token.instance_id ==
               atomic_load_explicit(&live_space->instance_id,
                                    memory_order_relaxed) &&
           token.declaration_revision ==
               atomic_load_explicit(&live_space->declaration_revision,
                                    memory_order_relaxed) &&
           token.base_dependency_epoch ==
               (live_space->overlay_base
                    ? space_overlay_dependency_epoch(live_space)
                    : 0u);
}

/* ── Unified Two-Tier Program-Clock + Theory-Token Guards ───────────────── */

/* Single-owner (thread-local / machine-local) guard.  Hot check compares
 * `program_clock` against `space_global_program_epoch()`; on movement, the
 * token is checked and, if still current, re-arms the clock stamp. */
typedef struct SpaceTheoryClockGuard {
    SpaceTheoryToken token;
    uint64_t program_clock;
    bool clock_known;
} SpaceTheoryClockGuard;

static inline void space_theory_clock_guard_capture(
        SpaceTheoryClockGuard *guard, const Space *space) {
    uint64_t clock = space_global_program_epoch();
    SpaceTheoryToken token = space_theory_token(space);
    guard->token = token;
    guard->program_clock = clock;
    guard->clock_known = space_theory_token_cacheable(token);
}

static inline bool space_theory_clock_guard_fast_current(
        const SpaceTheoryClockGuard *guard, const Space *space,
        uint64_t clock) {
    return guard->token.space == space &&
           guard->clock_known &&
           guard->program_clock == clock;
}

static inline bool space_theory_clock_guard_rearm(
        SpaceTheoryClockGuard *guard, const Space *space, uint64_t clock) {
    if (!space_theory_token_matches_live_space(guard->token, space))
        return false;
    guard->program_clock = clock;
    guard->clock_known = true;
    return true;
}

/* Shared-program atomic guard (used by immutable compiled regions shared
 * across threads).  Hot path is a single relaxed atomic load + compare;
 * re-arming stays out of line in a cold helper so callers keep no frame. */
typedef struct SpaceTheoryAtomicClockGuard {
    SpaceTheoryToken token;
    _Atomic uint64_t program_clock;
} SpaceTheoryAtomicClockGuard;

static inline void space_theory_atomic_clock_guard_capture(
        SpaceTheoryAtomicClockGuard *guard, const Space *space) {
    uint64_t clock = space_global_program_epoch();
    SpaceTheoryToken token = space_theory_token(space);
    guard->token = token;
    atomic_store_explicit(
        &guard->program_clock,
        space_theory_token_cacheable(token) ? clock : UINT64_MAX,
        memory_order_relaxed);
}

bool space_theory_atomic_clock_guard_recheck(
    SpaceTheoryAtomicClockGuard *guard) __attribute__((cold, noinline));

static inline bool space_theory_atomic_clock_guard_is_current(
        SpaceTheoryAtomicClockGuard *guard) {
    return space_global_program_epoch() ==
               atomic_load_explicit(&guard->program_clock,
                                    memory_order_relaxed) ||
           space_theory_atomic_clock_guard_recheck(guard);
}

/* ── Live Borrowed Theory View ──────────────────────────────────────────── */

typedef struct SpaceTheoryView {
    Space *space;
    SpaceTheoryToken token;
    uint64_t program_clock;
} SpaceTheoryView;

static inline SpaceTheoryView space_theory_view_open(Space *space) {
    uint64_t clock = space_global_program_epoch();
    return (SpaceTheoryView){
        .space = space,
        .token = space_theory_token(space),
        .program_clock = clock,
    };
}

/* Zero-overhead borrowed view for hot intra-turn dispatch predicates where
 * the caller does not retain the view across mutations. */
static inline SpaceTheoryView space_theory_view_borrow(Space *space) {
    return (SpaceTheoryView){
        .space = space,
    };
}

static inline bool space_theory_view_is_current(const SpaceTheoryView *view) {
    if (!view || !view->space)
        return false;
    if (space_theory_token_cacheable(view->token) &&
        view->program_clock == space_global_program_epoch())
        return true;
    return space_theory_token_is_current(view->token);
}

/* Suffix-tracked cache for incremental O(delta) equation collection over
 * plain native spaces (`space_theory_view_collect_equations`). */
typedef struct SpaceTheoryEquationCache {
    AtomId *own_alias_ids;
    size_t own_alias_len;
    size_t own_alias_cap;
    CettaCount own_alias_synced_len;
    uint64_t own_alias_prefix_epoch;
    CettaIndex own_alias_marked;
    uint64_t own_alias_universe_instance;
    uint64_t own_alias_universe_epoch;
    bool own_alias_valid;
} SpaceTheoryEquationCache;

void space_theory_equation_cache_free(SpaceTheoryEquationCache *cache);

/* Cold equation-definition presence helper invoked by `space_theory_view_head_has_rules`. */
bool space_theory_view_head_has_alias_rules_slow(
    const SpaceTheoryView *view, SymbolId head, unsigned admitted_forms)
    __attribute__((cold, noinline));

/* Unified definition collector for `head` across ordinary, directional, and
 * equational-alias definitions in exact source order.  When `out_via_alias` is
 * non-NULL, reports whether active `=e` equations participated.  Caller frees
 * `*out_equations`. */
bool space_theory_view_collect_head_rules(
    const SpaceTheoryView *view, SymbolId head, unsigned admitted_forms,
    Atom ***out_equations, size_t *out_count, bool *out_via_alias);

/* Collect all active equation atoms (`=e`, `=e>`, `=e<`) visible through
 * `view` (own atoms plus dependencies/overlays), updating `cache` when
 * non-NULL.  Caller frees `*out_atoms`. */
bool space_theory_view_collect_equations(
    const SpaceTheoryView *view, SpaceTheoryEquationCache *cache,
    Atom ***out_atoms, size_t *out_count);

/* ── Name-Stable Head Hashing & Atom Classification ─────────────────────── */

/* Exact-head maps place a symbol by its stable SymbolId so head lookups never
 * alias absent heads onto later definitions under growth. */
static inline uint32_t space_theory_symbol_id_mix(SymbolId head) {
    uint32_t mixed = (uint32_t)head;
    mixed ^= mixed >> 16;
    mixed *= 0x7feb352du;
    mixed ^= mixed >> 15;
    mixed *= 0x846ca68bu;
    mixed ^= mixed >> 16;
    return mixed;
}

static inline const SpaceHeadChain *space_theory_head_chain_find(
        const SpaceHeadChains *rows, SymbolId head) {
    if (head == SYMBOL_ID_NONE)
        return &rows->headless;
    if (rows->chain_cap == 0u)
        return NULL;
    uint32_t mask = rows->chain_cap - 1u;
    for (uint32_t slot = space_theory_symbol_id_mix(head) & mask;;
         slot = (slot + 1u) & mask) {
        const SpaceHeadChain *chain = &rows->chains[slot];
        if (chain->head == head)
            return chain;
        if (chain->head == SYMBOL_ID_NONE)
            return NULL;
    }
}

/* Partition of a top-level Space atom into its theory role. */
typedef enum SpaceTheoryAtomKind {
    SPACE_THEORY_ATOM_DATA = 0,
    SPACE_THEORY_ATOM_DEFINITION,             /* (= lhs rhs) */
    SPACE_THEORY_ATOM_DIRECTIONAL_DEFINITION, /* (=% lhs rhs) */
    SPACE_THEORY_ATOM_TYPE_ASSIGNMENT,        /* (: subject type) */
    SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT,     /* (:< sub super) */
    SPACE_THEORY_ATOM_EQUATION,               /* (=e a1 ... an), (=e> ...), (=e< ...) */
} SpaceTheoryAtomKind;

/* The heads of equation atoms. `(=e a1 ... an)` equates its members;
 * `(=e> a1 ... an)` also prefers each member's right neighbour, and
 * `(=e< a1 ... an)` its left neighbour. Each takes two or more members. */
static inline bool space_theory_is_alias_head(SymbolId head) {
    return head == g_builtin_syms.equals_e ||
           head == g_builtin_syms.equals_e_right ||
           head == g_builtin_syms.equals_e_left;
}

/* Single predicate for every definition, type/subtype assignment, or equation
 * head (`=`, `:`, `=%`, `:<`, `=e`, `=e>`, `=e<`).  Every user data head
 * symbol lies above `equals_e_left`, so a single range compare rejects them
 * all in one branch. */
static inline bool space_theory_is_theory_head(SymbolId head) {
    return head <= g_builtin_syms.equals_e_left &&
           (head == g_builtin_syms.equals ||
            head == g_builtin_syms.colon ||
            head == g_builtin_syms.equals_percent ||
            head == g_builtin_syms.subtype ||
            space_theory_is_alias_head(head));
}

/* Single head+arity atom classifier shared by write-time classification,
 * definition filtering, dense-relation exclusion, and dialect lookups. */
static inline SpaceTheoryAtomKind space_theory_classify_head_arity(
        SymbolId head, CettaExprLen arity) {
    if (arity >= 3u && head <= g_builtin_syms.equals_e_left) {
        if (arity == 3u) {
            if (head == g_builtin_syms.equals)
                return SPACE_THEORY_ATOM_DEFINITION;
            if (head == g_builtin_syms.equals_percent)
                return SPACE_THEORY_ATOM_DIRECTIONAL_DEFINITION;
            if (head == g_builtin_syms.colon)
                return SPACE_THEORY_ATOM_TYPE_ASSIGNMENT;
            if (head == g_builtin_syms.subtype)
                return SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT;
        }
        if (space_theory_is_alias_head(head))
            return SPACE_THEORY_ATOM_EQUATION;
    }
    return SPACE_THEORY_ATOM_DATA;
}

static inline bool space_theory_atom_kind_is_definition(
        SpaceTheoryAtomKind kind) {
    return kind == SPACE_THEORY_ATOM_DEFINITION ||
           kind == SPACE_THEORY_ATOM_DIRECTIONAL_DEFINITION;
}

static inline bool space_theory_atom_kind_is_type_assignment(
        SpaceTheoryAtomKind kind) {
    return kind == SPACE_THEORY_ATOM_TYPE_ASSIGNMENT ||
           kind == SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT;
}

static inline bool space_theory_atom_kind_is_program(SpaceTheoryAtomKind kind) {
    return kind != SPACE_THEORY_ATOM_DATA;
}

/* True iff `atom` is guaranteed to remain an ordinary data atom under any
 * variable substitution: neither a definition/assignment/equation form nor an
 * open head/variable that could instantiate to one. */
static inline bool space_theory_atom_is_guaranteed_data(const Atom *atom) {
    if (!atom || atom->kind == ATOM_VAR)
        return false;
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return true;
    const Atom *head = atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    return space_theory_classify_head_arity(head->sym_id, atom->expr.len) ==
           SPACE_THEORY_ATOM_DATA;
}

/* Classify one top-level Space atom into its theory role.
 * When `head_out` is non-NULL, writes the exact target head SymbolId (for `=`
 * / `=%`, the head symbol of an expression LHS; for `:`, the symbol subject or
 * compound subject head; otherwise SYMBOL_ID_NONE).  When
 * `compound_subject_out` is non-NULL, sets true when a `:` assignment has a
 * compound (expression) subject. */
SpaceTheoryAtomKind space_theory_classify_atom(
    const Atom *atom, SymbolId *head_out, bool *compound_subject_out);

/* Zero-decode header classifier over `CettaTermHdr` returning the exact atom
 * kind, affected target head symbol (`SYMBOL_ID_NONE` for wildcard/global
 * atoms), and whether the atom is open/wildcard for its role. */
SpaceTheoryAtomKind space_theory_classify_header(
    const TermUniverse *u, AtomId id, const CettaTermHdr *hdr,
    SymbolId *target_head_out, bool *is_open_out);

/* Validate an overlay chain's captured theory tokens (equation and declaration
 * revisions across every live overlay layer). */
bool space_theory_overlay_chain_tokens_current(
    const Space *space, const SpaceTheoryToken *views, size_t count);

/* ── Lazy Per-Head Clocks, Sealing & Protecting Imports ─────────────────── */

enum {
    SPACE_THEORY_ROLE_RULES  = 1u << 0, /* (= (h ...) _), (=% (h ...) _) */
    SPACE_THEORY_ROLE_DECLS  = 1u << 1, /* (: h _), (: (h ...) _), (:< _ _) */
    SPACE_THEORY_ROLE_ALIAS  = 1u << 2, /* (=e ...), (=e> ...), (=e< ...) */
    SPACE_THEORY_ROLE_DATA   = 1u << 3, /* (h ...) data rows */
    SPACE_THEORY_ROLE_ALL    = SPACE_THEORY_ROLE_RULES |
                               SPACE_THEORY_ROLE_DECLS |
                               SPACE_THEORY_ROLE_ALIAS |
                               SPACE_THEORY_ROLE_DATA,
    SPACE_THEORY_ROLE_OPAQUE = 1u << 4, /* bulk/unclassified/link mutation */
};

typedef struct SpaceHeadClockStamp {
    const Space *space;
    const Space *overlay_base;
    uint64_t instance_id;
    uint64_t table_id;
    SymbolId head;
    uint8_t roles;
    uint64_t rule_rev;
    uint64_t decl_rev;
    uint64_t data_rev;
    uint64_t open_rule_rev;
    uint64_t open_decl_rev;
    uint64_t open_data_rev;
    uint64_t opaque_rev;
    uint64_t equational_revision;
    uint64_t base_dependency_epoch;
    uint64_t base_prefix_epoch;
} SpaceHeadClockStamp;

/* Write-time mutation descriptor passed to `space_theory_note_mutation_slow`
 * only when `s->head_clocks != NULL` (or an importer watches heads). */
typedef struct SpaceHeadMutationNote {
    const Space *source_space;
    AtomId atom_id;
    const Atom *atom;
    AtomId secondary_atom_id; /* relocated tail on unordered swap-remove */
    SymbolId head;
    uint8_t roles;
    bool has_atom;
    bool has_head;
    bool already_noted;
    bool is_opaque;
} SpaceHeadMutationNote;

void space_head_clocks_free(struct SpaceHeadClocks *clocks);

/* Ensure `head` has an entry in `s->head_clocks` (lazily allocating the table
 * on first watch or seal). */
bool space_theory_watch_head(Space *s, SymbolId head);

/* Mark `head` sealed for `seal_mask` (subset of `SPACE_THEORY_ROLE_ALL`).
 * Subsequent mutations affecting that head/role (or open/opaque mutations)
 * advance the corresponding clock and clear the violated seal bits. */
bool space_theory_seal_head(Space *s, SymbolId head, uint8_t seal_mask);

/* Query the currently intact seal bitmask for `head` on `s` (0 if unsealed or
 * unwatched). */
uint8_t space_theory_head_sealed_mask(const Space *s, SymbolId head);

/* Capture a head-scoped clock stamp for `(s, head, roles)` (lazily watching
 * `head`). */
bool space_theory_head_clock_capture(
    Space *s, SymbolId head, uint8_t roles, SpaceHeadClockStamp *out);

/* Validate a previously captured `SpaceHeadClockStamp` against `s`. */
bool space_theory_head_clock_current(
    const Space *s, const SpaceHeadClockStamp *stamp);

/* Compact multi-head clock stamp for a caller-owned list of SymbolIds on a
 * plain (non-overlay) Space.  Because every component revision counter is
 * monotonic non-decreasing under a fixed `table_id`, equality of the sum
 * across the head set and requested roles is equivalent to pointwise
 * equality of every component clock. */
typedef struct SpaceHeadSetStamp {
    uint64_t table_id;
    uint64_t combined_rev;
    uint32_t head_count;
    uint8_t roles;
    uint8_t active;
} SpaceHeadSetStamp;

bool space_theory_head_set_clock_capture(
    Space *s, const SymbolId *heads, uint32_t head_count, uint8_t roles,
    SpaceHeadSetStamp *out);

bool space_theory_head_set_clock_current(
    const Space *s, const SymbolId *heads, uint32_t head_count,
    const SpaceHeadSetStamp *stamp);

/* True iff `importer` imports `module`, `module` seals `head` for all
 * requested `roles`, and neither `importer`'s own rows nor any sibling import
 * extends/shadows `(head, roles)` (including open rows and, when
 * `SPACE_THEORY_ROLE_ALIAS` is requested, active `=e` alias rows). */
bool space_theory_import_protects_head(
    const Space *importer, const Space *module, SymbolId head, uint8_t roles);

/* Cold write-path hook invoked only when `s->head_clocks != NULL`. */
extern _Atomic uint64_t cetta_space_global_head_clocks_active;

static inline bool space_theory_any_head_clocks_active(void) {
    return __builtin_expect(
        atomic_load_explicit(&cetta_space_global_head_clocks_active,
                             memory_order_acquire) != 0u,
        0);
}

void space_theory_note_mutation_slow(
    Space *s, const SpaceHeadMutationNote *note, uint8_t fallback_roles)
    __attribute__((cold, noinline));

/* ── Observation Coarsening Lattice ─────────────────────────────────────── */

/* Exact 7-demand observation lattice on completed runs:
 *   TIMED    : exact completion ticks and every fuel-cutoff prefix
 *   ORDERED  : ordered answer list (preserving order & duplicates)
 *   BAG      : unordered answer multiset (preserving payload & multiplicity)
 *   COUNT    : total cardinality
 *   FIRST    : first answer witness
 *   PRESENCE : non-emptiness on completed run
 *   ABSENCE  : emptiness on completed run (Boolean dual of PRESENCE) */
typedef enum SpaceObserverKind {
    SPACE_OBSERVER_TIMED = 0,
    SPACE_OBSERVER_ORDERED = 1,
    SPACE_OBSERVER_BAG = 2,
    SPACE_OBSERVER_COUNT = 3,
    SPACE_OBSERVER_FIRST = 4,
    SPACE_OBSERVER_PRESENCE = 5,
    SPACE_OBSERVER_ABSENCE = 6,
} SpaceObserverKind;

#define SPACE_OBSERVER_COUNT_KINDS 7u

/* Exact 49-pair classification of whether a fast path proved sound for
 * `proved` automatically serves a caller observing `requested` on completed
 * runs. */
static inline bool space_observer_licenses(SpaceObserverKind proved,
                                           SpaceObserverKind requested) {
    static const uint8_t k_masks[7] = {
        /* TIMED    */ 0x7Fu,
        /* ORDERED  */ (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6),
        /* BAG      */ (1u << 2) | (1u << 3) | (1u << 5) | (1u << 6),
        /* COUNT    */ (1u << 3) | (1u << 5) | (1u << 6),
        /* FIRST    */ (1u << 4) | (1u << 5) | (1u << 6),
        /* PRESENCE */ (1u << 5) | (1u << 6),
        /* ABSENCE  */ (1u << 5) | (1u << 6),
    };
    unsigned p = (unsigned)proved;
    unsigned r = (unsigned)requested;
    return p < SPACE_OBSERVER_COUNT_KINDS &&
           r < SPACE_OBSERVER_COUNT_KINDS &&
           ((k_masks[p] & (1u << r)) != 0u);
}

/* A caller with an explicit finite fuel budget (`fuel >= 0`) observes exact
 * ticks and fuel cutoffs (`SPACE_OBSERVER_TIMED`), refusing macro-step
 * shortcuts sound only for coarser completed-run observations. */
static inline SpaceObserverKind space_observer_from_fuel(
        SpaceObserverKind base, int fuel) {
    return fuel >= 0 ? SPACE_OBSERVER_TIMED : base;
}

/* ── Two-Axis Validity Check (`SpaceRegionGuard`) ───────────────────────── */

/* Axis 1 is `SpaceObserverKind` (what the caller observes).
 * Axis 2 is `SpaceRegionFootprintKind` (which version counters must remain
 * unchanged across snapshots):
 *   - THEORY_ONLY: function definitions, type assignments, and equations of
 *     `theory` (survives data writes).
 *   - THEORY_AND_DATA: also depends on the data atoms of `data` (required for
 *     counted queries, bag lookups, first-witness, and absence checks; positive
 *     presence may survive append-only growth when dependency-free).
 *   - HEAD_SCOPED: validates `theory` and/or `data` per head symbol via
 *     `SpaceHeadClockStamp` so writes to other head symbols keep the guard valid.
 * Optional `discovery` records a space snapshot that resolved `data` itself. */
typedef enum SpaceRegionFootprintKind {
    SPACE_FOOTPRINT_THEORY_ONLY = 0,
    SPACE_FOOTPRINT_THEORY_AND_DATA = 1,
    SPACE_FOOTPRINT_HEAD_SCOPED = 2,
} SpaceRegionFootprintKind;

typedef struct SpaceRegionGuard {
    SpaceObserverKind proved_observer;
    SpaceRegionFootprintKind footprint;
    bool positive_witness_present;
    bool data_dependency_free;
    bool has_discovery;
    bool has_theory_head;
    bool has_data_head;
    SpaceTheoryToken theory;
    SpaceReadToken data;
    SpaceReadToken discovery;
    SpaceHeadClockStamp theory_head;
    SpaceHeadClockStamp data_head;
} SpaceRegionGuard;

static inline SpaceRegionGuard space_region_guard_for_theory(
        SpaceObserverKind proved_observer, const Space *theory_space) {
    return (SpaceRegionGuard){
        .proved_observer = proved_observer,
        .footprint = SPACE_FOOTPRINT_THEORY_ONLY,
        .positive_witness_present = false,
        .data_dependency_free = true,
        .has_discovery = false,
        .theory = space_theory_token(theory_space),
    };
}

static inline SpaceRegionGuard space_region_guard_for_data(
        SpaceObserverKind proved_observer, const Space *theory_space,
        const Space *data_space, bool positive_witness_present) {
    return (SpaceRegionGuard){
        .proved_observer = proved_observer,
        .footprint = SPACE_FOOTPRINT_THEORY_AND_DATA,
        .positive_witness_present = positive_witness_present,
        .data_dependency_free =
            data_space != NULL && space_dependency_count(data_space) == 0u,
        .has_discovery = false,
        .theory = space_theory_token(theory_space),
        .data = space_read_token(data_space),
    };
}

/* Capture a per-head validity guard.  When `theory_roles != 0`, `theory_space`
 * is stamped per head symbol (`theory_roles | ALIAS`); when `theory_roles == 0`,
 * the full `SpaceTheoryToken` of `theory_space` is required.  When
 * `data_space != NULL && data_roles != 0`, `data_space` is stamped per head
 * symbol (`data_roles | ALIAS`). */
SpaceRegionGuard space_region_guard_for_head(
    SpaceObserverKind proved_observer,
    Space *theory_space, uint8_t theory_roles,
    Space *data_space, uint8_t data_roles,
    SymbolId head, bool positive_witness_present);

static inline SpaceRegionGuard space_region_guard_with_discovery(
        SpaceRegionGuard guard, const Space *discovery_space) {
    guard.has_discovery = discovery_space != NULL;
    guard.discovery = discovery_space
        ? space_read_token(discovery_space)
        : (SpaceReadToken){0};
    return guard;
}

/* Check whether `guard` remains valid at a later snapshot `(live_theory,
 * live_data, live_discovery)` for a caller observing `requested`. */
static inline bool space_region_guard_valid(
        const SpaceRegionGuard *guard, SpaceObserverKind requested,
        const Space *live_theory, const Space *live_data,
        const Space *live_discovery) {
    if (!guard ||
        !space_observer_licenses(guard->proved_observer, requested)) {
        return false;
    }
    if (guard->has_discovery &&
        !space_read_token_matches_live_space(guard->discovery,
                                             live_discovery)) {
        return false;
    }
    if (guard->footprint == SPACE_FOOTPRINT_HEAD_SCOPED) {
        if (guard->has_theory_head) {
            if (!space_theory_head_clock_current(live_theory,
                                                 &guard->theory_head))
                return false;
        } else if (!space_theory_token_matches_live_space(guard->theory,
                                                          live_theory)) {
            return false;
        }
        if (guard->has_data_head) {
            if (space_theory_head_clock_current(live_data,
                                                &guard->data_head))
                return true;
            if (requested == SPACE_OBSERVER_PRESENCE &&
                guard->positive_witness_present &&
                guard->data_dependency_free &&
                live_data && guard->data.space == live_data &&
                space_read_token_prefix_intact(guard->data) &&
                guard->data_head.equational_revision ==
                    space_equational_revision(live_data)) {
                return true;
            }
            return false;
        }
        return true;
    }
    if (!space_theory_token_matches_live_space(guard->theory, live_theory))
        return false;
    if (guard->footprint == SPACE_FOOTPRINT_THEORY_AND_DATA) {
        if (space_read_token_matches_live_space(guard->data, live_data))
            return true;
        /* Positive monotone presence survives append-only growth on a
         * dependency-free space.  Count, bag, first-witness, ordered, timed,
         * and absence/negative presence all require the exact data revision. */
        if (requested == SPACE_OBSERVER_PRESENCE &&
            guard->positive_witness_present &&
            guard->data_dependency_free &&
            live_data && guard->data.space == live_data &&
            space_read_token_prefix_intact(guard->data)) {
            return true;
        }
        return false;
    }
    return true;
}

#endif /* CETTA_SPACE_THEORY_H */
