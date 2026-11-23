/* Include space.h first when space_theory.h is included directly, before
 * setting CETTA_SPACE_THEORY_H, so space.h's own #include "space_theory.h"
 * defines SpaceTheoryToken at the exact right point. */
#ifndef CETTA_SPACE_H
#include "space.h"
#endif

#ifndef CETTA_SPACE_THEORY_H
#define CETTA_SPACE_THEORY_H

/*
 * Live Space Theory View (`SpaceTheoryView`), Versioned Theory Token
 * (`SpaceTheoryToken`), Clock Guards, Observer Lattice, and Regional Admission.
 *
 * A live MeTTa Space induces an executable GSLT theory over the shared Atom
 * signature:
 *   - ordinary rows remain inspectable data terms,
 *   - `(= lhs rhs)` / `(=% lhs rhs)` occurrences form its ordered rewrite rules,
 *   - `(: subject type)` / `(:< sub super)` occurrences form its declarations.
 *
 * Every dialect (HE, PeTTa, Prime) and regional fast realization reads and
 * validates this single theory projection via name-stable SymbolId keys and
 * the unified program-clock discipline (read clock before token; re-arm clock
 * only after confirming the token is still current).
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

static inline bool space_theory_view_is_current(const SpaceTheoryView *view) {
    if (!view || !view->space)
        return false;
    if (space_theory_token_cacheable(view->token) &&
        view->program_clock == space_global_program_epoch())
        return true;
    return space_theory_token_is_current(view->token);
}

/* ── Name-Stable Head Hashing & Row Classification ──────────────────────── */

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

/* Partition of a live Space row into its theory projection role. */
typedef enum SpaceTheoryRowKind {
    SPACE_THEORY_ROW_DATA = 0,
    SPACE_THEORY_ROW_EQUATION,          /* (= lhs rhs) */
    SPACE_THEORY_ROW_DIRECTIONAL_RULE,  /* (=% lhs rhs) */
    SPACE_THEORY_ROW_TYPE_DECLARATION,  /* (: subject type) */
    SPACE_THEORY_ROW_SUBTYPE_DECLARATION, /* (:< sub super) */
    SPACE_THEORY_ROW_EQUATIONAL_ALIAS,  /* (=e a1 ... an), (=e> ...), (=e< ...) */
} SpaceTheoryRowKind;

/* The heads of alias rows. `(=e a1 ... an)` equates its members;
 * `(=e> a1 ... an)` also prefers each member's right neighbour, and
 * `(=e< a1 ... an)` its left neighbour. Each takes two or more members. */
static inline bool space_theory_is_alias_head(SymbolId head) {
    return head == g_builtin_syms.equals_e ||
           head == g_builtin_syms.equals_e_right ||
           head == g_builtin_syms.equals_e_left;
}

/* Classify one top-level Space row into its theory projection role.
 * When `head_out` is non-NULL, writes the exact target head SymbolId (for `=`
 * / `=%`, the symbol heading the LHS expression; for `:`, the symbol subject;
 * otherwise SYMBOL_ID_NONE).  When `compound_subject_out` is non-NULL, sets
 * true when a `:` declaration has a compound (expression) subject. */
SpaceTheoryRowKind space_theory_classify_row(
    const Atom *row, SymbolId *head_out, bool *compound_subject_out);

/* Validate an overlay chain's captured theory tokens (equation and declaration
 * revisions across every live overlay layer). */
bool space_theory_overlay_chain_tokens_current(
    const Space *space, const SpaceTheoryToken *views, size_t count);

/* ── Observer Coarsening Lattice (OccurrenceObservers.determines_iff) ───── */

/* Exact 7-demand lattice on completed runs (Mettapedia
 * GSLT.Contracts.OccurrenceObservers.Demand):
 *   TIMED    : exact completion ticks and every fuel-cutoff prefix
 *   ORDERED  : ordered answer list (preserving order & duplicates)
 *   BAG      : unordered answer multiset (preserving payload & multiplicity)
 *   COUNT    : total cardinality (Nat)
 *   FIRST    : first answer witness (Option A)
 *   PRESENCE : non-emptiness (Bool) on completed run
 *   ABSENCE  : emptiness (Bool) on completed run (Boolean dual of PRESENCE) */
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

/* Exact 49-pair classification matching OccurrenceObservers.covers:
 * whether a regional realization proved sound for `proved` automatically
 * licenses a caller observing `requested` on completed runs. */
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
 * accelerations proved only for coarser completed-run observations. */
static inline SpaceObserverKind space_observer_from_fuel(
        SpaceObserverKind base, int fuel) {
    return fuel >= 0 ? SPACE_OBSERVER_TIMED : base;
}

/* ── Two-Axis Regional Admission (LiveSpaceTheory.at_later_cut) ─────────── */

/* Axis 1 is `SpaceObserverKind` (what the caller observes).
 * Axis 2 is `SpaceRegionFootprintKind` (which read footprints must remain
 * valid across cuts):
 *   - THEORY_ONLY: equations and declarations of `theory` (survives data writes).
 *   - THEORY_AND_DATA: also depends on the data rows of `data` (required for
 *     counted regions, bag reads, first-witness, and absence tests; positive
 *     presence may survive append-only prefix growth when dependency-free).
 * Optional `discovery` records a space read that resolved `data` itself. */
typedef enum SpaceRegionFootprintKind {
    SPACE_FOOTPRINT_THEORY_ONLY = 0,
    SPACE_FOOTPRINT_THEORY_AND_DATA = 1,
} SpaceRegionFootprintKind;

typedef struct SpaceRegionAdmission {
    SpaceObserverKind proved_observer;
    SpaceRegionFootprintKind footprint;
    bool positive_witness_present;
    bool data_dependency_free;
    bool has_discovery;
    SpaceTheoryToken theory;
    SpaceReadToken data;
    SpaceReadToken discovery;
} SpaceRegionAdmission;

static inline SpaceRegionAdmission space_region_admission_for_theory(
        SpaceObserverKind proved_observer, const Space *theory_space) {
    return (SpaceRegionAdmission){
        .proved_observer = proved_observer,
        .footprint = SPACE_FOOTPRINT_THEORY_ONLY,
        .positive_witness_present = false,
        .data_dependency_free = true,
        .has_discovery = false,
        .theory = space_theory_token(theory_space),
    };
}

static inline SpaceRegionAdmission space_region_admission_for_data(
        SpaceObserverKind proved_observer, const Space *theory_space,
        const Space *data_space, bool positive_witness_present) {
    return (SpaceRegionAdmission){
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

static inline SpaceRegionAdmission space_region_admission_with_discovery(
        SpaceRegionAdmission admission, const Space *discovery_space) {
    admission.has_discovery = discovery_space != NULL;
    admission.discovery = discovery_space
        ? space_read_token(discovery_space)
        : (SpaceReadToken){0};
    return admission;
}

/* Validate a regional admission at a later cut `(live_theory, live_data,
 * live_discovery)` for a caller observing `requested`. */
static inline bool space_region_admission_valid(
        const SpaceRegionAdmission *admission, SpaceObserverKind requested,
        const Space *live_theory, const Space *live_data,
        const Space *live_discovery) {
    if (!admission ||
        !space_observer_licenses(admission->proved_observer, requested) ||
        !space_theory_token_matches_live_space(admission->theory, live_theory)) {
        return false;
    }
    if (admission->has_discovery &&
        !space_read_token_matches_live_space(admission->discovery,
                                             live_discovery)) {
        return false;
    }
    if (admission->footprint == SPACE_FOOTPRINT_THEORY_AND_DATA) {
        if (space_read_token_matches_live_space(admission->data, live_data))
            return true;
        /* Positive monotone presence survives append-only growth on a
         * dependency-free space (LiveSpaceTheory + DeclaredHornReading).
         * Count, bag, first-witness, ordered, timed, and absence/negative
         * presence all require the exact data revision. */
        if (requested == SPACE_OBSERVER_PRESENCE &&
            admission->positive_witness_present &&
            admission->data_dependency_free &&
            live_data && admission->data.space == live_data &&
            space_read_token_prefix_intact(admission->data)) {
            return true;
        }
        return false;
    }
    return true;
}

#endif /* CETTA_SPACE_THEORY_H */
